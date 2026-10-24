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
	LauncherOptions options;
	options.folder = wxString::FromUTF8("/game data/árvíz & test/");
	const wxString game = "/program files/restunts";
	std::vector<wxString> arguments = launcher_game_arguments(game, options);
	check(arguments[GAME_ARGUMENT_INDEX] == game && arguments[DATA_OPTION_INDEX] == "--data-dir" &&
			  arguments[DATA_FOLDER_INDEX] == options.folder,
		  "Program and data paths must remain separate argv elements.");
	const legacy_char *defaults[] = {
		"--ogg:on", "--hv:auto", "--fullscreen:off", "--fps:off", "--controller-vibration:on",
		"--pg:on",	"--lc:on",	 "--lcb:on",		 "--dws:on"};
	for (const legacy_char *argument : defaults) {
		check(contains(arguments, argument), "Missing default game argument.");
	}
	check(!contains(arguments, "/ns") && !contains(arguments, "--nointro") &&
			  !contains(arguments, "--owoot") && !contains(arguments, "/amigagrass"),
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
									"--pg:off",
									"--lc:off",
									"--lcb:off",
									"--dws:off",
									"--owoot",
									"/amigagrass"};
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
		config.Flush();
	}
	launcher_read_options(settings, restored);
	check(restored.hypervision == LAUNCHER_HV_AUTO,
		  "An unknown stored HyperVision value must use the default.");
	wxRemoveFile(settings);
	launcher_read_options(settings, restored);
	check(restored.folder.empty() && restored.enabled[LAUNCHER_NEW_MIDI] &&
			  !restored.enabled[LAUNCHER_FULLSCREEN] && restored.hypervision == LAUNCHER_HV_AUTO,
		  "A missing configuration must restore current defaults, not previous user state.");
	check(!wxFileExists(settings), "Reading a missing configuration must not create a file.");
	return failures == 0 ? TEST_SUCCESS : TEST_FAILURE;
}
