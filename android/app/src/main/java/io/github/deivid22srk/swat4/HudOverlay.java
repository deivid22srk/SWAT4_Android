package io.github.deivid22srk.swat4;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.DisplayCutout;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;

/**
 * The SWAT 4 touch HUD, as a real Android View.
 *
 * Why Java: an Android View is composited by the system ABOVE the game
 * surface, so the stick and buttons stay visible through menu swapchains,
 * Bink movies and loading frames — none of the D3D hook fragility of the
 * previous C overlay. It also fixes the field bugs by construction: the
 * HUD is laid out and hit-tested in VIEW pixels (the same space the finger
 * reports), sized in dp, and the stick is a fixed, always-visible anchor
 * instead of appearing only under a dragged finger.
 *
 * The FULL HUD is always visible and always active, in EVERY context —
 * user requirement: "todos os botões mostrados a todo tempo". The stick
 * and every button, PORTA and ESC included, draw through missions, menus,
 * Bink movies and loading frames alike, and every one of them works
 * anywhere. What the REAL game state (HudBridge.poll -> hud_state.cpp
 * scans the engine's own object array) still decides is what a FREE
 * finger does:
 *   MISSION — free fingers look around; the stick is movement (WASD
 *             sectors with hysteresis).
 *   MENU    — free fingers are the game's own mouse (WM mouse at
 *             guest-client coordinates: direct Career/Training taps); the
 *             stick is the menu arrow keys.
 *   UNKNOWN — same as MENU (harmless: the guest ignores what it cannot
 *             use yet).
 * PORTA keeps a visual accent while a real ASwatDoor sits within the
 * game's own Use range (hud_door_near), but the button itself never
 * hides and its F always fires.
 *
 * The D-pad above the stick is the keyboard's arrow keys as four
 * always-on buttons (dpad_*.png icons, or chevron glyphs as fallback,
 * WM key downs, auto-repeat while held), so menus navigate without the
 * stick.
 *
 * Every control sends the game's own input through HudBridge.post (a
 * lock-free SPSC ring drained by the game thread): mouse buttons, DIK key
 * state/taps, WM menu mouse. Nothing is simulated outside the game's real
 * input paths. Zero per-frame allocation; the view only invalidates when
 * something actually changes (touch, context flip, door proximity).
 *
 * Buttons show tactical line icons instead of the text labels when the
 * icon set is available: decoded ONCE at construction, first from
 * <game>/hudicons/*.png (user-swappable on device, no rebuild) and then
 * from the APK's packaged assets/hudicons/ (docs/MANIFEST_ICONES_HUD.md
 * documents every file -> button). A missing or broken PNG keeps that
 * button's old text label, so the HUD degrades per button, never as a
 * whole. The grid loads its two pages (slot_1..slot_0 and fx_1..fx_10)
 * plus the two page chips the same way.
 *
 * Tuning stays in <game>/port/hud.ini — same keys and defaults the
 * runtime's previous overlay documented (version=2).
 */
public class HudOverlay extends View {
    /* ---------------- config (hud.ini) ---------------- */
    private float cfgScale = 1.0f, cfgOpacity = 0.55f, cfgSens = 1.0f, cfgStick = 1.0f;
    private boolean cfgInvertY, cfgHanded, cfgLabels = true, cfgHoldCrouch, cfgHoldZoom;

    /* ---------------- DIK (set-1) codes used by the layout ---------------- */
    private static final int DIK_ESC = 0x01, DIK_1 = 0x02, DIK_0 = 0x0B, DIK_TAB = 0x0F,
            DIK_Q = 0x10, DIK_W = 0x11, DIK_E = 0x12, DIK_R = 0x13, DIK_A = 0x1E, DIK_S = 0x1F,
            DIK_D = 0x20, DIK_F = 0x21, DIK_LSHIFT = 0x2A, DIK_Z = 0x2C, DIK_X = 0x2D,
            DIK_C = 0x2E, DIK_SPACE = 0x39, DIK_F1 = 0x3B, DIK_DEL = 0xD3,
            DIK_UP = 0xC8, DIK_DOWN = 0xD0, DIK_LEFT = 0xCB, DIK_RIGHT = 0xCD;

    /* ---------------- button table ---------------- */
    private static final int K_MHOLD = 0, K_MTAP = 1, K_KHOLD = 2, K_KTAP = 3, K_GRID = 4,
            K_ARROW = 5;

    /** One HUD button: id, kind, target (DIK or mouse button), label, size
     * in u units. Rects live in bx/by/bw/bh (view px). */
    private static final class Btn {
        final int id, kind, dik, mbtn;
        final String label;
        final float size;
        Btn(int id, int kind, int dik, int mbtn, String label, float size) {
            this.id = id; this.kind = kind; this.dik = dik; this.mbtn = mbtn;
            this.label = label; this.size = size;
        }
    }

    private static final int B_FOGO = 0, B_CMD = 1, B_SLOTS = 2, B_USAR = 3, B_PORTA = 4,
            B_REC = 5, B_ZOOM = 6, B_MODO = 7, B_AGACH = 8, B_CORRER = 9, B_INCL = 10,
            B_INCR = 11, B_TAB = 12, B_ESP = 13, B_ESC = 14, B_TROCA = 15,
            B_UP = 16, B_DOWN = 17, B_LEFT = 18, B_RIGHT = 19, B_NBASE = 20;

