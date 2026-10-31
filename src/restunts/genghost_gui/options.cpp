#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <algorithm>
#include <cstddef>
#include <set>
#ifdef __WXMSW__
#include <windows.h>
#endif
#include "options.h"
extern "C" {
#include "../c/csrpl.h"
}
#include "../genghost/simulation.h"

namespace
{

static const legacy_char SETTINGS_FILENAME[] = "genghost.ini";
static const legacy_char FOLDER_KEY[] = "folder";
static const legacy_char INPUT_KEY[] = "input";
static const legacy_char CAR_KEY[] = "car";
static const legacy_char OUTPUT_KEY[] = "output";
static const legacy_char MODE_KEY[] = "mode";
static const legacy_char SECONDS_KEY[] = "seconds";
static const legacy_char CORES_KEY[] = "cores";
static const legacy_char STRATEGY_KEY[] = "strategy";
static const legacy_char CAR_PREFIX[] = "CAR";
static const legacy_char CAR_DIRECTORY[] = "cars";
static const legacy_char REPORT_SUFFIX[] = ".json";
static const legacy_char *const MODE_VALUES[GENGHOST_MODE_COUNT] = {"generate", "fine-tune",
																	"gears-only"};
static const legacy_char *const STRATEGY_VALUES[GENGHOST_SEARCH_COUNT] = {"legacy", "state"};
static const legacy_uint GENERATION_PREFIX_LENGTH = 3U;
static const legacy_uint FINE_TUNE_PREFIX_LENGTH = 5U;
static const legacy_uint CAR_PREFIX_LENGTH = sizeof(CAR_PREFIX) - 1U;
static const legacy_uint REPLAY_SLOW_FRAME_RATE = 10U;
static const legacy_uint TRANSMISSION_AUTOMATIC = 1U;
static const size_t EXTENDED_TRACK_MAX_BYTES =
	CSTRK_HEADER_SIZE + (CSTRK_MAX_DIMENSION + 1U) * (CSTRK_MAX_DIMENSION + 1U) +
	CSTRK_MAX_DIMENSION * CSTRK_MAX_DIMENSION * 2U + CSTRK_MAX_OBJECTS * CSTRK_OBJECT_SIZE +
	CSTRK_MAX_NOTES;
static const size_t EXTENDED_REPLAY_MAX_BYTES =
	CSRPL_HEADER_SIZE + EXTENDED_TRACK_MAX_BYTES + GENGHOST_MAX_FRAMES;

static legacy_bool extended_input(const wxString &path)
{
	const wxString extension = wxFileName(path).GetExt().Upper();
	return extension == "CSTRK" || extension == "CSRPL";
}

static legacy_bool read_extended(const wxString &path, std::vector<legacy_u8> &bytes)
{
	wxLogNull silence;
	wxFFile file(path, "rb");
	if (!file.IsOpened() || file.Length() <= 0 ||
		static_cast<legacy_u64>(file.Length()) > EXTENDED_REPLAY_MAX_BYTES) {
		return false;
	}
	bytes.resize(static_cast<size_t>(file.Length()));
	return file.Read(bytes.data(), bytes.size()) == bytes.size();
}

static legacy_bool read_extended_replay(const wxString &path, std::vector<legacy_u8> &bytes,
										CSRPL_VIEW &view)
{
	return read_extended(path, bytes) &&
		   csrpl_decode(bytes.data(), bytes.size(), &view, nullptr, 0) &&
		   view.metadata.frame_count <= GENGHOST_MAX_FRAMES;
}

static legacy_bool valid_car(const wxString &car)
{
	if (car.length() != REPLAY_CAR_ID_SIZE) {
		return false;
	}
	for (const wxUniChar letter : car) {
		if (!((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z') ||
			  (letter >= '0' && letter <= '9') || letter == '_')) {
			return false;
		}
	}
	return true;
}

static wxString absolute_path(const wxString &path, const wxString &folder)
{
	if (path.empty()) {
		return wxEmptyString;
	}
	wxFileName file(path);
	file.MakeAbsolute(folder);
	file.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
	return file.GetFullPath();
}

static wxString output_path(const GenghostOptions &options)
{
	return absolute_path(options.output, options.folder);
}

static legacy_bool same_file(const wxString &source, const wxString &destination)
{
	if (wxFileName(source).SameAs(wxFileName(destination))) {
		return true;
	}
#ifdef __WXMSW__
	/* wxWidgets compares inode identities only on Unix. Windows needs handles
	 * to detect hard links and symbolic links independently of path spelling. */
	const legacy_u32 sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
	HANDLE first = CreateFileW(source.wc_str(), FILE_READ_ATTRIBUTES, sharing, nullptr,
							   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	HANDLE second = CreateFileW(destination.wc_str(), FILE_READ_ATTRIBUTES, sharing, nullptr,
								OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	const legacy_u32 error = GetLastError();
	legacy_bool same = true;
	if (first != INVALID_HANDLE_VALUE && second != INVALID_HANDLE_VALUE) {
		BY_HANDLE_FILE_INFORMATION left, right;
		if (GetFileInformationByHandle(first, &left) &&
			GetFileInformationByHandle(second, &right)) {
			same = left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
				   left.nFileIndexHigh == right.nFileIndexHigh &&
				   left.nFileIndexLow == right.nFileIndexLow;
		}
	} else if (first != INVALID_HANDLE_VALUE &&
			   (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) {
		same = false;
	}
	if (first != INVALID_HANDLE_VALUE) {
		CloseHandle(first);
	}
	if (second != INVALID_HANDLE_VALUE) {
		CloseHandle(second);
	}
	return same;
#else
	return false;
#endif
}

static legacy_bool read_header(const wxString &path, legacy_u8 header[REPLAY_GAMEINFO_SIZE])
{
	wxLogNull silence;
	wxFFile file(path, "rb");
	return file.IsOpened() && file.Read(header, REPLAY_GAMEINFO_SIZE) == REPLAY_GAMEINFO_SIZE;
}

static legacy_u16 header_word(const legacy_u8 *header, legacy_uint offset)
{
	return LEGACY_READ_U16_LE(header + offset);
}

static wxString header_car(const legacy_u8 *header)
{
	wxString car;
	for (legacy_uint index = 0; index < REPLAY_CAR_ID_SIZE; ++index) {
		car += wxUniChar(header[index]);
	}
	return valid_car(car) ? car.Upper() : wxString();
}

static legacy_bool valid_replay(const wxString &path, legacy_bool fine_tune)
{
	if (wxFileName(path).GetExt().CmpNoCase("csrpl") == 0) {
		std::vector<legacy_u8> bytes;
		CSRPL_VIEW view;
		if (!read_extended_replay(path, bytes, view)) {
			return false;
		}
		return !fine_tune || (view.metadata.frame_count != 0 && view.metadata.opponent_type == 0 &&
							  !header_car(view.metadata.player_car_id).empty());
	}
	legacy_u8 header[REPLAY_GAMEINFO_SIZE];
	if (!read_header(path, header)) {
		return false;
	}
	const legacy_u16 frames = header_word(header, offsetof(GAMEINFO, game_recordedframes));
	const legacy_u16 rate = header_word(header, offsetof(GAMEINFO, game_framespersec));
	if (frames > GENGHOST_LEGACY_MAX_FRAMES ||
		wxFileName::GetSize(path) != REPLAY_INPUT_OFFSET + frames ||
		header[offsetof(GAMEINFO, game_playertransmission)] > TRANSMISSION_AUTOMATIC ||
		(rate != REPLAY_SLOW_FRAME_RATE && rate != GENGHOST_FRAME_RATE)) {
		return false;
	}
	return !fine_tune ||
		   (frames != 0 && rate == GENGHOST_FRAME_RATE &&
			header[offsetof(GAMEINFO, game_opponenttype)] == 0 && !header_car(header).empty());
}

static legacy_int read_number(wxFileConfig &config, const legacy_char *key, legacy_int initial,
							  legacy_int maximum)
{
	const legacy_s64 value = config.ReadLong(key, initial);
	return value >= 0 && value <= maximum ? static_cast<legacy_int>(value) : initial;
}

static legacy_int read_selection(wxFileConfig &config, const legacy_char *key,
								 const legacy_char *const *values, legacy_int count)
{
	wxString value;
	config.Read(key, &value);
	for (legacy_int index = 0; index < count; ++index) {
		if (value == values[index]) {
			return index;
		}
	}
	return 0;
}

static wxString collect_directory(const wxString &path, legacy_bool required,
								  std::vector<wxString> *inputs, std::set<wxString> &cars)
{
	if (!required && !wxDirExists(path)) {
		return wxEmptyString;
	}
	if (!wxFileName::IsDirReadable(path)) {
		return "Cannot read files in: " + path;
	}
	wxDir directory(path);
	if (!directory.IsOpened()) {
		return "Cannot read files in: " + path;
	}
	wxString name;
	for (legacy_bool more = directory.GetFirst(&name, wxEmptyString, wxDIR_FILES); more;
		 more = directory.GetNext(&name)) {
		const wxFileName file(path, name);
		const wxString extension = file.GetExt().Upper();
		if (inputs != nullptr && (extension == "TRK" || extension == "RPL" ||
								  extension == "CSTRK" || extension == "CSRPL")) {
			inputs->push_back(absolute_path(file.GetFullPath(), wxEmptyString));
		}
		const wxString stem = file.GetName().Upper();
		if ((extension == "RES" || extension == "PRE") && stem.StartsWith(CAR_PREFIX) &&
			stem.length() == CAR_PREFIX_LENGTH + REPLAY_CAR_ID_SIZE) {
			const wxString car = stem.Mid(CAR_PREFIX_LENGTH);
			if (valid_car(car)) {
				cars.insert(car);
			}
		}
	}
	return wxEmptyString;
}

} // namespace

const LauncherOption genghost_physics_options[GENGHOST_PHYSICS_COUNT] = {
	LAUNCHER_POWER_GEAR,
	LAUNCHER_LEGACY_COLLISION,
	LAUNCHER_LEFT_CORNER_BIAS,
	LAUNCHER_DUAL_WAY_SWITCHING,
	LAUNCHER_CHICANE_FAST_GRASS,
	LAUNCHER_AMIGA_GRASS,
	LAUNCHER_OWOOT};

GenghostOptions::GenghostOptions()
	: mode(GENGHOST_MODE_GENERATE), seconds(GENGHOST_DEFAULT_SECONDS),
	  cores(GENGHOST_DEFAULT_CORES), strategy(GENGHOST_SEARCH_LEGACY)
{
	for (legacy_int index = 0; index < GENGHOST_PHYSICS_COUNT; ++index) {
		physics[index] = launcher_option_definitions[genghost_physics_options[index]].initial;
	}
}

wxString genghost_settings_path()
{
	wxFileName path(launcher_settings_path());
	if (path.GetFullPath().empty()) {
		return wxEmptyString;
	}
	path.SetFullName(SETTINGS_FILENAME);
	return path.GetFullPath();
}

void genghost_read_options(const wxString &path, GenghostOptions &options,
						   const wxString &shared_folder)
{
	options = GenghostOptions();
	options.folder = shared_folder;
	if (path.empty()) {
		return;
	}
	wxFileConfig config(wxEmptyString, wxEmptyString, path, wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
	config.DisableAutoSave();
	config.Read(FOLDER_KEY, &options.folder, shared_folder);
	config.Read(INPUT_KEY, &options.input);
	config.Read(CAR_KEY, &options.car);
	config.Read(OUTPUT_KEY, &options.output);
	options.seconds =
		read_number(config, SECONDS_KEY, GENGHOST_DEFAULT_SECONDS, GENGHOST_MAX_SECONDS);
	options.cores = read_number(config, CORES_KEY, GENGHOST_DEFAULT_CORES, GENGHOST_MAX_CORES);
	options.mode = read_selection(config, MODE_KEY, MODE_VALUES, GENGHOST_MODE_COUNT);
	options.strategy = read_selection(config, STRATEGY_KEY, STRATEGY_VALUES, GENGHOST_SEARCH_COUNT);
	for (legacy_int index = 0; index < GENGHOST_PHYSICS_COUNT; ++index) {
		const LauncherOptionDefinition &definition =
			launcher_option_definitions[genghost_physics_options[index]];
		options.physics[index] = config.ReadBool(definition.key, definition.initial);
	}
}

wxString genghost_save_options(const wxString &path, const GenghostOptions &options)
{
	if (path.empty()) {
		return "Cannot locate your settings directory.";
	}
	const wxFileName settings(path);
	if (!wxDirExists(settings.GetPath()) &&
		!wxFileName::Mkdir(settings.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
		return "Cannot create your settings directory: " + settings.GetPath();
	}
	wxFileConfig config(wxEmptyString, wxEmptyString, path, wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
	config.DisableAutoSave();
	legacy_bool written = config.Write(FOLDER_KEY, options.folder);
	written = config.Write(INPUT_KEY, options.input) && written;
	written = config.Write(CAR_KEY, options.car) && written;
	written = config.Write(OUTPUT_KEY, options.output) && written;
	written = config.Write(SECONDS_KEY, options.seconds) && written;
	written = config.Write(CORES_KEY, options.cores) && written;
	const legacy_int mode = options.mode >= 0 && options.mode < GENGHOST_MODE_COUNT
								? options.mode
								: GENGHOST_MODE_GENERATE;
	const legacy_int strategy = options.strategy >= 0 && options.strategy < GENGHOST_SEARCH_COUNT
									? options.strategy
									: GENGHOST_SEARCH_LEGACY;
	written = config.Write(MODE_KEY, MODE_VALUES[mode]) && written;
	written = config.Write(STRATEGY_KEY, STRATEGY_VALUES[strategy]) && written;
	for (legacy_int index = 0; index < GENGHOST_PHYSICS_COUNT; ++index) {
		written = config.Write(launcher_option_definitions[genghost_physics_options[index]].key,
							   options.physics[index]) &&
				  written;
	}
	if (!written || !config.Flush()) {
		return "Cannot save your settings: " + path;
	}
	return wxEmptyString;
}

wxString genghost_scan_catalog(const wxString &folder, const wxString &assets,
							   GenghostCatalog &catalog)
{
	catalog = GenghostCatalog();
	std::set<wxString> cars;
	wxString error = collect_directory(folder, true, &catalog.inputs, cars);
	if (!error.empty()) {
		return error;
	}
	error =
		collect_directory(wxFileName(folder, CAR_DIRECTORY).GetFullPath(), false, nullptr, cars);
	if (!error.empty()) {
		return error;
	}
	if (!assets.empty()) {
		error = collect_directory(wxFileName(assets, CAR_DIRECTORY).GetFullPath(), false, nullptr,
								  cars);
		if (!error.empty()) {
			return error;
		}
	}
	std::sort(catalog.inputs.begin(), catalog.inputs.end(),
			  [](const wxString &left, const wxString &right) {
				  const legacy_int comparison = left.CmpNoCase(right);
				  return comparison != 0 ? comparison < 0 : left < right;
			  });
	catalog.cars.assign(cars.begin(), cars.end());
	return wxEmptyString;
}

wxString genghost_resolved_input(const GenghostOptions &options)
{
	return absolute_path(options.input, options.folder);
}

wxString genghost_resolved_car(const GenghostOptions &options)
{
	if (options.mode == GENGHOST_MODE_GENERATE) {
		return valid_car(options.car) ? options.car.Upper() : wxString();
	}
	const wxString path = genghost_resolved_input(options);
	if (wxFileName(path).GetExt().CmpNoCase("csrpl") == 0) {
		std::vector<legacy_u8> bytes;
		CSRPL_VIEW view;
		return read_extended_replay(path, bytes, view) ? header_car(view.metadata.player_car_id)
													   : wxString();
	}
	legacy_u8 header[REPLAY_GAMEINFO_SIZE];
	return read_header(path, header) ? header_car(header) : wxString();
}

legacy_bool genghost_apply_replay_physics(GenghostOptions &options)
{
	const wxString input = genghost_resolved_input(options);
	if (wxFileName(input).GetExt().CmpNoCase("csrpl") != 0) {
		return false;
	}
	std::vector<legacy_u8> bytes;
	CSRPL_VIEW view;
	if (!read_extended_replay(input, bytes, view)) {
		return false;
	}
	static const legacy_u16 rules[GENGHOST_PHYSICS_COUNT] = {DRIVING_RULE_POWER_GEAR,
															 DRIVING_RULE_LEGACY_COLLISION,
															 DRIVING_RULE_LEFT_CORNER_BIAS,
															 DRIVING_RULE_DUAL_WAY_SWITCHING,
															 DRIVING_RULE_CHICANE_FAST_GRASS,
															 DRIVING_RULE_AMIGA_GRASS,
															 DRIVING_RULE_OWOOT};
	for (legacy_uint index = 0; index < GENGHOST_PHYSICS_COUNT; index++) {
		options.physics[index] = (view.metadata.driving_rules & rules[index]) != 0;
	}
	return true;
}

wxString genghost_default_output(const GenghostOptions &options)
{
	const legacy_bool fine_tune = options.mode != GENGHOST_MODE_GENERATE;
	const legacy_uint maximum = fine_tune ? FINE_TUNE_PREFIX_LENGTH : GENERATION_PREFIX_LENGTH;
	const wxString name = wxFileName(options.input).GetName().Upper();
	wxString prefix;
	for (const wxUniChar letter : name.Left(maximum)) {
		prefix += (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9')
					  ? letter
					  : wxUniChar('_');
	}
	if (prefix.empty()) {
		prefix = wxString("TRACK").Left(maximum);
	}
	const wxString extension = extended_input(options.input) ? ".csrpl" : ".rpl";
	const wxString suffix = fine_tune ? "-FT" + extension : "-" + options.car.Upper() + extension;
	return absolute_path(prefix + suffix, options.folder);
}

wxString genghost_validate_options(const GenghostOptions &options)
{
	if (options.mode < 0 || options.mode >= GENGHOST_MODE_COUNT) {
		return "Choose a search mode.";
	}
	if (options.seconds < 0 || options.seconds > GENGHOST_MAX_SECONDS) {
		return "Choose a budget from 0 to 86400 seconds; 0 searches until stopped.";
	}
	if (options.cores < 0 || options.cores > GENGHOST_MAX_CORES) {
		return "Choose 1 to 256 cores, or 0 for all available cores.";
	}
	if (options.strategy < 0 || options.strategy >= GENGHOST_SEARCH_COUNT) {
		return "Choose a search method.";
	}
	if (options.folder.empty() || !wxFileName::IsDirReadable(options.folder)) {
		return "Choose a readable game folder.";
	}
	const legacy_bool fine_tune = options.mode != GENGHOST_MODE_GENERATE;
	if (!fine_tune && !valid_car(options.car)) {
		return "Choose a four-character car code, such as COUN or DIA3.";
	}
	const wxString input = genghost_resolved_input(options);
	if (input.empty() || !wxFileName::IsFileReadable(input)) {
		return "Choose a readable track or replay.";
	}
	const wxString extension = wxFileName(input).GetExt().Upper();
	if (fine_tune) {
		if ((extension != "RPL" && extension != "CSRPL") || !valid_replay(input, true)) {
			return "Fine-tuning requires a complete solo .CSRPL or 20 Hz .RPL replay with a valid "
				   "car code.";
		}
	} else {
		legacy_bool valid = false;
		if (extension == "TRK") {
			valid = wxFileName::GetSize(input) == REPLAY_TRACK_SIZE;
		} else if (extension == "RPL" || extension == "CSRPL") {
			valid = valid_replay(input, false);
		} else if (extension == "CSTRK") {
			std::vector<legacy_u8> bytes;
			valid = read_extended(input, bytes) &&
					cstrk_validate_bytes(bytes.data(), bytes.size(), nullptr, 0);
		}
		if (!valid) {
			return "Choose a complete .TRK/.CSTRK track or .RPL/.CSRPL replay.";
		}
	}
	const wxString output = output_path(options);
	const wxString expected_extension = extended_input(input) ? "csrpl" : "rpl";
	if (output.empty() || wxFileName(output).GetExt().CmpNoCase(expected_extension) != 0) {
		return "Choose an output replay filename ending in ." + expected_extension + ".";
	}
	const wxString report = output + REPORT_SUFFIX;
	if (same_file(input, output) || same_file(input, report)) {
		return "Choose a different output filename to preserve your input file.";
	}
	if (wxDirExists(output) || wxDirExists(report) ||
		!wxFileName::IsDirWritable(wxFileName(output).GetPath()) ||
		(wxFileExists(output) && !wxFileName::IsFileWritable(output)) ||
		(wxFileExists(report) && !wxFileName::IsFileWritable(report))) {
		return "Choose a writable output location for the replay and its .json report.";
	}
	return wxEmptyString;
}

std::vector<wxString> genghost_arguments(const wxString &executable, const GenghostOptions &options)
{
	std::vector<wxString> arguments = {executable, "--data-dir",
									   absolute_path(options.folder, wxEmptyString),
									   genghost_resolved_input(options)};
	if (options.mode == GENGHOST_MODE_GENERATE) {
		arguments.push_back(options.car.Upper());
	} else {
		arguments.push_back(options.mode == GENGHOST_MODE_GEARS_ONLY ? "--fine-tune-gears-only"
																	 : "--fine-tune");
	}
	arguments.push_back("--seconds");
	arguments.push_back(wxString::Format("%d", options.seconds));
	arguments.push_back("--cores");
	arguments.push_back(wxString::Format("%d", options.cores));
	arguments.push_back("--strategy");
	arguments.push_back(
		STRATEGY_VALUES[options.strategy == GENGHOST_SEARCH_STATE ? GENGHOST_SEARCH_STATE
																  : GENGHOST_SEARCH_LEGACY]);
	arguments.push_back("--output");
	arguments.push_back(output_path(options));
	for (legacy_int index = 0; index < GENGHOST_PHYSICS_COUNT; ++index) {
		const LauncherOptionDefinition &definition =
			launcher_option_definitions[genghost_physics_options[index]];
		const legacy_char *argument =
			options.physics[index] ? definition.enabled_argument : definition.disabled_argument;
		if (argument == nullptr && genghost_physics_options[index] == LAUNCHER_OWOOT) {
			argument = "--owoot:off";
		} else if (argument == nullptr && genghost_physics_options[index] == LAUNCHER_AMIGA_GRASS) {
			argument = "--amigagrass:off";
		}
		if (argument != nullptr) {
			arguments.push_back(argument);
		}
	}
	return arguments;
}
