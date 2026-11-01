/* Behavioral test for the touch-HUD input path, host side (no game needed).
 *
 * Covers the exact contracts the field depends on:
 *   vinput (what the Java HUD feeds):
 *     - menu-context button taps deliver the WM click pair at the cursor
 *       AND mirror the DirectInput button state (the engine listens on
 *       both: WM for UWindow hover, DI buffered for IK_LeftMouse);
 *     - a tap BEFORE the first menu-finger move lands at the guest screen
 *       center instead of being dropped (field video 205831: first taps
 *       disappeared);
 *     - menu moves/clicks steer the engine cursor through the DI delta
 *       stream — the ONLY channel that moves it (field log shhxlog: 31
 *       WM_MOUSEMOVE + 31 WM clicks dispatched, cursor still frozen at
 *       0,0): closed-loop against a model of where the steering deltas
 *       put the engine cursor, so repeated steers push zero; the model
 *       tracks steering only (mission look does not move the GUI cursor
 *       — the GUI interaction only runs while menus are displayed), so
 *       a post-mission steer re-anchors from the last menu position;
 *     - mission-context taps stay DI-only (no WM double-fire);
 *     - key taps set the DIK state and post WM through the queue — or
 *       straight into the WndProc while a Bink movie is open (that loop
 *       never pumps);
 *   dinput (what the engine polls — the fix behind the dead menu HUD):
 *     - dev_GetDeviceData streams the mouse as buffered DIDEVICEOBJECTDATA:
 *       button edges as DIMOFS_BUTTON0..2 = 12..14 (0x80/0x00) and
 *       coalesced relative deltas as DIMOFS_X/Y = 0/4;
 *     - one event per returned entry, FIFO order survives cap=1 (how the
 *       windrv poll loop reads it), the stream drains to zero, DIGDD_PEEK
 *       reads without draining;
 *     - dev_GetDeviceState no longer steals the deltas (buttons only).
 *
 * Single translation unit: stubs first, then the runtime sources included
 * directly so the static SHIM functions stay reachable. C only (gcc), the
 * same way scripts/test_hud.sh compiles them.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <SDL2/SDL.h>  /* types for the SDL stubs below */
#include "x86.h"
#include "rt.h"        /* ShimDef/CPU/RT types so the stubs match exactly */

/* ---------------- stubs before the runtime sources ---------------- */
int g_log_level = 0; /* 1: show the runtime's own LOG lines */

/* guest memory: the same 4 GiB flat map main.c uses */
uint8_t *MEM;
static uint32_t g_brk = 0x20000000u; /* GUEST_HEAP_BASE */
uint32_t gmalloc(uint32_t size) {
    if (!size) size = 1;
    size = (size + 15u) & ~15u;
    uint32_t p = g_brk;
    g_brk += size;
    if (g_brk > 0x38000000u) abort();
    return p;
}
void gfree(uint32_t p) { (void)p; }

uint32_t rt_thunk(ShimFn fn, const char *name) { (void)fn; (void)name; return 0xFF000000u; }
void rt_register_shims(const ShimDef *defs) { (void)defs; }
void rt_register_shims_as(const char *dll, const ShimDef *defs) { (void)dll; (void)defs; }
void rt_fatal(const char *fmt, ...) { fprintf(stderr, "rt_fatal: %s\n", fmt); abort(); }
uint32_t rt_invoke(CPU *c, uint32_t fn, int nargs, const uint32_t *args) {
    (void)c; (void)fn; (void)nargs; (void)args; return 0;
}
CPU *rt_current_cpu(void) { return NULL; }

/* SDL bits dinput.c uses (no real SDL here) */
const Uint8 *platform_keys(int *n) { *n = 0; return NULL; }
static uint32_t fake_ticks = 1000;
Uint32 SDL_GetTicks(void) { return fake_ticks++; }
Uint32 SDL_GetRelativeMouseState(int *x, int *y) { *x = 0; *y = 0; return 0; } /* no physical mouse */

