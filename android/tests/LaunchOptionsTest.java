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
        require(LaunchOptions.DEFAULT_SOUND, "Sound must remain enabled by default.");
        require(!LaunchOptions.DEFAULT_SKIP_INTRO, "The intro must remain enabled by default.");
        require(!LaunchOptions.DEFAULT_TILT_STEERING, "Tilt steering must default to off.");
        require(!LaunchOptions.DEFAULT_SHOW_FPS, "FPS must default to hidden.");
        require(LaunchOptions.DEFAULT_SHOW_CONTROL_LAYOUT, "Control layout must default to visible.");
        require(LaunchOptions.DEFAULT_POWER_GEAR, "Power gear must preserve its native default.");
        require(LaunchOptions.DEFAULT_LEGACY_COLLISION, "Original collision bugs must preserve their native default.");
        require(LaunchOptions.DEFAULT_LEFT_CORNER_BIAS, "Left corner bias must preserve its native default.");
        require(LaunchOptions.DEFAULT_DUAL_WAY_SWITCHING, "Dual-Way Switching must preserve its native default.");
        require(!LaunchOptions.DEFAULT_OWOOT, "OWOOT must default to disabled.");
        require(!LaunchOptions.DEFAULT_AMIGA_GRASS, "Amiga grass must default to disabled.");
        require(LaunchOptions.POWER_GEAR_KEY.equals("power_gear"), "Keep saved Power Gear choices.");
        require(LaunchOptions.LEGACY_COLLISION_KEY.equals("legacy_collision"), "Keep saved collision choices.");
        require(LaunchOptions.LEFT_CORNER_BIAS_KEY.equals("left_corner_bias"), "Keep saved corner choices.");
        require(LaunchOptions.DUAL_WAY_SWITCHING_KEY.equals("dual_way_switching"), "Keep saved switching choices.");
        require(LaunchOptions.OWOOT_KEY.equals("owoot"), "Keep saved OWOOT choices.");
        require(LaunchOptions.AMIGA_GRASS_KEY.equals("amiga_grass"), "Keep saved Amiga Grass choices.");
        require(LaunchOptions.SOUND_KEY.equals("sound"), "Keep saved sound choices.");
        require(LaunchOptions.SKIP_INTRO_KEY.equals("skip_intro"), "Keep saved intro choices.");
        requireArguments(new LaunchOptions(LaunchOptions.DEFAULT_NEW_MIDI,
            LaunchOptions.DEFAULT_SHOW_FPS, LaunchOptions.DEFAULT_SHOW_CONTROL_LAYOUT,
            LaunchOptions.HyperVision.fromStored(null)),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on");
        requireArguments(new LaunchOptions(true, false, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:off", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on");
        requireArguments(new LaunchOptions(false, true, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:on", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on");
        requireArguments(new LaunchOptions(false, false, false, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.AUTO),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--hv:auto", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.FULL),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--hv:full", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--hv:high", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.MEDIUM),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--hv:medium", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.LOW),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--hv:low", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on");
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
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            false, true, true, true, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on",
            "--pg:off", "--lc:on", "--lcb:on", "--dws:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, false, true, true, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on",
            "--pg:on", "--lc:off", "--lcb:on", "--dws:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, false, true, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:off", "--dws:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, true, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--owoot");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, false, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "/amigagrass");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH,
            false, false, false, true, true, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--hv:high", "--control-layout:off",
            "--pg:off", "--lc:off", "--lcb:off", "--dws:on", "--owoot", "/amigagrass");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, false, false, LaunchOptions.DEFAULT_SOUND,
            LaunchOptions.DEFAULT_SKIP_INTRO),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, false, false, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "/ns");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, false, false, true, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--nointro");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH,
            false, false, false, true, true, true, false, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--hv:high", "--control-layout:off",
            "--pg:off", "--lc:off", "--lcb:off", "--dws:on", "--owoot", "/amigagrass", "/ns", "--nointro");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH,
            false, false, false, true, true, true, true, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--hv:high", "--control-layout:off",
            "--pg:off", "--lc:off", "--lcb:off", "--dws:on", "--owoot", "/amigagrass");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, false, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:off");
        for (boolean midi : new boolean[] {false, true}) {
            for (boolean fps : new boolean[] {false, true}) {
                for (boolean controls : new boolean[] {false, true}) {
                    for (LaunchOptions.HyperVision vision : LaunchOptions.HyperVision.values()) {
                        for (int rules = 0; rules < 64; rules++) {
                            boolean pg = (rules & 1) != 0;
                            boolean lc = (rules & 2) != 0;
                            boolean lcb = (rules & 4) != 0;
                            boolean dws = (rules & 8) != 0;
                            boolean owoot = (rules & 16) != 0;
                            boolean grass = (rules & 32) != 0;
                            String[] normal = new LaunchOptions(midi, fps, controls, vision,
                                pg, lc, lcb, dws, owoot, grass).arguments(DATA_DIRECTORY);
                            require(Arrays.asList(normal).contains("--dws:on") == dws,
                                "Switching must reflect the selected driving rule.");
                            require(Arrays.asList(normal).contains("--dws:off") == !dws,
                                "Disabling switching must emit its native startup option.");
                            for (boolean sound : new boolean[] {false, true}) {
                                for (boolean skipIntro : new boolean[] {false, true}) {
                                    String[] startup = new LaunchOptions(midi, fps, controls, vision,
                                        pg, lc, lcb, dws, owoot, grass, sound, skipIntro)
                                        .arguments(DATA_DIRECTORY);
                                    require(Arrays.equals(normal,
                                            Arrays.copyOf(startup, normal.length)),
                                        "Sound and intro must preserve all other launch choices.");
                                    require(Arrays.asList(startup).contains("/ns") == !sound,
                                        "Only disabling Sound must request native silence.");
                                    require(Arrays.asList(startup).contains("--nointro") == skipIntro,
                                        "Only selecting Skip intro must skip the native intro.");
                                    require(startup.length == normal.length
                                            + (sound ? 0 : 1) + (skipIntro ? 1 : 0),
                                        "Startup options must add only their own arguments.");
                                }
                            }
                        }
                    }
                }
            }
        }
        System.out.println("Android defaults, saved option keys, graphics/driving/audio/intro flags passed.");
    }
}
