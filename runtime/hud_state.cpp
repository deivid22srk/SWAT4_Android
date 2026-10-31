/* Real game-state reading for the contextual HUD.
 *
 * The game is a 32-bit x86 process lifted into this one; every guest address
 * is an offset into the flat 4 GiB MEM region (runtime/x86.h). This module
 * reads the object array the engine itself maintains and derives the HUD
 * context from REAL game objects — no polling of input, no guessing.
 *
 * Evidence for every constant here lives in docs/HUD_PLAN.md §1:
 *   - GObjObjects (TArray<UObject*>)   Core.dll RVA 0x294354   (export)
 *   - ASwatPlayer::PrivateStaticClass  SwatGame.dll RVA 0xab848 (export)
 *   - UObject::Class at +0x24          (disasm of UObject::IsA)
 *   - AActor::Location at +0x174       (disasm of GetAimLocation x2)
 *
 * Pure logic: no SDL, no D3D, no allocation — the selftest compiles this
 * file against a synthetic guest image. The scan is throttled (default 1 s);
 * between scans a cached pawn is validated with two reads. */
#include "hud_state.h"

#include <string.h>

/* Guest layout constants (see docs/HUD_PLAN.md §1 for the evidence).
 * UOBJ_OUTER_OFF: disasm of UObject::GetPathName (Core.dll RVA 0x2d47,
 * body walks the chain with `mov 0x18(%edi),%ecx` and recurses on it),
 * the same function pins Name.Index at +0x20 and the Names table base.
 * Actors are Outered by their ULevel, whose own Outer is the map package
 * ("Entry" at the main menu — the boot log shows "Level is Level
 * Entry.myLevel"). */
#define CORE_BASE          0x01000000u
#define SWATGAME_BASE      0x03200000u
#define RVA_GOBJOBJECTS    0x294354u
#define RVA_CLS_SWATPLAYER 0xab848u
#define RVA_CLS_SWATDOOR   0xa6408u
#define UOBJ_CLASS_OFF     0x24u
#define UOBJ_OUTER_OFF     0x18u
#define UOBJ_NAME_OFF      0x20u
#define ACTOR_LOC_OFF      0x174u
/* Guest ranges from docs/RECON.md §3: module images and the guest heap.
 * MOD_* is documentation (the module .data slots we read are addressed
 * directly); every dynamic pointer is checked against HEAP_*. */
#define MOD_LO             0x01000000u
#define MOD_HI             0x03E00000u
/* Dynamic guest VA: heap 0x20000000-0x40000000 PLUS the VirtualAlloc pool
 * 0x40000000-0x60000000 (docs/RECON.md §3). UE2 serves UObject memory from
 * FMallocBinned, which pools via VirtualAlloc: on device every UClass /
 * pawn / door / GObjObjects payload lives in the SECOND band (field log:
 * UViewport 0x45E73200, VirtualAlloc pools 0x477e0000/0x47a60000). Accepting
 * only the first band made scan() bail to HUD_CTX_UNKNOWN forever on the
 * phone (log: "hud: ... ctx 0" at the main menu): the overlay never saw the
 * menu, everMenu never latched in Java and every free tap degraded to the
 * movie-skip ESC instead of the menu mouse — the dead HUD of field video. */
#define HEAP_LO            0x20000000u
#define HEAP_HI            0x60000000u
#define GOBJ_NUM_CAP       200000u  /* far above any real object count */
/* FName table: Core.dll RVA 0x28c2c0 (guest 0x0128C2C0, docs/HUD_PLAN.md §1)
 * holds FNameEntry** Names. Entries are heap pointers; the index is masked
 * to 12 bits by the engine's own HashObject, so a wider value is bogus. */
#define NAMES_BASE         (CORE_BASE + 0x28c2c0u)
#define NAMES_MAX          0x1000u

static const uint8_t *s_mem;
static uint32_t s_last_scan;
static uint32_t s_interval = 1000;
static int s_ctx = HUD_CTX_UNKNOWN;
static uint32_t s_pawn;
static uint32_t s_pawn_loc[3];
static uint32_t s_cls;              /* cached UClass* of ASwatPlayer */
static uint32_t s_cls_door;         /* cached UClass* of ASwatDoor */
static uint32_t s_gen;              /* scan generation (diagnostics) */
static int s_entry_pawn;            /* pawn lives in the Entry map package */

/* Doors are static geometry: positions are read once per scan into this
 * fixed table (no allocation). 32 covers SWAT 4 mission maps with margin;
 * doors beyond the cap are invisible to the HUD (honest limitation). */
