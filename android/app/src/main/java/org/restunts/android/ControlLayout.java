package org.restunts.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import java.util.Arrays;

/** Stores edited centers as fractions of the usable screen area. */
final class ControlLayout {
    static final float MIN_SCALE = 1f;
    static final float MAX_SCALE = 3f;
    // The persisted order matches enum TOUCH_CONTROL in touch.h.
    static final int CONTROL_COUNT = 10;
    private static final int CENTER_COMPONENTS = 2;
    private static final float DEFAULT_CENTER = -1f;
    private static final String PREFERENCES = "control_layout";
    private static final String SCALE_KEY = "scale";
    private static final String CENTERS_KEY = "centers";
    private final float[] centers = new float[CONTROL_COUNT * CENTER_COMPONENTS];
    float scale = MIN_SCALE;

    private ControlLayout() {
        Arrays.fill(centers, DEFAULT_CENTER);
    }

    static void loadLibraries() {
        for (String library : Mt32RomLibrary.libraries()) {
            System.loadLibrary(library);
        }
    }

    static ControlLayout restore(Context context, Bundle state) {
        ControlLayout layout = new ControlLayout();
        float[] draft = state == null ? null : state.getFloatArray(CENTERS_KEY);
        if (draft != null && draft.length == layout.centers.length) {
            System.arraycopy(draft, 0, layout.centers, 0, draft.length);
            layout.scale = state.getFloat(SCALE_KEY, MIN_SCALE);
            return layout;
        }
        SharedPreferences preferences = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
        for (int index = 0; index < layout.centers.length; index++) {
            layout.centers[index] = preferences.getFloat(CENTERS_KEY + index, DEFAULT_CENTER);
        }
        layout.scale = preferences.getFloat(SCALE_KEY, MIN_SCALE);
        return layout;
    }

    void move(int control, float x, float y) {
        int index = control * CENTER_COMPONENTS;
        centers[index] = Math.max(0f, Math.min(1f, x));
        centers[index + 1] = Math.max(0f, Math.min(1f, y));
    }

    float[] circles(int width, int height) {
        return previewNative(centers, scale, width, height);
    }

    void writeTo(Bundle state) {
        state.putFloatArray(CENTERS_KEY, centers.clone());
        state.putFloat(SCALE_KEY, scale);
    }

    void save(Context context) {
        SharedPreferences.Editor editor = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
            .edit();
        for (int index = 0; index < centers.length; index++) {
            editor.putFloat(CENTERS_KEY + index, centers[index]);
        }
        editor.putFloat(SCALE_KEY, scale).apply();
    }

    void applyToGame() {
        applyNative(centers, scale);
    }

    private static native float[] previewNative(float[] centers, float scale, int width, int height);

    private static native void applyNative(float[] centers, float scale);
}
