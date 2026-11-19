#ifndef RESTUNTS_LAUNCHER_CONTROLLER_BINDINGS_DIALOG_H
#define RESTUNTS_LAUNCHER_CONTROLLER_BINDINGS_DIALOG_H

#include "options.h"

class wxWindow;

legacy_bool launcher_edit_controller_bindings(wxWindow *parent, const wxString &settings_path,
											  CONTROLLER_BINDINGS &bindings);

#endif
