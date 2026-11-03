#ifndef RESTUNTS_LAUNCHER_OPTIONS_H
#define RESTUNTS_LAUNCHER_OPTIONS_H

#include <wx/string.h>
#include <vector>
#include "../c/legacy.h"
#include "../c/renderer/graphics_backend.h"

/* wxWidgets overrides and configuration values require the exact bool ABI. */
typedef bool legacy_bool;

enum LauncherOption {
	LAUNCHER_SOUND,
	LAUNCHER_NEW_MIDI,
	LAUNCHER_SKIP_INTRO,
	LAUNCHER_VIBRATION,
	LAUNCHER_FULLSCREEN,
	LAUNCHER_SHOW_FPS,
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

#ifdef RESTUNTS_GRAPHICS_LAUNCHER
/* Dropdown positions are independent of the platform renderer enum values. */
enum LauncherRendererSelection { LAUNCHER_RENDERER_SOFTWARE, LAUNCHER_RENDERER_NATIVE };
#endif

struct LauncherGraphicsDevice {
	legacy_s32 index;
	wxString name;
};

struct LauncherOptions {
	wxString folder;
	legacy_bool enabled[LAUNCHER_OPTION_COUNT];
	legacy_int hypervision;
	legacy_int renderer;
	legacy_s32 device;
	LauncherOptions();
};

extern const LauncherOptionDefinition launcher_option_definitions[LAUNCHER_OPTION_COUNT];
extern const LauncherHyperVision launcher_hypervision_definitions[LAUNCHER_HV_COUNT];

wxString launcher_settings_path();
void launcher_read_options(const wxString &path, LauncherOptions &options);
wxString launcher_save_options(const wxString &path, const LauncherOptions &options);
#ifdef RESTUNTS_GRAPHICS_LAUNCHER
legacy_bool launcher_parse_graphics_devices(const wxString &report,
											std::vector<LauncherGraphicsDevice> &devices);
void launcher_normalize_graphics_options(LauncherOptions &options,
										 const std::vector<LauncherGraphicsDevice> &devices);
#endif
std::vector<wxString> launcher_game_arguments(const wxString &game, const LauncherOptions &options);

#endif
