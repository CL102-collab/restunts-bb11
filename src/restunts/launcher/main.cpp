#include <wx/wx.h>
#include <wx/dirdlg.h>
#include <wx/filename.h>
#include <wx/scrolwin.h>
#include <wx/statbox.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include "options.h"
#include "game_data.h"

static const legacy_char LAUNCHER_TITLE[] = "Chocolate Stunts";
static const legacy_char LAUNCHER_APP_NAME[] = "Restunts";
static const legacy_char DATA_DIRECTORY_OPTION[] = "--data-dir";
static const legacy_char GAME_OPTION[] = "--game";
static const legacy_char WAIT_FOR_GAME_OPTION[] = "--wait-for-game";
#ifdef __WXMSW__
static const legacy_char GAME_FILENAME[] = "restunts.exe";
#else
static const legacy_char GAME_FILENAME[] = "restunts";
#endif

static const legacy_int FIRST_ARGUMENT = 1;
static const legacy_int LAUNCHER_SUCCESS = 0;
static const legacy_int PROCESS_START_FAILURE = 0;
static const legacy_int SYNCHRONOUS_PROCESS_START_FAILURE = -1;
static const legacy_int FIXED_LAYOUT = 0;
static const legacy_int GROW_LAYOUT = 1;
static const legacy_int OPTION_COLUMNS = 2;
static const legacy_int OUTER_BORDER = 16;
static const legacy_int CONTROL_SPACING = 8;
static const legacy_int SCROLL_STEP = 16;
static const legacy_int CONTENT_WIDTH = 560;
static const legacy_int MINIMUM_DIALOG_HEIGHT = 320;
static const legacy_int VISIBLE_SCREEN_NUMERATOR = 9;
static const legacy_int VISIBLE_SCREEN_DENOMINATOR = 10;

static wxString initial_folder(const wxString &game)
{
	const wxString current = wxGetCwd();
	const wxString game_directory = wxFileName(game).GetPath();
	const wxString candidates[] = {current, wxFileName(current, "stunts").GetFullPath(),
								   game_directory,
								   wxFileName(game_directory, "stunts").GetFullPath(),
								   wxFileName(game_directory, "../../stunts").GetFullPath()};
	for (const wxString &candidate : candidates) {
		if (launcher_validate_game_data(candidate).empty()) {
			return candidate;
		}
	}
	return current;
}