#define HUD_MAXDOORS 32
static struct { uint32_t ptr, loc[3]; } s_doors[HUD_MAXDOORS];
static int s_ndoors;

static uint32_t r32(uint32_t a) {
    uint32_t v;
    memcpy(&v, s_mem + a, 4);
    return v;
}

static int in_heap(uint32_t a) { return a >= HEAP_LO && a < HEAP_HI; }

void hud_set_guest_mem(const void *mem) {
    s_mem = (const uint8_t *)mem;
}

int hud_game_ctx(void) { return s_ctx; }

uint32_t hud_scan_gen(void) { return s_gen; }

uint32_t hud_pawn(void) { return s_pawn; }

const uint32_t *hud_pawn_loc(void) { return s_pawn ? s_pawn_loc : NULL; }

/* Full rescan: walk the engine object array once, collecting the player
 * pawn and every ASwatDoor (both by exact class match). Anything unexpected
 * (core not initialized, heap pointer out of range) degrades to
 * HUD_CTX_UNKNOWN, never to a wrong positive. UClass objects are themselves
 * heap UObjects, so the PrivateStaticClass VALUE is heap-checked. */
static void read_loc(uint32_t obj, uint32_t out[3]) {
    memcpy(out, s_mem + obj + ACTOR_LOC_OFF, 12);
}

/* 1 when `obj`'s map package is named "Entry": the boot level behind the
 * main menu (log: "LoadMap: Entry", "Level is Level Entry.myLevel").
 * Actor -> ULevel -> UPackage via UObject::Outer at +0x18, then the package
 * FName at +0x20 indexes the Names table (base 0x38c2c0, pinned by the same
 * GetPathName disasm); the string sits at FNameEntry+0xC, ANSI or UTF-16.
 * Any read that leaves the heap degrades to 0 (treat as a real mission). */
static int in_entry_package(uint32_t obj) {
    uint32_t lvl = r32(obj + UOBJ_OUTER_OFF);
    if (!in_heap(lvl)) return 0;
    uint32_t pkg = r32(lvl + UOBJ_OUTER_OFF);
    if (!in_heap(pkg)) return 0;
    uint32_t fname = r32(pkg + UOBJ_NAME_OFF);
    if (fname >= NAMES_MAX) return 0;
    uint32_t entry = r32(NAMES_BASE + fname * 4);
    if (!in_heap(entry)) return 0;
    static const char name[6] = "Entry";
    for (int wide = 0; wide < 2; wide++) {
        int ok = 1;
        for (int i = 0; i < 6 && ok; i++) {
            uint8_t c = s_mem[entry + 0xC + i * (wide + 1)];
            ok = c == (uint8_t)name[i];
        }
        if (ok) return 1;
    }
    return 0;
}

static void scan(void) {
    s_pawn = 0;
    s_ndoors = 0;
    s_entry_pawn = 0;
    s_gen++;
    if (!s_mem) { s_ctx = HUD_CTX_UNKNOWN; return; }
    uint32_t cls = r32(SWATGAME_BASE + RVA_CLS_SWATPLAYER); /* module .data slot */
    uint32_t cls_door = r32(SWATGAME_BASE + RVA_CLS_SWATDOOR);
    if (!in_heap(cls)) { s_ctx = HUD_CTX_UNKNOWN; return; } /* not registered yet */
    uint32_t arr = CORE_BASE + RVA_GOBJOBJECTS;
    uint32_t data = r32(arr), num = r32(arr + 4);
    if (!num || num > GOBJ_NUM_CAP || !in_heap(data) ||
        data + num * 4 < data || data + num * 4 >= HEAP_HI) {
        s_ctx = HUD_CTX_UNKNOWN;
        return;
    }
    s_cls = cls;
    s_cls_door = in_heap(cls_door) ? cls_door : 0;
    for (uint32_t i = 0; i < num; i++) {
        uint32_t o = r32(data + i * 4);
        if (!in_heap(o)) continue;
        uint32_t oc = r32(o + UOBJ_CLASS_OFF);
        if (oc == cls && !s_pawn) {
            s_pawn = o;
            read_loc(o, s_pawn_loc);
        } else if (s_cls_door && oc == s_cls_door && s_ndoors < HUD_MAXDOORS &&
                   o + ACTOR_LOC_OFF + 12 < HEAP_HI) {
            s_doors[s_ndoors].ptr = o;
            read_loc(o, s_doors[s_ndoors].loc);
            s_ndoors++;
        }
    }
    /* A pawn in the boot Entry package is the menu backdrop, not a mission
     * (UE2 keeps that level loaded behind the GUI). Without this check the
     * HUD would claim a mission at the main menu whenever the engine leaves
     * that pawn in the array. */
    if (s_pawn && in_entry_package(s_pawn)) {
        s_entry_pawn = 1;
        s_pawn = 0;
    }
    s_ctx = s_pawn ? HUD_CTX_MISSION : HUD_CTX_MENU;
}

