#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/ffile.h>
#include <wx/init.h>
#include <algorithm>
#include <cstdio>
#include "options.h"

static const legacy_int TEST_SUCCESS = 0;
static const legacy_int TEST_FAILURE = 1;
static const legacy_int GAME_ARGUMENT_INDEX = 0;
static const legacy_int DATA_OPTION_INDEX = 1;
static const legacy_int DATA_FOLDER_INDEX = 2;

static legacy_int failures;

static void check(legacy_bool condition, const legacy_char *message)
{
	if (!condition) {
		std::fprintf(stderr, "%s\n", message);
		++failures;
	}
}

static legacy_bool contains(const std::vector<wxString> &arguments, const wxString &argument)
{
	return std::find(arguments.begin(), arguments.end(), argument) != arguments.end();
}

#ifdef RESTUNTS_GRAPHICS_LAUNCHER
static void check_graphics_options()
{
	LauncherOptions options;
	options.folder = "/game";
	check(options.renderer == GRAPHICS_RENDERER_SOFTWARE &&
			  options.device == GRAPHICS_DEVICE_DEFAULT,
		  "Renderer must default to Software with automatic device selection.");
	check(contains(launcher_game_arguments("game", options), "--renderer:software"),
		  "Software selection must reach the game explicitly.");
	std::vector<LauncherGraphicsDevice> devices;
	check(launcher_parse_graphics_devices(GRAPHICS_DEVICE_REPORT_HEADER, devices) &&
			  devices.empty(),
		  "A successful inventory without compatible devices must be accepted.");
	const wxString inventory =
		wxString::FromUTF8(GRAPHICS_DEVICE_REPORT_HEADER) + "0\tIntegrated GPU\n7\tDiscrete GPU\n";
	check(launcher_parse_graphics_devices(inventory, devices) && devices.size() == 2U &&
			  devices[0].index == 0 && devices[1].index == 7 && devices[1].name == "Discrete GPU",
		  "Device inventory must preserve compatible adapter names and physical indices.");
	wxString windows_inventory = inventory;
	windows_inventory.Replace("\n", "\r\n");
	check(launcher_parse_graphics_devices(windows_inventory, devices),
		  "Windows console line endings must be accepted.");
	options.renderer = GRAPHICS_RENDERER_VULKAN;
	options.device = 7;
	launcher_normalize_graphics_options(options, devices);
	std::vector<wxString> arguments = launcher_game_arguments("game", options);
	check(contains(arguments, "--renderer:vulkan") && contains(arguments, "--renderer-device:7") &&
			  contains(arguments, "--multi-core:off") && !contains(arguments, "--multi-core:on"),
		  "Vulkan must select the physical device and disable effective CPU rendering workers.");
	check(options.enabled[LAUNCHER_MULTI_CORE],
		  "Selecting Vulkan must preserve the user's Software multi-core preference.");
	const wxString settings = wxFileName::CreateTempFileName("restunts-renderer-options-");
	check(launcher_save_options(settings, options).empty(), "Could not save renderer preferences.");
	LauncherOptions restored;
	launcher_read_options(settings, restored);
	check(restored.renderer == GRAPHICS_RENDERER_VULKAN && restored.device == 7 &&
			  restored.enabled[LAUNCHER_MULTI_CORE],
		  "Renderer, device and retained CPU worker preference must round-trip.");
	launcher_normalize_graphics_options(restored, {});
	check(restored.renderer == GRAPHICS_RENDERER_SOFTWARE &&
			  restored.device == GRAPHICS_DEVICE_DEFAULT && restored.enabled[LAUNCHER_MULTI_CORE],
		  "Unavailable Vulkan hardware must normalize to Software without losing CPU preferences.");
	arguments = launcher_game_arguments("game", restored);
	check(contains(arguments, "--renderer:software") && contains(arguments, "--multi-core:on") &&
			  !contains(arguments, "--renderer-device:7"),
		  "Software must not pass an incompatible Vulkan device argument.");
	launcher_normalize_graphics_options(options, {{4, "Replacement GPU"}});
	check(options.renderer == GRAPHICS_RENDERER_VULKAN && options.device == 4,
		  "A missing preferred device must select an available compatible adapter.");
	{
		wxFileConfig config(wxEmptyString, wxEmptyString, settings, wxEmptyString,
							wxCONFIG_USE_LOCAL_FILE);
		config.DisableAutoSave();
		config.Write("renderer", "unknown-renderer");
		config.Write("renderer_device", "2147483648");
		config.Flush();
	}
	launcher_read_options(settings, restored);
	check(restored.renderer == GRAPHICS_RENDERER_SOFTWARE &&
			  restored.device == GRAPHICS_DEVICE_DEFAULT,
		  "Unknown renderer or overflowing saved device must use safe defaults.");
	wxRemoveFile(settings);
	const legacy_char *invalid[] = {
		"RESTUNTS_RENDER_DEVICES 2\n0\tGPU\n",	"RESTUNTS_RENDER_DEVICES 1\n0\tGPU\n0\tDuplicate\n",
		"RESTUNTS_RENDER_DEVICES 1\n-1\tGPU\n", "RESTUNTS_RENDER_DEVICES 1\n2147483648\tGPU\n",
		"RESTUNTS_RENDER_DEVICES 1\n0\t\n",		"RESTUNTS_RENDER_DEVICES 1\n0\tGPU\tinvalid\n",
	};
	for (const legacy_char *report : invalid) {
		check(!launcher_parse_graphics_devices(report, devices) && devices.empty(),
			  "Malformed or ambiguous device inventories must fail closed.");
	}
	wxString excessive = wxString::FromUTF8(GRAPHICS_DEVICE_REPORT_HEADER);
	for (legacy_u32 index = 0; index <= GRAPHICS_DEVICE_CAPACITY; ++index) {
		excessive += wxString::Format("%" LEGACY_PRIu32 "\tGPU\n", index);
	}
	check(!launcher_parse_graphics_devices(excessive, devices) && devices.empty(),
		  "Device inventories must respect their bounded adapter capacity.");
}
#else
static void check_graphics_options()
{
	LauncherOptions options;
	options.renderer = GRAPHICS_RENDERER_VULKAN;
	options.device = 7;
	for (const wxString &argument : launcher_game_arguments("game", options)) {
		check(!argument.StartsWith("--renderer:") && !argument.StartsWith("--renderer-device:"),
			  "Unsupported launcher targets must not pass renderer selection arguments.");
	}
}
#endif

