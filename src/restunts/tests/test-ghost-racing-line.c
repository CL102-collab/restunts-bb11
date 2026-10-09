#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/externs.h"
#include "../c/restunts.h"
#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/platform.h"
#include "../c/frame_internal.h"
#include "../c/game_input.h"
#include "../c/race_resources.h"
#include "../c/camera.h"
#include "../c/shape2d.h"
#include "../c/track_objects.h"
#include "../c/hires.h"
#include "../c/ghost.h"
#include "../c/resource.h"

#undef memcpy
#undef printf

static void write_capture(const legacy_u8 *pixels, legacy_s32 width, legacy_s32 height,
						  legacy_s16 mode, const char *name)
{
	const char *directory = getenv("RESTUNTS_TEST_CAPTURE_DIR");
	if (directory == NULL) {
		return;
	}
	char filename[1024];
	int length = snprintf(filename, sizeof(filename), "%s/line-%d-%s.ppm", directory, mode, name);
	assert(length > 0 && (size_t)length < sizeof(filename));
	FILE *file = fopen(filename, "wb");
	assert(file != NULL);
	legacy_s8 *resource = file_load_shape2d_fatal("sdmain");
	const legacy_u8 *palette =
		(const legacy_u8 *)locate_shape_fatal(resource, "!pal") + SHAPE2D_HEADER_SIZE;
	fprintf(file, "P6\n%ld %ld\n255\n", (long)width, (long)height);
	for (size_t index = 0; index < (size_t)width * height; index++) {
		legacy_u8 rgb[3];
		for (legacy_u16 channel = 0; channel < 3; channel++) {
			rgb[channel] = (palette[pixels[index] * 3U + channel] & 63U) * 255U / 63U;
		}
		assert(fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb));
	}
	assert(fclose(file) == 0);
	mmgr_free(resource);
}

/* Exercise the actual replay reconstruction and both rasterizers, rather than
 * merely checking the geometry submitted by the frame unit-test harness. */
static legacy_u8 *capture(legacy_u16 speed, legacy_s32 *width, legacy_s32 *height)
{
	struct GAMESTATE saved = state;
	struct GAMEINFO saved_config = gameconfig;
	struct GAMESTATE presentation = state;
	legacy_s8 seed_before[GAMESTATE_RANDOM_SEED_SIZE];
	legacy_s8 seed_after[GAMESTATE_RANDOM_SEED_SIZE];
	get_kevinrandom_seed(seed_before);
	presentation.playerstate.car_actual_speed = speed;
	full_redraw_frames_remaining = 1;
	sprite_select_render_window();
	update_frame_snapshot(0, &rect_windshield, &presentation, ghost_car_state(),
						  ghost_camera_state());
	frame_present(&rect_windshield);
	get_kevinrandom_seed(seed_after);
	assert(memcmp(&state, &saved, sizeof(state)) == 0);
	assert(memcmp(&gameconfig, &saved_config, sizeof(gameconfig)) == 0);
	assert(memcmp(seed_before, seed_after, sizeof(seed_before)) == 0);
	const legacy_u8 *pixels = hires_framebuffer(dos_memory_make_pointer(0xA000U, 0), width, height);
	assert(pixels != NULL);
	size_t size = (size_t)*width * *height;
	legacy_u8 *copy = malloc(size);
	assert(copy != NULL);
	memcpy(copy, pixels, size);
	return copy;
}

