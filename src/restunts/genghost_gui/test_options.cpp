#include <wx/ffile.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#if defined(__UNIX__)
#include <unistd.h>
#endif
#ifdef __WXMSW__
#include <windows.h>
#endif
#include "options.h"
#include "../genghost/simulation.h"

static const legacy_int TEST_SUCCESS = 0;
static const legacy_int TEST_FAILURE = 1;
static const legacy_uint TEST_REPLAY_FRAMES = 4U;
static const legacy_uint TEST_PLAYER_GEAR = 0U;
static const legacy_uint TEST_OPPONENT_TYPE = 1U;
static const legacy_uint ARGUMENT_INPUT_INDEX = 3U;
static const legacy_uint ARGUMENT_CAR_INDEX = 4U;
static const legacy_uint PHYSICS_AMIGA_INDEX = 5U;
static const legacy_uint PHYSICS_OWOOT_INDEX = 6U;
static legacy_int failures;

static void check(legacy_bool condition, const legacy_char *message)
{
	if (!condition) {
		std::fprintf(stderr, "%s\n", message);
		++failures;
	}
}

static legacy_bool contains(const std::vector<wxString> &arguments, const wxString &argument)
{
	return std::find(arguments.begin(), arguments.end(), argument) != arguments.end();
}

static void write_bytes(const wxString &path, const std::vector<legacy_u8> &bytes)
{
	wxFFile file(path, "wb");
	check(file.IsOpened() && file.Write(bytes.data(), bytes.size()) == bytes.size(),
		  "Cannot write test fixture.");
}

static void touch(const wxString &path)
{
	write_bytes(path, std::vector<legacy_u8>());
}

static std::vector<legacy_u8> replay()
{
	std::vector<legacy_u8> bytes(REPLAY_INPUT_OFFSET + TEST_REPLAY_FRAMES);
	const legacy_char car[] = "COUN";
	for (legacy_uint index = 0; index < REPLAY_CAR_ID_SIZE; ++index) {
		bytes[index] = car[index];
	}
	bytes[offsetof(GAMEINFO, game_playertransmission)] = TEST_PLAYER_GEAR;
	LEGACY_WRITE_U16_LE(bytes.data() + offsetof(GAMEINFO, game_framespersec), GENGHOST_FRAME_RATE);
	LEGACY_WRITE_U16_LE(bytes.data() + offsetof(GAMEINFO, game_recordedframes), TEST_REPLAY_FRAMES);
	return bytes;
}