void hud_game_tick(uint32_t now) {
    if (!s_mem) return;
    /* Fast death check, one read: a pawn whose Class stopped matching
     * (object destroyed) is dropped at once. Removal from the array, the
     * other way a mission ends, is caught by the periodic rescan below —
     * the array is the truth, not the cache. The pawn Location is refreshed
     * every tick so door proximity tracks the player, not the last scan. */
    if (s_ctx == HUD_CTX_MISSION && s_pawn) {
        if (in_heap(s_pawn) && r32(s_pawn + UOBJ_CLASS_OFF) == s_cls) {
            if (s_pawn + ACTOR_LOC_OFF + 12 < HEAP_HI)
                read_loc(s_pawn, s_pawn_loc);
        } else {
            s_pawn = 0;
            s_last_scan = 0; /* rescan at once: at most once per flip */
        }
    }
    if (s_last_scan && now - s_last_scan < s_interval) return;
    s_last_scan = now;
    scan();
}

int hud_door_count(void) { return s_ndoors; }

/* 1 when the last scan dropped a pawn because it lived in the Entry package
 * (menu backdrop) — diagnostics only. */
int hud_pawn_entry(void) { return s_entry_pawn; }

/* Nearest cached door within `range` units of the pawn's CURRENT Location
 * (the game's own UseInterface door contexts use Range=128).
 * Returns 1 and the squared distance, or 0. O(32) per call, no allocation. */
int hud_door_near(float range, float *dist2_out) {
    if (s_ctx != HUD_CTX_MISSION || !s_pawn || !s_ndoors) return 0;
    const float *p = (const float *)s_pawn_loc;
    float r2 = range * range;
    float best = -1.0f;
    for (int i = 0; i < s_ndoors; i++) {
        const float *d = (const float *)s_doors[i].loc;
        float dx = d[0] - p[0], dy = d[1] - p[1], dz = d[2] - p[2];
        float dist2 = dx * dx + dy * dy + dz * dz;
        if (dist2 <= r2 && (best < 0.0f || dist2 < best)) best = dist2;
    }
    if (best < 0.0f) return 0;
    if (dist2_out) *dist2_out = best;
    return 1;
}

void hud_state_reset_for_tests(void) {
    s_mem = NULL;
    s_last_scan = 0;
    s_interval = 1000;
    s_ctx = HUD_CTX_UNKNOWN;
    s_pawn = 0;
    s_cls = 0;
    s_cls_door = 0;
    s_ndoors = 0;
    s_gen = 0;
    s_entry_pawn = 0;
    memset(s_pawn_loc, 0, sizeof s_pawn_loc);
    memset(s_doors, 0, sizeof s_doors);
}

void hud_set_scan_interval(uint32_t ms) { s_interval = ms; }

/* ------------------------------------------------------------------ */
/* Selftest: builds a synthetic guest image (MAP_NORESERVE, so the 4 GiB
 * flat space stays uncommitted until touched) and walks the real scan.
 * Built only by scripts/test_hud.sh with -DHUD_STATE_SELFTEST. */
#ifdef HUD_STATE_SELFTEST

#include <stdio.h>
#include <sys/mman.h>

static int failures;
#define CHECK(x) do { \
    if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } \
} while (0)

