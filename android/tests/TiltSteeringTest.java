package org.restunts.android;

/** Host coverage for direction, display rotations, filtering and settings/lifecycle policy. */
public final class TiltSteeringTest {
    private static final double GRAVITY = 9.80665;

    private static void require(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static double[] pose(double rollDegrees, int rotation) {
        double horizontal = GRAVITY * Math.sin(Math.toRadians(rollDegrees));
        double vertical = GRAVITY * Math.cos(Math.toRadians(rollDegrees));
        switch (rotation) {
            case 0: return new double[] {horizontal, vertical, 0.0};
            case 1: return new double[] {vertical, -horizontal, 0.0};
            case 2: return new double[] {-horizontal, -vertical, 0.0};
            case 3: return new double[] {-vertical, horizontal, 0.0};
            default: throw new AssertionError("Invalid test rotation.");
        }
    }

    private static int axis(double degrees, int rotation) {
        double[] sample = pose(degrees, rotation);
        return TiltSteeringMath.axisForGravity(sample[0], sample[1], sample[2], rotation);
    }

    private static void directions() {
        for (int rotation = 0; rotation < 4; rotation++) {
            require(axis(0, rotation) == 0, "Upright must be neutral in every display rotation.");
            require(axis(3.9, rotation) == 0 && axis(-3.9, rotation) == 0,
                "Small movements must remain within the dead zone.");
            require(axis(17, rotation) == -16 && axis(-17, rotation) == 16,
                "Half-scale tilt must steer proportionally with left edge down steering left.");
            require(axis(30, rotation) == -32 && axis(-30, rotation) == 32,
                "Thirty degrees must reach full steering in both directions.");
            require(axis(70, rotation) == -32 && axis(-70, rotation) == 32,
                "Larger tilts must clamp to the supported native axis range.");
            int previous = 0;
            for (int angle = 0; angle <= 90; angle++) {
                int left = axis(angle, rotation);
                require(left <= previous && left >= -32, "Steering must increase smoothly with tilt.");
                require(axis(-angle, rotation) == -left, "Opposite tilt must give symmetric steering.");
                previous = left;
            }
        }
        require(TiltSteeringMath.axisForGravity(0, 0, GRAVITY, 0) == 0,
            "Laying the screen flat must not create horizontal steering.");
        double[] tilted = pose(17, 1);
        require(TiltSteeringMath.axisForGravity(tilted[0] / 2, tilted[1] / 2, 0, 1) == -16,
            "Direction must use vector angle rather than a fixed accelerometer magnitude.");
        require(TiltSteeringMath.axisForGravity(0, GRAVITY, 0, 99) == 0,
            "Unknown display rotation must not steer.");
    }

    private static void invalidSamples() {
        for (double[] invalid : new double[][] {
                {0, 0, 0}, {0.01, -0.02, 0.01}, {Double.NaN, 0, GRAVITY},
                {0, Double.POSITIVE_INFINITY, GRAVITY}, {0, GRAVITY, Double.NEGATIVE_INFINITY},
                {1000, 0, 0}, {Double.MAX_VALUE, Double.MAX_VALUE, 0}}) {
            require(!TiltSteeringMath.validGravity(invalid[0], invalid[1], invalid[2]),
                "Non-gravity samples must be rejected.");
            require(TiltSteeringMath.axisForGravity(invalid[0], invalid[1], invalid[2], 0) == 0,
                "Invalid vectors must never produce steering.");
        }
    }

    private static void filtering() {
        TiltSteeringMath.GravityFilter filter = new TiltSteeringMath.GravityFilter(0.12);
        double[] left = pose(17, 1);
        require(filter.axis(1) == 0, "A filter without a sample must be neutral.");
        require(filter.update(left[0], left[1], left[2], 1_000_000_000L), "First sample must be usable.");
        require(filter.axis(1) == -16, "Starting in a tilted pose must not recalibrate that pose to neutral.");
        require(!filter.update(0, 0, 0, 1_010_000_000L), "Free fall must not replace valid gravity.");
        require(!filter.update(Double.NaN, 0, GRAVITY, 1_020_000_000L), "NaN must be ignored by the filter.");
        require(!filter.update(left[0], left[1], left[2], 1_000_000_000L), "Duplicate timestamps are stale.");
        require(!filter.update(left[0], left[1], left[2], 900_000_000L), "Backdated timestamps are stale.");
        require(filter.axis(1) == -16, "Rejected input must leave the previous vector intact.");
        double[] right = pose(-17, 1);
        require(filter.update(right[0], right[1], right[2], 1_020_000_000L), "A fresh change must be accepted.");
        require(filter.axis(1) < 0, "One accelerometer spike must not instantly reverse steering.");
        for (long timestamp = 1_040_000_000L; timestamp <= 2_000_000_000L; timestamp += 20_000_000L) {
            require(filter.update(right[0], right[1], right[2], timestamp), "Steady samples must stay fresh.");
        }
        require(filter.axis(1) == 16, "Sustained opposite tilt must converge to the opposite steering value.");
        require(filter.update(left[0], left[1], left[2], 3_000_000_000L), "A sample after a pause must be fresh.");
        require(filter.axis(1) == -16, "A long gap must discard the old vector.");
        filter.reset();
        require(filter.axis(1) == 0, "Lifecycle reset must remove the old pose.");
        require(!filter.update(left[0], left[1], left[2], 0), "Missing sensor timestamps are invalid.");
        require(filter.update(right[0], right[1], right[2], 4_000_000_000L), "Restart must accept a fresh sample.");
        require(filter.axis(1) == 16, "Restart must keep the actual pose rather than auto-centering it.");
    }

    private static void optInPolicy() {
        require(!LaunchOptions.DEFAULT_TILT_STEERING, "Tilt steering must default to off on every device.");
        require(!TiltSteeringMath.shouldListen(LaunchOptions.DEFAULT_TILT_STEERING, true, true, false, true),
            "Available sensors must remain inactive until tilt steering is explicitly enabled.");
        require(TiltSteeringMath.shouldListen(true, true, true, false, true),
            "A saved or selected manual opt-in must enable available sensors.");
        require(!TiltSteeringMath.shouldListen(true, true, true, false, false),
            "Manual opt-in cannot enable unavailable sensors.");
    }

    private static void sampleFreshness() {
        require(TiltSteeringMath.freshSample(1_000_000_000L, 1_000_000_000L), "A current sample is fresh.");
        require(TiltSteeringMath.freshSample(1_000_000_000L, 1_500_000_000L), "A sample at the age limit is fresh.");
        require(!TiltSteeringMath.freshSample(1_000_000_000L, 1_500_000_001L), "An old queued sample must not renew native input.");
        require(!TiltSteeringMath.freshSample(0, 1_000_000_000L), "A missing timestamp cannot be fresh.");
        require(!TiltSteeringMath.freshSample(2_000_000_000L, 1_000_000_000L), "Future timestamps must not be accepted.");
    }

    private static void lifecyclePolicy() {
        for (int bits = 0; bits < 32; bits++) {
            boolean requested = (bits & 1) != 0;
            boolean resumed = (bits & 2) != 0;
            boolean focused = (bits & 4) != 0;
            boolean destroyed = (bits & 8) != 0;
            boolean available = (bits & 16) != 0;
            boolean expected = bits == (1 | 2 | 4 | 16);
            require(TiltSteeringMath.shouldListen(requested, resumed, focused, destroyed, available) == expected,
                "Sensors may run only while requested, resumed, focused, alive and available.");
        }
    }

    public static void main(String[] args) {
        directions();
        invalidSamples();
        filtering();
        optInPolicy();
        lifecyclePolicy();
        sampleFreshness();
        System.out.println("Tilt steering rotations, direction, dead zone, range, filtering, invalid samples, default-off preference and lifecycle policy passed.");
    }
}
