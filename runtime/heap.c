/* Guest heap (HeapAlloc/malloc) and VirtualAlloc region allocator.
 *
 * Heap: boundary-tag allocator with segregated free lists, living in
 * [GUEST_HEAP_BASE, GUEST_HEAP_END). Block layout (all guest memory):
 *   +0 prev block size | free bit (bit0 of +4)
 *   +4 this block size | flags
 *   +8 payload (16-byte aligned); free blocks store next/prev links here
 *
 * Hardened after the bnlog crash (SIGSEGV in gmalloc at MEM+9, i.e. the free
 * walk dereferenced a tiny guest address): guest code that writes through a
 * stale lock pointer or overruns a buffer corrupts free-list links or block
 * headers. Instead of crashing, the allocator now
 *   - validates every pointer/link it touches (range, alignment, size);
 *   - keeps a ring of recent alloc/free events and dumps it on corruption;
 *   - rebuilds the free lists from the boundary-tag chain, quarantining the
 *     memory after the break point (live blocks there keep working, we just
 *     never hand that memory out again);
 *   - drops (and logs) frees of pointers that are not valid heap blocks.
 *
 * lahlog follow-up: the first quarantine build used QUARANTINED(p) =
 * p >= poison_min. The rebuild walk had wrapped (a smashed size field sent it
 * below the heap base), so poison_min was under the heap and EVERY block -
 * including all future bump allocations - counted as quarantined; every free
 * was dropped and the heap walked off the end ("out of guest memory for a
 * 1579392 byte lock" ~120k log lines later). Quarantine is now the closed
 * range [poison_min, poison_max) (poison_max = heap top at rebuild time) and
 * the walk can no longer wrap. Large game-writable buffers (lock shadows,
 * sound data, BC levels) moved out of this heap entirely: they are backed by
 * VirtualAlloc regions (gbigalloc), whose bookkeeping lives on the host, so a
 * stray write past their end cannot reach allocator metadata at all.
 */
#include "rt.h"
#include <string.h>
#include <sys/mman.h>

#define HDR 8u
#define MINBLK 32u
#define NBINS 48
#define F_USED 1u
#define F_PADDED 2u

/* ---- overrun containment (njdjport crash) ----
 * During the SP-FoodWall (career) load the guest overran an 8328-byte
 * allocation with parsed Havok float data, smashing the next block header
 * (20848d18) and a live C++ object; the hardening quarantine kept the
 * allocator alive but the corrupted object still died on a virtual call
 * ("no recompiled code at 00000010"). Windows-style allocator slack absorbs
 * exactly this class of small overrun, so every allocation of 512+ bytes now
 * carries a tail: 0xA5-filled slack up to a 64-byte block rounding plus a
 * 16-byte record {magic, requested n, magic, magic} at the very end. The
 * overrun lands in the slack (game survives), gsize/HeapSize still report the
 * requested size, and gfree verifies the record and reports the overrun with
 * the block identity so the writer can be pinpointed from the log. */
#define PAD_MIN 512u
#define PAD_ALIGN 64u
#define TAIL 16u
#define TAIL_MAGIC 0xA5A5A5A5u
#define TAIL_N(p) (M32((p) + 4))

static pthread_mutex_t g_heap_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_top = GUEST_HEAP_BASE + 8; /* first block header; payload 16-aligned */
static uint32_t g_last_block = 0;
static uint32_t g_bins[NBINS];

/* ---- diagnostics ----
 * All block headers start at addresses ≡ 8 (mod 16): the heap starts at
 * GUEST_HEAP_BASE+8 and every block size is a multiple of 16. Payloads are
 * therefore 16-aligned and a payload pointer p satisfies (p & 15) == 0. */
#define HEAP_RING 64 /* power of two */
typedef struct { uint32_t addr, size; const char *tag; uint8_t isfree; } HeapEv;
static HeapEv g_ring[HEAP_RING];
static uint32_t g_ring_i;
static __thread const char *t_tag;
static uint32_t g_poison_min = 0xFFFFFFFFu; /* first quarantined byte (guest addr) */
static uint32_t g_poison_max;               /* heap top at the last rebuild */
static uint32_t g_corrupt_events;

void gmalloc_set_tag(const char *tag) { t_tag = tag; }

#define BSIZE(b) (M32((b) + 4) & ~15u)
#define BUSED(b) (M32((b) + 4) & F_USED)
#define BPADDED(b) (M32((b) + 4) & F_PADDED)
#define BPREV(b) (M32(b))

