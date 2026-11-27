#include <wx/wx.h>
#include <wx/checklst.h>
#include <wx/dirdlg.h>
#include <wx/filename.h>
#include <utility>
#include "../c/legacy.h"
#include "multi_replay_catalog.h"
#include "multi_replay_dialog.h"

static const legacy_int DIALOG_WIDTH = 660;
static const legacy_int DIALOG_HEIGHT = 420;
static const legacy_int MINIMUM_WIDTH = 410;
static const legacy_int MINIMUM_HEIGHT = 290;
static const legacy_int SCREEN_MARGIN = 40;
static const legacy_int BORDER = 12;
static const legacy_int SPACING = 8;
static const legacy_int FIXED_LAYOUT = 0;
static const legacy_int GROW_LAYOUT = 1;

class MultiReplayDialog : public wxDialog
{
	public:
	MultiReplayDialog(wxWindow *parent, const wxString &initial_folder)
		: wxDialog(parent, wxID_ANY, "Multi-replay", wxDefaultPosition, wxDefaultSize,
				   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	{
		const legacy_int border = FromDIP(BORDER);
		const legacy_int spacing = FromDIP(SPACING);
		wxBoxSizer *layout = new wxBoxSizer(wxVERTICAL);
		layout->Add(new wxStaticText(this, wxID_ANY, "Replay folder"), FIXED_LAYOUT,
					wxLEFT | wxRIGHT | wxTOP, border);
		wxBoxSizer *folder_row = new wxBoxSizer(wxHORIZONTAL);
		folder_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
								 wxTE_READONLY);
		folder_->SetName("Replay folder");
		folder_row->Add(folder_, GROW_LAYOUT, wxALIGN_CENTER_VERTICAL | wxRIGHT, spacing);
		wxButton *browse = new wxButton(this, wxID_OPEN, "&Browse...");
		folder_row->Add(browse, FIXED_LAYOUT, wxALIGN_CENTER_VERTICAL);
		layout->Add(folder_row, FIXED_LAYOUT, wxEXPAND | wxALL, border);

		wxBoxSizer *lists = new wxBoxSizer(wxHORIZONTAL);
		wxStaticBoxSizer *track_box = new wxStaticBoxSizer(wxVERTICAL, this, "Tracks");
		tracks_ = new wxListBox(track_box->GetStaticBox(), wxID_ANY, wxDefaultPosition,
								wxDefaultSize, 0, nullptr, wxLB_SINGLE);
		tracks_->SetName("Tracks");
		track_box->Add(tracks_, GROW_LAYOUT, wxEXPAND | wxALL, spacing);
		lists->Add(track_box, GROW_LAYOUT, wxEXPAND | wxRIGHT, spacing);
		wxStaticBoxSizer *replay_box = new wxStaticBoxSizer(wxVERTICAL, this, "Replays");
		select_all_ = new wxCheckBox(replay_box->GetStaticBox(), wxID_ANY, "Select all");
		replay_box->Add(select_all_, FIXED_LAYOUT, wxLEFT | wxRIGHT | wxTOP, spacing);
		replays_ = new wxCheckListBox(replay_box->GetStaticBox(), wxID_ANY);
		replays_->SetName("Replays");
		replay_box->Add(replays_, GROW_LAYOUT, wxEXPAND | wxALL, spacing);
		lists->Add(replay_box, GROW_LAYOUT, wxEXPAND);
		layout->Add(lists, GROW_LAYOUT, wxEXPAND | wxLEFT | wxRIGHT, border);

		status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
		layout->Add(status_, FIXED_LAYOUT, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, border);
		status_->Hide();
		layout->Add(CreateButtonSizer(wxOK | wxCANCEL), FIXED_LAYOUT, wxEXPAND | wxALL, border);
		start_ = static_cast<wxButton *>(FindWindow(wxID_OK));
		start_->SetLabel("&Start replay");
		start_->SetDefault();
		SetSizer(layout);
		const wxRect screen = wxGetClientDisplayRect();
		const legacy_int width =
			wxMin(FromDIP(DIALOG_WIDTH), screen.width - FromDIP(SCREEN_MARGIN));
		const legacy_int height =
			wxMin(FromDIP(DIALOG_HEIGHT), screen.height - FromDIP(SCREEN_MARGIN));
		SetMinClientSize(
			wxSize(wxMin(width, FromDIP(MINIMUM_WIDTH)), wxMin(height, FromDIP(MINIMUM_HEIGHT))));
		SetClientSize(width, height);
		CentreOnParent();

		browse->Bind(wxEVT_BUTTON, &MultiReplayDialog::browse_folder, this);
		tracks_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &) { show_track_replays(); });
		select_all_->Bind(wxEVT_CHECKBOX, &MultiReplayDialog::toggle_replays, this);
		replays_->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent &) { update_selection(); });
		Bind(wxEVT_BUTTON, &MultiReplayDialog::start_replay, this, wxID_OK);

		wxFileName directory = wxFileName::DirName(initial_folder);
		if (!initial_folder.empty() && directory.MakeAbsolute() && directory.DirExists()) {
			scan_folder(directory.GetPath());
		} else {
			folder_status_ = "Choose a folder containing replay files.";
			show_track_replays();
		}
		browse->SetFocus();
	}

	std::vector<wxString> take_selection()
	{
		return std::move(selection_);
	}

	private:
	void set_status(const wxString &message)
	{
		status_->SetLabel(message);
		status_->Wrap(FromDIP(MINIMUM_WIDTH - 2 * BORDER));
		status_->Show(!message.empty());
		Layout();
	}

	void browse_folder(wxCommandEvent &)
	{
		wxDirDialog chooser(this, "Choose a replay folder", folder_->GetValue(),
							wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
		if (chooser.ShowModal() == wxID_OK) {
			scan_folder(chooser.GetPath());
		}
	}

	void scan_folder(const wxString &folder)
	{
		wxBusyCursor busy;
		wxEventBlocker blocker(tracks_, wxEVT_LISTBOX);
		folder_->SetValue(folder);
		tracks_->Clear();
		const wxString error = launcher_scan_multi_replays(folder, catalog_);
		for (const MultiReplayTrack &track : catalog_.tracks) {
			tracks_->Append(wxString::Format("%s (%u)", track.name,
											 static_cast<legacy_uint>(track.replays.size())));
		}
		tracks_->SetSelection(wxNOT_FOUND);
		wxString status = error;
		if (status.empty() && catalog_.tracks.empty()) {
			status = "No playable replays were found in this folder.";
		}
		if (error.empty() && catalog_.invalid_files != 0U) {
			if (!status.empty()) {
				status += '\n';
			}
			const auto count = static_cast<legacy_uint>(catalog_.invalid_files);
			status += wxString::Format("Skipped %u unreadable or unsupported replay file%s.", count,
									   count == 1U ? "" : "s");
		}
		folder_status_ = status;
		show_track_replays();
	}

	const MultiReplayTrack *selected_track() const
	{
		const legacy_int index = tracks_->GetSelection();
		return index == wxNOT_FOUND ? nullptr : &catalog_.tracks[index];
	}

	void show_track_replays()
	{
		wxEventBlocker blocker(replays_, wxEVT_CHECKLISTBOX);
		replays_->Clear();
		const MultiReplayTrack *track = selected_track();
		if (track != nullptr) {
			for (const MultiReplayFile &replay : track->replays) {
				replays_->Append(replay.name);
			}
		}
		const auto has_replays = !replays_->IsEmpty();
		replays_->Enable(has_replays);
		select_all_->Enable(has_replays);
		update_selection();
	}

	void toggle_replays(wxCommandEvent &event)
	{
		wxEventBlocker blocker(replays_, wxEVT_CHECKLISTBOX);
		for (legacy_uint index = 0; index < replays_->GetCount(); ++index) {
			replays_->Check(index, event.IsChecked());
		}
		update_selection();
	}

	void update_selection()
	{
		wxArrayInt checked;
		const legacy_uint count = replays_->GetCheckedItems(checked);
		select_all_->SetValue(count != 0U && count == replays_->GetCount());
		start_->Enable(count != 0U);
		set_status(folder_status_);
	}

	void start_replay(wxCommandEvent &)
	{
		const MultiReplayTrack *track = selected_track();
		wxArrayInt checked;
		if (track == nullptr || replays_->GetCheckedItems(checked) == 0U) {
			return;
		}
		std::vector<wxString> selected;
		for (legacy_int index : checked) {
			selected.push_back(track->replays[index].path);
		}
		selection_ = std::move(selected);
		EndModal(wxID_OK);
	}

	MultiReplayCatalog catalog_;
	std::vector<wxString> selection_;
	wxString folder_status_;
	wxTextCtrl *folder_;
	wxListBox *tracks_;
	wxCheckBox *select_all_;
	wxCheckListBox *replays_;
	wxStaticText *status_;
	wxButton *start_;
};

std::vector<wxString> launcher_choose_multi_replays(wxWindow *parent, const wxString &folder)
{
	MultiReplayDialog dialog(parent, folder);
	return dialog.ShowModal() == wxID_OK ? dialog.take_selection() : std::vector<wxString>();
}
