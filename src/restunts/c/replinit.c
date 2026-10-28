#include "restunts.h"
#include "memmgr.h"
#include "trackdata_layout.h"
#include "externs.h"
#include "extended_track.h"

#define REPLAY_SLOW_CALLBACK_DIVISOR 2U
#define VIDEO_GEOMETRY_UNIT_SCALE 1
#define VIDEO_GEOMETRY_FULL_MASK (-1)

void init_video_geometry_flags(void)
{
	video_shape_width_scale = VIDEO_GEOMETRY_UNIT_SCALE;
	video_x_alignment = VIDEO_GEOMETRY_UNIT_SCALE;
	video_x_alignment_mask = VIDEO_GEOMETRY_FULL_MASK;
	video_buffer_height_divisor = VIDEO_GEOMETRY_UNIT_SCALE;
}

void init_row_tables(void)
{
	legacy_s16 width = (legacy_s16)extended_track_width();
	legacy_s16 height = (legacy_s16)extended_track_height();
	legacy_s16 count = width > height ? width : height;
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		count++;
	}
#endif
	for (legacy_s16 i = 0; i < count; i++) {
		legacy_s16 inverse_row = LEGACY_S16_WRAP_SUB(height - 1, i);
		legacy_s16 track_position = LEGACY_S16_SHL(inverse_row, TRACK_TILE_POSITION_SHIFT);
		legacy_s16 terrain_position = LEGACY_S16_SHL(i, TRACK_TILE_POSITION_SHIFT);
		trackrows[i] = LEGACY_S16_WRAP_MUL(width, inverse_row);
		terrainrows[i] = LEGACY_S16_WRAP_MUL(width, i);
		track_row_positions[i] = track_position;
		track_column_positions[i] = terrain_position;
		track_row_centers[i] = LEGACY_S16_WRAP_ADD(track_position, TRACK_TILE_HALF_SIZE);
		terrainpos[i] = terrain_position;
		terraincenterpos[i] = LEGACY_S16_WRAP_ADD(terrain_position, TRACK_TILE_HALF_SIZE);
		track_column_centers[i] = LEGACY_S16_WRAP_ADD(terrain_position, TRACK_TILE_HALF_SIZE);
	}
}

void init_trackdata(void)
{
	legacy_s8 far *trkptr = mmgr_alloc_resbytes("trakdata", TRACKDATA_ALLOCATION_SIZE);
	track_primary_route_links = (legacy_s16 far *)trkptr;
	trkptr += TRACKDATA_LINK_TABLE_SIZE;
	track_alternate_route_links = (legacy_s16 far *)trkptr;
	trkptr += TRACKDATA_LINK_TABLE_SIZE;
	opponent_route_track_indices = trkptr;
	trkptr += TRACKDATA_LINK_TABLE_SIZE;
	player_aero_resistance_table = (legacy_s16 far *)trkptr;
	trkptr += TRACKDATA_AERO_TABLE_SIZE;
	opponent_aero_resistance_table = (legacy_s16 far *)trkptr;
	trkptr += TRACKDATA_AERO_TABLE_SIZE;
	reserved_trackside_camera_words = (legacy_s16 far *)trkptr;
	trkptr += TRACKDATA_AERO_TABLE_SIZE;
	trackside_camera_ground_heights = (legacy_s16 far *)trkptr;
	trkptr += TRACKDATA_AERO_TABLE_SIZE;
	roadside_sign_headings = (legacy_s16 far *)trkptr;
	trkptr += TRACKDATA_DIRECTION_TABLE_SIZE;
	trackside_camera_positions = (struct VECTOR far *)trkptr;
	trkptr += TRACKDATA_CAMERA_VECTOR_SIZE;
	roadside_sign_positions = (struct VECTOR far *)trkptr;
	trkptr += TRACKDATA_CHECK_VECTOR_SIZE;
	track_highscore_table = trkptr;
	trkptr += TRACKDATA_HIGHSCORE_SIZE;
	sprite_background_state_stack = trkptr;
	trkptr += TRACKDATA_SPRITE_STATE_STACK_SIZE;
	replay_header_buffer = trkptr;
	trkptr += TRACKDATA_REPLAY_HEADER_SIZE;
	track_element_map = (legacy_u8 far *)trkptr;
	trkptr += TRACKDATA_MAP_SIZE;
	track_terrain_map = (legacy_u8 far *)trkptr;
	trkptr += TRACKDATA_MAP_SIZE;
	replay_input_buffer = trkptr;
	trkptr += TRACKDATA_REPLAY_INPUT_BUFFER_SIZE;
	track_route_element_ids = trkptr;
	trkptr += TRACKDATA_MAP_SIZE;
	track_route_traversal_flags = trkptr;
	trkptr += TRACKDATA_MAP_SIZE;
	roadside_sign_indices_by_tile = (legacy_u8 far *)trkptr;
	trkptr += TRACKDATA_MAP_SIZE;
	track_and_directory_backup = trkptr;
	trkptr += TRACKDATA_TRACK_FILE_APPEND_SIZE;
	track_route_columns = trkptr;
	trkptr += TRACKDATA_MAP_SIZE;
	track_route_rows = trkptr;
	trkptr += TRACKDATA_MAP_SIZE;
	roadside_sign_shape_indices = (legacy_u8 far *)trkptr;
	trkptr += TRACKDATA_OBJECT_INDEX_SIZE;
}

void reset_race_loop_state(void)
{
	frame_callback_countdown = 1;
	slow_replay_countdown = REPLAY_SLOW_CALLBACK_DIVISOR;
	elapsed_time2 = 0;
	race_exit_request = 0;
	race_start_sequence_state = RACE_START_SEQUENCE_INACTIVE;
	start_flag_animation = 0;
}
