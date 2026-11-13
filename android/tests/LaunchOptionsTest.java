package org.restunts.android;

import java.util.Arrays;

public final class LaunchOptionsTest {
    private static final int CHICANE_FAST_GRASS_MASK = 64;
    private static final int DRIVING_RULE_COMBINATIONS = CHICANE_FAST_GRASS_MASK * 2;
    private static final String DATA_DIRECTORY = "/local/imported game";

    private static void require(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static void requireArguments(LaunchOptions options, String... expected) {
        java.util.ArrayList<String> modernExpected = new java.util.ArrayList<>(Arrays.asList(expected));
        int mute = modernExpected.indexOf("/ns");
        if (mute >= 0) {
            modernExpected.set(mute, "--nosound");
        } else {
            int intro = modernExpected.indexOf("--nointro");
            modernExpected.add(intro < 0 ? modernExpected.size() : intro, "--soundmode:adlib");
        }
        String[] actual = options.arguments(DATA_DIRECTORY);
        require(Arrays.equals(actual, modernExpected.toArray(new String[0])),
            "Unexpected arguments: " + Arrays.toString(actual));
    }

    public static void main(String[] args) {
        require(!LaunchOptions.DEFAULT_NEW_MIDI, "New MIDI must default to original AdLib.");
        require(LaunchOptions.DEFAULT_SOUND, "Sound must remain enabled by default.");
        require(!LaunchOptions.DEFAULT_SKIP_INTRO, "The intro must remain enabled by default.");
        require(!LaunchOptions.DEFAULT_TILT_STEERING, "Tilt steering must default to off.");
        require(!LaunchOptions.DEFAULT_SHOW_FPS, "FPS must default to hidden.");
        require(!LaunchOptions.DEFAULT_SHOW_MPH, "MPH must default to hidden.");
        require(LaunchOptions.DEFAULT_MULTI_CORE, "Multi-core rendering must default to on.");
        require(LaunchOptions.DEFAULT_SHOW_CONTROL_LAYOUT, "Control layout must default to visible.");
        require(LaunchOptions.DEFAULT_POWER_GEAR, "Power gear must preserve its native default.");
        require(LaunchOptions.DEFAULT_LEGACY_COLLISION, "Original collision bugs must preserve their native default.");
        require(LaunchOptions.DEFAULT_LEFT_CORNER_BIAS, "Left corner bias must preserve its native default.");
        require(LaunchOptions.DEFAULT_DUAL_WAY_SWITCHING, "Dual-Way Switching must preserve its native default.");
        require(LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, "Chicane fast-grass must preserve its native default.");
        require(!LaunchOptions.DEFAULT_OWOOT, "OWOOT must default to disabled.");
        require(!LaunchOptions.DEFAULT_AMIGA_GRASS, "Amiga grass must default to disabled.");
        require(LaunchOptions.POWER_GEAR_KEY.equals("power_gear"), "Keep saved Power Gear choices.");
        require(LaunchOptions.LEGACY_COLLISION_KEY.equals("legacy_collision"), "Keep saved collision choices.");
        require(LaunchOptions.LEFT_CORNER_BIAS_KEY.equals("left_corner_bias"), "Keep saved corner choices.");
        require(LaunchOptions.DUAL_WAY_SWITCHING_KEY.equals("dual_way_switching"), "Keep saved switching choices.");
        require(LaunchOptions.CHICANE_FAST_GRASS_KEY.equals("chicane_fast_grass"),
            "Keep saved Chicane fast-grass choices.");
        require(LaunchOptions.OWOOT_KEY.equals("owoot"), "Keep saved OWOOT choices.");
        require(LaunchOptions.AMIGA_GRASS_KEY.equals("amiga_grass"), "Keep saved Amiga Grass choices.");
        require(LaunchOptions.SOUND_KEY.equals("sound"), "Keep saved sound choices.");
        require(LaunchOptions.MULTI_CORE_KEY.equals("multi_core"), "Keep saved multi-core choices.");
        require(LaunchOptions.SHOW_MPH_KEY.equals("show_mph"), "Keep saved MPH choices.");
        require(LaunchOptions.SKIP_INTRO_KEY.equals("skip_intro"), "Keep saved intro choices.");
        requireArguments(new LaunchOptions(LaunchOptions.DEFAULT_NEW_MIDI,
            LaunchOptions.DEFAULT_SHOW_FPS, LaunchOptions.DEFAULT_SHOW_CONTROL_LAYOUT,
            LaunchOptions.HyperVision.fromStored(null)),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--showmph:off", "--multi-core:on", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, false, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:off", "--showmph:off", "--multi-core:on", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, true, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:on", "--showmph:off", "--multi-core:on", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, false, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--showmph:off", "--multi-core:on", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--showmph:off", "--multi-core:on", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.AUTO),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--showmph:off", "--multi-core:on", "--hv:auto", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.FULL),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--showmph:off", "--multi-core:on", "--hv:full", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--showmph:off", "--multi-core:on", "--hv:high", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.MEDIUM),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--showmph:off", "--multi-core:on", "--hv:medium", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.LOW),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--showmph:off", "--multi-core:on", "--hv:low", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.FULL,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false,
            true, false, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--showmph:off", "--multi-core:on",
            "--hv:full", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false,
            true, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--showmph:off", "--multi-core:off",
            "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        for (boolean fps : new boolean[] {false, true}) {
            for (boolean mph : new boolean[] {false, true}) {
                LaunchOptions options = new LaunchOptions(false, fps, true,
                    LaunchOptions.HyperVision.OFF, true, true, true, true,
                    LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false,
                    true, false, true, mph);
                String[] startup = options.arguments(DATA_DIRECTORY);
                require(options.showMph == mph, "MPH must retain the launcher's saved choice.");
                require(Arrays.asList(startup).contains("--showmph:on") == mph,
                    "Enabling MPH must emit its native startup option independently of FPS.");
                require(Arrays.asList(startup).contains("--showmph:off") == !mph,
                    "Disabling MPH must emit its native startup option independently of FPS.");
                require(Arrays.asList(startup).contains("--fps:on") == fps,
                    "MPH must preserve the selected FPS option.");
            }
        }
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
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--showmph:off", "--multi-core:on", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            false, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--showmph:off", "--multi-core:on", "--control-layout:on",
            "--pg:off", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, false, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--showmph:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:off", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, false, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--showmph:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:off", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, true, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--showmph:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on", "--owoot");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--showmph:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on", "--amigagrass");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH,
            false, false, false, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, true, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--showmph:off", "--multi-core:on", "--hv:high", "--control-layout:off",
            "--pg:off", "--lc:off", "--lcb:off", "--dws:on", "--cfg:on", "--amigagrass", "--owoot");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false, LaunchOptions.DEFAULT_SOUND,
            LaunchOptions.DEFAULT_SKIP_INTRO),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--showmph:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--showmph:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on", "/ns");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false, true, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--showmph:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on", "--nointro");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH,
            false, false, false, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, true, true, false, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--showmph:off", "--multi-core:on", "--hv:high", "--control-layout:off",
            "--pg:off", "--lc:off", "--lcb:off", "--dws:on", "--cfg:on", "--amigagrass", "--owoot", "/ns", "--nointro");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH,
            false, false, false, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, true, true, true, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--showmph:off", "--multi-core:on", "--hv:high", "--control-layout:off",
            "--pg:off", "--lc:off", "--lcb:off", "--dws:on", "--cfg:on", "--amigagrass", "--owoot");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, false, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--showmph:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:off", "--cfg:on");
        for (boolean midi : new boolean[] {false, true}) {
            for (boolean fps : new boolean[] {false, true}) {
                for (boolean controls : new boolean[] {false, true}) {
                    for (LaunchOptions.HyperVision vision : LaunchOptions.HyperVision.values()) {
                        for (int rules = 0; rules < DRIVING_RULE_COMBINATIONS; rules++) {
                            boolean pg = (rules & 1) != 0;
                            boolean lc = (rules & 2) != 0;
                            boolean lcb = (rules & 4) != 0;
                            boolean dws = (rules & 8) != 0;
                            boolean owoot = (rules & 16) != 0;
                            boolean grass = (rules & 32) != 0;
                            boolean fastGrass = (rules & CHICANE_FAST_GRASS_MASK) != 0;
                            String[] normal = new LaunchOptions(midi, fps, controls, vision,
                                pg, lc, lcb, dws, fastGrass, owoot, grass).arguments(DATA_DIRECTORY);
                            require(Arrays.asList(normal).contains("--dws:on") == dws,
                                "Switching must reflect the selected driving rule.");
                            require(Arrays.asList(normal).contains("--dws:off") == !dws,
                                "Disabling switching must emit its native startup option.");
                            require(Arrays.asList(normal).contains("--cfg:on") == fastGrass,
                                "Chicane fast-grass must reflect the selected driving rule.");
                            require(Arrays.asList(normal).contains("--cfg:off") == !fastGrass,
                                "Disabling Chicane fast-grass must emit its native startup option.");
                            for (boolean sound : new boolean[] {false, true}) {
                                for (boolean skipIntro : new boolean[] {false, true}) {
                                    String[] startup = new LaunchOptions(midi, fps, controls, vision,
                                        pg, lc, lcb, dws, fastGrass, owoot, grass, sound, skipIntro)
                                        .arguments(DATA_DIRECTORY);
                                    require(Arrays.equals(Arrays.copyOf(normal, normal.length - 1),
                                            Arrays.copyOf(startup, normal.length - 1)),
                                        "Sound and intro must preserve all other launch choices.");
                                    require(Arrays.asList(startup).contains("--nosound") == !sound,
                                        "Only disabling Sound must request native silence.");
                                    require(Arrays.asList(startup).contains("--nointro") == skipIntro,
                                        "Only selecting Skip intro must skip the native intro.");
                                    require(startup.length == normal.length
                                            + (skipIntro ? 1 : 0),
                                        "Startup options must add only their own arguments.");
                                }
                            }
                        }
                    }
                }
            }
        }
        require(LaunchOptions.SoundMode.fromStored(null, true) == LaunchOptions.SoundMode.ADLIB,
            "Existing enabled sound preferences must migrate to Ad Lib.");
        require(LaunchOptions.SoundMode.fromStored(null, false) == LaunchOptions.SoundMode.NONE,
            "Existing disabled sound preferences must migrate to No sound.");
        require(LaunchOptions.SoundMode.fromStored("invalid", false) == LaunchOptions.SoundMode.NONE,
            "Invalid stored modes must preserve the old disabled sound preference.");
        require(LaunchOptions.SoundMode.fromStored("MT32", false) == LaunchOptions.SoundMode.MT32,
            "An explicit mode must supersede the legacy sound preference.");
        String[] soundLabels = {"No sound", "Ad Lib", "MT-32"};
        for (int index = 0; index < soundLabels.length; index++) {
            LaunchOptions.SoundMode mode = LaunchOptions.SoundMode.values()[index];
            require(mode.toString().equals(soundLabels[index]), "Unexpected sound dropdown label.");
            require(LaunchOptions.SoundMode.fromStored(mode.name(), true) == mode,
                "Sound mode must survive saving and restoring.");
        }
        LaunchOptions defaults = new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF);
        String romDirectory = "/data/user/0/org.restunts.android/files/mt32-roms";
        String model = "mt32_1_07";
        LaunchOptions mt32 = defaults.withSoundMode(LaunchOptions.SoundMode.MT32, romDirectory, model);
        java.util.List<String> mt32Arguments = Arrays.asList(mt32.arguments(DATA_DIRECTORY));
        require(mt32.sound && mt32.soundMode == LaunchOptions.SoundMode.MT32,
            "MT-32 is an enabled sound mode.");
        require(mt32Arguments.contains("--soundmode:mt-32")
                && !mt32Arguments.contains("--soundmode:adlib")
                && !mt32Arguments.contains("--nosound"),
            "MT-32 must emit exactly its selected sound mode.");
        require(mt32Arguments.get(mt32Arguments.indexOf("--mt-32-rom-dir") + 1).equals(romDirectory),
            "Pass the app-private ROM directory as a separate argument.");
        require(mt32Arguments.get(mt32Arguments.indexOf("--mt-32-model") + 1).equals(model),
            "Pass the exact selected ROM set to native playback.");
        for (LaunchOptions.SoundMode mode : new LaunchOptions.SoundMode[] {
                LaunchOptions.SoundMode.ADLIB, LaunchOptions.SoundMode.NONE}) {
            LaunchOptions inactive = mt32.withSoundMode(mode, romDirectory, model);
            java.util.List<String> arguments = Arrays.asList(inactive.arguments(DATA_DIRECTORY));
            require(!arguments.contains("--mt-32-rom-dir") && !arguments.contains("--mt-32-model")
                    && !arguments.contains(romDirectory) && !arguments.contains(model),
                "Inactive MT-32 settings must never affect Ad Lib or muted launches.");
            require(arguments.contains("--nosound") == (mode == LaunchOptions.SoundMode.NONE),
                "Only No sound emits --nosound.");
            require(arguments.contains("--ogg:off"), "Keep the OGG selection independent of sound mode.");
        }
        LaunchOptions oggMt32 = new LaunchOptions(true, false, true, LaunchOptions.HyperVision.OFF)
            .withSoundMode(LaunchOptions.SoundMode.MT32, romDirectory, model);
        require(Arrays.asList(oggMt32.arguments(DATA_DIRECTORY)).contains("--ogg:on"),
            "OGG music replacement can coexist with MT-32 effects.");
        System.out.println("Android defaults, saved option keys, graphics/driving/audio/intro flags passed.");
    }
}