legacy_int main()
{
	wxInitializer initialize;
	if (!initialize.IsOk()) {
		return TEST_FAILURE;
	}
	check_graphics_options();
	check(LAUNCHER_CHICANE_FAST_GRASS + 1 == LAUNCHER_AMIGA_GRASS &&
			  LAUNCHER_AMIGA_GRASS + 1 == LAUNCHER_OWOOT &&
			  LAUNCHER_OWOOT + 1 == LAUNCHER_OPTION_COUNT,
		  "Last driving rules must be Chicane Fast-Grass, Amiga Grass, OWOOT.");
	check(wxString(launcher_option_definitions[LAUNCHER_CHICANE_FAST_GRASS].label) ==
			  "Chicane Fast-Grass",
		  "The Chicane Fast-Grass checkbox must use its public label.");
	LauncherOptions options;
	options.folder = wxString::FromUTF8("/game data/árvíz & test/");
	const wxString game = "/program files/restunts";
	std::vector<wxString> arguments = launcher_game_arguments(game, options);
	check(arguments[GAME_ARGUMENT_INDEX] == game && arguments[DATA_OPTION_INDEX] == "--data-dir" &&
			  arguments[DATA_FOLDER_INDEX] == options.folder,
		  "Program and data paths must remain separate argv elements.");
	const legacy_char *defaults[] = {"--ogg:on",
									 "--hv:auto",
									 "--fullscreen:off",
									 "--fps:off",
									 "--controller-vibration:on",
									 "--multi-core:on",
									 "--pg:on",
									 "--lc:on",
									 "--lcb:on",
									 "--dws:on",
									 "--cfg:on"};
	for (const legacy_char *argument : defaults) {
		check(contains(arguments, argument), "Missing default game argument.");
	}
	check(!contains(arguments, "/ns") && !contains(arguments, "--nointro") &&
			  !contains(arguments, "--owoot") && !contains(arguments, "--amigagrass"),
		  "Optional game switches must not be enabled by default.");
	for (legacy_int index = 0; index < LAUNCHER_OPTION_COUNT; ++index) {
		options.enabled[index] = !options.enabled[index];
	}
	options.hypervision = LAUNCHER_HV_OFF;
	arguments = launcher_game_arguments(game, options);
	const legacy_char *changed[] = {"/ns",
									"--ogg:off",
									"--nointro",
									"--controller-vibration:off",
									"--fullscreen:on",
									"--fps:on",
									"--multi-core:off",
									"--pg:off",
									"--lc:off",
									"--lcb:off",
									"--dws:off",
									"--cfg:off",
									"--owoot",
									"--amigagrass"};
	for (const legacy_char *argument : changed) {
		check(contains(arguments, argument), "Missing changed game argument.");
	}
	for (const wxString &argument : arguments) {
		check(!argument.StartsWith("--hv:"), "HyperVision Off must omit the startup preset.");
	}
	const legacy_char *presets[] = {"--hv:auto", "--hv:full", "--hv:high", "--hv:medium",
									"--hv:low"};
	for (legacy_int index = LAUNCHER_HV_AUTO; index < LAUNCHER_HV_COUNT; ++index) {
		options.hypervision = index;
		check(contains(launcher_game_arguments(game, options), presets[index - LAUNCHER_HV_AUTO]),
			  "HyperVision selection must reach the game unchanged.");
	}

	const wxString settings = wxFileName::CreateTempFileName("restunts-launcher-options-");
	check(!settings.empty(), "Cannot create temporary settings file.");
	check(launcher_save_options(settings, options).empty(), "Could not save per-user options.");
	LauncherOptions restored;
	launcher_read_options(settings, restored);
	check(restored.folder == options.folder && restored.hypervision == options.hypervision,
		  "Folder and HyperVision must round-trip through configuration.");
	for (legacy_int index = 0; index < LAUNCHER_OPTION_COUNT; ++index) {
		check(restored.enabled[index] == options.enabled[index],
			  "Checkbox did not round-trip through configuration.");
	}
	wxString original;
	{
		wxFFile file(settings, "rb");
		file.ReadAll(&original);
	}
	launcher_read_options(settings, restored);
	{
		wxFFile file(settings, "rb");
		wxString after_read;
		file.ReadAll(&after_read);
		check(after_read == original, "Reading settings must not rewrite them or save defaults.");
	}
	{
		wxFileConfig config(wxEmptyString, wxEmptyString, settings, wxEmptyString,
							wxCONFIG_USE_LOCAL_FILE);
		config.DisableAutoSave();
		config.Write("hypervision", "unknown-future-preset");
		config.DeleteEntry(launcher_option_definitions[LAUNCHER_CHICANE_FAST_GRASS].key);
		config.DeleteEntry(launcher_option_definitions[LAUNCHER_MULTI_CORE].key);
		config.Flush();
	}
	launcher_read_options(settings, restored);
	check(
		restored.enabled[LAUNCHER_CHICANE_FAST_GRASS] &&
			restored.enabled[LAUNCHER_AMIGA_GRASS] == options.enabled[LAUNCHER_AMIGA_GRASS] &&
			restored.enabled[LAUNCHER_OWOOT] == options.enabled[LAUNCHER_OWOOT],
		"Existing configurations must enable Chicane Fast-Grass and preserve other driving rules.");
	check(restored.enabled[LAUNCHER_MULTI_CORE],
		  "Configurations without a saved multi-core setting must use the enabled default.");
	check(restored.hypervision == LAUNCHER_HV_AUTO,
		  "An unknown stored HyperVision value must use the default.");
	wxRemoveFile(settings);
	launcher_read_options(settings, restored);
	check(restored.folder.empty() && restored.enabled[LAUNCHER_NEW_MIDI] &&
			  !restored.enabled[LAUNCHER_FULLSCREEN] && restored.enabled[LAUNCHER_MULTI_CORE] &&
			  restored.enabled[LAUNCHER_CHICANE_FAST_GRASS] &&
			  restored.hypervision == LAUNCHER_HV_AUTO,
		  "A missing configuration must restore current defaults, not previous user state.");
	check(!wxFileExists(settings), "Reading a missing configuration must not create a file.");
	return failures == 0 ? TEST_SUCCESS : TEST_FAILURE;
}
