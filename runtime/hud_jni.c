/* Java HUD bridge: the touch HUD moved to the Android activity (Java), so
 * this file is the only thing the runtime keeps of it:
 *
 *   1. A lock-free SPSC event ring. The Java UI thread posts FINAL input
 *      intents (look deltas, mouse buttons, DIK keys, menu mouse moves);
 *      the game's main thread drains the ring inside the present pump
 *      (platform_present_hook) and applies them through vinput — exactly
 *      the single-thread contract vinput/user32 were built on.
 *
 *   2. JNI entry points for io.github.deivid22srk.swat4.HudBridge:
 *        post(int type,int a,int b)  -> hud_post (UI thread, never blocks)
 *        poll(int[] out)             -> HUD context + guest client size
 *                                       (Java maps finger px -> guest px)
 *
 *   3. hud_pump(): called from platform_present_hook on the game thread;
 *      refreshes the guest-memory handle, ticks the game-state scan
 *      (hud_state.cpp, throttled 1 s) and drains the ring.
 *
 * No allocation, no locks: ring indices use acquire/release atomics, the
 * table is power-of-two sized. Ring overflow DROPS the event (look deltas
 * coalesce naturally; a dropped key edge would desync state, so key edges
 * are what Java never lets overflow — one DOWN/UP pair per finger).
 *
 * Selftest: build with -DHUD_RING_SELFTEST (host cc, no JNI needed) —
 * scripts/test_hud.sh compiles and runs it.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hud_state.h"
#include "vinput.h"

/* ------------------------------------------------------------------ */
/* Event ring (SPSC: one producer = Java UI thread, one consumer = the */
/* game's main thread).                                                */
/* ------------------------------------------------------------------ */

enum {
    HQ_LOOK = 0,        /* a=dx, b=dy (relative mouse px)               */
    HQ_MBTN,            /* a=button 0/1/2, b=down                       */
    HQ_KEY,             /* a=DIK, b=down | HQK_WM (WM post requested)   */
    HQ_MMOVE,           /* a=x, b=y in GUEST CLIENT pixels              */
    HQ_MENUCLICK,       /* a=down (menu left button)                    */
};
#define HQK_WM 0x100

#define HQ_N 256u               /* power of two */
#define HQ_MASK (HQ_N - 1u)

typedef struct { uint32_t t, a, b; } HQEv;

static HQEv hq_ring[HQ_N];
static uint32_t hq_tail;        /* producer-private write cursor        */
static uint32_t hq_head;        /* consumer-private read cursor         */
static uint32_t hq_dropped;     /* diagnostics: overflow drops          */
static uint32_t hq_applied;     /* diagnostics: events drained+applied  */

/* Called from the Java UI thread. Never blocks; drops on overflow. */
void hud_post(uint32_t t, uint32_t a, uint32_t b) {
    uint32_t tail = hq_tail;
    if (tail - hq_head == HQ_N) { hq_dropped++; return; }
    hq_ring[tail & HQ_MASK].t = t;
    hq_ring[tail & HQ_MASK].a = a;
    hq_ring[tail & HQ_MASK].b = b;
    __atomic_store_n(&hq_tail, tail + 1, __ATOMIC_RELEASE);
}

/* Applied on the game thread (same thread the DirectInput shim and the
 * user32 message queue were designed for). */
static void hud_apply(const HQEv *e) {
    switch (e->t) {
    case HQ_LOOK:
        vinput_mouse_delta((int32_t)e->a, (int32_t)e->b);
        break;
    case HQ_MBTN:
        vinput_mouse_button(e->a, e->b);
        break;
    case HQ_KEY:
        if (e->b & HQK_WM) vinput_key_tap((uint8_t)e->a, e->b & 1);
        else               vinput_key((uint8_t)e->a, e->b & 1);
        break;
    case HQ_MMOVE:
        vinput_menu_move((int)e->a, (int)e->b);
        break;
    case HQ_MENUCLICK:
        vinput_menu_button(e->a);
        break;
    default:
        break;
    }
}

/* Called from the game thread (platform_present_hook). */
void hud_drain(void) {
    for (;;) {
        uint32_t head = hq_head;
        uint32_t tail = __atomic_load_n(&hq_tail, __ATOMIC_ACQUIRE);
        if (head == tail) break;
        HQEv e = hq_ring[head & HQ_MASK];
        __atomic_store_n(&hq_head, head + 1, __ATOMIC_RELEASE);
        hq_applied++;
        hud_apply(&e);
    }
}

/* Game-thread pump: guest memory handle self-heals (the 4 GiB region is
 * allocated once, but keep the order honest), state scan ticks throttled,
 * Java intents drain. */
void hud_pump(uint32_t now_ms, const void *guest_mem) {
    static const void *last_mem;
    if (guest_mem && guest_mem != last_mem) {
        last_mem = guest_mem;
        hud_set_guest_mem(guest_mem);
    }
    hud_game_tick(now_ms);
    hud_drain();
}

uint32_t hud_dropped_events(void) { return hq_dropped; }
uint32_t hud_applied_events(void) { return hq_applied; }

