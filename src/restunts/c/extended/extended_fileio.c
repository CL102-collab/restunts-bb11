#include "extended_fileio.h"
#ifdef RESTUNTS_SDL3
#include <stdio.h>
#include <string.h>
#include "../fileio.h"
#include "../externs.h"
#include "extended_file.h"
#include "extended_track.h"
#include "extended_replay_runtime.h"
#include "../driving_rules.h"
#include "../ui/ui_dialog.h"
#define FILE_REPLAY_VERSION_WARNING_SIZE 224U
#define FILE_REPLAY_RULES_WARNING_SIZE 512U

void file_warn_replay_version(legacy_u16 version)
{
#ifndef RESTUNTS_HEADLESS
	if (version <= CSRPL_VERSION) {
		return;
	}
	legacy_s8 message[FILE_REPLAY_VERSION_WARNING_SIZE];
	snprintf((legacy_char *)message, sizeof(message),
			 "Replay recorded with revision v%" LEGACY_PRIu32 " is newer.]"
			 "Game records revision v%" LEGACY_PRIu32 ".]"
			 "Physics may differ; playback may desync.]"
			 "Press OK to continue playback.]}[ OK ]",
			 (legacy_u32)version, (legacy_u32)CSRPL_VERSION);
	show_dialog(DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, message, DIALOG_AUTO_POSITION,
				DIALOG_AUTO_POSITION, dialog_border_color, 0, 0);
#else
	(void)version;
#endif
}

void file_warn_replay_driving_rules(legacy_u16 recorded)
{
#ifndef RESTUNTS_HEADLESS
	if (recorded == driving_rules_current()) {
		return;
	}
	legacy_s8 message[FILE_REPLAY_RULES_WARNING_SIZE];
	snprintf((legacy_char *)message, sizeof(message),
			 "Replay uses different driving rules.]Playback may desync.]Recorded rules:]"
			 "Power Gear: %s]Original Collision Bugs: %s]Left Corner Bias: %s]"
			 "Dual-Way Switching: %s]Chicane Fast-Grass: %s]Amiga Grass: %s]OWOOT: %s]"
			 "Press OK to continue playback.]}[ OK ]",
			 recorded & DRIVING_RULE_POWER_GEAR ? "On" : "Off",
			 recorded & DRIVING_RULE_LEGACY_COLLISION ? "On" : "Off",
			 recorded & DRIVING_RULE_LEFT_CORNER_BIAS ? "On" : "Off",
			 recorded & DRIVING_RULE_DUAL_WAY_SWITCHING ? "On" : "Off",
			 recorded & DRIVING_RULE_CHICANE_FAST_GRASS ? "On" : "Off",
			 recorded & DRIVING_RULE_AMIGA_GRASS ? "On" : "Off",
			 recorded & DRIVING_RULE_OWOOT ? "On" : "Off");
	show_dialog(DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, message, DIALOG_AUTO_POSITION,
				DIALOG_AUTO_POSITION, dialog_border_color, 0, 0);
#else
	(void)recorded;
#endif
}

#ifndef RESTUNTS_HEADLESS
legacy_s16 extended_fileio_load_replay(const legacy_s8 *directory, const legacy_s8 *name,
									   const legacy_s8 *extension, legacy_s16 *result)
{
	if (strcmp(extension, ".csrpl") != 0) {
		extended_track_unload();
		extended_track_clear_name();
		extended_replay_runtime_clear();
		return 0;
	}
	file_build_path(directory, name, extension, g_path_buf);
	g_is_busy = 1;
	*result = (legacy_s16)extended_file_load_replay(g_path_buf);
	if (*result == 0) {
		file_set_replay_filename(g_path_buf);
	}
	g_is_busy = 0;
	if (*result == 0) {
		file_warn_replay_version(extended_replay_version());
		file_warn_replay_driving_rules(extended_replay_driving_rules());
	}
	return 1;
}

legacy_s16 extended_fileio_write_replay(const legacy_s8 *filename, legacy_s16 *result)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	g_is_busy = 1;
	*result = (legacy_s16)extended_file_write_replay(filename);
	if (*result == 0) {
		file_set_replay_filename(filename);
	}
	g_is_busy = 0;
	return 1;
}
#endif
#endif
