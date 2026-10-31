package io.github.deivid22srk.swat4;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.os.Build;
import android.os.Bundle;
import java.io.File;
import java.io.FileWriter;
import java.util.Date;
import java.util.List;
import org.libsdl.app.SDLActivity;

public class GameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "dxvk_d3d9", "main" };
    }

    /* argv[1] = game folder, from the launcher. */
    @Override
    protected String[] getArguments() {
        String game = getIntent().getStringExtra("game");
        return game == null ? new String[0] : new String[] { game };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        saveLastExit();
        super.onCreate(savedInstanceState);
        installHud();
    }

    @Override
    protected void onPause() {
        /* a held FOGO/CORRER must not survive the app going away */
        if (hud != null) hud.releaseAll();
        super.onPause();
    }

    private HudOverlay hud; /* kept for onPause: drop held buttons */

    /* The contextual touch HUD: a real Android View composited above the
     * game surface (visible through menus, movies and loading frames) that
     * converts touches into the game's own input via HudBridge. SDLActivity
     * built its RelativeLayout and SDLSurface inside super.onCreate(), so
     * adding ours to the content frame now puts it deterministically on
     * top; if the runtime bridge is missing the view stays passive. */
    private void installHud() {
        try {
            android.view.ViewGroup content =
                    (android.view.ViewGroup) findViewById(android.R.id.content);
            if (content == null) return;
            hud = new HudOverlay(this, getIntent().getStringExtra("game"));
            content.addView(hud, new android.view.ViewGroup.LayoutParams(
                    android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                    android.view.ViewGroup.LayoutParams.MATCH_PARENT));
        } catch (Throwable ignored) {
            /* a HUD failure must never take the game down */
        }
    }

    /* Why the previous run ended (crash, low-memory kill, ...), kept in
     * <game>/port/last_exit.txt: log.txt is replaced on every launch. */
    private void saveLastExit() {
        if (Build.VERSION.SDK_INT < 30) return;
        String game = getIntent().getStringExtra("game");
        if (game == null) return;
        try {
            ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
            List<ApplicationExitInfo> l = am.getHistoricalProcessExitReasons(null, 0, 3);
            File dir = new File(game, "port");
            if (!dir.isDirectory() && !dir.mkdirs()) return;
            try (FileWriter w = new FileWriter(new File(dir, "last_exit.txt"))) {
                if (l.isEmpty()) w.write("no previous exit reported by Android\n");
                for (ApplicationExitInfo e : l) {
                    w.write(new Date(e.getTimestamp()) + ": reason " + reasonName(e.getReason())
                            + " status " + e.getStatus() + " importance " + e.getImportance()
                            + " pss " + e.getPss() / 1024 + "MB rss " + e.getRss() / 1024 + "MB"
                            + " desc " + e.getDescription() + "\n");
                }
                ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
                am.getMemoryInfo(mi);
                w.write("device RAM " + mi.totalMem / (1 << 20) + "MB, available now " + mi.availMem / (1 << 20)
                        + "MB, low-memory threshold " + mi.threshold / (1 << 20) + "MB\n");
            }
        } catch (Exception ignored) {
        }
    }

    private static String reasonName(int r) {
        switch (r) {
        case ApplicationExitInfo.REASON_LOW_MEMORY: return "LOW_MEMORY";
        case ApplicationExitInfo.REASON_CRASH: return "CRASH_JAVA";
        case ApplicationExitInfo.REASON_CRASH_NATIVE: return "CRASH_NATIVE";
        case ApplicationExitInfo.REASON_SIGNALED: return "SIGNALED";
        case ApplicationExitInfo.REASON_ANR: return "ANR";
        case ApplicationExitInfo.REASON_EXIT_SELF: return "EXIT_SELF";
        case ApplicationExitInfo.REASON_USER_REQUESTED: return "USER_REQUESTED";
        case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE: return "EXCESSIVE_RESOURCE_USAGE";
        case ApplicationExitInfo.REASON_OTHER: return "OTHER";
        default: return "code " + r;
        }
    }
}
