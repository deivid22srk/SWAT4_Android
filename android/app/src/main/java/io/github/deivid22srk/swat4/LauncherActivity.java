package io.github.deivid22srk.swat4;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;

/* Start screen: choose the SWAT 4 install root (the folder with Content/ and
 * ContentExpansion/), verify the supported build by size + SHA-1, list what is
 * missing, set port options, press Play. The folder is passed to the native
 * side as argv[1]; logs and saves go to <root>/port, never Android/data. */
public class LauncherActivity extends Activity {
    private static final int REQ_FOLDER = 1, REQ_PERM = 2;

    /* The exe this port was recompiled from: GOG Gold build (BuildTime
     * 2005-06-08). A different build has code at other addresses; every fixed
     * address in runtime/ would have to be redone. See docs/RECON.md. */
    static final long EXE_SIZE = 180224L;
    static final String EXE_SHA1 = "94d1a40f8f60b6664f539cab4c38a444b7c4a1e7";

    /* DLLs the runtime loads at start-up; the launcher names the missing ones. */
    static final String[] REQUIRED_SYSTEM = {
            "Core.dll", "Engine.dll", "Window.dll", "WinDrv.dll", "D3DDrv.dll",
            "ALAudio.dll", "DefOpenAL32.dll", "GUI.dll", "binkw32.dll",
            "SwatGame.dll", "SwatGui.dll"};

    private SharedPreferences prefs;
    private TextView folderText, folderStatus;
    private Button playButton;
    private Spinner scale, layout;
    private CheckBox touch, nintendo, skipMovies, verbose;
    private boolean pendingPick, pendingPlay, askedAllFiles;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences("launcher", MODE_PRIVATE);
        buildUi();
        refreshFolder();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshFolder();
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private TextView text(LinearLayout parent, String s, int sp, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(Color.rgb(230, 225, 210));
        if (bold) t.setTypeface(null, android.graphics.Typeface.BOLD);
        t.setPadding(0, dp(6), 0, dp(2));
        parent.addView(t);
        return t;
    }

    private Spinner spinner(LinearLayout parent, String key, String label, String[] items, int def) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        TextView t = new TextView(this);
        t.setText(label);
        t.setTextSize(16);
        t.setTextColor(Color.rgb(230, 225, 210));
        row.addView(t, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        Spinner s = new Spinner(this);
        android.widget.ArrayAdapter<String> a = new android.widget.ArrayAdapter<>(
                this, android.R.layout.simple_spinner_item, items);
        a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        s.setAdapter(a);
        s.setSelection(Math.min(prefs.getInt(key, def), items.length - 1));
        row.addView(s, new LinearLayout.LayoutParams(dp(200), ViewGroup.LayoutParams.WRAP_CONTENT));
        parent.addView(row);
        return s;
    }

    private CheckBox check(LinearLayout parent, String key, String label, boolean def) {
        CheckBox c = new CheckBox(this);
        c.setText(label);
        c.setTextSize(16);
        c.setTextColor(Color.rgb(230, 225, 210));
        c.setChecked(prefs.getBoolean(key, def));
        parent.addView(c);
        return c;
    }

    private static final String[] SCALES = {"1.0 (native)", "0.9", "0.8", "0.7", "0.6", "0.5", "0.4", "0.3", "0.2"};
    private static final String[] LAYOUTS = {"Xbox (A/B, X/Y)", "Nintendo (A/B, X/Y swapped)"};

    private void buildUi() {
        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(Color.rgb(18, 20, 26));
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(16), dp(24), dp(24));
        sv.addView(root);

        text(root, "SWAT 4", 28, true);
        text(root, "Android port \u2014 static recompilation of the PC 2005 build", 14, false);

        text(root, "Game folder", 20, true);
        folderText = text(root, "", 14, false);
        folderStatus = text(root, "", 14, false);
        Button pick = new Button(this);
        pick.setText("Choose game folder");
        pick.setOnClickListener(v -> {
            if (hasStorage()) openPicker();
            else { pendingPick = true; requestStorage(); }
        });
        root.addView(pick);

