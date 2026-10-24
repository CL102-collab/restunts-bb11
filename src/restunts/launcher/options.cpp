#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#ifdef __HAIKU__
#include <FindDirectory.h>
#include <Path.h>
#endif
#include "options.h"

static const legacy_char SETTINGS_FILENAME[] = "launcher.ini";
static const legacy_char SETTINGS_FOLDER_KEY[] = "folder";
static const legacy_char SETTINGS_HYPERVISION_KEY[] = "hypervision";
static const legacy_char DATA_DIRECTORY_ARGUMENT[] = "--data-dir";

const LauncherOptionDefinition launcher_option_definitions[LAUNCHER_OPTION_COUNT] = {
	{"sound", "Sound enabled", nullptr, "/ns", true},
	{"new_midi", "New MIDI", "--ogg:on", "--ogg:off", true},
	{"skip_intro", "Skip intro", "--nointro", nullptr, false},
	{"vibration", "Controller vibration", "--controller-vibration:on", "--controller-vibration:off",
	 true},
	{"fullscreen", "Fullscreen", "--fullscreen:on", "--fullscreen:off", false},
	{"show_fps", "Show FPS", "--fps:on", "--fps:off", false},
	{"power_gear", "Power Gear", "--pg:on", "--pg:off", true},
	{"legacy_collision", "Original Collision Bugs", "--lc:on", "--lc:off", true},
	{"left_corner_bias", "Left Corner Bias", "--lcb:on", "--lcb:off", true},
	{"dual_way_switching", "Dual-Way Switching", "--dws:on", "--dws:off", true},
	{"owoot", "OWOOT", "--owoot", nullptr, false},
	{"amiga_grass", "Amiga Grass", "/amigagrass", nullptr, false}};

const LauncherHyperVision launcher_hypervision_definitions[LAUNCHER_HV_COUNT] = {
	{"off", "Off", nullptr},
	{"auto", "Auto", "--hv:auto"},
	{"full", "Full", "--hv:full"},
	{"high", "High", "--hv:high"},
	{"medium", "Medium", "--hv:medium"},
	{"low", "Low", "--hv:low"}};

LauncherOptions::LauncherOptions() : hypervision(LAUNCHER_HV_AUTO)
{
	for (legacy_int index = 0; index < LAUNCHER_OPTION_COUNT; ++index) {
		enabled[index] = launcher_option_definitions[index].initial;
	}
}

wxString launcher_settings_path()
{
	wxFileName directory;
#if defined(__WXMSW__)
	directory = wxFileName::DirName(wxStandardPaths::Get().GetUserConfigDir());
	directory.AppendDir("Restunts");
#elif defined(__WXOSX__)
	directory = wxFileName::DirName(wxStandardPaths::Get().GetUserDataDir());
#elif defined(__HAIKU__)
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK) {
		return wxEmptyString;
	}
	directory = wxFileName::DirName(wxString::FromUTF8(path.Path()));
	directory.AppendDir("Restunts");
#else
	wxString base;
	if (!wxGetEnv("XDG_CONFIG_HOME", &base) || base.empty() ||
		!wxFileName::DirName(base).IsAbsolute()) {
		base = wxFileName(wxGetHomeDir(), ".config").GetFullPath();
	}
	directory = wxFileName::DirName(base);
	directory.AppendDir("restunts");
#endif
	return wxFileName(directory.GetPath(), SETTINGS_FILENAME).GetFullPath();
}

void launcher_read_options(const wxString &path, LauncherOptions &options)
{
	options = LauncherOptions();
	if (path.empty()) {
		return;
	}
	wxFileConfig config(wxEmptyString, wxEmptyString, path, wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
	config.DisableAutoSave();
	config.Read(SETTINGS_FOLDER_KEY, &options.folder);
	for (legacy_int index = 0; index < LAUNCHER_OPTION_COUNT; ++index) {
		options.enabled[index] = config.ReadBool(launcher_option_definitions[index].key,
												 launcher_option_definitions[index].initial);
	}
	wxString hypervision;
	config.Read(SETTINGS_HYPERVISION_KEY, &hypervision);
	for (legacy_int index = 0; index < LAUNCHER_HV_COUNT; ++index) {
		if (hypervision == launcher_hypervision_definitions[index].value) {
			options.hypervision = index;
			break;
		}
	}
}

wxString launcher_save_options(const wxString &path, const LauncherOptions &options)
{
	if (path.empty()) {
		return "Cannot locate your settings directory.";
	}
	wxFileName config_path(path);
	if (!wxFileName::DirExists(config_path.GetPath()) &&
		!wxFileName::Mkdir(config_path.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
		return "Cannot create your settings directory: " + config_path.GetPath();
	}
	wxFileConfig config(wxEmptyString, wxEmptyString, path, wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
	config.DisableAutoSave();
	legacy_bool written = config.Write(SETTINGS_FOLDER_KEY, options.folder);
	for (legacy_int index = 0; index < LAUNCHER_OPTION_COUNT; ++index) {
		written =
			config.Write(launcher_option_definitions[index].key, options.enabled[index]) && written;
	}
	const legacy_int hypervision =
		options.hypervision >= LAUNCHER_HV_OFF && options.hypervision < LAUNCHER_HV_COUNT
			? options.hypervision
			: LAUNCHER_HV_AUTO;
	written = config.Write(SETTINGS_HYPERVISION_KEY,
						   launcher_hypervision_definitions[hypervision].value) &&
			  written;
	if (!written || !config.Flush()) {
		return "Cannot save your settings: " + path;
	}
	return wxEmptyString;
}

std::vector<wxString> launcher_game_arguments(const wxString &game, const LauncherOptions &options)
{
	std::vector<wxString> arguments = {game, DATA_DIRECTORY_ARGUMENT, options.folder};
	for (legacy_int index = 0; index < LAUNCHER_OPTION_COUNT; ++index) {
		const LauncherOptionDefinition &definition = launcher_option_definitions[index];
		const legacy_char *argument =
			options.enabled[index] ? definition.enabled_argument : definition.disabled_argument;
		if (argument != nullptr) {
			arguments.push_back(argument);
		}
	}
	if (options.hypervision > LAUNCHER_HV_OFF && options.hypervision < LAUNCHER_HV_COUNT) {
		arguments.push_back(launcher_hypervision_definitions[options.hypervision].argument);
	}
	return arguments;
}
