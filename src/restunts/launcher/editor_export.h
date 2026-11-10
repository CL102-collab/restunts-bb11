#ifndef RESTUNTS_LAUNCHER_EDITOR_EXPORT_H
#define RESTUNTS_LAUNCHER_EDITOR_EXPORT_H

#include <wx/filename.h>
#include <wx/string.h>

/* Materialize the self-contained editor in the browser-visible Downloads
 * folder. Empty means success; exported is unchanged on failure. */
wxString launcher_export_editor(const wxFileName &source, const wxString &downloads,
								wxFileName &exported);

#endif
