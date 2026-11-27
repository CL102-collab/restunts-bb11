#include "multi_replay_catalog.h"

#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <new>
#include <utility>

extern "C" {
#include "../c/extended/csrpl.h"
#include "../c/gamestate.h"
#include "../c/opponent/opponent.h"
#include "../c/replay/replay.h"
#include "../c/track/trackdata_layout.h"
}

namespace
{

using ReplayBytes = std::vector<legacy_u8>;

static wxString display_track_name(const void *bytes, size_t capacity)
{
	const auto *name = static_cast<const legacy_char *>(bytes);
	const auto *terminator = static_cast<const legacy_char *>(std::memchr(name, 0, capacity));
	const size_t length = terminator == nullptr ? capacity : static_cast<size_t>(terminator - name);
	wxString display = wxString::FromUTF8(name, length);
	if (display.empty() && length != 0) {
		display = wxString(name, wxConvISO8859_1, length);
	}
	return display.Trim();
}

static legacy_int read_legacy_track(const ReplayBytes &replay, wxString &name, ReplayBytes &track)
{
	GAMEINFO config;
	replay_gameinfo_decode(&config, replay.data());
	if (config.game_recordedframes == 0 ||
		config.game_recordedframes > TRACKDATA_REPLAY_INPUT_BUFFER_SIZE ||
		replay_file_size(config.game_recordedframes) != replay.size() ||
		(config.game_framespersec != GAME_FRAME_RATE_LOW &&
		 config.game_framespersec != GAME_FRAME_RATE_NORMAL) ||
		static_cast<legacy_u8>(config.game_opponenttype) > OPPONENT_LEGACY_LAST ||
		config.game_playertransmission < TRANSMISSION_MANUAL ||
		config.game_playertransmission > TRANSMISSION_AUTOMATIC ||
		(config.game_opponenttype != 0 &&
		 (config.game_opponenttransmission < TRANSMISSION_MANUAL ||
		  config.game_opponenttransmission > TRANSMISSION_AUTOMATIC))) {
		return false;
	}
	name = display_track_name(config.game_trackname, sizeof(config.game_trackname));
	track.assign(replay.begin() + REPLAY_GAMEINFO_SIZE, replay.begin() + REPLAY_INPUT_OFFSET);
	return true;
}

static legacy_int read_extended_track(const ReplayBytes &replay, wxString &name, ReplayBytes &track)
{
	CSRPL_VIEW view;
	if (!csrpl_decode(replay.data(), replay.size(), &view, nullptr, 0) ||
		view.metadata.frame_count == 0 ||
		(view.metadata.opponent_type > CSRPL_CLASSIC_OPPONENT_LAST &&
		 (view.metadata.opponent_type != CSRPL_UNIT_OPPONENT_TYPE ||
		  std::memcmp(view.metadata.opponent_id, "UNIT", CSRPL_OPPONENT_ID_SIZE) != 0))) {
		return false;
	}
	name = display_track_name(view.metadata.track_name, sizeof(view.metadata.track_name));
	track.assign(view.track_data, view.track_data + view.track_size);
	return true;
}

static legacy_int read_track(const MultiReplayFile &candidate, wxString &name, ReplayBytes &track)
{
	if (!wxFileName::FileExists(candidate.path)) {
		return false;
	}
	wxFFile file(candidate.path, "rb");
	if (!file.IsOpened()) {
		return false;
	}
	const legacy_int extended = candidate.name.Lower().EndsWith(".csrpl");
	const auto minimum =
		static_cast<wxFileOffset>(extended ? CSRPL_HEADER_SIZE : REPLAY_INPUT_OFFSET);
	const auto maximum = static_cast<wxFileOffset>(
		extended ? LEGACY_S32_MAX : replay_file_size(TRACKDATA_REPLAY_INPUT_BUFFER_SIZE));
	const wxFileOffset length = file.Length();
	if (length < minimum || length > maximum) {
		return false;
	}
	ReplayBytes bytes(static_cast<size_t>(length));
	if (file.Read(bytes.data(), bytes.size()) != bytes.size()) {
		return false;
	}
	return extended ? read_extended_track(bytes, name, track)
					: read_legacy_track(bytes, name, track);
}

static std::vector<MultiReplayFile> replay_files(wxDir &folder)
{
	std::vector<MultiReplayFile> files;
	wxString filename;
	for (legacy_int more = folder.GetFirst(&filename, wxEmptyString, wxDIR_FILES | wxDIR_HIDDEN);
		 more; more = folder.GetNext(&filename)) {
		const wxString folded = filename.Lower();
		if (folded.EndsWith(".rpl") || folded.EndsWith(".csrpl")) {
			files.push_back({filename, wxFileName(folder.GetName(), filename).GetFullPath()});
		}
	}
	std::sort(files.begin(), files.end(),
			  [](const MultiReplayFile &left, const MultiReplayFile &right) {
				  const legacy_int order = left.name.CmpNoCase(right.name);
				  return order == 0 ? left.path.Cmp(right.path) < 0 : order < 0;
			  });
	return files;
}

static MultiReplayCatalog scan_replays(wxDir &folder)
{
	MultiReplayCatalog catalog;
	// Same-name variants share a label but keep distinct embedded track payloads.
	std::map<wxString, std::map<ReplayBytes, size_t>> track_indices;
	for (MultiReplayFile &file : replay_files(folder)) {
		wxString name;
		ReplayBytes track;
		if (!read_track(file, name, track)) {
			++catalog.invalid_files;
			continue;
		}
		auto &variants = track_indices[name.Lower()];
		const auto entry = variants.insert(std::make_pair(std::move(track), catalog.tracks.size()));
		if (entry.second) {
			catalog.tracks.push_back({name.empty() ? wxString("Unnamed track") : name, {}});
		}
		catalog.tracks[entry.first->second].replays.push_back(std::move(file));
	}
	std::stable_sort(catalog.tracks.begin(), catalog.tracks.end(),
					 [](const MultiReplayTrack &left, const MultiReplayTrack &right) {
						 return left.name.CmpNoCase(right.name) < 0;
					 });
	return catalog;
}

} // namespace

wxString launcher_scan_multi_replays(const wxString &directory, MultiReplayCatalog &catalog)
{
	catalog = MultiReplayCatalog{};
	wxLogNull quiet;
	wxFileName path = wxFileName::DirName(directory);
	if (directory.empty() || !path.MakeAbsolute()) {
		return "Choose an existing replay folder.";
	}
	wxDir folder(path.GetPath());
	if (!folder.IsOpened()) {
		return "Cannot open the selected replay folder.";
	}
	try {
		catalog = scan_replays(folder);
	} catch (const std::bad_alloc &) {
		return "There is insufficient memory to scan the selected replay folder.";
	}
	return wxEmptyString;
}