/* Tail record sits in the last 16 bytes of a padded block's payload area. */
static inline uint32_t tail_addr(uint32_t b) { return b + BSIZE(b) - TAIL; }

static void pad_write_tail(uint32_t b, uint32_t n) {
    uint32_t t = tail_addr(b);
    uint32_t fill_start = b + HDR + n, fill_end = t;
    if (fill_end > fill_start) memset(MEM + fill_start, 0xA5, fill_end - fill_start);
    M32(t) = TAIL_MAGIC; TAIL_N(t) = n; M32(t + 8) = TAIL_MAGIC; M32(t + 12) = TAIL_MAGIC;
}

/* Returns bytes the guest wrote past its requested size: the distance from the
 * first non-0xA5 byte in the slack to the tail record (an extent estimate),
 * 0 if the slack is intact, or 0xFFFFFFFF when the record itself is smashed. */
static uint32_t pad_check(uint32_t b, uint32_t *req) {
    uint32_t t = tail_addr(b);
    if (M32(t) != TAIL_MAGIC || M32(t + 8) != TAIL_MAGIC || M32(t + 12) != TAIL_MAGIC) {
        *req = TAIL_N(t); return 0xFFFFFFFFu; /* record itself smashed: big overrun */
    }
    *req = TAIL_N(t);
    if (*req > BSIZE(b) - HDR - TAIL) return 0xFFFFFFFFu; /* insane req: distrust all */
    uint32_t p = b + HDR + *req;
    while (p < t && M8(p) == 0xA5) p++;
    return p < t ? t - p : 0;
}
#define NEXTF(b) M32((b) + 8)
#define PREVF(b) M32((b) + 12)

static void link_free(uint32_t b);

static int bin_of(uint32_t sz) {
    /* 16-byte classes up to 512, then logarithmic */
    if (sz <= 512) return (int)(sz >> 4);           /* 0..32 */
    int l = 31 - __builtin_clz(sz);                 /* 9.. */
    int b = 33 + (l - 9);
    return b < NBINS ? b : NBINS - 1;
}

static void ring_note(uint32_t addr, uint32_t size, int isfree) {
    HeapEv *e = &g_ring[g_ring_i];
    e->addr = addr; e->size = size; e->tag = t_tag; e->isfree = (uint8_t)isfree;
    g_ring_i = (g_ring_i + 1) & (HEAP_RING - 1);
}

static void ring_dump(uint32_t near_addr) {
    LOG("heap: last guest-heap events (most recent first, near %08x):", near_addr);
    for (uint32_t k = 0; k < HEAP_RING; k++) {
        uint32_t i = (g_ring_i + HEAP_RING - 1 - k) & (HEAP_RING - 1);
        if (!g_ring[i].addr) continue;
        LOG("heap:   %s %08x size=%u tag=%s", g_ring[i].isfree ? "free " : "alloc",
            g_ring[i].addr, g_ring[i].size, g_ring[i].tag ? g_ring[i].tag : "-");
    }
}

/* Report a corruption event; full details + ring only for the first few. */
static void heap_corrupt(const char *what, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t hit = __sync_fetch_and_add(&g_corrupt_events, 1u);
    if (hit < 8 || hit % 500 == 0)
        LOG("heap: CORRUPTION #%u: %s (a=%08x b=%08x c=%08x)", hit + 1, what, a, b, c);
    if (hit < 8) ring_dump(a);
}

static int block_sane(uint32_t b) {
    if (b < GUEST_HEAP_BASE + 8 || b >= g_top) return 0;
    if ((b & 15) != 8) return 0;
    uint32_t sz = BSIZE(b);
    if (sz < MINBLK || (sz & 15)) return 0;
    if ((uint64_t)b + sz > g_top) return 0; /* 64-bit: a smashed size must wrap-detect, not wrap-pass */
    return 1;
}

/* A free-list link must point at a plausible block start (or be null). */
static int link_sane(uint32_t p) {
    return p == 0 || ((p & 15) == 8 && p >= GUEST_HEAP_BASE + 8 && p < g_top);
}

/* Rebuild the segregated free lists by walking the boundary-tag chain.
 * Everything from the first bad header up to the current heap top is
 * quarantined: never relinked, never handed out again, but left untouched so
 * live allocations there keep working. Blocks allocated after the rebuild
 * (at/above poison_max) are ordinary and free normally. */
