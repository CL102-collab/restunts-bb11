#include "editor_open.h"
#include "editor_export.h"
#include "host_environment.h"
#include "browser.h"
#include "../c/legacy.h"
#include <wx/log.h>
#include <wx/stdpaths.h>

static const legacy_char TRACK_EDITOR_FILENAME[] = "tracked.html";
static const legacy_char TRACK_EDITOR_DIRECTORY[] = "../share/restunts/editor/";

wxString launcher_open_editor()
{
	const wxFileName launcher(wxStandardPaths::Get().GetExecutablePath());
	wxFileName editor(launcher.GetPathWithSep() + TRACK_EDITOR_DIRECTORY + TRACK_EDITOR_FILENAME);
	if (!editor.FileExists()) {
		editor.Assign(launcher.GetPath(), TRACK_EDITOR_FILENAME);
	}
	if (!editor.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE) || !editor.FileExists()) {
		return "The track editor is missing. Keep the complete runtime package together.";
	}
	legacy_int opened;
	wxString browser_error;
	{
		wxLogNull quiet;
#ifdef __linux__
		wxFileName persistent;
		const wxString export_error = launcher_export_editor(
			editor, wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Downloads), persistent);
		if (!export_error.empty()) {
			return export_error;
		}
		editor = persistent;
		wxExecuteEnv browser_environment;
		wxGetEnvMap(&browser_environment.env);
		launcher_restore_host_environment(browser_environment.env);
		opened = launcher_open_web_browser(editor, browser_environment, &browser_error);
#else
#ifdef __WXQT__
		opened = wxLaunchDefaultApplication(editor.GetFullPath());
#else
		opened = wxLaunchDefaultBrowser(wxFileName::FileNameToURL(editor));
#endif
#endif
	}
	if (!opened) {
		return browser_error.empty()
				   ? "Cannot open the track editor in your default browser: " + editor.GetFullPath()
				   : browser_error;
	}
	return wxString();
}
