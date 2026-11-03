#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#ifdef RESTUNTS_GRAPHICS_LAUNCHER
#include <wx/tokenzr.h>
#endif
#ifdef __HAIKU__
#include <FindDirectory.h>
#include <Path.h>
#endif
#include "options.h"

static const legacy_char SETTINGS_FILENAME[] = "launcher.ini";
static const legacy_char SETTINGS_FOLDER_KEY[] = "folder";
static const legacy_char SETTINGS_HYPERVISION_KEY[] = "hypervision";
#ifdef RESTUNTS_GRAPHICS_LAUNCHER
static const legacy_char SETTINGS_RENDERER_KEY[] = "renderer";
static const legacy_char SETTINGS_DEVICE_KEY[] = "renderer_device";
static const legacy_char SOFTWARE_RENDERER_VALUE[] = "software";
static const legacy_char NATIVE_RENDERER_VALUE[] = GRAPHICS_RENDERER_NATIVE_VALUE;
static const legacy_char SOFTWARE_RENDERER_ARGUMENT[] = "--renderer:software";
static const legacy_char NATIVE_RENDERER_ARGUMENT[] = "--renderer:" GRAPHICS_RENDERER_NATIVE_VALUE;
#endif
static const legacy_char DATA_DIRECTORY_ARGUMENT[] = "--data-dir";

const LauncherOptionDefinition launcher_option_definitions[LAUNCHER_OPTION_COUNT] = {
	{"sound", "Sound enabled", nullptr, "/ns", true},
	{"new_midi", "New MIDI", "--ogg:on", "--ogg:off", true},
	{"skip_intro", "Skip intro", "--nointro", nullptr, false},
	{"vibration", "Controller vibration", "--controller-vibration:on", "--controller-vibration:off",
	 true},
	{"fullscreen", "Fullscreen", "--fullscreen:on", "--fullscreen:off", false},
	{"show_fps", "Show FPS", "--fps:on", "--fps:off", false},
	{"multi_core", "Multi-core rendering", "--multi-core:on", "--multi-core:off", true},
	{"power_gear", "Power Gear", "--pg:on", "--pg:off", true},
	{"legacy_collision", "Original Collision Bugs", "--lc:on", "--lc:off", true},
	{"left_corner_bias", "Left Corner Bias", "--lcb:on", "--lcb:off", true},
	{"dual_way_switching", "Dual-Way Switching", "--dws:on", "--dws:off", true},
	{"chicane_fast_grass", "Chicane Fast-Grass", "--cfg:on", "--cfg:off", true},
	{"amiga_grass", "Amiga Grass", "--amigagrass", nullptr, false},
	{"owoot", "OWOOT", "--owoot", nullptr, false}};

const LauncherHyperVision launcher_hypervision_definitions[LAUNCHER_HV_COUNT] = {
	{"off", "Off", nullptr},
	{"auto", "Auto", "--hv:auto"},
	{"full", "Full", "--hv:full"},
	{"high", "High", "--hv:high"},
	{"medium", "Medium", "--hv:medium"},
	{"low", "Low", "--hv:low"}};

LauncherOptions::LauncherOptions()
	: hypervision(LAUNCHER_HV_AUTO), renderer(GRAPHICS_RENDERER_SOFTWARE),
	  device(GRAPHICS_DEVICE_DEFAULT)
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
#ifdef RESTUNTS_GRAPHICS_LAUNCHER
	wxString renderer;
	config.Read(SETTINGS_RENDERER_KEY, &renderer);
	if (renderer == NATIVE_RENDERER_VALUE) {
		options.renderer = GRAPHICS_RENDERER_NATIVE;
	}
	wxString device;
	config.Read(SETTINGS_DEVICE_KEY, &device);
	legacy_s64 index;
	if (device.ToLongLong(&index) && index >= GRAPHICS_DEVICE_DEFAULT &&
		index <= static_cast<legacy_s64>(LEGACY_S32_MAX)) {
		options.device = static_cast<legacy_s32>(index);
	}
