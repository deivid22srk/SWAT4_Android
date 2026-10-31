/* Touch overlay for SWAT 4 (Android port).
 *
 * Two halves, both in this file:
 *
 *  1. Pure logic (also compiled by the selftest, no SDL/D3D): hud.ini config,
 *     button layout, multitouch state machine and the stick/look/button roles.
 *     The machine drives the game through vinput (vinput.c), which the
 *     DirectInput shim merges into the device data the game polls.
 *
 *  2. Rendering glue: called from B_IDirect3DDevice9_Present with the real
 *     host IDirect3DDevice9*. Draws a bitmap-font atlas generated at runtime
 *     plus preallocated XYZRHW|DIFFUSE|TEX1 quads, wrapping every frame in a
 *     state block (manual save/restore as fallback). Any D3D failure disables
 *     the overlay instead of taking the game down.
 *
 * Everything here runs on the game's main thread (event pump and Present both
 * go through platform_present_hook), so no locking is needed. No allocation
 * happens per frame or per event: the vertex array, finger table and config
 * are all static. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "vinput.h"

#ifdef TOUCH_SELFTEST
#define OLOG(...) do {} while (0)
#else
#include "rt.h"
#include <SDL2/SDL.h>
#if defined(__has_include)
#if __has_include(<d3d9.h>)
#define TOUCH_HAS_D3D 1
#include <d3d9.h>
#endif
#endif
#define OLOG(...) LOG(__VA_ARGS__)
extern "C" void *platform_window(void);
#endif

/* ------------------------------------------------------------------ */
/* Pure logic                                                          */
/* ------------------------------------------------------------------ */

#define TOUCH_MAXF 10   /* SDL can track more, we need 10 fingers at most */
#define TOUCH_MAXPEND 8 /* in-flight 120 ms tap auto-releases */

/* Finger roles. */
enum { R_NONE = 0, R_STICK, R_LOOK, R_BTN, R_CONSUMED };

/* Button ids (also indices into the eased-alpha array). */
enum {
    B_FOGO, B_CMD, B_USAR, B_REC, B_AGACH, B_CORRER, B_ZOOM, B_MODO,
    B_INCL, B_INCR, B_ESC, B_ESP, B_TAB, B_TROCA, B_SLOTS,
    B_SLOTS_BASE,           /* +0..9: the 1..0 / F1..F10 grid slots */
    B_FX = B_SLOTS_BASE + 10,
    B_MAXID = B_FX + 1,
};

/* What holding/tapping a button does. */
enum { K_MOUSE_HOLD, K_MOUSE_TAP, K_KEY_HOLD, K_KEY_TAP, K_GRID };

/* DIK (set-1) codes used by the layout. */
enum {
    DIK_ESC = 0x01, DIK_1 = 0x02, DIK_0 = 0x0B, DIK_TAB = 0x0F, DIK_Q = 0x10,
    DIK_W = 0x11, DIK_E = 0x12, DIK_R = 0x13, DIK_A = 0x1E, DIK_S = 0x1F,
    DIK_D = 0x20, DIK_Z = 0x2C, DIK_X = 0x2D, DIK_C = 0x2E, DIK_SPACE = 0x39,
    DIK_LSHIFT = 0x2A, DIK_F1 = 0x3B, DIK_DEL = 0xD3,
};

typedef struct {
    float scale;        /* 0.5..2.0, multiplies h/720 */
    float opacity;      /* 0.15..1.0 */
    float look_sens;    /* 0.2..3.0 */
    float stick_radius; /* 0.7..1.6, multiplies 0.16*h */
    int look_invert_y;
    int handed;         /* 1 = mirrored (stick right, actions left) */
    int labels;         /* draw text labels */
    int hold_crouch;    /* 1 = AGACH holds C down, 0 = 120 ms tap (game toggle) */
    int hold_zoom;      /* 1 = ZOOM holds Z down, 0 = 120 ms tap (game toggle) */
} HudCfg;

/* Screen insets (notch/taskbar), pixels. */
typedef struct { float top, bottom, left, right; } Safe;

typedef struct {
    uint8_t id, kind, dik;  /* dik: DIK code for K_KEY_* */
    int8_t mbtn;            /* 0/1/2 for K_MOUSE_* */
    char label[8];
    float x, y, w, h;
} Btn;

typedef struct { long id; int role; uint8_t btn_id; float ox, oy, lx, ly, cx, cy; uint32_t sector; } Finger;
typedef struct { uint8_t dik; int8_t mbtn; uint32_t due; } Pend;

typedef struct {
    HudCfg cfg;
    Safe safe;
    int w, h;
    Btn btns[15]; int nbtn;          /* base buttons, laid out for the current size */
    Btn grid[11]; int ngrid;         /* slots grid (SLOTS button) */
    float panel[4];                  /* grid panel rect for the tap-outside test */
    int grid_open, grid_page;
    Finger f[TOUCH_MAXF];
    int active;                      /* fingers with a role (orphan detection/logging) */
    float zone[4];                   /* stick zone rect */
    float stick_r, stick_dead;       /* stick radius and deadzone, pixels */
    float stick_dx, stick_dy;        /* clamped stick vector while held, for drawing */
    int stick_held;
    Pend pend[TOUCH_MAXPEND]; int npend;
    float alpha[B_MAXID];            /* eased per-button opacity */
    float fade;                      /* whole-overlay ease-in */
} TouchState;

/* Buttons are released by id, not by index: the layout can be rebuilt
 * (window size change) while a finger holds a button, and indices shift. */
static const Btn *btn_by_id(const TouchState *ts, uint8_t id) {
    for (int i = 0; i < ts->nbtn; i++)
        if (ts->btns[i].id == id) return &ts->btns[i];
    return NULL;
}

/* Classic public-domain 5x7 font, rewritten: one byte per column, bit 0 =
 * top row, for ASCII 32..126. Cell 0 (space) is all-ones on purpose: the
 * renderer samples it as a solid white texel for untextured fills, and the
 * label drawing skips spaces so the block never shows. */
static const uint8_t k_font5x7[95][5] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, /* 32 ' ' (solid white texel) */
    {0x00, 0x00, 0x5F, 0x00, 0x00}, /* 33 '!' */
    {0x00, 0x07, 0x00, 0x07, 0x00}, /* 34 '"' */
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, /* 35 '#' */
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* 36 '$' */
    {0x23, 0x13, 0x08, 0x64, 0x62}, /* 37 '%' */
    {0x36, 0x49, 0x55, 0x22, 0x50}, /* 38 '&' */
    {0x00, 0x05, 0x03, 0x00, 0x00}, /* 39 '\'' */
    {0x00, 0x1C, 0x22, 0x41, 0x00}, /* 40 '(' */
    {0x00, 0x41, 0x22, 0x1C, 0x00}, /* 41 ')' */
    {0x14, 0x08, 0x3E, 0x08, 0x14}, /* 42 '*' */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* 43 '+' */
    {0x00, 0x50, 0x30, 0x00, 0x00}, /* 44 ',' */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* 45 '-' */
    {0x00, 0x30, 0x30, 0x00, 0x00}, /* 46 '.' */
    {0x20, 0x10, 0x08, 0x04, 0x02}, /* 47 '/' */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 48 '0' */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 49 '1' */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 50 '2' */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 51 '3' */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 52 '4' */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 53 '5' */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 54 '6' */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 55 '7' */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 56 '8' */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 57 '9' */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* 58 ':' */
    {0x00, 0x56, 0x36, 0x00, 0x00}, /* 59 ';' */
    {0x00, 0x08, 0x14, 0x22, 0x41}, /* 60 '<' */
    {0x14, 0x14, 0x14, 0x14, 0x14}, /* 61 '=' */
    {0x41, 0x22, 0x14, 0x08, 0x00}, /* 62 '>' */
    {0x02, 0x01, 0x51, 0x09, 0x06}, /* 63 '?' */
    {0x32, 0x49, 0x79, 0x41, 0x3E}, /* 64 '@' */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* 65 'A' */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* 66 'B' */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* 67 'C' */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* 68 'D' */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* 69 'E' */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* 70 'F' */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* 71 'G' */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* 72 'H' */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* 73 'I' */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* 74 'J' */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* 75 'K' */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* 76 'L' */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* 77 'M' */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* 78 'N' */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* 79 'O' */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* 80 'P' */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* 81 'Q' */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* 82 'R' */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* 83 'S' */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* 84 'T' */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* 85 'U' */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* 86 'V' */
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* 87 'W' */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* 88 'X' */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* 89 'Y' */
    {0x61, 0x51, 0x49, 0x41, 0x43}, /* 90 'Z' */
    {0x00, 0x7F, 0x41, 0x41, 0x00}, /* 91 '[' */
    {0x02, 0x04, 0x08, 0x10, 0x20}, /* 92 '\\' */
    {0x00, 0x41, 0x41, 0x7F, 0x00}, /* 93 ']' */
    {0x04, 0x02, 0x01, 0x02, 0x04}, /* 94 '^' */
    {0x40, 0x40, 0x40, 0x40, 0x40}, /* 95 '_' */
    {0x00, 0x01, 0x02, 0x04, 0x00}, /* 96 '`' */
    {0x20, 0x54, 0x54, 0x54, 0x78}, /* 97 'a' */
    {0x7F, 0x48, 0x44, 0x44, 0x38}, /* 98 'b' */
    {0x38, 0x44, 0x44, 0x44, 0x20}, /* 99 'c' */
    {0x38, 0x44, 0x44, 0x48, 0x7F}, /* 100 'd' */
    {0x38, 0x54, 0x54, 0x54, 0x18}, /* 101 'e' */
    {0x08, 0x7E, 0x09, 0x01, 0x02}, /* 102 'f' */
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, /* 103 'g' */
    {0x7F, 0x08, 0x04, 0x04, 0x78}, /* 104 'h' */
    {0x00, 0x44, 0x7D, 0x40, 0x00}, /* 105 'i' */
    {0x20, 0x40, 0x44, 0x3D, 0x00}, /* 106 'j' */
    {0x7F, 0x10, 0x28, 0x44, 0x00}, /* 107 'k' */
    {0x00, 0x41, 0x7F, 0x40, 0x00}, /* 108 'l' */
    {0x7C, 0x04, 0x18, 0x04, 0x78}, /* 109 'm' */
    {0x7C, 0x08, 0x04, 0x04, 0x78}, /* 110 'n' */
    {0x38, 0x44, 0x44, 0x44, 0x38}, /* 111 'o' */
    {0x7C, 0x14, 0x14, 0x14, 0x08}, /* 112 'p' */
    {0x08, 0x14, 0x14, 0x18, 0x7C}, /* 113 'q' */
    {0x7C, 0x08, 0x04, 0x04, 0x08}, /* 114 'r' */
    {0x48, 0x54, 0x54, 0x54, 0x20}, /* 115 's' */
    {0x04, 0x3F, 0x44, 0x40, 0x20}, /* 116 't' */
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, /* 117 'u' */
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, /* 118 'v' */
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, /* 119 'w' */
    {0x44, 0x28, 0x10, 0x28, 0x44}, /* 120 'x' */
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, /* 121 'y' */
    {0x44, 0x64, 0x54, 0x4C, 0x44}, /* 122 'z' */
    {0x00, 0x08, 0x36, 0x41, 0x00}, /* 123 '{' */
    {0x00, 0x00, 0x7F, 0x00, 0x00}, /* 124 '|' */
    {0x00, 0x41, 0x36, 0x08, 0x00}, /* 125 '}' */
    {0x08, 0x04, 0x08, 0x10, 0x08}, /* 126 '~' */
};

