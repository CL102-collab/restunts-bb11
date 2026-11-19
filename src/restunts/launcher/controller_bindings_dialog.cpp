#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <algorithm>
#include <vector>
#include "controller_bindings_dialog.h"

static const legacy_int DIALOG_WIDTH = 600;
static const legacy_int DIALOG_HEIGHT = 580;
static const legacy_int MINIMUM_WIDTH = 390;
static const legacy_int MINIMUM_HEIGHT = 280;
static const legacy_int SCREEN_MARGIN = 40;
static const legacy_int BORDER = 12;
static const legacy_int SPACING = 8;
static const legacy_int SCROLL_STEP = 20;
static const legacy_int FIXED_LAYOUT = 0;
static const legacy_int GROW = 1;
static const legacy_int BINDING_COLUMNS = 2;
static const legacy_int ACTION_COLUMN = 1;

class ControllerBindingsDialog : public wxDialog
{
	public:
	ControllerBindingsDialog(wxWindow *parent, const wxString &settings_path,
							 CONTROLLER_BINDINGS &bindings)
		: wxDialog(parent, wxID_ANY, "Controller bindings", wxDefaultPosition, wxDefaultSize,
				   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
		  settings_path_(settings_path), bindings_(bindings)
	{
		const legacy_int border = FromDIP(BORDER);
		const legacy_int spacing = FromDIP(SPACING);
		wxBoxSizer *layout = new wxBoxSizer(wxVERTICAL);
		wxNotebook *tabs = new wxNotebook(this, wxID_ANY);
		const legacy_char *const tab_names[CONTROLLER_CONTEXT_COUNT] = {"Racing", "Replay",
																		"Menus"};
		for (legacy_u8 context = 0; context < CONTROLLER_CONTEXT_COUNT; ++context) {
			add_tab(tabs, static_cast<CONTROLLER_CONTEXT>(context), tab_names[context]);
		}
		layout->Add(tabs, GROW, wxEXPAND | wxALL, border);

		status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
		layout->Add(status_, FIXED_LAYOUT, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, border);
		status_->Hide();

		wxBoxSizer *footer = new wxBoxSizer(wxHORIZONTAL);
		wxButton *restore = new wxButton(this, wxID_ANY, "Restore defaults");
		restore->SetToolTip("Restore the default bindings for all tabs.");
		footer->Add(restore, FIXED_LAYOUT, wxALIGN_CENTER_VERTICAL | wxRIGHT, spacing);
		footer->AddStretchSpacer();
		footer->Add(CreateButtonSizer(wxOK | wxCANCEL), FIXED_LAYOUT, wxALIGN_CENTER_VERTICAL);
		static_cast<wxButton *>(FindWindow(wxID_OK))->SetLabel("&Save");
		layout->Add(footer, FIXED_LAYOUT, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, border);

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

		select_bindings(bindings_);
		restore->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
			CONTROLLER_BINDINGS defaults;
			controller_bindings_reset(&defaults);
			select_bindings(defaults);
		});
		Bind(wxEVT_BUTTON, &ControllerBindingsDialog::save, this, wxID_OK);
	}

	private:
	struct BindingChoice {
		wxChoice *widget;
		std::vector<legacy_u8> actions;
	};

	void add_tab(wxNotebook *tabs, CONTROLLER_CONTEXT context, const legacy_char *name)
	{
		const legacy_int spacing = FromDIP(SPACING);
		wxScrolledWindow *page =
			new wxScrolledWindow(tabs, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
		page->SetScrollRate(FIXED_LAYOUT, FromDIP(SCROLL_STEP));
		wxFlexGridSizer *grid = new wxFlexGridSizer(BINDING_COLUMNS, spacing, spacing);
		grid->AddGrowableCol(ACTION_COLUMN);
		/* Keep triggers and stick movements above the optional button rows. */
		for (legacy_u8 row = 0; row < CONTROLLER_CONTROL_COUNT; ++row) {
			const legacy_u8 control =
				(row + CONTROLLER_CONTROL_LEFT_TRIGGER) % CONTROLLER_CONTROL_COUNT;
			const wxString label = wxString::FromUTF8(controller_binding_control_labels[control]);
			grid->Add(new wxStaticText(page, wxID_ANY, label), FIXED_LAYOUT,
					  wxALIGN_CENTER_VERTICAL);
			BindingChoice &choice = choices_[context][control];
			choice.widget = new wxChoice(page, wxID_ANY);
			choice.widget->SetName(label);
			choice.widget->Bind(wxEVT_MOUSEWHEEL, [this, page, context](wxMouseEvent &event) {
				scroll_page(page, context, event);
			});
			for (legacy_u8 action = 0; action < CONTROLLER_ACTION_COUNT; ++action) {
				if (controller_binding_allowed(context, static_cast<CONTROLLER_CONTROL>(control),
											   static_cast<CONTROLLER_ACTION>(action))) {
					choice.widget->Append(
						wxString::FromUTF8(controller_binding_action_labels[action]));
					choice.actions.push_back(action);
				}
			}
			grid->Add(choice.widget, FIXED_LAYOUT, wxEXPAND);
		}
		wxBoxSizer *fields = new wxBoxSizer(wxVERTICAL);
		fields->Add(grid, FIXED_LAYOUT, wxEXPAND | wxALL, FromDIP(BORDER));
		page->SetSizer(fields);
		page->FitInside();
		tabs->AddPage(page, name);
	}

	void scroll_page(wxScrolledWindow *page, CONTROLLER_CONTEXT context, const wxMouseEvent &event)
	{
		/* Keep wheel gestures from changing a binding as the user scrolls the tab. */
		const legacy_int wheel_delta = event.GetWheelDelta();
		if (event.GetWheelAxis() != wxMOUSE_WHEEL_VERTICAL || wheel_delta <= 0) {
			return;
		}
		const legacy_int lines = event.IsPageScroll()
									 ? page->GetClientSize().y / FromDIP(SCROLL_STEP)
									 : event.GetLinesPerAction();
		const legacy_int rotation = scroll_remainder_[context] + event.GetWheelRotation() * lines;
		scroll_remainder_[context] = rotation % wheel_delta;
		page->Scroll(wxDefaultCoord, wxMax(0, page->GetViewStart().y - rotation / wheel_delta));
	}

	void select_bindings(const CONTROLLER_BINDINGS &bindings)
	{
		for (legacy_u8 context = 0; context < CONTROLLER_CONTEXT_COUNT; ++context) {
			for (legacy_u8 control = 0; control < CONTROLLER_CONTROL_COUNT; ++control) {
				const BindingChoice &choice = choices_[context][control];
				const auto selected = std::find(choice.actions.begin(), choice.actions.end(),
												bindings.actions[context][control]);
				choice.widget->SetSelection(
					selected == choice.actions.end()
						? wxNOT_FOUND
						: static_cast<legacy_int>(selected - choice.actions.begin()));
			}
		}
	}

	void save(wxCommandEvent &)
	{
		CONTROLLER_BINDINGS edited = bindings_;
		for (legacy_u8 context = 0; context < CONTROLLER_CONTEXT_COUNT; ++context) {
			for (legacy_u8 control = 0; control < CONTROLLER_CONTROL_COUNT; ++control) {
				const BindingChoice &choice = choices_[context][control];
				const legacy_int selected = choice.widget->GetSelection();
				if (selected != wxNOT_FOUND) {
					edited.actions[context][control] = choice.actions[selected];
				}
			}
		}
		wxString error;
		{
			wxLogNull quiet;
			error = launcher_save_controller_bindings(settings_path_, edited);
		}
		if (!error.empty()) {
			status_->SetLabel(error);
			status_->Wrap(GetClientSize().GetWidth() - FromDIP(BORDER + BORDER));
			status_->Show();
			Layout();
			return;
		}
		bindings_ = edited;
		EndModal(wxID_OK);
	}

	wxString settings_path_;
	CONTROLLER_BINDINGS &bindings_;
	BindingChoice choices_[CONTROLLER_CONTEXT_COUNT][CONTROLLER_CONTROL_COUNT];
	legacy_int scroll_remainder_[CONTROLLER_CONTEXT_COUNT] = {};
	wxStaticText *status_;
};

legacy_bool launcher_edit_controller_bindings(wxWindow *parent, const wxString &settings_path,
											  CONTROLLER_BINDINGS &bindings)
{
	ControllerBindingsDialog dialog(parent, settings_path, bindings);
	return dialog.ShowModal() == wxID_OK;
}
