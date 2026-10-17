package org.restunts.android;

import android.app.Activity;
import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;

/** Owns tilt sensor registration for the visible, resumed game Activity only. */
public final class TiltSteering implements SensorEventListener {
    interface Output {
        void setTilt(int axis, boolean enabled);
    }

    private final Activity activity;
    private final SensorManager manager;
    private final Sensor gravity;
    private final Sensor accelerometer;
    private final Output output;
    private final boolean requested;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean resumed;
    private boolean focused;
    private boolean destroyed;
    private boolean listening;
    private Sensor activeSensor;
    private int rotation = -1;
    private long startedAt;
    private TiltSteeringMath.GravityFilter filter;

    TiltSteering(Activity activity, boolean requested, Output output) {
        this.activity = activity;
        this.requested = requested;
        this.output = output;
        manager = sensorManager(activity);
        gravity = sensor(manager, Sensor.TYPE_GRAVITY);
        accelerometer = sensor(manager, Sensor.TYPE_ACCELEROMETER);
    }

    private static SensorManager sensorManager(Context context) {
        Object service = context == null ? null : context.getSystemService(Context.SENSOR_SERVICE);
        return service instanceof SensorManager ? (SensorManager)service : null;
    }

    private static Sensor sensor(SensorManager manager, int type) {
        if (manager == null) {
            return null;
        }
        try {
            return manager.getDefaultSensor(type);
        } catch (SecurityException unavailable) {
            return null;
        }
    }

    public static boolean isAvailable(Context context) {
        SensorManager manager = sensorManager(context);
        return sensor(manager, Sensor.TYPE_GRAVITY) != null
            || sensor(manager, Sensor.TYPE_ACCELEROMETER) != null;
    }

    void setResumed(boolean resumed) {
        this.resumed = resumed;
        updateRegistration();
    }

    void setFocused(boolean focused) {
        this.focused = focused;
        updateRegistration();
    }

    void close() {
        destroyed = true;
        updateRegistration();
    }

    private void updateRegistration() {
        boolean available = gravity != null || accelerometer != null;
        if (!TiltSteeringMath.shouldListen(requested, resumed, focused, destroyed, available)) {
            stop();
        } else if (!listening) {
            output.setTilt(0, false);
            if (!start(gravity)) {
                start(accelerometer);
            }
        }
    }

    private boolean start(Sensor sensor) {
        if (sensor == null || manager == null) {
            return false;
        }
        filter = new TiltSteeringMath.GravityFilter(sensor.getType() == Sensor.TYPE_GRAVITY ? 0.04 : 0.12);
        rotation = -1;
        activeSensor = sensor;
        startedAt = SystemClock.elapsedRealtimeNanos();
        // Explicit main-thread delivery serializes callbacks with lifecycle teardown.
        listening = true;
        try {
            if (manager.registerListener(this, sensor, SensorManager.SENSOR_DELAY_GAME, handler)) {
                return true;
            }
        } catch (SecurityException unavailable) {
            // Sensor privacy/device policy can make a reported sensor unavailable.
        }
        listening = false;
        activeSensor = null;
        manager.unregisterListener(this);
        return false;
    }

    private void stop() {
        // Queued callbacks must see this before unregistering or publishing neutral.
        listening = false;
        activeSensor = null;
        if (manager != null) {
            manager.unregisterListener(this);
        }
        if (filter != null) {
            filter.reset();
        }
        rotation = -1;
        output.setTilt(0, false);
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (!listening || !TiltSteeringMath.shouldListen(requested, resumed, focused, destroyed, true)
                || event.sensor != activeSensor || event.values.length < 3
                || event.timestamp < startedAt
                || !TiltSteeringMath.freshSample(event.timestamp, SystemClock.elapsedRealtimeNanos())) {
            return;
        }
        int displayRotation = activity.getWindowManager().getDefaultDisplay().getRotation();
        if (displayRotation != rotation) {
            filter.reset();
            rotation = displayRotation;
        }
        if (filter.update(event.values[0], event.values[1], event.values[2], event.timestamp)) {
            // Send unchanged samples too, so a steady tilt remains fresh to native input.
            output.setTilt(filter.axis(rotation), true);
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
        // Vector validity and filtering determine whether each sample is usable.
    }
}
