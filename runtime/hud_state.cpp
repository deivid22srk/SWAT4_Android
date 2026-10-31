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

/* Guest layout constants (see docs/HUD_PLAN.md §1 for the evidence). */
#define CORE_BASE          0x01000000u
#define SWATGAME_BASE      0x03200000u
#define RVA_GOBJOBJECTS    0x294354u
#define RVA_CLS_SWATPLAYER 0xab848u
#define UOBJ_CLASS_OFF     0x24u
#define ACTOR_LOC_OFF      0x174u
/* Guest ranges from docs/RECON.md §3: module images and the guest heap.
 * MOD_* is documentation (the module .data slots we read are addressed
 * directly); every dynamic pointer is checked against HEAP_*. */
#define MOD_LO             0x01000000u
#define MOD_HI             0x03E00000u
#define HEAP_LO            0x20000000u
#define HEAP_HI            0x40000000u
#define GOBJ_NUM_CAP       200000u  /* far above any real object count */

static const uint8_t *s_mem;
static uint32_t s_last_scan;
static uint32_t s_interval = 1000;
static int s_ctx = HUD_CTX_UNKNOWN;
static uint32_t s_pawn;
static uint32_t s_pawn_loc[3];
static uint32_t s_cls;              /* cached UClass* of ASwatPlayer */

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

uint32_t hud_pawn(void) { return s_pawn; }

const uint32_t *hud_pawn_loc(void) { return s_pawn ? s_pawn_loc : NULL; }

/* Full rescan: walk the engine object array once for the player pawn.
 * Anything unexpected (core not initialized, heap pointer out of range)
 * degrades to HUD_CTX_UNKNOWN, never to a wrong positive. UClass objects are
 * themselves heap UObjects, so the PrivateStaticClass VALUE is heap-checked. */
static void scan(void) {
    s_pawn = 0;
    if (!s_mem) { s_ctx = HUD_CTX_UNKNOWN; return; }
    uint32_t cls = r32(SWATGAME_BASE + RVA_CLS_SWATPLAYER); /* module .data slot */
    if (!in_heap(cls)) { s_ctx = HUD_CTX_UNKNOWN; return; } /* not registered yet */
    uint32_t arr = CORE_BASE + RVA_GOBJOBJECTS;
    uint32_t data = r32(arr), num = r32(arr + 4);
    if (!num || num > GOBJ_NUM_CAP || !in_heap(data) ||
        data + num * 4 < data || data + num * 4 >= HEAP_HI) {
        s_ctx = HUD_CTX_UNKNOWN;
        return;
    }
    s_cls = cls;
    for (uint32_t i = 0; i < num; i++) {
        uint32_t o = r32(data + i * 4);
        if (!in_heap(o)) continue;
        if (r32(o + UOBJ_CLASS_OFF) == cls) { s_pawn = o; break; }
    }
    if (s_pawn && s_pawn + ACTOR_LOC_OFF + 12 < HEAP_HI)
        memcpy(s_pawn_loc, s_mem + s_pawn + ACTOR_LOC_OFF, 12);
    s_ctx = s_pawn ? HUD_CTX_MISSION : HUD_CTX_MENU;
}

void hud_game_tick(uint32_t now) {
    if (!s_mem) return;
    /* Fast death check, one read: a pawn whose Class stopped matching
     * (object destroyed) is dropped at once. Removal from the array, the
     * other way a mission ends, is caught by the periodic rescan below —
     * the array is the truth, not the cache. */
    if (s_ctx == HUD_CTX_MISSION && s_pawn &&
        (!in_heap(s_pawn) || r32(s_pawn + UOBJ_CLASS_OFF) != s_cls)) {
        s_pawn = 0;
        s_last_scan = 0; /* rescan at once: at most once per flip */
    }
    if (s_last_scan && now - s_last_scan < s_interval) return;
    s_last_scan = now;
    scan();
}

void hud_state_reset_for_tests(void) {
    s_mem = NULL;
    s_last_scan = 0;
    s_interval = 1000;
    s_ctx = HUD_CTX_UNKNOWN;
    s_pawn = 0;
    s_cls = 0;
    memset(s_pawn_loc, 0, sizeof s_pawn_loc);
}

void hud_set_scan_interval(uint32_t ms) { s_interval = ms; }