static float maxf(float a, float b) { return a > b ? a : b; }
static float minf(float a, float b) { return a < b ? a : b; }

static void hud_cfg_defaults(HudCfg *c) {
    memset(c, 0, sizeof *c);
    c->scale = 1.0f;
    c->opacity = 0.55f;
    c->look_sens = 1.0f;
    c->stick_radius = 1.0f;
}
static void hud_cfg_clamp(HudCfg *c) {
    c->scale = minf(maxf(c->scale, 0.5f), 2.0f);
    c->opacity = minf(maxf(c->opacity, 0.15f), 1.0f);
    c->look_sens = minf(maxf(c->look_sens, 0.2f), 3.0f);
    c->stick_radius = minf(maxf(c->stick_radius, 0.7f), 1.6f);
    c->look_invert_y = c->look_invert_y ? 1 : 0;
    c->handed = c->handed ? 1 : 0;
    c->labels = c->labels ? 1 : 0;
    c->hold_crouch = c->hold_crouch ? 1 : 0;
    c->hold_zoom = c->hold_zoom ? 1 : 0;
}

/* Tiny parser for <g_save_dir>/hud.ini: lines "key=value", '#' comments. */
static int hud_cfg_load(HudCfg *c, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char *p = strchr(line, '#');
        if (p) *p = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *key = line, *val = eq + 1;
        while (*key == ' ' || *key == '\t') key++;
        if (eq == line) continue;
        char *e = eq - 1;
        while (e > line && (*e == ' ' || *e == '\t')) *e-- = 0;
        if (!strcmp(key, "scale")) c->scale = strtof(val, NULL);
        else if (!strcmp(key, "opacity")) c->opacity = strtof(val, NULL);
        else if (!strcmp(key, "look_sens")) c->look_sens = strtof(val, NULL);
        else if (!strcmp(key, "stick_radius")) c->stick_radius = strtof(val, NULL);
        else if (!strcmp(key, "look_invert_y")) c->look_invert_y = (int)strtol(val, NULL, 10);
        else if (!strcmp(key, "handed")) c->handed = (int)strtol(val, NULL, 10);
        else if (!strcmp(key, "labels")) c->labels = (int)strtol(val, NULL, 10);
        else if (!strcmp(key, "hold_crouch")) c->hold_crouch = (int)strtol(val, NULL, 10);
        else if (!strcmp(key, "hold_zoom")) c->hold_zoom = (int)strtol(val, NULL, 10);
    }
    fclose(f);
    hud_cfg_clamp(c);
    return 1;
}

static void hud_cfg_save(const char *path, const HudCfg *c) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    /* Only written when the file does not exist yet (ts_init); user edits
     * are loaded, clamped and kept, never overwritten. */
    fprintf(f, "# SWAT4 Android touch overlay config\n"
               "# scale 0.5-2.0, opacity 0.15-1.0, look_sens 0.2-3.0, stick_radius 0.7-1.6\n"
               "# look_invert_y / handed / labels / hold_crouch / hold_zoom: 0 or 1\n"
               "# AGACH (C) and ZOOM (Z) are 120 ms taps by default (the game's own\n"
               "# crouch/zoom modes toggle); set hold_crouch/hold_zoom=1 to hold the key.\n"
               "scale=%g\nopacity=%g\nlook_sens=%g\nlook_invert_y=%d\n"
               "handed=%d\nlabels=%d\nstick_radius=%g\nhold_crouch=%d\nhold_zoom=%d\n",
            (double)c->scale, (double)c->opacity, (double)c->look_sens, c->look_invert_y,
            c->handed, c->labels, (double)c->stick_radius, c->hold_crouch, c->hold_zoom);
    fclose(f);
}

static void hud_safe_defaults(Safe *s, int w, int h) {
    /* No usable-bounds info: assume 2% of height at the top, 1% elsewhere. */
    s->top = 0.02f * h;
    s->bottom = s->left = s->right = 0.01f * h;
    (void)w;
}

/* Layout unit: h/720 scaled by config, capped so the tallest button stack
 * (392u right column + 64u top row) always fits between the safe insets. */
static float hud_unit(const HudCfg *cfg, int w, int h, const Safe *s) {
    float u = h / 720.0f * cfg->scale;
    float fit = (h - s->top - s->bottom) / 456.0f;
    return minf(u, fit);
}

/* Buttons are at least 5.5% of the screen height so they stay tappable at
 * any resolution and scale setting. */
static float hud_sz(float units, float u, int h) { return maxf(units * u, 0.055f * h); }

static void btn_set(Btn *b, int id, int kind, int dik, int mbtn, const char *label, float x, float y, float w, float h) {
    b->id = (uint8_t)id;
    b->kind = (uint8_t)kind;
    b->dik = (uint8_t)dik;
    b->mbtn = (int8_t)mbtn;
    snprintf(b->label, sizeof b->label, "%s", label);
    b->x = x; b->y = y; b->w = w; b->h = h;
}

/* Base button layout. All rects are computed for a right-handed player and
 * mirrored inside the usable band when cfg->handed is set. The centre of the
 * screen is left free (look zone). */
static int hud_layout(const HudCfg *cfg, int w, int h, const Safe *s, Btn *out, int maxn) {
    if (w < 320 || h < 240 || maxn < 15) return 0;
    float u = hud_unit(cfg, w, h, s);
    float g = 10 * u;
    float xr = w - s->right, yb = h - s->bottom; /* usable right/bottom edges */
    float f = hud_sz(150, u, h), c = hud_sz(100, u, h), us = hud_sz(90, u, h);
    float r = hud_sz(72, u, h), sm = hud_sz(56, u, h), e = hud_sz(64, u, h);
    float zx = s->left, zy = yb - 0.38f * h, zw = 0.30f * w;

    /* right cluster: fire column, then use/reload column, then crouch column */
    btn_set(out + B_FOGO, B_FOGO, K_MOUSE_HOLD, 0, 0, "FOGO", xr - f, yb - f, f, f);
    btn_set(out + B_CMD, B_CMD, K_MOUSE_HOLD, 0, 1, "CMD", xr - c, out[B_FOGO].y - g - c, c, c);
    btn_set(out + B_SLOTS, B_SLOTS, K_GRID, 0, 0, "SLOTS", xr - sm, out[B_CMD].y - g - sm, sm, sm);
    btn_set(out + B_USAR, B_USAR, K_MOUSE_TAP, 0, 2, "USAR", out[B_FOGO].x - g - us, yb - us, us, us);
    btn_set(out + B_REC, B_REC, K_KEY_TAP, DIK_R, -1, "REC", out[B_USAR].x - g - r, yb - r, r, r);
    btn_set(out + B_ZOOM, B_ZOOM, cfg->hold_zoom ? K_KEY_HOLD : K_KEY_TAP, DIK_Z, -1, "ZOOM",
            out[B_REC].x, out[B_REC].y - g - r, r, r);
    btn_set(out + B_MODO, B_MODO, K_KEY_TAP, DIK_X, -1, "MODO", out[B_REC].x, out[B_ZOOM].y - g - r, r, r);
    btn_set(out + B_AGACH, B_AGACH, cfg->hold_crouch ? K_KEY_HOLD : K_KEY_TAP, DIK_C, -1, "AGACH",
            out[B_REC].x - g - r, yb - r, r, r);
    btn_set(out + B_CORRER, B_CORRER, K_KEY_HOLD, DIK_LSHIFT, -1, "CORRER", out[B_AGACH].x, out[B_AGACH].y - g - r, r, r);
    /* left edge above the stick zone */
    btn_set(out + B_INCL, B_INCL, K_KEY_HOLD, DIK_Q, -1, "INC<", zx + 2 * u, zy - g - r, r, r);
    btn_set(out + B_INCR, B_INCR, K_KEY_HOLD, DIK_E, -1, "INC>", out[B_INCL].x + r + g, out[B_INCL].y, r, r);
    /* right edge, above the SLOTS button */
    btn_set(out + B_TAB, B_TAB, K_KEY_TAP, DIK_TAB, -1, "TAB", xr - sm, out[B_SLOTS].y - g - sm, sm, sm);
    btn_set(out + B_ESP, B_ESP, K_KEY_TAP, DIK_SPACE, -1, "ESP", out[B_TAB].x - g - sm, out[B_TAB].y, sm, sm);
    /* top row */
    btn_set(out + B_ESC, B_ESC, K_KEY_TAP, DIK_ESC, -1, "ESC", xr - e, s->top, e, e);
    btn_set(out + B_TROCA, B_TROCA, K_KEY_TAP, DIK_DEL, -1, "TROCA", out[B_ESC].x - g - e, s->top, e, e);

    if (cfg->handed) {
        for (int i = 0; i < 15; i++) {
            float d = out[i].x - s->left;
            out[i].x = (w - s->right) - d - out[i].w;
        }
        zx = (w - s->right) - (zx - s->left) - zw;
    }
    (void)zw;
    return 15;
}

