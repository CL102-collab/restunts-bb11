#include "browser.h"
#include "browser_command.h"
#include <wx/dir.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>
#include <vector>

static const legacy_char DEFAULT_DATA_DIRECTORIES[] = "/usr/local/share:/usr/share";
static const legacy_char *const BROWSER_QUERIES[] = {
	"xdg-settings get default-web-browser", "xdg-mime query default x-scheme-handler/https",
	"xdg-mime query default x-scheme-handler/http"};

static wxString browser_variable(const wxExecuteEnv &environment, const wxString &name)
{
	const auto value = environment.env.find(name);
	return value == environment.env.end() ? wxString() : value->second;
}

static wxFileName browser_desktop_file(const wxString &identifier, const wxExecuteEnv &environment)
{
	if (!identifier.EndsWith(".desktop") || identifier.Contains("/") || identifier.Contains("\\") ||
		identifier.Contains("\n") || identifier.Contains("\r")) {
		return wxFileName();
	}
	std::vector<wxString> roots;
	wxString home = browser_variable(environment, "XDG_DATA_HOME");
	if (home.empty()) {
		home = browser_variable(environment, "HOME") + "/.local/share";
	}
	roots.push_back(home);
	wxString directories = browser_variable(environment, "XDG_DATA_DIRS");
	if (directories.empty()) {
		directories = DEFAULT_DATA_DIRECTORIES;
	}
	wxStringTokenizer tokens(directories, ":");
	while (tokens.HasMoreTokens()) {
		roots.push_back(tokens.GetNextToken());
	}
	for (const auto &root : roots) {
		wxFileName file = wxFileName::DirName(root);
		if (!file.IsAbsolute()) {
			continue;
		}
		file.AppendDir("applications");
		const wxString directory = file.GetPathWithSep();
		file.SetFullName(identifier);
		if (file.FileExists()) {
			return file;
		}
		wxArrayString nested;
		wxDir::GetAllFiles(directory, &nested, "*.desktop", wxDIR_FILES | wxDIR_DIRS);
		for (const auto &candidate : nested) {
			wxString desktop_id = candidate.Mid(directory.length());
			desktop_id.Replace("/", "-");
			if (desktop_id == identifier) {
				return wxFileName(candidate);
			}
		}
	}
	return wxFileName();
}

static wxString browser_unescape(const wxString &value)
{
	wxString result;
	for (size_t index = 0; index < value.length(); ++index) {
		if (value[index] == '\\' && index + 1 < value.length()) {
			const wxUniChar next = value[index + 1];
			if (next == 's' || next == 'n' || next == 'r' || next == 't' || next == '\\') {
				result += next == 's'	? ' '
						  : next == 'n' ? '\n'
						  : next == 'r' ? '\r'
						  : next == 't' ? '\t'
										: '\\';
				++index;
				continue;
			}
		}
		result += value[index];
	}
	return result;
}

static wxStringToStringHashMap browser_desktop_entry(const wxFileName &desktop)
{
	wxStringToStringHashMap entry;
	wxTextFile file;
	if (!file.Open(desktop.GetFullPath())) {
		return entry;
	}
	legacy_int active = 0;
	for (size_t index = 0; index < file.GetLineCount(); ++index) {
		wxString line = file.GetLine(index);
		line.Trim().Trim(false);
		if (line.StartsWith("[")) {
			active = line == "[Desktop Entry]";
		} else if (active && !line.StartsWith("#") && line.Contains("=")) {
			entry[line.BeforeFirst('=')] = browser_unescape(line.AfterFirst('='));
		}
	}
	return entry;
}

static legacy_int browser_execute(const std::vector<wxString> &arguments,
								  const wxExecuteEnv &environment)
{
	if (arguments.empty()) {
		return 0;
	}
	std::vector<const wxChar *> argv;
	for (const auto &argument : arguments) {
		argv.push_back(argument.c_str());
	}
	argv.push_back(nullptr);
	return wxExecute(argv.data(), wxEXEC_ASYNC, nullptr, &environment) > 0;
}

static wxString browser_quoted_argument(wxString value)
{
	value.Replace("\\", "\\\\");
	value.Replace("\"", "\\\"");
	return "\"" + value + "\"";
}

static wxFileName browser_portal_file(const wxFileName &editor, const wxString &application,
									  const wxExecuteEnv &environment, wxString *error)
{
	wxString flatpak;
	if (!wxFindFileInPath(&flatpak, browser_variable(environment, "PATH"), "flatpak")) {
		if (error) {
			*error = "Cannot grant the browser access to the editor: flatpak is unavailable.";
		}
		return wxFileName();
	}
	const wxString command = browser_quoted_argument(flatpak) + " document-export " +
							 browser_quoted_argument("--app=" + application) + " " +
							 browser_quoted_argument(editor.GetFullPath());
	wxArrayString output, errors;
	if (wxExecute(command, output, errors, wxEXEC_NOEVENTS, &environment) == 0 && !output.empty()) {
		wxFileName exported(output.front());
		if (exported.IsAbsolute() && exported.FileExists()) {
			return exported;
		}
	}
	if (error) {
		*error = "Cannot grant the browser access to the editor through the document portal.";
		if (!errors.empty()) {
			*error += " " + errors.front();
		}
	}
	return wxFileName();
}

legacy_int launcher_open_web_browser(const wxFileName &editor, const wxExecuteEnv &environment,
									 wxString *error)
{
	wxFileName desktop;
	for (const auto query : BROWSER_QUERIES) {
		wxArrayString output, errors;
		if (wxExecute(query, output, errors, wxEXEC_NOEVENTS, &environment) == 0 &&
			!output.empty()) {
			wxString identifier = output.front();
			identifier.Trim().Trim(false);
			desktop = browser_desktop_file(identifier, environment);
			if (desktop.FileExists()) {
				break;
			}
		}
	}
	if (!desktop.FileExists()) {
		return 0;
	}
	const wxStringToStringHashMap entry = browser_desktop_entry(desktop);
	const auto hidden = entry.find("Hidden");
	if (entry.empty() || (hidden != entry.end() && hidden->second == "true")) {
		return 0;
	}
	wxFileName browser_editor = editor;
	const auto flatpak = entry.find("X-Flatpak");
	if (flatpak != entry.end() && !flatpak->second.empty()) {
		browser_editor = browser_portal_file(editor, flatpak->second, environment, error);
		if (!browser_editor.FileExists()) {
			return 0;
		}
	}
	wxString gio;
	/* Modern GIO handles Flatpak file forwarding and desktop integration.
	 * Older distributions use Exec directly and need no new host dependency. */
	if (wxFindFileInPath(&gio, browser_variable(environment, "PATH"), "gio")) {
		return browser_execute(
			{gio, "launch", desktop.GetFullPath(), wxFileName::FileNameToURL(browser_editor)},
			environment);
	}
	const auto exec = entry.find("Exec"), terminal = entry.find("Terminal");
	if (exec == entry.end() || (terminal != entry.end() && terminal->second == "true")) {
		return 0;
	}
	const auto name = entry.find("Name"), icon = entry.find("Icon");
	return browser_execute(
		launcher_browser_command(exec->second, browser_editor, desktop,
								 name == entry.end() ? desktop.GetName() : name->second,
								 icon == entry.end() ? wxString() : icon->second),
		environment);
}
