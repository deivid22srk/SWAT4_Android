/* Virtual input synthesis: see vinput.h for the contract. All state is
 * static (zero malloc); the overlay produces, the DirectInput shim consumes,
 * everything on the game's main thread. */
#include "rt.h"
#include "vinput.h"

void user32_key_event(uint32_t vk, uint32_t sc, int down);
/* Direct WM delivery into the guest WndProc (user32.c): SendMessage
 * semantics on the game thread. The movie loop never pumps the queue, so
 * keys that only sit there can never reach it. */
void user32_key_direct(uint32_t vk, uint32_t sc, int down);
/* Posts a raw mouse message into the game's queue (user32.c): msg =
 * WM_MOUSEMOVE/WM_LBUTTONDOWN/WM_LBUTTONUP, wp = MK_* flags, lp =
 * (y<<16)|x in client pixels. Implemented like user32_key_event. */
void user32_mouse_msg(uint32_t msg, uint32_t wp, uint32_t lp);

static volatile int32_t s_mx, s_my;       /* accumulated mouse deltas */
static volatile uint32_t s_btn;           /* bit0 left, bit1 right, bit2 middle */
static uint8_t s_keys[256];               /* DIK -> 0/1 */
static volatile uint32_t s_edges;         /* key transitions (diagnostics) */

/* ---- menu mouse state (see vinput.h; used by the button path too) ---- */
#define WM_MOUSEMOVE    0x0200u
#define WM_LBUTTONDOWN  0x0201u
#define WM_LBUTTONUP    0x0202u
#define MK_LBUTTON      0x0001u

static int s_cur_x, s_cur_y = -1;   /* last cursor position (y=-1: unset) */
static int s_cur_left;              /* synthesized left button state */

/* Closed-loop cursor steering: the engine's GUI cursor ONLY moves through
 * the DirectInput delta stream (windrv maps DIMOFS_X/Y -> IK_MouseX/Y;
 * WM_MOUSEMOVE never reaches the input layer — field log shhxlog: 31
 * WM_MOUSEMOVE + 31 WM clicks dispatched, menu cursor still frozen at
 * 0,0). s_eng_mx/s_eng_my models where the steering deltas put the
 * engine cursor. Premise: UE2's GUI cursor is moved only by the GUI
 * while menus are displayed; mission look does not move it — the model
 * therefore tracks steering only, and the first steer after a mission
 * re-steers from the last menu position. The field one-shot + periodic
 * cur/steers log (platform_present_hook) falsifies this if wrong.
 * Guest client pixels; game-thread only (see vinput.h). */
static int s_eng_mx, s_eng_my;      /* believed engine cursor position */
static uint32_t s_steers;           /* steers that pushed non-zero deltas */
static uint8_t s_steer_logged;      /* one-shot field diagnostic */

static void cur_post(uint32_t msg, uint32_t wp) {
    user32_mouse_msg(msg, wp, ((uint32_t)s_cur_y << 16) | ((uint32_t)s_cur_x & 0xFFFF));
}
static void menu_click_edge(int down); /* shared left-button edge (below) */

/* Push the engine cursor to (x,y) through the same DI delta stream the
 * look path uses — the only channel that moves it while menus are up.
 * Idempotent: the model lands exactly on (x,y), so repeated steers push
 * zero. */
static void menu_steer(int x, int y) {
    int dx = x - s_eng_mx, dy = y - s_eng_my;
    if (!dx && !dy) return;
    if (!s_steer_logged) {
        s_steer_logged = 1;
        /* lets the field log check the init (engine cursor starts at 0,0)
         * and gain (multiplier guarded to 1.0) assumptions */
        LOG("menu steer: first delta dx=%d dy=%d -> cur %d,%d", dx, dy, x, y);
    }
    s_steers++;
    vinput_mouse_delta(dx, dy);
    /* model updated at steer time, not on take/delta: the engine cursor
     * is believed parked at the last steered target across missions */
    s_eng_mx = x;
    s_eng_my = y;
}