    private static final Btn[] BASE = {
            new Btn(B_FOGO, K_MHOLD, 0, 0, "FOGO", 150),
            new Btn(B_CMD, K_MHOLD, 0, 1, "CMD", 100),
            new Btn(B_SLOTS, K_GRID, 0, -1, "SLOTS", 56),
            new Btn(B_USAR, K_MTAP, 0, 2, "USAR", 90),
            new Btn(B_PORTA, K_KTAP, DIK_F, -1, "PORTA", 72),   /* door-gated */
            new Btn(B_REC, K_KTAP, DIK_R, -1, "REC", 72),
            new Btn(B_ZOOM, K_KTAP, DIK_Z, -1, "ZOOM", 72),     /* kind per cfg */
            new Btn(B_MODO, K_KTAP, DIK_X, -1, "MODO", 72),
            new Btn(B_AGACH, K_KTAP, DIK_C, -1, "AGACH", 72),   /* kind per cfg */
            new Btn(B_CORRER, K_KHOLD, DIK_LSHIFT, -1, "CORRER", 72),
            new Btn(B_INCL, K_KHOLD, DIK_Q, -1, "INC<", 72),
            new Btn(B_INCR, K_KHOLD, DIK_E, -1, "INC>", 72),
            new Btn(B_TAB, K_KTAP, DIK_TAB, -1, "TAB", 56),
            new Btn(B_ESP, K_KTAP, DIK_SPACE, -1, "ESP", 56),
            new Btn(B_ESC, K_KTAP, DIK_ESC, -1, "ESC", 64),
            new Btn(B_TROCA, K_KTAP, DIK_DEL, -1, "TROCA", 64),
            new Btn(B_UP, K_ARROW, DIK_UP, -1, null, 56),
            new Btn(B_DOWN, K_ARROW, DIK_DOWN, -1, null, 56),
            new Btn(B_LEFT, K_ARROW, DIK_LEFT, -1, null, 56),
            new Btn(B_RIGHT, K_ARROW, DIK_RIGHT, -1, null, 56),
    };

    /* Grid: 10 slots (1..0 or F1..F10 by page) + page chip (index 10). */
    private static final int GRID_N = 11;
    private final Btn[] grid = new Btn[GRID_N];
    private final float[] gx = new float[GRID_N], gy = new float[GRID_N],
            gsz = new float[GRID_N];
    private final float[] panel = new float[4];
    private boolean gridOpen;
    private int gridPage;

    /* ---------------- layout (view px) ---------------- */
    private final float[] bx = new float[B_NBASE], by = new float[B_NBASE],
            bsz = new float[B_NBASE];
    private final boolean[] pressed = new boolean[B_NBASE];
    private float u;                        /* layout unit */
    private float zoneL, zoneT, zoneR, zoneB; /* stick zone */
    private float stickR, stickDead;
    private float anchorX, anchorY;         /* resting stick anchor */
    private float safeTop, safeBottom, safeLeft, safeRight;
    private int vw, vh;                     /* view size at layout time */

    /* ---------------- live state ---------------- */
    private static final int R_NONE = 0, R_STICK = 1, R_LOOK = 2, R_BTN = 3, R_MENU = 4;

    private static final class Ptr {
        int id = 0;
        int role = R_NONE;
        int btn = -1;          /* index into BASE, or -1 */
        float lx, ly;          /* last finger position (look deltas) */
        float sx, sy;          /* stick finger start (relative stick) */
        long repAt;            /* repeat deadline for a held K_ARROW button */
    }

    private final Ptr[] ptrs = new Ptr[10];
    private int nMenuFingers;               /* menu click: down while >0 */
    private boolean stickHeld;
    private float stickVX, stickVY;         /* knob offset from anchor (draw) */
    private int stickSector;                /* 1=left 2=right 4=up 8=down */

    private int ctx;                        /* HudBridge.CTX_* */
    private boolean doorNear;
    /** live: a Bink movie file is open (intro videos — runtime-reported). */
    private boolean movie;
    /** latched once the main menu has been seen — CTX_MENU from the object
     * scan OR the GUI_BIT pump signal (the guest pumped its message queue
     * while a movie is open: the menu behind its background video) — so a
     * dead object scan can never reduce the whole session to the
     * movie-skip key. From then on free fingers must stay the menu mouse,
     * never the skip key. */
    private boolean everMenu;
    private int guestW, guestH;             /* guest client px (menu mapping) */
    private long menuArrowRepAt;            /* menu stick repeat deadline */

    /* pending 120 ms tap auto-releases (preallocated table) */
    private final int[] pendTarget = new int[16];
    private final boolean[] pendIsMouse = new boolean[16];
    private final long[] pendDue = new long[16];
    private int nPend;

    /* ---------------- icons (decoded once at construction) ------------ */
    /** Icon file per BASE entry, index-aligned with BASE (B_FOGO..B_RIGHT). */
    private static final String[] BASE_ICON = {
            "fogo", "cmd", "slots", "usar", "porta",
            "rec", "zoom", "modo", "agach", "correr",
            "incl", "incr", "tab", "esp", "esc", "troca",
            "dpad_up", "dpad_down", "dpad_left", "dpad_right",
    };
    private final Bitmap[] baseIcon = new Bitmap[B_NBASE];
    private final Bitmap[] slotIcon = new Bitmap[10];   /* grid page 0: 1..9,0 */
    private final Bitmap[] fxIcon = new Bitmap[10];     /* grid page 1: F1..F10 */
    private Bitmap icToggleNum, icToggleFx;             /* grid page chips */

    /* ---------------- drawing (all preallocated) ---------------- */
    private final Paint pFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pText = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pCircle = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pIcon = new Paint(Paint.ANTI_ALIAS_FLAG
            | Paint.FILTER_BITMAP_FLAG | Paint.DITHER_FLAG);
    private final RectF rf = new RectF();
    private final RectF ri = new RectF();   /* icon dst inside a button */
    /* D-pad chevron glyphs: built once per relayout, only stroked onDraw
     * (fallback for when the dpad_*.png icons are absent) */
    private final Path[] chev = new Path[4];

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final int[] pollOut = new int[3];
    private final String iniPath;

    public HudOverlay(Context c, String gameDir) {
        super(c);
        for (int i = 0; i < ptrs.length; i++) ptrs[i] = new Ptr();
        for (int i = 0; i < chev.length; i++) chev[i] = new Path();
        iniPath = gameDir == null ? null : new File(gameDir, "port/hud.ini").getAbsolutePath();
        loadCfg();
        loadIcons(c, gameDir);
        setFocusable(false);
        setFocusableInTouchMode(false);
        setClickable(true);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        pStroke.setStyle(Paint.Style.STROKE);
        pText.setTextAlign(Paint.Align.CENTER);
        pCircle.setStyle(Paint.Style.FILL);
    }

    /* ------------------------------------------------------------------ */
    /* hud.ini: same keys, defaults and clamps the runtime overlay used    */
    /* ------------------------------------------------------------------ */
    private void loadCfg() {
        boolean fresh = iniPath == null;
        if (!fresh) fresh = !readCfg(new File(iniPath));
        if (fresh && iniPath != null) writeCfg();
        clampCfg();
    }