static void heap_rebuild(void) {
    memset(g_bins, 0, sizeof g_bins);
    g_last_block = 0;
    uint32_t b = GUEST_HEAP_BASE + 8;
    int freed = 0;
    while (b < g_top) {
        uint32_t sz;
        /* 64-bit step check: a smashed size field used to wrap b below the
         * heap base, which quarantined the whole heap including every future
         * allocation. */
        if (!block_sane(b) || (sz = BSIZE(b), (uint64_t)b + sz > g_top)) {
            if (g_poison_min == 0xFFFFFFFFu || b < g_poison_min) g_poison_min = b;
            if (g_top > g_poison_max) g_poison_max = g_top;
            LOG("heap: block chain broken at %08x (hdr %08x %08x links %08x %08x); "
                "memory [%08x..%08x) quarantined (leaked but still usable)",
                b, M32(b), M32(b + 4), M32(b + 8), M32(b + 12), g_poison_min, g_poison_max);
            for (uint32_t a = (b > GUEST_HEAP_BASE + 32 ? b - 32 : GUEST_HEAP_BASE);
                 a + 16 <= g_top && a < b + 48; a += 16)
                LOG("heap:   %08x: %08x %08x %08x %08x", a, M32(a), M32(a + 4), M32(a + 8), M32(a + 12));
            break;
        }
        if (!BUSED(b)) { link_free(b); freed++; }
        b += BSIZE(b);
    }
    LOG("heap: free lists rebuilt from boundary tags, %d free blocks relinked "
        "(quarantine %08x..%08x, top now %08x)", freed, g_poison_min, g_poison_max, g_top);
}

/* Blocks inside the closed quarantine range stay alive for the game but are
 * never handed out again; frees of them are dropped. Blocks above poison_max
 * (allocated after the rebuild) are NOT quarantined. */
#define QUARANTINED(p) (g_poison_max != 0 && (p) >= g_poison_min && (p) < g_poison_max)

static void unlink_free(uint32_t b) {
    uint32_t n = NEXTF(b), p = PREVF(b);
    if (!link_sane(p)) p = 0;
    if (!link_sane(n)) n = 0;
    if (p) NEXTF(p) = n;
    else g_bins[bin_of(BSIZE(b))] = n;
    if (n) PREVF(n) = p;
}

static void link_free(uint32_t b) {
    int k = bin_of(BSIZE(b));
    NEXTF(b) = g_bins[k];
    PREVF(b) = 0;
    if (g_bins[k]) PREVF(g_bins[k]) = b;
    g_bins[k] = b;
}

static void set_block(uint32_t b, uint32_t size, int used) {
    M32(b + 4) = size | (used ? F_USED : 0);
    uint32_t nx = b + size;
    if (nx < g_top) M32(nx) = size;
}

uint32_t gmalloc(uint32_t n) {
    uint32_t need = (n + HDR + 15) & ~15u;
    if (need < MINBLK) need = MINBLK;
    if (n >= PAD_MIN) need = ((need + PAD_ALIGN - 1) & ~(PAD_ALIGN - 1)) + TAIL;
    if (n > 0x7FFFFFFF) return 0;
    pthread_mutex_lock(&g_heap_lock);
    int rebuilt = 0;
restart:
    for (int k = bin_of(need); k < NBINS; k++) {
        uint32_t steps = 0;
        uint32_t b = g_bins[k];
        while (b) {
            if (++steps > (20u << 20)) { /* cycle/size guard */
                heap_corrupt("free-list walk stuck", b, k, steps);
                if (!rebuilt) { rebuilt = 1; heap_rebuild(); goto restart; }
                break;
            }
            if (!block_sane(b) || !link_sane(NEXTF(b)) || !link_sane(PREVF(b))) {
                heap_corrupt("free-list entry", b, k, M32(b + 4));
                if (!rebuilt) { rebuilt = 1; heap_rebuild(); goto restart; }
                break;
            }
            uint32_t sz = BSIZE(b);
            if (sz >= need) {
                unlink_free(b);
                if (sz - need >= MINBLK) {
                    set_block(b, need, 1);
                    uint32_t r = b + need;
                    M32(r) = need;
                    set_block(r, sz - need, 0);
                    if (g_last_block == b) g_last_block = r;
                    link_free(r);
                } else {
                    set_block(b, sz, 1);
                }
                if (n >= PAD_MIN) { M32(b + 4) |= F_PADDED; pad_write_tail(b, n); }
                uint32_t ret = b + HDR;
                ring_note(ret, n, 0);
                pthread_mutex_unlock(&g_heap_lock);
                return ret;
            }
            b = NEXTF(b);
        }
    }
    if (g_top + need > GUEST_HEAP_END) {
        pthread_mutex_unlock(&g_heap_lock);
        LOG("guest heap exhausted (%u bytes requested)", n);
        return 0;
    }
    uint32_t b = g_top;
    M32(b) = g_last_block ? BSIZE(g_last_block) : 0;
    g_top += need;
    set_block(b, need, 1);
    g_last_block = b;
    if (n >= PAD_MIN) { M32(b + 4) |= F_PADDED; pad_write_tail(b, n); }
    uint32_t ret = b + HDR;
    ring_note(ret, n, 0);
    pthread_mutex_unlock(&g_heap_lock);
    return ret;
}

