/* Host platform layer on SDL2: window (Vulkan surface for DXVK), event pump.
 * The same code runs on desktop Linux and on Android. */
#include "game.h"
#include "rt.h"
#include "vinput.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <math.h>
#include <unistd.h>   /* _exit: teardown-free quit (see SDL_QUIT below) */
#ifndef __ANDROID__
#include <fcntl.h>
#endif

static SDL_Window *g_window;
static int g_frames;
static void load_key_script(void);

/* ------------------------------------------------------------------ */
/* Loading splash: the UE2 boot loads ~300 packages before the first
 * D3D frame; on a phone that is minutes of pure black. Show the game's
 * own StartupLogo.bmp plus a bind-count progress bar on the platform
 * window from process start until guest D3D9 CreateDevice. The window
 * surface is destroyed again before DXVK creates its Vulkan swapchain
 * (SDL_DestroyWindowSurface releases any software/texture framebuffer
 * the splash used), so the two never fight over the native window. */
static SDL_Thread *g_splash_thread;
static SDL_mutex *g_splash_mx;
static SDL_Surface *g_splash_logo;
static volatile int g_splash_quit;
static volatile int g_splash_binds;  /* LoadLibrary count -> progress */
static volatile int g_splash_loads;  /* "Loading file" events -> package preload progress */

static void splash_draw(void) {
    SDL_Surface *s = SDL_GetWindowSurface(g_window);
    if (!s) return;
    SDL_FillRect(s, NULL, SDL_MapRGB(s->format, 10, 10, 14));
    if (g_splash_logo && g_splash_logo->w <= s->w) {
        SDL_Rect dst;
        dst.w = g_splash_logo->w;
        dst.h = g_splash_logo->h;
        if (dst.w > s->w * 2 / 3) {
            /* only shrink via SDL_BlitScaled; never upscale a 512px logo */
            dst.w = s->w * 2 / 3;
            dst.h = g_splash_logo->h * dst.w / g_splash_logo->w;
            dst.x = (s->w - dst.w) / 2;
            dst.y = (s->h - dst.h) / 2 - s->h / 12;
            SDL_BlitScaled(g_splash_logo, NULL, s, &dst);
        } else {
            dst.x = (s->w - dst.w) / 2;
            dst.y = (s->h - dst.h) / 2 - s->h / 12;
            SDL_BlitSurface(g_splash_logo, NULL, s, &dst);
        }
        /* progress: p = 1 - e^(-binds/90), capped at 95% until the first frame.
         * The DLL binds stop after module init, and the boot then spends its
         * real time in the package preload (~160 "Loading file" events) with
         * no new binds — that is the frozen bar the user saw. Carry the bar
         * from ~66% (≈95 binds) to 95% over the preload. */
        int n = g_splash_binds;
        int pct = 100 - (int)(100.0 * exp(-n / 90.0));
        {
            int loads = g_splash_loads < 160 ? g_splash_loads : 160;
            int pl = 66 + loads * 29 / 160;
            if (pl > pct) pct = pl;
        }
        if (pct > 95) pct = 95;
        if (pct < 2) pct = 2;
        int bw = dst.w * 3 / 4, bx = (s->w - bw) / 2, by = dst.y + dst.h + s->h / 24, bh = 6;
        SDL_Rect frame = {bx - 1, by - 1, bw + 2, bh + 2};
        SDL_FillRect(s, &frame, SDL_MapRGB(s->format, 80, 84, 96));
        SDL_Rect inner = {bx, by, bw, bh};
        SDL_FillRect(s, &inner, SDL_MapRGB(s->format, 10, 10, 14));
        SDL_Rect fill = {bx, by, bw * pct / 100, bh};
        if (fill.w > 0) SDL_FillRect(s, &fill, SDL_MapRGB(s->format, 46, 116, 181));
    }
    SDL_UpdateWindowSurface(g_window);
}

static int SDLCALL splash_thread(void *arg) {
    (void)arg;
    while (!g_splash_quit) {
        if (SDL_LockMutex(g_splash_mx) == 0) {
            splash_draw();
            SDL_PumpEvents();  /* keep the WM/app responsive during the load */
            SDL_UnlockMutex(g_splash_mx);
        }
        SDL_Delay(120);
        fflush(stderr);  /* the log is fully buffered; keep it fresh on kills */
    }
    return 0;
}

static Uint32 g_window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN;