/* The equip grid: 10 slots (1..0 or F1..F10 by page) plus a page button. */
static int hud_grid_layout(const HudCfg *cfg, int page, int w, int h, const Safe *s,
                    Btn *out, int maxn, float *panel) {
    if (w < 320 || h < 240 || maxn < 11) return 0;
    float u = hud_unit(cfg, w, h, s);
    float gg = 8 * u, pad = 16 * u, title = 22 * u;
    float sl = hud_sz(64, u, h), fx = hud_sz(56, u, h);
    float pw = 5 * sl + 4 * gg + 2 * pad;
    float ph = title + gg + 2 * sl + gg + fx + 2 * pad;
    float px = (w - pw) * 0.5f, py = (h - ph) * 0.42f;
    px = minf(maxf(px, s->left), w - s->right - pw);
    py = minf(maxf(py, s->top), h - s->bottom - ph);
    if (panel) { panel[0] = px; panel[1] = py; panel[2] = pw; panel[3] = ph; }
    for (int i = 0; i < 10; i++) {
        float bx = px + pad + (i % 5) * (sl + gg);
        float by = py + pad + title + gg + (i / 5) * (sl + gg);
        char label[4];
        if (page) snprintf(label, sizeof label, "F%d", i + 1);
        else snprintf(label, sizeof label, "%d", i == 9 ? 0 : i + 1);
        int dik = page ? DIK_F1 + i : DIK_1 + i;
        btn_set(out + i, B_SLOTS_BASE + i, K_KEY_TAP, dik, -1, label, bx, by, sl, sl);
    }
    btn_set(out + 10, B_FX, K_GRID, 0, 0, page ? "NUM" : "FX",
            px + (pw - fx) * 0.5f, py + pad + title + gg + 2 * sl + gg, fx, fx);
    return 11;
}

static int hud_hit(const Btn *bs, int n, float x, float y) {
    for (int i = 0; i < n; i++)
        if (x >= bs[i].x && x < bs[i].x + bs[i].w && y >= bs[i].y && y < bs[i].y + bs[i].h)
            return i;
    return -1;
}

/* 8-way sector mask from a stick vector: 1=left 2=right 4=up 8=down, with
 * hysteresis: a bit turns on past ~26 deg off-axis and only turns off below
 * ~19 deg, so finger jitter around the 22.5 deg wedge boundary cannot flap
 * key edges into the game (digital WASD). prev is the currently reported
 * sector; the deadzone still zeroes everything outright. */
static uint32_t stick_sector(float dx, float dy, float dead, uint32_t prev) {
    float mag = sqrtf(dx * dx + dy * dy);
    if (mag <= dead || mag <= 0.0f) return 0;
    float nx = dx / mag, ny = dy / mag;
    const float on = 0.44f, off = 0.33f; /* sin 26 deg / sin 19 deg */
    uint32_t mask = 0;
    if (prev & 1) { if (nx < -off) mask |= 1; } else if (nx < -on) mask |= 1;
    if (prev & 2) { if (nx >  off) mask |= 2; } else if (nx >  on) mask |= 2;
    if (prev & 4) { if (ny < -off) mask |= 4; } else if (ny < -on) mask |= 4;
    if (prev & 8) { if (ny >  off) mask |= 8; } else if (ny >  on) mask |= 8;
    return mask;
}

/* Sector -> WASD, only the bits that changed (no re-sends per frame). */
static void stick_keys(uint32_t old, uint32_t cur) {
    static const uint32_t bit[4] = {1, 2, 4, 8};
    static const uint8_t dik[4] = {DIK_A, DIK_D, DIK_W, DIK_S};
    for (int i = 0; i < 4; i++) {
        int was = (old & bit[i]) != 0, is = (cur & bit[i]) != 0;
        if (was != is) vinput_key(dik[i], is);
    }
}

/* Exponential approach, ~95% of the way after 120 ms (no flicker between
 * frames because it converges instead of stepping). */
static float ease_to(float cur, float target, float dt_ms) {
    if (dt_ms <= 0.0f || cur == target) return cur;
    float v = target + (cur - target) * expf(-dt_ms / 40.0f);
    if (fabsf(v - target) < 0.01f) v = target;
    return v;
}

static void pend_fire(TouchState *ts, int i) {
    if (ts->pend[i].mbtn >= 0) vinput_mouse_button(ts->pend[i].mbtn, 0);
    else vinput_key_tap(ts->pend[i].dik, 0);
    ts->pend[i] = ts->pend[--ts->npend];
}

static void pend_add(TouchState *ts, uint8_t dik, int8_t mbtn, uint32_t now) {
    /* Dedupe: a second press of the same tap while the first 120 ms window
     * is still open must not schedule a second release (double WM up). */
    for (int i = 0; i < ts->npend; i++)
        if (ts->pend[i].dik == dik && ts->pend[i].mbtn == mbtn) return;
    if (ts->npend == TOUCH_MAXPEND) {
        /* Table full: release immediately instead of leaking a stuck key. */
        if (mbtn >= 0) vinput_mouse_button(mbtn, 0);
        else vinput_key_tap(dik, 0);
        return;
    }
    ts->pend[ts->npend++] = (Pend){dik, mbtn, now + 120};
}

static void ts_tick(TouchState *ts, uint32_t now) {
    for (int i = ts->npend - 1; i >= 0; i--)
        if ((int32_t)(now - ts->pend[i].due) >= 0) pend_fire(ts, i);
}

static void apply_press(TouchState *ts, const Btn *b, uint32_t now) {
    switch (b->kind) {
    case K_MOUSE_HOLD: vinput_mouse_button(b->mbtn, 1); break;
    case K_MOUSE_TAP: vinput_mouse_button(b->mbtn, 1); pend_add(ts, 0, b->mbtn, now); break;
    case K_KEY_HOLD: vinput_key(b->dik, 1); break;
    case K_KEY_TAP: vinput_key_tap(b->dik, 1); pend_add(ts, b->dik, -1, now); break;
    default: break;
    }
}

static void apply_release(const Btn *b) {
    switch (b->kind) {
    case K_MOUSE_HOLD: vinput_mouse_button(b->mbtn, 0); break;
    case K_KEY_HOLD: vinput_key(b->dik, 0); break;
    default: break; /* taps release themselves via pend */
    }
}

/* (Re)build the layouts when the window size or safe area changed. */
static void ts_ensure_layout(TouchState *ts, int w, int h, const Safe *s) {
    if (ts->nbtn && ts->w == w && ts->h == h &&
        !memcmp(&ts->safe, s, sizeof(*s))) return;
    ts->w = w;
    ts->h = h;
    ts->safe = *s;
    ts->nbtn = hud_layout(&ts->cfg, w, h, s, ts->btns, 15);
    ts->ngrid = hud_grid_layout(&ts->cfg, ts->grid_page, w, h, s, ts->grid, 11, ts->panel);
    ts->zone[0] = s->left;
    ts->zone[1] = h - s->bottom - 0.38f * h;
    ts->zone[2] = 0.30f * w;
    ts->zone[3] = 0.38f * h;
    if (ts->cfg.handed)
        ts->zone[0] = (w - s->right) - (ts->zone[0] - s->left) - ts->zone[2];
    ts->stick_r = ts->cfg.stick_radius * 0.16f * h;
    ts->stick_dead = 0.12f * ts->stick_r;
}

static int in_zone(const float *z, float x, float y) {
    return x >= z[0] && x < z[0] + z[2] && y >= z[1] && y < z[1] + z[3];
}

static Finger *finger_find(TouchState *ts, long id) {
    for (int i = 0; i < TOUCH_MAXF; i++)
        if (ts->f[i].role != R_NONE && ts->f[i].id == id) return &ts->f[i];
    return NULL;
}