class LauncherDialog : public wxDialog
{
	public:
	LauncherDialog(const wxString &game, const wxString &folder_override, legacy_bool wait_for_game)
		: wxDialog(nullptr, wxID_ANY, LAUNCHER_TITLE, wxDefaultPosition, wxDefaultSize,
				   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
		  game_(game), settings_path_(launcher_settings_path()), wait_for_game_(wait_for_game)
	{
		{
			wxLogNull quiet;
			launcher_read_options(settings_path_, options_);
		}
		if (!folder_override.empty()) {
			options_.folder = folder_override;
		} else if (options_.folder.empty()) {
			options_.folder = initial_folder(game_);
		}
		const legacy_int border = FromDIP(OUTER_BORDER);
		const legacy_int spacing = FromDIP(CONTROL_SPACING);
		wxBoxSizer *layout = new wxBoxSizer(wxVERTICAL);
		wxScrolledWindow *content =
			new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
		content->SetScrollRate(FIXED_LAYOUT, FromDIP(SCROLL_STEP));
		wxBoxSizer *fields = new wxBoxSizer(wxVERTICAL);
		fields->Add(
			new wxStaticText(content, wxID_ANY, "Choose your Broderbund Stunts 1.1 game folder."),
			FIXED_LAYOUT, wxLEFT | wxRIGHT | wxTOP, border);
		wxBoxSizer *folder_row = new wxBoxSizer(wxHORIZONTAL);
		folder_ = new wxTextCtrl(content, wxID_ANY, options_.folder);
		folder_->SetName("Game data folder");
		folder_row->Add(folder_, GROW_LAYOUT, wxEXPAND | wxRIGHT, spacing);
		wxButton *browse = new wxButton(content, wxID_OPEN, "&Browse...");
		folder_row->Add(browse, FIXED_LAYOUT, wxALIGN_CENTER_VERTICAL);
		fields->Add(folder_row, FIXED_LAYOUT, wxEXPAND | wxALL, border);

		add_group(content, fields, "Startup options", LAUNCHER_SOUND, LAUNCHER_FULLSCREEN);
		wxStaticBoxSizer *graphics = new wxStaticBoxSizer(wxVERTICAL, content, "Graphics settings");
		wxFlexGridSizer *graphics_grid = new wxFlexGridSizer(OPTION_COLUMNS, spacing, border);
		add_option(graphics->GetStaticBox(), graphics_grid, LAUNCHER_FULLSCREEN);
		add_option(graphics->GetStaticBox(), graphics_grid, LAUNCHER_SHOW_FPS);
		graphics_grid->Add(new wxStaticText(graphics->GetStaticBox(), wxID_ANY, "HyperVision"),
						   FIXED_LAYOUT, wxALIGN_CENTER_VERTICAL);
		hypervision_ = new wxChoice(graphics->GetStaticBox(), wxID_ANY);
		for (const LauncherHyperVision &preset : launcher_hypervision_definitions) {
			hypervision_->Append(preset.label);
		}
		hypervision_->SetSelection(options_.hypervision);
		graphics_grid->Add(hypervision_, FIXED_LAYOUT, wxEXPAND);
		graphics->Add(graphics_grid, FIXED_LAYOUT, wxEXPAND | wxALL, spacing);
		fields->Add(graphics, FIXED_LAYOUT, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, border);
		add_group(content, fields, "Driving rules", LAUNCHER_POWER_GEAR, LAUNCHER_OPTION_COUNT);
		content->SetSizer(fields);
		content->FitInside();
		layout->Add(content, GROW_LAYOUT, wxEXPAND);
		status_ = new wxStaticText(this, wxID_ANY,
								   "Settings are saved for your user when you start the game.");
		status_->Wrap(FromDIP(CONTENT_WIDTH));
		layout->Add(status_, FIXED_LAYOUT, wxEXPAND | wxLEFT | wxRIGHT, border);
		layout->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), FIXED_LAYOUT, wxEXPAND | wxALL,
					border);
		wxButton *start = static_cast<wxButton *>(FindWindow(wxID_OK));
		start->SetLabel("&Start game");
		start->SetDefault();
		SetSizer(layout);
		const wxSize fields_minimum = fields->CalcMin();
		const wxSize footer_minimum = layout->CalcMin();
		const wxRect screen = wxGetClientDisplayRect();
		const legacy_int maximum_width =
			screen.width * VISIBLE_SCREEN_NUMERATOR / VISIBLE_SCREEN_DENOMINATOR;
		const legacy_int maximum_height =
			screen.height * VISIBLE_SCREEN_NUMERATOR / VISIBLE_SCREEN_DENOMINATOR;
		const legacy_int width =
			wxMin(wxMax(wxMax(fields_minimum.x, footer_minimum.x), FromDIP(CONTENT_WIDTH)),
				  maximum_width);
		const legacy_int height = wxMin(fields_minimum.y + footer_minimum.y, maximum_height);
		SetClientSize(width, height);
		SetMinSize(wxSize(width, wxMin(height, FromDIP(MINIMUM_DIALOG_HEIGHT))));
		Centre();
		folder_->SetFocus();
		browse->Bind(wxEVT_BUTTON, &LauncherDialog::browse_folder, this);
		Bind(wxEVT_BUTTON, &LauncherDialog::start_game, this, wxID_OK);
	}

	legacy_int game_exit_code() const
	{
		return game_exit_code_;
	}

	private:
	void add_option(wxWindow *parent, wxSizer *layout, legacy_int index)
	{
		checks_[index] = new wxCheckBox(parent, wxID_ANY, launcher_option_definitions[index].label);
		checks_[index]->SetValue(options_.enabled[index]);
		layout->Add(checks_[index], FIXED_LAYOUT, wxALIGN_CENTER_VERTICAL);
	}

	void add_group(wxWindow *parent, wxSizer *layout, const wxString &label, legacy_int first,
				   legacy_int end)
	{
		wxStaticBoxSizer *group = new wxStaticBoxSizer(wxVERTICAL, parent, label);
		wxFlexGridSizer *grid =
			new wxFlexGridSizer(OPTION_COLUMNS, FromDIP(CONTROL_SPACING), FromDIP(OUTER_BORDER));
		for (legacy_int index = first; index < end; ++index) {
			add_option(group->GetStaticBox(), grid, index);
		}
		group->Add(grid, FIXED_LAYOUT, wxEXPAND | wxALL, FromDIP(CONTROL_SPACING));
		layout->Add(group, FIXED_LAYOUT, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
					FromDIP(OUTER_BORDER));
	}

	void set_status(const wxString &message)
	{
		status_->SetLabel(message);
		status_->Wrap(FromDIP(CONTENT_WIDTH));
		Layout();
	}

	void browse_folder(wxCommandEvent &)
	{
		wxDirDialog chooser(this, "Choose the game data folder", folder_->GetValue(),
							wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
		if (chooser.ShowModal() == wxID_OK) {
			folder_->SetValue(chooser.GetPath());
			set_status("Press Start game to use the selected folder.");
		}
	}

	void start_game(wxCommandEvent &)
	{
		if (folder_->GetValue().empty()) {
			set_status("Choose a game data folder before starting.");
			return;
		}
		wxFileName directory = wxFileName::DirName(folder_->GetValue());
		if (!directory.MakeAbsolute() || !directory.DirExists()) {
			set_status("The selected game data folder does not exist.");
			return;
		}
		const wxString folder = directory.GetPath();
		const wxString validation = launcher_validate_game_data(folder);
		if (!validation.empty()) {
			set_status(validation);
			return;
		}
		if (!wxFileName::IsFileExecutable(game_)) {
			set_status("The game executable is missing or cannot be launched: " + game_);
			return;
		}
		options_.folder = folder;
		for (legacy_int index = 0; index < LAUNCHER_OPTION_COUNT; ++index) {
			options_.enabled[index] = checks_[index]->GetValue();
		}
		options_.hypervision = hypervision_->GetSelection();
		wxString save_error;
		{
			wxLogNull quiet;
			save_error = launcher_save_options(settings_path_, options_);
		}
		if (!save_error.empty()) {
			set_status(save_error);
			return;
		}
		const std::vector<wxString> arguments = launcher_game_arguments(game_, options_);
		std::vector<const wxChar *> argument_vector;
		for (const wxString &argument : arguments) {
			argument_vector.push_back(argument.c_str());
		}
		argument_vector.push_back(nullptr);
		wxExecuteEnv environment;
		environment.cwd = folder;
		legacy_s64 process;
		{
			wxLogNull quiet;
			if (wait_for_game_) {
				Hide();
			}
			process = wxExecute(argument_vector.data(), wait_for_game_ ? wxEXEC_SYNC : wxEXEC_ASYNC,
								nullptr, &environment);
		}
		if (process ==
			(wait_for_game_ ? SYNCHRONOUS_PROCESS_START_FAILURE : PROCESS_START_FAILURE)) {
			Show();
			set_status("Cannot start the game executable: " + game_);
			return;
		}
		if (wait_for_game_) {
			game_exit_code_ = static_cast<legacy_int>(process);
		}
		EndModal(wxID_OK);
	}

	wxString game_;
	wxString settings_path_;
	LauncherOptions options_;
	wxTextCtrl *folder_;
	wxChoice *hypervision_;
	wxCheckBox *checks_[LAUNCHER_OPTION_COUNT];
	wxStaticText *status_;
	legacy_bool wait_for_game_;
	legacy_int game_exit_code_ = LAUNCHER_SUCCESS;
};