static void splash_init(int w, int h) {
    (void)w; (void)h;
    if (!(SDL_GetWindowFlags(g_window) & SDL_WINDOW_SHOWN)) return;
    extern char g_game_dir[];
    char path[1400];
    snprintf(path, sizeof path, "%s/Content/System/Logos/StartupLogo.bmp", g_game_dir);
    g_splash_logo = SDL_LoadBMP(path);
    if (!g_splash_logo) {
        LOG("splash: no logo (%s: %s), showing plain background", path, SDL_GetError());
        g_splash_logo = SDL_CreateRGBSurfaceWithFormat(0, 256, 64, 32, SDL_PIXELFORMAT_ABGR8888);
        if (g_splash_logo) SDL_FillRect(g_splash_logo, NULL, SDL_MapRGB(g_splash_logo->format, 10, 10, 14));
    }
    g_splash_mx = SDL_CreateMutex();
    g_splash_quit = 0;
    g_splash_thread = SDL_CreateThread(splash_thread, "splash", NULL);
    LOG("splash: up");
}

/* Called from the guest D3D9 CreateDevice bridge, before DXVK touches the
 * window. The texture-framebuffer splash drove the window through a GL
 * renderer, which strips SDL_WINDOW_VULKAN (and an EGL/GLX context) from
 * it — so tear the splash down and recreate the window fresh; DXVK then
 * gets a clean Vulkan-capable window from platform_window(). */
void splash_finish(void) {
    if (!g_splash_thread) return;
    g_splash_quit = 1;
    SDL_WaitThread(g_splash_thread, NULL);
    g_splash_thread = NULL;
    SDL_DestroyMutex(g_splash_mx);
    g_splash_mx = NULL;
    if (g_splash_logo) { SDL_FreeSurface(g_splash_logo); g_splash_logo = NULL; }
    int w = 1280, h = 720;
    SDL_GetWindowSize(g_window, &w, &h);
    SDL_DestroyWindow(g_window);
    g_window = SDL_CreateWindow(GAME_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                w, h, g_window_flags);
    if (!g_window) rt_fatal("SDL_CreateWindow after splash: %s", SDL_GetError());
    /* Android may land the recreated window smaller than the display mode
     * (camera cutout: display 2400x1080, window 2285x1080). The guest's
     * client area must match the WINDOW pixels, not the display mode, or
     * the menu cursor coordinates from the touch overlay are off by the
     * letterbox ratio. */
    SDL_GetWindowSize(g_window, &w, &h);
    extern void user32_set_screen(int, int);
    user32_set_screen(w, h);
    LOG("splash: done (%d dll binds); window recreated flags=%x size %dx%d",
        g_splash_binds, SDL_GetWindowFlags(g_window), w, h);
}

void splash_bind_bump(void) { g_splash_binds++; }
void splash_load_bump(void) { g_splash_loads++; }
/* ------------------------------------------------------------------ */

void platform_init(void) {
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0)
        rt_fatal("SDL_Init: %s", SDL_GetError());
    /* DXVK's SDL2 WSI asks for instance extensions before any surface
     * exists; SDL only exposes them once libvulkan is loaded. */
    if (!SDL_Vulkan_LoadLibrary(NULL))
        LOG("SDL_Vulkan_LoadLibrary failed: %s (D3D9 will not start)", SDL_GetError());
    int w = 1280, h = 720;
    Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN;
    if (getenv("LB_WINDOW")) sscanf(getenv("LB_WINDOW"), "%dx%d", &w, &h);  /* desktop testing */
#ifdef __ANDROID__
    flags |= SDL_WINDOW_FULLSCREEN;
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0) { w = dm.w; h = dm.h; }
#endif
    g_window = SDL_CreateWindow(GAME_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!g_window) {
        /* Headless fallback (CI/desktop sem GPU): tenta de novo sem a
         * exigência de superfície Vulkan; o D3D9 vai falhar depois e o jogo
         * segue pelo caminho de erro normal do UE2. */
        LOG("SDL Vulkan window indisponível (%s); tentando janela headless", SDL_GetError());
        flags &= ~(Uint32)SDL_WINDOW_VULKAN;
        flags &= ~(Uint32)SDL_WINDOW_SHOWN;
        g_window = SDL_CreateWindow(GAME_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
        if (!g_window) rt_fatal("SDL_CreateWindow: %s", SDL_GetError());
    }
    g_window_flags = flags;  /* kept for the post-splash window recreate */
    extern void user32_set_screen(int, int);
    user32_set_screen(w, h);
    LOG("platform: window %dx%d", w, h);
    splash_init(w, h);
    load_key_script();
}

void *platform_window(void) { return g_window; }

/* Scripted input for automated runs: LB_KEYS="frame:Key[:frames],..." holds
 * the SDL-named key (e.g. Return, Space, Left) from that frame on. */
typedef struct { int frame, len; SDL_Scancode sc; } KeyEv;
static KeyEv g_script[256];
static int g_nscript;
static Uint8 g_keys[SDL_NUM_SCANCODES];
static int g_padonly;  /* LB_PADONLY=1: scripted keys drive only the virtual XInput pad */