static void ts_down(TouchState *ts, long fid, float x, float y, int w, int h, uint32_t now) {
    ts_ensure_layout(ts, w, h, &ts->safe);
    Finger *fr = NULL;
    for (int i = 0; i < TOUCH_MAXF && !fr; i++)
        if (ts->f[i].role == R_NONE) fr = &ts->f[i];
    if (!fr) return; /* more than TOUCH_MAXF fingers: ignore the extras */

    /* Grid takes the tap first: slots fire their key and close, FX flips the
     * page, a tap outside the panel closes without touching the game. */
    if (ts->grid_open) {
        int gi = hud_hit(ts->grid, ts->ngrid, x, y);
        if (gi >= 0 && ts->grid[gi].id == B_FX) {
            ts->grid_page ^= 1;
            ts->ngrid = hud_grid_layout(&ts->cfg, ts->grid_page, w, h, &ts->safe, ts->grid, 11, ts->panel);
            OLOG("overlay: slots page -> %d", ts->grid_page);
        } else if (gi >= 0) {
            vinput_key_tap(ts->grid[gi].dik, 1);
            pend_add(ts, ts->grid[gi].dik, -1, now);
            ts->grid_open = 0;
        } else if (!(x >= ts->panel[0] && x < ts->panel[0] + ts->panel[2] &&
                     y >= ts->panel[1] && y < ts->panel[1] + ts->panel[3])) {
            ts->grid_open = 0;
            OLOG("overlay: slots grid closed (tap outside)");
        }
        fr->id = fid;
        fr->role = R_CONSUMED;
        ts->active++;
        return;
    }

    int bi = hud_hit(ts->btns, ts->nbtn, x, y);
    fr->id = fid;
    fr->sector = 0;
    fr->cx = fr->cy = 0;
    if (bi >= 0) {
        fr->role = R_BTN;
        fr->btn_id = ts->btns[bi].id;
        apply_press(ts, &ts->btns[bi], now);
        if (ts->btns[bi].id == B_SLOTS) {
            ts->grid_open = 1;
            OLOG("overlay: slots grid open (page %d)", ts->grid_page);
        }
    } else if (in_zone(ts->zone, x, y)) {
        fr->role = R_STICK;
        fr->ox = x;
        fr->oy = y; /* floating stick: the origin is where the finger landed */
        ts->stick_held++;
    } else {
        fr->role = R_LOOK;
        fr->lx = x;
        fr->ly = y;
        fr->cx = fr->cy = 0; /* sub-pixel carry so slow drags lose nothing */
    }
    ts->active++;
}

static void ts_move(TouchState *ts, long fid, float x, float y) {
    Finger *fr = finger_find(ts, fid);
    if (!fr) return;
    if (fr->role == R_STICK) {
        float dx = x - fr->ox, dy = y - fr->oy;
        float mag = sqrtf(dx * dx + dy * dy);
        if (mag > ts->stick_r && mag > 0.0f) {
            /* leash the origin so the finger stays on the rim */
            fr->ox = x - dx * ts->stick_r / mag;
            fr->oy = y - dy * ts->stick_r / mag;
            dx = x - fr->ox;
            dy = y - fr->oy;
        }
        ts->stick_dx = dx;
        ts->stick_dy = dy;
        uint32_t sec = stick_sector(dx, dy, ts->stick_dead, fr->sector);
        if (sec != fr->sector) {
            stick_keys(fr->sector, sec);
            fr->sector = sec;
        }
    } else if (fr->role == R_LOOK) {
        /* Sub-pixel carry: raw drag distance accumulates, only whole
         * scaled pixels are emitted, the rest stays for the next event. */
        float cx = fr->cx + (x - fr->lx), cy = fr->cy + (y - fr->ly);
        int tx = (int)(cx * ts->cfg.look_sens), ty = (int)(cy * ts->cfg.look_sens);
        int iy = ts->cfg.look_invert_y ? -ty : ty;
        fr->cx = cx - tx / ts->cfg.look_sens;
        fr->cy = cy - ty / ts->cfg.look_sens;
        if (tx || iy) vinput_mouse_delta(tx, iy);
        fr->lx = x;
        fr->ly = y;
    }
}

static void ts_up(TouchState *ts, long fid) {
    Finger *fr = finger_find(ts, fid);
    if (!fr) return;
    if (fr->role == R_BTN) {
        const Btn *b = btn_by_id(ts, fr->btn_id);
        if (b) apply_release(b);
    } else if (fr->role == R_STICK) {
        stick_keys(fr->sector, 0);
        fr->sector = 0;
        if (--ts->stick_held <= 0) { ts->stick_held = 0; ts->stick_dx = ts->stick_dy = 0; }
    }
    fr->role = R_NONE;
    if (ts->active > 0) ts->active--;
}

/* App lost focus / finger tracking: drop everything so no key or mouse
 * button stays latched into the game. */
static void ts_release_all(TouchState *ts, uint32_t now) {
    for (int i = 0; i < TOUCH_MAXF; i++) {
        Finger *fr = &ts->f[i];
        if (fr->role == R_NONE) continue;
        if (fr->role == R_BTN) {
            const Btn *b = btn_by_id(ts, fr->btn_id);
            if (b) apply_release(b);
        } else if (fr->role == R_STICK) {
            stick_keys(fr->sector, 0);
            fr->sector = 0;
        }
        fr->role = R_NONE;
    }
    ts->active = 0;
    ts->stick_held = 0;
    ts->stick_dx = ts->stick_dy = 0;
    ts->grid_open = 0;
    while (ts->npend) pend_fire(ts, ts->npend - 1); /* due or not: release now */
    (void)now;
}

/* Load (or create with defaults) hud.ini. Returns the values via ts->cfg. */
static void ts_init(TouchState *ts, const char *ini_path) {
    memset(ts, 0, sizeof *ts);
    hud_cfg_defaults(&ts->cfg);
    if (!hud_cfg_load(&ts->cfg, ini_path)) {
        hud_cfg_save(ini_path, &ts->cfg);
        OLOG("overlay: wrote defaults to %s", ini_path);
    }
    hud_cfg_clamp(&ts->cfg);
    ts->fade = 0.0f;
}

/* ------------------------------------------------------------------ */
/* Selftest (TOUCH_SELFTEST): no SDL, no D3D, pure logic + vinput      */
/* ------------------------------------------------------------------ */
#ifdef TOUCH_SELFTEST

