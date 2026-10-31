#include <wx/wx.h>
#include <wx/combobox.h>
#include <wx/dirdlg.h>
#include <wx/file.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/process.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stdpaths.h>
#include <wx/stopwatch.h>
#include <wx/timer.h>
#include <wx/wrapsizer.h>
#include "options.h"
#include "../launcher/game_data.h"
#include <string>

static const legacy_char WINDOW_TITLE[] = "Genghost - Build a faster ghost";
static const legacy_char APP_NAME[] = "Restunts";
#ifdef __WXMSW__
static const legacy_char GENERATOR_FILENAME[] = "genghost.exe";
#else
static const legacy_char GENERATOR_FILENAME[] = "genghost";
#endif
static const legacy_char *const MODE_LABELS[] = {"Generate replay", "Fine-tune replay",
												 "Fine-tune gears only"};
static const legacy_char *const STRATEGY_LABELS[] = {"Route search", "State search (preview)"};
static const legacy_int FIXED_LAYOUT = 0;
static const legacy_int GROW_LAYOUT = 1;
static const legacy_int BORDER = 16;
static const legacy_int SPACING = 8;
static const legacy_int FIELD_COLUMNS = 2;
static const legacy_int CONTENT_WIDTH = 720;
static const legacy_int WINDOW_HEIGHT = 850;
static const legacy_int LOG_HEIGHT = 180;
static const legacy_int SCROLL_STEP = 16;
static const legacy_int SCREEN_NUMERATOR = 9;
static const legacy_int SCREEN_DENOMINATOR = 10;
static const legacy_int POLL_MILLISECONDS = 100;
static const legacy_int MILLISECONDS_PER_SECOND = 1000;
static const legacy_int SECONDS_PER_MINUTE = 60;
static const legacy_int FIRST_ARGUMENT = 1;
static const legacy_int PROCESS_START_FAILURE = 0;
static const legacy_int PROCESS_SUCCESS = 0;
static const legacy_int PROCESS_NO_REPLAY = 2;
static const legacy_u32 STREAM_CHUNK = 4096;
static const legacy_u32 STREAM_POLL_LIMIT = 65536;
static const legacy_u32 LOG_LIMIT = 262144;

static wxString initial_folder(const wxString &executable)
{
	const wxString current = wxGetCwd();
	const wxString directory = wxFileName(executable).GetPath();
	const wxString candidates[] = {current, wxFileName(current, "stunts").GetFullPath(), directory,
								   wxFileName(directory, "stunts").GetFullPath(),
								   wxFileName(directory, "../../stunts").GetFullPath()};
	for (const wxString &candidate : candidates) {
		if (launcher_validate_game_data(candidate).empty()) {
			return candidate;
		}
	}
	return current;
}

static wxString report_contents(const wxString &path)
{
	wxFile file(path);
	wxString contents;
	if (file.IsOpened()) {
		file.ReadAll(&contents);
	}
	return contents;
}

