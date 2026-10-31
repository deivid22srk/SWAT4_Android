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
#define HEAP_LO            0x20000000u
#define HEAP_HI            0x40000000u
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