static int g_fail;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (cond) printf("PASS: %s\n", #cond);                         \
        else { printf("FAIL: %s (line %d)\n", #cond, __LINE__); g_fail++; } \
    } while (0)

/* externs recorded by scripts/selftest_stubs.c (compiled as C) */
extern "C" {
extern int g_wm_n;
extern uint32_t g_wm_vk[], g_wm_sc[];
extern int g_wm_down[];
}

static const Btn *tbtn(TouchState *ts, int id) {
    for (int i = 0; i < ts->nbtn; i++)
        if (ts->btns[i].id == id) return &ts->btns[i];
    return NULL;
}

static void font_dump(const char *chars) {
    printf("--- font sample ---\n");
    for (int row = 0; row < 7; row++) {
        for (const char *p = chars; *p; p++) {
            int idx = (uint8_t)*p - 32;
            if (idx < 0 || idx > 94) { printf("      "); continue; }
            for (int col = 0; col < 5; col++) {
                int px = (k_font5x7[idx][col] >> row) & 1;
                printf(px ? "##" : "  ");
            }
            printf("  ");
        }
        printf("\n");
    }
}

static int rect_in_screen(const Btn *b, int w, int h) {
    return b->x >= -1 && b->y >= -1 && b->x + b->w <= w + 1 && b->y + b->h <= h + 1;
}

int main(void) {
    const int W = 1280, H = 720;
    uint32_t now = 1000;
    Safe s;
    hud_safe_defaults(&s, W, H);

    font_dump("AGRSWZ0159<>=.FC/E");
    font_dump("abdimnopqrus");

    /* --- config: defaults, clamp, ini round-trip --- */
    {
        HudCfg c;
        hud_cfg_defaults(&c);
        CHECK(c.scale == 1.0f && c.opacity == 0.55f && c.look_sens == 1.0f && !c.look_invert_y);
        c.scale = 99; c.opacity = -1; c.look_sens = 99; c.stick_radius = 0.1f;
        c.look_invert_y = 5; c.handed = 3; c.labels = 0;
        hud_cfg_clamp(&c);
        CHECK(c.scale == 2.0f && c.opacity == 0.15f && c.look_sens == 3.0f);
        CHECK(c.stick_radius == 0.7f && c.look_invert_y == 1 && c.handed == 1);
        FILE *f = fopen("/tmp/ts_test_bad.ini", "w");
        CHECK(f != NULL);
        if (f) {
            fputs("scale=9.0\nopacity=0.0\nlook_sens=99\nlook_invert_y=1\nhanded=1\nstick_radius=0.1\nlabels=0\n", f);
            fclose(f);
        }
        TouchState ts;
        ts_init(&ts, "/tmp/ts_test_bad.ini");
        CHECK(ts.cfg.scale == 2.0f && ts.cfg.opacity == 0.15f && ts.cfg.look_sens == 3.0f);
        CHECK(ts.cfg.look_invert_y == 1 && ts.cfg.handed == 1 && ts.cfg.stick_radius == 0.7f);
        remove("/tmp/ts_test_missing.ini");
        TouchState t2;
        ts_init(&t2, "/tmp/ts_test_missing.ini");
        CHECK(t2.cfg.scale == 1.0f && t2.cfg.opacity == 0.55f); /* defaults + file written */
        FILE *g = fopen("/tmp/ts_test_missing.ini", "r");
        CHECK(g != NULL); /* defaults file was created */
        if (g) fclose(g);
    }

    /* --- layout: min touch target, on-screen, mirror --- */
    {
        for (int pass = 0; pass < 3; pass++) {
            HudCfg c;
            hud_cfg_defaults(&c);
            c.scale = pass == 0 ? 0.5f : pass == 1 ? 1.0f : 2.0f;
            Btn bs[15];
            CHECK(hud_layout(&c, W, H, &s, bs, 15) == 15);
            float minb = 0.055f * H;
            int okmin = 1, okscr = 1;
            for (int i = 0; i < 15; i++) {
                if (bs[i].h < minb - 0.5f) okmin = 0;
                if (!rect_in_screen(&bs[i], W, H)) okscr = 0;
            }
            CHECK(okmin);
            CHECK(okscr);
            /* fire and command must not overlap */
            const Btn *f = &bs[B_FOGO], *cm = &bs[B_CMD];
            int sep = f->x >= cm->x + cm->w || cm->x >= f->x + f->w ||
                      f->y >= cm->y + cm->h || cm->y >= f->y + f->h;
            CHECK(sep);
        }
        HudCfg c;
        hud_cfg_defaults(&c);
        c.handed = 1;
        Btn bs[15];
        hud_layout(&c, W, H, &s, bs, 15);
        CHECK(bs[B_FOGO].x + bs[B_FOGO].w * 0.5f < W * 0.5f); /* fire moved left */
    }

    /* --- 1. tap on FOGO -> mouse button 0 down, then up --- */
    TouchState ts;
    remove("/tmp/ts_test_missing.ini");
    ts_init(&ts, "/tmp/ts_test_missing.ini"); /* defaults */
    ts_ensure_layout(&ts, W, H, &s);
    {
        const Btn *f = tbtn(&ts, B_FOGO);
        CHECK(f != NULL);
        ts_down(&ts, 101, f->x + f->w / 2, f->y + f->h / 2, W, H, now);
        CHECK((vinput_mouse_buttons() & 1u) != 0);
        CHECK(finger_find(&ts, 101) && finger_find(&ts, 101)->role == R_BTN);
        ts_up(&ts, 101);
        CHECK((vinput_mouse_buttons() & 1u) == 0);
        /* USAR: middle button 120 ms tap */
        const Btn *u = tbtn(&ts, B_USAR);
        int dx0, dy0;
        vinput_mouse_take(&dx0, &dy0);
        ts_down(&ts, 102, u->x + u->w / 2, u->y + u->h / 2, W, H, now);
        CHECK((vinput_mouse_buttons() & 4u) != 0);
        ts_tick(&ts, now + 50);
        CHECK((vinput_mouse_buttons() & 4u) != 0); /* still held before 120 ms */
        ts_tick(&ts, now + 200);
        CHECK((vinput_mouse_buttons() & 4u) == 0); /* auto-released */
        ts_up(&ts, 102);
    }

    /* --- 2. stick drag NE -> W+D, no re-edges inside the same sector --- */
    {
        uint32_t e0 = vinput_key_edges();
        float sx = ts.zone[0] + ts.zone[2] * 0.5f, sy = ts.zone[1] + ts.zone[3] * 0.5f;
        ts_down(&ts, 201, sx, sy, W, H, now);
        CHECK(finger_find(&ts, 201) && finger_find(&ts, 201)->role == R_STICK);
        ts_move(&ts, 201, sx + 60, sy - 60);
        CHECK(vinput_keys()[DIK_W] && vinput_keys()[DIK_D]);
        CHECK(!vinput_keys()[DIK_A] && !vinput_keys()[DIK_S]);
        CHECK(vinput_key_edges() == e0 + 2); /* exactly W-down + D-down */
        ts_move(&ts, 201, sx + 80, sy - 80); /* same sector */
        CHECK(vinput_key_edges() == e0 + 2); /* nothing re-sent */
        ts_move(&ts, 201, sx + 5, sy + 5);   /* back inside the deadzone */
        CHECK(vinput_key_edges() == e0 + 4 && !vinput_keys()[DIK_W] && !vinput_keys()[DIK_D] &&
              !vinput_keys()[DIK_A] && !vinput_keys()[DIK_S]);
        ts_up(&ts, 201);
        CHECK(vinput_key_edges() == e0 + 4);
    }

    /* --- 3. look drag 40 px -> ~40*sens mouse deltas; carry; invert_y --- */
    {
        int mx, my;
        vinput_mouse_take(&mx, &my);
        CHECK(mx == 0 && my == 0);
        ts_down(&ts, 301, W * 0.5f, H * 0.45f, W, H, now); /* centre: look zone */
        CHECK(finger_find(&ts, 301) && finger_find(&ts, 301)->role == R_LOOK);
        ts_move(&ts, 301, W * 0.5f + 40, H * 0.45f);
        vinput_mouse_take(&mx, &my);
        CHECK(mx >= 39 && mx <= 41 && my == 0);
        /* three small moves of 0.4 px each: only the sub-pixel carry makes
         * the third one emit a whole pixel */
        ts_move(&ts, 301, W * 0.5f + 40.4f, H * 0.45f);
        ts_move(&ts, 301, W * 0.5f + 40.8f, H * 0.45f);
        vinput_mouse_take(&mx, &my);
        CHECK(mx == 0);
        ts_move(&ts, 301, W * 0.5f + 41.2f, H * 0.45f);
        vinput_mouse_take(&mx, &my);
        CHECK(mx == 1); /* 0.4 + 0.4 + 0.4 = 1.2 -> one pixel kept for later */
        ts.cfg.look_invert_y = 1;
        ts_move(&ts, 301, W * 0.5f + 41.2f, H * 0.45f + 40);
        vinput_mouse_take(&mx, &my);
        CHECK(mx == 0 && my == -40);
        ts_up(&ts, 301);
    }

    /* --- 4. stick + REC hold + look drag at the same time --- */
    {
        float sx = ts.zone[0] + ts.zone[2] * 0.5f, sy = ts.zone[1] + ts.zone[3] * 0.5f;
        const Btn *rec = tbtn(&ts, B_REC);
        int dx0, dy0;
        vinput_mouse_take(&dx0, &dy0);
        ts_down(&ts, 401, sx, sy, W, H, now);
        ts_down(&ts, 402, rec->x + rec->w / 2, rec->y + rec->h / 2, W, H, now + 20);
        ts_down(&ts, 403, W * 0.5f, H * 0.45f, W, H, now + 30);
        ts_move(&ts, 401, sx + 55, sy - 55);
        ts_move(&ts, 403, W * 0.5f + 30, H * 0.45f);
        ts_tick(&ts, now + 50);
        CHECK(vinput_keys()[DIK_W] && vinput_keys()[DIK_D]);
        CHECK(vinput_keys()[DIK_R]); /* REC pressed, auto-up not due yet */
        int mx, my;
        vinput_mouse_take(&mx, &my);
        CHECK(mx >= 29 && mx <= 31 && my == 0);
        CHECK(finger_find(&ts, 401)->role == R_STICK && finger_find(&ts, 402)->role == R_BTN &&
              finger_find(&ts, 403)->role == R_LOOK);
        ts_tick(&ts, now + 200);
        CHECK(!vinput_keys()[DIK_R]); /* tap finished while stick stays on */
        CHECK(vinput_keys()[DIK_W]);
        ts_up(&ts, 401);
        ts_up(&ts, 402);
        ts_up(&ts, 403);
        CHECK(!vinput_keys()[DIK_W] && !vinput_keys()[DIK_D]);
    }

    /* --- 5. SLOTS grid: open, tap slot 5, tap outside closes --- */
    {
        uint32_t e0 = vinput_key_edges();
        int wm0 = g_wm_n;
        const Btn *sl = tbtn(&ts, B_SLOTS);
        ts_down(&ts, 501, sl->x + sl->w / 2, sl->y + sl->h / 2, W, H, now);
        CHECK(ts.grid_open);
        const Btn *slot5 = &ts.grid[4];
        CHECK(slot5->id == B_SLOTS_BASE + 4 && slot5->dik == 0x06);
        ts_down(&ts, 502, slot5->x + slot5->w / 2, slot5->y + slot5->h / 2, W, H, now + 10);
        CHECK(!ts.grid_open);               /* grid closed itself */
        CHECK(vinput_keys()[0x06]);         /* DIK 5 down */
        CHECK(g_wm_n > wm0 && g_wm_sc[g_wm_n - 1] == 0x06 && g_wm_down[g_wm_n - 1] == 1);
        ts_up(&ts, 502);
        ts_up(&ts, 501);
        ts_tick(&ts, now + 200);
        CHECK(!vinput_keys()[0x06]);
        CHECK(vinput_key_edges() == e0 + 2);
        /* tap outside the panel closes without game input */
        ts_down(&ts, 503, sl->x + sl->w / 2, sl->y + sl->h / 2, W, H, now);
        CHECK(ts.grid_open);
        int wm1 = g_wm_n;
        ts_down(&ts, 504, 12, 12, W, H, now + 10);
        CHECK(!ts.grid_open && g_wm_n == wm1); /* consumed, no key sent */
        /* FX flips the page to F1..F10 */
        ts_down(&ts, 505, sl->x + sl->w / 2, sl->y + sl->h / 2, W, H, now);
        const Btn *fx = &ts.grid[10];
        CHECK(fx->id == B_FX);
        ts_down(&ts, 506, fx->x + fx->w / 2, fx->y + fx->h / 2, W, H, now + 10);
        CHECK(ts.grid_open && ts.grid_page == 1 && ts.grid[0].dik == 0x3B);
        ts_up(&ts, 506);
        ts_up(&ts, 505);
        ts_down(&ts, 507, 12, 12, W, H, now + 20); /* close again */
        CHECK(!ts.grid_open && ts.grid_page == 1);
    }

    /* --- 6. ESC tap posts WM + DIK, auto-release --- */
    {
        int wm0 = g_wm_n;
        uint32_t e0 = vinput_key_edges();
        const Btn *esc = tbtn(&ts, B_ESC);
        ts_down(&ts, 601, esc->x + esc->w / 2, esc->y + esc->h / 2, W, H, now);
        CHECK(vinput_keys()[DIK_ESC]);
        CHECK(g_wm_n > wm0 && g_wm_vk[g_wm_n - 1] == 0x1B && g_wm_sc[g_wm_n - 1] == 0x01 &&
              g_wm_down[g_wm_n - 1] == 1);
        ts_tick(&ts, now + 200);
        CHECK(!vinput_keys()[DIK_ESC]);
        CHECK(g_wm_vk[g_wm_n - 1] == 0x1B && g_wm_down[g_wm_n - 1] == 0);
        CHECK(vinput_key_edges() == e0 + 2);
        ts_up(&ts, 601);
    }

    /* --- 7. hold keys (CORRER/INC) release with the finger --- */
    {
        const Btn *run = tbtn(&ts, B_CORRER);
        const Btn *cl = tbtn(&ts, B_INCL);
        ts_down(&ts, 701, run->x + run->w / 2, run->y + run->h / 2, W, H, now);
        ts_down(&ts, 702, cl->x + cl->w / 2, cl->y + cl->h / 2, W, H, now);
        CHECK(vinput_keys()[DIK_LSHIFT] && vinput_keys()[DIK_Q]);
        ts_tick(&ts, now + 500); /* holds are not auto-released */
        CHECK(vinput_keys()[DIK_LSHIFT] && vinput_keys()[DIK_Q]);
        ts_up(&ts, 701);
        ts_up(&ts, 702);
        CHECK(!vinput_keys()[DIK_LSHIFT] && !vinput_keys()[DIK_Q]);
    }

    /* --- 8. focus lost -> release all --- */
    {
        float sx = ts.zone[0] + ts.zone[2] * 0.5f, sy = ts.zone[1] + ts.zone[3] * 0.5f;
        const Btn *f = tbtn(&ts, B_FOGO);
        const Btn *esc = tbtn(&ts, B_ESC);
        ts_down(&ts, 801, sx, sy, W, H, now);
        ts_move(&ts, 801, sx + 60, sy - 60);
        ts_down(&ts, 802, f->x + 5, f->y + 5, W, H, now);
        ts_down(&ts, 803, esc->x + 5, esc->y + 5, W, H, now + 10);
        CHECK((vinput_mouse_buttons() & 1u) && vinput_keys()[DIK_W] && vinput_keys()[DIK_ESC]);
        ts_release_all(&ts, now + 30);
        CHECK(vinput_mouse_buttons() == 0);
        CHECK(!vinput_keys()[DIK_W] && !vinput_keys()[DIK_D] && !vinput_keys()[DIK_ESC]);
        CHECK(ts.npend == 0 && ts.active == 0 && !ts.grid_open);
        for (int i = 0; i < TOUCH_MAXF; i++) CHECK(ts.f[i].role == R_NONE);
        ts_up(&ts, 801); /* late UP events after release: ignored */
        CHECK(vinput_mouse_buttons() == 0 && !vinput_keys()[DIK_W]);
    }

    /* --- easing --- */
    {
        CHECK(ease_to(0.5f, 1.0f, 0.0f) == 0.5f);
        float a = ease_to(0.0f, 1.0f, 120.0f);
        CHECK(a > 0.9f && a <= 1.0f);
        CHECK(ease_to(0.0f, 1.0f, 16.0f) > 0.0f && ease_to(0.0f, 1.0f, 16.0f) < 0.5f);
        CHECK(ease_to(0.4f, 0.4f, 16.0f) == 0.4f);
    }

    /* --- every label is ASCII (the atlas has no accents) --- */
    {
        int ok = 1;
        for (int i = 0; i < ts.nbtn && ok; i++)
            for (const char *p = ts.btns[i].label; *p; p++)
                if ((uint8_t)*p < 32 || (uint8_t)*p > 126) ok = 0;
        CHECK(ok);
    }

    /* --- 9. stick hysteresis: jitter near the wedge boundary cannot flap --- */
    {
        float sx = ts.zone[0] + ts.zone[2] * 0.5f, sy = ts.zone[1] + ts.zone[3] * 0.5f;
        ts_down(&ts, 901, sx, sy, W, H, now);
        ts_move(&ts, 901, sx + 70, sy - 70); /* NE, inside the leash: W+D */
        CHECK(vinput_keys()[DIK_W] && vinput_keys()[DIK_D]);
        uint32_t e0 = vinput_key_edges();
        ts_move(&ts, 901, sx + 70, sy - 28); /* 21.8 deg: inside the hold band */
        CHECK(vinput_key_edges() == e0); /* no edge: the UP bit is kept alive */
        CHECK(vinput_keys()[DIK_W] && vinput_keys()[DIK_D]);
        ts_move(&ts, 901, sx + 70, sy - 14); /* 11.3 deg: below the off threshold */
        CHECK(vinput_key_edges() == e0 + 1); /* W released exactly once */
        CHECK(!vinput_keys()[DIK_W] && vinput_keys()[DIK_D]);
        ts_up(&ts, 901);
        CHECK(!vinput_keys()[DIK_D]);
    }

    /* --- 10. hold_crouch / hold_zoom switch AGACH and ZOOM between tap/hold --- */
    {
        ts.cfg.hold_crouch = 1;
        ts.nbtn = 0;
        ts.ngrid = 0;
        ts_ensure_layout(&ts, W, H, &ts.safe);
        const Btn *ag = tbtn(&ts, B_AGACH);
        CHECK(ag != NULL && ag->kind == K_KEY_HOLD);
        ts_down(&ts, 1001, ag->x + ag->w / 2, ag->y + ag->h / 2, W, H, now);
        ts_tick(&ts, now + 300);
        CHECK(vinput_keys()[DIK_C]); /* holds past the 120 ms mark */
        ts_up(&ts, 1001);
        CHECK(!vinput_keys()[DIK_C]);
        ts.cfg.hold_crouch = 0;
        ts.nbtn = 0;
        ts_ensure_layout(&ts, W, H, &ts.safe);
        const Btn *ag2 = tbtn(&ts, B_AGACH);
        CHECK(ag2 != NULL && ag2->kind == K_KEY_TAP);
        ts_down(&ts, 1002, ag2->x + ag2->w / 2, ag2->y + ag2->h / 2, W, H, now);
        ts_tick(&ts, now + 300);
        CHECK(!vinput_keys()[DIK_C]); /* tap released itself */
        ts_up(&ts, 1002);
    }

    /* --- 11. tap dedupe: pressing again inside the window adds no pend --- */
    {
        const Btn *rec = tbtn(&ts, B_REC);
        ts_down(&ts, 1101, rec->x + rec->w / 2, rec->y + rec->h / 2, W, H, now);
        ts_down(&ts, 1102, rec->x + rec->w / 2, rec->y + rec->h / 2, W, H, now + 10);
        CHECK(ts.npend == 1);
        ts_tick(&ts, now + 300);
        CHECK(ts.npend == 0 && !vinput_keys()[DIK_R]);
        ts_up(&ts, 1101);
        ts_up(&ts, 1102);
    }

    /* --- 12. pend overflow: the 9th distinct pending tap releases at once --- */
    {
        uint32_t e0 = vinput_key_edges();
        const int ids[9] = {B_USAR, B_REC, B_MODO, B_ZOOM, B_ESC, B_ESP, B_TAB, B_TROCA, B_AGACH};
        for (int i = 0; i < 9; i++) {
            const Btn *b = tbtn(&ts, ids[i]);
            CHECK(b != NULL);
            if (!b) continue;
            ts_down(&ts, 1300 + i, b->x + b->w / 2, b->y + b->h / 2, W, H, now + i);
        }
        CHECK(ts.npend == TOUCH_MAXPEND);
        CHECK(!vinput_keys()[DIK_C]); /* the overflow tap (AGACH) went down+up */
        CHECK(vinput_key_edges() == e0 + 9); /* 7 held diks + C down + C up */
        for (int i = 0; i < 9; i++) ts_up(&ts, 1300 + i);
        ts_tick(&ts, now + 400);
        CHECK(ts.npend == 0);
        CHECK(!vinput_keys()[DIK_R] && !vinput_keys()[DIK_X] && !vinput_keys()[DIK_Z] &&
              !vinput_keys()[DIK_ESC] && !vinput_keys()[DIK_SPACE] && !vinput_keys()[DIK_TAB] &&
              !vinput_keys()[DIK_DEL]);
    }

    /* --- 13. no two base buttons overlap (3 scales + mirrored) --- */
    {
        for (int pass = 0; pass < 4; pass++) {
            HudCfg c;
            hud_cfg_defaults(&c);
            c.scale = pass == 0 ? 0.5f : pass == 1 ? 1.0f : 2.0f;
            c.handed = pass == 3;
            Btn bs[15];
            CHECK(hud_layout(&c, W, H, &s, bs, 15) == 15);
            int ok = 1;
            for (int i = 0; i < 15 && ok; i++)
                for (int j = i + 1; j < 15 && ok; j++) {
                    const Btn *a = &bs[i], *b = &bs[j];
                    if (a->x < b->x + b->w && b->x < a->x + a->w &&
                        a->y < b->y + b->h && b->y < a->y + a->h) ok = 0;
                }
            CHECK(ok);
        }
    }

    printf(g_fail ? "\nSELFTEST FAILED (%d)\n" : "\nSELFTEST OK (%d failures)\n", g_fail);
    return g_fail ? 1 : 0;
}
#endif /* TOUCH_SELFTEST */

#ifndef TOUCH_SELFTEST
/* ------------------------------------------------------------------ */
/* Overlay glue: SDL events + D3D9 rendering                           */
/* ------------------------------------------------------------------ */

static TouchState g_ts;
static volatile int g_disabled;    /* fatal D3D failure: overlay off for good */
static int g_d3d_ok;               /* font texture ready */
static int g_logged_init;          /* one-shot init log */
static int g_font_fails;           /* retry counter for transient font failures */

static IDirect3DTexture9 *g_font;
static IDirect3DPixelShader9 *g_old_ps;
static IDirect3DVertexShader9 *g_old_vs;
static IDirect3DVertexDeclaration9 *g_old_decl;
static IDirect3DVertexBuffer9 *g_old_vb;
static IDirect3DBaseTexture9 *g_old_tex1;
static UINT g_old_vb_ofs, g_old_vb_str;

/* Manual state save. Every device state the overlay changes is captured
 * here and put back byte-exact after the draw, so the game's own state at
 * Present time is preserved (a D3DSBT_ALL state block would restore a
 * snapshot from init time, which may be stale by hundreds of frames). */
static struct {
    DWORD rs[30];
    DWORD tss[9];
    DWORD tss1;                      /* stage-1 COLOROP (multitexture safety) */
    DWORD samp[6];
    DWORD fvf;
} g_ms;
static const D3DRENDERSTATETYPE k_rs_save[30] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_SRCBLEND,
    D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_ALPHABLENDENABLE, D3DRS_CULLMODE,
    D3DRS_LIGHTING, D3DRS_STENCILENABLE, D3DRS_ALPHAFUNC, D3DRS_ALPHAREF,
    D3DRS_SRGBWRITEENABLE, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SCISSORTESTENABLE,
    D3DRS_COLORWRITEENABLE, D3DRS_FILLMODE, D3DRS_SHADEMODE, D3DRS_DITHERENABLE,
    D3DRS_SPECULARENABLE, D3DRS_BLENDFACTOR, D3DRS_TEXTUREFACTOR,
    D3DRS_NORMALIZENORMALS, D3DRS_MULTISAMPLEANTIALIAS, D3DRS_ANTIALIASEDLINEENABLE,
    D3DRS_TWOSIDEDSTENCILMODE, D3DRS_CCW_STENCILFAIL, D3DRS_DEPTHBIAS,
    D3DRS_FOGENABLE, D3DRS_WRAP0,
};
static const D3DTEXTURESTAGESTATETYPE k_tss_save[9] = {
    D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2,
    D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2,
    D3DTSS_TEXCOORDINDEX, D3DTSS_RESULTARG, D3DTSS_TEXTURETRANSFORMFLAGS,
};
static const D3DSAMPLERSTATETYPE k_samp_save[6] = {
    D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE,
};

/* SDL window size, asked fresh every time: the window is recreated when the
 * splash finishes, so nothing may cache it. */
static void win_size(int *w, int *h) {
    *w = *h = 0;
    SDL_Window *win = (SDL_Window *)platform_window();
    if (win) SDL_GetWindowSize(win, w, h);
}

/* Deduce notch/bars from the display usable bounds vs the window. */
static void overlay_safe(Safe *s, int w, int h) {
    SDL_Rect r;
    int wx, wy, ok = 0;
    SDL_Window *win = (SDL_Window *)platform_window();
    if (win && SDL_GetDisplayUsableBounds(0, &r) == 0) {
        SDL_GetWindowPosition(win, &wx, &wy);
        s->top = maxf(0.0f, (float)(r.y - wy));
        s->bottom = maxf(0.0f, (float)((wy + h) - (r.y + r.h)));
        s->left = maxf(0.0f, (float)(r.x - wx));
        s->right = maxf(0.0f, (float)((wx + w) - (r.x + r.w)));
        /* sanity cap: insets that eat the screen are bogus */
        ok = s->top + s->bottom < 0.4f * h && s->left + s->right < 0.4f * w;
    }
    if (!ok) hud_safe_defaults(s, w, h);
}

/* ------------------------- vertex building ------------------------- */

enum { VMAX = 512 };
typedef struct { float x, y, z, rhw; uint32_t c; float u, v; } Vtx;
static Vtx g_vb[VMAX * 6];
static int g_nv;

static void vq(float x0, float y0, float x1, float y1, uint32_t c, float u0, float v0, float u1, float v1) {
    if (g_nv + 6 > VMAX * 6) return; /* budget: drop, never crash */
    Vtx *v = g_vb + g_nv;
    g_nv += 6;
    v[0] = (Vtx){x0, y0, 0, 1, c, u0, v0};
    v[1] = (Vtx){x1, y0, 0, 1, c, u1, v0};
    v[2] = (Vtx){x0, y1, 0, 1, c, u0, v1};
    v[3] = (Vtx){x1, y0, 0, 1, c, u1, v0};
    v[4] = (Vtx){x1, y1, 0, 1, c, u1, v1};
    v[5] = (Vtx){x0, y1, 0, 1, c, u0, v1};
}

/* Cell 0 of the atlas (space) is solid white: sampling it multiplies the
 * vertex colour through unchanged, so fills need no shader switch. */
#define UV_SOLID 0.004f

static void box(float x, float y, float w, float h, uint32_t fill, uint32_t border) {
    vq(x, y, x + w, y + h, fill, UV_SOLID, UV_SOLID, UV_SOLID, UV_SOLID);
    float t = maxf(1.0f, w * 0.012f);
    vq(x, y, x + w, y + t, border, UV_SOLID, UV_SOLID, UV_SOLID, UV_SOLID);
    vq(x, y + h - t, x + w, y + h, border, UV_SOLID, UV_SOLID, UV_SOLID, UV_SOLID);
    vq(x, y + t, x + t, y + h - t, border, UV_SOLID, UV_SOLID, UV_SOLID, UV_SOLID);
    vq(x + w - t, y + t, x + w, y + h - t, border, UV_SOLID, UV_SOLID, UV_SOLID, UV_SOLID);
}

/* Fit a label into a rect using the 5x7 atlas: glyph pixel unit g gives
 * 7g height and (6*len-1)*g width; shrink g until both fit. */
static void text_in(float x, float y, float w, float h, const char *s, uint32_t c) {
    int len = 0;
    for (const char *p = s; *p; p++)
        if (*p != ' ') len++;
    if (!len) return;
    float g = minf(h * 0.5f / 7.0f, w * 0.9f / (6.0f * len - 1.0f));
    if (g <= 0) return;
    float adv = 6 * g, gw = 5 * g, gh = 7 * g;
    float tx = x + (w - ((6.0f * len - 1.0f) * g)) * 0.5f;
    float ty = y + (h - gh) * 0.5f;
    for (const char *p = s; *p; p++) {
        int idx = (uint8_t)*p - 32;
        if (idx < 0 || idx > 94) idx = 63; /* '?' for anything off-atlas */
        if (*p != ' ') {
            float u0 = (float)(idx & 15) * 8.0f / 128.0f + 0.002f;
            float v0 = (float)(idx >> 4) * 8.0f / 64.0f + 0.002f;
            float u1 = u0 + (5.0f - 0.04f) / 128.0f;
            float v1 = v0 + (7.0f - 0.04f) / 64.0f;
            vq(tx, ty, tx + gw, ty + gh, c, u0, v0, u1, v1);
        }
        tx += adv;
    }
}

static uint32_t shade(uint32_t rgb, float a) {
    uint32_t al = (uint32_t)(a * 255.0f + 0.5f);
    if (al > 255) al = 255;
    return (al << 24) | (rgb & 0xFFFFFF);
}

static int btn_pressed(const TouchState *ts, int id) {
    for (int i = 0; i < TOUCH_MAXF; i++)
        if (ts->f[i].role == R_BTN && ts->f[i].btn_id == id) return 1;
    return 0;
}

static void build_frame(TouchState *ts) {
    g_nv = 0;
    ts->fade = ease_to(ts->fade, 1.0f, 16.7f); /* one Present ~= one frame */
    float fade = ts->fade;

    for (int i = 0; i < ts->nbtn; i++) {
        const Btn *b = &ts->btns[i];
        int pressed = btn_pressed(ts, b->id);
        float target = pressed ? 1.0f : ts->cfg.opacity;
        float a = ease_to(ts->alpha[b->id], target, 16.7f);
        ts->alpha[b->id] = a;
        a *= fade;
        uint32_t fill = shade(pressed ? (b->id == B_FOGO ? 0xa03028 : 0x4a78b4) : 0x2a323c,
                              pressed ? maxf(a, 0.85f) : a);
        uint32_t border = shade(b->id == B_FOGO ? 0xc06050 : 0x7890a8, maxf(a, 0.5f));
        box(b->x, b->y, b->w, b->h, fill, border);
        if (ts->cfg.labels) text_in(b->x, b->y, b->w, b->h, b->label, shade(0xffffff, maxf(a, 0.6f)));
    }

    if (ts->grid_open) {
        /* drawn after the base layer: dim backdrop, panel, title, slots */
        vq(0, 0, (float)ts->w, (float)ts->h, shade(0x000000, 0.55f * fade),
           UV_SOLID, UV_SOLID, UV_SOLID, UV_SOLID);
        box(ts->panel[0], ts->panel[1], ts->panel[2], ts->panel[3],
            shade(0x18202c, 0.92f * fade), shade(0x5a7a9a, fade));
        text_in(ts->panel[0], ts->panel[1], ts->panel[2], 22 * hud_unit(&ts->cfg, ts->w, ts->h, &ts->safe),
                ts->grid_page ? "F1 - F10" : "1 - 0  EQUIPAR", shade(0xd8e4f0, fade));
        for (int i = 0; i < ts->ngrid; i++) {
            const Btn *b = &ts->grid[i];
            uint32_t fill = shade(b->id == B_FX ? 0x3a2a20 : 0x24303e, 0.85f * fade);
            box(b->x, b->y, b->w, b->h, fill, shade(0x8098b0, fade));
            if (ts->cfg.labels)
                text_in(b->x, b->y, b->w, b->h, b->label, shade(0xffffff, fade));
        }
    }

    /* floating stick: rim + nub, only while held */
    for (int i = 0; i < TOUCH_MAXF; i++) {
        if (ts->f[i].role != R_STICK) continue;
        float ox = ts->f[i].ox, oy = ts->f[i].oy, r = ts->stick_r;
        box(ox - r, oy - r, 2 * r, 2 * r, shade(0x101820, 0.20f * fade), shade(0x90a8c0, 0.55f * fade));
        float nx = ox + ts->stick_dx, ny = oy + ts->stick_dy;
        box(nx - r * 0.22f, ny - r * 0.22f, r * 0.44f, r * 0.44f,
            shade(0x6a9ad8, 0.80f * fade), shade(0xc8d8f0, 0.90f * fade));
    }
}

/* --------------------------- D3D9 draw ----------------------------- */

static int font_make(IDirect3DDevice9 *d) {
    if (FAILED(d->CreateTexture(128, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_font, NULL)))
        return 0;
    D3DLOCKED_RECT lr;
    if (FAILED(g_font->LockRect(0, &lr, NULL, 0)) || !lr.pBits) {
        g_font->Release();
        g_font = NULL;
        return 0;
    }
    uint32_t *px = (uint32_t *)lr.pBits;
    for (int row = 0; row < 8; row++)
        for (int cell = 0; cell < 16; cell++) {
            int idx = row * 16 + cell;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    int set;
                    if (idx > 94) set = 0;
                    else if (x >= 5 || y >= 7) set = 0;
                    else set = (k_font5x7[idx][x] >> y) & 1;
                    px[(row * 8 + y) * (lr.Pitch / 4) + cell * 8 + x] = set ? 0xFFFFFFFFu : 0;
                }
        }
    g_font->UnlockRect(0);
    return 1;
}

