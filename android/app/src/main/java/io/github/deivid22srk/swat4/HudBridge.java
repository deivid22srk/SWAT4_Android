package io.github.deivid22srk.swat4;

/**
 * Java&lt;-&gt;runtime bridge for the touch HUD.
 *
 * The HUD itself is a real Android View (HudOverlay): Android draws it above
 * the game surface, so it stays visible through menu swapchains, movies and
 * loading frames — no dependence on the game's Present path, and the stick
 * and buttons are sized in dp, not in the game's render pixels.
 *
 * This class is only the pipe into the runtime (libmain.so):
 *
 *   - post(): pushes a FINAL input intent (look delta, mouse button, DIK
 *     key, menu mouse move/click) into a lock-free SPSC ring. The game's
 *     main thread drains it inside the present pump and applies it through
 *     vinput, on the same thread that owns the DirectInput shim and the
 *     user32 message queue.
 *
 *   - poll(): reads the HUD context derived from REAL game objects
 *     (hud_state.cpp scans the engine object array) plus the guest client
 *     size, so the overlay can map finger pixels -> guest client pixels
 *     1:1 with what the game's GUI compares against.
 *
 * All calls are cheap and non-blocking. Do not hold references to out[].
 */
public final class HudBridge {
    /** Event types posted via post(). Must match runtime/hud_jni.c. */
    public static final int EV_LOOK = 0;        /* a=dx, b=dy (relative px)      */
    public static final int EV_MBTN = 1;        /* a=btn 0/1/2, b=down           */
    public static final int EV_KEY = 2;         /* a=DIK, b=down | 0x100 (WM)    */
    public static final int EV_MMOVE = 3;       /* a=x, b=y in GUEST client px   */
    public static final int EV_MENUCLICK = 4;   /* a=down (menu left button)     */

    /** HUD context (from hud_state.h, packed in poll() out[0] low bits). */
    public static final int CTX_UNKNOWN = 0;    /* guest not readable yet        */
    public static final int CTX_MENU = 1;       /* no player pawn: menus, movies */
    public static final int CTX_MISSION = 2;    /* a real ASwatPlayer exists     */

    /** poll() out[0] bit 8: a real door sits within the game's Use range. */
    public static final int DOOR_BIT = 1 << 8;

    /** poll() out[0] bit 9: a Bink movie file (.bik) is open — the intro
     * videos. The guest pumps no messages during them, so the overlay
     * turns free-finger taps into the skip key (ESC) instead of the menu
     * mouse while this bit is set (and the menu has never been seen). */
    public static final int MOVIE_BIT = 1 << 9;

    /** poll() out[0] bit 10: the guest pumped its message queue WHILE a
     * movie is open — the main menu running behind its background video
     * (menus pump every frame; the intro Bink loop never pumps). The
     * overlay latches everMenu on this and stops routing free-finger
     * touches to the movie-skip ESC even when the object scan stays
     * CTX_UNKNOWN (field shhxlog: every menu tap degraded to Esc). */
    public static final int GUI_BIT = 1 << 10;

    private static volatile boolean sOk = false;

    static {
        try {
            /* SDLActivity already loaded "main"; this is a no-op there and
             * keeps the bridge usable if the load order ever changes. */
            System.loadLibrary("main");
            sOk = true;
        } catch (Throwable t) {
            sOk = false;
        }
    }

    /** false only if libmain.so could not load: the overlay then stays
     * passive (draws nothing, consumes nothing) and the game keeps its
     * default behaviour instead of a half-broken HUD. */
    public static boolean ok() {
        return sOk;
    }

    /**
     * Reads HUD state. out[0] = ctx | (doorNear ? DOOR_BIT : 0),
     * out[1] = guest client width, out[2] = guest client height.
     * Returns false when the runtime is unavailable or out is too small.
     */
    public static native boolean poll(int[] out);

    /** Posts an input intent into the ring (never blocks; drops on overflow). */
    public static native void post(int type, int a, int b);

    private HudBridge() {
    }
}
