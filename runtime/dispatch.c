/* Indirect call/jump dispatch, host<->guest call bridging, and traps. */
#include "rt.h"
#include "rt_table.h"
#include <string.h>
#include <time.h>

unsigned rt_module_count(unsigned m);

#define MAX_THUNKS 16384
static struct { ShimFn fn; const char *name; } g_thunks[MAX_THUNKS];
static uint32_t g_nthunks;
static pthread_mutex_t g_thunk_lock = PTHREAD_MUTEX_INITIALIZER;

uint32_t rt_thunk(ShimFn fn, const char *name) {
    pthread_mutex_lock(&g_thunk_lock);
    if (g_nthunks >= MAX_THUNKS) rt_fatal("out of thunks");
    uint32_t i = g_nthunks++;
    g_thunks[i].fn = fn;
    g_thunks[i].name = name;
    pthread_mutex_unlock(&g_thunk_lock);
    return GUEST_THUNK_BASE + i * 4;
}

const char *rt_thunk_name(uint32_t a) {
    if (a >= GUEST_THUNK_BASE && (a - GUEST_THUNK_BASE) / 4 < g_nthunks) return g_thunks[(a - GUEST_THUNK_BASE) / 4].name;
    return NULL;
}

/* Small direct-mapped cache in front of the sorted function table. */
#define CACHE_BITS 14
static struct { uint32_t addr; GuestFn fn; } g_cache[1 << CACHE_BITS];

static GuestFn lookup(uint32_t a) {
    unsigned h = (a ^ (a >> 13)) & ((1 << CACHE_BITS) - 1);
    if (g_cache[h].addr == a && g_cache[h].fn) return g_cache[h].fn;
    for (unsigned m = 0; m < g_nmodules; m++) {
        const FnEntry *t = g_modules[m].table;
        unsigned n = rt_module_count(m);
        if (!n || a < t[0].addr || a > t[n - 1].addr) continue;
        unsigned lo = 0, hi = n;
        while (lo < hi) {
            unsigned mid = (lo + hi) / 2;
            if (t[mid].addr < a) lo = mid + 1;
            else hi = mid;
        }
        if (lo < n && t[lo].addr == a) {
            g_cache[h].addr = a;
            g_cache[h].fn = t[lo].fn;
            return t[lo].fn;
        }
    }
    return NULL;
}

/* API trace with run-length compression: the idle/poll threads emit the same
 * call sites millions of times (the log reached 9M lines / 373 MB at log=2,
 * dominating boot time on FUSE storage). Calls whose site+stack were already
 * traced in the recent window are counted, not printed.
 *
 * 45log follow-up: the sound-database build alone makes ~1.5M API calls and
 * the whole boot ~44M; the old key path spent a snprintf plus a 64x strcmp
 * scan on EVERY call (x2 per shim: enter+leave), which at log=2 cost more
 * host CPU than the emulated game itself. Same window semantics, now cheap:
 * manual key build, 32-bit FNV-1a, hash-only window, single fwrite emit. */
#define T_HIST 64
static uint32_t t_hist[T_HIST];
static unsigned t_hn;
static uint32_t t_rep;
static uint64_t t_bytes;              /* trace bytes written this run */
static int t_over;                    /* byte cap hit: stop tracing, keep LOG() */

/* A verbose run (log=2) traces tens of millions of calls; on a phone that
 * once made a 609 MB log.txt in a single session. Cap the trace at ~96 MB
 * per run: regular LOG() lines (errors, milestones) keep flowing, only the
 * call trace goes quiet. */
#define T_BYTE_CAP (96ull << 20)

static char *t_emit_hex(char *p, uint32_t v) {
    static const char d[] = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) *p++ = d[(v >> (i * 4)) & 0xf];
    return p;
}

static uint32_t t_key_hash(int enter, const char *name, uint32_t esp, uint32_t ret) {
    char key[112], *p = key;
    *p++ = (char)('0' + (enter & 1));
    *p++ = ' ';
    while (*name && p < key + sizeof key - 24) *p++ = *name++;
    *p++ = ' ';
    p = t_emit_hex(p, esp);
    *p++ = ' ';
    p = t_emit_hex(p, ret);
    uint32_t h = 2166136261u;
    for (const char *q = key; q < p; q++) { h ^= (uint8_t)*q; h *= 16777619u; }
    return h ? h : 1; /* 0 marks an empty window slot */
}

static void trace_thunk(int enter, const char *name, uint32_t esp, uint32_t ret) {
    if (t_over) return;
    uint32_t h = t_key_hash(enter, name, esp, ret);
    for (unsigned i = 0; i < T_HIST; i++)
        if (t_hist[i] == h) {
            t_rep++;
            if (t_rep % 20000 == 0) {
                char b[64];
                int n = snprintf(b, sizeof b, "   (repeated calls x%u)\n", t_rep);
                fwrite(b, 1, n, stderr);
                t_bytes += (uint64_t)n;
            }
            return;
        }
    if (t_rep) {
        char b[32];
        int n = snprintf(b, sizeof b, "   (x%u)\n", t_rep);
        fwrite(b, 1, n, stderr);
        t_bytes += (uint64_t)n;
        t_rep = 0;
    }
    t_hist[t_hn] = h;
    t_hn = (t_hn + 1) & (T_HIST - 1);
    /* "-> name esp=xxxxxxxx ret=xxxxxxxx": thunk names are static strings far
     * below the 128-byte headroom left after the fixed parts. */
    char line[160], *p = line;
    *p++ = enter ? '-' : '<';
    *p++ = '>';
    *p++ = ' ';
    const char *q = name;
    while (*q && p < line + sizeof line - 32) *p++ = *q++;
    memcpy(p, " esp=", 5); p += 5;
    p = t_emit_hex(p, esp);
    if (enter) {
        memcpy(p, " ret=", 5); p += 5;
        p = t_emit_hex(p, ret);
    }
    *p++ = '\n';
    fwrite(line, 1, p - line, stderr);
    t_bytes += (uint64_t)(p - line);
    if (t_bytes > T_BYTE_CAP) {
        t_over = 1;
        t_rep = 0;
        LOG("trace: byte cap reached (%llu MB), call tracing off for this run",
            (unsigned long long)(t_bytes >> 20));
    }
}

