package org.restunts.android;

import java.util.Arrays;
import java.util.List;

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
        String[] actual = options.arguments(DATA_DIRECTORY);
        require(Arrays.equals(actual, expected), "Unexpected arguments: " + Arrays.toString(actual));
    }

    private static LaunchOptions graphicsOptions(LaunchOptions.Renderer renderer, int device,
            boolean multiCore) {
        return new LaunchOptions(LaunchOptions.DEFAULT_NEW_MIDI, LaunchOptions.DEFAULT_SHOW_FPS,
            LaunchOptions.DEFAULT_SHOW_CONTROL_LAYOUT, LaunchOptions.HyperVision.OFF,
            LaunchOptions.DEFAULT_POWER_GEAR, LaunchOptions.DEFAULT_LEGACY_COLLISION,
            LaunchOptions.DEFAULT_LEFT_CORNER_BIAS, LaunchOptions.DEFAULT_DUAL_WAY_SWITCHING,
            LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, LaunchOptions.DEFAULT_OWOOT,
            LaunchOptions.DEFAULT_AMIGA_GRASS, LaunchOptions.DEFAULT_SOUND,
            LaunchOptions.DEFAULT_SKIP_INTRO, multiCore, renderer, device);
    }

    private static void rendererSelection() {
        require(LaunchOptions.Renderer.SOFTWARE.toString().equals("Software"),
            "Software must be the first renderer choice.");
        require(LaunchOptions.Renderer.VULKAN.toString().equals("Vulkan"),
            "Keep the native renderer label.");
        require(LaunchOptions.Renderer.values()[0] == LaunchOptions.Renderer.SOFTWARE,
            "Software must remain the default renderer.");
        for (LaunchOptions.Renderer renderer : LaunchOptions.Renderer.values()) {
            require(LaunchOptions.Renderer.fromStored(renderer.value) == renderer,
                "Renderer selection must survive preference and Intent storage.");
        }
        for (String invalid : new String[] {null, "", "Software", "VULKAN", "unknown"}) {
            require(LaunchOptions.Renderer.fromStored(invalid) == LaunchOptions.Renderer.SOFTWARE,
                "Invalid renderer preferences must restore Software.");
        }
        LaunchOptions software = graphicsOptions(LaunchOptions.Renderer.SOFTWARE, 3, true);
        List<String> softwareArguments = Arrays.asList(software.arguments(DATA_DIRECTORY));
        require(softwareArguments.contains("--multi-core:on"), "Software keeps the worker preference.");
        require(softwareArguments.stream().noneMatch(value -> value.startsWith("--renderer")),
            "Software must not request a Vulkan device.");
        require(software.rendererDevice == 3, "Software must retain the remembered Vulkan device.");
        LaunchOptions vulkan = graphicsOptions(LaunchOptions.Renderer.VULKAN, 3, true);
        List<String> vulkanArguments = Arrays.asList(vulkan.arguments(DATA_DIRECTORY));
        require(vulkanArguments.contains("--renderer:vulkan"), "Vulkan must select the native renderer.");
        require(vulkanArguments.contains("--renderer-device:3"), "Forward the selected native device index.");
        require(vulkanArguments.contains("--multi-core:off") && !vulkanArguments.contains("--multi-core:on"),
            "Vulkan must never run CPU rendering workers.");
        require(vulkan.multiCore, "Disabling the Vulkan widget must not erase the Software preference.");
        for (int automatic : new int[] {LaunchOptions.DEFAULT_RENDERER_DEVICE, -100}) {
            LaunchOptions options = graphicsOptions(LaunchOptions.Renderer.VULKAN, automatic, false);
            require(options.rendererDevice == LaunchOptions.DEFAULT_RENDERER_DEVICE,
                "Invalid saved indices must restore automatic native device selection.");
            require(Arrays.stream(options.arguments(DATA_DIRECTORY))
                    .noneMatch(value -> value.startsWith("--renderer-device:")),
                "Automatic selection must not pass an invalid device index.");
        }
        require(graphicsOptions(null, LaunchOptions.DEFAULT_RENDERER_DEVICE, true).renderer
                == LaunchOptions.Renderer.SOFTWARE,
            "Missing Intent renderer selection must remain Software.");
        require(VulkanDevices.query(VulkanDevices.MINIMUM_VULKAN_API - 1).isEmpty(),
            "Android 5/6 must remain usable without loading a Vulkan library.");
        List<VulkanDevices.Device> devices = VulkanDevices.fromNativeReport(
            new String[] {"0", "Same GPU", "3", "Same GPU"});
        require(devices.size() == 2 && devices.get(1).index == 3,
            "Native physical-device indices must not be replaced by dropdown positions.");
        require(!devices.get(0).toString().equals(devices.get(1).toString()),
            "Identical graphics-device names must still have distinguishable choices.");
        require(VulkanDevices.find(devices, 3) == devices.get(1)
                && VulkanDevices.find(devices, 1) == null,
            "Revalidation must find the actual native device index.");
        for (String[] invalid : new String[][] {null, {"0"}, {"x", "GPU"}, {"-1", "GPU"},
                {"0", null}, {"0", " "}, {"0", "GPU", "0", "Other GPU"}}) {
            require(VulkanDevices.fromNativeReport(invalid).isEmpty(),
                "Incomplete or invalid native inventories must not advertise Vulkan.");
        }
    }

    public static void main(String[] args) {
        rendererSelection();
        require(!LaunchOptions.DEFAULT_NEW_MIDI, "New MIDI must default to original AdLib.");
        require(LaunchOptions.DEFAULT_SOUND, "Sound must remain enabled by default.");
        require(!LaunchOptions.DEFAULT_SKIP_INTRO, "The intro must remain enabled by default.");
        require(!LaunchOptions.DEFAULT_TILT_STEERING, "Tilt steering must default to off.");
        require(!LaunchOptions.DEFAULT_SHOW_FPS, "FPS must default to hidden.");
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
        require(LaunchOptions.SKIP_INTRO_KEY.equals("skip_intro"), "Keep saved intro choices.");
        requireArguments(new LaunchOptions(LaunchOptions.DEFAULT_NEW_MIDI,
            LaunchOptions.DEFAULT_SHOW_FPS, LaunchOptions.DEFAULT_SHOW_CONTROL_LAYOUT,
            LaunchOptions.HyperVision.fromStored(null)),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--multi-core:on", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, false, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:off", "--multi-core:on", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, true, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:on", "--multi-core:on", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, false, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--multi-core:on", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.OFF),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--multi-core:on", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.AUTO),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--multi-core:on", "--hv:auto", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.FULL),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--multi-core:on", "--hv:full", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--multi-core:on", "--hv:high", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, true, LaunchOptions.HyperVision.MEDIUM),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--multi-core:on", "--hv:medium", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.LOW),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--multi-core:on", "--hv:low", "--control-layout:off", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.FULL,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false,
            true, false, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--multi-core:on",
            "--hv:full", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false,
            true, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--multi-core:off",
            "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
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
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--multi-core:on", "--control-layout:on", "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            false, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--multi-core:on", "--control-layout:on",
            "--pg:off", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, false, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:off", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, false, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:off", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, true, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on", "--owoot");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on", "--amigagrass");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH,
            false, false, false, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, true, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--multi-core:on", "--hv:high", "--control-layout:off",
            "--pg:off", "--lc:off", "--lcb:off", "--dws:on", "--cfg:on", "--amigagrass", "--owoot");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false, LaunchOptions.DEFAULT_SOUND,
            LaunchOptions.DEFAULT_SKIP_INTRO),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on", "/ns");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false, true, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--multi-core:on", "--control-layout:on",
            "--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on", "--nointro");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH,
            false, false, false, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, true, true, false, true),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--multi-core:on", "--hv:high", "--control-layout:off",
            "--pg:off", "--lc:off", "--lcb:off", "--dws:on", "--cfg:on", "--amigagrass", "--owoot", "/ns", "--nointro");
        requireArguments(new LaunchOptions(true, true, false, LaunchOptions.HyperVision.HIGH,
            false, false, false, true, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, true, true, true, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:on", "--fps:on", "--multi-core:on", "--hv:high", "--control-layout:off",
            "--pg:off", "--lc:off", "--lcb:off", "--dws:on", "--cfg:on", "--amigagrass", "--owoot");
        requireArguments(new LaunchOptions(false, false, true, LaunchOptions.HyperVision.OFF,
            true, true, true, false, LaunchOptions.DEFAULT_CHICANE_FAST_GRASS, false, false),
            "--data-dir", DATA_DIRECTORY, "--ogg:off", "--fps:off", "--multi-core:on", "--control-layout:on",
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
