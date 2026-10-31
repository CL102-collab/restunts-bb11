#ifndef RESTUNTS_GENGHOST_GUI_EXTENDED_OPTIONS_H
#define RESTUNTS_GENGHOST_GUI_EXTENDED_OPTIONS_H

#include "options.h"

/* The original options model delegates format-specific file and replay policy. */
legacy_bool extended_genghost_input(const wxString &path);
legacy_bool extended_genghost_replay_car(const wxString &path, wxString &car);
legacy_bool extended_genghost_apply_replay_physics(GenghostOptions &options);
legacy_bool extended_genghost_validate_input(const wxString &path, legacy_bool fine_tune,
											 legacy_bool &valid);
wxString extended_genghost_output_extension(const wxString &path);

#endif
