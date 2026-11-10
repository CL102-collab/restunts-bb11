#include "browser_command.h"
#include "../c/legacy.h"
#include <wx/cmdline.h>

static const legacy_char DEPRECATED_FIELDS[] = "dDnNvm";

static legacy_int command_quotes_valid(const wxString &command)
{
	wxUniChar quote = '\0';
	legacy_int escaped = 0;
	for (const auto character : command) {
		if (character == '\0') {
			return 0;
		}
		if (escaped) {
			escaped = 0;
		} else if (character == '\\') {
			escaped = 1;
		} else if (quote != '\0') {
			if (character == quote) {
				quote = '\0';
			}
		} else if (character == '\'' || character == '"') {
			quote = character;
		} else if (character == '\n' || character == '\r') {
			return 0;
		}
	}
	return quote == '\0' && !escaped;
}

std::vector<wxString> launcher_browser_command(const wxString &exec, const wxFileName &editor,
											   const wxFileName &desktop, const wxString &name,
											   const wxString &icon)
{
	std::vector<wxString> result;
	if (!command_quotes_valid(exec)) {
		return result;
	}
	const wxArrayString tokens = wxCmdLineParser::ConvertStringToArgs(exec, wxCMD_LINE_SPLIT_UNIX);
	if (tokens.empty() || tokens[0].empty() || tokens[0].Find('=') != wxNOT_FOUND) {
		return result;
	}
	const wxString uri = wxFileName::FileNameToURL(editor);
	legacy_int file_fields = 0;
	for (size_t index = 0; index < tokens.size(); ++index) {
		const wxString &token = tokens[index];
		if (token == "%i") {
			if (!index) {
				return {};
			}
			if (!icon.empty()) {
				result.push_back("--icon");
				result.push_back(icon);
			}
			continue;
		}
		if (token.length() == 2 && token[0] == '%' &&
			wxString(DEPRECATED_FIELDS).Find(token[1]) != wxNOT_FOUND) {
			if (!index) {
				return {};
			}
			continue;
		}
		wxString expanded;
		for (size_t position = 0; position < token.length(); ++position) {
			const wxUniChar character = token[position];
			if (character != '%') {
				expanded += character;
				continue;
			}
			if (++position == token.length()) {
				return {};
			}
			const wxUniChar field = token[position];
			if (field == '%') {
				expanded += '%';
			} else if (!index) {
				return {};
			} else if (field == 'u' || field == 'U' || field == 'f' || field == 'F') {
				if (++file_fields > 1 || ((field == 'U' || field == 'F') && token.length() != 2)) {
					return {};
				}
				expanded += (field == 'u' || field == 'U') ? uri : editor.GetFullPath();
			} else if (field == 'c') {
				expanded += name;
			} else if (field == 'k') {
				expanded += desktop.GetFullPath();
			} else {
				return {};
			}
		}
		result.push_back(expanded);
	}
	if (!file_fields) {
		result.push_back(uri);
	}
	return result;
}