static void ms_save(IDirect3DDevice9 *d) {
    for (int i = 0; i < 30; i++) d->GetRenderState(k_rs_save[i], &g_ms.rs[i]);
    for (int i = 0; i < 9; i++) d->GetTextureStageState(0, k_tss_save[i], &g_ms.tss[i]);
    d->GetTextureStageState(1, D3DTSS_COLOROP, &g_ms.tss1);
    for (int i = 0; i < 6; i++) d->GetSamplerState(0, k_samp_save[i], &g_ms.samp[i]);
    d->GetFVF(&g_ms.fvf);
    /* Get* AddRefs the shader; released after the restore below. */
    d->GetPixelShader(&g_old_ps);
    d->GetVertexShader(&g_old_vs);
    /* dxvk's GetVertexDeclaration also returns a new reference (verified in
     * d3d9_device.cpp: *ppDecl = m_state.vertexDecl.ref()), like the shaders. */
    if (FAILED(d->GetVertexDeclaration(&g_old_decl))) g_old_decl = NULL;
    if (FAILED(d->GetStreamSource(0, &g_old_vb, &g_old_vb_ofs, &g_old_vb_str))) {
        g_old_vb = NULL;
        g_old_vb_ofs = g_old_vb_str = 0;
    }
    d->GetTexture(1, &g_old_tex1);
}