    private boolean readCfg(File f) {
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            String line;
            int version = 0;
            while ((line = r.readLine()) != null) {
                int cmt = line.indexOf('#');
                if (cmt >= 0) line = line.substring(0, cmt);
                int eq = line.indexOf('=');
                if (eq <= 0) continue;
                String k = line.substring(0, eq).trim();
                String v = line.substring(eq + 1).trim();
                switch (k) {
                    case "version": version = parseInt(v); break;
                    case "scale": cfgScale = parseFloat(v, cfgScale); break;
                    case "opacity": cfgOpacity = parseFloat(v, cfgOpacity); break;
                    case "look_sens": cfgSens = parseFloat(v, cfgSens); break;
                    case "stick_radius": cfgStick = parseFloat(v, cfgStick); break;
                    case "look_invert_y": cfgInvertY = parseInt(v) != 0; break;
                    case "handed": cfgHanded = parseInt(v) != 0; break;
                    case "labels": cfgLabels = parseInt(v) != 0; break;
                    case "hold_crouch": cfgHoldCrouch = parseInt(v) != 0; break;
                    case "hold_zoom": cfgHoldZoom = parseInt(v) != 0; break;
                    /* "always_show" (v2) is gone: the HUD is always shown */
                    default: break;
                }
            }
            return version >= 2;             /* stale pre-v2 file: rewrite once */
        } catch (IOException | RuntimeException e) {
            return false;
        }
    }

    private void writeCfg() {
        try {
            File f = new File(iniPath);
            File d = f.getParentFile();
            if (d != null && !d.isDirectory() && !d.mkdirs()) return;
            try (FileWriter w = new FileWriter(f)) {
                w.write("version=2\n");
                w.write("scale=" + trimF(cfgScale) + "\n");
                w.write("opacity=" + trimF(cfgOpacity) + "\n");
                w.write("look_sens=" + trimF(cfgSens) + "\n");
                w.write("stick_radius=" + trimF(cfgStick) + "\n");
                w.write("look_invert_y=" + (cfgInvertY ? 1 : 0) + "\n");
                w.write("handed=" + (cfgHanded ? 1 : 0) + "\n");
                w.write("labels=" + (cfgLabels ? 1 : 0) + "\n");
                w.write("hold_crouch=" + (cfgHoldCrouch ? 1 : 0) + "\n");
                w.write("hold_zoom=" + (cfgHoldZoom ? 1 : 0) + "\n");
            }
        } catch (IOException ignored) {
        }
    }

    private static String trimF(float v) {
        return Float.toString(Math.round(v * 1000f) / 1000f);
    }

    private static int parseInt(String v) {
        try {
            return (int) Long.parseLong(v.trim());
        } catch (RuntimeException e) {
            return 0;
        }
    }

    private static float parseFloat(String v, float def) {
        try {
            return Float.parseFloat(v.trim());
        } catch (RuntimeException e) {
            return def;
        }
    }

    private void clampCfg() {
        cfgScale = clamp(cfgScale, 0.5f, 2.0f);
        cfgOpacity = clamp(cfgOpacity, 0.15f, 1.0f);
        cfgSens = clamp(cfgSens, 0.2f, 3.0f);
        cfgStick = clamp(cfgStick, 0.7f, 1.6f);
    }

    /* ------------------------------------------------------------------ */
    /* Icon set: <game>/hudicons/ wins (swappable on device), then the     */
    /* APK assets; a missing PNG keeps that button's text label            */
    /* ------------------------------------------------------------------ */
    private void loadIcons(Context c, String gameDir) {
        File dir = gameDir == null ? null : new File(gameDir, "hudicons");
        for (int i = 0; i < B_NBASE && i < BASE_ICON.length; i++)
            baseIcon[i] = decodeOne(dir, BASE_ICON[i], c);
        for (int i = 0; i < 10; i++) {
            slotIcon[i] = decodeOne(dir, "slot_" + ((i + 1) % 10), c);
            fxIcon[i] = decodeOne(dir, "fx_" + (i + 1), c);
        }
        icToggleNum = decodeOne(dir, "toggle_num", c);
        icToggleFx = decodeOne(dir, "toggle_fx", c);
    }

    /** name.png from dir, else from assets hudicons/, else null. Never
     * throws: a broken set must never take the HUD down. */
    private static Bitmap decodeOne(File dir, String name, Context c) {
        try {
            if (dir != null) {
                File f = new File(dir, name + ".png");
                if (f.isFile()) {
                    Bitmap b = BitmapFactory.decodeFile(f.getAbsolutePath());
                    if (b != null) return b;
                }
            }
            InputStream in = c.getAssets().open("hudicons/" + name + ".png");
            try {
                return BitmapFactory.decodeStream(in);
            } finally {
                try { in.close(); } catch (IOException ignored) {
                }
            }
        } catch (IOException | RuntimeException e) {
            return null;
        }
    }

    private static float clamp(float v, float lo, float hi) {
        return v < lo ? lo : v > hi ? hi : v;
    }

    /* ------------------------------------------------------------------ */
    /* Lifecycle: context polling runs only while attached                 */
    /* ------------------------------------------------------------------ */
    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        handler.removeCallbacks(pollTick);
        handler.post(pollTick);
    }

    @Override
    protected void onDetachedFromWindow() {
        releaseAll();
        handler.removeCallbacksAndMessages(null);
        super.onDetachedFromWindow();
    }

    /* ------------------------------------------------------------------ */
    /* Layout                                                              */
    /* ------------------------------------------------------------------ */
    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        super.onSizeChanged(w, h, ow, oh);
        relayout(w, h);
    }

    private void relayout(int w, int h) {
        vw = w;
        vh = h;
        if (w < 320 || h < 240) return;
        /* cutout / gesture insets (defensive: values may be absent) */
        safeTop = safeBottom = safeLeft = safeRight = 0;
        try {
            WindowInsets in = getRootWindowInsets();
            if (in != null) {
                DisplayCutout cut = in.getDisplayCutout();
                if (cut != null) {
                    safeTop = Math.max(0, cut.getSafeInsetTop());
                    safeBottom = Math.max(0, cut.getSafeInsetBottom());
                    safeLeft = Math.max(0, cut.getSafeInsetLeft());
                    safeRight = Math.max(0, cut.getSafeInsetRight());
                }
                if (android.os.Build.VERSION.SDK_INT >= 29) {
                    android.graphics.Insets gi = in.getSystemGestureInsets();
                    safeBottom = Math.max(safeBottom, Math.max(0, gi.bottom));
                    safeLeft = Math.max(safeLeft, Math.max(0, gi.left));
                    safeRight = Math.max(safeRight, Math.max(0, gi.right));
                }
            }
        } catch (RuntimeException ignored) {
        }
        if (safeTop + safeBottom > 0.4f * h || safeLeft + safeRight > 0.4f * w) {
            safeTop = safeBottom = safeLeft = safeRight = 0; /* bogus ROM values */
        }
        float d = getResources().getDisplayMetrics().density;
        safeTop += 0.01f * h;
        safeBottom += d * 8;
        safeLeft += d * 4;
        safeRight += d * 4;

        u = (Math.min(w, h) / 720f) * cfgScale;
        float g = 10 * u;
        float xr = w - safeRight, yb = h - safeBottom;

        for (int i = 0; i < B_NBASE; i++) bsz[i] = BASE[i].size * u;
        float f = bsz[B_FOGO], c = bsz[B_CMD], sm = bsz[B_SLOTS], r = bsz[B_REC],
                us = bsz[B_USAR], e = bsz[B_ESC];

        bx[B_FOGO] = xr - f; by[B_FOGO] = yb - f;
        bx[B_CMD] = xr - c; by[B_CMD] = by[B_FOGO] - g - c;
        bx[B_SLOTS] = xr - sm; by[B_SLOTS] = by[B_CMD] - g - sm;
        bx[B_USAR] = bx[B_FOGO] - g - us; by[B_USAR] = yb - us;
        bx[B_PORTA] = bx[B_USAR]; by[B_PORTA] = by[B_USAR] - g - r;
        bx[B_REC] = bx[B_USAR] - g - r; by[B_REC] = yb - r;
        bx[B_ZOOM] = bx[B_REC]; by[B_ZOOM] = by[B_REC] - g - r;
        bx[B_MODO] = bx[B_REC]; by[B_MODO] = by[B_ZOOM] - g - r;
        bx[B_AGACH] = bx[B_REC] - g - r; by[B_AGACH] = yb - r;
        bx[B_CORRER] = bx[B_AGACH]; by[B_CORRER] = by[B_AGACH] - g - r;

        zoneL = safeLeft; zoneR = zoneL + 0.30f * w;
        zoneB = yb; zoneT = zoneB - 0.38f * h;
        bx[B_INCL] = zoneL + 4 * u; by[B_INCL] = zoneT - g - r;
        bx[B_INCR] = bx[B_INCL] + r + g; by[B_INCR] = by[B_INCL];
        bx[B_TAB] = xr - sm; by[B_TAB] = by[B_SLOTS] - g - sm;
        bx[B_ESP] = bx[B_TAB] - g - sm; by[B_ESP] = by[B_TAB];
        bx[B_ESC] = xr - e; by[B_ESC] = safeTop;
        bx[B_TROCA] = bx[B_ESC] - g - e; by[B_TROCA] = safeTop;

        /* D-pad cluster: the keyboard's arrow keys, plus-shaped, above the
         * lean buttons — menus navigate with real arrow keys, and the
         * handed mirror below maps it to the other side like every button.
         * X comes from the UNMIRRORED stick-zone center (anchorX is only
         * assigned after that mirror), so the mirror lands the cluster
         * exactly on the mirrored stick anchor. The cluster shrinks before
         * it can clip above the screen: at scale 2.0 the naive 56u buttons
         * pushed B_UP past y<0 (critic r1: unreachable and untouchable). */
        float dp = 56 * u;
        float span = by[B_INCL] - g - safeTop;  /* vertical room below the INC row */
        if (3 * dp + 2 * g > span) dp = Math.max(1f, (span - 2 * g) / 3f);
        bsz[B_UP] = bsz[B_DOWN] = bsz[B_LEFT] = bsz[B_RIGHT] = dp;
        float ancX = zoneL + (zoneR - zoneL) * 0.5f;
        by[B_UP] = by[B_INCL] - g - 3 * dp - 2 * g;
        by[B_LEFT] = by[B_INCL] - g - 2 * dp - g;
        by[B_DOWN] = by[B_INCL] - g - dp;
        bx[B_UP] = bx[B_DOWN] = ancX - dp * 0.5f;
        bx[B_LEFT] = ancX - dp * 0.5f - dp - g;
        bx[B_RIGHT] = ancX + dp * 0.5f + g;

        if (cfgHanded) {
            for (int i = 0; i < B_NBASE; i++) {
                float dist = bx[i] - safeLeft;
                bx[i] = (w - safeRight) - dist - bsz[i];
            }
            float zl = zoneL;
            zoneL = (w - safeRight) - (zoneR - safeLeft);
            zoneR = (w - safeRight) - (zl - safeLeft);
        }

        stickR = 0.085f * h * cfgStick;
        stickDead = 0.18f * stickR;
        anchorX = (zoneL + zoneR) * 0.5f;
        anchorY = zoneB - stickR - 14 * u;

        /* chevron glyphs for the four D-pad buttons: rebuilt here, after
         * the handed mirror changed bx[], so they always track the final
         * rects; onDraw only strokes them (zero per-frame allocation) */
        for (int i = B_UP; i <= B_RIGHT; i++) {
            float x = bx[i], y = by[i], s2 = bsz[i];
            Path p = chev[i - B_UP];
            p.reset();
            switch (i) {
                case B_UP:
                    p.moveTo(x + 0.32f * s2, y + 0.56f * s2);
                    p.lineTo(x + 0.50f * s2, y + 0.38f * s2);
                    p.lineTo(x + 0.70f * s2, y + 0.56f * s2);
                    break;
                case B_DOWN:
                    p.moveTo(x + 0.32f * s2, y + 0.44f * s2);
                    p.lineTo(x + 0.50f * s2, y + 0.62f * s2);
                    p.lineTo(x + 0.70f * s2, y + 0.44f * s2);
                    break;
                case B_LEFT:
                    p.moveTo(x + 0.58f * s2, y + 0.32f * s2);
                    p.lineTo(x + 0.38f * s2, y + 0.50f * s2);
                    p.lineTo(x + 0.58f * s2, y + 0.70f * s2);
                    break;
                default: /* B_RIGHT */
                    p.moveTo(x + 0.42f * s2, y + 0.32f * s2);
                    p.lineTo(x + 0.62f * s2, y + 0.50f * s2);
                    p.lineTo(x + 0.42f * s2, y + 0.70f * s2);
                    break;
            }
        }

        layoutGrid();
    }

    private void layoutGrid() {
        if (vw < 320 || vh < 240) return;
        float gg = 8 * u, pad = 16 * u, title = 22 * u;
        float sl = 64 * u, fx = 56 * u;
        float pw = 5 * sl + 4 * gg + 2 * pad;
        float ph = title + gg + 2 * sl + gg + fx + 2 * pad;
        float px = (vw - pw) * 0.5f, py = (vh - ph) * 0.42f;
        px = Math.max(Math.min(px, vw - safeRight - pw), safeLeft);
        py = Math.max(Math.min(py, vh - safeBottom - ph), safeTop);
        panel[0] = px; panel[1] = py; panel[2] = pw; panel[3] = ph;
        for (int i = 0; i < 10; i++) {
            int dik = gridPage != 0 ? DIK_F1 + i : DIK_1 + i;
            String label = gridPage != 0 ? ("F" + (i + 1))
                    : (i == 9 ? "0" : Integer.toString(i + 1));
            grid[i] = new Btn(100 + i, K_KTAP, dik, -1, label, 64);
            gx[i] = px + pad + (i % 5) * (sl + gg);
            gy[i] = py + pad + title + gg + (i / 5) * (sl + gg);
            gsz[i] = sl;
        }
        grid[10] = new Btn(110, K_GRID, 0, -1, gridPage != 0 ? "NUM" : "FX", 56);
        gx[10] = px + (pw - fx) * 0.5f;
        gy[10] = py + pad + title + gg + 2 * sl + gg;
        gsz[10] = fx;
    }

    /* ------------------------------------------------------------------ */
    /* Context polling (100 ms while attached): only flips invalidate      */
    /* ------------------------------------------------------------------ */
    private final Runnable pollTick = new Runnable() {
        @Override
        public void run() {
            boolean changed = false;
            if (HudBridge.ok() && HudBridge.poll(pollOut)) {
                int nctx = pollOut[0] & 0xFF;
                boolean ndoor = (pollOut[0] & HudBridge.DOOR_BIT) != 0;
                boolean nmov = (pollOut[0] & HudBridge.MOVIE_BIT) != 0;
                guestW = pollOut[1];
                guestH = pollOut[2];
                if (nctx == HudBridge.CTX_MENU || (pollOut[0] & HudBridge.GUI_BIT) != 0) everMenu = true;
                if (nctx != ctx) {
                    releaseAll(); /* buttons live everywhere: drop holds on any flip */
                    ctx = nctx;
                    changed = true;
                }
                if (ndoor != doorNear) {
                    doorNear = ndoor;
                    changed = true;
                }
                if (nmov != movie) {
                    movie = nmov;
                    changed = true;
                }
            }
            if (changed) invalidate();
            handler.postDelayed(this, 100);
        }
    };

    /* pending tap releases + menu arrow repeat — scheduled only while busy */
    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            long now = SystemClock.uptimeMillis();
            for (int i = nPend - 1; i >= 0; i--)
                if (pendDue[i] <= now) fireRelease(i);
            /* held D-pad arrows repeat exactly like a keyboard: WM keyup +
             * keydown pairs on the same 280 ms cadence the menu stick uses */
            boolean arrowRep = false;
            for (Ptr q : ptrs) {
                if (q.role == R_BTN && q.btn >= 0 && BASE[q.btn].kind == K_ARROW
                        && q.repAt != 0) {
                    if (now >= q.repAt) {
                        HudBridge.post(HudBridge.EV_KEY, BASE[q.btn].dik, 0x100);
                        HudBridge.post(HudBridge.EV_KEY, BASE[q.btn].dik, 0x100 | 1);
                        q.repAt = now + 280;
                    }
                    arrowRep = true;
                }
            }
            if (menuArrowRepAt != 0) {
                if (now >= menuArrowRepAt) {
                    repeatMenuArrow();
                    menuArrowRepAt = now + 280;
                }
                handler.postDelayed(this, 16);
            } else if (nPend > 0) {
                handler.postDelayed(this, 16);
            } else if (arrowRep) {
                handler.postDelayed(this, 16);
            }
        }
    };

    private void fireRelease(int i) {
        if (pendIsMouse[i]) HudBridge.post(HudBridge.EV_MBTN, pendTarget[i], 0);
        else HudBridge.post(HudBridge.EV_KEY, pendTarget[i], 0x100); /* WM keyup */
        pendTarget[i] = pendTarget[nPend - 1];
        pendIsMouse[i] = pendIsMouse[nPend - 1];
        pendDue[i] = pendDue[nPend - 1];
        nPend--;
    }

    private void pendAdd(int target, boolean mouse) {
        long now = SystemClock.uptimeMillis();
        for (int i = 0; i < nPend; i++)
            if (pendTarget[i] == target && pendIsMouse[i] == mouse) return;
        if (nPend == pendTarget.length) {
            /* table full: release immediately instead of leaking a stuck key */
            if (mouse) HudBridge.post(HudBridge.EV_MBTN, target, 0);
            else HudBridge.post(HudBridge.EV_KEY, target, 0x100);
            return;
        }
        pendTarget[nPend] = target;
        pendIsMouse[nPend] = mouse;
        pendDue[nPend] = now + 120;
        nPend++;
        handler.removeCallbacks(tick);
        handler.postDelayed(tick, 16);
    }

    /* ------------------------------------------------------------------ */
    /* Input                                                               */
    /* ------------------------------------------------------------------ */
    @Override
    public boolean onTouchEvent(MotionEvent ev) {
        if (!HudBridge.ok()) return false; /* no runtime: stay passive */
        final int act = ev.getActionMasked();
        final int idx = ev.getActionIndex();
        /* fresh context on every DOWN: roles must never be assigned from a
         * stale context right after a movie/menu/mission flip */
        if (act == MotionEvent.ACTION_DOWN || act == MotionEvent.ACTION_POINTER_DOWN) {
            if (HudBridge.poll(pollOut)) {
                int nctx = pollOut[0] & 0xFF;
                boolean ndoor = (pollOut[0] & HudBridge.DOOR_BIT) != 0;
                guestW = pollOut[1];
                guestH = pollOut[2];
                if (nctx == HudBridge.CTX_MENU || (pollOut[0] & HudBridge.GUI_BIT) != 0) everMenu = true;
                movie = (pollOut[0] & HudBridge.MOVIE_BIT) != 0;
                if (nctx != ctx) {
                    releaseAll();
                    ctx = nctx;
                    invalidate();
                }
                if (ndoor != doorNear) {
                    doorNear = ndoor;
                    invalidate();
                }
            }
        }

        switch (act) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                down(ev.getPointerId(idx), ev.getX(idx), ev.getY(idx));
                break;
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < ev.getPointerCount(); i++)
                    move(ev.getPointerId(i), ev.getX(i), ev.getY(i), ev);
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                up(ev.getPointerId(idx));
                break;
            case MotionEvent.ACTION_CANCEL:
                releaseAll();
                break;
            default:
                return super.onTouchEvent(ev);
        }
        return true; /* every finger is ours: the game sees synthesized input only */
    }

    private Ptr ptr(int id) {
        for (Ptr p : ptrs) if (p.role != R_NONE && p.id == id) return p;
        return null;
    }

    private void down(int id, float x, float y) {
        Ptr p = ptr(id);
        if (p == null) {
            for (Ptr q : ptrs) {
                if (q.role == R_NONE) { p = q; break; }
            }
            if (p == null) return; /* 10 fingers: ignore the rest */
            p.id = id;
        }
        p.lx = x;
        p.ly = y;

        /* buttons live in EVERY context (always visible, always active):
         * hit test first, then the grid, then the stick zone; a free
         * finger looks around in missions and is the game's own mouse in
         * menus, movies and boot. */
        int bi = hitBase(x, y);
        if (bi >= 0) {
            p.role = R_BTN;
            pressBtn(p, bi);
        } else if (gridOpen) {
            int gi = hitGrid(x, y);
            p.role = R_BTN;
            p.btn = -1;
            if (gi >= 0) {
                pressGrid(p, gi);
            } else if (gi == -1) {
                gridOpen = false; /* tap outside the panel closes it */
                invalidate();
            }
            /* gi == -2: inside the panel between slots: consume, do nothing */
        } else if (inZone(x, y)) {
            if (stickHeld) {
                p.role = R_BTN;   /* second stick finger: consumed */
                p.btn = -1;
            } else {
                p.role = R_STICK;
                p.sx = x;
                p.sy = y;
                stickHeld = true;
                stickVX = stickVY = 0;
            }
        } else if (ctx == HudBridge.CTX_MISSION) {
            p.role = R_LOOK;
        } else if (movie && !everMenu) {
            /* startup movies: no GUI exists yet and the guest never pumps its
             * message queue there, so a free finger is the skip key (ESC)
             * instead of the menu mouse. The runtime delivers the WM straight
             * into the game's WndProc while a .bik is open. everMenu latches
             * once the menu exists: the menu's own background video must keep
             * taking real clicks. */
            p.role = R_BTN;
            p.btn = -1;
            HudBridge.post(HudBridge.EV_KEY, DIK_ESC, 0x100 | 1);
            pendAdd(DIK_ESC, false);
        } else { /* MENU / UNKNOWN: the finger is the game's mouse */
            p.role = R_MENU;
            menuMove(x, y);
            if (++nMenuFingers == 1) HudBridge.post(HudBridge.EV_MENUCLICK, 1, 0);
        }
        invalidate();
    }

    private void move(int id, float x, float y, MotionEvent ev) {
        Ptr p = ptr(id);
        if (p == null) return;
        switch (p.role) {
            case R_STICK: {
                float vx = x - p.sx, vy = y - p.sy;
                float mag = (float) Math.sqrt(vx * vx + vy * vy);
                if (mag > stickR) {
                    vx = vx / mag * stickR;
                    vy = vy / mag * stickR;
                }
                stickVX = vx;
                stickVY = vy;
                setStickSector(sector(vx, vy, stickDead, stickSector));
                invalidate();
                break;
            }
            case R_LOOK: {
                /* historical batch takes pointer INDICES, not ids */
                final int pi = ev.findPointerIndex(id);
                if (pi < 0) break;
                float dx = 0, dy = 0;
                /* one post per MotionEvent covering the full path */
                final int hs = ev.getHistorySize();
                for (int h = 0; h <= hs; h++) {
                    float hx = h < hs ? ev.getHistoricalX(pi, h) : x;
                    float hy = h < hs ? ev.getHistoricalY(pi, h) : y;
                    dx += hx - p.lx;
                    dy += hy - p.ly;
                    p.lx = hx;
                    p.ly = hy;
                }
                if (dx != 0 || dy != 0) {
                    int mx = Math.round(dx * cfgSens);
                    int my = Math.round((cfgInvertY ? -dy : dy) * cfgSens);
                    if (mx != 0 || my != 0) HudBridge.post(HudBridge.EV_LOOK, mx, my);
                }
                break;
            }
            case R_MENU:
                menuMove(x, y);
                break;
            default:
                break; /* buttons do not drag */
        }
    }

    private void up(int id) {
        Ptr p = ptr(id);
        if (p == null) return;
        if (p.role == R_STICK) {
            stickHeld = false;
            stickVX = stickVY = 0;
            setStickSector(0);
        } else if (p.role == R_BTN) {
            if (p.btn >= 0) {
                pressed[p.btn] = false;
                releaseBtn(p, p.btn);
                p.btn = -1;
            }
        } else if (p.role == R_MENU) {
            if (--nMenuFingers <= 0) {
                nMenuFingers = 0;
                HudBridge.post(HudBridge.EV_MENUCLICK, 0, 0);
            }
        }
        p.role = R_NONE;
        p.id = 0;
        invalidate();
    }

    /** Releases everything: context flip, ACTION_CANCEL, app loss, pause. */
    public void releaseAll() {
        for (Ptr p : ptrs) {
            if (p.role == R_BTN && p.btn >= 0) {
                pressed[p.btn] = false;
                releaseBtn(p, p.btn);
            }
            p.role = R_NONE;
            p.id = 0;
            p.btn = -1;
            p.repAt = 0;
        }
        stickHeld = false;
        stickVX = stickVY = 0;
        menuArrowRepAt = 0;
        setStickSector(0);
        while (nPend > 0) fireRelease(nPend - 1);
        if (nMenuFingers > 0) {
            nMenuFingers = 0;
            HudBridge.post(HudBridge.EV_MENUCLICK, 0, 0);
        }
        gridOpen = false;
        invalidate();
    }

    /* ------------- button semantics (the game's own binds, nothing made up) -- */
    private void pressBtn(Ptr p, int bi) {
        p.btn = bi;
        Btn b = BASE[bi];
        int kind = b.kind;
        if (bi == B_ZOOM && cfgHoldZoom) kind = K_KHOLD;
        if (bi == B_AGACH && cfgHoldCrouch) kind = K_KHOLD;
        switch (kind) {
            case K_MHOLD:
                pressed[bi] = true;
                HudBridge.post(HudBridge.EV_MBTN, b.mbtn, 1);
                break;
            case K_MTAP:
                HudBridge.post(HudBridge.EV_MBTN, b.mbtn, 1);
                pendAdd(b.mbtn, true);
                break;
            case K_KHOLD:
                pressed[bi] = true;
                HudBridge.post(HudBridge.EV_KEY, b.dik, 1);
                break;
            case K_KTAP:
                HudBridge.post(HudBridge.EV_KEY, b.dik, 0x100 | 1);
                pendAdd(b.dik, false);
                break;
            case K_GRID:
                gridOpen = !gridOpen;
                if (gridOpen) layoutGrid();
                invalidate();
                break;
            case K_ARROW:
                pressed[bi] = true;
                HudBridge.post(HudBridge.EV_KEY, b.dik, 0x100 | 1); /* key down + WM */
                p.repAt = SystemClock.uptimeMillis() + 280;
                handler.removeCallbacks(tick);
                handler.postDelayed(tick, 280);
                break;
            default:
                break;
        }
    }

    private void releaseBtn(Ptr p, int bi) {
        /* holds drop with the LAST finger on that button */
        for (Ptr q : ptrs) {
            if (q != p && q.role == R_BTN && q.btn == bi) return;
        }
        Btn b = BASE[bi];
        if (b.kind == K_MHOLD) HudBridge.post(HudBridge.EV_MBTN, b.mbtn, 0);
        else if (b.kind == K_KHOLD) HudBridge.post(HudBridge.EV_KEY, b.dik, 0);
        else if (b.kind == K_ARROW) {
            HudBridge.post(HudBridge.EV_KEY, b.dik, 0x100); /* WM keyup */
            for (Ptr q : ptrs)
                if (q.role == R_BTN && q.btn == bi) q.repAt = 0;
        }
    }

    private void pressGrid(Ptr p, int gi) {
        p.btn = -1;
        if (gi == 10) {
            gridPage ^= 1;
            layoutGrid();
            invalidate();
            return;
        }
        Btn b = grid[gi];
        if (b == null) return;
        HudBridge.post(HudBridge.EV_KEY, b.dik, 0x100 | 1);
        pendAdd(b.dik, false);
    }

    private int hitBase(float x, float y) {
        for (int i = 0; i < B_NBASE; i++) {
            /* PORTA is never hidden; doorNear only brightens it */
            if (x >= bx[i] && x < bx[i] + bsz[i] && y >= by[i] && y < by[i] + bsz[i]) return i;
        }
        return -1;
    }

    private int hitGrid(float x, float y) {
        if (x < panel[0] || y < panel[1] || x >= panel[0] + panel[2] || y >= panel[1] + panel[3])
            return -1; /* outside the panel */
        for (int i = 0; i < GRID_N; i++)
            if (x >= gx[i] && x < gx[i] + gsz[i] && y >= gy[i] && y < gy[i] + gsz[i]) return i;
        return -2; /* inside the panel, between slots */
    }

    private boolean inZone(float x, float y) {
        return x >= zoneL && x < zoneR && y >= zoneT && y < zoneB;
    }

    /* ---------------- stick sectors (hysteresis, as the C overlay had) -- */
    private int sector(float vx, float vy, float dead, int prev) {
        float mag = (float) Math.sqrt(vx * vx + vy * vy);
        if (mag <= dead || mag <= 0) return 0;
        float nx = vx / mag, ny = vy / mag;
        final float on = 0.44f, off = 0.33f; /* sin 26 deg / sin 19 deg */
        int mask = 0;
        if ((prev & 1) != 0) { if (nx < -off) mask |= 1; } else if (nx < -on) mask |= 1;
        if ((prev & 2) != 0) { if (nx > off) mask |= 2; } else if (nx > on) mask |= 2;
        if ((prev & 4) != 0) { if (ny < -off) mask |= 4; } else if (ny < -on) mask |= 4;
        if ((prev & 8) != 0) { if (ny > off) mask |= 8; } else if (ny > on) mask |= 8;
        return mask;
    }

    private static final int[] SECTOR_DIK = {DIK_A, DIK_D, DIK_W, DIK_S};
    private static final int[] MENU_DIK = {DIK_LEFT, DIK_RIGHT, DIK_UP, DIK_DOWN};

    private void setStickSector(int cur) {
        int old = stickSector;
        stickSector = cur;
        int delta = old ^ cur;
        if (delta == 0) return;
        boolean menu = ctx != HudBridge.CTX_MISSION;
        for (int i = 0; i < 4; i++) {
            int bit = 1 << i;
            if ((delta & bit) == 0) continue;
            if (menu) {
                /* menus: the game's own arrow navigation (UE2 GUI focus) */
                HudBridge.post(HudBridge.EV_KEY, MENU_DIK[i],
                        (cur & bit) != 0 ? 0x100 | 1 : 0x100);
            } else {
                /* movement: polled DIK state (no WM), edges only */
                HudBridge.post(HudBridge.EV_KEY, SECTOR_DIK[i], (cur & bit) != 0 ? 1 : 0);
            }
        }
        if (menu) {
            if (cur == 0) {
                menuArrowRepAt = 0; /* sector released: stop repeating */
            } else if (old == 0) {
                menuArrowRepAt = SystemClock.uptimeMillis() + 280;
                handler.removeCallbacks(tick);
                handler.postDelayed(tick, 280);
            }
        }
    }

    /** Auto-repeat while a menu direction is held: release + press, exactly
     * like a keyboard does it (the WM keydown is what moves GUI focus). */
    private void repeatMenuArrow() {
        for (int i = 0; i < 4; i++) {
            if ((stickSector & (1 << i)) != 0) {
                HudBridge.post(HudBridge.EV_KEY, MENU_DIK[i], 0x100);
                HudBridge.post(HudBridge.EV_KEY, MENU_DIK[i], 0x100 | 1);
                return;
            }
        }
        menuArrowRepAt = 0;
    }

    /* menu mouse: view px -> guest client px (1:1 with the game's render) */
    private void menuMove(float x, float y) {
        if (guestW <= 0 || guestH <= 0 || vw <= 0 || vh <= 0) return;
        HudBridge.post(HudBridge.EV_MMOVE,
                (int) (x * guestW / vw), (int) (y * guestH / vh));
    }

    /* ------------------------------------------------------------------ */
    /* Drawing: preallocated objects only, only when something changed     */
    /* ------------------------------------------------------------------ */
    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int alpha = (int) (255 * cfgOpacity);

        /* stick: fixed anchor, visible in EVERY context (ghost knob when
         * idle); in missions it moves, in menus it navigates */
        float lw = Math.max(2f, u * 1.2f);
        pCircle.setColor(Color.argb(alpha / 3, 0, 0, 0));
        canvas.drawCircle(anchorX, anchorY, stickR, pCircle);
        pStroke.setStrokeWidth(lw);
        pStroke.setColor(Color.argb(alpha / 2, 255, 255, 255));
        canvas.drawCircle(anchorX, anchorY, stickR, pStroke);
        pCircle.setColor(Color.argb(Math.min(255, alpha + 60), 235, 235, 235));
        canvas.drawCircle(anchorX + stickVX, anchorY + stickVY, stickR * 0.38f, pCircle);

        /* every button, in EVERY context (PORTA brightens near a door) */
        for (int i = 0; i < B_NBASE; i++) {
            drawBtn(canvas, baseIcon[i], BASE[i].label, bx[i], by[i], bsz[i],
                    pressed[i], i == B_PORTA && doorNear, alpha);
        }
        /* D-pad chevrons: fallback glyph only when the dpad_*.png icon is
         * absent (the icon is drawn by the button loop above); the paths
         * are only stroked here, never rebuilt per frame */
        for (int i = B_UP; i <= B_RIGHT; i++) {
            if (baseIcon[i] != null) continue;
            pStroke.setStrokeWidth(Math.max(2f, bsz[i] * 0.1f));
            pStroke.setColor(pressed[i]
                    ? Color.argb(Math.min(255, alpha + 70), 255, 255, 255)
                    : Color.argb(alpha, 255, 255, 255));
            canvas.drawPath(chev[i - B_UP], pStroke);
        }
        if (gridOpen) drawGrid(canvas, alpha);
    }

    private void drawBtn(Canvas c, Bitmap icon, String label, float x, float y,
                         float sz, boolean down, boolean accent, int alpha) {
        rf.set(x, y, x + sz, y + sz);
        float rad = sz * 0.24f;
        pFill.setColor(down ? Color.argb(Math.min(255, alpha + 50), 40, 52, 66)
                : Color.argb(alpha * 2 / 3, 12, 16, 22));
        c.drawRoundRect(rf, rad, rad, pFill);
        pStroke.setStrokeWidth(Math.max(2f, u * 1.2f));
        pStroke.setColor(accent ? Color.argb(alpha, 79, 195, 247)
                : Color.argb(alpha, 255, 255, 255));
        c.drawRoundRect(rf, rad, rad, pStroke);
        if (cfgLabels) {
            if (icon != null) {
                /* the icon replaces the text label; the bitmaps carry their
                 * own 96/128 safe area, so they are inset from the shell */
                float isz = sz * 0.82f;
                float ins = (sz - isz) * 0.5f;
                ri.set(x + ins, y + ins, x + ins + isz, y + ins + isz);
                pIcon.setAlpha(Math.min(255, alpha + 70));
                c.drawBitmap(icon, null, ri, pIcon);
            } else if (label != null) {
                pText.setTextSize(Math.max(9f, sz * 0.22f));
                pText.setColor(Color.argb(Math.min(255, alpha + 70), 255, 255, 255));
                float ty = y + sz * 0.5f - (pText.ascent() + pText.descent()) * 0.5f;
                c.drawText(label, x + sz * 0.5f, ty, pText);
            }
        }
    }

    private void drawGrid(Canvas c, int alpha) {
        pFill.setColor(Color.argb(alpha * 3 / 4, 8, 10, 14));
        rf.set(panel[0], panel[1], panel[0] + panel[2], panel[1] + panel[3]);
        c.drawRoundRect(rf, 12 * u, 12 * u, pFill);
        pStroke.setStrokeWidth(Math.max(2f, u * 1.2f));
        pStroke.setColor(Color.argb(alpha, 255, 255, 255));
        c.drawRoundRect(rf, 12 * u, 12 * u, pStroke);
        for (int i = 0; i < GRID_N; i++) {
            Btn b = grid[i];
            if (b == null) continue;
            Bitmap ic;
            if (i == 10) ic = gridPage != 0 ? icToggleNum : icToggleFx;
            else ic = gridPage != 0 ? fxIcon[i] : slotIcon[i];
            drawBtn(c, ic, b.label, gx[i], gy[i], gsz[i], false, i == 10, alpha);
        }
    }
}