        text(root, "Options", 20, true);
        scale = spinner(root, "scale", "Resolution scale", SCALES, 0);
        layout = spinner(root, "layout", "Controller layout", LAYOUTS, 0);
        touch = check(root, "touch", "Touch controls overlay (not implemented yet)", true);
        nintendo = check(root, "nintendo", "Nintendo button layout on controllers", false);
        skipMovies = check(root, "skipmovies", "Skip Bink cutscenes (experimental)", false);
        verbose = check(root, "verbose", "Verbose log (port/log.txt)", true);

        playButton = new Button(this);
        playButton.setText("Play");
        playButton.setTextSize(22);
        playButton.setOnClickListener(v -> play());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(64));
        lp.topMargin = dp(16);
        root.addView(playButton, lp);
        setContentView(sv);
    }

    /* Storage: the app targets API 29 with legacy storage, so the normal
     * "allow access to files" prompt gives the game plain file access. If the
     * device ignores legacy storage, all-files access is asked for once. This
     * is the same approach as the reference ports: the game must not live in
     * Android/data (Android forbids another app from reading it). */
    private boolean hasStorage() {
        return checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
                || (Build.VERSION.SDK_INT >= 30 && Environment.isExternalStorageManager());
    }

    private void requestStorage() {
        requestPermissions(new String[] {android.Manifest.permission.READ_EXTERNAL_STORAGE,
                android.Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQ_PERM);
    }

    @Override
    public void onRequestPermissionsResult(int req, String[] perms, int[] res) {
        super.onRequestPermissionsResult(req, perms, res);
        if (req != REQ_PERM) return;
        boolean ok = hasStorage();
        if (ok && pendingPick) openPicker();
        if (ok && pendingPlay) play();
        pendingPick = pendingPlay = false;
        refreshFolder();
    }

    private boolean hasAllFiles() {
        return Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager();
    }

    private void requestAllFiles() {
        try {
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName())));
        } catch (Exception e) {
            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        }
    }

    private void openPicker() {
        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_FOLDER);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_FOLDER || res != RESULT_OK || data == null || data.getData() == null) return;
        String path = treeToPath(data.getData());
        if (path == null) {
            folderStatus.setText("That location is not a plain folder; choose one on internal storage or the SD card.");
            return;
        }
        prefs.edit().putString("folder", path).apply();
        List<String> bad = checkFolder(path);
        refreshFolder();
        if (!askedAllFiles && !hasAllFiles() && unreadable(bad)) {
            askedAllFiles = true;
            requestAllFiles();
        }
    }

    /* content://...tree/primary:Games/SWAT4 -> /storage/emulated/0/Games/SWAT4 */
    private static String treeToPath(Uri tree) {
        String id;
        try { id = DocumentsContract.getTreeDocumentId(tree); } catch (Exception e) { return null; }
        int c = id.indexOf(':');
        if (c < 0) return null;
        String vol = id.substring(0, c), rel = id.substring(c + 1);
        String base = vol.equalsIgnoreCase("primary") ? Environment.getExternalStorageDirectory().getPath()
                : "/storage/" + vol;
        return rel.isEmpty() ? base : base + "/" + rel;
    }

    private static File findCi(File dir, String name) {
        File f = new File(dir, name);
        if (f.exists()) return f;
        String[] l = dir.list();
        if (l != null) for (String s : l) if (s.equalsIgnoreCase(name)) return new File(dir, s);
        return null;
    }

    private String sha1(File f) {
        String key = "sha1:" + f.getAbsolutePath(), stamp = f.length() + ":" + f.lastModified();
        String cached = prefs.getString(key, null);
        if (cached != null && cached.startsWith(stamp + "=")) return cached.substring(stamp.length() + 1);
        try (java.io.InputStream in = new FileInputStream(f)) {
            MessageDigest md = MessageDigest.getInstance("SHA-1");
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0; ) md.update(buf, 0, n);
            StringBuilder sb = new StringBuilder();
            for (byte x : md.digest()) sb.append(String.format("%02x", x));
            String hex = sb.toString();
            prefs.edit().putString(key, stamp + "=" + hex).apply();
            return hex;
        } catch (Exception e) {
            return null;
        }
    }

    private boolean unreadable(List<String> bad) {
        for (String b : bad) if (b.startsWith("cannot read")) return true;
        return false;
    }

    /* Everything that is missing or wrong with the chosen folder. */
    private List<String> checkFolder(String path) {
        List<String> bad = new ArrayList<>();
        if (path == null) { bad.add("no folder chosen"); return bad; }
        File root = new File(path);
        if (!root.isDirectory()) { bad.add("folder not found"); return bad; }
        File content = findCi(root, "Content");
        if (content == null) {
            bad.add(root.canRead() ? "Content/ not found \u2014 choose the SWAT 4 install root"
                                   : "cannot read the folder");
            return bad;
        }
        File sys = findCi(content, "System");
        if (sys == null) { bad.add("Content/System/ not found"); return bad; }

        File exe = findCi(sys, "Swat4.exe");
        if (exe == null) {
            bad.add("Content/System/Swat4.exe missing");
        } else if (!exe.canRead()) {
            bad.add("cannot read Swat4.exe");
        } else if (exe.length() != EXE_SIZE) {
            bad.add("exe size " + exe.length() + " (expected " + EXE_SIZE + "): unsupported build");
        } else {
            String got = sha1(exe);
            if (got == null) bad.add("cannot read the exe to check its SHA-1");
            else if (!got.equals(EXE_SHA1))
                bad.add("exe SHA-1 " + got.substring(0, 8) + ", expected " + EXE_SHA1.substring(0, 8)
                        + ": unsupported build");
        }
        for (String dll : REQUIRED_SYSTEM)
            if (findCi(sys, dll) == null) bad.add(dll + " missing");
        if (findCi(root, "ContentExpansion") == null)
            bad.add("ContentExpansion/ missing (base game can still run; expansion is not installed)");
        return bad;
    }

    private boolean blocking(List<String> bad) {
        for (String b : bad)
            if (!b.startsWith("ContentExpansion/")) return true;
        return false;
    }

    private void refreshFolder() {
        String path = prefs.getString("folder", null);
        folderText.setText(path == null
                ? "No folder chosen. Copy your PC install anywhere outside Android/data "
                  + "(e.g. Internal storage/Games/SWAT4) and choose the folder that contains Content/."
                : path);
        List<String> bad = checkFolder(path);
        folderStatus.setText(bad.isEmpty() ? "Ready" : (blocking(bad) ? "Not ready: " : "Warning: ")
                + String.join("; ", bad));
        folderStatus.setTextColor(bad.isEmpty() ? Color.rgb(140, 200, 120) : Color.rgb(230, 150, 110));
        playButton.setEnabled(!blocking(bad));
    }

    private void play() {
        if (!hasStorage()) { pendingPlay = true; requestStorage(); return; }
        String folder = prefs.getString("folder", null);
        if (folder == null) return;
        prefs.edit()
                .putInt("scale", scale.getSelectedItemPosition())
                .putInt("layout", layout.getSelectedItemPosition())
                .putBoolean("touch", touch.isChecked())
                .putBoolean("nintendo", nintendo.isChecked())
                .putBoolean("skipmovies", skipMovies.isChecked())
                .putBoolean("verbose", verbose.isChecked())
                .apply();
        try {
            File port = new File(folder, "port");
            if (!port.isDirectory() && !port.mkdirs()) throw new java.io.IOException("cannot create " + port);
            try (Writer w = new OutputStreamWriter(new FileOutputStream(new File(port, "port.cfg")))) {
                w.write("# written by the launcher; the native side reads log= at start-up\n");
                w.write("scale=" + String.format(java.util.Locale.US, "%.1f", 1.0 - 0.1 * scale.getSelectedItemPosition()) + "\n");
                w.write("touch=" + (touch.isChecked() ? 1 : 0) + "\n");
                w.write("nintendo=" + ((nintendo.isChecked() || layout.getSelectedItemPosition() == 1) ? 1 : 0) + "\n");
                w.write("skipmovies=" + (skipMovies.isChecked() ? 1 : 0) + "\n");
                w.write("log=" + (verbose.isChecked() ? 2 : 1) + "\n");
            }
        } catch (Exception ex) {
            folderStatus.setText("Could not write the settings: " + ex);
            return;
        }
        Intent i = new Intent(this, GameActivity.class);
        i.putExtra("game", folder);
        startActivity(i);
        finish();
    }
}