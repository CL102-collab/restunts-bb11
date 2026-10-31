#ifndef RESTUNTS_GENGHOST_GUI_OPTIONS_H
#define RESTUNTS_GENGHOST_GUI_OPTIONS_H

#include "../launcher/options.h"

enum GenghostMode {
	GENGHOST_MODE_GENERATE,
	GENGHOST_MODE_FINE_TUNE,
	GENGHOST_MODE_GEARS_ONLY,
	GENGHOST_MODE_COUNT
};

enum GenghostStrategy { GENGHOST_SEARCH_LEGACY, GENGHOST_SEARCH_STATE, GENGHOST_SEARCH_COUNT };

enum {
	GENGHOST_DEFAULT_SECONDS = 1800,
	GENGHOST_MAX_SECONDS = 86400,
	GENGHOST_DEFAULT_CORES = 1,
	GENGHOST_MAX_CORES = 256,
	GENGHOST_PHYSICS_COUNT = 7
};

struct GenghostOptions {
	wxString folder;
	wxString input;
	wxString car;
	wxString output;
	legacy_int mode;
	legacy_int seconds;
	legacy_int cores;
	legacy_int strategy;
	legacy_bool physics[GENGHOST_PHYSICS_COUNT];
	GenghostOptions();
};

struct GenghostCatalog {
	std::vector<wxString> inputs;
	std::vector<wxString> cars;
};

extern const LauncherOption genghost_physics_options[GENGHOST_PHYSICS_COUNT];

wxString genghost_settings_path();
void genghost_read_options(const wxString &path, GenghostOptions &options,
						   const wxString &shared_folder = wxEmptyString);
wxString genghost_save_options(const wxString &path, const GenghostOptions &options);

/* assets is the resource base directory whose cars/ folder accompanies genghost.
 * Inputs are absolute paths; car codes are sorted uppercase four-character ids. */
wxString genghost_scan_catalog(const wxString &folder, const wxString &assets,
							   GenghostCatalog &catalog);
wxString genghost_resolved_input(const GenghostOptions &options);
wxString genghost_resolved_car(const GenghostOptions &options);
wxString genghost_default_output(const GenghostOptions &options);
/* Load a selected CSRPL's recorded rules before the user adjusts its checkboxes. */
legacy_bool genghost_apply_replay_physics(GenghostOptions &options);
wxString genghost_validate_options(const GenghostOptions &options);
std::vector<wxString> genghost_arguments(const wxString &executable,
										 const GenghostOptions &options);

#endif
