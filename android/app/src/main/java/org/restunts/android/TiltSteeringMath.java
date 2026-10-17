package org.restunts.android;

/** Sensor math and lifecycle policy without Android framework dependencies. */
public final class TiltSteeringMath {
    public static final int MAX_AXIS = 32;
    public static final double DEAD_ZONE_DEGREES = 4.0;
    public static final double FULL_STEERING_DEGREES = 30.0;
    private static final double MIN_GRAVITY = 1.0;
    private static final double MAX_GRAVITY = 30.0;

    private TiltSteeringMath() {}

    private static boolean finite(double value) {
        return !Double.isNaN(value) && !Double.isInfinite(value);
    }

    public static boolean validGravity(double x, double y, double z) {
        if (!finite(x) || !finite(y) || !finite(z)) {
            return false;
        }
        double magnitude = Math.hypot(Math.hypot(x, y), z);
        return magnitude >= MIN_GRAVITY && magnitude <= MAX_GRAVITY;
    }

    /** Rotation values match Surface.ROTATION_0/90/180/270 (0 through 3). */
    public static int axisForGravity(double x, double y, double z, int rotation) {
        if (!validGravity(x, y, z)) {
            return 0;
        }
        double horizontal;
        switch (rotation) {
            case 0: horizontal = x; break;
            case 1: horizontal = -y; break;
            case 2: horizontal = -x; break;
            case 3: horizontal = y; break;
            default: return 0;
        }
        double magnitude = Math.hypot(Math.hypot(x, y), z);
        double angle = Math.toDegrees(Math.asin(Math.max(-1.0, Math.min(1.0, horizontal / magnitude))));
        double amount = (Math.abs(angle) - DEAD_ZONE_DEGREES)
            / (FULL_STEERING_DEGREES - DEAD_ZONE_DEGREES);
        if (amount <= 0.0) {
            return 0;
        }
        int axis = (int)Math.round(Math.min(1.0, amount) * MAX_AXIS);
        // Positive displayed acceleration means the left edge is down: steer left.
        return angle > 0.0 ? -axis : axis;
    }

    public static boolean freshSample(long timestamp, long now) {
        return timestamp > 0 && timestamp <= now && now - timestamp <= 500_000_000L;
    }

    public static boolean shouldListen(boolean requested, boolean resumed, boolean focused,
            boolean destroyed, boolean available) {
        return requested && resumed && focused && !destroyed && available;
    }

    /** Time-based low-pass filtering of the raw device vector, with no pose calibration. */
    public static final class GravityFilter {
        private final double timeConstantSeconds;
        private long timestamp;
        private double x;
        private double y;
        private double z;
        private boolean initialized;

        public GravityFilter(double timeConstantSeconds) {
            if (!finite(timeConstantSeconds) || timeConstantSeconds <= 0.0) {
                throw new IllegalArgumentException("A positive filter time constant is required.");
            }
            this.timeConstantSeconds = timeConstantSeconds;
        }

        public void reset() {
            initialized = false;
            timestamp = 0;
            x = y = z = 0.0;
        }

        public boolean update(double sampleX, double sampleY, double sampleZ, long sampleTimestamp) {
            if (!validGravity(sampleX, sampleY, sampleZ) || sampleTimestamp <= 0
                    || (initialized && sampleTimestamp <= timestamp)) {
                return false;
            }
            double elapsed = initialized ? (sampleTimestamp - timestamp) / 1_000_000_000.0 : 0.0;
            if (!initialized || elapsed > 0.5) {
                // The first real pose is steering input, not a new neutral calibration.
                x = sampleX;
                y = sampleY;
                z = sampleZ;
            } else {
                double alpha = 1.0 - Math.exp(-elapsed / timeConstantSeconds);
                x += alpha * (sampleX - x);
                y += alpha * (sampleY - y);
                z += alpha * (sampleZ - z);
            }
            timestamp = sampleTimestamp;
            initialized = true;
            return validGravity(x, y, z);
        }

        public int axis(int rotation) {
            return initialized ? axisForGravity(x, y, z, rotation) : 0;
        }
    }
}
