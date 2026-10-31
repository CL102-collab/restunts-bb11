#include "track_objects.h"
#include "fatal.h"
#include "extended_ghost_runtime.h"
#ifdef RESTUNTS_SDL3
#include <stdlib.h>
#include "ghost_internal.h"
#include "extended_ghost.h"
#include "extended_track.h"
#include "extended_replay_runtime.h"
#include "externs.h"
#include "fileio.h"
#include "platform.h"
static struct EXTENDED_GHOST_REPLAY ghost_extended;
static struct EXTENDED_REPLAY_SUSPENDED saved_extended;
legacy_s16 ghost_select_replay_format(const legacy_s8 *directory, const legacy_s8 *name,
									  const legacy_s8 *extension)
{
	if (strcmp(extension, ".csrpl") != 0) {
		legacy_u8 *previous = NULL;
		size_t previous_size = 0;
		if (extended_track_active()) {
			if (!cstrk_encode(extended_track_model(), &previous, &previous_size, NULL, 0)) {
				return 1;
			}
			extended_track_unload();
		}
		legacy_s16 result = ghost_select_replay(directory, name);
		if (result != 0 && previous) {
			(void)extended_track_load(previous, previous_size, NULL, 0);
			(void)track_setup();
		} else if (result == 0) {
			extended_replay_runtime_clear();
		}
		free(previous);
		return result;
	}
	legacy_s8 path[REPLAY_FILENAME_SIZE];
	if ((directory ? strlen(directory) : 0) + strlen(name) + sizeof(".csrpl/") > sizeof(path)) {
		return 1;
	}
	file_build_path(directory, name, ".csrpl", path);
	struct EXTENDED_GHOST_REPLAY candidate = {0};
	struct GAMEINFO config;
	if (!extended_ghost_read(path, &candidate, &config)) {
		return 1;
	}
	struct GAMEINFO checked = config;
	checked.game_recordedframes = 0;
	if (!ghost_valid_config(&checked)) {
		extended_ghost_free(&candidate);
		return 1;
	}
	legacy_s8 temp_name[GHOST_TEMP_NAME_SIZE];
	legacy_u16 destination = ghost_create_file(temp_name, 'H');
	if (!destination) {
		extended_ghost_free(&candidate);
		return 1;
	}
	legacy_u8 *previous = NULL;
	size_t previous_size = 0;
	legacy_u8 legacy_track[REPLAY_TRACK_SIZE];
	if (extended_track_active()) {
		if (!cstrk_encode(extended_track_model(), &previous, &previous_size, NULL, 0)) {
			dos_file_close(destination);
			dos_file_remove(temp_name);
			extended_ghost_free(&candidate);
			return 1;
		}
	} else {
		memcpy(legacy_track, track_element_map, sizeof(legacy_track));
	}
	legacy_int loaded =
		extended_track_load(candidate.view.track_data, candidate.view.track_size, NULL, 0);
	if (!loaded || track_setup() != 0) {
		if (loaded) {
			if (previous) {
				(void)extended_track_load(previous, previous_size, NULL, 0);
			} else {
				extended_track_unload();
				memcpy(track_element_map, legacy_track, sizeof(legacy_track));
				init_row_tables();
			}
			(void)track_setup();
		}
		free(previous);
		dos_file_close(destination);
		dos_file_remove(temp_name);
		extended_ghost_free(&candidate);
		return 1;
	}
	free(previous);
	ghost_clear();
	ghost_file = destination;
	strcpy(ghost_temp_name, temp_name);
	ghost_extended = candidate;
	ghost_config = config;
	extended_replay_runtime_clear();
	if (!ghost_cleanup_registered) {
		add_exit_handler(ghost_cleanup);
		ghost_cleanup_registered = 1;
	}
	memcpy(gameconfig.game_trackname, config.game_trackname, REPLAY_TRACK_NAME_SIZE);
	file_warn_replay_version(ghost_extended.view.metadata.version);
	file_warn_replay_driving_rules(ghost_extended.view.metadata.driving_rules);
	return 0;
}
void extended_ghost_runtime_clear(void)
{
	extended_ghost_free(&ghost_extended);
}
legacy_u32 extended_ghost_runtime_recorded_frames(legacy_u32 fallback)
{
	return ghost_extended.bytes ? ghost_extended.view.metadata.frame_count : fallback;
}
legacy_u32 extended_ghost_runtime_pose_offset(legacy_u32 fallback)
{
	return ghost_extended.bytes ? 0 : fallback;
}
legacy_s16 extended_ghost_runtime_check_track(void)
{
	if (ghost_extended.bytes) {
		if (!extended_ghost_matches_track(&ghost_extended)) {
			ghost_clear();
		}
		return 1;
	}
	if (extended_track_active()) {
		ghost_clear();
		return 1;
	}
	return 0;
}
legacy_s16 extended_ghost_runtime_prepare_inputs(legacy_u32 pose_size, legacy_s16 *failure)
{
	if (!ghost_extended.bytes) {
		return 0;
	}
	*failure =
		extended_ghost_runtime_recorded_frames(0) > (legacy_u32)LEGACY_S32_MAX / pose_size - 1U;
	return 1;
}
legacy_s16 extended_ghost_runtime_begin(void)
{
	extended_replay_runtime_suspend(&saved_extended);
	if (ghost_extended.bytes && !extended_replay_runtime_load(&ghost_extended.view)) {
		extended_replay_runtime_resume(&saved_extended);
		return 0;
	}
	return 1;
}
void extended_ghost_runtime_end(void)
{
	extended_replay_runtime_resume(&saved_extended);
}
void extended_ghost_runtime_camera(struct GHOST_CAMERA_STATE *camera)
{
	camera->frame = ghost_extended.bytes ? extended_replay_frame() : (legacy_u16)state.game_frame;
	camera->crash_frame =
		ghost_extended.bytes ? extended_replay_clocks.player_end : (legacy_u16)state.game_pEndFrame;
}
legacy_s16 extended_ghost_runtime_line_width(void)
{
	return extended_track_width();
}
legacy_s16 extended_ghost_runtime_line_height(void)
{
	return extended_track_height();
}
#endif
