package org.restunts.android;

import java.util.Arrays;

public final class LaunchOptionsTest {
    private static final String DATA_DIRECTORY = "/local/imported game";

    private static void require(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static void requireArguments(LaunchOptions options, String... expected) {
        String[] actual = options.arguments(DATA_DIRECTORY);
        require(Arrays.equals(actual, expected), "Unexpected arguments: " + Arrays.toString(actual));
    }

    public static void main(String[] args) {
        require(!LaunchOptions.DEFAULT_NEW_MIDI, "New MIDI must default to original AdLib.");
        require(!LaunchOptions.DEFAULT_TILT_STEERING, "Tilt steering must default to off.");
        require(!LaunchOptions.DEFAULT_SHOW_FPS, "FPS must default to hidden.");
        require(LaunchOptions.DEFAULT_SHOW_CONTROL_LAYOUT, "Control layout must default to visible.");
        require(LaunchOptions.DEFAULT_POWER_GEAR, "Power gear must preserve its native default.");
        require(LaunchOptions.DEFAULT_LEGACY_COLLISION, "Original collision bugs must preserve their native default.");
        require(LaunchOptions.DEFAULT_LEFT_CORNER_BIAS, "Left corner bias must preserve its native default.");
        require(!LaunchOptions.DEFAULT_OWOOT, "OWOOT must default to disabled.");
        require(!LaunchOptions.DEFAULT_AMIGA_GRASS, "Amiga grass must default to disabled.");
        requireArguments(new LaunchOptions(LaunchOptions.DEFAULT_NEW_MIDI,
            LaunchOptions.DEFAULT_SHOW_FPS, LaunchOptions.DEFAULT_SHOW_CONTROL_LAYOUT,
            LaunchOptions.HyperVision.fromStored(null)),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on");
        requireArguments(new LaunchOptions(true, false, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:off", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on");
        requireArguments(new LaunchOptions(false, true, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:on", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on");
        requireArguments(new LaunchOptions(false, false, false, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.AUTO),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--hv:auto", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.FULL),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--hv:full", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--hv:high", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.MEDIUM),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--hv:medium", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.LOW),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--hv:low", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on");
        String[] labels = {"Off", "Auto", "Full", "High", "Medium", "Low"};
        LaunchOptions.HyperVision[] presets = LaunchOptions.HyperVision.values();
        require(presets.length == labels.length, "Unexpected HyperVision choices.");
        for (int index = 0; index < presets.length; index++) {
            require(presets[index].toString().equals(labels[index]), "Unexpected preset label or order.");
            require(LaunchOptions.HyperVision.fromStored(presets[index].name()) == presets[index],
                "Stored preset must survive a launcher restart.");
        }
        for (String invalid : new String[] {null, "", "auto", "unknown", "7"}) {
            require(LaunchOptions.HyperVision.fromStored(invalid) == LaunchOptions.HyperVision.OFF,
                "Invalid stored preset must fall back to Off.");
        }
        requireArguments(new LaunchOptions(false, false, true, null),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            false, true, true, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on",
            "--pg:off", "--lc:on", "--lcb:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, false, true, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on",
            "--pg:on", "--lc:off", "--lcb:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, false, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:off");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--owoot");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, false, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "/amigagrass");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH,
            false, false, false, true, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--hv:high", "--control-layout:off",
            "--pg:off", "--lc:off", "--lcb:off", "--owoot", "/amigagrass");
        System.out.println("Android launch defaults, independent graphics/driving flags and HyperVision presets passed.");
    }
}
