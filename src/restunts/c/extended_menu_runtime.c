#include "extended_menu_runtime.h"
#ifdef RESTUNTS_SDL3
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "externs.h"
#include "fileio.h"
#include "fatal.h"
#include "ui_dialog.h"
#include "hires.h"
#include "platform.h"
#include "extended_track.h"
#include "extended_validation.h"
#include "extended_file.h"
#include "extended_editor.h"
#include "extended_replay_runtime.h"
#define TRACK_VERSION_WARNING_SIZE 192U
static legacy_u8 *main_menu_extended_track_backup;
static size_t main_menu_extended_track_backup_size;
void extended_menu_backup_track(void)
{
	free(main_menu_extended_track_backup);
	main_menu_extended_track_backup = NULL;
	main_menu_extended_track_backup_size = 0;
	if (extended_track_active()) {
		legacy_char error[128];
		if (!cstrk_encode(extended_track_model(), &main_menu_extended_track_backup,
						  &main_menu_extended_track_backup_size, error, sizeof(error))) {
			fatal_error("Cannot retain track: %s", error);
		}
	}
}
legacy_s16 extended_menu_restore_track(void)
{
	legacy_int was_extended = extended_track_active() || extended_replay_is_active();
	if (main_menu_extended_track_backup) {
		gameconfig = gameconfigcopy;
		legacy_char error[128];
		if (!extended_track_load(main_menu_extended_track_backup,
								 main_menu_extended_track_backup_size, error, sizeof(error))) {
			fatal_error("Cannot restore track: %s", error);
		}
		free(main_menu_extended_track_backup);
		main_menu_extended_track_backup = NULL;
		main_menu_extended_track_backup_size = 0;
	} else {
		extended_track_unload();
		if (was_extended) {
			gameconfig = gameconfigcopy;
		} else {
			_memcpy(&gameconfigcopy, &gameconfig, sizeof(struct GAMEINFO));
		}
	}
	extended_replay_runtime_clear();

	return !extended_track_active();
}
void extended_menu_warn_track_version(void)
{
	if (!extended_track_active()) {
		return;
	}
	const struct CSTRK_TRACK *track = extended_track_model();
	if (track->version <= CSTRK_VERSION) {
		return;
	}
	legacy_s8 message[TRACK_VERSION_WARNING_SIZE];
	snprintf((legacy_char *)message, sizeof(message),
			 "Track revision v%" LEGACY_PRIu32 " is newer.]"
			 "Game supports revision v%" LEGACY_PRIu32 ".]"
			 "The track may not work correctly.]Press OK to continue racing.]}[ OK ]",
			 (legacy_u32)track->version, (legacy_u32)CSTRK_VERSION);
	show_dialog(DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, message, DIALOG_AUTO_POSITION,
				DIALOG_AUTO_POSITION, dialog_border_color, 0, 0);
}
void extended_menu_validation_error(legacy_s16 result, legacy_s16 column, legacy_s16 row)
{
	if (extended_track_active()) {
		legacy_char message[192];
		extended_track_validation_message(result, column, row, message, sizeof(message));
		show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND, message,
					DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, dialog_border_color, 0, 0);
	}
}
void extended_menu_load_startup_track(void)
{
	if (file_find(g_path_buf) == 0) {
		file_build_path(track_directory, gameconfig.game_trackname, ".cstrk", g_path_buf);
		if (file_find(g_path_buf) != 0) {
			if (extended_file_load_track(g_path_buf) != 0) {
				fatal_error("Cannot load track: %s", extended_file_error());
			}
		} else {
			file_build_path(track_directory, gameconfig.game_trackname, ".trk", g_path_buf);
			extended_track_unload();
			extended_replay_runtime_clear();
			file_read_fatal(g_path_buf, track_element_map);
		}
	} else {
		extended_track_unload();
		extended_replay_runtime_clear();
		file_read_fatal(g_path_buf, track_element_map);
	}
}
void extended_menu_begin_track_recording(void)
{
	if (extended_track_active()) {
		extended_replay_runtime_begin();
	}
}
legacy_u8 extended_menu_horizon(legacy_u8 fallback)
{
	return extended_track_active() ? extended_track_model()->horizon : fallback;
}
legacy_s16 extended_menu_live_preview(void)
{
	return extended_track_active() && hires_enabled();
}
legacy_s16 extended_menu_load_selected_track(const legacy_s8 *extension, const legacy_s8 *path,
											 const legacy_s8 *previous_name)
{
	if (strcmp(extension, ".cstrk") == 0) {
		if (extended_file_load_track(path) != 0) {
			memcpy(gameconfig.game_trackname, previous_name, REPLAY_TRACK_NAME_SIZE);
			show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
						(void *)extended_file_error(), DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION,
						dialog_border_color, 0, 0);
			return 0;
		}
	} else {
		extended_track_unload();
		extended_replay_runtime_clear();
		file_read_fatal(path, track_element_map);
	}
	return 1;
}
legacy_s16 extended_menu_open_editor(void)
{
	if (!extended_track_active()) {
		return 1;
	}
	if (!extended_editor_open()) {
		show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
					"Open tracked.html to edit Chocolate Stunts tracks.]", DIALOG_AUTO_POSITION,
					DIALOG_AUTO_POSITION, dialog_border_color, 0, 0);
	}
	return 0;
}
legacy_s16 extended_menu_needs_legacy_shapes(void)
{
	return !extended_track_active();
}
#endif