void vinput_mouse_delta(int dx, int dy) {
    s_mx += dx;
    s_my += dy;
}

void vinput_mouse_take(int *dx, int *dy) {
    *dx = s_mx;
    *dy = s_my;
    s_mx = 0;
    s_my = 0;
}

void vinput_mouse_button(int btn0a2, int down) {
    if (btn0a2 < 0 || btn0a2 > 2) return;
    uint32_t bit = 1u << btn0a2;
    /* Menus/movies/boot (not a mission): a real mouse also delivers the
     * WM at the cursor, and the GUI listens ONLY to messages — a tap that
     * only set the DirectInput bit was invisible there (field video
     * screen-20261031-183211: every HUD button dead at the main menu).
     * Button 0 shares the menu-button edge state so the WM pair stays
     * consistent with EV_MENUCLICK; buttons 1/2 post their own WM pair.
     * A click before the first menu-finger move still gets a real place
     * to land: the guest screen center — a real mouse is always
     * somewhere, and dropping the click made first taps disappear (field
     * video screen-20261031-205831: only the stick did anything).
     * Missions stay DI-only: the WM click would double-fire IK_LeftMouse
     * next to the polled device state. */
    extern int hud_game_ctx(void);
    if (hud_game_ctx() != 2 /* HUD_CTX_MISSION */) {
        if (s_cur_y < 0) {
            extern void user32_get_screen(int *w, int *h);
            int w, h;
            user32_get_screen(&w, &h);
            if (w > 0 && h > 0) {
                s_cur_x = w / 2;
                s_cur_y = h / 2;
            }
        }
        static const uint32_t down_msg[3] = {0x201, 0x204, 0x207}; /* L/R/M */
        static const uint32_t up_msg[3]   = {0x202, 0x205, 0x208};
        static const uint32_t mk_flag[3]  = {0x0001, 0x0002, 0x0010};
        if (btn0a2 == 0) {
            menu_click_edge(down);
            return;
        }
        /* Buttons 1/2 share the closed-loop cursor: steer to the (possibly
         * center-fallback) cursor before the WM pair — button 0's edge
         * steers inside menu_click_edge instead. Up edges push zero. */
        if (s_cur_y >= 0) menu_steer(s_cur_x, s_cur_y);
        if (down) s_btn |= bit; else s_btn &= ~bit;
        cur_post(down ? down_msg[btn0a2] : up_msg[btn0a2], down ? mk_flag[btn0a2] : 0);
        return;
    }
    if (down) s_btn |= bit;
    else s_btn &= ~bit;
}

uint32_t vinput_mouse_buttons(void) { return s_btn; }

void vinput_key(uint8_t dik, int down) {
    if (s_keys[dik] != (down ? 1 : 0)) {
        s_keys[dik] = down ? 1 : 0;
        s_edges++;
    }
}

const uint8_t *vinput_keys(void) { return s_keys; }

/* DIK (set-1 make code) -> Windows virtual-key code, for the overlay taps.
 * Only the keys the overlay can synthesize are mapped; 0 = no WM posted. */
