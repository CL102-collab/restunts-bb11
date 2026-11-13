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
static const legacy_char SETTINGS_SOUND_MODE_KEY[] = "sound_mode";
static const legacy_char SETTINGS_MT32_FOLDER_KEY[] = "mt32_rom_folder";
static const legacy_char SETTINGS_MT32_MODEL_KEY[] = "mt32_model";
static const legacy_char SETTINGS_LEGACY_SOUND_KEY[] = "sound";
static const legacy_int JSON_HEX_DIGITS = 4;
static const legacy_u32 JSON_HEX_BASE = 16U;
static const legacy_u32 JSON_SURROGATE_FIRST = 0xD800U;
static const legacy_u32 JSON_SURROGATE_LAST = 0xDFFFU;
static const legacy_u32 JSON_LOW_SURROGATE_FIRST = 0xDC00U;
static const legacy_u32 JSON_SURROGATE_BITS = 10U;
static const legacy_u32 JSON_UNICODE_SUPPLEMENT = 0x10000U;
static const legacy_int JSON_MAX_DEPTH = 16;
static const size_t ROM_INVENTORY_MAX_CHARACTERS = 65536U;
static const legacy_char DATA_DIRECTORY_ARGUMENT[] = "--data-dir";

const LauncherOptionDefinition launcher_option_definitions[LAUNCHER_OPTION_COUNT] = {
	{"new_midi", "OGG music replacement", "--ogg:on", "--ogg:off", false},
	{"skip_intro", "Skip intro", "--nointro", nullptr, false},
	{"vibration", "Controller vibration", "--controller-vibration:on", "--controller-vibration:off",
	 true},
	{"fullscreen", "Fullscreen", "--fullscreen:on", "--fullscreen:off", false},
	{"show_fps", "Show FPS", "--fps:on", "--fps:off", false},
	{"show_mph", "Show MPH (Ctrl+F9)", "--showmph:on", "--showmph:off", false},
	{"multi_core", "Multi-core rendering", "--multi-core:on", "--multi-core:off", true},
	{"power_gear", "Power Gear", "--pg:on", "--pg:off", true},
	{"legacy_collision", "Original Collision Bugs", "--lc:on", "--lc:off", true},
	{"left_corner_bias", "Left Corner Bias", "--lcb:on", "--lcb:off", true},
	{"dual_way_switching", "Dual-Way Switching", "--dws:on", "--dws:off", true},
	{"chicane_fast_grass", "Chicane Fast-Grass", "--cfg:on", "--cfg:off", true},
	{"amiga_grass", "Amiga Grass", "--amigagrass", nullptr, false},
	{"owoot", "OWOOT", "--owoot", nullptr, false}};

const LauncherSoundDefinition launcher_sound_definitions[LAUNCHER_SOUND_MODE_COUNT] = {
	{"none", "No sound", "--nosound"},
	{"pc-speaker", "PC Speaker", "--soundmode:pc-speaker"},
	{"adlib", "Ad Lib", "--soundmode:adlib"},
	{"mt-32", "MT-32", "--soundmode:mt-32"}};

const LauncherHyperVision launcher_hypervision_definitions[LAUNCHER_HV_COUNT] = {
	{"off", "Off", nullptr},
	{"auto", "Auto", "--hv:auto"},
	{"full", "Full", "--hv:full"},
	{"high", "High", "--hv:high"},
	{"medium", "Medium", "--hv:medium"},
	{"low", "Low", "--hv:low"}};

LauncherOptions::LauncherOptions() : sound_mode(LAUNCHER_SOUND_ADLIB), hypervision(LAUNCHER_HV_AUTO)
{
	for (legacy_int index = 0; index < LAUNCHER_OPTION_COUNT; ++index) {
		enabled[index] = launcher_option_definitions[index].initial;
	}
}

/* The game owns ROM identification. The launcher only reads its small JSON inventory,
 * so it has no independent ROM table or dependency on the synthesizer library. */
class LauncherInventoryReader
{
	public:
	explicit LauncherInventoryReader(const wxString &json) : json_(json)
	{
	}

