#ifndef RESTUNTS_LAUNCHER_MULTI_REPLAY_DIALOG_H
#define RESTUNTS_LAUNCHER_MULTI_REPLAY_DIALOG_H

#include <vector>
#include <wx/string.h>

class wxWindow;

std::vector<wxString> launcher_choose_multi_replays(wxWindow *parent, const wxString &folder);

#endif
