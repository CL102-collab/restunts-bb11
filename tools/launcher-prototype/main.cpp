#include <wx/wx.h>
#include <wx/dirdlg.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <vector>
#include "../../src/restunts/c/legacy.h"

/* wxWidgets virtual methods and its configuration API require the exact bool ABI. */
typedef bool legacy_bool;

static const legacy_char PROTOTYPE_TITLE[] = "Restunts launcher prototype";
static const legacy_char CONFIG_DIRECTORY[] = "Restunts";
static const legacy_char CONFIG_FILENAME[] = "launcher-prototype.ini";
static const legacy_char CONFIG_FOLDER_KEY[] = "folder";
static const legacy_char CONFIG_FULLSCREEN_KEY[] = "fullscreen";
static const legacy_char GAME_OPTION[] = "--game";
static const legacy_char DATA_DIRECTORY_OPTION[] = "--data-dir";
static const legacy_char FULLSCREEN_ON_OPTION[] = "--fullscreen:on";
static const legacy_char FULLSCREEN_OFF_OPTION[] = "--fullscreen:off";
#ifdef __WXMSW__
static const legacy_char PROBE_FILENAME[] = "launcher-probe-child.exe";
#else
static const legacy_char PROBE_FILENAME[] = "launcher-probe-child";
#endif

static const legacy_int FIRST_ARGUMENT_INDEX = 1;
static const legacy_int LAYOUT_FIXED = 0;
static const legacy_int LAYOUT_GROW = 1;
static const legacy_int OUTER_BORDER = 16;
static const legacy_int CONTROL_SPACING = 8;
static const legacy_int FOLDER_MINIMUM_WIDTH = 440;
static const legacy_int STATUS_WRAP_WIDTH = 560;
static const legacy_int PROTOTYPE_EXIT_SUCCESS = 0;
static const legacy_int PROTOTYPE_EXIT_FAILURE = 1;
static const legacy_int PROCESS_START_FAILURE = 0;
static const legacy_bool DEFAULT_FULLSCREEN = false;

