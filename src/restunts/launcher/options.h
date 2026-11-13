#ifndef RESTUNTS_LAUNCHER_OPTIONS_H
#define RESTUNTS_LAUNCHER_OPTIONS_H

#include <wx/string.h>
#include <vector>
#include "../c/legacy.h"

/* wxWidgets overrides and configuration values require the exact bool ABI. */
typedef bool legacy_bool;

enum LauncherOption {
	LAUNCHER_NEW_MIDI,
	LAUNCHER_SKIP_INTRO,
	LAUNCHER_VIBRATION,
	LAUNCHER_FULLSCREEN,
	LAUNCHER_SHOW_FPS,
	LAUNCHER_SHOW_MPH,
	LAUNCHER_MULTI_CORE,
	LAUNCHER_POWER_GEAR,
	LAUNCHER_LEGACY_COLLISION,
	LAUNCHER_LEFT_CORNER_BIAS,
	LAUNCHER_DUAL_WAY_SWITCHING,
	LAUNCHER_CHICANE_FAST_GRASS,
	LAUNCHER_AMIGA_GRASS,
	LAUNCHER_OWOOT,
	LAUNCHER_OPTION_COUNT
};

struct LauncherOptionDefinition {
	const legacy_char *key;
	const legacy_char *label;
	const legacy_char *enabled_argument;
	const legacy_char *disabled_argument;
	legacy_bool initial;
};

struct LauncherHyperVision {
	const legacy_char *value;
	const legacy_char *label;
	const legacy_char *argument;
};

enum {
	LAUNCHER_HV_OFF,
	LAUNCHER_HV_AUTO,
	LAUNCHER_HV_FULL,
	LAUNCHER_HV_HIGH,
	LAUNCHER_HV_MEDIUM,
	LAUNCHER_HV_LOW,
	LAUNCHER_HV_COUNT
};

enum LauncherSoundMode {
	LAUNCHER_SOUND_NONE,
	LAUNCHER_SOUND_PC_SPEAKER,
	LAUNCHER_SOUND_ADLIB,
	LAUNCHER_SOUND_MT32,
	LAUNCHER_SOUND_MODE_COUNT
};

struct LauncherSoundDefinition {
	const legacy_char *value;
	const legacy_char *label;
	const legacy_char *argument;
};

struct LauncherRomSet {
	wxString id;
	wxString label;
};

struct LauncherOptions {
	wxString folder;
	wxString mt32_rom_folder;
	wxString mt32_model;
	legacy_int sound_mode;
	legacy_bool enabled[LAUNCHER_OPTION_COUNT];
	legacy_int hypervision;
	LauncherOptions();
};

extern const LauncherOptionDefinition launcher_option_definitions[LAUNCHER_OPTION_COUNT];
extern const LauncherSoundDefinition launcher_sound_definitions[LAUNCHER_SOUND_MODE_COUNT];
extern const LauncherHyperVision launcher_hypervision_definitions[LAUNCHER_HV_COUNT];

legacy_bool launcher_parse_rom_inventory(const wxString &json, std::vector<LauncherRomSet> &sets,
										 wxString &error);
wxString launcher_settings_path();
void launcher_read_options(const wxString &path, LauncherOptions &options);
wxString launcher_save_options(const wxString &path, const LauncherOptions &options);
std::vector<wxString> launcher_game_arguments(const wxString &game, const LauncherOptions &options);

#endif
