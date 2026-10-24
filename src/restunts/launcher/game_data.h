#ifndef RESTUNTS_LAUNCHER_GAME_DATA_H
#define RESTUNTS_LAUNCHER_GAME_DATA_H

#include <wx/string.h>

/* Empty means the selected folder contains the supported MISC version resource.
 * This filesystem-only probe never changes the working directory or starts SDL. */
wxString launcher_validate_game_data(const wxString &folder);

#endif