static void check_colors(legacy_u16 reference_speed, legacy_s16 mode)
{
	legacy_s32 width, height, other_width, other_height;
	legacy_u8 *green = capture(0, &width, &height);
	legacy_u8 *red = capture(LEGACY_U16_MAX, &other_width, &other_height);
	assert(width == other_width && height == other_height);
	legacy_u8 *yellow = capture(reference_speed, &other_width, &other_height);
	assert(width == other_width && height == other_height);
	legacy_u32 colored = 0;
	legacy_u32 neutral = 0;
	for (size_t index = 0; index < (size_t)width * height; index++) {
		if (green[index] == 10 && red[index] == 12) {
			colored++;
			if (yellow[index] == 14) {
				neutral++;
			}
		}
	}
	printf("Ghost racing line mode %d: %lu green/red pixels, %lu yellow pixels (%ldx%ld)\n", mode,
		   (unsigned long)colored, (unsigned long)neutral, (long)width, (long)height);
	write_capture(green, width, height, mode, "green");
	write_capture(red, width, height, mode, "red");
	write_capture(yellow, width, height, mode, "yellow");
	assert(colored > 0);
	assert(neutral > 0);
	free(green);
	free(red);
	free(yellow);
}

legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[])
{
	assert(argc == 2);
	init_main(argc, argv);
	init_div0();
	init_row_tables();
	mainresptr = file_load_resfile("main");
	fontdefptr = file_load_resource(0, "fontdef.fnt");
	fontnptr = file_load_resource(0, "fontn.fnt");
	font_set_fontdef();
	init_polyinfo();
	init_trackdata();
	reset_race_loop_state();
	init_kevinrandom("kevin");
	cvxptr = mmgr_alloc_resbytes("cvx", sizeof(struct GAMESTATE) * GAMESTATE_CHECKPOINT_COUNT);
	assert(file_load_replay("", argv[1]) == 0);
	assert(ghost_select_replay("", argv[1]) == 0);
	gameconfig.game_opponenttype = 0;
	framespersec = gameconfig.game_framespersec;
	assert(ghost_prepare_race() == 0);
	assert(ghost_is_active() != 0);
	/* Reload the controls after reconstruction and start the live player's race. */
	assert(file_load_replay("", argv[1]) == 0);
	gameconfig.game_opponenttype = 0;
	gameconfigcopy = gameconfig;
	assert(track_setup() == 0);
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
	assert(setup_player_cars_without_dashboard() == 0);
	kbormouse = 0;
	replay_playback_speed = REPLAY_PLAYBACK_NORMAL;
	race_exit_request = 1;
	game_replay_mode_copy = -1;
	frame_buffer_index = dashboard_buffer_index = 0;
	recording_limit_warning_requested = 0;
	dashb_toggle = 0;
	followOpponentFlag = 0;
	start_flag_animation = 500;
	rect_windshield = (struct RECTANGLE){0, 320, 0, 200};
	set_projection(35, 200 / 6, 320, 200);
	restore_gamestate(0);
	const char *requested_frame = getenv("RESTUNTS_TEST_PLAYER_FRAME");
	legacy_s16 player_frame = requested_frame != NULL ? atoi(requested_frame) : 100;
	assert(player_frame > 0 && player_frame < gameconfig.game_recordedframes);
	while (state.game_frame < player_frame) {
		update_gamestate();
	}
	init_rect_arrays();
	shape3d_set_legacy_render_stack(NULL, 0, 0, NULL);
	/* The live fixture follows the same controls, so its current speed matches
	 * the nearby recorded segment even while the ghost brakes farther ahead. */
	legacy_u16 reference_speed = state.playerstate.car_actual_speed;
	/* The line near the player must remain after the ghost has left this area. */
	ghost_update(gameconfig.game_recordedframes, framespersec);
	check_colors(reference_speed, 0);
	supersight_enabled = 1;
	hires_set_enabled(1);
	for (legacy_s16 scale = 1; scale <= 4; scale *= 2) {
		hires_set_render_scale(scale);
		check_colors(reference_speed, scale);
	}
	ghost_end_race();
	legacy_s32 width, height, other_width, other_height;
	legacy_u8 *slow = capture(0, &width, &height);
	legacy_u8 *fast = capture(LEGACY_U16_MAX, &other_width, &other_height);
	assert(width == other_width && height == other_height);
	assert(memcmp(slow, fast, (size_t)width * height) == 0);
	free(slow);
	free(fast);
	ghost_clear();
	puts("Ghost racing line: live speed colors, distant ghost, and physics isolation passed");
	return 0;
}
