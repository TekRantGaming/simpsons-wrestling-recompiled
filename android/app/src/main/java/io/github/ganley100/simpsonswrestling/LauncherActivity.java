package io.github.ganley100.simpsonswrestling;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.OpenableColumns;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.View;
import android.view.ViewGroup;
import android.text.InputType;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.regex.Matcher;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.regex.Pattern;

/**
 * The TV launcher: the screen behind the home-screen banner. It does on
 * Android what the PC launcher (launcher/main.cpp) does: finds the player's
 * own disc image, keeps the settings (launcher.txt for the game's plugin,
 * settings.toml for the runtime) and starts the game.
 *
 * Everything is reachable with a TV remote's D-pad and OK button.
 */
public class LauncherActivity extends Activity {
    private static final int REQ_PICK_DISC = 1;
    private static final int REQ_STORAGE = 2;

    private static final int YELLOW = 0xFFF2C94C;
    private static final int BLUE = 0xFF2F80ED;
    private static final int NAVY = 0xFF1A2B4C;

    private File home;
    private File launcherTxt;
    private final Map<String, String> settings = new LinkedHashMap<>();

    private TextView discLabel;
    private Button playButton;
    private LinearLayout optionList;

    // ------------------------------------------------------------ lifecycle

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        home = getFilesDir();
        launcherTxt = new File(home, "launcher.txt");
        loadSettings();
        buildUi();
        unpackRuntimeData();
        if (discPath() == null) autoFindDisc();
        refresh();
        playButton.requestFocus();
        unpackBundledDisc();
    }

    // ------------------------------------------------------------ settings

    /** One choice on the settings list: launcher.txt key, label, values. */
    private static final class Option {
        final String key, label, def;
        final String[] values, names;
        Option(String key, String label, String def, String[] values, String[] names) {
            this.key = key; this.label = label; this.def = def; this.values = values; this.names = names;
        }
    }

    // Defaults suit a low-end TV box: the original 30 FPS and native
    // resolution. Faster boxes can switch to 60 FPS and 720p.
    private static final Option[] OPTIONS = {
        new Option("frame_rate", "Frame rate", "30",
                new String[] {"30", "60"}, new String[] {"30 FPS (original, lightest)", "60 FPS (smooth, needs a faster box)"}),
        new Option("android_resolution", "Picture", "native",
                new String[] {"native", "720p"}, new String[] {"Original resolution (fastest)", "720p (sharper, slower)"}),
        new Option("widescreen", "Widescreen", "on",
                new String[] {"on", "off"}, new String[] {"16:9", "Off (4:3)"}),
        new Option("android_smooth", "Texture filtering", "off",
                new String[] {"off", "on"}, new String[] {"Off (sharp pixels)", "On (smooth)"}),
        new Option("skip_intro", "Skip intro movies", "on",
                new String[] {"on", "off"}, new String[] {"On", "Off"}),
        new Option("unlock_all", "Unlock everything", "on",
                new String[] {"on", "off"}, new String[] {"On", "Off"}),
        new Option("fast_loading", "Fast loading", "on",
                new String[] {"on", "off"}, new String[] {"On", "Off"}),
        new Option("android_fps_counter", "FPS counter", "off",
                new String[] {"off", "on"}, new String[] {"Off", "On"}),
    };

    private String get(String key, String def) {
        String v = settings.get(key);
        return v == null || v.isEmpty() ? def : v;
    }

    private void loadSettings() {
        settings.clear();
        for (Option o : OPTIONS) settings.put(o.key, o.def);
        for (String line : readLines(launcherTxt)) {
            int eq = line.indexOf('=');
            if (line.startsWith("#") || eq < 0) continue;
            settings.put(line.substring(0, eq).trim(), line.substring(eq + 1).trim());
        }
    }

    private void saveSettings() {
        StringBuilder sb = new StringBuilder(
                "# The Simpsons Wrestling for Android TV: launcher settings.\n"
                + "# Read by the game's plugin (simpsons_mods.c) as on PC.\n");
        for (Map.Entry<String, String> e : settings.entrySet()) {
            sb.append(e.getKey()).append(" = ").append(e.getValue()).append('\n');
        }
        writeText(launcherTxt, sb.toString());
    }

    private String discPath() {
        String d = settings.get("disc");
        if (d == null || d.isEmpty() || !new File(d).canRead()) return null;
        return d;
    }

    // ------------------------------------------------------------ UI

    private int dp(float v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()));
    }

    private Button makeButton(String text, int normal, int textColor, boolean big) {
        Button b = new Button(this);
        b.setAllCaps(false);
        b.setText(text);
        b.setTextColor(textColor);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, big ? 22 : 16);
        b.setTypeface(Typeface.DEFAULT, big ? Typeface.BOLD : Typeface.NORMAL);
        b.setGravity(big ? Gravity.CENTER : Gravity.CENTER_VERTICAL | Gravity.START);
        b.setPadding(dp(18), dp(big ? 14 : 8), dp(18), dp(big ? 14 : 8));
        b.setFocusable(true);
        b.setFocusableInTouchMode(false);

        GradientDrawable idle = new GradientDrawable();
        idle.setColor(normal);
        idle.setCornerRadius(dp(10));
        GradientDrawable focused = new GradientDrawable();
        focused.setColor(normal == YELLOW ? 0xFFFFE07A : 0xFF3A5A94);
        focused.setCornerRadius(dp(10));
        focused.setStroke(dp(3), Color.WHITE);
        StateListDrawable bg = new StateListDrawable();
        bg.addState(new int[] {android.R.attr.state_focused}, focused);
        bg.addState(new int[] {android.R.attr.state_pressed}, focused);
        bg.addState(new int[] {}, idle);
        b.setBackground(bg);
        b.setOnFocusChangeListener((v, has) -> v.animate().scaleX(has ? 1.03f : 1f).scaleY(has ? 1.03f : 1f).setDuration(120).start());

        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, dp(5), 0, dp(5));
        b.setLayoutParams(lp);
        return b;
    }

    private TextView makeText(String text, float sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return t;
    }

    private void buildUi() {
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(NAVY);

        ImageView bg = new ImageView(this);
        bg.setImageResource(R.drawable.cover);
        bg.setScaleType(ImageView.ScaleType.CENTER_CROP);
        bg.setAlpha(0.25f);
        root.addView(bg, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        // TV overscan: keep content inside the title-safe area.
        row.setPadding(dp(48), dp(27), dp(48), dp(27));
        root.addView(row, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // Left: the box art.
        ImageView cover = new ImageView(this);
        cover.setImageResource(R.drawable.cover);
        cover.setAdjustViewBounds(true);
        cover.setScaleType(ImageView.ScaleType.FIT_CENTER);
        LinearLayout.LayoutParams coverLp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 0.8f);
        coverLp.setMargins(0, 0, dp(32), 0);
        row.addView(cover, coverLp);

        // Right: disc, PLAY, settings.
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        row.addView(scroll, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1.2f));
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(col, new ScrollView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        col.addView(makeText("The Simpsons Wrestling", 30, YELLOW, true));
        col.addView(makeText("Recompiled for Android TV  ·  v" + versionName(), 14, 0xFFB0C4E8, false));

        discLabel = makeText("", 15, Color.WHITE, false);
        discLabel.setPadding(0, dp(12), 0, dp(6));
        col.addView(discLabel);

        playButton = makeButton("▶  PLAY", YELLOW, NAVY, true);
        playButton.setOnClickListener(v -> play());
        col.addView(playButton);

        Button pick = makeButton("Choose disc image…", BLUE, Color.WHITE, false);
        pick.setOnClickListener(v -> chooseDisc());
        col.addView(pick);

        Button drive = makeButton("Import from Google Drive link…", BLUE, Color.WHITE, false);
        drive.setOnClickListener(v -> askDriveLink());
        col.addView(drive);

        TextView settingsTitle = makeText("Settings", 18, YELLOW, true);
        settingsTitle.setPadding(0, dp(14), 0, dp(2));
        col.addView(settingsTitle);
        optionList = new LinearLayout(this);
        optionList.setOrientation(LinearLayout.VERTICAL);
        col.addView(optionList);

        Button help = makeButton("Controls and help", 0xFF2A3F66, Color.WHITE, false);
        help.setOnClickListener(v -> showHelp());
        col.addView(help);

        Button log = makeButton("Show last game log", 0xFF2A3F66, Color.WHITE, false);
        log.setOnClickListener(v -> showLastLog());
        col.addView(log);

        setContentView(root);
    }

    private void refresh() {
        String disc = discPath();
        discLabel.setText(disc == null
                ? "No disc image yet. Choose your own copy of The Simpsons Wrestling (PlayStation, USA): a .cue with its .bin, a single .bin, or a .chd."
                : "Disc: " + new File(disc).getName());
        playButton.setEnabled(disc != null);
        playButton.setAlpha(disc != null ? 1f : 0.5f);

        optionList.removeAllViews();
        for (final Option o : OPTIONS) {
            final Button b = makeButton("", 0xFF22365C, Color.WHITE, false);
            b.setText(o.label + ":  " + nameOf(o, get(o.key, o.def)));
            b.setOnClickListener(v -> {
                String cur = get(o.key, o.def);
                int i = 0;
                while (i < o.values.length && !o.values[i].equals(cur)) i++;
                settings.put(o.key, o.values[(i + 1) % o.values.length]);
                saveSettings();
                b.setText(o.label + ":  " + nameOf(o, get(o.key, o.def)));
            });
            optionList.addView(b);
        }
    }

    private static String nameOf(Option o, String value) {
        for (int i = 0; i < o.values.length; i++) if (o.values[i].equals(value)) return o.names[i];
        return value;
    }

    /** The end of the last game run's output (and crash report), to read off the TV. */
    private void showLastLog() {
        StringBuilder sb = new StringBuilder();
        for (String name : new String[] {"psx_crash.txt", "last_run.log"}) {
            List<String> lines = readLines(new File(home, name));
            if (lines.isEmpty()) continue;
            sb.append("── ").append(name).append(" ──\n");
            for (String l : lines.subList(Math.max(0, lines.size() - 40), lines.size())) sb.append(l).append('\n');
            sb.append('\n');
        }
        TextView t = makeText(sb.length() == 0 ? "No game log yet: press PLAY first." : sb.toString(), 11, Color.WHITE, false);
        t.setTypeface(Typeface.MONOSPACE);
        t.setPadding(dp(16), dp(8), dp(16), dp(8));
        t.setFocusable(true);
        ScrollView sv = new ScrollView(this);
        sv.addView(t);
        new AlertDialog.Builder(this)
                .setTitle("Last game log (take a photo of this if the game crashed)")
                .setView(sv)
                .setPositiveButton("OK", null)
                .show();
    }

    private void showHelp() {
        new AlertDialog.Builder(this)
                .setTitle("Controls and help")
                .setMessage(
                    "A gamepad (Bluetooth or USB) is best: it works like a PlayStation controller. "
                    + "With two gamepads, two people can play.\n\n"
                    + "TV remote:  D-pad = D-pad,  OK = ✕,  Back = △,  Menu / Play-Pause = START.\n\n"
                    + "Getting your disc onto the TV:\n"
                    + "• Google Drive: share the disc image as \"Anyone with the link\" (a .chd, a single .bin, or a .zip "
                    + "with the .cue and .bin) and use \"Import from Google Drive link…\".\n"
                    + "• Copy the .cue and .bin (or a .chd) to a USB stick or the Download folder, then use "
                    + "\"Choose disc image…\".\n"
                    + "• Or copy them to Android/data/" + getPackageName() + "/files/disc/ on the TV "
                    + "(adb push works): the game finds them there by itself.\n\n"
                    + "Slow box? Keep 30 FPS and the original resolution. Saves go to memory card files inside the app.\n\n"
                    + "No game data is included: you need your own copy of the game.")
                .setPositiveButton("OK", null)
                .show();
    }

    // ------------------------------------------------------------ runtime data

    private String versionName() {
        try {
            return getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (PackageManager.NameNotFoundException e) {
            return "?";
        }
    }

    /**
     * Copies the runtime's data (game.toml, mods/bundled, BIOS...) from the APK
     * into the game folder, once per installed version. Settings, keybinds and
     * memory cards are never part of it, so they survive updates.
     */
    private void unpackRuntimeData() {
        String stamp;
        try {
            PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            stamp = pi.versionName + "/" + pi.lastUpdateTime;
        } catch (PackageManager.NameNotFoundException e) {
            stamp = "unknown";
        }
        File marker = new File(home, ".runtime-version");
        if (stamp.equals(readText(marker).trim())) return;
        AssetManager am = getAssets();
        try {
            for (String rel : readLines(am.open("runtime/files.txt"))) {
                if (rel.isEmpty()) continue;
                File out = new File(home, rel);
                File parent = out.getParentFile();
                if (parent != null) parent.mkdirs();
                try (InputStream in = am.open("runtime/" + rel); OutputStream os = new FileOutputStream(out)) {
                    copy(in, os, null);
                }
            }
            writeText(marker, stamp);
        } catch (IOException e) {
            toast("Could not unpack the game data: " + e.getMessage());
        }
    }

    // ------------------------------------------------------------ bundled disc

    /**
     * A private one-file build (gradle -PbundleDisc=...) carries the player's
     * disc image in assets/bundled-disc. Unpack it once per installed version
     * and select it, so PLAY works straight after installing.
     */
    private void unpackBundledDisc() {
        final String[] names;
        try {
            names = getAssets().list("bundled-disc");
        } catch (IOException e) {
            return;
        }
        if (names == null || names.length == 0) return;
        String main = null;
        for (String n : names) if (ext(n).equals("cue") || ext(n).equals("chd")) main = n;
        if (main == null) main = names[0];
        final String mainName = main;
        String stamp;
        try {
            stamp = String.valueOf(getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime);
        } catch (PackageManager.NameNotFoundException e) {
            stamp = "unknown";
        }
        final File marker = new File(home, ".bundled-disc");
        final File dir = new File(home, "bundled-disc");
        if (stamp.equals(readText(marker).trim())) {
            // Already unpacked for this version (a .chd may since have been
            // converted to .bin/.cue and removed: discPath() points there).
            if (discPath() == null && new File(dir, mainName).canRead()) { useDisc(new File(dir, mainName)); refresh(); }
            return;
        }
        final String stampFinal = stamp;
        final AlertDialog progress = new AlertDialog.Builder(this)
                .setTitle("Setting up the game")
                .setMessage("First start: unpacking the disc…")
                .setCancelable(false)
                .show();
        new Thread(() -> {
            String error = null;
            try {
                dir.mkdirs();
                File[] old = dir.listFiles();
                if (old != null) for (File f : old) f.delete();
                for (String n : names) {
                    long total = 0;
                    try {
                        total = getAssets().openFd("bundled-disc/" + n).getLength();
                    } catch (IOException ignored) {
                    }
                    final long all = Math.max(total, 1);
                    final long[] done = {0};
                    try (InputStream in = getAssets().open("bundled-disc/" + n);
                         OutputStream os = new FileOutputStream(new File(dir, n))) {
                        copy(in, os, k -> {
                            done[0] += k;
                            final int pct = (int) Math.min(100, done[0] * 100 / all);
                            runOnUiThread(() -> progress.setMessage("First start: unpacking the disc… " + pct + "%"));
                        });
                    }
                }
                writeText(marker, stampFinal);
            } catch (IOException e) {
                error = e.getMessage();
            }
            final String err = error;
            runOnUiThread(() -> {
                progress.dismiss();
                if (err != null) {
                    alert("Could not unpack the game: " + err + "\n\nIs there enough free space on the TV?");
                } else {
                    useDisc(new File(dir, mainName));
                    refresh();
                    playButton.requestFocus();
                }
            });
        }).start();
    }

    // ------------------------------------------------------------ disc image

    private File discDir() {
        File d = new File(home, "disc");
        d.mkdirs();
        return d;
    }

    /** Disc images dropped into Android/data/<package>/files/disc are found without asking. */
    private void autoFindDisc() {
        File ext = getExternalFilesDir("disc");
        List<File> found = new ArrayList<>();
        if (ext != null) scan(ext, 1, found);
        scan(discDir(), 0, found);
        if (!found.isEmpty()) useDisc(found.get(0));
    }

    private void chooseDisc() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        try {
            startActivityForResult(i, REQ_PICK_DISC);
        } catch (ActivityNotFoundException e) {
            // Many Android TVs have no file picker: look in the usual places.
            searchStorage();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_PICK_DISC) return;
        if (resultCode != RESULT_OK || data == null) {
            if (resultCode != RESULT_OK && data == null) offerStorageSearch();
            return;
        }
        List<Uri> uris = new ArrayList<>();
        ClipData clip = data.getClipData();
        if (clip != null) {
            for (int k = 0; k < clip.getItemCount(); k++) uris.add(clip.getItemAt(k).getUri());
        } else if (data.getData() != null) {
            uris.add(data.getData());
        }
        importPicked(uris);
    }

    private void offerStorageSearch() {
        new AlertDialog.Builder(this)
                .setMessage("Look for the disc image in Downloads and on USB drives instead?")
                .setPositiveButton("Search", (d, w) -> searchStorage())
                .setNegativeButton("Cancel", null)
                .show();
    }

    private String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception ignored) {
        }
        String p = uri.getLastPathSegment();
        return p == null ? "disc" : p.substring(p.lastIndexOf('/') + 1);
    }

    private static String ext(String name) {
        int dot = name.lastIndexOf('.');
        return dot < 0 ? "" : name.substring(dot + 1).toLowerCase(Locale.ROOT);
    }

    /** Copies the picked files into the game folder and selects the disc. */
    private void importPicked(final List<Uri> uris) {
        final Map<String, Uri> byName = new LinkedHashMap<>();
        for (Uri u : uris) byName.put(displayName(u), u);
        String cue = null, chd = null;
        List<String> bins = new ArrayList<>();
        for (String n : byName.keySet()) {
            switch (ext(n)) {
                case "cue": cue = n; break;
                case "chd": chd = n; break;
                case "bin": case "img": bins.add(n); break;
                default: break;
            }
        }
        final String mainName;
        String synthCue = null;
        if (chd != null) {
            mainName = chd;
        } else if (cue != null) {
            String cueText = readUri(byName.get(cue));
            for (String f : cueFiles(cueText)) {
                if (!byName.containsKey(f)) {
                    alert("Also select \"" + f + "\" together with the .cue (select both files in the picker), "
                            + "or choose only the .bin.");
                    return;
                }
            }
            mainName = cue;
        } else if (bins.size() == 1) {
            // A lone .bin: The Simpsons Wrestling is a single data track.
            String bin = bins.get(0);
            mainName = bin.substring(0, bin.lastIndexOf('.')) + ".cue";
            synthCue = "FILE \"" + bin + "\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";
        } else {
            alert("Choose a .cue (with its .bin), a single .bin, or a .chd file.");
            return;
        }

        final String cueToWrite = synthCue;
        final AlertDialog progress = new AlertDialog.Builder(this)
                .setTitle("Copying the disc image")
                .setMessage("Starting…")
                .setCancelable(false)
                .show();
        new Thread(() -> {
            File dir = discDir();
            File[] old = dir.listFiles();
            if (old != null) for (File f : old) f.delete();
            long total = 0;
            for (Uri u : byName.values()) total += size(u);
            final long all = Math.max(total, 1);
            final long[] done = {0};
            String error = null;
            try {
                for (Map.Entry<String, Uri> e : byName.entrySet()) {
                    String t = ext(e.getKey());
                    if (!t.equals("cue") && !t.equals("bin") && !t.equals("img") && !t.equals("chd")) continue;
                    try (InputStream in = getContentResolver().openInputStream(e.getValue());
                         OutputStream os = new FileOutputStream(new File(dir, e.getKey()))) {
                        if (in == null) throw new IOException("cannot open " + e.getKey());
                        copy(in, os, n -> {
                            done[0] += n;
                            final int pct = (int) (done[0] * 100 / all);
                            runOnUiThread(() -> progress.setMessage(pct + "%"));
                        });
                    }
                }
                if (cueToWrite != null) writeText(new File(dir, mainName), cueToWrite);
            } catch (IOException ex) {
                error = ex.getMessage();
            }
            final String err = error;
            runOnUiThread(() -> {
                progress.dismiss();
                if (err != null) {
                    alert("Copying failed: " + err + "\n\nIs there enough free space on the TV?");
                } else {
                    useDisc(new File(dir, mainName));
                    refresh();
                    playButton.requestFocus();
                }
            });
        }).start();
    }

    // ------------------------------------------------------------ Google Drive

    /**
     * Asks for a Google Drive share link ("Anyone with the link") to the disc
     * image: a .chd, a single .bin, or a .zip holding the .cue and .bin.
     */
    private void askDriveLink() {
        final EditText input = new EditText(this);
        input.setSingleLine(true);
        input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        input.setHint("https://drive.google.com/file/d/…");
        input.setText(get("drive_link", ""));
        new AlertDialog.Builder(this)
                .setTitle("Import from Google Drive")
                .setMessage("In Google Drive, share your disc image as \"Anyone with the link\" and paste or type the link. "
                        + "Use a .chd, a single .bin, or a .zip holding the .cue and .bin.")
                .setView(input)
                .setPositiveButton("Download", (d, w) -> {
                    String link = input.getText().toString().trim();
                    String id = driveFileId(link);
                    if (id == null) {
                        alert("That does not look like a Google Drive file link.");
                        return;
                    }
                    settings.put("drive_link", link);
                    saveSettings();
                    downloadFromDrive(id);
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    /** The file id in a Drive link (…/file/d/ID/…, …?id=ID…, or a bare id). */
    static String driveFileId(String link) {
        if (link == null) return null;
        Matcher m = Pattern.compile("/d/([A-Za-z0-9_-]{10,})").matcher(link);
        if (m.find()) return m.group(1);
        m = Pattern.compile("[?&]id=([A-Za-z0-9_-]{10,})").matcher(link);
        if (m.find()) return m.group(1);
        return link.matches("[A-Za-z0-9_-]{20,}") ? link : null;
    }

    private void downloadFromDrive(final String id) {
        final AlertDialog progress = new AlertDialog.Builder(this)
                .setTitle("Downloading from Google Drive")
                .setMessage("Connecting…")
                .setCancelable(false)
                .show();
        new Thread(() -> {
            String error = null;
            File result = null;
            File dir = discDir();
            File part = new File(home, "drive-download.part");
            try {
                HttpURLConnection c = (HttpURLConnection) new URL(
                        "https://drive.usercontent.google.com/download?id=" + id + "&export=download&confirm=t").openConnection();
                c.setInstanceFollowRedirects(true);
                c.setConnectTimeout(20000);
                c.setReadTimeout(60000);
                int code = c.getResponseCode();
                String type = c.getContentType() == null ? "" : c.getContentType();
                if (code != 200 || type.startsWith("text/html")) {
                    throw new IOException("Google Drive did not send the file (HTTP " + code + "). "
                            + "Check that the link is shared as \"Anyone with the link\".");
                }
                String name = fileNameFrom(c.getHeaderField("Content-Disposition"));
                final long total = c.getContentLengthLong();
                final long[] done = {0};
                try (InputStream in = c.getInputStream(); OutputStream os = new FileOutputStream(part)) {
                    copy(in, os, n -> {
                        done[0] += n;
                        final String msg = total > 0
                                ? (done[0] * 100 / total) + "%  (" + (done[0] >> 20) + " / " + (total >> 20) + " MB)"
                                : (done[0] >> 20) + " MB";
                        runOnUiThread(() -> progress.setMessage(msg));
                    });
                }
                File[] old = dir.listFiles();
                if (old != null) for (File f : old) f.delete();
                String t = ext(name);
                if (t.equals("zip")) {
                    runOnUiThread(() -> progress.setMessage("Unpacking…"));
                    result = unzipDisc(part, dir);
                    part.delete();
                } else if (t.equals("chd") || t.equals("bin") || t.equals("img")) {
                    File out = new File(dir, name);
                    if (!part.renameTo(out)) throw new IOException("could not save " + name);
                    result = out;
                } else {
                    part.delete();
                    throw new IOException("\"" + name + "\" is not a .chd, .bin or .zip disc image.");
                }
                if (result == null) throw new IOException("the .zip holds no .cue, .bin or .chd file.");
            } catch (IOException ex) {
                part.delete();
                error = ex.getMessage();
            }
            final String err = error;
            final File disc = result;
            runOnUiThread(() -> {
                progress.dismiss();
                if (err != null) {
                    alert("Download failed: " + err + "\n\nIs the TV online, and is there enough free space?");
                } else {
                    useDisc(disc);
                    refresh();
                    playButton.requestFocus();
                }
            });
        }).start();
    }

    private static String fileNameFrom(String disposition) {
        if (disposition != null) {
            Matcher m = Pattern.compile("filename\\*=UTF-8''([^;]+)").matcher(disposition);
            if (m.find()) {
                try {
                    return new File(java.net.URLDecoder.decode(m.group(1), "UTF-8")).getName();
                } catch (Exception ignored) {
                }
            }
            m = Pattern.compile("filename=\"?([^\";]+)\"?").matcher(disposition);
            if (m.find()) return new File(m.group(1).trim()).getName();
        }
        return "disc.bin";
    }

    /** Unpacks the disc files of a .zip into dir; returns the .cue/.chd/.bin to play. */
    private static File unzipDisc(File zip, File dir) throws IOException {
        File cue = null, chd = null, bin = null;
        try (ZipInputStream in = new ZipInputStream(new FileInputStream(zip))) {
            ZipEntry e;
            while ((e = in.getNextEntry()) != null) {
                if (e.isDirectory()) continue;
                String name = new File(e.getName()).getName();
                String t = ext(name);
                if (!t.equals("cue") && !t.equals("bin") && !t.equals("img") && !t.equals("chd")) continue;
                File out = new File(dir, name);
                try (OutputStream os = new FileOutputStream(out)) {
                    copy(in, os, null);
                }
                if (t.equals("cue")) cue = out;
                else if (t.equals("chd")) chd = out;
                else if (bin == null) bin = out;
            }
        }
        return cue != null ? cue : chd != null ? chd : bin;
    }

    private long size(Uri u) {
        try (Cursor c = getContentResolver().query(u, new String[] {OpenableColumns.SIZE}, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) return c.getLong(0);
        } catch (Exception ignored) {
        }
        return 0;
    }

    private void searchStorage() {
        if (Build.VERSION.SDK_INT >= 23 && Build.VERSION.SDK_INT <= 32
                && checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.READ_EXTERNAL_STORAGE}, REQ_STORAGE);
            return;
        }
        List<File> found = new ArrayList<>();
        File ext = getExternalFilesDir("disc");
        if (ext != null) scan(ext, 1, found);
        File primary = Environment.getExternalStorageDirectory();
        for (String sub : new String[] {"Download", "Downloads", "Games", "PSX", "PS1", "ROMs", "roms", "Roms", ""}) {
            scan(sub.isEmpty() ? primary : new File(primary, sub), sub.isEmpty() ? 1 : 3, found);
        }
        File[] volumes = new File("/storage").listFiles();
        if (volumes != null) {
            for (File v : volumes) {
                if (!v.getName().equals("emulated") && !v.getName().equals("self")) scan(v, 3, found);
            }
        }
        File[] mnt = new File("/mnt/media_rw").listFiles();
        if (mnt != null) for (File v : mnt) scan(v, 3, found);

        if (found.isEmpty()) {
            alert("No .cue, .bin or .chd file was found in Downloads or on USB drives.\n\n"
                    + "Copy your disc image there (or to Android/data/" + getPackageName() + "/files/disc/) and try again.");
            return;
        }
        final String[] names = new String[found.size()];
        for (int k = 0; k < names.length; k++) names[k] = found.get(k).getAbsolutePath();
        final List<File> list = found;
        new AlertDialog.Builder(this)
                .setTitle("Choose the disc image")
                .setItems(names, (d, which) -> {
                    useDisc(list.get(which));
                    refresh();
                    playButton.requestFocus();
                })
                .show();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQ_STORAGE) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) searchStorage();
            else alert("Without storage access the game can only use \"Choose disc image…\" or the app's own disc folder.");
        }
    }

    private void scan(File dir, int depth, List<File> out) {
        if (dir == null || depth < 0 || !dir.isDirectory()) return;
        File[] files = dir.listFiles();
        if (files == null) return;
        List<File> bins = new ArrayList<>();
        boolean hasCue = false;
        for (File f : files) {
            if (f.isDirectory()) {
                if (!f.getName().startsWith(".") && !f.getName().equals("Android")) scan(f, depth - 1, out);
                continue;
            }
            String t = ext(f.getName());
            if ((t.equals("cue") || t.equals("chd")) && f.canRead() && !out.contains(f)) {
                out.add(f);
                if (t.equals("cue")) hasCue = true;
            } else if (t.equals("bin") && f.canRead()) {
                bins.add(f);
            }
        }
        // A .bin without a .cue next to it can still be used (a cue is written for it).
        if (!hasCue) for (File b : bins) if (!out.contains(b)) out.add(b);
    }

    /** Selects a disc image found on storage. A lone .bin gets a .cue in the game folder. */
    private void useDisc(File f) {
        File disc = f;
        if (ext(f.getName()).equals("bin")) {
            String base = f.getName().substring(0, f.getName().lastIndexOf('.'));
            File cue = new File(discDir(), base + ".cue");
            writeText(cue, "FILE \"" + f.getAbsolutePath() + "\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n");
            disc = cue;
        } else if (ext(f.getName()).equals("cue")) {
            for (String name : cueFiles(readText(f))) {
                File bin = new File(name).isAbsolute() ? new File(name) : new File(f.getParentFile(), name);
                if (!bin.canRead()) {
                    alert("The .cue points to \"" + name + "\", which is not next to it (or cannot be read).");
                    return;
                }
            }
        }
        settings.put("disc", disc.getAbsolutePath());
        saveSettings();
    }

    private static List<String> cueFiles(String cue) {
        List<String> files = new ArrayList<>();
        Matcher m = Pattern.compile("(?im)^\\s*FILE\\s+(?:\"([^\"]+)\"|(\\S+))").matcher(cue);
        while (m.find()) files.add(m.group(1) != null ? m.group(1) : m.group(2));
        return files;
    }

    // ------------------------------------------------------------ play

    private void play() {
        String disc = discPath();
        if (disc == null) {
            chooseDisc();
            return;
        }
        if (ext(disc).equals("chd")) {
            convertChd(new File(disc), this::play);
            return;
        }
        launchGame(disc);
    }

    // ------------------------------------------------------------ .chd -> .bin

    // android/native/android_chd.c (in libmain.so, which also holds the game).
    private static native String nativeChdToBin(String chd, String bin, String binName);
    private static native int nativeChdProgress();
    private static boolean nativeLoaded;

    /**
     * Unpacks a .chd once into a plain .bin + .cue next to the game. Playing
     * from a .chd makes the game decompress the disc while it runs, which a
     * TV stick cannot afford. The app's own copy of the .chd is deleted
     * afterwards to free the space; a .chd elsewhere (USB stick) is kept.
     */
    private void convertChd(final File chd, final Runnable then) {
        if (!nativeLoaded) {
            try {
                System.loadLibrary("main");
                nativeLoaded = true;
            } catch (Throwable t) {
                alert("Could not load the game library: " + t.getMessage());
                return;
            }
        }
        final String base = chd.getName().substring(0, chd.getName().length() - 4);
        final boolean ours = chd.getAbsolutePath().startsWith(home.getAbsolutePath());
        final File dir = ours ? chd.getParentFile() : discDir();
        final File bin = new File(dir, base + ".bin");
        final File cue = new File(dir, base + ".cue");
        // A PlayStation disc is at most ~800 MB raw; keep a margin.
        long need = Math.max(chd.length() * 6, 300L << 20) + (50L << 20);
        if (dir.getUsableSpace() < need) {
            alert("Not enough free space on the TV to unpack the disc (about " + (need >> 20) + " MB needed, "
                    + (dir.getUsableSpace() >> 20) + " MB free). The game will play from the .chd instead, "
                    + "which is slower.");
            launchGame(chd.getAbsolutePath());
            return;
        }
        final AlertDialog progress = new AlertDialog.Builder(this)
                .setTitle("Preparing the disc (one time only)")
                .setMessage("Unpacking the .chd so the game loads faster…")
                .setCancelable(false)
                .show();
        final boolean[] running = {true};
        final android.os.Handler ui = new android.os.Handler(getMainLooper());
        ui.post(new Runnable() {
            @Override public void run() {
                if (!running[0]) return;
                progress.setMessage("Unpacking the .chd so the game loads faster… " + nativeChdProgress() / 10 + "%");
                ui.postDelayed(this, 400);
            }
        });
        new Thread(() -> {
            final String result = nativeChdToBin(chd.getAbsolutePath(), bin.getAbsolutePath(), bin.getName());
            runOnUiThread(() -> {
                running[0] = false;
                progress.dismiss();
                if (result == null || result.startsWith("error")) {
                    bin.delete();
                    alert("Could not unpack the .chd (" + result + "). The game will play from the .chd instead.");
                    launchGame(chd.getAbsolutePath());
                    return;
                }
                writeText(cue, result);
                if (ours) chd.delete();
                settings.put("disc", cue.getAbsolutePath());
                saveSettings();
                refresh();
                then.run();
            });
        }).start();
    }

    private void launchGame(String disc) {
        writeRuntimeSettings();
        writeKeybindsOnce();
        Intent i = new Intent(this, GameActivity.class);
        i.putExtra(GameActivity.EXTRA_DISC, disc);
        startActivity(i);
    }

    private static int connectedGamepads() {
        int n = 0;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice d = InputDevice.getDevice(id);
            if (d == null || d.isVirtual()) continue;
            int s = d.getSources();
            if ((s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                    || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) n++;
        }
        return n;
    }

    /** The runtime's settings.toml, as the PC launcher writes it, tuned for TV boxes. */
    private void writeRuntimeSettings() {
        Toml toml = new Toml(new File(home, "settings.toml"));
        toml.set("video", "renderer", "\"software\"");
        toml.set("video", "fullscreen", "1");
        toml.set("video", "vsync", "\"on\"");
        toml.set("video", "internal_resolution", "\"" + get("android_resolution", "native") + "\"");
        toml.set("video", "antialiasing", "false");
        toml.set("video", "fxaa", "false");
        toml.set("video", "texture_filtering", get("android_smooth", "off").equals("on") ? "\"bilinear\"" : "\"nearest\"");
        toml.set("video", "fps_counter", get("android_fps_counter", "off").equals("on") ? "true" : "false");
        toml.set("video", "stretch", "false");
        toml.set("video", "crt_filter", "\"raw\"");
        toml.set("video", "scanlines", "false");
        toml.set("video", "frame_interpolation", "false");
        // TV audio paths are slower than a PC's: a longer buffer avoids crackle.
        toml.set("audio", "latency_ms", "180");
        int pads = connectedGamepads();
        String p1 = pads > 0 ? "gamepad" : "keyboard";
        String p2 = pads > 1 ? "gamepad" : pads == 1 ? "keyboard" : "none";
        toml.set("controller", "p1_device", "\"" + p1 + "\"");
        toml.set("controller", "p2_device", "\"" + p2 + "\"");
        toml.set("launcher", "skip_launcher", "true");
        toml.save();
    }

    /**
     * keybinds.ini with the TV remote on player 1: OK is Cross, Back is
     * Triangle, Menu / Play-Pause is Start. Written once; the runtime keeps
     * the file afterwards.
     */
    private void writeKeybindsOnce() {
        File f = new File(home, "keybinds.ini");
        if (f.exists()) return;
        String common =
                "up        = Up\n" + "down      = Down\n" + "left      = Left\n" + "right     = Right\n"
                + "circle    = S\n" + "square    = Z\n"
                + "l1        = Q\n" + "r1        = W\n" + "l2        = E\n" + "r2        = R\n"
                + "l3        = T\n" + "r3        = Y\n" + "select    = Right Shift\n"
                + "ls_up     = Up\n" + "ls_down   = Down\n" + "ls_left   = Left\n" + "ls_right  = Right\n"
                + "rs_up     = None\n" + "rs_down   = None\n" + "rs_left   = None\n" + "rs_right  = None\n";
        String p1 = "[player1]\n" + common
                + "cross     = Return, X\n"
                + "triangle  = AC Back, A\n"
                + "start     = Menu, MediaPlayPause\n";
        String p2 = "[player2]\n" + common
                + "cross     = X\n" + "triangle  = A\n" + "start     = Return\n";
        writeText(f, "# The Simpsons Wrestling for Android TV: keyboard / TV remote buttons.\n"
                + "# Player 1: remote OK = Cross, Back = Triangle, Menu or Play/Pause = Start.\n\n"
                + p1 + "\n" + p2);
    }

    /** Sets `key = value` in [section] of a TOML file, keeping everything else. */
    private static final class Toml {
        private final File file;
        private final List<String> lines;

        Toml(File file) {
            this.file = file;
            this.lines = readLines(file);
        }

        void set(String section, String key, String value) {
            String header = "[" + section + "]";
            int start = -1;
            for (int i = 0; i < lines.size(); i++) {
                if (lines.get(i).trim().equals(header)) { start = i; break; }
            }
            if (start < 0) {
                if (!lines.isEmpty() && !lines.get(lines.size() - 1).trim().isEmpty()) lines.add("");
                lines.add(header);
                lines.add(key + " = " + value);
                return;
            }
            int end = lines.size();
            for (int i = start + 1; i < lines.size(); i++) {
                if (lines.get(i).trim().startsWith("[")) { end = i; break; }
            }
            Pattern p = Pattern.compile("^\\s*" + Pattern.quote(key) + "\\s*=.*$");
            for (int i = start + 1; i < end; i++) {
                if (p.matcher(lines.get(i)).matches()) {
                    lines.set(i, key + " = " + value);
                    return;
                }
            }
            int at = end;
            while (at > start + 1 && lines.get(at - 1).trim().isEmpty()) at--;
            lines.add(at, key + " = " + value);
        }

        void save() {
            StringBuilder sb = new StringBuilder();
            for (String l : lines) sb.append(l).append('\n');
            writeText(file, sb.toString());
        }
    }

    // ------------------------------------------------------------ helpers

    private interface Progress { void add(long n); }

    private static void copy(InputStream in, OutputStream out, Progress p) throws IOException {
        byte[] buf = new byte[1 << 20];
        int n;
        while ((n = in.read(buf)) > 0) {
            out.write(buf, 0, n);
            if (p != null) p.add(n);
        }
    }

    private static List<String> readLines(InputStream in) throws IOException {
        List<String> out = new ArrayList<>();
        try (BufferedReader r = new BufferedReader(new InputStreamReader(in, StandardCharsets.UTF_8))) {
            String l;
            while ((l = r.readLine()) != null) out.add(l);
        }
        return out;
    }

    private static List<String> readLines(File f) {
        try {
            return readLines(new FileInputStream(f));
        } catch (IOException e) {
            return new ArrayList<>();
        }
    }

    private static String readText(File f) {
        StringBuilder sb = new StringBuilder();
        for (String l : readLines(f)) sb.append(l).append('\n');
        return sb.toString();
    }

    private String readUri(Uri u) {
        try (InputStream in = getContentResolver().openInputStream(u)) {
            if (in == null) return "";
            StringBuilder sb = new StringBuilder();
            for (String l : readLines(in)) sb.append(l).append('\n');
            return sb.toString();
        } catch (IOException e) {
            return "";
        }
    }

    private static void writeText(File f, String text) {
        File parent = f.getParentFile();
        if (parent != null) parent.mkdirs();
        try (OutputStream os = new FileOutputStream(f)) {
            os.write(text.getBytes(StandardCharsets.UTF_8));
        } catch (IOException ignored) {
        }
    }

    private void alert(String msg) {
        new AlertDialog.Builder(this).setMessage(msg).setPositiveButton("OK", null).show();
    }

    private void toast(String msg) {
        android.widget.Toast.makeText(this, msg, android.widget.Toast.LENGTH_LONG).show();
    }
}