int main(void) {
    /* S1: no guest memory -> UNKNOWN (never guess) */
    hud_state_reset_for_tests();
    CHECK(hud_game_ctx() == HUD_CTX_UNKNOWN);
    hud_game_tick(0);
    CHECK(hud_game_ctx() == HUD_CTX_UNKNOWN);

    void *img = mmap(NULL, 0x40000000u, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    CHECK(img != MAP_FAILED);
    if (img == MAP_FAILED) return 1;
    uint8_t *m = (uint8_t *)img;
    auto w32 = [&](uint32_t a, uint32_t v) { memcpy(m + a, &v, 4); };

    const uint32_t GOBJ = 0x01000000u + 0x294354u;   /* Core.dll GObjObjects  */
    const uint32_t CLS  = 0x03200000u + 0xab848u;    /* ASwatPlayer::StaticClass slot */
    const uint32_t CLSD = 0x03200000u + 0xa6408u;    /* ASwatDoor::StaticClass slot   */
    const uint32_t NAMES = 0x01000000u + 0x28c2c0u;
    const uint32_t CLSOBJ = 0x2AAA1000, CLSDOBJ = 0x2AAA2000;
    const uint32_t PAWN = 0x2BBB2000, DOOR1 = 0x2BBB3000, DOOR2 = 0x2BBB4000;
    const uint32_t DATA = 0x2CCC3000, LVL = 0x2DDD4000, PKG = 0x2DDD5000;
    const uint32_t FENTRY = 0x2EEE6000;              /* FNameEntry for "Entry" */

    /* S2: class slot not registered yet -> UNKNOWN */
    hud_set_guest_mem(m);
    hud_set_scan_interval(1);
    hud_game_tick(0);
    CHECK(hud_game_ctx() == HUD_CTX_UNKNOWN);

    /* engine reality: UClass objects are heap objects; the array holds a
     * pawn and two doors */
    w32(CLS, CLSOBJ);
    w32(CLSD, CLSDOBJ);
    w32(CLSOBJ + 0x24, CLSOBJ);          /* a UClass points to itself */
    w32(CLSDOBJ + 0x24, CLSDOBJ);
    w32(PAWN + 0x24, CLSOBJ);
    w32(DOOR1 + 0x24, CLSDOBJ);
    w32(DOOR2 + 0x24, CLSDOBJ);
    const float pawn_loc[3] = {100.0f, 200.0f, 50.0f};
    const float door1_loc[3] = {110.0f, 210.0f, 50.0f};  /* ~14 units away */
    const float door2_loc[3] = {90000.0f, 0.0f, 0.0f};   /* far */
    memcpy(m + PAWN + 0x174, pawn_loc, 12);
    memcpy(m + DOOR1 + 0x174, door1_loc, 12);
    memcpy(m + DOOR2 + 0x174, door2_loc, 12);
    w32(GOBJ, DATA);
    w32(GOBJ + 4, 3);
    w32(DATA + 0, PAWN);
    w32(DATA + 4, DOOR1);
    w32(DATA + 8, DOOR2);

    /* pawn NOT in Entry (outer chain points to a package named something
     * else; the FName entry is not "Entry") -> MISSION */
    w32(PAWN + 0x18, LVL);
    w32(LVL + 0x18, PKG);
    w32(PKG + 0x20, 7);                  /* name index 7 */
    w32(NAMES + 7 * 4, FENTRY);
    memcpy(m + FENTRY + 0xC, "SwatGame", 9);
    hud_game_tick(2);
    CHECK(hud_game_ctx() == HUD_CTX_MISSION);
    CHECK(hud_pawn() == PAWN);
    CHECK(hud_door_count() == 2);
    float d2 = -1;
    CHECK(hud_door_near(128.0f, &d2) == 1);
    CHECK(d2 >= 0 && d2 < 128.0f * 128.0f);

    /* S3: the same pawn living in the boot "Entry" package is the menu
     * backdrop: MENU, pawn dropped, entry flag set */
    w32(NAMES + 7 * 4, FENTRY);
    memcpy(m + FENTRY + 0xC, "Entry\0\0", 8);
    hud_game_tick(4);
    CHECK(hud_game_ctx() == HUD_CTX_MENU);
    CHECK(hud_pawn() == 0);
    CHECK(hud_pawn_entry() == 1);

    /* S4: fast death check — the pawn's class stops matching (destroyed
     * object reuse): dropped on the next tick without waiting the scan.
     * The recycled object now matches the door class, so the rescan sees
     * live objects but no pawn: MENU (with the recycled one among doors). */
    memcpy(m + FENTRY + 0xC, "SwatGame", 9);
    hud_game_tick(6);
    CHECK(hud_game_ctx() == HUD_CTX_MISSION);
    w32(PAWN + 0x24, CLSDOBJ);           /* turned into a door?! object died */
    hud_game_tick(7);
    CHECK(hud_game_ctx() == HUD_CTX_MENU);
    CHECK(hud_pawn() == 0);
    CHECK(hud_door_count() == 3);

    /* S5: array payload out of the heap -> UNKNOWN, no crash */
    w32(PAWN + 0x24, CLSOBJ);
    w32(GOBJ, 0x7FFFFFFF);               /* not a heap pointer */
    hud_game_tick(8);
    CHECK(hud_game_ctx() == HUD_CTX_UNKNOWN);

    munmap(img, 0x40000000u);
    printf(failures ? "hud_state selftest: %d FAILURES\n" : "hud_state selftest: ok\n",
           failures);
    return failures ? 1 : 0;
}
#endif /* HUD_STATE_SELFTEST */
