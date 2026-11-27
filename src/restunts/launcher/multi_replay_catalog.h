#ifndef RESTUNTS_LAUNCHER_MULTI_REPLAY_CATALOG_H
#define RESTUNTS_LAUNCHER_MULTI_REPLAY_CATALOG_H

#include <wx/string.h>
#include <vector>

struct MultiReplayFile {
	wxString name;
	wxString path;
};

struct MultiReplayTrack {
	wxString name;
	std::vector<MultiReplayFile> replays;
};

struct MultiReplayCatalog {
	std::vector<MultiReplayTrack> tracks;
	size_t invalid_files = 0;
};

wxString launcher_scan_multi_replays(const wxString &directory, MultiReplayCatalog &catalog);

#endif