static void load_key_script(void) {
    const char *s = getenv("LB_KEYS");
    g_padonly = getenv("LB_PADONLY") && *getenv("LB_PADONLY") == '1';
    while (s && *s && g_nscript < 256) {
        char name[32] = "";
        int fr = 0, len = 10, n = 0;
        if (sscanf(s, "%d:%31[^:,]%n", &fr, name, &n) < 2) break;
        s += n;
        if (*s == ':') { s++; len = (int)strtol(s, (char **)&s, 10); }
        SDL_Scancode sc = SDL_GetScancodeFromName(name);
        if (sc != SDL_SCANCODE_UNKNOWN) g_script[g_nscript++] = (KeyEv){fr, len, sc};
        else LOG("LB_KEYS: unknown key %s", name);
        if (*s == ',') s++;
    }
}

/* SDL scancode -> Windows virtual-key code */
static uint32_t vk_of(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return 'A' + (s - SDL_SCANCODE_A);
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return '1' + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return 0x70 + (s - SDL_SCANCODE_F1);
    switch (s) {
    case SDL_SCANCODE_0: return '0';
    case SDL_SCANCODE_RETURN: return 0x0D;
    case SDL_SCANCODE_ESCAPE: return 0x1B;
    case SDL_SCANCODE_BACKSPACE: return 0x08;
    case SDL_SCANCODE_TAB: return 0x09;
    case SDL_SCANCODE_SPACE: return 0x20;
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return 0x10;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x11;
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x12;
    case SDL_SCANCODE_LEFT: return 0x25;
    case SDL_SCANCODE_UP: return 0x26;
    case SDL_SCANCODE_RIGHT: return 0x27;
    case SDL_SCANCODE_DOWN: return 0x28;
    case SDL_SCANCODE_PAGEUP: return 0x21;
    case SDL_SCANCODE_PAGEDOWN: return 0x22;
    case SDL_SCANCODE_END: return 0x23;
    case SDL_SCANCODE_HOME: return 0x24;
    case SDL_SCANCODE_INSERT: return 0x2D;
    case SDL_SCANCODE_DELETE: return 0x2E;
    default: return 0;
    }
}

static void key_event(SDL_Scancode sc, int down) {
    extern void user32_key_event(uint32_t vk, uint32_t sc, int down);
    uint32_t vk = vk_of(sc);
    /* PC scan code: approximate with the DirectInput code, which matches set 1 */
    extern uint32_t dinput_dik_of(int sdl_scancode);
    user32_key_event(vk, dinput_dik_of(sc), down);
}

/* Emits key events for scripted keys whose state changed this frame. */
static void script_events(void) {
    static Uint8 prev[SDL_NUM_SCANCODES];
    if (!g_nscript) return;
    Uint8 cur[SDL_NUM_SCANCODES] = {0};
    for (int i = 0; i < g_nscript; i++)
        if (g_frames >= g_script[i].frame && g_frames < g_script[i].frame + g_script[i].len) cur[g_script[i].sc] = 1;
    for (int i = 0; i < g_nscript; i++) {
        SDL_Scancode sc = g_script[i].sc;
        if (cur[sc] != prev[sc]) {
            if (!g_padonly) key_event(sc, cur[sc]);
            SDL_Event e = {0};
            e.type = cur[sc] ? SDL_KEYDOWN : SDL_KEYUP;
            e.key.keysym.scancode = sc;
            e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
            extern void input_handle_event(const SDL_Event *e);
            input_handle_event(&e);
            prev[sc] = cur[sc];
        }
    }
}

extern uint32_t dinput_dik_of(int sdl_scancode);

const Uint8 *platform_keys(int *n) {
    int nk;
    const Uint8 *ks = SDL_GetKeyboardState(&nk);
    int scripted = g_nscript && !g_padonly;
    int virtual_ = vinput_active(); /* touch overlay keys (stick, hold buttons) */
    if (!scripted && !virtual_) { if (n) *n = nk; return ks; }
    /* vinput state is DIK-indexed while this table is SDL-scancode-indexed:
     * copy so it can be OR-ed, mapping each scancode through the same DIK
     * table the DirectInput shim uses (identity for overlay keys). */
    memcpy(g_keys, ks, nk < SDL_NUM_SCANCODES ? nk : SDL_NUM_SCANCODES);
    if (virtual_) {
        const uint8_t *vks = vinput_keys();
        for (int i = 0; i < nk; i++)
            if (!g_keys[i]) {
                uint32_t d = dinput_dik_of(i);
                if (d && vks[d]) g_keys[i] = 1;
            }
    }
    for (int i = 0; i < g_nscript && scripted; i++)
        if (g_frames >= g_script[i].frame && g_frames < g_script[i].frame + g_script[i].len) g_keys[g_script[i].sc] = 1;
    if (n) *n = SDL_NUM_SCANCODES;
    return g_keys;
}

