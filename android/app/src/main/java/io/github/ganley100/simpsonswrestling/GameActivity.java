package io.github.ganley100.simpsonswrestling;

import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

/**
 * The recompiled game. SDL loads libmain.so and runs the game's main() on its
 * own thread; this class only tells it where its files are and what to play.
 *
 * Started by {@link LauncherActivity} with the disc image path. Runs in its
 * own process (":game", see AndroidManifest.xml).
 */
public class GameActivity extends SDLActivity {
    static final String EXTRA_DISC = "disc";
    private static final String TAG = "SimpsonsWrestling";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // android_host.c reads this when libmain.so loads (inside
        // super.onCreate) and makes it the working directory, which is where
        // the game keeps game.toml, settings and memory cards.
        try {
            Os.setenv("SWR_HOME", getFilesDir().getAbsolutePath(), true);
        } catch (ErrnoException e) {
            Log.e(TAG, "setenv SWR_HOME", e);
        }
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    protected String[] getLibraries() {
        // SDL is linked into libmain.so (static), so there is nothing else to load.
        return new String[] { "main" };
    }

    @Override
    protected String getMainFunction() {
        // android_host.c: gives the game an argv[0] inside its files folder,
        // then calls the game's main().
        return "SWR_AndroidMain";
    }

    @Override
    protected String[] getArguments() {
        String disc = getIntent() != null ? getIntent().getStringExtra(EXTRA_DISC) : null;
        if (disc == null || disc.isEmpty()) {
            return new String[] { "--no-launcher" };
        }
        return new String[] { "--no-launcher", "--disc", disc };
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // The game cannot be restarted inside the same process; this process
        // only ever hosts the game, so end it with the activity.
        if (isFinishing()) {
            android.os.Process.killProcess(android.os.Process.myPid());
        }
    }
}
