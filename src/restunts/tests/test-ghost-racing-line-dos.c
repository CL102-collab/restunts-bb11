/* Retain the complete game's startup and memory footprint, as RACEMEM does. */
#define run_main_menu_loop ghost_line_original_main_menu_loop
#include "../c/restunts.c"
#undef run_main_menu_loop

/* Also exercise a synthetic route larger than any 16-bit allocation. */
#include "../c/ghost.c"

#include "frame_internal.h"
#include "ghost.h"
#include "race_graphics.h"
#include "residue.h"

static legacy_s16 ghost_line_check_large_stream(void)
{
	struct GHOST_LINE_POSE end = {0};
	ghost_line_clear();
	end.center.x = 64000;
	end.center.z = 66000;
	end.right.x = end.up.y = 1;
	ghost_line_builder.sample = end;
	ghost_line_builder.sample.center.z = 64000;
	for (legacy_u16 index = 0; index < 2048; index++) {
		ghost_line_append(&end, index);
	}
	if (ghost_line_count != 2048 || ghost_line_file == 0) {
		return 1;
	}
	legacy_s8 filename[GHOST_TEMP_NAME_SIZE];
	strcpy(filename, ghost_line_temp_name);
	gameconfig.game_opponenttype = 0;
	ghost_active = 1;
	legacy_u16 remaining = 2048;
	for (const struct GHOST_RACING_LINE_SEGMENT *segment = ghost_racing_line_first(0, 29);
		 segment != 0; segment = ghost_racing_line_next(segment)) {
		if (remaining == 0 || segment->speed != --remaining || segment->start_left.lx != 63872 ||
			segment->end_right.lz != 66000) {
			return 1;
		}
	}
	ghost_end_race();
	return remaining != 0 || ghost_line_file != 0 || dos_file_find_first(filename) != 0;
}

static legacy_u32 ghost_line_checksum(const void *data, legacy_u16 length)
{
	const legacy_u8 *bytes = data;
	legacy_u32 result = 2166136261UL;
	for (legacy_u16 index = 0; index < length; index++) {
		result = (result ^ bytes[index]) * 16777619UL;
	}
	return result;
}

static legacy_s16 ghost_line_capture(const legacy_s8 *name, legacy_u16 speed)
{
	/* Row-sized readback avoids a 64 KB conventional-memory screenshot. */
	static struct {
		struct SHAPE2D header;
		legacy_u8 pixels[GAME_SCREEN_WIDTH];
	} row = {{GAME_SCREEN_WIDTH, 1, 0, 0, 0, 0, {0, 0, 0, 0}}, {0}};
	legacy_s8 seed_before[GAMESTATE_RANDOM_SEED_SIZE];
	legacy_s8 seed_after[GAMESTATE_RANDOM_SEED_SIZE];
	state.playerstate.car_actual_speed = speed;
	legacy_u32 state_before = ghost_line_checksum(&state, sizeof(state));
	legacy_u32 config_before = ghost_line_checksum(&gameconfig, sizeof(gameconfig));
	get_kevinrandom_seed(seed_before);
	full_redraw_frames_remaining = 1;
	init_rect_arrays();
	sprite_select_render_window();
	update_frame(0, &rect_windshield);
	frame_present(&rect_windshield);
	get_kevinrandom_seed(seed_after);
	if (state_before != ghost_line_checksum(&state, sizeof(state)) ||
		config_before != ghost_line_checksum(&gameconfig, sizeof(gameconfig)) ||
		ghost_line_checksum(seed_before, sizeof(seed_before)) !=
			ghost_line_checksum(seed_after, sizeof(seed_after))) {
		return 1;
	}
	legacy_u16 file = dos_file_open(name, DOS_FILE_CREATE);
	if (file == 0) {
		return 1;
	}
	sprite_select_screen();
	legacy_s16 result = 0;
	for (legacy_s16 y = 0; y < GAME_SCREEN_HEIGHT; y++) {
		sprite_clear_shape_alt(&row.header, 0, y);
		if (dos_file_write(file, row.pixels, sizeof(row.pixels)) != sizeof(row.pixels)) {
			result = 1;
			break;
		}
	}
	(void)dos_file_close(file);
	return result;
}

static legacy_s16 ghost_line_check_full_queue(void)
{
	legacy_u32 original_count = ghost_line_count;
	ghost_line_count = 0;
	for (legacy_s16 mode = 0; mode < 2; mode++) {
		supersight_enabled = mode;
		frame_supersight_reset();
		if (ghost_line_capture(mode == 0 ? "BASE0.RAW" : "BASE1.RAW", 0)) {
			return 1;
		}
	}
	ghost_line_count = original_count;
	legacy_u32 copies = GHOST_LINE_MAX_SEGMENTS / original_count - 1;
	if (copies > 64) {
		copies = 64;
	}
	/* Duplicate the actual, visible route until it overwhelms both fixed scene
	 * budgets. Preserve each original link while extending the same tile list. */
	for (legacy_u16 tile = 0; tile < GHOST_LINE_TILE_COUNT; tile++) {
		legacy_u32 original = ghost_line_heads[tile];
		while (original != GHOST_LINE_NO_SEGMENT) {
			const struct GHOST_RACING_LINE_SEGMENT *source = ghost_line_at(original);
			if (source == 0) {
				return 1;
			}
			struct GHOST_RACING_LINE_SEGMENT copy = *source;
			original = copy.next;
			for (legacy_u32 repeat = 0; repeat < copies; repeat++) {
				copy.next = ghost_line_heads[tile];
				if (!ghost_seek(ghost_line_file, ghost_line_count * sizeof(copy)) ||
					dos_file_write(ghost_line_file, &copy, sizeof(copy)) != sizeof(copy)) {
					return 1;
				}
				ghost_line_heads[tile] = ghost_line_count++;
			}
		}
	}
	for (legacy_s16 mode = 0; mode < 2; mode++) {
		supersight_enabled = mode;
		frame_supersight_reset();
		if (ghost_line_capture(mode == 0 ? "DENSE0.RAW" : "DENSE1.RAW", 0)) {
			return 1;
		}
	}
	return 0;
}

