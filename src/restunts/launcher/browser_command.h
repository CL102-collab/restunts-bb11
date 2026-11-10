#ifndef RESTUNTS_LAUNCHER_BROWSER_COMMAND_H
#define RESTUNTS_LAUNCHER_BROWSER_COMMAND_H

#include <wx/filename.h>
#include <vector>

/* Expand a decoded Desktop Entry Exec value without invoking a shell.
 * Invalid or ambiguous field codes return an empty argument vector. */
std::vector<wxString> launcher_browser_command(const wxString &exec, const wxFileName &editor,
											   const wxFileName &desktop, const wxString &name,
											   const wxString &icon);

#endif