class GenghostFrame : public wxFrame
{
	public:
	GenghostFrame(const wxString &generator, const wxString &folder)
		: wxFrame(nullptr, wxID_ANY, WINDOW_TITLE), generator_(generator), timer_(this)
	{
		LauncherOptions shared;
		{
			wxLogNull quiet;
			launcher_read_options(launcher_settings_path(), shared);
			genghost_read_options(genghost_settings_path(), options_, shared.folder);
		}
		automatic_output_ = genghost_default_output(options_);
		if (!folder.empty()) {
			options_.folder = folder;
		} else if (options_.folder.empty()) {
			options_.folder = initial_folder(generator_);
		}
		const legacy_int border = FromDIP(BORDER);
		const legacy_int spacing = FromDIP(SPACING);
		wxBoxSizer *layout = new wxBoxSizer(wxVERTICAL);
		fields_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
		fields_->SetScrollRate(FIXED_LAYOUT, FromDIP(SCROLL_STEP));
		wxBoxSizer *fields = new wxBoxSizer(wxVERTICAL);
		wxStaticText *intro = new wxStaticText(
			fields_, wxID_ANY,
			"Generate a ghost from a track and car, or fine-tune a completed replay using its "
			"recorded car. All generated replays use manual transmission.");
		intro->Wrap(FromDIP(CONTENT_WIDTH - BORDER - BORDER));
		fields->Add(intro, FIXED_LAYOUT, wxEXPAND | wxALL, border);

		wxStaticBoxSizer *source = new wxStaticBoxSizer(wxVERTICAL, fields_, "Game and input");
		wxWindow *source_parent = source->GetStaticBox();
		source->Add(new wxStaticText(source_parent, wxID_ANY, "Original Stunts 1.1 game folder"),
					FIXED_LAYOUT, wxLEFT | wxRIGHT | wxTOP, spacing);
		wxBoxSizer *folder_row = new wxBoxSizer(wxHORIZONTAL);
		folder_ = new wxTextCtrl(source_parent, wxID_ANY, options_.folder);
		folder_->SetName("Game data folder");
		folder_row->Add(folder_, GROW_LAYOUT, wxEXPAND | wxRIGHT, spacing);
		wxButton *browse_folder = new wxButton(source_parent, wxID_ANY, "Browse...");
		wxButton *refresh = new wxButton(source_parent, wxID_ANY, "Refresh");
		folder_row->Add(browse_folder, FIXED_LAYOUT, wxRIGHT, spacing);
		folder_row->Add(refresh, FIXED_LAYOUT);
		source->Add(folder_row, FIXED_LAYOUT, wxEXPAND | wxALL, spacing);
		source->Add(new wxStaticText(source_parent, wxID_ANY, "Track or replay"), FIXED_LAYOUT,
					wxLEFT | wxRIGHT, spacing);
		wxBoxSizer *input_row = new wxBoxSizer(wxHORIZONTAL);
		input_ = new wxComboBox(source_parent, wxID_ANY, options_.input);
		input_->SetName("Track or replay");
		input_row->Add(input_, GROW_LAYOUT, wxEXPAND | wxRIGHT, spacing);
		wxButton *browse_input = new wxButton(source_parent, wxID_ANY, "Browse...");
		input_row->Add(browse_input, FIXED_LAYOUT);
		source->Add(input_row, FIXED_LAYOUT, wxEXPAND | wxALL, spacing);
		fields->Add(source, FIXED_LAYOUT, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, border);

		wxStaticBoxSizer *search = new wxStaticBoxSizer(wxVERTICAL, fields_, "Search");
		wxWindow *search_parent = search->GetStaticBox();
		wxFlexGridSizer *grid = new wxFlexGridSizer(FIELD_COLUMNS, spacing, border);
		grid->AddGrowableCol(GROW_LAYOUT);
		mode_ = new wxChoice(search_parent, wxID_ANY);
		for (const legacy_char *label : MODE_LABELS) {
			mode_->Append(label);
		}
		mode_->SetSelection(options_.mode);
		add_field(grid, search_parent, "Search mode", mode_);
		car_ = new wxComboBox(search_parent, wxID_ANY, options_.car);
		car_->SetName("Car code");
		add_field(grid, search_parent, "Car (four-character code)", car_);
		seconds_ = new wxSpinCtrl(search_parent, wxID_ANY);
		seconds_->SetRange(FIXED_LAYOUT, GENGHOST_MAX_SECONDS);
		seconds_->SetValue(options_.seconds);
		add_field(grid, search_parent, "Search time (seconds; 0 = until stopped)", seconds_);
		cores_ = new wxSpinCtrl(search_parent, wxID_ANY);
		cores_->SetRange(FIXED_LAYOUT, GENGHOST_MAX_CORES);
		cores_->SetValue(options_.cores);
		add_field(grid, search_parent, "CPU cores (0 = all available)", cores_);
		strategy_ = new wxChoice(search_parent, wxID_ANY);
		for (const legacy_char *label : STRATEGY_LABELS) {
			strategy_->Append(label);
		}
		strategy_->SetSelection(options_.strategy);
		add_field(grid, search_parent, "Search method", strategy_);
		search->Add(grid, FIXED_LAYOUT, wxEXPAND | wxALL, spacing);
		wxStaticText *hint =
			new wxStaticText(search_parent, wxID_ANY,
							 "Fine-tuning uses the replay's car. Gears only changes shift timing. "
							 "More CPU cores use more memory.");
		hint->Wrap(FromDIP(CONTENT_WIDTH - BORDER - BORDER));
		search->Add(hint, FIXED_LAYOUT, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, spacing);
		fields->Add(search, FIXED_LAYOUT, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, border);

		wxStaticBoxSizer *rules = new wxStaticBoxSizer(wxVERTICAL, fields_, "Driving rules");
		wxFlexGridSizer *rule_grid = new wxFlexGridSizer(FIELD_COLUMNS, spacing, border);
		for (legacy_int index = 0; index < GENGHOST_PHYSICS_COUNT; ++index) {
			const LauncherOptionDefinition &definition =
				launcher_option_definitions[genghost_physics_options[index]];
			physics_[index] = new wxCheckBox(rules->GetStaticBox(), wxID_ANY, definition.label);
			physics_[index]->SetValue(options_.physics[index]);
			rule_grid->Add(physics_[index], FIXED_LAYOUT);
		}
		rules->Add(rule_grid, FIXED_LAYOUT, wxEXPAND | wxALL, spacing);
		rules->Add(new wxStaticText(rules->GetStaticBox(), wxID_ANY,
									"Use the same driving rules when playing the saved replay."),
				   FIXED_LAYOUT, wxLEFT | wxRIGHT | wxBOTTOM, spacing);
		fields->Add(rules, FIXED_LAYOUT, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, border);

		wxBoxSizer *output_row = new wxBoxSizer(wxHORIZONTAL);
		output_row->Add(new wxStaticText(fields_, wxID_ANY, "Save replay to"), FIXED_LAYOUT,
						wxALIGN_CENTER_VERTICAL | wxRIGHT, spacing);
		output_ = new wxTextCtrl(fields_, wxID_ANY, options_.output);
		output_->SetName("Output replay");
		output_row->Add(output_, GROW_LAYOUT, wxEXPAND | wxRIGHT, spacing);
		wxButton *browse_output = new wxButton(fields_, wxID_ANY, "Browse...");
		output_row->Add(browse_output, FIXED_LAYOUT);
		fields->Add(output_row, FIXED_LAYOUT, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, border);
		fields_->SetSizer(fields);
		fields_->FitInside();
		layout->Add(fields_, GROW_LAYOUT, wxEXPAND);

		wxWrapSizer *buttons = new wxWrapSizer(wxHORIZONTAL);
		generate_ = new wxButton(this, wxID_ANY, "Generate ghost");
		stop_ = new wxButton(this, wxID_ANY, "Stop and keep best replay");
		export_replay_ = new wxButton(this, wxID_ANY, "Export replay...");
		export_report_ = new wxButton(this, wxID_ANY, "Export report...");
		buttons->Add(generate_, FIXED_LAYOUT, wxRIGHT, spacing);
		buttons->Add(stop_, FIXED_LAYOUT, wxRIGHT, spacing);
		buttons->Add(export_replay_, FIXED_LAYOUT, wxRIGHT, spacing);
		buttons->Add(export_report_, FIXED_LAYOUT);
		layout->Add(buttons, FIXED_LAYOUT, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, border);
		status_ = new wxStaticText(this, wxID_ANY, "Choose your game folder and track or replay.");
		layout->Add(status_, FIXED_LAYOUT, wxEXPAND | wxALL, border);
		log_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
							  wxSize(FromDIP(CONTENT_WIDTH), FromDIP(LOG_HEIGHT)),
							  wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
		log_->SetFont(wxFontInfo().Family(wxFONTFAMILY_TELETYPE));
		log_->SetName("Search log");
		layout->Add(log_, FIXED_LAYOUT, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, border);
		SetSizer(layout);
		const wxSize screen = wxGetClientDisplayRect().GetSize();
		SetClientSize(wxSize(
			wxMin(FromDIP(CONTENT_WIDTH), screen.x * SCREEN_NUMERATOR / SCREEN_DENOMINATOR),
			wxMin(FromDIP(WINDOW_HEIGHT), screen.y * SCREEN_NUMERATOR / SCREEN_DENOMINATOR)));
		Centre();

		browse_folder->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { choose_folder(); });
		refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { refresh_catalog(); });
		browse_input->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { choose_input(); });
		browse_output->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { choose_output(); });
		input_->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { update_selection(); });
		mode_->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { update_selection(); });
		car_->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { update_selection(); });
		generate_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { start(); });
		stop_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { request_stop(); });
		export_replay_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { export_result(false); });
		export_report_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { export_result(true); });
		Bind(wxEVT_TIMER, &GenghostFrame::on_timer, this);
		Bind(wxEVT_END_PROCESS, &GenghostFrame::on_process_exit, this);
		Bind(wxEVT_CLOSE_WINDOW, &GenghostFrame::on_close, this);
		refresh_catalog();
		update_controls();
	}

	private:
	void add_field(wxSizer *grid, wxWindow *parent, const wxString &label, wxWindow *control)
	{
		grid->Add(new wxStaticText(parent, wxID_ANY, label), FIXED_LAYOUT, wxALIGN_CENTER_VERTICAL);
		grid->Add(control, GROW_LAYOUT, wxEXPAND);
	}

	GenghostOptions read_controls() const
	{
		GenghostOptions options = options_;
		options.folder = folder_->GetValue();
		if (!options.folder.empty()) {
			wxFileName directory = wxFileName::DirName(options.folder);
			directory.MakeAbsolute();
			options.folder = directory.GetPath();
		}
		options.input = input_->GetValue();
		options.car = car_->GetValue().Upper();
		options.output = output_->GetValue();
		options.mode = mode_->GetSelection();
		options.seconds = seconds_->GetValue();
		options.cores = cores_->GetValue();
		options.strategy = strategy_->GetSelection();
		for (legacy_int index = 0; index < GENGHOST_PHYSICS_COUNT; ++index) {
			options.physics[index] = physics_[index]->GetValue();
		}
		return options;
	}

	void set_status(const wxString &message)
	{
		status_->SetLabel(message);
		status_->Wrap(GetClientSize().x - FromDIP(BORDER + BORDER));
		Layout();
	}

	void refresh_catalog()
	{
		const wxString previous_input = input_->GetValue();
		const wxString previous_car = car_->GetValue();
		GenghostCatalog catalog;
		const wxString error =
			genghost_scan_catalog(folder_->GetValue(), wxFileName(generator_).GetPath(), catalog);
		input_->Freeze();
		input_->Clear();
		for (const wxString &path : catalog.inputs) {
			input_->Append(wxFileName(path).GetFullName());
		}
		input_->ChangeValue(previous_input.empty() && !catalog.inputs.empty()
								? wxFileName(catalog.inputs.front()).GetFullName()
								: previous_input);
		input_->Thaw();
		car_->Clear();
		for (const wxString &code : catalog.cars) {
			car_->Append(code);
		}
		car_->ChangeValue(previous_car.empty() && !catalog.cars.empty() ? catalog.cars.front()
																		: previous_car);
		update_selection();
		if (!error.empty()) {
			set_status(error);
		} else if (catalog.inputs.empty()) {
			set_status("No .TRK, .CSTRK, .RPL or .CSRPL files in this folder. Browse to a track or "
					   "replay.");
		} else {
			set_status("Ready. The best completed replay and its .json report are saved "
					   "as the search improves them.");
		}
	}

	void update_selection()
	{
		GenghostOptions options = read_controls();
		const wxString input = genghost_resolved_input(options);
		if (input != physics_source_ && genghost_apply_replay_physics(options)) {
			for (legacy_int index = 0; index < GENGHOST_PHYSICS_COUNT; index++) {
				physics_[index]->SetValue(options.physics[index]);
			}
			physics_source_ = input;
		}
		const legacy_bool fine_tune = options.mode != GENGHOST_MODE_GENERATE;
		car_->Enable(!fine_tune);
		if (fine_tune) {
			const wxString replay_car = genghost_resolved_car(options);
			car_->ChangeValue(replay_car);
		}
		if (output_->GetValue().empty() || output_->GetValue() == automatic_output_) {
			automatic_output_ = genghost_default_output(options);
			output_->ChangeValue(automatic_output_);
		}
		generate_->SetLabel(fine_tune ? "Fine-tune replay" : "Generate ghost");
	}

	void update_controls()
	{
		const legacy_bool running = process_ != nullptr;
		fields_->Enable(!running);
		generate_->Enable(!running);
		stop_->Enable(running && !stopping_);
		export_replay_->Enable(!running && result_available_ && wxFileExists(result_path_));
		export_report_->Enable(!running && result_available_ &&
							   wxFileExists(result_path_ + ".json"));
	}

	void choose_folder()
	{
		wxDirDialog dialog(this, "Choose your Stunts 1.1 game folder", folder_->GetValue(),
						   wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
		if (dialog.ShowModal() == wxID_OK) {
			folder_->ChangeValue(dialog.GetPath());
			output_->ChangeValue(wxEmptyString);
			refresh_catalog();
		}
	}

	void choose_input()
	{
		wxFileDialog dialog(
			this, "Choose a track or replay", folder_->GetValue(), wxEmptyString,
			"Tracks and replays|*.trk;*.TRK;*.rpl;*.RPL;*.cstrk;*.CSTRK;*.csrpl;*.CSRPL|All "
			"files|*",
			wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (dialog.ShowModal() == wxID_OK) {
			input_->SetValue(dialog.GetPath());
		}
	}

	void choose_output()
	{
		const wxFileName current(output_->GetValue());
		wxFileDialog dialog(
			this, "Save generated replay", current.GetPath(), current.GetFullName(),
			"Chocolate Stunts replays (*.csrpl)|*.csrpl|Stunts replays (*.rpl)|*.rpl",
			wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (dialog.ShowModal() == wxID_OK) {
			output_->ChangeValue(dialog.GetPath());
		}
	}

	void start()
	{
		options_ = read_controls();
		wxString error;
		{
			wxLogNull quiet;
			error = launcher_validate_game_data(options_.folder);
			if (error.empty()) {
				error = genghost_validate_options(options_);
			}
		}
		if (!error.empty()) {
			set_status(error);
			return;
		}
		if (!wxFileName::IsFileExecutable(generator_)) {
			set_status("Cannot launch the generator: " + generator_);
			return;
		}
		wxFileName output(options_.output);
		output.MakeAbsolute(options_.folder);
		const wxString destination = output.GetFullPath();
		if ((wxFileExists(destination) || wxFileExists(destination + ".json")) &&
			wxMessageBox("Replace the existing replay and report at:\n" + destination,
						 "Replace generated replay?", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION,
						 this) != wxYES) {
			return;
		}
		{
			wxLogNull quiet;
			error = genghost_save_options(genghost_settings_path(), options_);
		}
		if (!error.empty()) {
			set_status(error);
			return;
		}
		stop_path_ = wxFileName::CreateTempFileName("genghost-stop-");
		if (stop_path_.empty()) {
			set_status("Cannot create the search stop control file.");
			return;
		}
		std::vector<wxString> arguments = genghost_arguments(generator_, options_);
		arguments.push_back("--stop-file");
		arguments.push_back(stop_path_);
		std::vector<const wxChar *> argv;
		for (const wxString &argument : arguments) {
			argv.push_back(argument.c_str());
		}
		argv.push_back(nullptr);
		wxExecuteEnv environment;
		environment.cwd = options_.folder;
		process_ = new wxProcess(this);
		process_->Redirect();
		result_path_ = destination;
		{
			wxLogNull quiet;
			previous_report_ = report_contents(result_path_ + ".json");
		}
		log_->Clear();
		stdout_pending_.clear();
		stderr_pending_.clear();
		result_available_ = false;
		stopping_ = false;
		legacy_s64 pid;
		{
			wxLogNull quiet;
			pid =
				wxExecute(argv.data(), wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE, process_, &environment);
		}
		if (pid == PROCESS_START_FAILURE) {
			delete process_;
			process_ = nullptr;
			wxRemoveFile(stop_path_);
			stop_path_.clear();
			set_status("Cannot start the generator: " + generator_);
			update_controls();
			return;
		}
		elapsed_.Start();
		timer_.Start(POLL_MILLISECONDS);
		set_status("Searching. Improvements are saved automatically.");
		update_controls();
	}

	void request_stop()
	{
		if (!process_ || stopping_) {
			return;
		}
		wxFile file(stop_path_, wxFile::write);
		if (!file.IsOpened() || !file.Write("stop\n") || !file.Close()) {
			close_pending_ = false;
			set_status("Cannot request a stop. Check access to: " + stop_path_);
			return;
		}
		stopping_ = true;
		set_status("Stopping the search and preserving its best completed replay...");
		update_controls();
	}

	void append_log(const wxString &text)
	{
		log_->AppendText(text);
		if (static_cast<legacy_u32>(log_->GetLastPosition()) > LOG_LIMIT) {
			log_->Remove(FIXED_LAYOUT,
						 log_->GetLastPosition() - static_cast<legacy_int>(LOG_LIMIT));
		}
	}

	void drain_stream(wxInputStream *stream, std::string &pending, legacy_bool final)
	{
		legacy_char buffer[STREAM_CHUNK];
		legacy_u32 received = 0;
		while (stream && stream->CanRead() && received < STREAM_POLL_LIMIT) {
			stream->Read(buffer, STREAM_CHUNK);
			const legacy_u32 count = static_cast<legacy_u32>(stream->LastRead());
			if (!count) {
				break;
			}
			pending.append(buffer, count);
			received += count;
		}
		const std::string::size_type line_end = pending.find_last_of('\n');
		if (line_end != std::string::npos) {
			append_log(wxString::FromUTF8(pending.data(), line_end + GROW_LAYOUT));
			pending.erase(FIXED_LAYOUT, line_end + GROW_LAYOUT);
		}
		if (!pending.empty() && (final || pending.size() >= STREAM_POLL_LIMIT)) {
			append_log(wxString::FromUTF8(pending.data(), pending.size()));
			pending.clear();
		}
	}

	void drain_output(legacy_bool final)
	{
		drain_stream(process_->GetInputStream(), stdout_pending_, final);
		drain_stream(process_->GetErrorStream(), stderr_pending_, final);
	}

	void on_timer(wxTimerEvent &)
	{
		if (!process_) {
			return;
		}
		drain_output(false);
		if (!stopping_) {
			const legacy_s64 seconds = elapsed_.Time() / MILLISECONDS_PER_SECOND;
			set_status(wxString::Format("Searching: %lld:%02lld elapsed. Best replay: %s",
										static_cast<legacy_s64>(seconds / SECONDS_PER_MINUTE),
										static_cast<legacy_s64>(seconds % SECONDS_PER_MINUTE),
										result_path_));
		}
	}

	void on_process_exit(wxProcessEvent &event)
	{
		if (!process_) {
			return;
		}
		timer_.Stop();
		drain_output(true);
		delete process_;
		process_ = nullptr;
		wxRemoveFile(stop_path_);
		stop_path_.clear();
		const legacy_int status = event.GetExitCode();
		wxString report;
		{
			wxLogNull quiet;
			report = report_contents(result_path_ + ".json");
		}
		result_available_ = wxFileExists(result_path_) && !report.empty() &&
							(status == PROCESS_SUCCESS || report != previous_report_);
		if (result_available_) {
			set_status(
				(stopping_ ? "Stopped. Best replay saved: " : "Replay saved: ") + result_path_ +
				(status != PROCESS_SUCCESS && !stopping_ ? " (search failed; see log)" : ""));
		} else if (stopping_ || status == PROCESS_NO_REPLAY) {
			set_status(stopping_ ? "Stopped before finding a completed replay."
								 : "Search finished without a completed replay. Try more time or "
								   "another method.");
		} else {
			set_status(
				wxString::Format("Generator exited with status %d. See the search log.", status));
		}
		stopping_ = false;
		update_controls();
		if (close_pending_) {
			Destroy();
		}
	}

	void export_result(legacy_bool report)
	{
		const wxString source = result_path_ + (report ? ".json" : "");
		const wxFileName original(source);
		wxFileDialog dialog(
			this, report ? "Export driving-rule report" : "Export replay", wxEmptyString,
			original.GetFullName(),
			report ? "JSON reports (*.json)|*.json"
				   : "Chocolate Stunts replays (*.csrpl)|*.csrpl|Stunts replays (*.rpl)|*.rpl",
			wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (dialog.ShowModal() == wxID_OK) {
			wxFileName destination(dialog.GetPath());
			if (destination.SameAs(original)) {
				set_status("The result is already saved there: " + source);
			} else if (wxCopyFile(source, destination.GetFullPath(), true)) {
				set_status("Exported: " + destination.GetFullPath());
			} else {
				set_status("Cannot export to: " + destination.GetFullPath());
			}
		}
	}

	void on_close(wxCloseEvent &event)
	{
		if (process_) {
			if (event.CanVeto()) {
				event.Veto();
				close_pending_ = true;
				request_stop();
			} else {
				request_stop();
				process_->Detach();
				process_ = nullptr;
				Destroy();
			}
			return;
		}
		Destroy();
	}

	wxString generator_;
	GenghostOptions options_;
	wxScrolledWindow *fields_;
	wxTextCtrl *folder_;
	wxComboBox *input_;
	wxChoice *mode_;
	wxComboBox *car_;
	wxSpinCtrl *seconds_;
	wxSpinCtrl *cores_;
	wxChoice *strategy_;
	wxCheckBox *physics_[GENGHOST_PHYSICS_COUNT];
	wxTextCtrl *output_;
	wxButton *generate_;
	wxButton *stop_;
	wxButton *export_replay_;
	wxButton *export_report_;
	wxStaticText *status_;
	wxTextCtrl *log_;
	wxTimer timer_;
	wxStopWatch elapsed_;
	wxProcess *process_ = nullptr;
	wxString stop_path_;
	wxString automatic_output_;
	wxString physics_source_;
	wxString result_path_;
	wxString previous_report_;
	std::string stdout_pending_;
	std::string stderr_pending_;
	legacy_bool result_available_ = false;
	legacy_bool stopping_ = false;
	legacy_bool close_pending_ = false;
};

class GenghostApp : public wxApp
{
	public:
	legacy_bool OnInit() override
	{
		SetAppName(APP_NAME);
		SetAppDisplayName("Genghost");
		wxString generator;
		wxString folder;
		for (legacy_int index = FIRST_ARGUMENT; index < argc; ++index) {
			const wxString argument = argv[index];
			if (argument != "--data-dir" && argument != "--generator") {
				return argument_error("Unknown option: " + argument);
			}
			if (++index >= argc || wxString(argv[index]).empty()) {
				return argument_error("Missing value for " + argument);
			}
			wxString &value = argument == "--generator" ? generator : folder;
			if (!value.empty()) {
				return argument_error("Specify " + argument + " only once.");
			}
			value = argv[index];
		}
		wxFileName executable(generator.empty() ? wxStandardPaths::Get().GetExecutablePath()
												: generator);
		if (generator.empty()) {
			executable.SetFullName(GENERATOR_FILENAME);
		}
		if (!executable.MakeAbsolute()) {
			return argument_error("Cannot resolve the generator executable.");
		}
		GenghostFrame *frame = new GenghostFrame(executable.GetFullPath(), folder);
		SetTopWindow(frame);
		frame->Show();
		return true;
	}

	private:
	legacy_bool argument_error(const wxString &message)
	{
		wxMessageBox(message +
						 "\n\nUsage: genghost-gui [--data-dir FOLDER] [--generator EXECUTABLE]",
					 "Genghost", wxOK | wxICON_ERROR);
		return false;
	}
};

wxIMPLEMENT_APP(GenghostApp);
