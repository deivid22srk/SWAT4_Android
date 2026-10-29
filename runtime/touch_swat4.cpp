/* Touch overlay: not implemented yet.
 *
 * SWAT 4 is a tactical FPS with a squad-command menu; the planned mapping is in
 * README.md ("Controles"). This file satisfies the hooks the D3D9 bridge and the
 * event loop call and reports "no overlay", so nothing is drawn on top of the
 * game and the missing feature is visible in the code instead of pretending to
 * work. See docs/TESTED.md. */
#include "rt.h"

extern "C" int touch_available(void) { return 0; }
extern "C" void touch_draw(void *) {}
extern "C" void touch_handle_event(const void *) {}
extern "C" void touch_setup_guest(void) {}