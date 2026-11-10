#ifndef RESTUNTS_LAUNCHER_HOST_ENVIRONMENT_H
#define RESTUNTS_LAUNCHER_HOST_ENVIRONMENT_H

#include <wx/utils.h>
#include "../c/legacy.h"

/* Restore the caller's environment captured by AppRun for an external desktop
 * application. Returns zero outside AppImage; never changes this process. */
legacy_int launcher_restore_host_environment(wxEnvVariableHashMap &environment);

#endif