/* Interactive test input: LB_KEYFIFO=path, lines "Key[:frames]" are queued
 * at the current frame (desktop testing only). */
static void poll_key_fifo(void) {
#ifndef __ANDROID__
    static FILE *fifo;
    static int tried;
    if (!tried) {
        tried = 1;
        const char *p = getenv("LB_KEYFIFO");
        if (p) {
            int fd = open(p, O_RDONLY | O_NONBLOCK);
            if (fd >= 0) fifo = fdopen(fd, "r");
        }
    }
    if (!fifo) return;
    char line[64];
    while (fgets(line, sizeof line, fifo)) {
        char name[32] = "";
        int len = 8;
        if (!strncmp(line, "dump ", 5)) {  /* "dump <file>": save the exe's data sections */
            line[strcspn(line, "\n")] = 0;
            FILE *d = fopen(line + 5, "wb");
            extern uint32_t g_image_size;
            if (d) { fwrite(MEM + GAME_EXE_BASE, 1, g_image_size, d); fclose(d); }
            LOG("keyfifo: dumped data to %s at frame %d", line + 5, g_frames);
            continue;
        }
        if (sscanf(line, "%31[^:\n]:%d", name, &len) < 1) continue;
        SDL_Scancode sc = SDL_GetScancodeFromName(name);
        if (sc == SDL_SCANCODE_UNKNOWN || g_nscript >= 256) continue;
        g_script[g_nscript++] = (KeyEv){g_frames + 1, len, sc};
        LOG("keyfifo: %s at frame %d", name, g_frames + 1);
    }
    clearerr(fifo);
#endif
}

void platform_present_hook(void) {
    SDL_Event e;
    poll_key_fifo();
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            LOG("quit requested");
            fflush(NULL);
            /* _exit, never exit(): the atexit chain runs SDL_Quit, whose
             * Android video teardown races the activity's own destroy
             * (onDestroy -> nativeQuit destroys Android_ActivityMutex
             * while this thread can still pump it) and aborts with
             * "FORTIFY: pthread_mutex_lock called on a destroyed mutex"
             * (field log glog). The OS reclaims everything anyway. */
            _exit(0);
        }
        if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)
            key_event(e.key.keysym.scancode, e.type == SDL_KEYDOWN);
        extern void input_handle_event(const SDL_Event *e);
        input_handle_event(&e);
    }
    /* The touch HUD lives in the Android activity (Java); its synthetic
     * intents arrive through hud_pump (ring drained on THIS thread, the
     * single-thread contract vinput/user32 were built on), which also
     * ticks the game-state scan that feeds the HUD context. This MUST be
     * once per present, NOT inside the SDL_PollEvent loop: the Java
     * overlay consumes every finger, so SDL events are rare while the
     * user sits in a menu, a movie or a quiet mission — a drain gated on
     * SDL events meant HUD presses piled up in the ring and NEVER
     * reached the game exactly there (intro videos unskippable, menu
     * clicks dead, field log glog). SDL touch events are not consumed
     * here: the Java overlay posts final intents instead. */
    {
        extern void hud_pump(uint32_t now_ms, const void *guest_mem);
        hud_pump(SDL_GetTicks(), MEM); /* x86.h: the flat 4 GiB region */
    }
    script_events();
    g_frames++;
    if (g_frames % 300 == 0) {
        LOG("frame %d", g_frames);
        /* field evidence that HUD intents flow (ring applied/dropped and
         * the live context), only when the counters moved since the last
         * report: one line per 300 frames at most */
        extern uint32_t hud_applied_events(void);
        extern uint32_t hud_dropped_events(void);
        extern int hud_game_ctx(void);
        static uint32_t la = 0xFFFFFFFFu, ld;
        uint32_t a = hud_applied_events(), d = hud_dropped_events();
        if (a != la || d != ld) {
            LOG("hud: %u applied, %u dropped, ctx %d", a, d, hud_game_ctx());
            la = a;
            ld = d;
        }
    }
    if (g_frames % 60 == 0) fflush(stderr);  /* fully buffered log: periodic flush */
    {   /* canary: the main thread's static TLS pointer (TEB slot 0 +0x2C) */
        static uint32_t last;
        uint32_t v = M32(GUEST_TEB_BASE + 0x2C);
        if (last && v != last)
            fprintf(stderr, "frame %d: main TEB+0x2C changed %08x -> %08x (TEB+0x24 tid %u, +0x1000 %08x)\n",
                    g_frames, last, v, M32(GUEST_TEB_BASE + 0x24), M32(GUEST_TEB_BASE + 0x1000));
        last = v;
    }
}
