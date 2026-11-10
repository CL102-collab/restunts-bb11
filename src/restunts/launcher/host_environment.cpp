#include "host_environment.h"

static const legacy_char HOST_ENVIRONMENT_VERSION[] = "RESTUNTS_APPIMAGE_HOST_ENVIRONMENT";
static const legacy_char HOST_ENVIRONMENT_PREFIX[] = "RESTUNTS_APPIMAGE_HOST_";
static const legacy_char *const HOST_VARIABLES[] = {
	"LD_LIBRARY_PATH",	"PANGO_RC_FILE",	 "GDK_PIXBUF_MODULE_FILE", "FONTCONFIG_PATH",
	"FONTCONFIG_FILE",	"LIBTHAI_DICTDIR",	 "GIO_EXTRA_MODULES",	   "RESTUNTS_ALSA_CONFIG_DIR",
	"ALSA_CONFIG_PATH", "PULSE_RUNTIME_PATH"};

legacy_int launcher_restore_host_environment(wxEnvVariableHashMap &environment)
{
	const auto version = environment.find(HOST_ENVIRONMENT_VERSION);
	if (version == environment.end() || version->second != "1") {
		return 0;
	}
	for (const auto name : HOST_VARIABLES) {
		const wxString saved_name = wxString(HOST_ENVIRONMENT_PREFIX) + name;
		const auto saved = environment.find(saved_name);
		if (saved != environment.end()) {
			const wxString value = saved->second;
			if (value == "0") {
				environment.erase(name);
			} else if (value.StartsWith("1")) {
				environment[name] = value.Mid(1);
			}
			environment.erase(saved_name);
		}
	}
	environment.erase(HOST_ENVIRONMENT_VERSION);
	return 1;
}