void gfree(uint32_t p) {
    if (!p) return;
    pthread_mutex_lock(&g_heap_lock);
    uint32_t b = p - HDR;
    if (p < GUEST_HEAP_BASE + 16 || p >= g_top || (p & 15) || !block_sane(b)) {
        heap_corrupt("bad free pointer", p, 0, 0);
        pthread_mutex_unlock(&g_heap_lock);
        return;
    }
    if (QUARANTINED(b)) {
        LOG("heap: free of quarantined block %08x dropped", p);
        pthread_mutex_unlock(&g_heap_lock);
        return;
    }
    if (!BUSED(b)) {
        heap_corrupt("double free", p, 0, 0);
        pthread_mutex_unlock(&g_heap_lock);
        return;
    }
    if (BPADDED(b)) {
        uint32_t req, over = pad_check(b, &req);
        static uint32_t overrun_reports;
        if (over == 0xFFFFFFFFu) {
            if (++overrun_reports <= 8) {
                LOG("heap: OVERRUN block %08x (req %u): tail record smashed - guest "
                    "wrote far past the end; block leaked, heap unaffected", b, req);
                ring_dump(b);
            }
            pthread_mutex_unlock(&g_heap_lock);
            return; /* true extent unknown: leak instead of corrupting the chain */
        } else if (over && ++overrun_reports <= 8) {
            LOG("heap: OVERRUN block %08x req=%u: guest wrote at least %u bytes past "
                "its requested size (contained in slack, no heap damage)", b, req, over);
            ring_dump(b);
        }
    }
    uint32_t sz = BSIZE(b);
    /* merge with next */
    uint32_t nx = b + sz;
    if (nx < g_top) {
        if (!block_sane(nx))
            heap_corrupt("next-block header on free", nx, b, M32(nx + 4));
        else if (!BUSED(nx)) {
            unlink_free(nx);
            if (g_last_block == nx) g_last_block = b;
            sz += BSIZE(nx);
        }
    }
    /* merge with previous */
    uint32_t prevsz = BPREV(b);
    if (b > GUEST_HEAP_BASE + 8 && prevsz) {
        uint32_t pv = b - prevsz;
        if (pv < GUEST_HEAP_BASE + 8 || (pv & 15) != 8 || pv >= b ||
            pv + BSIZE(pv) != b || BSIZE(pv) != prevsz) {
            heap_corrupt("prev-block tag on free", b, prevsz, 0);
        } else if (!BUSED(pv)) {
            unlink_free(pv);
            if (g_last_block == b) g_last_block = pv;
            sz += BSIZE(pv);
            b = pv;
        }
    }
    ring_note(p, sz, 1);
    if (b + sz == g_top && !QUARANTINED(b)) {
        /* give the tail back to the bump region; never shrink into or below
         * the quarantine, whose live blocks must stay untouched */
        g_top = b;
        g_last_block = 0;
        if (b > GUEST_HEAP_BASE + 8 && BPREV(b)) g_last_block = b - BPREV(b);
        if (g_last_block && QUARANTINED(g_last_block)) g_last_block = 0;
        pthread_mutex_unlock(&g_heap_lock);
        return;
    }
    set_block(b, sz, 0);
    link_free(b);
    pthread_mutex_unlock(&g_heap_lock);
}