	legacy_bool read(std::vector<LauncherRomSet> &sets, wxString &error)
	{
		legacy_bool have_sets = false;
		legacy_bool have_error = false;
		if (!take('{')) {
			return false;
		}
		if (!take('}')) {
			do {
				wxString key;
				if (!string(key) || !take(':')) {
					return false;
				}
				if (key == "sets") {
					if (have_sets || !array(sets)) {
						return false;
					}
					have_sets = true;
				} else if (key == "error") {
					if (have_error || !string(error)) {
						return false;
					}
					have_error = true;
				} else if (!skip_value(0)) {
					return false;
				}
			} while (take(','));
			if (!take('}')) {
				return false;
			}
		}
		whitespace();
		return have_sets && position_ == json_.length();
	}

	private:
	void whitespace()
	{
		while (position_ < json_.length()) {
			const wxUniChar value = json_[position_];
			if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
				break;
			}
			++position_;
		}
	}

	legacy_bool take(wxUniChar wanted)
	{
		whitespace();
		if (position_ == json_.length() || json_[position_] != wanted) {
			return false;
		}
		++position_;
		return true;
	}

	legacy_bool hex(legacy_u32 &value)
	{
		value = 0;
		for (legacy_int index = 0; index < JSON_HEX_DIGITS; ++index) {
			if (position_ == json_.length()) {
				return false;
			}
			const wxUniChar digit = json_[position_++];
			legacy_u32 part;
			if (digit >= '0' && digit <= '9') {
				part = digit.GetValue() - '0';
			} else if (digit >= 'a' && digit <= 'f') {
				part = digit.GetValue() - 'a' + 10U;
			} else if (digit >= 'A' && digit <= 'F') {
				part = digit.GetValue() - 'A' + 10U;
			} else {
				return false;
			}
			value = value * JSON_HEX_BASE + part;
		}
		return true;
	}

	legacy_bool string(wxString &value)
	{
		value.clear();
		if (!take('"')) {
			return false;
		}
		while (position_ < json_.length()) {
			wxUniChar character = json_[position_++];
			if (character == '"') {
				return true;
			}
			if (character.GetValue() < ' ') {
				return false;
			}
			if (character != '\\') {
				value += character;
				continue;
			}
			if (position_ == json_.length()) {
				return false;
			}
			character = json_[position_++];
			if (character == 'u') {
				legacy_u32 code;
				if (!hex(code)) {
					return false;
				}
				if (code >= JSON_SURROGATE_FIRST && code < JSON_LOW_SURROGATE_FIRST) {
					legacy_u32 low;
					if (position_ + 2U > json_.length() || json_[position_++] != '\\' ||
						json_[position_++] != 'u' || !hex(low) || low < JSON_LOW_SURROGATE_FIRST ||
						low > JSON_SURROGATE_LAST) {
						return false;
					}
					code = JSON_UNICODE_SUPPLEMENT +
						   ((code - JSON_SURROGATE_FIRST) << JSON_SURROGATE_BITS) + low -
						   JSON_LOW_SURROGATE_FIRST;
				} else if (code >= JSON_LOW_SURROGATE_FIRST && code <= JSON_SURROGATE_LAST) {
					return false;
				}
				if (code == 0) {
					return false;
				}
				value += wxUniChar(code);
			} else if (character == '"' || character == '\\' || character == '/') {
				value += character;
			} else if (character == 'n') {
				value += '\n';
			} else if (character == 'r') {
				value += '\r';
			} else if (character == 't') {
				value += '\t';
			} else if (character == 'b') {
				value += '\b';
			} else if (character == 'f') {
				value += '\f';
			} else {
				return false;
			}
		}
		return false;
	}

	legacy_bool array(std::vector<LauncherRomSet> &sets)
	{
		if (!take('[')) {
			return false;
		}
		if (take(']')) {
			return true;
		}
		do {
			LauncherRomSet set;
			if (!take('{')) {
				return false;
			}
			do {
				wxString key;
				if (!string(key) || !take(':')) {
					return false;
				}
				if (key == "id") {
					if (!set.id.empty() || !string(set.id)) {
						return false;
					}
				} else if (key == "label") {
					if (!set.label.empty() || !string(set.label)) {
						return false;
					}
				} else if (!skip_value(0)) {
					return false;
				}
			} while (take(','));
			if (!take('}') || set.id.empty() || set.label.empty()) {
				return false;
			}
			for (const LauncherRomSet &previous : sets) {
				if (previous.id == set.id) {
					return false;
				}
			}
			sets.push_back(set);
		} while (take(','));
		return take(']');
	}

	legacy_bool skip_value(legacy_int depth)
	{
		if (depth >= JSON_MAX_DEPTH) {
			return false;
		}
		whitespace();
		if (position_ == json_.length()) {
			return false;
		}
		if (json_[position_] == '"') {
			wxString ignored;
			return string(ignored);
		}
		if (take('{')) {
			if (take('}')) {
				return true;
			}
			do {
				wxString key;
				if (!string(key) || !take(':') || !skip_value(depth + 1)) {
					return false;
				}
			} while (take(','));
			return take('}');
		}
		if (take('[')) {
			if (take(']')) {
				return true;
			}
			do {
				if (!skip_value(depth + 1)) {
					return false;
				}
			} while (take(','));
			return take(']');
		}
		const size_t first = position_;
		while (position_ < json_.length()) {
			const wxUniChar value = json_[position_];
			if (value == ',' || value == '}' || value == ']' || value == ' ' || value == '\t' ||
				value == '\r' || value == '\n') {
				break;
			}
			++position_;
		}
		const wxString primitive = json_.Mid(first, position_ - first);
		legacy_f64 number;
		return primitive == "null" || primitive == "true" || primitive == "false" ||
			   (!primitive.empty() && primitive.ToDouble(&number));
	}

	const wxString &json_;
	size_t position_ = 0;
};