static legacy_s16 ghost_line_check(void)
{
	cvxptr = mmgr_alloc_resbytes("cvx", sizeof(struct GAMESTATE) * GAMESTATE_CHECKPOINT_COUNT);
	(void)file_load_replay("", "DEFAULT");
	if (ghost_select_replay("", "DEFAULT") != 0) {
		return 1;
	}
	gameconfig.game_opponenttype = 0;
	framespersec = gameconfig.game_framespersec;
	if (ghost_prepare_race() != 0 || ghost_is_active() == 0) {
		return 1;
	}
	legacy_u32 segments = 0;
	for (legacy_s16 south = 0; south < 30; south++) {
		for (legacy_s16 east = 0; east < 30; east++) {
			for (const struct GHOST_RACING_LINE_SEGMENT *segment =
					 ghost_racing_line_first(east, south);
				 segment != 0; segment = ghost_racing_line_next(segment)) {
				if (++segments > 131072UL) {
					return 1;
				}
			}
		}
	}
	if (segments == 0) {
		return 1;
	}
	(void)file_load_replay("", "DEFAULT");
	gameconfig.game_opponenttype = 0;
	gameconfigcopy = gameconfig;
	if (track_setup() != 0) {
		return 1;
	}
	replay_filename[0] = 0;
	init_game_state(-1);
	viewport_bottom_cache = -1;
	run_game_random = LEGACY_S16_SHL(get_kevinrandom(), 3U);
	replaybar_toggle = 0;
	is_in_replay = 1;
	idle_expired = 0;
	cameramode = CAMERA_MODE_COCKPIT;
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	detail_level = 0;
	slow_video_mgmt = slow_video_mgmt_copy = 1;
	if (setup_player_cars_without_dashboard() != 0) {
		return 1;
	}
	kbormouse = 0;
	replay_playback_speed = REPLAY_PLAYBACK_NORMAL;
	race_exit_request = 1;
	game_replay_mode_copy = -1;
	frame_buffer_index = dashboard_buffer_index = 0;
	recording_limit_warning_requested = 0;
	dashb_toggle = 0;
	followOpponentFlag = 0;
	start_flag_animation = 500;
	rect_windshield.left = 0;
	rect_windshield.right = GAME_SCREEN_WIDTH;
	rect_windshield.top = 0;
	rect_windshield.bottom = GAME_SCREEN_HEIGHT;
	set_projection(35, GAME_SCREEN_HEIGHT / 6, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT);
	restore_gamestate(0);
	while (state.game_frame < 100) {
		update_gamestate();
	}
	shape3d_set_legacy_render_stack(0, 0, 0, 0);
	legacy_u16 reference_speed = state.playerstate.car_actual_speed;
	ghost_update(gameconfig.game_recordedframes, framespersec);
	legacy_s16 result = 0;
	for (legacy_s16 mode = 0; mode < 2 && result == 0; mode++) {
		supersight_enabled = mode;
		frame_supersight_reset();
		result = ghost_line_capture(mode == 0 ? "GREEN0.RAW" : "GREEN1.RAW", 0) ||
				 ghost_line_capture(mode == 0 ? "RED0.RAW" : "RED1.RAW", LEGACY_U16_MAX) ||
				 ghost_line_capture(mode == 0 ? "YELLOW0.RAW" : "YELLOW1.RAW", reference_speed);
	}
	if (result == 0) {
		result = ghost_line_check_full_queue();
	}
	ghost_end_race();
	if (ghost_is_active() != 0) {
		result = 1;
	}
	result = result || ghost_line_capture("CLEAR0.RAW", 0) ||
			 ghost_line_capture("CLEAR1.RAW", LEGACY_U16_MAX);
	ghost_clear();
	/* This dump-style fixture has no cockpit allocation to unload. */
	mmgr_free(sdgameresptr);
	idle_expired = 1;
	free_player_cars();
	mmgr_release(cvxptr);
	return result;
}

legacy_s16 run_main_menu_loop(legacy_s16 argc, legacy_s8 *argv[])
{
	init_full_game(argc, argv);
	legacy_s16 result = ghost_line_check_large_stream();
	if (result == 0) {
		result = ghost_line_check();
	}
	shutdown_dos_game();
	if (result == 0) {
		static const legacy_s8 success[] = "DOS ghost racing line check passed\r\n";
		dos_write_stdout(success, sizeof(success) - 1);
	} else {
		static const legacy_s8 failure[] = "DOS ghost racing line check failed\r\n";
		dos_write_stdout(failure, sizeof(failure) - 1);
	}
	return result;
}