static void ms_apply(IDirect3DDevice9 *d) {
    d->SetPixelShader(NULL);
    d->SetVertexShader(NULL);
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetRenderState(D3DRS_WRAP0, 0);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    d->SetTextureStageState(0, D3DTSS_RESULTARG, D3DTA_CURRENT);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    d->SetTexture(0, g_font);
    /* UE2 has multitexture paths: a live stage-1 COLOROP would modulate the
     * HUD with whatever texture the game left bound there. */
    d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    d->SetTexture(1, NULL);
}

static void ms_restore(IDirect3DDevice9 *d) {
    for (int i = 0; i < 30; i++) d->SetRenderState(k_rs_save[i], g_ms.rs[i]);
    for (int i = 0; i < 9; i++) d->SetTextureStageState(0, k_tss_save[i], g_ms.tss[i]);
    for (int i = 0; i < 6; i++) d->SetSamplerState(0, k_samp_save[i], g_ms.samp[i]);
    d->SetPixelShader(g_old_ps);
    d->SetVertexShader(g_old_vs);
    d->SetFVF(g_ms.fvf);
    d->SetTextureStageState(1, D3DTSS_COLOROP, g_ms.tss1);
    d->SetTexture(1, g_old_tex1);
    if (g_old_tex1) { g_old_tex1->Release(); g_old_tex1 = NULL; }
    if (g_old_decl) { d->SetVertexDeclaration(g_old_decl); g_old_decl->Release(); g_old_decl = NULL; }
    if (g_old_vb) { d->SetStreamSource(0, g_old_vb, g_old_vb_ofs, g_old_vb_str); g_old_vb->Release(); g_old_vb = NULL; }
    if (g_old_ps) { g_old_ps->Release(); g_old_ps = NULL; }
    if (g_old_vs) { g_old_vs->Release(); g_old_vs = NULL; }
}

