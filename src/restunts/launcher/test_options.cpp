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

legacy_int main()
{
	wxInitializer initialize;
	if (!initialize.IsOk()) {
		return TEST_FAILURE;
	}
	check(LAUNCHER_CHICANE_FAST_GRASS + 1 == LAUNCHER_AMIGA_GRASS &&
			  LAUNCHER_AMIGA_GRASS + 1 == LAUNCHER_OWOOT &&
			  LAUNCHER_OWOOT + 1 == LAUNCHER_OPTION_COUNT,
		  "Last driving rules must be Chicane Fast-Grass, Amiga Grass, OWOOT.");
	check(wxString(launcher_option_definitions[LAUNCHER_CHICANE_FAST_GRASS].label) ==
			  "Chicane Fast-Grass",
		  "The Chicane Fast-Grass checkbox must use its public label.");
	check(LAUNCHER_SHOW_FPS + 1 == LAUNCHER_SHOW_MPH &&
			  wxString(launcher_option_definitions[LAUNCHER_SHOW_MPH].label) ==
				  "Show MPH (Ctrl+F9)",
		  "Show MPH must follow Show FPS and name its in-game shortcut.");
	check(LAUNCHER_SOUND_NONE + 1 == LAUNCHER_SOUND_PC_SPEAKER &&
			  LAUNCHER_SOUND_PC_SPEAKER + 1 == LAUNCHER_SOUND_ADLIB &&
			  LAUNCHER_SOUND_ADLIB + 1 == LAUNCHER_SOUND_MT32 &&
			  LAUNCHER_SOUND_MT32 + 1 == LAUNCHER_SOUND_MODE_COUNT,
		  "Sound modes must appear in the order No sound, PC Speaker, Ad Lib, MT-32.");
	check(wxString(launcher_sound_definitions[LAUNCHER_SOUND_PC_SPEAKER].label) == "PC Speaker",
		  "The PC Speaker choice must use its public label.");
	LauncherOptions options;
	check(!options.enabled[LAUNCHER_SHOW_MPH], "Show MPH must default to disabled.");
	check(options.sound_mode == LAUNCHER_SOUND_ADLIB, "Sound must default to Ad Lib.");
	check(options.video_mode == LAUNCHER_VIDEO_VGA, "Video mode must default to VGA.");
	check(wxString(launcher_option_definitions[LAUNCHER_NEW_MIDI].label) ==
			  "OGG music replacement (Shift+F10)",
		  "The replacement music control must use its descriptive public label.");
	options.folder = wxString::FromUTF8("/game data/árvíz & test/");
	const wxString game = "/program files/restunts";
	std::vector<wxString> arguments = launcher_game_arguments(game, options);
	check(arguments[GAME_ARGUMENT_INDEX] == game && arguments[DATA_OPTION_INDEX] == "--data-dir" &&
			  arguments[DATA_FOLDER_INDEX] == options.folder,
		  "Program and data paths must remain separate argv elements.");
	const legacy_char *defaults[] = {
		"--soundmode:adlib", "--videomode:vga", "--ogg:off",	 "--hv:auto",
		"--fullscreen:off",	 "--fps:off",		"--showmph:off", "--controller-vibration:on",
		"--multi-core:on",	 "--pg:on",			"--lc:on",		 "--lcb:on",
		"--dws:on",			 "--cfg:on"};
	for (const legacy_char *argument : defaults) {
		check(contains(arguments, argument), "Missing default game argument.");
	}
	check(!contains(arguments, "--nosound") && !contains(arguments, "/ns") &&
			  !contains(arguments, "--nointro") && !contains(arguments, "--owoot") &&
			  !contains(arguments, "--amigagrass"),
		  "Optional game switches must not be enabled by default.");
	for (legacy_int index = 0; index < LAUNCHER_OPTION_COUNT; ++index) {
		options.enabled[index] = !options.enabled[index];
	}
	options.sound_mode = LAUNCHER_SOUND_NONE;
	options.hypervision = LAUNCHER_HV_OFF;
	arguments = launcher_game_arguments(game, options);
	const legacy_char *changed[] = {
		"--nosound",	   "--ogg:on", "--nointro",	   "--controller-vibration:off",
		"--fullscreen:on", "--fps:on", "--showmph:on", "--multi-core:off",
		"--pg:off",		   "--lc:off", "--lcb:off",	   "--dws:off",
		"--cfg:off",	   "--owoot",  "--amigagrass"};
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

	const legacy_char *video_modes[] = {"--videomode:vga", "--videomode:ega", "--videomode:cga"};
	for (legacy_int video_mode = LAUNCHER_VIDEO_VGA; video_mode < LAUNCHER_VIDEO_MODE_COUNT;
		 video_mode++) {
		options.video_mode = video_mode;
		for (legacy_int hypervision = LAUNCHER_HV_OFF; hypervision < LAUNCHER_HV_COUNT;
			 hypervision++) {
			options.hypervision = hypervision;
			arguments = launcher_game_arguments(game, options);
			legacy_int video_arguments = 0;
			for (const wxString &argument : arguments) {
				video_arguments += argument.StartsWith("--videomode:");
			}
			check(video_arguments == 1 && contains(arguments, video_modes[video_mode]),
				  "Each video mode must reach the game independently of HyperVision.");
			if (hypervision > LAUNCHER_HV_OFF) {
				check(contains(arguments, presets[hypervision - LAUNCHER_HV_AUTO]),
					  "Selecting EGA or CGA must preserve the HyperVision preset.");
			}
		}
	}
	const legacy_int invalid_video_modes[] = {-1, LAUNCHER_VIDEO_MODE_COUNT};
	for (legacy_int invalid : invalid_video_modes) {
		options.video_mode = invalid;
		check(contains(launcher_game_arguments(game, options), "--videomode:vga"),
			  "An invalid video mode must safely produce the VGA argument.");
	}
	options.video_mode = LAUNCHER_VIDEO_EGA;

	options.sound_mode = LAUNCHER_SOUND_MT32;
	options.mt32_rom_folder = wxString::FromUTF8("/ROM folders/árvíz & test/");
	options.mt32_model = "mt32_1_07";
	arguments = launcher_game_arguments(game, options);
	const auto rom_argument = std::find(arguments.begin(), arguments.end(), "--mt-32-rom-dir");
	const auto model_argument = std::find(arguments.begin(), arguments.end(), "--mt-32-model");
	check(contains(arguments, "--soundmode:mt-32") && !contains(arguments, "--nosound") &&
			  !contains(arguments, "--soundmode:adlib"),
		  "MT-32 must select exactly its sound mode.");
	check(rom_argument != arguments.end() && rom_argument + 1 != arguments.end() &&
			  *(rom_argument + 1) == options.mt32_rom_folder && model_argument != arguments.end() &&
			  model_argument + 1 != arguments.end() && *(model_argument + 1) == options.mt32_model,
		  "ROM folder and model must be passed as separate arguments without shell quoting.");
	options.sound_mode = LAUNCHER_SOUND_NONE;
	arguments = launcher_game_arguments(game, options);
	check(contains(arguments, "--nosound") && !contains(arguments, "--mt-32-rom-dir") &&
			  !contains(arguments, "--mt-32-model") && !contains(arguments, "--soundmode:mt-32"),
		  "No sound must not require or pass ROM arguments.");
	options.sound_mode = LAUNCHER_SOUND_PC_SPEAKER;
	options.enabled[LAUNCHER_NEW_MIDI] = false;
	arguments = launcher_game_arguments(game, options);
	check(contains(arguments, "--soundmode:pc-speaker") && !contains(arguments, "--nosound") &&
			  !contains(arguments, "--soundmode:adlib") &&
			  !contains(arguments, "--soundmode:mt-32") &&
			  !contains(arguments, "--mt-32-rom-dir") && !contains(arguments, "--mt-32-model") &&
			  contains(arguments, "--ogg:off"),
		  "PC Speaker must select its mode without ROM arguments or enabling replacement music.");
	options.enabled[LAUNCHER_NEW_MIDI] = true;
	check(contains(launcher_game_arguments(game, options), "--ogg:on"),
		  "PC Speaker must retain an independently enabled replacement music setting.");
	options.sound_mode = LAUNCHER_SOUND_MODE_COUNT;
	check(contains(launcher_game_arguments(game, options), "--soundmode:adlib"),
		  "An invalid stored sound mode must safely use Ad Lib.");
	options.sound_mode = LAUNCHER_SOUND_MT32;

	std::vector<LauncherRomSet> sets;
	wxString inventory_error;
	check(launcher_parse_rom_inventory(
			  "{\"sets\":[{\"id\":\"mt32_1_07\",\"label\":\"MT-32 (old, v1.07)\"},"
			  "{\"label\":\"MT-32 (new, v2.07)\",\"id\":\"mt32_2_07\"}],\"error\":\"\"}",
			  sets, inventory_error) &&
			  sets.size() == 2U && sets[0].id == "mt32_1_07" &&
			  sets[1].label == "MT-32 (new, v2.07)" && inventory_error.empty(),
		  "The ROM dropdown must show all complete sets returned by the game.");
	check(
		launcher_parse_rom_inventory(
			"{\"metadata\":{\"nested\":[true,null,12]},\"error\":\"Missing PCM ROM\",\"sets\":[]}",
			sets, inventory_error) &&
			sets.empty() && inventory_error == "Missing PCM ROM",
		"An incomplete ROM inventory must retain the game's actionable explanation.");
	check(launcher_parse_rom_inventory(
			  "{\"sets\":[{\"id\":\"test\",\"label\":\"Quote \\\" and \\u00e1\"}],\"error\":\"\"}",
			  sets, inventory_error) &&
			  sets.size() == 1U && sets[0].label == wxString::FromUTF8("Quote \" and á"),
		  "ROM inventory strings must decode escaped labels and Unicode.");
	const legacy_char *invalid_inventories[] = {
		"{}",
		"{\"sets\":[]",
		"{\"sets\":[{\"id\":\"test\"}]}",
		"{\"sets\":[{\"id\":\"same\",\"label\":\"One\"},{\"id\":\"same\",\"label\":\"Two\"}]}",
		"{\"sets\":[{\"id\":\"bad\",\"label\":\"\\uD800\"}]}",
		"{\"sets\":[]} trailing"};
	for (const legacy_char *invalid : invalid_inventories) {
		check(!launcher_parse_rom_inventory(invalid, sets, inventory_error) && sets.empty() &&
				  !inventory_error.empty(),
			  "Malformed inventory must not leave selectable partial results.");
	}

	check(!launcher_parse_rom_inventory(wxString(' ', 65537U), sets, inventory_error) &&
			  sets.empty() && !inventory_error.empty(),
		  "An oversized inventory must be rejected before parsing or allocating ROM choices.");

	const wxString settings = wxFileName::CreateTempFileName("restunts-launcher-options-");
	check(!settings.empty(), "Cannot create temporary settings file.");
	check(launcher_save_options(settings, options).empty(), "Could not save per-user options.");
	LauncherOptions restored;
	launcher_read_options(settings, restored);
	check(restored.folder == options.folder && restored.hypervision == options.hypervision &&
			  restored.video_mode == options.video_mode,
		  "Folder, HyperVision and video mode must round-trip through configuration.");
	for (legacy_int mode = LAUNCHER_VIDEO_VGA; mode < LAUNCHER_VIDEO_MODE_COUNT; mode++) {
		LauncherOptions video_options = options;
		video_options.video_mode = mode;
		check(launcher_save_options(settings, video_options).empty(),
			  "Could not save video mode options.");
		launcher_read_options(settings, restored);
		check(restored.video_mode == mode && restored.hypervision == options.hypervision,
			  "All video modes must round-trip independently of HyperVision.");
	}
	LauncherOptions invalid_video_options = options;
	invalid_video_options.video_mode = LAUNCHER_VIDEO_MODE_COUNT;
	check(launcher_save_options(settings, invalid_video_options).empty(),
		  "Could not save options with an invalid video mode.");
	launcher_read_options(settings, restored);
	check(restored.video_mode == LAUNCHER_VIDEO_VGA,
		  "Invalid video mode indices must be saved as the VGA default.");
	check(restored.sound_mode == LAUNCHER_SOUND_MT32 &&
			  restored.mt32_rom_folder == options.mt32_rom_folder &&
			  restored.mt32_model == options.mt32_model,
		  "Sound mode, ROM folder, and chosen ROM set must round-trip through configuration.");
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
	LauncherOptions pc_options = options;
	pc_options.sound_mode = LAUNCHER_SOUND_PC_SPEAKER;
	check(launcher_save_options(settings, pc_options).empty(),
		  "Could not save PC Speaker options.");
	launcher_read_options(settings, restored);
	check(restored.sound_mode == LAUNCHER_SOUND_PC_SPEAKER &&
			  restored.enabled[LAUNCHER_NEW_MIDI] == pc_options.enabled[LAUNCHER_NEW_MIDI],
		  "PC Speaker must round-trip without changing replacement music.");
	{
		wxFileConfig config(wxEmptyString, wxEmptyString, settings, wxEmptyString,
							wxCONFIG_USE_LOCAL_FILE);
		config.DisableAutoSave();
		wxString saved_mode;
		config.Read("sound_mode", &saved_mode);
		check(saved_mode == "pc-speaker", "PC Speaker must persist as the stable pc-speaker ID.");
		config.Write("hypervision", "unknown-future-preset");
		config.Write("video_mode", "unknown-future-mode");
		config.DeleteEntry("sound_mode");
		config.Write("sound", false);
		config.DeleteEntry(launcher_option_definitions[LAUNCHER_CHICANE_FAST_GRASS].key);
		config.DeleteEntry(launcher_option_definitions[LAUNCHER_MULTI_CORE].key);
		config.DeleteEntry(launcher_option_definitions[LAUNCHER_SHOW_MPH].key);
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
	check(!restored.enabled[LAUNCHER_SHOW_MPH],
		  "Configurations without a saved Show MPH setting must use the disabled default.");
	check(restored.video_mode == LAUNCHER_VIDEO_VGA,
		  "An unknown stored video mode must use the VGA default.");
	check(restored.hypervision == LAUNCHER_HV_AUTO,
		  "An unknown stored HyperVision value must use the default.");
	check(restored.sound_mode == LAUNCHER_SOUND_NONE &&
			  restored.enabled[LAUNCHER_NEW_MIDI] == options.enabled[LAUNCHER_NEW_MIDI],
		  "The legacy disabled sound checkbox must migrate without changing replacement music.");
	struct SavedSoundCase {
		const legacy_char *id;
		legacy_int expected_mode;
	};
	const SavedSoundCase saved_sound_modes[] = {{"none", LAUNCHER_SOUND_NONE},
												{"adlib", LAUNCHER_SOUND_ADLIB},
												{"mt-32", LAUNCHER_SOUND_MT32},
												{"unknown-future-mode", LAUNCHER_SOUND_ADLIB}};
	for (const SavedSoundCase &test : saved_sound_modes) {
		{
			wxFileConfig config(wxEmptyString, wxEmptyString, settings, wxEmptyString,
								wxCONFIG_USE_LOCAL_FILE);
			config.DisableAutoSave();
			config.Write("sound_mode", test.id);
			config.Flush();
		}
		launcher_read_options(settings, restored);
		check(restored.sound_mode == test.expected_mode,
			  "Existing sound IDs must keep their meaning and unknown IDs must default to Ad Lib.");
	}
	{
		wxFileConfig config(wxEmptyString, wxEmptyString, settings, wxEmptyString,
							wxCONFIG_USE_LOCAL_FILE);
		config.DisableAutoSave();
		config.DeleteEntry("sound_mode");
		config.Write("sound", true);
		config.DeleteEntry("video_mode");
		config.Flush();
	}
	launcher_read_options(settings, restored);
	check(restored.video_mode == LAUNCHER_VIDEO_VGA,
		  "Configurations without a video mode must use the VGA default.");
	check(restored.sound_mode == LAUNCHER_SOUND_ADLIB,
		  "The legacy enabled sound checkbox must retain the Ad Lib default.");
	wxRemoveFile(settings);
	launcher_read_options(settings, restored);
	check(restored.folder.empty() && restored.mt32_rom_folder.empty() &&
			  restored.mt32_model.empty() && restored.sound_mode == LAUNCHER_SOUND_ADLIB &&
			  !restored.enabled[LAUNCHER_NEW_MIDI] && !restored.enabled[LAUNCHER_FULLSCREEN] &&
			  !restored.enabled[LAUNCHER_SHOW_MPH] && restored.enabled[LAUNCHER_MULTI_CORE] &&
			  restored.enabled[LAUNCHER_CHICANE_FAST_GRASS] &&
			  restored.hypervision == LAUNCHER_HV_AUTO && restored.video_mode == LAUNCHER_VIDEO_VGA,
		  "A missing configuration must restore current defaults, not previous user state.");
	check(!wxFileExists(settings), "Reading a missing configuration must not create a file.");
	return failures == 0 ? TEST_SUCCESS : TEST_FAILURE;
}