uint32_t gsize(uint32_t p) {
    if (!p) return 0;
    pthread_mutex_lock(&g_heap_lock);
    uint32_t b = p - HDR;
    if (p < GUEST_HEAP_BASE + 16 || p >= g_top || (p & 15) || !block_sane(b)) {
        heap_corrupt("gsize bad pointer", p, 0, 0);
        pthread_mutex_unlock(&g_heap_lock);
        return 0;
    }
    uint32_t r;
    if (BPADDED(b)) {
        uint32_t t = tail_addr(b);
        if (M32(t) == TAIL_MAGIC && M32(t + 8) == TAIL_MAGIC && TAIL_N(t) <= BSIZE(b) - HDR - TAIL) r = TAIL_N(t);
        else { heap_corrupt("gsize padded tail", p, 0, 0); r = BSIZE(b) - HDR; }
    } else r = BSIZE(b) - HDR;
    pthread_mutex_unlock(&g_heap_lock);
    return r;
}

uint32_t grealloc(uint32_t p, uint32_t n) {
    if (!p) return gmalloc(n);
    uint32_t old = gsize(p);
    if (n <= old) return p;
    uint32_t q = gmalloc(n);
    if (!q) return 0;
    if (old) memcpy(MEM + q, MEM + p, old);
    gfree(p);
    return q;
}

uint32_t gstrdup(const char *s) {
    uint32_t n = (uint32_t)strlen(s) + 1;
    uint32_t p = gmalloc(n);
    memcpy(MEM + p, s, n);
    return p;
}

/* ---- VirtualAlloc: 64 KiB granularity, first-fit over a region list ---- */
#define MAX_REGIONS 4096
static struct { uint32_t base, size; } g_regions[MAX_REGIONS];
static int g_nregions;
static pthread_mutex_t g_va_lock = PTHREAD_MUTEX_INITIALIZER;

uint32_t gvalloc(uint32_t addr, uint32_t size) {
    size = (size + 0xFFFF) & ~0xFFFFu;
    pthread_mutex_lock(&g_va_lock);
    if (addr) {
        /* commit inside an existing reservation: already backed */
        for (int i = 0; i < g_nregions; i++)
            if (addr >= g_regions[i].base && addr < g_regions[i].base + g_regions[i].size) {
                pthread_mutex_unlock(&g_va_lock);
                return addr;
            }
    }
    uint32_t cand = GUEST_VA_BASE;
    for (;;) {
        int clash = 0;
        for (int i = 0; i < g_nregions; i++) {
            uint32_t b = g_regions[i].base, e = b + g_regions[i].size;
            if (cand < e && cand + size > b) { cand = e; clash = 1; }
        }
        if (!clash) break;
    }
    if ((uint64_t)cand + size > GUEST_VA_END || g_nregions == MAX_REGIONS) {
        pthread_mutex_unlock(&g_va_lock);
        return 0;
    }
    g_regions[g_nregions].base = cand;
    g_regions[g_nregions].size = size;
    g_nregions++;
    pthread_mutex_unlock(&g_va_lock);
    return cand; /* pages are zero: never touched, or dropped by gvfree */
}

void gvfree(uint32_t addr) {
    pthread_mutex_lock(&g_va_lock);
    for (int i = 0; i < g_nregions; i++)
        if (g_regions[i].base == addr) {
            madvise(MEM + addr, g_regions[i].size, MADV_DONTNEED);
            g_regions[i] = g_regions[--g_nregions];
            break;
        }
    pthread_mutex_unlock(&g_va_lock);
}

/* ---- big game-writable buffers ----
 * D3D lock shadows, DirectSound data and BC levels are handed to the game as
 * raw pointers it writes with unknown discipline. Back anything reasonably
 * large with a VirtualAlloc region: the region list lives on the host, so a
 * write past the buffer end (observed: 49+ zero bytes past a 1.5 MiB tex-rect
 * shadow, lahlog) lands in zero pages, never in heap metadata. The 64 KiB
 * region rounding doubles as free overrun slack. Callers remember the va flag
 * and release with gbigfree. */
#define GUEST_BIG_MIN (256u * 1024u)
uint32_t gbigalloc(uint32_t n, int *va) {
    if (n >= GUEST_BIG_MIN) {
        uint32_t p = gvalloc(0, n);
        if (p) { if (va) *va = 1; return p; }
        LOG("gbigalloc: VA space exhausted for %u bytes, falling back to the heap", n);
    }
    if (va) *va = 0;
    return gmalloc(n);
}

void gbigfree(uint32_t p, int va) {
    if (!p) return;
    if (va) gvfree(p);
    else gfree(p);
}
