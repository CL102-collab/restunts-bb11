package org.restunts.android;

import java.util.ArrayList;
import java.util.List;

/** Launcher choices shared by preference storage, Intent extras and native arguments. */
public final class LaunchOptions {
    public static final String NEW_MIDI_KEY = "new_midi";
    public static final String TILT_STEERING_KEY = "tilt_steering";
    public static final String SHOW_FPS_KEY = "show_fps";
    public static final String SHOW_CONTROL_LAYOUT_KEY = "show_control_layout";
    public static final String HYPERVISION_KEY = "hypervision";
    public static final String POWER_GEAR_KEY = "power_gear";
    public static final String LEGACY_COLLISION_KEY = "legacy_collision";
    public static final String LEFT_CORNER_BIAS_KEY = "left_corner_bias";
    public static final String OWOOT_KEY = "owoot";
    public static final String AMIGA_GRASS_KEY = "amiga_grass";
    public static final boolean DEFAULT_NEW_MIDI = false;
    public static final boolean DEFAULT_TILT_STEERING = false;
    public static final boolean DEFAULT_SHOW_FPS = false;
    public static final boolean DEFAULT_SHOW_CONTROL_LAYOUT = true;
    public static final boolean DEFAULT_POWER_GEAR = true;
    public static final boolean DEFAULT_LEGACY_COLLISION = true;
    public static final boolean DEFAULT_LEFT_CORNER_BIAS = true;
    public static final boolean DEFAULT_OWOOT = false;
    public static final boolean DEFAULT_AMIGA_GRASS = false;

    public enum HyperVision {
        OFF("Off", null),
        AUTO("Auto", "auto"),
        FULL("Full", "full"),
        HIGH("High", "high"),
        MEDIUM("Medium", "medium"),
        LOW("Low", "low");

        private final String label;
        private final String argument;

        HyperVision(String label, String argument) {
            this.label = label;
            this.argument = argument;
        }

        public static HyperVision fromStored(String value) {
            if (value != null) {
                for (HyperVision option : values()) {
                    if (option.name().equals(value)) {
                        return option;
                    }
                }
            }
            return OFF;
        }

        @Override
        public String toString() {
            return label;
        }
    }

    public final boolean newMidi;
    public final boolean showFps;
    public final boolean showControlLayout;
    public final HyperVision hyperVision;
    public final boolean powerGear;
    public final boolean legacyCollision;
    public final boolean leftCornerBias;
    public final boolean owoot;
    public final boolean amigaGrass;

    public LaunchOptions(boolean newMidi, boolean showFps, boolean showControlLayout,
            HyperVision hyperVision) {
        this(newMidi, showFps, showControlLayout, hyperVision, DEFAULT_POWER_GEAR,
            DEFAULT_LEGACY_COLLISION, DEFAULT_LEFT_CORNER_BIAS, DEFAULT_OWOOT, DEFAULT_AMIGA_GRASS);
    }

    public LaunchOptions(boolean newMidi, boolean showFps, boolean showControlLayout,
            HyperVision hyperVision, boolean powerGear, boolean legacyCollision,
            boolean leftCornerBias, boolean owoot, boolean amigaGrass) {
        this.newMidi = newMidi;
        this.showFps = showFps;
        this.showControlLayout = showControlLayout;
        this.hyperVision = hyperVision == null ? HyperVision.OFF : hyperVision;
        this.powerGear = powerGear;
        this.legacyCollision = legacyCollision;
        this.leftCornerBias = leftCornerBias;
        this.owoot = owoot;
        this.amigaGrass = amigaGrass;
    }

    public String[] arguments(String dataDirectory) {
        List<String> arguments = new ArrayList<>();
        arguments.add("--data-dir");
        arguments.add(dataDirectory);
        arguments.add(newMidi ? "--ogg:on" : "--ogg:off");
        arguments.add(showFps ? "--fps:on" : "--fps:off");
        if (hyperVision != HyperVision.OFF) {
            arguments.add("--hv:" + hyperVision.argument);
        }
        arguments.add(showControlLayout ? "--control-layout:on" : "--control-layout:off");
        arguments.add(powerGear ? "--pg:on" : "--pg:off");
        arguments.add(legacyCollision ? "--lc:on" : "--lc:off");
        arguments.add(leftCornerBias ? "--lcb:on" : "--lcb:off");
        if (owoot) {
            arguments.add("--owoot");
        }
        if (amigaGrass) {
            arguments.add("/amigagrass");
        }
        return arguments.toArray(new String[0]);
    }
}
