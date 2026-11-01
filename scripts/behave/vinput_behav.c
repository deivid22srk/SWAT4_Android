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
    n_key = n_keydir = n_mouse = 0;
    stub_ctx = 1;
    stub_movie = 0;
    stub_w = 1600; stub_h = 720;
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

    /* 7c. FIFO order survives cap=1 (how the windrv poll loop reads) */
    reset_all();
    dev = mouse_device();
    vinput_mouse_delta(2, 0);
    vinput_mouse_button(0, 1);
    a5[0] = dev;
    M32(pn) = 1;
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 1 && M32(buf) == 12 && M32(buf + 4) == 0x80); /* edge first */
    M32(pn) = 1;
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 1 && M32(buf) == 0 && M32(buf + 4) == 2u);    /* then X */
    M32(pn) = 1;
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 0);
    printf("behav: DI FIFO order across cap=1 calls: ok\n");

    /* 7d. DIGDD_PEEK reads without draining */
    reset_all();
    dev = mouse_device();
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

    /* 7e. GetDeviceState no longer consumes deltas (buttons only) */
    reset_all();
    dev = mouse_device();
    vinput_mouse_delta(9, -9);
    uint32_t st = gmalloc(32);
    a3[0] = dev; a3[1] = 16; a3[2] = st;
    run_shim(shim_dev_GetDeviceState, 3, a3);
    CHECK(M32(st) == 0 && M32(st + 4) == 0);      /* no delta theft */
    CHECK(M8(st + 12) == 0);                      /* button up */
    vinput_mouse_button(0, 1);
    run_shim(shim_dev_GetDeviceState, 3, a3);
    CHECK(M8(st + 12) == 0x80);                   /* button mirrors */
    a5[0] = dev; a5[1] = 20; a5[2] = buf; a5[3] = pn; a5[4] = 0;
    M32(pn) = 4;
    run_shim(shim_dev_GetDeviceData, 5, a5);
    CHECK(M32(pn) == 3);                          /* edge + X + Y, in order */
    CHECK(M32(buf) == 12 && M32(buf + 4) == 0x80);
    CHECK(M32(buf + 20) == 0 && M32(buf + 24) == 9u);
    CHECK(M32(buf + 40) == 4 && M32(buf + 44) == (uint32_t)-9);
    printf("behav: GetDeviceState buttons-only, deltas reach GetDeviceData: ok\n");

    printf(failures ? "vinput/dinput behav: %d FAILURES\n" : "vinput/dinput behav: all ok\n",
           failures);
    return failures ? 1 : 0;
}
