package org.restunts.android;

import java.util.ArrayList;
import java.util.List;

/** Launcher choices shared by preference storage, Intent extras and native arguments. */
public final class LaunchOptions {
    public static final String NEW_MIDI_KEY = "new_midi";
    public static final String SOUND_KEY = "sound";
    public static final String SOUND_MODE_KEY = "sound_mode";
    public static final String MT32_ROM_DIRECTORY_KEY = "mt32_rom_directory";
    public static final String MT32_MODEL_KEY = "mt32_model";
    public static final String VIBRATION_KEY = "vibration";
    public static final String SKIP_INTRO_KEY = "skip_intro";
    public static final String TILT_STEERING_KEY = "tilt_steering";
    public static final String SHOW_FPS_KEY = "show_fps";
    public static final String SHOW_MPH_KEY = "show_mph";
    public static final String MULTI_CORE_KEY = "multi_core";
    public static final String SHOW_CONTROL_LAYOUT_KEY = "show_control_layout";
    public static final String HYPERVISION_KEY = "hypervision";
    public static final String POWER_GEAR_KEY = "power_gear";
    public static final String LEGACY_COLLISION_KEY = "legacy_collision";
    public static final String LEFT_CORNER_BIAS_KEY = "left_corner_bias";
    public static final String DUAL_WAY_SWITCHING_KEY = "dual_way_switching";
    public static final String CHICANE_FAST_GRASS_KEY = "chicane_fast_grass";
    public static final String OWOOT_KEY = "owoot";
    public static final String AMIGA_GRASS_KEY = "amiga_grass";
    public static final boolean DEFAULT_NEW_MIDI = false;
    public static final boolean DEFAULT_SOUND = true;
    public static final boolean DEFAULT_VIBRATION = true;
    public static final boolean DEFAULT_SKIP_INTRO = false;
    public static final boolean DEFAULT_TILT_STEERING = false;
    public static final boolean DEFAULT_SHOW_FPS = false;
    public static final boolean DEFAULT_SHOW_MPH = false;
    public static final boolean DEFAULT_MULTI_CORE = true;
    public static final boolean DEFAULT_SHOW_CONTROL_LAYOUT = true;
    public static final boolean DEFAULT_POWER_GEAR = true;
    public static final boolean DEFAULT_LEGACY_COLLISION = true;
    public static final boolean DEFAULT_LEFT_CORNER_BIAS = true;
    public static final boolean DEFAULT_DUAL_WAY_SWITCHING = true;
    public static final boolean DEFAULT_CHICANE_FAST_GRASS = true;
    public static final boolean DEFAULT_OWOOT = false;
    public static final boolean DEFAULT_AMIGA_GRASS = false;

    public enum SoundMode {
        NONE("No sound", "--nosound"),
        ADLIB("Ad Lib", "--soundmode:adlib"),
        MT32("MT-32", "--soundmode:mt-32");

        private final String label;
        private final String argument;

        SoundMode(String label, String argument) {
            this.label = label;
            this.argument = argument;
        }

        /** Preserve the old Sound checkbox when no new selection is saved. */
        public static SoundMode fromStored(String value, boolean oldSound) {
            if (value != null) {
                for (SoundMode option : values()) {
                    if (option.name().equals(value)) {
                        return option;
                    }
                }
            }
            return oldSound ? ADLIB : NONE;
        }

        @Override
        public String toString() {
            return label;
        }
    }

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
    public final boolean sound;
    public final SoundMode soundMode;
    public final String mt32RomDirectory;
    public final String mt32Model;
    public final boolean skipIntro;
    public final boolean showFps;
    public final boolean showMph;
    public final boolean multiCore;
    public final boolean showControlLayout;
    public final HyperVision hyperVision;
    public final boolean powerGear;
    public final boolean legacyCollision;
    public final boolean leftCornerBias;
    public final boolean dualWaySwitching;
    public final boolean chicaneFastGrass;
    public final boolean owoot;
    public final boolean amigaGrass;

    public LaunchOptions(boolean newMidi, boolean showFps, boolean showControlLayout,
            HyperVision hyperVision) {
        this(newMidi, showFps, showControlLayout, hyperVision, DEFAULT_POWER_GEAR,
            DEFAULT_LEGACY_COLLISION, DEFAULT_LEFT_CORNER_BIAS, DEFAULT_DUAL_WAY_SWITCHING,
            DEFAULT_CHICANE_FAST_GRASS, DEFAULT_OWOOT, DEFAULT_AMIGA_GRASS);
    }

    public LaunchOptions(boolean newMidi, boolean showFps, boolean showControlLayout,
            HyperVision hyperVision, boolean powerGear, boolean legacyCollision,
            boolean leftCornerBias, boolean dualWaySwitching, boolean chicaneFastGrass,
            boolean owoot, boolean amigaGrass) {
        this(newMidi, showFps, showControlLayout, hyperVision, powerGear, legacyCollision,
            leftCornerBias, dualWaySwitching, chicaneFastGrass, owoot, amigaGrass,
            DEFAULT_SOUND, DEFAULT_SKIP_INTRO);
    }

