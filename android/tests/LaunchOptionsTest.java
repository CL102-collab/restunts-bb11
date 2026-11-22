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
        require(!LaunchOptions.DEFAULT_WIDESCREEN, "Widescreen must default to off.");
        require(!LaunchOptions.DEFAULT_SHOW_FPS, "FPS must default to hidden.");
        require(!LaunchOptions.DEFAULT_SHOW_MPH, "MPH must default to hidden.");
        require(LaunchOptions.DEFAULT_SHOW_DASHBOARD, "The dashboard must default to visible.");
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
        require(LaunchOptions.WIDESCREEN_KEY.equals("widescreen"), "Keep saved widescreen choices.");
        require(LaunchOptions.SHOW_MPH_KEY.equals("show_mph"), "Keep saved MPH choices.");
        require(LaunchOptions.SHOW_DASHBOARD_KEY.equals("show_dashboard"),
            "The dashboard selection must have its own preference key.");
        require(LaunchOptions.SKIP_INTRO_KEY.equals("skip_intro"), "Keep saved intro choices.");
        requireArguments(new LaunchOptions(LaunchOptions.DEFAULT_NEW_MIDI,
            LaunchOptions.DEFAULT_SHOW_FPS, LaunchOptions.DEFAULT_SHOW_CONTROL_LAYOUT,
            LaunchOptions.HyperVision.fromStored(null)),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, false, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, true, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:on", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, false, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--widescreen:off", "--fps:on", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.AUTO),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--widescreen:off", "--fps:on", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--hv:auto", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.FULL),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--widescreen:off", "--fps:on", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--hv:full", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--widescreen:off", "--fps:on", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--hv:high", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.MEDIUM),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--widescreen:off", "--fps:on", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--hv:medium", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.LOW),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--widescreen:off", "--fps:on", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--hv:low", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.FULL,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false,
            true, false, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga",
            "--hv:full", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false,
            true, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:off", "--videomode:vga",
            "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        for (boolean fps : new boolean[] {false, true}) {
            for (boolean mph : new boolean[] {false, true}) {
                LaunchOptions options = new LaunchOptions(false, fps, true,
                    LaunchOptions.HyperVision.OFF, true, true, true, true,
                    LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false,
                    true, false, true, mph);
                String[] startup = options.arguments(DATA_DIRECTORY);
                require(options.showMph == mph, "MPH must retain the launcher's saved choice.");
                require(Arrays.asList(startup).contains("--mph:on") == mph,
                    "Enabling MPH must emit its native startup option independently of FPS.");
                require(Arrays.asList(startup).contains("--mph:off") == !mph,
                    "Disabling MPH must emit its native startup option independently of FPS.");
                require(Arrays.asList(startup).contains("--fps:on") == fps,
                    "MPH must preserve the selected FPS option.");
            }
        }
        require(LaunchOptions.VIDEO_MODE_KEY.equals("video_mode"),
            "Keep saved classic video mode choices.");
        String[] videoLabels = {"VGA", "EGA", "CGA", "Tandy"};
        LaunchOptions.VideoMode[] videoModes = LaunchOptions.VideoMode.values();
        require(videoModes.length == videoLabels.length, "Unexpected classic video modes.");
        for (int index = 0; index < videoModes.length; index++) {
            LaunchOptions.VideoMode mode = videoModes[index];
            require(mode.toString().equals(videoLabels[index]), "Unexpected video label or order.");
            require(LaunchOptions.VideoMode.fromStored(mode.name()) == mode,
                "Stored video modes must survive a launcher restart.");
            for (LaunchOptions.HyperVision vision : LaunchOptions.HyperVision.values()) {
                LaunchOptions original = new LaunchOptions(true, true, false, vision);
                LaunchOptions selected = original.withVideoMode(mode);
                java.util.List<String> arguments = Arrays.asList(selected.arguments(DATA_DIRECTORY));
                String expectedVideo = "--videomode:" + videoLabels[index].toLowerCase(java.util.Locale.ROOT);
                require(arguments.contains(expectedVideo), "Emit the selected classic video mode.");
                long modeArguments = arguments.stream().filter(value -> value.startsWith("--videomode:")).count();
                require(modeArguments == 1, "Emit exactly one classic video mode.");
                java.util.List<String> originalArguments = new java.util.ArrayList<>(
                    Arrays.asList(original.arguments(DATA_DIRECTORY)));
                originalArguments.set(originalArguments.indexOf("--videomode:vga"), expectedVideo);
                require(arguments.equals(originalArguments),
                    "Changing classic video mode must preserve all other launch choices.");
                require(selected.withSoundMode(LaunchOptions.SoundMode.MT32, "/roms", "test").videoMode == mode,
                    "Sound changes must preserve the selected classic video mode.");
            }
        }
        for (String invalid : new String[] {null, "", "ega", "cga", "tandy", "unknown", "7"}) {
            require(LaunchOptions.VideoMode.fromStored(invalid) == LaunchOptions.VideoMode.VGA,
                "Missing or invalid stored video modes must default to VGA.");
        }
        require(new LaunchOptions(false, false, true, null).videoMode == LaunchOptions.VideoMode.VGA,
            "Existing launcher constructors must preserve VGA by default.");
        require(new LaunchOptions(false, false, true, null).withVideoMode(null).videoMode
                == LaunchOptions.VideoMode.VGA,
            "Missing Intent selections must default to VGA.");
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
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            false, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:on",
            "--pg:off", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, false, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:on",
            "--pg:on", "--lc:off", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, false, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:off", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, true, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on", "--owoot");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on", "--amigagrass");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH,
            false, false, false, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, true, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--widescreen:off", "--fps:on", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--hv:high", "--control-layout:off",
            "--pg:off", "--lc:off", "--lcb:off", "--dws:on", "--cfg:on", "--amigagrass", "--owoot");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false, LaunchOptions.DEFAULT_SOUND,
            LaunchOptions.DEFAULT_SKIP_INTRO),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on", "/ns");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false, true, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on", "--nointro");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH,
            false, false, false, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, true, true, false, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--widescreen:off", "--fps:on", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--hv:high", "--control-layout:off",
            "--pg:off", "--lc:off", "--lcb:off", "--dws:on", "--cfg:on", "--amigagrass", "--owoot", "/ns", "--nointro");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH,
            false, false, false, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, true, true, true, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--widescreen:off", "--fps:on", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--hv:high", "--control-layout:off",
            "--pg:off", "--lc:off", "--lcb:off", "--dws:on", "--cfg:on", "--amigagrass", "--owoot");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, false, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--widescreen:off", "--fps:off", "--mph:off",
            "--dashboard:on", "--multi-core:on", "--videomode:vga", "--control-layout:on",
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
        String[] soundLabels = {"No sound", "PC Speaker", "Tandy", "Ad Lib", "MT-32"};
        require(LaunchOptions.SoundMode.values().length == soundLabels.length,
            "The sound dropdown must contain exactly the supported modes.");
        for (int index = 0; index < soundLabels.length; index++) {
            LaunchOptions.SoundMode mode = LaunchOptions.SoundMode.values()[index];
            require(mode.toString().equals(soundLabels[index]), "Unexpected sound dropdown label.");
            require(LaunchOptions.SoundMode.fromStored(mode.name(), true) == mode,
                "Sound mode must survive saving and restoring.");
        }
        LaunchOptions defaults = new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF);
        require(!defaults.widescreen, "Existing launch settings must keep widescreen disabled.");
        LaunchOptions dashboard = defaults.withVideoMode(LaunchOptions.VideoMode.TANDY)
            .withSoundMode(LaunchOptions.SoundMode.MT32, "/roms", "model").withWidescreen(true);
        for (boolean visible : new boolean[] {false, true}) {
            LaunchOptions selected = dashboard.withShowDashboard(visible);
            java.util.List<String> expected = new java.util.ArrayList<>(
                Arrays.asList(dashboard.arguments(DATA_DIRECTORY)));
            expected.set(expected.indexOf("--dashboard:on"),
                visible ? "--dashboard:on" : "--dashboard:off");
            require(selected.showDashboard == visible
                    && Arrays.asList(selected.arguments(DATA_DIRECTORY)).equals(expected),
                "Changing dashboard visibility must change only its native startup argument.");
            require(selected.withVideoMode(LaunchOptions.VideoMode.VGA).showDashboard == visible
                    && selected.withSoundMode(LaunchOptions.SoundMode.NONE, "", "").showDashboard == visible
                    && selected.withWidescreen(false).showDashboard == visible,
                "Changing other launch options must preserve the dashboard selection.");
        }
        for (LaunchOptions.HyperVision vision : LaunchOptions.HyperVision.values()) {
            LaunchOptions original = new LaunchOptions(true, true, false, vision)
                .withVideoMode(LaunchOptions.VideoMode.TANDY)
                .withSoundMode(LaunchOptions.SoundMode.MT32, "/roms", "model");
            for (boolean enabled : new boolean[] {false, true}) {
                LaunchOptions wide = original.withWidescreen(enabled);
                require(wide.widescreen == enabled, "Retain the selected widescreen option.");
                java.util.List<String> expectedWide = new java.util.ArrayList<>(
                    Arrays.asList(original.arguments(DATA_DIRECTORY)));
                expectedWide.set(expectedWide.indexOf("--widescreen:off"),
                    enabled ? "--widescreen:on" : "--widescreen:off");
                require(Arrays.asList(wide.arguments(DATA_DIRECTORY)).equals(expectedWide),
                    "Widescreen must change only its own native argument for every HyperVision preset.");
                for (LaunchOptions.VideoMode mode : LaunchOptions.VideoMode.values()) {
                    require(wide.withVideoMode(mode).widescreen == enabled,
                        "Classic video changes must preserve widescreen for later HyperVision use.");
                }
                for (LaunchOptions.SoundMode mode : LaunchOptions.SoundMode.values()) {
                    require(wide.withSoundMode(mode, "/roms", "model").widescreen == enabled,
                        "Sound changes must preserve the widescreen setting.");
                }
                require(!wide.withWidescreen(false).widescreen,
                    "Turning widescreen off must restore the original setting.");
            }
        }
        String romDirectory = "/data/user/0/org.restunts.android/files/mt32-roms";
        String model = "mt32_1_07";
        LaunchOptions mt32 = defaults.withSoundMode(LaunchOptions.SoundMode.MT32, romDirectory, model);
        for (LaunchOptions.VideoMode mode : LaunchOptions.VideoMode.values()) {
            LaunchOptions selectedVideo = mt32.withVideoMode(mode);
            java.util.List<String> expectedVideoMt32 = new java.util.ArrayList<>(
                Arrays.asList(mt32.arguments(DATA_DIRECTORY)));
            expectedVideoMt32.set(expectedVideoMt32.indexOf("--videomode:vga"),
                "--videomode:" + mode.name().toLowerCase(java.util.Locale.ROOT));
            require(Arrays.asList(selectedVideo.arguments(DATA_DIRECTORY)).equals(expectedVideoMt32),
                "Video mode changes must preserve the selected MT-32 ROM folder and model.");
        }
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
                LaunchOptions.SoundMode.ADLIB, LaunchOptions.SoundMode.PC_SPEAKER,
                LaunchOptions.SoundMode.TANDY,
                LaunchOptions.SoundMode.NONE}) {
            LaunchOptions inactive = mt32.withSoundMode(mode, romDirectory, model);
            java.util.List<String> arguments = Arrays.asList(inactive.arguments(DATA_DIRECTORY));
            require(!arguments.contains("--mt-32-rom-dir") && !arguments.contains("--mt-32-model")
                    && !arguments.contains(romDirectory) && !arguments.contains(model),
                "Inactive MT-32 settings must never affect Ad Lib, PC Speaker, Tandy or muted launches.");
            require(arguments.contains("--nosound") == (mode == LaunchOptions.SoundMode.NONE),
                "Only No sound emits --nosound.");
            require(arguments.contains("--ogg:off"), "Keep the OGG selection independent of sound mode.");
        }
        LaunchOptions pcSpeaker = defaults.withSoundMode(LaunchOptions.SoundMode.PC_SPEAKER,
            romDirectory, model);
        java.util.List<String> speakerArguments = Arrays.asList(pcSpeaker.arguments(DATA_DIRECTORY));
        require(pcSpeaker.sound && speakerArguments.contains("--soundmode:pc-speaker")
                && !speakerArguments.contains("--soundmode:adlib")
                && !speakerArguments.contains("--soundmode:mt-32")
                && !speakerArguments.contains("--nosound"),
            "PC Speaker must emit only its own enabled sound mode.");
        require(speakerArguments.contains("--ogg:off"),
            "Choosing PC Speaker must preserve the OGG music default.");
        require(LaunchOptions.SoundMode.fromStored("PC_SPEAKER", true)
                == LaunchOptions.SoundMode.PC_SPEAKER,
            "Remember PC Speaker without depending on dropdown ordinals.");
        require(LaunchOptions.SoundMode.fromStored("ADLIB", true) == LaunchOptions.SoundMode.ADLIB,
            "Adding a dropdown item must preserve previously saved Ad Lib choices.");
        LaunchOptions tandy = defaults.withSoundMode(LaunchOptions.SoundMode.TANDY, romDirectory, model);
        java.util.List<String> tandyArguments = Arrays.asList(tandy.arguments(DATA_DIRECTORY));
        require(tandy.sound && tandyArguments.contains("--soundmode:tandy")
                && !tandyArguments.contains("--soundmode:adlib")
                && !tandyArguments.contains("--nosound") && tandyArguments.contains("--ogg:off"),
            "Tandy must select its enabled sound mode and preserve the OGG music default.");
        require(LaunchOptions.SoundMode.fromStored("TANDY", true) == LaunchOptions.SoundMode.TANDY,
            "Remember Tandy without depending on dropdown ordinals.");
        LaunchOptions oggMt32 = new LaunchOptions(true, false, true, LaunchOptions.HyperVision.OFF)
            .withSoundMode(LaunchOptions.SoundMode.MT32, romDirectory, model);
        require(Arrays.asList(oggMt32.arguments(DATA_DIRECTORY)).contains("--ogg:on"),
            "OGG music replacement can coexist with MT-32 effects.");
        System.out.println("Android defaults, saved option keys, dashboard and other launch flags passed.");
    }
}