class LauncherApp : public wxApp
{
	public:
	legacy_bool OnInit() override
	{
		SetAppName(LAUNCHER_APP_NAME);
		SetAppDisplayName(LAUNCHER_TITLE);
		for (legacy_int index = FIRST_ARGUMENT; index < argc; ++index) {
			const wxString argument = argv[index];
			if (argument == WAIT_FOR_GAME_OPTION) {
				if (wait_for_game_) {
					return argument_error("Specify " + argument + " only once.");
				}
				wait_for_game_ = true;
				continue;
			}
			if (argument != GAME_OPTION && argument != DATA_DIRECTORY_OPTION) {
				return argument_error("Unknown launcher option: " + argument);
			}
			if (++index >= argc || wxString(argv[index]).empty()) {
				return argument_error("Missing value for " + argument);
			}
			wxString &value = argument == GAME_OPTION ? game_ : folder_;
			if (!value.empty()) {
				return argument_error("Specify " + argument + " only once.");
			}
			value = argv[index];
		}
		wxFileName game;
		if (game_.empty()) {
			game = wxFileName(wxStandardPaths::Get().GetExecutablePath());
			game.SetFullName(GAME_FILENAME);
		} else {
			game = wxFileName(game_);
		}
		if (!game.MakeAbsolute()) {
			return argument_error("Cannot resolve the game executable: " + game.GetFullPath());
		}
		game_ = game.GetFullPath();
		return true;
	}

	legacy_int OnRun() override
	{
		LauncherDialog dialog(game_, folder_, wait_for_game_);
		SetTopWindow(&dialog);
		dialog.ShowModal();
		SetTopWindow(nullptr);
		return dialog.game_exit_code();
	}

	private:
	legacy_bool argument_error(const wxString &message)
	{
		wxMessageBox(message +
						 "\n\nUsage: restunts-launcher [--data-dir FOLDER] [--game EXECUTABLE] "
						 "[--wait-for-game]",
					 LAUNCHER_TITLE, wxOK | wxICON_ERROR);
		return false;
	}

	wxString game_;
	wxString folder_;
	legacy_bool wait_for_game_ = false;
};

wxIMPLEMENT_APP(LauncherApp);
