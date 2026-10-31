/* Virtual input synthesis: the touch HUD (now a Java View in the Android
 * activity) drives the game through this layer instead of a real
 * mouse/keyboard. The DirectInput shim (dinput.c) ORs this state into the
 * device data the game polls, and key taps additionally post WM_KEYDOWN/WM_KEYUP
 * through user32_key_event, mirroring what platform_sdl.c does for real SDL keys.
 *
 * extern "C" matters here: vinput.c is built by the C compiler in the real
 * builds (runtime/*.c glob) while hud_state.cpp/hud_jni callers may be C++, and
 * the hud selftests link the same two ABIs together.
 *
 * Single-thread contract: producers (the hud_jni ring drain and the state
 * scan, both on the game's main thread via platform_present_hook) and
 * consumers (the game's DirectInput polls, same thread) never run
 * concurrently on this side; the Java UI thread only pushes into the
 * lock-free SPSC ring and never touches vinput directly. */
#ifndef VINPUT_H
#define VINPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Relative mouse movement accumulated since the last take, in pixels. */
void vinput_mouse_delta(int dx, int dy);
/* Hands over (and zeroes) the accumulated mouse deltas. */
void vinput_mouse_take(int *dx, int *dy);

/* Mouse button 0/1/2 (left/right/middle) state. */
void vinput_mouse_button(int btn0a2, int down);
/* Bitmask of held mouse buttons: bit0=left, bit1=right, bit2=middle. */
uint32_t vinput_mouse_buttons(void);

/* DirectInput (set-1 make) code keyboard state, state only: merged into the
 * immediate/buffered device data, no window messages. Used for held keys
 * (movement, sprint, lean) where the game polls the device state. */
void vinput_key(uint8_t dik, int down);
const uint8_t *vinput_keys(void);

/* Like vinput_key, but also posts WM_KEYDOWN/WM_KEYUP (dik -> VK table
 * below). Used for discrete actions (use, reload, ESC, slot numbers, ...)
 * that the game may handle through its window procedure. */
void vinput_key_tap(uint8_t dik, int down);

/* Diagnostics: number of key state transitions seen (0->1 or 1->0). The
 * overlay selftest uses it to assert that repeated stick motion inside the
 * same sector does not re-send edges. */
uint32_t vinput_key_edges(void);

/* Fast check: any virtual key currently held (keyboard only). Lets callers
 * skip a merge copy when the overlay is idle. */
int vinput_active(void);

/* ---- menu mouse (HUD hidden: the game UI gets real mouse input) ----
 * SWAT 4's menus are Windows-mouse driven (the GUI cursor sits parked at
 * 0,0 on touch builds because GetCursorPos never moved and no WM_MOUSEMOVE
 * is ever posted). While the overlay is hidden it feeds finger positions
 * here; vinput tracks the virtual cursor and posts WM_MOUSEMOVE plus
 * WM_LBUTTONDOWN/WM_LBUTTONUP through user32_mouse_msg, and GetCursorPos
 * reports the same cursor. Coordinates are window client pixels. */
/* Cursor moved to (x,y): posts WM_MOUSEMOVE only when the integer position
 * changed (finger events stream far faster than the game consumes). */
void vinput_menu_move(int x, int y);
/* Left-button edge: 1 = press (WM_LBUTTONDOWN), 0 = release (WM_LBUTTONUP).
 * Duplicate edges are ignored, so several fingers can share the state. */
void vinput_menu_button(int down);
/* Current virtual cursor for the GetCursorPos shim; 0,0 before first use. */
void vinput_cursor(int *x, int *y);

#ifdef __cplusplus
}
#endif

#endif