#endif
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
#ifdef RESTUNTS_GRAPHICS_LAUNCHER
	written = config.Write(SETTINGS_RENDERER_KEY, options.renderer == GRAPHICS_RENDERER_NATIVE
													  ? NATIVE_RENDERER_VALUE
													  : SOFTWARE_RENDERER_VALUE) &&
			  written;
	const legacy_s32 device =
		options.device >= GRAPHICS_DEVICE_DEFAULT ? options.device : GRAPHICS_DEVICE_DEFAULT;
	written =
		config.Write(SETTINGS_DEVICE_KEY, wxString::Format("%" LEGACY_PRId32, device)) && written;
#endif
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
		legacy_bool enabled = options.enabled[index];
#ifdef RESTUNTS_GRAPHICS_LAUNCHER
		if (index == LAUNCHER_MULTI_CORE && options.renderer == GRAPHICS_RENDERER_NATIVE) {
			enabled = false;
		}
#endif
		const legacy_char *argument =
			enabled ? definition.enabled_argument : definition.disabled_argument;
		if (argument != nullptr) {
			arguments.push_back(argument);
		}
	}
	if (options.hypervision > LAUNCHER_HV_OFF && options.hypervision < LAUNCHER_HV_COUNT) {
		arguments.push_back(launcher_hypervision_definitions[options.hypervision].argument);
	}
#ifdef RESTUNTS_GRAPHICS_LAUNCHER
	arguments.push_back(options.renderer == GRAPHICS_RENDERER_NATIVE ? NATIVE_RENDERER_ARGUMENT
																	 : SOFTWARE_RENDERER_ARGUMENT);
	if (options.renderer == GRAPHICS_RENDERER_NATIVE && options.device >= 0) {
		arguments.push_back(wxString::Format("--renderer-device:%" LEGACY_PRId32, options.device));
	}
#endif
	return arguments;
}

#ifdef RESTUNTS_GRAPHICS_LAUNCHER
legacy_bool launcher_parse_graphics_devices(const wxString &report,
											std::vector<LauncherGraphicsDevice> &devices)
{
	devices.clear();
	wxString normalized = report;
	normalized.Replace("\r\n", "\n");
	const wxString header = wxString::FromUTF8(GRAPHICS_DEVICE_REPORT_HEADER);
	if (!normalized.StartsWith(header)) {
		return false;
	}
	wxStringTokenizer lines(normalized.Mid(header.length()), "\n", wxTOKEN_RET_EMPTY_ALL);
	while (lines.HasMoreTokens()) {
		const wxString line = lines.GetNextToken();
		if (line.empty() && !lines.HasMoreTokens()) {
			continue;
		}
		const size_t separator = line.find('\t');
		if (separator == wxString::npos || separator == 0 || separator + 1 >= line.length() ||
			devices.size() >= GRAPHICS_DEVICE_CAPACITY) {
			devices.clear();
			return false;
		}
		const wxString index_text = line.Left(separator);
		for (const wxUniChar character : index_text) {
			if (character < '0' || character > '9') {
				devices.clear();
				return false;
			}
		}
		legacy_s64 index;
		if (!index_text.ToLongLong(&index) || index > static_cast<legacy_s64>(LEGACY_S32_MAX)) {
			devices.clear();
			return false;
		}
		const wxString name = line.Mid(separator + 1);
		if (name.length() >= GRAPHICS_DEVICE_NAME_CAPACITY) {
			devices.clear();
			return false;
		}
		for (const wxUniChar character : name) {
			if (character < ' ' || character == 127) {
				devices.clear();
				return false;
			}
		}
		for (const LauncherGraphicsDevice &existing : devices) {
			if (existing.index == index) {
				devices.clear();
				return false;
			}
		}
		devices.push_back({static_cast<legacy_s32>(index), name});
	}
	return true;
}

void launcher_normalize_graphics_options(LauncherOptions &options,
										 const std::vector<LauncherGraphicsDevice> &devices)
{
	if (options.renderer != GRAPHICS_RENDERER_NATIVE || devices.empty()) {
		options.renderer = GRAPHICS_RENDERER_SOFTWARE;
	}
	for (const LauncherGraphicsDevice &device : devices) {
		if (device.index == options.device) {
			return;
		}
	}
	options.device = devices.empty() ? GRAPHICS_DEVICE_DEFAULT : devices.front().index;
}
#endif
