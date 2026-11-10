#include <wx/init.h>
#include <cstdio>
#include "host_environment.h"

static legacy_int failures;
static const legacy_char *const HOST_KEYS[] = {
	"LD_LIBRARY_PATH",	"PANGO_RC_FILE",	 "GDK_PIXBUF_MODULE_FILE", "FONTCONFIG_PATH",
	"FONTCONFIG_FILE",	"LIBTHAI_DICTDIR",	 "GIO_EXTRA_MODULES",	   "RESTUNTS_ALSA_CONFIG_DIR",
	"ALSA_CONFIG_PATH", "PULSE_RUNTIME_PATH"};
static const legacy_char PREFIX[] = "RESTUNTS_APPIMAGE_HOST_";
static const legacy_char VERSION[] = "RESTUNTS_APPIMAGE_HOST_ENVIRONMENT";

static void check(legacy_int condition, const legacy_char *message)
{
	if (!condition) {
		std::fprintf(stderr, "%s\n", message);
		++failures;
	}
}

static void check_original_environment(const legacy_char *original)
{
	wxEnvVariableHashMap environment;
	wxGetEnvMap(&environment);
	environment[VERSION] = "1";
	environment["DISPLAY"] = ":77";
	environment["DBUS_SESSION_BUS_ADDRESS"] = "unix:path=/desktop/session";
	for (const auto name : HOST_KEYS) {
		environment[name] = "/bundle/override";
		environment[wxString(PREFIX) + name] = original ? "1" + wxString::FromUTF8(original) : "0";
	}
	const wxEnvVariableHashMap parent = environment;
	check(launcher_restore_host_environment(environment), "AppRun snapshot was not recognized");
	for (const auto name : HOST_KEYS) {
		const auto value = environment.find(name);
		check(original ? value != environment.end() && value->second == wxString::FromUTF8(original)
					   : value == environment.end(),
			  "Desktop variable was not restored exactly");
		check(environment.find(wxString(PREFIX) + name) == environment.end(),
			  "Private snapshot leaked to the desktop child");
		check(parent.find(name)->second == "/bundle/override", "Launcher environment was changed");
	}
	check(environment.find(VERSION) == environment.end(), "Version marker leaked to child");
	check(environment["DISPLAY"] == ":77" &&
			  environment["DBUS_SESSION_BUS_ADDRESS"] == "unix:path=/desktop/session",
		  "Desktop display/session configuration was changed");
}

static void check_child_process()
{
#if defined(__UNIX__)
	wxExecuteEnv child;
	wxGetEnvMap(&child.env);
	child.env[VERSION] = "1";
	for (const auto name : HOST_KEYS) {
		child.env[name] = "/bundle/override";
		child.env[wxString(PREFIX) + name] = "0";
	}
	child.env["RESTUNTS_KEEP_DESKTOP_SESSION"] = "untouched";
	check(launcher_restore_host_environment(child.env), "Child environment was not restored");
	const wxString shell = "/bin/sh";
	const wxString option = "-c";
	const wxString script = "test \"${LD_LIBRARY_PATH+x}\" != x && "
							"test \"${FONTCONFIG_PATH+x}\" != x && "
							"test \"${GIO_EXTRA_MODULES+x}\" != x && "
							"test \"${RESTUNTS_APPIMAGE_HOST_ENVIRONMENT+x}\" != x && "
							"test \"$RESTUNTS_KEEP_DESKTOP_SESSION\" = untouched";
	const wxChar *arguments[] = {shell.c_str(), option.c_str(), script.c_str(), nullptr};
	check(wxExecute(arguments, wxEXEC_SYNC | wxEXEC_NOEVENTS, nullptr, &child) == 0,
		  "Desktop child inherited bundled settings");
#endif
}

legacy_int main()
{
	wxInitializer initialize;
	if (!initialize.IsOk()) {
		return 1;
	}
	wxEnvVariableHashMap ordinary;
	ordinary["LD_LIBRARY_PATH"] = "/caller/lib";
	check(!launcher_restore_host_environment(ordinary), "Non-AppImage environment was recognized");
	check(ordinary.size() == 1 && ordinary["LD_LIBRARY_PATH"] == "/caller/lib",
		  "Ordinary environment was changed");
	check_original_environment(nullptr);
	check_original_environment("");
	check_original_environment("/host libraries/\xc3\xa9:'quoted':$HOME:$(false)\nnext line");
	check_child_process();
	return failures ? 1 : 0;
}
