/* Virtual input synthesis: see vinput.h for the contract. All state is
 * static (zero malloc); the overlay produces, the DirectInput shim consumes,
 * everything on the game's main thread. */
#include "rt.h"
#include "vinput.h"

void user32_key_event(uint32_t vk, uint32_t sc, int down);

static volatile int32_t s_mx, s_my;       /* accumulated mouse deltas */
static volatile uint32_t s_btn;           /* bit0 left, bit1 right, bit2 middle */
static uint8_t s_keys[256];               /* DIK -> 0/1 */
static volatile uint32_t s_edges;         /* key transitions (diagnostics) */

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
    case 0x2A: return 0x10; /* Shift */
    case 0x2C: return 'Z';
    case 0x2D: return 'X';
    case 0x2E: return 'C';
    case 0x39: return 0x20; /* Space */
    case 0x1C: return 0x0D; /* Enter */
    case 0xD3: return 0x2E; /* Delete */
    default: return 0;
    }
}

void vinput_key_tap(uint8_t dik, int down) {
    vinput_key(dik, down);
    user32_key_event(vk_of_dik(dik), dik, down);
}

uint32_t vinput_key_edges(void) { return s_edges; }

int vinput_active(void) {
    for (int i = 0; i < 256; i++)
        if (s_keys[i]) return 1;
    return 0;
}