/* vinput.c externs, recorded instead of delivered */
static uint32_t rec_key[64][3];    static int n_key;
static uint32_t rec_keydir[64][3]; static int n_keydir;
static uint32_t rec_mouse[64][3];  static int n_mouse;
void user32_key_event(uint32_t vk, uint32_t sc, int down) {
    if (n_key < 64) { rec_key[n_key][0] = vk; rec_key[n_key][1] = sc; rec_key[n_key][2] = (uint32_t)down; n_key++; }
}
void user32_key_direct(uint32_t vk, uint32_t sc, int down) {
    if (n_keydir < 64) { rec_keydir[n_keydir][0] = vk; rec_keydir[n_keydir][1] = sc; rec_keydir[n_keydir][2] = (uint32_t)down; n_keydir++; }
}
void user32_mouse_msg(uint32_t msg, uint32_t wp, uint32_t lp) {
    if (n_mouse < 64) { rec_mouse[n_mouse][0] = msg; rec_mouse[n_mouse][1] = wp; rec_mouse[n_mouse][2] = lp; n_mouse++; }
}
static int stub_ctx = 1;   /* HUD_CTX_MENU */
static int stub_movie = 0; /* no .bik open */
int hud_game_ctx(void) { return stub_ctx; }
int hud_movie_playing(void) { return stub_movie; }
static int stub_w = 1600, stub_h = 720;
void user32_get_screen(int *w, int *h) { if (w) *w = stub_w; if (h) *h = stub_h; }
int input_pad_count(void) { return 0; } /* no physical pads in the test */

/* ---------------- the sources under test ---------------- */
#include "../../runtime/vinput.c"
#include "../../runtime/dinput.c"

/* ---------------- tiny check harness ---------------- */
static int failures;
#define CHECK(x) do { \
    if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } \
} while (0)

static void reset_all(void) {
    s_mx = 0; s_my = 0;
    s_btn = 0;
    memset(s_keys, 0, sizeof s_keys);
    s_edges = 0;
    s_cur_x = 0; s_cur_y = -1; s_cur_left = 0;
    s_eng_mx = 0; s_eng_my = 0; s_steers = 0; s_steer_logged = 0;
    n_key = n_keydir = n_mouse = 0;
    stub_ctx = 1;
    stub_movie = 0;
    stub_w = 1600; stub_h = 720;
}

/* Park the menu cursor at the guest screen center and drain the steering
 * deltas (models the windrv poll between frames — in the field the engine
 * consumes them long before the next tap): the DI stream below then holds
 * only what each subtest feeds. */
static void park_cursor_center(void) {
    vinput_menu_move(stub_w / 2, stub_h / 2);
    int dx, dy;
    vinput_mouse_take(&dx, &dy);
}

/* Run one shim call with args placed in guest memory (ARG(n) reads them
 * from esp+4+4n, above the fake return address). */
static CPU the_cpu;
static uint32_t run_shim(ShimFn fn, int nargs, const uint32_t *args) {
    uint32_t esp = 0x30000000u;
    M32(esp) = 0x00DEAD00u;
    for (int i = 0; i < nargs; i++) M32(esp + 4 + 4 * i) = args[i];
    memset(&the_cpu, 0, sizeof the_cpu);
    the_cpu.r[4] = esp;
    fn(&the_cpu);
    return the_cpu.r[0];
}

/* A DEV_MOUSE DirectInput device (guest-side COM object -> DIDev). */
static uint32_t mouse_device(void) {
    static HComClass k;
    DIDev *d = calloc(1, sizeof *d);
    d->kind = DEV_MOUSE;
    return hcom_new(&k, d);
}