static uint32_t vk_of_dik(uint8_t dik) {
    if (dik >= 0x02 && dik <= 0x0B) return dik == 0x0B ? '0' : '1' + (dik - 0x02); /* 1..9,0 */
    if (dik >= 0x3B && dik <= 0x44) return 0x70 + (dik - 0x3B); /* F1..F10 */
    switch (dik) {
    case 0x01: return 0x1B; /* Esc */
    case 0x0E: return 0x08; /* Backspace */
    case 0x0F: return 0x09; /* Tab */
    case 0x10: return 'Q';
    case 0x11: return 'W';
    case 0x12: return 'E';
    case 0x13: return 'R';
    case 0x1E: return 'A';
    case 0x1F: return 'S';
    case 0x20: return 'D';
    case 0x21: return 'F';
    case 0x2A: return 0x10; /* Shift */
    case 0x2C: return 'Z';
    case 0x2D: return 'X';
    case 0x2E: return 'C';
    case 0x39: return 0x20; /* Space */
    case 0x1C: return 0x0D; /* Enter */
    case 0xD3: return 0x2E; /* Delete */
    /* arrows: the Java HUD navigates SWAT 4's menus with the stick the way
     * a keyboard does (UE2 GUI focus moves on the WM, not on DI state) */
    case 0xC8: return 0x26; /* Up */
    case 0xD0: return 0x28; /* Down */
    case 0xCB: return 0x25; /* Left */
    case 0xCD: return 0x27; /* Right */
    default: return 0;
    }
}

void vinput_key_tap(uint8_t dik, int down) {
    int was = s_keys[dik];
    vinput_key(dik, down);
    /* Post the WM only on a real state edge: a second finger pressing the
     * same tap button inside the 120 ms window must not send a duplicate
     * WM_KEYDOWN (the DirectInput path dedupes by state, this one doesn't). */
    if (was == (down ? 1 : 0)) return;
    /* Movie phase: the guest never pumps its message queue there (bnlog
     * iutlog: the three startup videos swallowed every queued key), so
     * hand the WM straight to the game's WndProc — the channel a real
     * keyboard would reach it through. Everywhere else the queue works
     * (the game's own pump drains it) and keeps the engine's own timing. */
    if (hud_movie_playing()) user32_key_direct(vk_of_dik(dik), dik, down);
    else user32_key_event(vk_of_dik(dik), dik, down);
}

uint32_t vinput_key_edges(void) { return s_edges; }

int vinput_active(void) {
    for (int i = 0; i < 256; i++)
        if (s_keys[i]) return 1;
    return 0;
}

/* ---- menu mouse: see vinput.h ---- */

void vinput_menu_move(int x, int y) {
    /* Steer BEFORE the WM post: when the WM is deduped away the steer
     * still runs (an unchanged spot pushes zero deltas, look drift does
     * not). */
    menu_steer(x, y);
    if (x == s_cur_x && y == s_cur_y) return; /* dedupe: fingers stream at ~120 Hz */
    s_cur_x = x;
    s_cur_y = y;
    cur_post(WM_MOUSEMOVE, s_cur_left ? MK_LBUTTON : 0);
}

void vinput_menu_button(int down) {
    menu_click_edge(down);
}

/* Single source of the left-button edge shared by the free-finger menu
 * mouse (EV_MENUCLICK) and button 0 taps in menu-like contexts: real-edge
 * dedupe, the DirectInput mirror (the movie skip and some menu paths poll
 * the device instead of the queue) and the WM pair at the cursor. */
static void menu_click_edge(int down) {
    down = down ? 1 : 0;
    if (down == s_cur_left) return; /* WM expects real edges only */
    s_cur_left = down;
    /* Down edges steer the engine cursor onto the click point first: the
     * click must land where the finger is, not where the model drifted
     * (video screen-20261101-062041: clicks fired where no control was).
     * Up edges add nothing. */
    if (down && s_cur_y >= 0) menu_steer(s_cur_x, s_cur_y);
    if (down) s_btn |= 1u; else s_btn &= ~1u; /* DI mirror */
    cur_post(down ? WM_LBUTTONDOWN : WM_LBUTTONUP, down ? MK_LBUTTON : 0);
}

void vinput_cursor(int *x, int *y) {
    if (x) *x = s_cur_x;
    if (y) *y = s_cur_y < 0 ? 0 : s_cur_y;
}

void vinput_cursor_stats(int *ex, int *ey, uint32_t *steers) {
    if (ex) *ex = s_eng_mx;
    if (ey) *ey = s_eng_my;
    if (steers) *steers = s_steers;
}
