#include "multi_replay_catalog.h"

#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>

extern "C" {
#include "../c/extended/csrpl.h"
#include "../c/gamestate.h"
#include "../c/replay/replay.h"
}

using TestTrack = std::array<legacy_u8, REPLAY_TRACK_SIZE>;
static const legacy_u16 TEST_FRAMES = 2U;

static void require(legacy_int condition, const legacy_char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void write_bytes(const wxString &path, const legacy_u8 *bytes, size_t size)
{
	wxFFile file(path, "wb");
	require(file.IsOpened() && file.Write(bytes, size) == size && file.Close(),
			"Cannot write replay fixture.");
}

static void write_legacy(const wxFileName &path, const legacy_char *name, const TestTrack &track)
{
	GAMEINFO config{};
	std::memcpy(config.game_playercarid, "COUN", REPLAY_CAR_ID_SIZE);
	std::memcpy(config.game_trackname, name, std::strlen(name));
	config.game_framespersec = GAME_FRAME_RATE_LOW;
	config.game_recordedframes = TEST_FRAMES;
	std::array<legacy_u8, REPLAY_INPUT_OFFSET + TEST_FRAMES> bytes{};
	replay_gameinfo_encode(bytes.data(), &config);
	std::memcpy(bytes.data() + REPLAY_GAMEINFO_SIZE, track.data(), track.size());
	write_bytes(path.GetFullPath(), bytes.data(), bytes.size());
}

static void write_extended(const wxFileName &path, const legacy_char *name, const TestTrack &track)
{
	CSRPL_METADATA metadata{};
	metadata.version = CSRPL_VERSION;
	metadata.flags = CSRPL_FLAG_CLASSIC_TRACK;
	metadata.frame_count = TEST_FRAMES;
	metadata.frames_per_second = GAME_FRAME_RATE_NORMAL;
	std::memcpy(metadata.player_car_id, "COUN", CSRPL_CAR_ID_SIZE);
	std::memcpy(metadata.track_name, name, std::strlen(name));
	const std::array<legacy_u8, TEST_FRAMES> inputs{};
	legacy_u8 *bytes = nullptr;
	size_t size = 0;
	require(csrpl_encode(&metadata, track.data(), track.size(), inputs.data(), inputs.size(),
						 &bytes, &size, nullptr, 0),
			"Cannot encode the classic-track CSRPL fixture.");
	std::unique_ptr<legacy_u8, decltype(&std::free)> storage(bytes, &std::free);
	write_bytes(path.GetFullPath(), storage.get(), size);
}

static void scenarios(const wxString &root)
{
	const TestTrack original{};
	TestTrack variant = original;
	variant.front() = 1;
	const wxString replay_name = wxString::FromUTF8("a replay \xc3\xa9.RPL");
	const wxFileName legacy(root, replay_name);
	const wxFileName extended(root, "B replay.CsRpL");
	write_legacy(legacy, "ALPHA", original);
	write_extended(extended, "alpha", original);
	write_legacy(wxFileName(root, "D variant.rpl"), "ALPHA", variant);
	write_legacy(wxFileName(root, "C other track.rpl"), "ZULU", original);
	write_legacy(wxFileName(root, "E fallback.rpl"), "B\xe9", original);
	const legacy_u8 invalid[] = {0};
	write_bytes(wxFileName(root, "broken.rpl").GetFullPath(), invalid, sizeof(invalid));
	write_bytes(wxFileName(root, "notes.txt").GetFullPath(), invalid, sizeof(invalid));
	const wxString subfolder = wxFileName(root, "subfolder.rpl").GetFullPath();
	require(wxFileName::Mkdir(subfolder), "Cannot create nested replay folder.");
	write_legacy(wxFileName(subfolder, "nested.rpl"), "NESTED", original);

	MultiReplayCatalog catalog;
	require(launcher_scan_multi_replays(root, catalog).empty(), "Cannot scan the replay folder.");
	require(catalog.invalid_files == 1U, "Wrong invalid-file count.");
	require(catalog.tracks.size() == 4U, "Wrong number of track variants.");
	require(catalog.tracks[0].name == "ALPHA" && catalog.tracks[0].replays.size() == 2U &&
				catalog.tracks[0].replays[0].name == replay_name &&
				catalog.tracks[0].replays[0].path == legacy.GetFullPath() &&
				catalog.tracks[0].replays[1].path == extended.GetFullPath(),
			"Matching classic tracks must group across formats and sort replay names.");
	require(catalog.tracks[1].name == "ALPHA" && catalog.tracks[1].replays.size() == 1U &&
				catalog.tracks[1].replays[0].name == "D variant.rpl",
			"Different classic track content must keep its own group.");
	require(catalog.tracks[2].name == wxString::FromUTF8("B\xc3\xa9") &&
				catalog.tracks[3].name == "ZULU" && catalog.tracks[3].replays.size() == 1U,
			"Distinct track names must sort alphabetically and preserve legacy text.");
	require(
		!launcher_scan_multi_replays(wxFileName(root, "absent").GetFullPath(), catalog).empty() &&
			catalog.tracks.empty() && catalog.invalid_files == 0,
		"Opening a missing folder must clear the previous catalog and return an error.");
}

legacy_int main()
{
	wxInitializer initialize;
	if (!initialize.IsOk()) {
		return EXIT_FAILURE;
	}
	const wxString root = wxFileName::CreateTempFileName("restunts-replay-catalog-");
	if (root.empty() || !wxRemoveFile(root) || !wxFileName::Mkdir(root)) {
		return EXIT_FAILURE;
	}
	legacy_int result = EXIT_SUCCESS;
	try {
		scenarios(root);
	} catch (const std::exception &error) {
		std::fprintf(stderr, "%s\n", error.what());
		result = EXIT_FAILURE;
	}
	wxFileName::Rmdir(root, wxPATH_RMDIR_RECURSIVE);
	return result;
}
