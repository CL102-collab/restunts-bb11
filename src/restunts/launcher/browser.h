#ifndef RESTUNTS_LAUNCHER_BROWSER_H
#define RESTUNTS_LAUNCHER_BROWSER_H

#include <wx/filename.h>
#include <wx/utils.h>
#include "../c/legacy.h"

/* Open local editor HTML with the configured web browser, independently of
 * the desktop's local HTML-file association. The child uses host settings. */
legacy_int launcher_open_web_browser(const wxFileName &editor, const wxExecuteEnv &environment,
									 wxString *error = nullptr);

#endif
