package com.rp5np.systemshock;

import android.app.KeyguardManager;
import android.content.Context;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.os.Bundle;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.util.Log;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

public class ShockolateActivity extends SDLActivity {
    private static final String TAG = "RP5NP";

    // TEMPORARY AUDLIFE: System.nanoTime and native CLOCK_MONOTONIC share a clock.
    private void traceAudioLifecycle(String event) {
        if (!BuildConfig.AUDIO_DIAGNOSTICS) return;
        KeyguardManager km = (KeyguardManager)getSystemService(Context.KEYGUARD_SERVICE);
        android.os.PowerManager pm = (android.os.PowerManager)getSystemService(Context.POWER_SERVICE);
        Log.i("AUDLIFE", "t_us=" + System.nanoTime() / 1000 + " java=" + event
                + " focus=" + hasWindowFocus() + " locked=" + (km != null && km.isKeyguardLocked())
                + " interactive=" + (pm != null && pm.isInteractive())
                + " native=" + mCurrentNativeState + " next=" + mNextNativeState);
    }

    @Override protected void onPause() {
        traceAudioLifecycle("onPause.before");
        super.onPause();
        traceAudioLifecycle("onPause.after");
    }

    @Override protected void onStop() {
        traceAudioLifecycle("onStop.before");
        super.onStop();
        traceAudioLifecycle("onStop.after");
    }

    @Override protected void onStart() {
        traceAudioLifecycle("onStart.before");
        super.onStart();
        traceAudioLifecycle("onStart.after");
    }

    /** Called only for accepted touch-control edges from the native bridge. */
    public void touchHaptic(int strength, int level) {
        if (!hasWindowFocus() || isFinishing()) return;
        Vibrator vibrator = (Vibrator)getSystemService(Context.VIBRATOR_SERVICE);
        if (vibrator == null || !vibrator.hasVibrator()) return;
        int safeStrength = Math.max(1, Math.min(3, strength));
        boolean confirmation = level >= 2;
        int duration = confirmation ? 20 : 10;
        int amplitude = Math.min(180, (confirmation ? 52 : 28) * safeStrength);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            vibrator.vibrate(VibrationEffect.createOneShot(duration, amplitude));
        else vibrator.vibrate(duration);
    }

    private void lockOrientation(String phase) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        Log.i(TAG, "JAVA_ORIENTATION_LOCK=LANDSCAPE phase=" + phase);
    }

    @Override public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        lockOrientation("sdl");
        Log.i(TAG, "JAVA_SDL_ORIENTATION_OVERRIDE width=" + w + " height=" + h +
                " resizable=" + resizable + " hint=" + hint);
    }

    private void applyWindowPolicy() {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON | WindowManager.LayoutParams.FLAG_FULLSCREEN);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                c.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            );
        }
    }

    private void applyKeyguardPolicy() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
            setShowWhenLocked(true);
            setTurnScreenOn(true);
        }
        KeyguardManager km = (KeyguardManager)getSystemService(Context.KEYGUARD_SERVICE);
        if (km == null) return;
        Log.i(TAG, "JAVA_KEYGUARD_LOCKED=" + km.isKeyguardLocked());
        if (km.isKeyguardLocked() && Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            km.requestDismissKeyguard(this, null);
        }
    }

    @Override protected void onCreate(Bundle state) {
        Log.i(TAG, "JAVA_ONCREATE_BEGIN");
        lockOrientation("onCreate-pre-super");
        super.onCreate(state);
        lockOrientation("onCreate-post-super");
        applyWindowPolicy();
        applyKeyguardPolicy();
        Log.i(TAG, "JAVA_ONCREATE_END");
    }

    @Override protected void onResume() {
        traceAudioLifecycle("onResume.before");
        super.onResume();
        traceAudioLifecycle("onResume.after");
        lockOrientation("onResume");
        applyWindowPolicy();
        applyKeyguardPolicy();
        Log.i(TAG, "JAVA_ONRESUME");
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        traceAudioLifecycle("focus.before=" + hasFocus);
        super.onWindowFocusChanged(hasFocus);
        traceAudioLifecycle("focus.after=" + hasFocus);
        Log.i(TAG, "JAVA_WINDOW_FOCUS=" + hasFocus);
        if (hasFocus) applyWindowPolicy();
    }

    @Override protected String[] getArguments() {
        return new String[] { "-nosplash" };
    }
}
