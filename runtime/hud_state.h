/* Real game-state context for the touch HUD (see hud_state.cpp). */
#ifndef HUD_STATE_H
#define HUD_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HUD context derived from real game objects:
 *   UNKNOWN  guest memory not usable yet (boot, app-only build): keep the
 *            current overlay behaviour (visible), never guess.
 *   MENU     engine object array is live but there is no ASwatPlayer:
 *            menu / loading / debriefing — the HUD hides and lets touches
 *            through to the game UI.
 *   MISSION  a real ASwatPlayer object exists: the HUD is active. */
enum { HUD_CTX_UNKNOWN = 0, HUD_CTX_MENU = 1, HUD_CTX_MISSION = 2 };

void hud_set_guest_mem(const void *mem);
int  hud_game_ctx(void);
void hud_game_tick(uint32_t now_ms);
uint32_t hud_pawn(void);
const uint32_t *hud_pawn_loc(void);   /* 3 raw float bits: X Y Z, or NULL */

/* Diagnostics: bumped once per scan, lets the overlay log context changes
 * with the evidence behind them (pawn/doors/level) once per second at most. */
uint32_t hud_scan_gen(void);

/* Doors (stage 3): refreshed on each scan (1 s), positions are static. */
int hud_door_count(void);
/* 1 when the last scan dropped a pawn because it sat in the Entry package
 * (the boot level behind the main menu) — diagnostics only. */
int hud_pawn_entry(void);
/* 1 when a cached ASwatDoor sits within `range` game units of the pawn's
 * CURRENT Location; the game's own door contexts use Range=128. */
int hud_door_near(float range, float *dist2_out);

/* test hooks (selftest only) */
void hud_state_reset_for_tests(void);
void hud_set_scan_interval(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* HUD_STATE_H */
