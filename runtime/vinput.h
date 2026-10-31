/* Virtual input synthesis: the touch overlay (touch_swat4.cpp) drives the
 * game through this layer instead of a real mouse/keyboard. The DirectInput
 * shim (dinput.c) ORs this state into the device data the game polls, and
 * key taps additionally post WM_KEYDOWN/WM_KEYUP through user32_key_event,
 * mirroring what platform_sdl.c does for real SDL keys.
 *
 * extern "C" matters here: vinput.c is built by the C compiler in the real
 * builds (runtime/*.c glob) while touch_swat4.cpp is C++, and the overlay
 * selftest links the same two ABIs together.
 *
 * Single-thread contract: producers (overlay event pump and overlay draw,
 * both on the main thread via platform_present_hook) and consumers (the
 * game's DirectInput polls, also on the main thread) never run concurrently,
 * so plain volatile accesses suffice; no locks, zero allocation. */
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

#ifdef __cplusplus
}
#endif

#endif
