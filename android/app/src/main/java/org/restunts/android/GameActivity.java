package org.restunts.android;

import android.content.Intent;
import android.content.Context;
import android.content.res.Configuration;
import android.os.Bundle;
import android.util.SparseArray;
import android.util.SparseIntArray;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.View;
import java.io.File;
import org.libsdl.app.SDLActivity;

/** SDL owns lifecycle, graphics, audio, multitouch and the software keyboard. */
public final class GameActivity extends SDLActivity {
    private static Context applicationContext;
    private final SparseArray<SparseIntArray> remotePressed = new SparseArray<>();
    private final SparseIntArray remoteHeld = new SparseIntArray();
    private boolean television;

    @Override
    public void onCreate(Bundle state) {
        applicationContext = getApplicationContext();
        television = (getResources().getConfiguration().uiMode & Configuration.UI_MODE_TYPE_MASK)
            == Configuration.UI_MODE_TYPE_TELEVISION;
        super.onCreate(state);
    }

    private boolean remoteNumberInput(KeyEvent event) {
        View focused = getCurrentFocus();
        InputDevice device = event.getDevice();
        return television && (event.getFlags() & KeyEvent.FLAG_SOFT_KEYBOARD) == 0
            && device != null && !device.isVirtual()
            && device.getKeyboardType() != InputDevice.KEYBOARD_TYPE_ALPHABETIC
            && (focused == null || !focused.onCheckIsTextEditor());
    }

    private int remoteKeyCode(KeyEvent event) {
        if (event.getKeyCode() >= KeyEvent.KEYCODE_1 && event.getKeyCode() <= KeyEvent.KEYCODE_5
                && !remoteNumberInput(event)) {
            return KeyEvent.KEYCODE_UNKNOWN;
        }
        switch (event.getKeyCode()) {
            case KeyEvent.KEYCODE_PROG_GREEN:
                return KeyEvent.KEYCODE_ENTER;
            case KeyEvent.KEYCODE_PROG_RED:
                return KeyEvent.KEYCODE_ESCAPE;
            case KeyEvent.KEYCODE_PROG_BLUE:
            case KeyEvent.KEYCODE_3:
                return KeyEvent.KEYCODE_D;
            case KeyEvent.KEYCODE_PROG_YELLOW:
            case KeyEvent.KEYCODE_1:
                return KeyEvent.KEYCODE_T;
            case KeyEvent.KEYCODE_2:
                return KeyEvent.KEYCODE_C;
            case KeyEvent.KEYCODE_4:
                return KeyEvent.KEYCODE_R;
            case KeyEvent.KEYCODE_5:
                return KeyEvent.KEYCODE_Q;
            case KeyEvent.KEYCODE_CHANNEL_UP:
            case KeyEvent.KEYCODE_CHANNEL_DOWN:
                return event.getKeyCode();
            default:
                return KeyEvent.KEYCODE_UNKNOWN;
        }
    }

    private static boolean remoteShift(int keyCode) {
        return keyCode == KeyEvent.KEYCODE_CHANNEL_UP || keyCode == KeyEvent.KEYCODE_CHANNEL_DOWN;
    }

    private static native boolean requestRemoteShift(boolean up);

    private void releaseRemoteKey(int keyCode) {
        if (remoteShift(keyCode)) {
            return;
        }
        int held = remoteHeld.get(keyCode);
        if (held > 1) {
            remoteHeld.put(keyCode, held - 1);
        } else {
            remoteHeld.delete(keyCode);
            if (!mBrokenLibraries) {
                onNativeKeyUp(keyCode);
            }
        }
    }

    private void releaseRemoteKeys() {
        if (!mBrokenLibraries) {
            for (int index = 0; index < remoteHeld.size(); index++) {
                onNativeKeyUp(remoteHeld.keyAt(index));
            }
        }
        remoteHeld.clear();
        remotePressed.clear();
    }

    @Override
    protected void onPause() {
        releaseRemoteKeys();
        super.onPause();
    }

    @Override
    public void onWindowFocusChanged(boolean focused) {
        if (!focused) {
            releaseRemoteKeys();
        }
        super.onWindowFocusChanged(focused);
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        SparseIntArray pressed = remotePressed.get(event.getDeviceId());
        int keyCode = pressed == null ? KeyEvent.KEYCODE_UNKNOWN
            : pressed.get(event.getKeyCode(), KeyEvent.KEYCODE_UNKNOWN);
        if (keyCode != KeyEvent.KEYCODE_UNKNOWN) {
            // Release the original mapping even if a text editor opens or the device disappears.
            if (event.getAction() == KeyEvent.ACTION_UP) {
                pressed.delete(event.getKeyCode());
                if (pressed.size() == 0) {
                    remotePressed.remove(event.getDeviceId());
                }
                releaseRemoteKey(keyCode);
            }
            return true;
        }
        keyCode = remoteKeyCode(event);
        if (keyCode == KeyEvent.KEYCODE_UNKNOWN) {
            return super.dispatchKeyEvent(event);
        }
        if (mBrokenLibraries) {
            return false;
        }
        // Keep each press latched: remotes can repeat DOWN with an unchanged repeat count.
        if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
            if (pressed == null) {
                pressed = new SparseIntArray();
                remotePressed.put(event.getDeviceId(), pressed);
            }
            pressed.put(event.getKeyCode(), keyCode);
            if (remoteShift(keyCode)) {
                requestRemoteShift(keyCode == KeyEvent.KEYCODE_CHANNEL_UP);
            } else {
                remoteHeld.put(keyCode, remoteHeld.get(keyCode) + 1);
                // Bypass Android text commits and unsupported remote-to-gamepad translation.
                onNativeKeyDown(keyCode);
            }
        }
        return true;
    }

    /** Called on the native game thread after a complete game-file write. */
    public static void persistSavedFile(String path) {
        if (applicationContext != null) {
            GameDataStorage.publish(applicationContext, path);
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "nuked-opl2", "main"};
    }

    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        LaunchOptions options = new LaunchOptions(
            intent.getBooleanExtra(LaunchOptions.NEW_MIDI_KEY, LaunchOptions.DEFAULT_NEW_MIDI),
            intent.getBooleanExtra(LaunchOptions.SHOW_FPS_KEY, LaunchOptions.DEFAULT_SHOW_FPS),
            LaunchOptions.HyperVision.fromStored(intent.getStringExtra(LaunchOptions.HYPERVISION_KEY)));
        return options.arguments(new File(getFilesDir(), "game").getAbsolutePath());
    }
}