void rt_call(CPU *c, uint32_t t) {
    if (t >= GUEST_THUNK_BASE) {
        uint32_t i = (t - GUEST_THUNK_BASE) / 4;
        if (i < g_nthunks) {
            if (g_log_level > 1) trace_thunk(1, g_thunks[i].name, c->r[4], M32(c->r[4]));
            if (!g_thunks[i].fn)
                rt_fatal("unimplemented import %s (called from %08x)", g_thunks[i].name, M32(c->r[4]));
            g_thunks[i].fn(c);
            if (g_log_level > 1) trace_thunk(0, g_thunks[i].name, c->r[4], 0);
            return;
        }
    }
    GuestFn fn = lookup(t);
    if (!fn) {
        uint32_t ret = M32(c->r[4]), esp = c->r[4];
        uint32_t a0 = M32(esp + 4);
        fprintf(stderr, "bad call: esp=%08x args %08x %08x %08x %08x\n", esp, a0, M32(esp + 8), M32(esp + 12), M32(esp + 16));
        if (a0 >= 0x10000) {
            uint32_t vt = M32(a0);
            fprintf(stderr, "  [arg0] = %08x %08x %08x %08x\n", vt, M32(a0 + 4), M32(a0 + 8), M32(a0 + 12));
            if (vt >= 0x10000) fprintf(stderr, "  vtbl: %08x %08x %08x ... [19]=%08x\n", M32(vt), M32(vt + 4), M32(vt + 8), M32(vt + 0x4c));
        }
        /* njdjport: the Havok .hke importer dispatches creators through
         * [table + (row*32+col)*4 + 0x18c] (row/col from two virtual calls).
         * When the loaded function pointer is tiny, dump the table window it
         * came from so the registry state is visible in the log. r1=ecx held
         * M32(P) (the table), r6=esi the combined index at the call site. */
        static uint32_t badcall_dumps;
        if (t < 0x10000 && badcall_dumps < 8) {
            badcall_dumps++;
            uint32_t tab = c->r[1], idx = c->r[6];
            fprintf(stderr, "  regs eax=%08x ecx=%08x edx=%08x ebx=%08x ebp=%08x esi=%08x edi=%08x\n",
                    c->r[0], c->r[1], c->r[2], c->r[3], c->r[5], c->r[6], c->r[7]);
            if (tab > 0x10000 && tab < 0x40000000u) {
                fprintf(stderr, "  table %08x: window around [esi*4+0x18c] (idx=%08x):\n", tab, idx);
                for (int k = -4; k <= 11; k++) {
                    int64_t a = (int64_t)tab + 0x18c + ((int64_t)idx << 2) + 4 * k;
                    if (a >= 0x10000 && a < 0x40000000u)
                        fprintf(stderr, "    %s[%+d]: %08x\n", k == 0 ? "target" : "      ", k, M32((uint32_t)a));
                }
            }
        }
        rt_fatal("no recompiled code at %08x (return address %08x)", t, ret);
    }
    fn(c);
}

uint32_t rt_invoke(CPU *c, uint32_t fn, int nargs, const uint32_t *args) {
    uint32_t saved_esp = c->r[4];
    for (int i = nargs - 1; i >= 0; i--) {
        c->r[4] -= 4;
        M32(c->r[4]) = args[i];
    }
    c->r[4] -= 4;
    M32(c->r[4]) = 0xFEEDF00Du; /* fake return address */
    rt_call(c, fn);
    /* works for both stdcall (callee popped) and cdecl (we restore) */
    c->r[4] = saved_esp;
    return c->r[0];
}

void rt_unimpl(CPU *c, uint32_t addr, const char *what) {
    rt_fatal("unimplemented instruction '%s' at %08x", what, addr);
}

void rt_int3(CPU *c, uint32_t addr) {
    rt_fatal("int3 at %08x", addr);
}

void rt_cpuid(CPU *c, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    uint32_t leaf = *eax;
    if (leaf == 0) {
        *eax = 1;
        memcpy(ebx, "Genu", 4);
        memcpy(edx, "ineI", 4);
        memcpy(ecx, "ntel", 4);
    } else if (leaf == 1) {
        *eax = 0x00000F29; /* a Pentium 4 */
        *ebx = 0;
        *ecx = 0;
        *edx = (1u << 0) | (1u << 4) | (1u << 15) | (1u << 23) | (1u << 25) | (1u << 26); /* fpu tsc cmov mmx sse sse2 */
    } else {
        *eax = *ebx = *ecx = *edx = 0;
    }
}

uint64_t rt_rdtsc(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    /* UE2 estimates the CPU speed with rdtsc/QPC and picks RenderDetail from
     * it. A 1 GHz result made it select detail 0 and give up; report a 2005
     * desktop-class ~3 GHz instead. */
    return ((uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec) * 3;
}