    public LaunchOptions(boolean newMidi, boolean showFps, boolean showControlLayout,
            HyperVision hyperVision, boolean powerGear, boolean legacyCollision,
            boolean leftCornerBias, boolean dualWaySwitching, boolean chicaneFastGrass,
            boolean owoot, boolean amigaGrass,
            boolean sound, boolean skipIntro) {
        this(newMidi, showFps, showControlLayout, hyperVision, powerGear, legacyCollision,
            leftCornerBias, dualWaySwitching, chicaneFastGrass, owoot, amigaGrass,
            sound, skipIntro, DEFAULT_MULTI_CORE);
    }

    public LaunchOptions(boolean newMidi, boolean showFps, boolean showControlLayout,
            HyperVision hyperVision, boolean powerGear, boolean legacyCollision,
            boolean leftCornerBias, boolean dualWaySwitching, boolean chicaneFastGrass,
            boolean owoot, boolean amigaGrass,
            boolean sound, boolean skipIntro, boolean multiCore) {
        this(newMidi, showFps, showControlLayout, hyperVision, powerGear, legacyCollision,
            leftCornerBias, dualWaySwitching, chicaneFastGrass, owoot, amigaGrass,
            sound, skipIntro, multiCore, DEFAULT_SHOW_MPH);
    }

    public LaunchOptions(boolean newMidi, boolean showFps, boolean showControlLayout,
            HyperVision hyperVision, boolean powerGear, boolean legacyCollision,
            boolean leftCornerBias, boolean dualWaySwitching, boolean chicaneFastGrass,
            boolean owoot, boolean amigaGrass,
            boolean sound, boolean skipIntro, boolean multiCore, boolean showMph) {
        this(newMidi, showFps, showControlLayout, hyperVision, powerGear, legacyCollision,
            leftCornerBias, dualWaySwitching, chicaneFastGrass, owoot, amigaGrass,
            sound ? SoundMode.ADLIB : SoundMode.NONE, skipIntro, multiCore, showMph, "", "");
    }

    private LaunchOptions(boolean newMidi, boolean showFps, boolean showControlLayout,
            HyperVision hyperVision, boolean powerGear, boolean legacyCollision,
            boolean leftCornerBias, boolean dualWaySwitching, boolean chicaneFastGrass,
            boolean owoot, boolean amigaGrass, SoundMode soundMode, boolean skipIntro,
            boolean multiCore, boolean showMph, String mt32RomDirectory, String mt32Model) {
        this.newMidi = newMidi;
        this.soundMode = soundMode == null ? SoundMode.ADLIB : soundMode;
        this.sound = this.soundMode != SoundMode.NONE;
        this.mt32RomDirectory = mt32RomDirectory == null ? "" : mt32RomDirectory;
        this.mt32Model = mt32Model == null ? "" : mt32Model;
        this.skipIntro = skipIntro;
        this.showFps = showFps;
        this.showMph = showMph;
        this.multiCore = multiCore;
        this.showControlLayout = showControlLayout;
        this.hyperVision = hyperVision == null ? HyperVision.OFF : hyperVision;
        this.powerGear = powerGear;
        this.legacyCollision = legacyCollision;
        this.leftCornerBias = leftCornerBias;
        this.dualWaySwitching = dualWaySwitching;
        this.chicaneFastGrass = chicaneFastGrass;
        this.owoot = owoot;
        this.amigaGrass = amigaGrass;
    }

    public LaunchOptions withSoundMode(SoundMode mode, String directory, String model) {
        return new LaunchOptions(newMidi, showFps, showControlLayout, hyperVision, powerGear,
            legacyCollision, leftCornerBias, dualWaySwitching, chicaneFastGrass, owoot,
            amigaGrass, mode, skipIntro, multiCore, showMph, directory, model);
    }

    public String[] arguments(String dataDirectory) {
        List<String> arguments = new ArrayList<>();
        arguments.add("--data-dir");
        arguments.add(dataDirectory);
        arguments.add(newMidi ? "--ogg:on" : "--ogg:off");
        arguments.add(showFps ? "--fps:on" : "--fps:off");
        arguments.add(showMph ? "--showmph:on" : "--showmph:off");
        arguments.add(multiCore ? "--multi-core:on" : "--multi-core:off");
        if (hyperVision != HyperVision.OFF) {
            arguments.add("--hv:" + hyperVision.argument);
        }
        arguments.add(showControlLayout ? "--control-layout:on" : "--control-layout:off");
        arguments.add(powerGear ? "--pg:on" : "--pg:off");
        arguments.add(legacyCollision ? "--lc:on" : "--lc:off");
        arguments.add(leftCornerBias ? "--lcb:on" : "--lcb:off");
        arguments.add(dualWaySwitching ? "--dws:on" : "--dws:off");
        arguments.add(chicaneFastGrass ? "--cfg:on" : "--cfg:off");
        if (amigaGrass) {
            arguments.add("--amigagrass");
        }
        if (owoot) {
            arguments.add("--owoot");
        }
        arguments.add(soundMode.argument);
        if (soundMode == SoundMode.MT32) {
            arguments.add("--mt-32-rom-dir");
            arguments.add(mt32RomDirectory);
            if (!mt32Model.isEmpty()) {
                arguments.add("--mt-32-model");
                arguments.add(mt32Model);
            }
        }
        if (skipIntro) {
            arguments.add("--nointro");
        }
        return arguments.toArray(new String[0]);
    }
}
