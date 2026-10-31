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

/* test hooks (selftest only) */
void hud_state_reset_for_tests(void);
void hud_set_scan_interval(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* HUD_STATE_H */