class PrototypeDialog : public wxDialog
{
	public:
	PrototypeDialog(const wxString &target, const wxString &initial_folder, legacy_bool probe)
		: wxDialog(nullptr, wxID_ANY, PROTOTYPE_TITLE, wxDefaultPosition, wxDefaultSize,
				   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
		  target_(target), probe_(probe)
	{
		wxFileName config_directory =
			wxFileName::DirName(wxStandardPaths::Get().GetUserConfigDir());
		config_directory.AppendDir(CONFIG_DIRECTORY);
		config_path_ = wxFileName(config_directory.GetPath(), CONFIG_FILENAME).GetFullPath();
		wxFileConfig saved(wxEmptyString, wxEmptyString, config_path_, wxEmptyString,
						   wxCONFIG_USE_LOCAL_FILE);
		saved.DisableAutoSave();
		wxString folder;
		saved.Read(CONFIG_FOLDER_KEY, &folder, wxGetCwd());
		if (!initial_folder.empty()) {
			folder = initial_folder;
		}

		wxBoxSizer *layout = new wxBoxSizer(wxVERTICAL);
		layout->Add(new wxStaticText(this, wxID_ANY, "Game data folder:"), LAYOUT_FIXED,
					wxLEFT | wxRIGHT | wxTOP, OUTER_BORDER);
		wxBoxSizer *folder_row = new wxBoxSizer(wxHORIZONTAL);
		folder_ = new wxTextCtrl(this, wxID_ANY, folder);
		folder_->SetMinSize(wxSize(FOLDER_MINIMUM_WIDTH, wxDefaultCoord));
		folder_row->Add(folder_, LAYOUT_GROW, wxEXPAND | wxRIGHT, CONTROL_SPACING);
		wxButton *browse = new wxButton(this, wxID_OPEN, "&Browse...");
		folder_row->Add(browse, LAYOUT_FIXED, wxALIGN_CENTER_VERTICAL);
		layout->Add(folder_row, LAYOUT_FIXED, wxEXPAND | wxALL, OUTER_BORDER);

		fullscreen_ = new wxCheckBox(this, wxID_ANY, "&Fullscreen");
		fullscreen_->SetValue(saved.ReadBool(CONFIG_FULLSCREEN_KEY, DEFAULT_FULLSCREEN));
		layout->Add(fullscreen_, LAYOUT_FIXED, wxLEFT | wxRIGHT | wxBOTTOM, OUTER_BORDER);
		status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
		layout->Add(status_, LAYOUT_FIXED, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, OUTER_BORDER);
		layout->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), LAYOUT_FIXED, wxEXPAND | wxALL,
					OUTER_BORDER);
		wxButton *start = static_cast<wxButton *>(FindWindow(wxID_OK));
		start->SetLabel("&Start");
		start->SetDefault();
		SetSizer(layout);
		set_status(probe_ ? "The probe records the selected folder, fullscreen state and working "
							"directory. Settings are saved for your user when Start is pressed."
						  : "Start launches the selected game with --data-dir. Fullscreen is saved "
							"here; game fullscreen wiring is deferred in this prototype.");
		layout->SetSizeHints(this);
		Centre();
		folder_->SetFocus();
		browse->Bind(wxEVT_BUTTON, &PrototypeDialog::browse_folder, this);
		Bind(wxEVT_BUTTON, &PrototypeDialog::start_game, this, wxID_OK);
	}

	private:
	void set_status(const wxString &message)
	{
		status_->SetLabel(message);
		status_->Wrap(STATUS_WRAP_WIDTH);
		GetSizer()->Fit(this);
	}

	void browse_folder(wxCommandEvent &)
	{
		wxDirDialog chooser(this, "Choose the game data folder", folder_->GetValue(),
							wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
		if (chooser.ShowModal() == wxID_OK) {
			folder_->SetValue(chooser.GetPath());
		}
	}

	legacy_bool save_options(const wxString &folder)
	{
		wxFileName config(config_path_);
		if (!wxFileName::DirExists(config.GetPath()) &&
			!wxFileName::Mkdir(config.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
			set_status("Cannot create your settings directory: " + config.GetPath());
			return false;
		}
		wxFileConfig saved(wxEmptyString, wxEmptyString, config_path_, wxEmptyString,
						   wxCONFIG_USE_LOCAL_FILE);
		saved.DisableAutoSave();
		if (!saved.Write(CONFIG_FOLDER_KEY, folder) ||
			!saved.Write(CONFIG_FULLSCREEN_KEY, fullscreen_->GetValue()) || !saved.Flush()) {
			set_status("Cannot save your settings: " + config_path_);
			return false;
		}
		return true;
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
		if (!wxFileName::IsFileExecutable(target_)) {
			set_status("The executable is missing or cannot be launched: " + target_);
			return;
		}
		const wxString folder = directory.GetPath();
		if (!save_options(folder)) {
			return;
		}
		std::vector<wxString> arguments;
		arguments.push_back(target_);
		arguments.push_back(DATA_DIRECTORY_OPTION);
		arguments.push_back(folder);
		if (probe_) {
			arguments.push_back(fullscreen_->GetValue() ? FULLSCREEN_ON_OPTION
														: FULLSCREEN_OFF_OPTION);
		}
		std::vector<const wxChar *> argument_vector;
		for (const wxString &argument : arguments) {
			argument_vector.push_back(argument.c_str());
		}
		argument_vector.push_back(nullptr);
		wxExecuteEnv environment;
		environment.cwd = folder;
		const legacy_s64 process =
			wxExecute(argument_vector.data(), wxEXEC_ASYNC, nullptr, &environment);
		if (process == PROCESS_START_FAILURE) {
			set_status("Cannot start the executable: " + target_);
			return;
		}
		EndModal(wxID_OK);
	}

	wxString target_;
	wxString config_path_;
	legacy_bool probe_;
	wxTextCtrl *folder_;
	wxCheckBox *fullscreen_;
	wxStaticText *status_;
};

class PrototypeApp : public wxApp
{
	public:
	legacy_bool OnInit() override
	{
		SetAppName(PROTOTYPE_TITLE);
		for (legacy_int index = FIRST_ARGUMENT_INDEX; index < argc; index++) {
			const wxString argument = argv[index];
			if (argument == GAME_OPTION || argument == DATA_DIRECTORY_OPTION) {
				if (++index >= argc || wxString(argv[index]).empty()) {
					return argument_error("Missing value for " + argument);
				}
				if (argument == GAME_OPTION) {
					if (!target_.empty()) {
						return argument_error("Specify only one executable.");
					}
					target_ = argv[index];
				} else {
					initial_folder_ = argv[index];
				}
			} else if (argument.StartsWith("--")) {
				return argument_error("Unknown option: " + argument);
			} else if (target_.empty()) {
				target_ = argument;
			} else {
				return argument_error("Specify only one executable.");
			}
		}
		probe_ = target_.empty();
		if (probe_) {
			wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
			executable.SetFullName(PROBE_FILENAME);
			target_ = executable.GetFullPath();
		} else {
			wxFileName executable(target_);
			if (!executable.MakeAbsolute()) {
				return argument_error("Cannot resolve executable path: " + target_);
			}
			target_ = executable.GetFullPath();
		}
		return true;
	}

	legacy_int OnRun() override
	{
		PrototypeDialog dialog(target_, initial_folder_, probe_);
		SetTopWindow(&dialog);
		dialog.ShowModal();
		SetTopWindow(nullptr);
		return PROTOTYPE_EXIT_SUCCESS;
	}

	private:
	legacy_bool argument_error(const wxString &message)
	{
		SetErrorExitCode(PROTOTYPE_EXIT_FAILURE);
		wxMessageBox(message +
						 "\n\nUsage: launcher-prototype [--game EXECUTABLE] [--data-dir FOLDER]",
					 PROTOTYPE_TITLE, wxOK | wxICON_ERROR);
		return false;
	}

	wxString target_;
	wxString initial_folder_;
	legacy_bool probe_;
};

wxIMPLEMENT_APP(PrototypeApp);