legacy_bool launcher_parse_rom_inventory(const wxString &json, std::vector<LauncherRomSet> &sets,
										 wxString &error)
{
	sets.clear();
	error.clear();
	if (json.length() > ROM_INVENTORY_MAX_CHARACTERS) {
		error = "The game returned an oversized MT-32 ROM inventory.";
		return false;
	}
	LauncherInventoryReader reader(json);
	if (!reader.read(sets, error)) {
		sets.clear();
		error = "The game returned an invalid MT-32 ROM inventory.";
		return false;
	}
	return true;
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
	config.Read(SETTINGS_MT32_FOLDER_KEY, &options.mt32_rom_folder);
	config.Read(SETTINGS_MT32_MODEL_KEY, &options.mt32_model);
	wxString sound_mode;
	if (config.Read(SETTINGS_SOUND_MODE_KEY, &sound_mode)) {
		for (legacy_int index = 0; index < LAUNCHER_SOUND_MODE_COUNT; ++index) {
			if (sound_mode == launcher_sound_definitions[index].value) {
				options.sound_mode = index;
				break;
			}
		}
	} else if (!config.ReadBool(SETTINGS_LEGACY_SOUND_KEY, true)) {
		options.sound_mode = LAUNCHER_SOUND_NONE;
	}
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
	const legacy_int sound_mode =
		options.sound_mode >= LAUNCHER_SOUND_NONE && options.sound_mode < LAUNCHER_SOUND_MODE_COUNT
			? options.sound_mode
			: LAUNCHER_SOUND_ADLIB;
	written = config.Write(SETTINGS_SOUND_MODE_KEY, launcher_sound_definitions[sound_mode].value) &&
			  written;
	written = config.Write(SETTINGS_LEGACY_SOUND_KEY, sound_mode != LAUNCHER_SOUND_NONE) && written;
	written = config.Write(SETTINGS_MT32_FOLDER_KEY, options.mt32_rom_folder) && written;
	written = config.Write(SETTINGS_MT32_MODEL_KEY, options.mt32_model) && written;
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
	const legacy_int sound_mode =
		options.sound_mode >= LAUNCHER_SOUND_NONE && options.sound_mode < LAUNCHER_SOUND_MODE_COUNT
			? options.sound_mode
			: LAUNCHER_SOUND_ADLIB;
	arguments.push_back(launcher_sound_definitions[sound_mode].argument);
	if (sound_mode == LAUNCHER_SOUND_MT32) {
		arguments.push_back("--mt-32-rom-dir");
		arguments.push_back(options.mt32_rom_folder);
		if (!options.mt32_model.empty()) {
			arguments.push_back("--mt-32-model");
			arguments.push_back(options.mt32_model);
		}
	}
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