legacy_int main()
{
	wxInitializer initialize;
	if (!initialize.IsOk()) {
		return TEST_FAILURE;
	}
	const wxString directory = wxFileName::CreateTempFileName("restunts-genghost-options-");
	check(!directory.empty() && wxRemoveFile(directory) && wxFileName::Mkdir(directory),
		  "Cannot create test directory.");
	const wxString game =
		wxFileName(directory, wxString::FromUTF8("game árvíz & test")).GetFullPath();
	const wxString assets = wxFileName(directory, "assets").GetFullPath();
	const wxString game_cars = wxFileName(game, "cars").GetFullPath();
	const wxString assets_cars = wxFileName(assets, "cars").GetFullPath();
	check(wxFileName::Mkdir(game_cars, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) &&
			  wxFileName::Mkdir(assets_cars, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL),
		  "Cannot create catalog fixture folders.");
	const wxString track = wxFileName(game, "Default.tRk").GetFullPath();
	const wxString input = wxFileName(game, "Winner.rPl").GetFullPath();
	write_bytes(track, std::vector<legacy_u8>(REPLAY_TRACK_SIZE));
	write_bytes(input, replay());
	touch(wxFileName(game, "CarCoun.pRe").GetFullPath());
	touch(wxFileName(game_cars, "cardia3.res").GetFullPath());
	touch(wxFileName(assets_cars, "CARCOUN.RES").GetFullPath());
	touch(wxFileName(assets_cars, "CARPMIN.RES").GetFullPath());
	touch(wxFileName(assets_cars, "CARBAD!.RES").GetFullPath());
	touch(wxFileName(assets_cars, "CARLONGER.RES").GetFullPath());
	touch(wxFileName(game, "readme.txt").GetFullPath());
	check(wxFileName::Mkdir(wxFileName(game, "directory.trk").GetFullPath()),
		  "Cannot create directory exclusion fixture.");
	GenghostCatalog catalog;
	check(genghost_scan_catalog(game, assets, catalog).empty(), "Cannot scan game catalog.");
	check(catalog.inputs == std::vector<wxString>({track, input}),
		  "Catalog must sort actual track/replay files and handle mixed-case extensions.");
	check(catalog.cars == std::vector<wxString>({"COUN", "DIA3", "PMIN"}),
		  "Cars must include loose, custom and bundled resources with valid deduplicated codes.");
	check(!genghost_scan_catalog(wxFileName(directory, "missing").GetFullPath(), assets, catalog)
				  .empty() &&
			  catalog.inputs.empty() && catalog.cars.empty(),
		  "Unreadable catalogs must report an error and discard previous selections.");

	GenghostOptions options;
	options.folder = game;
	options.input = wxFileName(track).GetFullName();
	options.car = "coun";
	options.output = "new replay & result.rpl";
	check(genghost_validate_options(options).empty(), "Default generation options must be valid.");
	options.car = wxString::FromUTF8("ßaaa");
	check(!genghost_validate_options(options).empty(),
		  "Car resource codes must contain four ASCII letters, digits or underscores.");
	options.car = "coun";
	check(genghost_resolved_input(options) == track && genghost_resolved_car(options) == "COUN",
		  "Relative input paths must resolve from the game folder; car codes must normalize.");
	check(wxFileName(genghost_default_output(options)).GetFullName() == "DEF-COUN.rpl",
		  "Default generation output must use a DOS 8.3 replay name.");
	const wxString executable = "/program files/genghost & test";
	std::vector<wxString> arguments = genghost_arguments(executable, options);
	check(
		arguments.front() == executable && arguments[ARGUMENT_INPUT_INDEX] == track &&
			arguments[ARGUMENT_CAR_INDEX] == "COUN" && contains(arguments, game) &&
			contains(arguments, wxFileName(game, options.output).GetFullPath()),
		"Paths containing spaces, Unicode and shell characters must remain individual arguments.");
	const legacy_char *default_physics[] = {"--pg:on", "--lc:on", "--lcb:on", "--dws:on",
											"--cfg:on"};
	for (const legacy_char *flag : default_physics) {
		check(contains(arguments, flag), "Default physics must match the startup window.");
	}
	check(!contains(arguments, "--amigagrass") && !contains(arguments, "--owoot"),
		  "Optional physics must start disabled.");
	for (legacy_int index = 0; index < GENGHOST_PHYSICS_COUNT; ++index) {
		options.physics[index] = !options.physics[index];
	}
	options.strategy = GENGHOST_SEARCH_STATE;
	options.cores = 0;
	options.seconds = 0;
	arguments = genghost_arguments(executable, options);
	const legacy_char *changed_physics[] = {"--pg:off",	 "--lc:off",	 "--lcb:off", "--dws:off",
											"--cfg:off", "--amigagrass", "--owoot"};
	for (const legacy_char *flag : changed_physics) {
		check(contains(arguments, flag), "Every changed physics checkbox must reach genghost.");
	}
	check(contains(arguments, "state") && genghost_validate_options(options).empty(),
		  "State search, automatic core count and unlimited time must be supported.");
	options.mode = GENGHOST_MODE_FINE_TUNE;
	options.input = input;
	options.car = "DIA3";
	check(genghost_validate_options(options).empty() && genghost_resolved_car(options) == "COUN",
		  "Fine-tuning must use the input replay car independently of the selected car.");
	check(wxFileName(genghost_default_output(options)).GetFullName() == "WINNE-FT.rpl",
		  "Fine-tuning must choose a separate DOS 8.3 output name.");
	arguments = genghost_arguments(executable, options);
	check(contains(arguments, "--fine-tune") && !contains(arguments, "DIA3") &&
			  !contains(arguments, "--fine-tune-gears-only"),
		  "Fine-tune arguments must omit CAR and choose exactly one refinement switch.");
	options.mode = GENGHOST_MODE_GEARS_ONLY;
	arguments = genghost_arguments(executable, options);
	check(contains(arguments, "--fine-tune-gears-only") && !contains(arguments, "--fine-tune"),
		  "Gears-only refinement must select its dedicated mode.");
	options.output = input;
	check(!genghost_validate_options(options).empty(), "Input replay must never be overwritten.");
	options.output = "new replay & result.rpl";
#if defined(__UNIX__) || defined(__WXMSW__)
	options.mode = GENGHOST_MODE_GENERATE;
	const wxString alias = wxFileName(game, "alias.rpl").GetFullPath();
#ifdef __WXMSW__
	check(CreateHardLinkW(alias.wc_str(), input.wc_str(), nullptr) != 0,
		  "Cannot create hard-link fixture.");
#else
	check(link(input.fn_str(), alias.fn_str()) == 0, "Cannot create hard-link fixture.");
#endif
	options.output = alias;
	check(!genghost_validate_options(options).empty(),
		  "Hard-linked output must preserve the input.");
	options.output = "new replay & result.rpl";
	const wxString report = wxFileName(game, options.output).GetFullPath() + ".json";
#ifdef __WXMSW__
	check(CreateHardLinkW(report.wc_str(), input.wc_str(), nullptr) != 0,
		  "Cannot create report hard-link fixture.");
#else
	check(link(input.fn_str(), report.fn_str()) == 0, "Cannot create report hard-link fixture.");
#endif
	check(!genghost_validate_options(options).empty(),
		  "A report hard link must preserve the input.");
	check(wxRemoveFile(report), "Cannot remove report fixture.");
#if defined(__UNIX__)
	const wxString symbolic = wxFileName(game, "symbolic.rpl").GetFullPath();
	check(symlink(input.fn_str(), symbolic.fn_str()) == 0, "Cannot create symbolic-link fixture.");
	options.output = symbolic;
	check(!genghost_validate_options(options).empty(),
		  "Symbolic output links must preserve the input.");
	check(wxRemoveFile(symbolic), "Cannot remove symbolic-link fixture.");
	options.output = "new replay & result.rpl";
#endif
	options.mode = GENGHOST_MODE_GEARS_ONLY;
#endif
	std::vector<legacy_u8> invalid_replay = replay();
	invalid_replay[offsetof(GAMEINFO, game_opponenttype)] = TEST_OPPONENT_TYPE;
	write_bytes(input, invalid_replay);
	check(!genghost_validate_options(options).empty(), "Fine-tuning must reject opponent replays.");
	invalid_replay = replay();
	invalid_replay.pop_back();
	write_bytes(input, invalid_replay);
	check(!genghost_validate_options(options).empty(), "Fine-tuning must reject truncated inputs.");
	write_bytes(input, replay());
	options.input = track;
	check(!genghost_validate_options(options).empty(),
		  "Fine-tuning must reject track-only inputs.");
	options.input = input;
	options.seconds = GENGHOST_MAX_SECONDS + 1;
	check(!genghost_validate_options(options).empty(),
		  "Budgets above the supported range must fail.");
	options.seconds = GENGHOST_DEFAULT_SECONDS;
	options.cores = GENGHOST_MAX_CORES + 1;
	check(!genghost_validate_options(options).empty(),
		  "Worker counts above the engine limit must fail.");
	options.cores = GENGHOST_MAX_CORES;
	options.output =
		wxFileName(wxFileName(game, "missing").GetFullPath(), "result.rpl").GetFullPath();
	check(!genghost_validate_options(options).empty(),
		  "Missing output directories must be rejected.");
	options.output = "new replay & result.rpl";
	check(genghost_validate_options(options).empty(),
		  "Valid fine-tune options must remain accepted.");

	const wxString settings = wxFileName(directory, "genghost.ini").GetFullPath();
	check(genghost_save_options(settings, options).empty(), "Could not save generator settings.");
	GenghostOptions restored;
	genghost_read_options(settings, restored, "different shared folder");
	check(restored.folder == options.folder && restored.input == options.input &&
			  restored.car == options.car && restored.output == options.output &&
			  restored.mode == options.mode && restored.seconds == options.seconds &&
			  restored.cores == options.cores && restored.strategy == options.strategy,
		  "Generator settings must round-trip independently of startup window defaults.");
	for (legacy_int index = 0; index < GENGHOST_PHYSICS_COUNT; ++index) {
		check(restored.physics[index] == options.physics[index], "Physics did not round-trip.");
	}
	wxString original;
	{
		wxFFile file(settings, "rb");
		file.ReadAll(&original);
	}
	genghost_read_options(settings, restored);
	{
		wxFFile file(settings, "rb");
		wxString after_read;
		file.ReadAll(&after_read);
		check(after_read == original, "Reading settings must not rewrite them.");
	}
	{
		wxFileConfig config(wxEmptyString, wxEmptyString, settings, wxEmptyString,
							wxCONFIG_USE_LOCAL_FILE);
		config.DisableAutoSave();
		config.Write("seconds", GENGHOST_MAX_SECONDS + 1);
		config.Write("cores", GENGHOST_MAX_CORES + 1);
		config.Write("mode", "future-mode");
		config.Write("strategy", "future-strategy");
		config.DeleteEntry(launcher_option_definitions[LAUNCHER_CHICANE_FAST_GRASS].key);
		config.Flush();
	}
	genghost_read_options(settings, restored);
	check(restored.seconds == GENGHOST_DEFAULT_SECONDS &&
			  restored.cores == GENGHOST_DEFAULT_CORES && restored.mode == GENGHOST_MODE_GENERATE &&
			  restored.strategy == GENGHOST_SEARCH_LEGACY &&
			  restored.physics[LAUNCHER_CHICANE_FAST_GRASS - LAUNCHER_POWER_GEAR] &&
			  restored.physics[PHYSICS_AMIGA_INDEX] && restored.physics[PHYSICS_OWOOT_INDEX],
		  "Invalid stored values must use defaults while preserving valid physics settings.");
	check(wxRemoveFile(settings), "Cannot remove settings fixture.");
	genghost_read_options(settings, restored, game);
	check(restored.folder == game && restored.input.empty() && restored.output.empty() &&
			  restored.seconds == GENGHOST_DEFAULT_SECONDS && !wxFileExists(settings),
		  "First launch must share the startup game folder without creating settings on read.");
	check(wxFileName(genghost_settings_path()).GetPath() ==
				  wxFileName(launcher_settings_path()).GetPath() &&
			  wxFileName(genghost_settings_path()).GetFullName() == "genghost.ini",
		  "Generator settings must live beside launcher.ini in their own file.");
	check(wxFileName::Rmdir(directory, wxPATH_RMDIR_RECURSIVE), "Cannot remove test fixtures.");
	return failures == 0 ? TEST_SUCCESS : TEST_FAILURE;
}