int main(void) {
    MEM = mmap(NULL, 1ull << 32, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (MEM == MAP_FAILED) { perror("mmap"); return 2; }

    uint32_t buf = gmalloc(128), pn = gmalloc(4);
    uint32_t a5[5], a3[3];

    /* ---- 1. menu tap with no cursor placed yet: WM pair at screen center */
    reset_all();
    vinput_mouse_button(0, 1);
    CHECK(n_mouse == 1);
    CHECK(rec_mouse[0][0] == 0x201);            /* WM_LBUTTONDOWN */
    CHECK((rec_mouse[0][2] & 0xFFFF) == 800);   /* x = 1600/2 */
    CHECK((rec_mouse[0][2] >> 16) == 360);      /* y = 720/2 */
    CHECK(rec_mouse[0][1] == 0x0001);           /* MK_LBUTTON */
    CHECK((vinput_mouse_buttons() & 1u) == 1u); /* DI mirror is DOWN */
    vinput_mouse_button(0, 0);
    CHECK(n_mouse == 2);
    CHECK(rec_mouse[1][0] == 0x202);            /* WM_LBUTTONUP */
    CHECK((vinput_mouse_buttons() & 1u) == 0u);
    printf("behav: menu tap lands at the screen center: ok\n");

    /* ---- 2. real edges only: duplicate DOWN/UP adds nothing */
    vinput_mouse_button(0, 1);                    /* real edge: posts */
    int base = n_mouse;
    vinput_mouse_button(0, 1);                    /* duplicate: nothing */
    CHECK(n_mouse == base);
    vinput_mouse_button(0, 0);                    /* real edge: posts */
    CHECK(n_mouse == base + 1);
    vinput_mouse_button(0, 0);                    /* duplicate: nothing */
    CHECK(n_mouse == base + 1);
    printf("behav: menu click edge dedupe: ok\n");

    /* ---- 3. free-finger menu move: absolute WM_MOUSEMOVE, no clicks */
    reset_all();
    vinput_menu_move(320, 180);
    CHECK(n_mouse == 1);
    CHECK(rec_mouse[0][0] == 0x200);            /* WM_MOUSEMOVE */
    CHECK((rec_mouse[0][2] & 0xFFFF) == 320);
    CHECK((rec_mouse[0][2] >> 16) == 180);
    vinput_menu_move(320, 180);                 /* same spot: deduped */
    CHECK(n_mouse == 1);
    printf("behav: menu mouse move + dedupe: ok\n");

    /* ---- 4. mission context: DI only, zero WM mouse traffic */
    reset_all();
    stub_ctx = 2; /* HUD_CTX_MISSION */
    vinput_mouse_button(0, 1);
    CHECK(n_mouse == 0);
    CHECK((vinput_mouse_buttons() & 1u) == 1u);
    vinput_mouse_button(0, 0);
    CHECK(n_mouse == 0);
    CHECK(vinput_mouse_buttons() == 0u);
    printf("behav: mission taps stay DI-only: ok\n");

    /* ---- 5. key taps: DIK state + WM through the queue */
    reset_all();
    vinput_key_tap(0xC8 /* DIK_UP */, 1);
    CHECK(vinput_keys()[0xC8] == 1);
    CHECK(n_key == 1 && rec_key[0][0] == 0x26 /* VK_UP */ && rec_key[0][2] == 1);
    CHECK(n_keydir == 0);
    vinput_key_tap(0xC8, 0);
    CHECK(vinput_keys()[0xC8] == 0);
    CHECK(n_key == 2 && rec_key[1][2] == 0);
    printf("behav: key tap -> DIK state + queued WM: ok\n");

    /* ---- 6. key taps while a movie plays: straight to the WndProc */
    reset_all();
    stub_movie = 1;
    vinput_key_tap(0x01 /* DIK_ESC */, 1);
    CHECK(n_key == 0);                            /* queue bypassed */
    CHECK(n_keydir == 1 && rec_keydir[0][0] == 0x1B && rec_keydir[0][2] == 1);
    vinput_key_tap(0x01, 0);
    CHECK(n_keydir == 2 && rec_keydir[1][2] == 0);
    stub_movie = 0;
    printf("behav: movie-phase key taps dispatch directly: ok\n");

    /* ---- 7. dinput: the buffered mouse stream the engine polls */
    reset_all();
    uint32_t dev = mouse_device();
    CHECK(dev != 0);
    park_cursor_center(); /* clicks must not inject steering deltas here */

    /* 7a. one click = two edges (down 0x80, up 0x00), one per call */
    vinput_mouse_button(0, 1);
    a5[0] = dev; a5[1] = 20; a5[2] = buf; a5[3] = pn; a5[4] = 0;
    M32(pn) = 1; /* the guest asks for one event per call */
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 1);
    CHECK(M32(buf) == 12);        /* DIMOFS_BUTTON0 */
    CHECK(M32(buf + 4) == 0x80);  /* pressed */
    M32(pn) = 1;
    vinput_mouse_button(0, 0);
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 1);
    CHECK(M32(buf) == 12);
    CHECK(M32(buf + 4) == 0x00);  /* released */
    M32(pn) = 1;
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 0);          /* drained: exactly the edges, nothing more */
    printf("behav: DI buffered click edges (12: 0x80 -> 0x00): ok\n");

    /* 7b. deltas coalesce and stream as DIMOFS_X then DIMOFS_Y */
    reset_all();
    dev = mouse_device();
    vinput_mouse_delta(5, 0);
    vinput_mouse_delta(-3, -7);   /* two posts before any poll */
    a5[0] = dev;
    M32(pn) = 4;                  /* enough room: X and Y in one call */
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 2);
    CHECK(M32(buf) == 0 && M32(buf + 4) == 2u);   /* dx = 5 + -3, DIMOFS_X */
    CHECK(M32(buf + 20) == 4);                    /* DIMOFS_Y */
    CHECK(M32(buf + 24) == (uint32_t)-7);         /* sign-extended */
    M32(pn) = 4;
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 0);
    printf("behav: DI buffered coalesced deltas (0/4): ok\n");

    /* 7c. FIFO order survives cap=1 (how the windrv poll loop reads),
     * and deltas LEAD the button edge inside one poll: a tap's steer
     * deltas and press edge pend in the same poll (hud_drain at Present,
     * windrv poll next frame tick) — deltas must lead or the click fires
     * at the stale cursor. The model ignores raw look deltas, so the
     * click here adds no steering of its own. */
    reset_all();
    dev = mouse_device();
    park_cursor_center();
    vinput_mouse_delta(2, 0);
    vinput_mouse_button(0, 1);
    a5[0] = dev;
    M32(pn) = 1;
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 1 && M32(buf) == 0 && M32(buf + 4) == 2u);    /* X first */
    M32(pn) = 1;
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 1 && M32(buf) == 12 && M32(buf + 4) == 0x80); /* then edge */
    M32(pn) = 1;
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 0);
    printf("behav: DI FIFO order across cap=1 calls: ok\n");

    /* 7d. DIGDD_PEEK reads without draining */
    reset_all();
    dev = mouse_device();
    park_cursor_center();
    vinput_mouse_button(1, 1);                    /* right button */
    a5[0] = dev; a5[1] = 20; a5[2] = buf; a5[3] = pn;
    M32(pn) = 4;
    a5[4] = 1; /* DIGDD_PEEK: transitions are visible... */
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 1 && M32(buf) == 13 && M32(buf + 4) == 0x80);
    M32(pn) = 4;
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 1 && M32(buf) == 13);        /* ...but stay buffered */
    a5[4] = 0;
    M32(pn) = 4;
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 1 && M32(buf) == 13);        /* non-peek drains */
    M32(pn) = 4;
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 0);
    printf("behav: DI DIGDD_PEEK sees but leaves the buffer: ok\n");

    /* 7e. GetDeviceState no longer consumes deltas (buttons only), and
     * the buffered stream one poll reads is [X, Y, edge] — deltas lead
     * the press edge in the same call (the model ignores look deltas, so
     * the click adds no steering here). */
    reset_all();
    dev = mouse_device();
    park_cursor_center();
    uint32_t st = gmalloc(32);
    a3[0] = dev; a3[1] = 16; a3[2] = st;
    run_shim(shim_dev_GetDeviceState, 3, a3);
    CHECK(M32(st) == 0 && M32(st + 4) == 0);      /* no delta theft */
    CHECK(M8(st + 12) == 0);                      /* button up */
    vinput_mouse_delta(9, -9);
    vinput_mouse_button(0, 1);
    run_shim(shim_dev_GetDeviceState, 3, a3);
    CHECK(M8(st + 12) == 0x80);                   /* button mirrors */
    a5[0] = dev; a5[1] = 20; a5[2] = buf; a5[3] = pn; a5[4] = 0;
    M32(pn) = 4;
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 3);                          /* X + Y + edge, in order */
    CHECK(M32(buf) == 0 && M32(buf + 4) == 9u);   /* DIMOFS_X */
    CHECK(M32(buf + 20) == 4 && M32(buf + 24) == (uint32_t)-9); /* DIMOFS_Y */
    CHECK(M32(buf + 40) == 12 && M32(buf + 44) == 0x80);        /* then BUTTON0 */
    printf("behav: GetDeviceState buttons-only, deltas reach GetDeviceData: ok\n");

    /* ---- 8. closed-loop cursor steering: the GUI cursor only moves via
     * the DI delta stream (field log shhxlog: WM_MOUSEMOVE alone left it
     * frozen at 0,0), so the menu path must steer it to the touch point */

    /* 8a. menu move steers exactly once and posts the WM */
    reset_all();
    g_log_level = 1; /* show the one-shot first-steer LOG line once */
    vinput_menu_move(100, 50);
    g_log_level = 0;
    int tdx, tdy;
    vinput_mouse_take(&tdx, &tdy);
    CHECK(tdx == 100 && tdy == 50);
    vinput_mouse_take(&tdx, &tdy);
    CHECK(tdx == 0 && tdy == 0);                 /* exactly once */
    CHECK(n_mouse == 1);
    CHECK(rec_mouse[0][0] == 0x200);             /* WM_MOUSEMOVE */
    CHECK((rec_mouse[0][2] & 0xFFFF) == 100);
    CHECK((rec_mouse[0][2] >> 16) == 50);
    int ex, ey; uint32_t steers;
    vinput_cursor_stats(&ex, &ey, &steers);
    CHECK(ex == 100 && ey == 50 && steers == 1); /* model landed on target */
    vinput_cursor_stats(NULL, NULL, NULL);       /* NULLs tolerated */
    printf("behav: menu move steers the engine cursor once: ok\n");

    /* 8b. repeated move to the SAME spot: no double steering */
    vinput_menu_move(100, 50);
    vinput_mouse_take(&tdx, &tdy);
    CHECK(tdx == 0 && tdy == 0);
    CHECK(n_mouse == 1);                         /* WM still deduped */
    vinput_cursor_stats(NULL, NULL, &steers);
    CHECK(steers == 1);                          /* zero-delta steers don't count */
    printf("behav: repeated menu move does not re-steer: ok\n");

    /* 8c. mission-look resync: the model IGNORES look deltas (UE2's GUI
     * cursor is moved only by the GUI while menus are displayed) — after
     * a mission the first menu move re-steers from the LAST MENU
     * POSITION, not from wherever looking drifted to */
    reset_all();
    vinput_menu_move(400, 300);                  /* last menu position (a,b) */
    vinput_mouse_take(&tdx, &tdy);
    CHECK(tdx == 400 && tdy == 300);             /* its steer, polled */
    vinput_mouse_delta(500, 400);                /* mission look */
    vinput_mouse_take(&tdx, &tdy);
    CHECK(tdx == 500 && tdy == 400);             /* the engine polls it every frame */
    vinput_menu_move(100, 50);
    vinput_mouse_take(&tdx, &tdy);
    CHECK(tdx == 100 - 400 && tdy == 50 - 300);  /* (100-a, 50-b): look ignored */
    vinput_cursor_stats(&ex, &ey, NULL);
    CHECK(ex == 100 && ey == 50);                /* model at the new target */
    printf("behav: menu move re-steers after mission look from the last menu position: ok\n");

    /* 8d. click with the cursor unset: the center fallback steers too,
     * and the tap's stream shape in ONE poll is deltas-then-edge (the F1
     * fix: hud_drain applies the steer + press together at Present, the
     * windrv poll reads both in one GetDeviceData call) */
    reset_all();
    dev = mouse_device();
    vinput_mouse_button(0, 1);
    CHECK(n_mouse == 1 && rec_mouse[0][0] == 0x201); /* WM pair unchanged */
    a5[0] = dev; a5[1] = 20; a5[2] = buf; a5[3] = pn; a5[4] = 0;
    M32(pn) = 4;
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 3);                         /* X, Y, edge — deltas lead */
    CHECK(M32(buf) == 0 && M32(buf + 4) == (uint32_t)(stub_w / 2));
    CHECK(M32(buf + 20) == 4 && M32(buf + 24) == (uint32_t)(stub_h / 2));
    CHECK(M32(buf + 40) == 12 && M32(buf + 44) == 0x80);
    vinput_mouse_take(&tdx, &tdy);
    CHECK(tdx == 0 && tdy == 0);                 /* the poll drained the steer */
    vinput_mouse_button(0, 0);
    vinput_mouse_take(&tdx, &tdy);
    CHECK(tdx == 0 && tdy == 0);                 /* up edge adds no deltas */
    printf("behav: first click steers the center fallback: ok\n");

    /* 8e. click at the current cursor after a move: no extra deltas, the
     * DI button edge still mirrors */
    reset_all();
    vinput_menu_move(100, 50);
    vinput_mouse_take(&tdx, &tdy);               /* the move's steer, polled */
    CHECK(tdx == 100 && tdy == 50);
    vinput_mouse_button(0, 1);
    vinput_mouse_take(&tdx, &tdy);
    CHECK(tdx == 0 && tdy == 0);                 /* already there */
    CHECK((vinput_mouse_buttons() & 1u) == 1u);  /* DI bit mirrors */
    vinput_mouse_button(0, 0);
    CHECK((vinput_mouse_buttons() & 1u) == 0u);
    vinput_mouse_take(&tdx, &tdy);
    CHECK(tdx == 0 && tdy == 0);
    printf("behav: click at the cursor adds no deltas, DI mirror intact: ok\n");

    /* 8f. buttons 1/2 share the closed-loop cursor */
    reset_all();
    vinput_mouse_button(1, 1);                   /* right, cursor unset */
    vinput_mouse_take(&tdx, &tdy);
    CHECK(tdx == stub_w / 2 && tdy == stub_h / 2);
    vinput_mouse_button(1, 0);
    vinput_mouse_take(&tdx, &tdy);
    CHECK(tdx == 0 && tdy == 0);
    vinput_menu_move(100, 50);
    vinput_mouse_take(&tdx, &tdy);
    /* the fallback click left the model at the center: the move steers
     * from there, not from 0,0 (closed-loop, not absolute) */
    CHECK(tdx == 100 - stub_w / 2 && tdy == 50 - stub_h / 2);
    vinput_mouse_button(2, 1);                   /* middle at the cursor */
    vinput_mouse_take(&tdx, &tdy);
    CHECK(tdx == 0 && tdy == 0);
    CHECK((vinput_mouse_buttons() & 4u) == 4u);
    vinput_mouse_button(2, 0);
    printf("behav: right/middle buttons steer like the left: ok\n");

    printf(failures ? "vinput/dinput behav: %d FAILURES\n" : "vinput/dinput behav: all ok\n",
           failures);
    return failures ? 1 : 0;
}