/* ------------------------------------------------------------------ */
/* JNI (Android build only; the host selftest uses the plain C above). */
/* ------------------------------------------------------------------ */
#if defined(__ANDROID__) && !defined(HUD_RING_SELFTEST)

#include <jni.h>

void user32_get_screen(int *w, int *h);

/* Returns 1 with out[0] = ctx | doorNear<<8, out[1] = guest client w,
 * out[2] = guest client h. Safe any time: before the guest boots the
 * context is HUD_CTX_UNKNOWN and the client size is the last known one.
 * Reads plain ints written by the game thread — 32-bit aligned loads are
 * atomic on ARM; values are advisory (HUD layout), never used for
 * correctness of game state. */
JNIEXPORT jboolean JNICALL
Java_io_github_deivid22srk_swat4_HudBridge_poll(JNIEnv *env, jclass cls,
                                                jintArray out) {
    (void)cls;
    if (!out) return JNI_FALSE;
    jsize n = (*env)->GetArrayLength(env, out);
    if (n < 3) return JNI_FALSE;
    jint v[3];
    v[0] = (jint)hud_game_ctx();
    if (hud_door_near(128.0f, NULL)) v[0] |= 1 << 8; /* game's own door Range */
    int w, h;
    user32_get_screen(&w, &h);
    v[1] = (jint)w;
    v[2] = (jint)h;
    (*env)->SetIntArrayRegion(env, out, 0, 3, v);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_io_github_deivid22srk_swat4_HudBridge_post(JNIEnv *env, jclass cls,
                                                jint type, jint a, jint b) {
    (void)env;
    (void)cls;
    hud_post((uint32_t)type, (uint32_t)a, (uint32_t)b);
}

#endif /* __ANDROID__ && !HUD_RING_SELFTEST */

/* ------------------------------------------------------------------ */
/* Selftest (host): producer/consumer order, overflow drop, apply path */
/* ------------------------------------------------------------------ */
#ifdef HUD_RING_SELFTEST

#include <stdio.h>
static int failures;
#define CHECK(x) do { \
    if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } \
} while (0)

/* vinput/user32 stubs: the selftest links hud_jni.c WITHOUT the real
 * vinput.c to prove the ring only needs the interface. */
static int s_look, s_mbtn_down, s_mbtn_btn, s_key_dik, s_key_down, s_key_wm, s_mmove, s_mclick;
void vinput_mouse_delta(int dx, int dy) { s_look += dx + dy; }
void vinput_mouse_button(int b, int down) { s_mbtn_btn = b; s_mbtn_down = down; }
void vinput_key(uint8_t dik, int down) { s_key_dik = dik; s_key_down = down; s_key_wm = 0; }
void vinput_key_tap(uint8_t dik, int down) { s_key_dik = dik; s_key_down = down; s_key_wm = 1; }
void vinput_menu_move(int x, int y) { s_mmove = x * 1000 + y; }
void vinput_menu_button(int down) { s_mclick = down; }
void hud_set_guest_mem(const void *mem) { (void)mem; }
void hud_game_tick(uint32_t now) { (void)now; }
int  hud_game_ctx(void) { return 2; }
int  hud_door_near(float r, float *d) { (void)r; (void)d; return 0; }

int main(void) {
    /* empty ring drains nothing */
    hud_drain();
    CHECK(s_look == 0);

    /* FIFO order across a wrap boundary: first 200 (no overflow), drain,
     * then 120 more crossing the 256 index wrap with the ring empty */
    for (uint32_t i = 0; i < 200; i++) hud_post(HQ_LOOK, (int)i, 1);
    hud_drain();
    CHECK(s_look == (199 * 200) / 2 + 200);
    for (uint32_t i = 0; i < 120; i++) hud_post(HQ_LOOK, (int)i, 1);
    hud_drain();
    CHECK(s_look == (199 * 200) / 2 + 200 + (119 * 120) / 2 + 120);

    /* key edges keep the WM flag, buttons pass through */
    hud_post(HQ_KEY, 0x26, 1 | HQK_WM);
    hud_post(HQ_MBTN, 0, 1);
    hud_drain();
    CHECK(s_key_dik == 0x26 && s_key_down == 1 && s_key_wm == 1);
    CHECK(s_mbtn_btn == 0 && s_mbtn_down == 1);

    /* overflow drops instead of corrupting: fill, add 2, drain sees the
     * first HQ_N, exactly the 2 extras are counted as dropped */
    s_mmove = 0;
    uint32_t drop0 = hq_dropped;
    for (uint32_t i = 0; i < HQ_N + 2; i++) hud_post(HQ_MMOVE, (int)i, 0);
    CHECK(hq_dropped - drop0 == 2);
    hud_drain();
    CHECK(s_mmove == (int)(HQ_N - 1) * 1000);

    /* menu click passthrough */
    hud_post(HQ_MENUCLICK, 1, 0);
    hud_drain();
    CHECK(s_mclick == 1);

    printf(failures ? "hud_jni selftest: %d FAILURES\n" : "hud_jni selftest: ok\n",
           failures);
    return failures ? 1 : 0;
}
#endif /* HUD_RING_SELFTEST */