/* One-time per-device init: the font atlas. State save/restore happens on
 * every draw instead, so nothing else needs one-time setup. */
static int overlay_d3d_init(IDirect3DDevice9 *d) {
    if (!font_make(d)) {
        OLOG("overlay: font texture failed, overlay disabled");
        return 0;
    }
    return 1;
}

extern "C" int touch_available(void) {
    if (g_disabled) return 0;
    if (g_d3d_ok) return 1;
#ifdef __ANDROID__
    return 1; /* decided on the first Present; keeps phones on vsync */
#else
    const char *e = getenv("LB_TOUCH");
    return e && *e == '1'; /* desktop test runs opt in */
#endif
}

extern "C" void touch_notify_reset(void) {
    /* Font atlas lives in D3DPOOL_MANAGED and survives Reset; the per-draw
     * state save/restore captures whatever the device holds at Present time,
     * so there is nothing to rebuild. Kept as a hook for future needs. */
    OLOG("overlay: device reset (managed resources survive)");
}

extern "C" void touch_release_all(void) {
    ts_release_all(&g_ts, SDL_GetTicks());
}

extern "C" void touch_setup_guest(void) {
    char path[1100];
    snprintf(path, sizeof path, "%s/hud.ini", g_save_dir);
    ts_init(&g_ts, path);
    OLOG("overlay: hud.ini scale=%g opacity=%g sens=%g invert=%d handed=%d labels=%d stick_r=%g",
         (double)g_ts.cfg.scale, (double)g_ts.cfg.opacity, (double)g_ts.cfg.look_sens,
         g_ts.cfg.look_invert_y, g_ts.cfg.handed, g_ts.cfg.labels, (double)g_ts.cfg.stick_radius);
}

extern "C" void touch_handle_event(const SDL_Event *e) {
    if (g_disabled) return;
    switch (e->type) {
    case SDL_FINGERDOWN: {
        int w, h;
        win_size(&w, &h);
        if (w > 0 && h > 0)
            ts_down(&g_ts, (long)e->tfinger.fingerId, e->tfinger.x * w, e->tfinger.y * h, w, h,
                    SDL_GetTicks());
        break;
    }
    case SDL_FINGERMOTION: {
        Finger *f = finger_find(&g_ts, (long)e->tfinger.fingerId);
        if (f) /* window size already known from the DOWN */
            ts_move(&g_ts, (long)e->tfinger.fingerId, e->tfinger.x * g_ts.w,
                    e->tfinger.y * g_ts.h);
        break;
    }
    case SDL_FINGERUP: {
        Finger *f = finger_find(&g_ts, (long)e->tfinger.fingerId);
        if (f) ts_up(&g_ts, (long)e->tfinger.fingerId);
        break;
    }
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
            e->window.event == SDL_WINDOWEVENT_MINIMIZED ||
            e->window.event == SDL_WINDOWEVENT_LEAVE) {
            if (g_ts.active || g_ts.npend || g_ts.grid_open) {
                OLOG("overlay: window event %d, releasing all touches", e->window.event);
                touch_release_all();
            }
        }
        break;
    default:
        break; /* SDL_KEYDOWN etc. are handled elsewhere; SDL_TEXTINPUT ignored */
    }
}

extern "C" void touch_draw(void *dev) {
    IDirect3DDevice9 *d = (IDirect3DDevice9 *)dev;
    if (g_disabled || !d) return;

    if (!g_d3d_ok) {
        /* Transient failures (device lost while the app is backgrounded) are
         * retried on the next Present; only give up after 300 attempts. */
        if (!overlay_d3d_init(d)) {
            if (++g_font_fails == 1 || g_font_fails % 300 == 0)
                OLOG("overlay: font texture failed (attempt %d), retrying", g_font_fails);
            if (g_font_fails >= 300) {
                g_disabled = 1;
                OLOG("overlay: giving up after %d font failures", g_font_fails);
            }
            return;
        }
        g_d3d_ok = 1;
    }

    int w, h;
    win_size(&w, &h);
    if (w < 16 || h < 16) return;
    Safe s;
    overlay_safe(&s, w, h);
    ts_ensure_layout(&g_ts, w, h, &s);

    ts_tick(&g_ts, SDL_GetTicks());
    build_frame(&g_ts);
    if (!g_nv) return;

    if (!g_logged_init) {
        g_logged_init = 1;
        OLOG("overlay: init ok window %dx%d safe t%.0f b%.0f l%.0f r%.0f quads=%d "
             "u=%.2f stick r=%.0f dead=%.0f",
             w, h, s.top, s.bottom, s.left, s.right, g_nv / 6,
             (double)hud_unit(&g_ts.cfg, w, h, &s), (double)g_ts.stick_r, (double)g_ts.stick_dead);
    }

    /* Manual state save/restore around the draw: everything the overlay
     * touches is captured and put back byte-exact, including the texture on
     * stage 0 (not covered by render/sampler state captures). */
    IDirect3DBaseTexture9 *old_tex = NULL;
    d->GetTexture(0, &old_tex);
    ms_save(d);
    ms_apply(d);
    HRESULT hr = d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, g_nv / 3, g_vb, sizeof(Vtx));
    ms_restore(d);
    d->SetTexture(0, old_tex);
    if (old_tex) old_tex->Release();
    if (FAILED(hr) && hr != D3DERR_WASSTILLDRAWING)
        OLOG("overlay: DrawPrimitiveUP hr=%08lx", (unsigned long)hr);
}

#endif /* !TOUCH_SELFTEST */
