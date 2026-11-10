#include "browser_command.h"
#include "../c/legacy.h"
#include <wx/init.h>
#include <cstdio>
#include <initializer_list>

static legacy_int failures;
static wxFileName editor;
static wxFileName desktop_file;
static const wxString NAME = wxString::FromUTF8("Default browser \xc3\xa9");
static const wxString ICON = "browser icon";

static void check_arguments(const wxString &exec, std::initializer_list<wxString> expected,
							const wxString &icon = ICON)
{
	const auto actual = launcher_browser_command(exec, editor, desktop_file, NAME, icon);
	if (actual != std::vector<wxString>(expected)) {
		std::fprintf(stderr, "Incorrect browser arguments for %s\n", exec.utf8_str().data());
		++failures;
	}
}

static void check_rejected(const wxString &exec)
{
	if (!launcher_browser_command(exec, editor, desktop_file, NAME, ICON).empty()) {
		std::fprintf(stderr, "Accepted invalid browser Exec: %s\n", exec.utf8_str().data());
		++failures;
	}
}

legacy_int main()
{
	wxInitializer initialize;
	if (!initialize.IsOk()) {
		return 1;
	}
	editor.AssignDir(wxFileName::GetTempDir());
	editor.AppendDir("editor folder");
	editor.SetFullName(wxString::FromUTF8("éditor.html"));
	desktop_file.AssignDir(wxFileName::GetTempDir());
	desktop_file.AppendDir("applications");
	desktop_file.SetFullName("browser with spaces.desktop");
	const wxString uri = wxFileName::FileNameToURL(editor);
	const wxString file = editor.GetFullPath();
	const wxString desktop = desktop_file.GetFullPath();
	check_arguments("browser %u", {"browser", uri});
	check_arguments("browser %U", {"browser", uri});
	check_arguments("browser %f", {"browser", file});
	check_arguments("browser %F", {"browser", file});
	check_arguments("browser --new-window", {"browser", "--new-window", uri});
	check_arguments("browser --url=%u", {"browser", "--url=" + uri});
	check_arguments("browser --file=%f", {"browser", "--file=" + file});
	check_arguments("browser --name=%c %k", {"browser", "--name=" + NAME, desktop, uri});
	check_arguments("browser %i %u", {"browser", "--icon", ICON, uri});
	check_arguments("browser %i %u", {"browser", uri}, wxEmptyString);
	check_arguments("browser 100%% %u", {"browser", "100%", uri});
	check_arguments("browser %d %D %n %N %v %m %u", {"browser", uri});
	check_arguments("\"/opt/browser folder/browser\" --profile \"profile with spaces\" %u",
					{"/opt/browser folder/browser", "--profile", "profile with spaces", uri});
	check_arguments("browser \"literal $HOME;$(false)\" --name %c %u",
					{"browser", "literal $HOME;$(false)", "--name", NAME, uri});
	check_arguments("browser \"\" %u", {"browser", "", uri});
	check_arguments("browser '%u'", {"browser", uri});
	check_arguments("browser path\\ with\\ spaces %u", {"browser", "path with spaces", uri});
	check_arguments(
		"/usr/bin/flatpak run --branch=stable --file-forwarding org.example.Browser @@u %u @@",
		{"/usr/bin/flatpak", "run", "--branch=stable", "--file-forwarding", "org.example.Browser",
		 "@@u", uri, "@@"});
	check_arguments("/usr/bin/env BROWSER_PROFILE=private browser %U",
					{"/usr/bin/env", "BROWSER_PROFILE=private", "browser", uri});
	for (const wxString bad :
		 {wxString(), wxString("   "), wxString("\"\" %u"), wxString("browser \"unfinished"),
		  wxString("browser 'unfinished"), wxString("browser trailing\\"), wxString("browser %"),
		  wxString("browser %x"), wxString("browser --icon=%i"), wxString("browser --url=%U"),
		  wxString("browser --file=%F"), wxString("browser %u %f"), wxString("browser %u %U"),
		  wxString("browser %F %F"), wxString("browser --old=%d"), wxString("%c %u"),
		  wxString("NAME=browser %u"), wxString("browser\n--new-window %u")}) {
		check_rejected(bad);
	}
	return failures ? 1 : 0;
}
