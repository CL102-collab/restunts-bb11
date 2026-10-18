/* Opt-in native integration test. It follows the classic replay capture path
 * and additionally loads/draws the real dashboard. No synthetic game assets
 * or alternate renderer are involved. */
#include <exec/memory.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>
#include "amiga.h"
#include "../../c/externs.h"
#include "../../c/restunts.h"
#include "../../c/platform.h"
#include "../../c/fileio.h"
#include "../../c/memmgr.h"
#include "../../c/shape2d.h"
#include "../../c/shape3d.h"
#include "../../c/scene_resources.h"
#include "../../c/trackdata_layout.h"
#include "../../c/track_objects.h"
#include "../../c/race_resources.h"
#include "../../c/dashboard.h"
#include "../../c/camera.h"
#include "../../c/game_input.h"
#include "../../c/audio_control.h"
#include "../../c/fatal.h"

/* Diagnostics use the native stdio ABI, including 32-bit memory sizes. */
#undef printf

#define SMOKE_REPLAY_TICKS 3U
#define SMOKE_CHECKPOINT_BYTES 22400UL

static legacy_s16 smoke_check(legacy_s16 condition, const char *description)
{
	if (!condition) {
		printf("AMIGA SMOKE FAIL: %s\n", description);
		fflush(stdout);
	}
	return condition;
}

static void smoke_progress(const char *description)
{
	printf("AMIGA SMOKE: %s\n", description);
	fflush(stdout);
}

/* Export the same 12-bit, 32-entry palette and index mapping sent to ECS. */
static legacy_s16 smoke_write_picture(void)
{
	static const char header[] = "P6\n320 200\n255\n";
	legacy_u8 row[AMIGA_WIDTH * 3U];
	const legacy_u8 *pixels = amiga_video_pixels();
	legacy_u16 output = dos_file_open((const legacy_s8 *)"AMIGASMOKE.PPM", DOS_FILE_CREATE);
	legacy_s16 ok;
	if (!output) {
		return 0;
	}
	ok = dos_file_write(output, header, sizeof(header) - 1U) == sizeof(header) - 1U;
	for (legacy_u16 y = 0; ok && y < AMIGA_HEIGHT; ++y) {
		for (legacy_u16 x = 0; x < AMIGA_WIDTH; ++x) {
			legacy_u16 color = amiga_video_color(y, pixels[(legacy_u32)y * AMIGA_WIDTH + x]);
			row[x * 3U] = ((color >> 8U) & 15U) * 17U;
			row[x * 3U + 1U] = ((color >> 4U) & 15U) * 17U;
			row[x * 3U + 2U] = (color & 15U) * 17U;
		}
		ok = dos_file_write(output, row, sizeof(row)) == sizeof(row);
	}
	if (dos_file_close(output) != 0) {
		ok = 0;
	}
	return ok;
}

static legacy_u16 smoke_region_colors(legacy_u16 top, legacy_u16 bottom)
{
	legacy_u8 seen[4096 / 8];
	legacy_u16 count = 0;
	const legacy_u8 *pixels = amiga_video_pixels();
	if (top >= bottom || bottom > AMIGA_HEIGHT) {
		return 0;
	}
	memset(seen, 0, sizeof(seen));
	for (legacy_u32 offset = (legacy_u32)top * AMIGA_WIDTH;
		 offset < (legacy_u32)bottom * AMIGA_WIDTH; ++offset) {
		legacy_u16 color = amiga_video_color(offset / AMIGA_WIDTH, pixels[offset]);
		if (!(seen[color >> 3] & (1U << (color & 7U)))) {
			seen[color >> 3] |= 1U << (color & 7U);
			++count;
		}
	}
	return count;
}

static void smoke_render_frame(void)
{
	sprite_select_render_window();
	if (full_redraw_frames_remaining != 0) {
		sprite_set_target_clip_bounds(0, AMIGA_WIDTH, dashbmp_y_copy, height_above_replaybar);
		setup_car_shapes(DASHBOARD_OPERATION_REDRAW_STATIC);
	}
	update_frame(0, &rect_windshield);
	if (dastbmp_y != 0) {
		shape2d_render_bmp_as_mask(dasmshapeptr);
		shape2d_rle_or_far_pointer(dastbmp_y2, dastseg);
	}
	frame_present(&rect_windshield);
	sprite_set_target_clip_bounds(0, AMIGA_WIDTH, dashbmp_y_copy, height_above_replaybar);
	setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
	sprite_set_target_clip_bounds(0, AMIGA_WIDTH, 0, AMIGA_HEIGHT);
	mouse_draw_opaque_check();
	amiga_video_present();
	if (full_redraw_frames_remaining != 0) {
		--full_redraw_frames_remaining;
	}
}

legacy_s16 amiga_game_smoke(void)
{
	legacy_s8 program[] = "restunts";
	legacy_s8 *arguments[] = {program};
	legacy_u16 initial_frame, world_colors, dashboard_colors;
	legacy_u32 hash = 2166136261UL;
	legacy_u32 highwater, capacity;
	legacy_u32 started, rendering_started, rendering_ticks, loading_ticks;
	legacy_s16 cars_ready = 0, ok = 1;
	smoke_progress("initialize original game assets and native platform");
	started = amiga_clock_ticks();
	init_main(1, arguments);
	init_div0();
	init_row_tables();
	mainresptr = file_load_resfile((const legacy_s8 *)"main");
	fontdefptr = file_load_resource(FILE_RESOURCE_BINARY_FATAL, (const legacy_s8 *)"fontdef.fnt");
	fontnptr = file_load_resource(FILE_RESOURCE_BINARY_FATAL, (const legacy_s8 *)"fontn.fnt");
	font_set_fontdef();
	init_polyinfo();
	init_trackdata();
	reset_race_loop_state();
	init_kevinrandom((const legacy_s8 *)"kevin");
	smoke_progress("load DEFAULT.RPL and build its track");
	if (!smoke_check(file_load_replay((const legacy_s8 *)"", (const legacy_s8 *)"DEFAULT") == 0,
					 "DEFAULT.RPL could not be loaded")) {
		ok = 0;
		goto finished;
	}
	if (!smoke_check(gameconfig.game_recordedframes >= SMOKE_REPLAY_TICKS &&
						 (gameconfig.game_framespersec == GAME_FRAME_RATE_LOW ||
						  gameconfig.game_framespersec == GAME_FRAME_RATE_NORMAL),
					 "replay frame count or rate is invalid")) {
		ok = 0;
		goto finished;
	}
	replay_filename[0] = 0;
	memcpy(&gameconfigcopy, &gameconfig, sizeof(gameconfig));
	for (legacy_u16 index = 0; index < TRACKDATA_LINK_TABLE_SIZE; ++index) {
		track_and_directory_backup[index] = track_element_map[index];
	}
	for (legacy_u16 index = 0; index < TRACKDATA_CHECKPOINT_DATA_SIZE; ++index) {
		track_and_directory_backup[index + TRACKDATA_LINK_TABLE_SIZE] = track_directory[index];
		track_and_directory_backup[index + TRACKDATA_CHECKPOINT_SECOND_OFFSET] =
			replay_directory[index];
	}
	if (!smoke_check(track_setup() == 0, "track route setup failed")) {
		ok = 0;
		goto finished;
	}
	cvxptr = mmgr_alloc_resbytes((const legacy_s8 *)"cvx", SMOKE_CHECKPOINT_BYTES);
	framespersec = gameconfig.game_framespersec;
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
	viewport_bottom_cache = -1;
	run_game_random = LEGACY_S16_SHL(get_kevinrandom(), 3U);
	replaybar_toggle = 0;
	is_in_replay = 1;
	idle_expired = 0;
	camera_select_mode(CAMERA_MODE_COCKPIT);
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	detail_level = 0;
	slow_video_mgmt = 0;
	slow_video_mgmt_copy = 0;
	supersight_enabled = 0;
	smoke_progress("load car, dashboard, sound, skybox and track geometry");
	if (!smoke_check(setup_player_cars() == 0, "race resource setup failed")) {
		ok = 0;
		goto finished;
	}
	cars_ready = 1;
	if (!smoke_check(game1ptr != NULL && game2ptr != NULL &&
						 game3dshapes[PLAYER_CAR_LOW_SHAPE].shape3d_numverts != 0 &&
						 stdaresptr != NULL && stdbresptr != NULL && render_window_sprite != NULL &&
						 dashbmp_y > roofbmpheight && (legacy_u16)dashbmp_y < AMIGA_HEIGHT,
					 "loaded scene or dashboard geometry is invalid")) {
		ok = 0;
		goto finished;
	}
	kbormouse = 0;
	replay_playback_speed = REPLAY_PLAYBACK_NORMAL;
	race_exit_request = 1;
	game_replay_mode_copy = -1;
	frame_buffer_index = dashboard_buffer_index = 0;
	recording_limit_warning_requested = 0;
	dashb_toggle = dashboard_visible = 1;
	replaybar_enabled = 0;
	followOpponentFlag = 0;
	height_above_replaybar = AMIGA_HEIGHT;
	roofbmpheight_copy = roofbmpheight;
	dashbmp_y_copy = dashbmp_y;
	start_flag_animation = 500;
	rect_windshield.left = 0;
	rect_windshield.right = AMIGA_WIDTH;
	rect_windshield.top = roofbmpheight;
	rect_windshield.bottom = dashbmp_y;
	set_projection(35, dashbmp_y / 6, AMIGA_WIDTH, dashbmp_y);
	restore_gamestate(0);
	init_rect_arrays();
	full_redraw_frames_remaining = 1;
	initial_frame = (legacy_u16)state.game_frame;
	smoke_progress("render cockpit and advance recorded driving input");
	rendering_started = amiga_clock_ticks();
	loading_ticks = rendering_started - started;
	smoke_render_frame();
	for (legacy_u16 frame = 0; frame < SMOKE_REPLAY_TICKS; ++frame) {
		update_gamestate();
		smoke_render_frame();
	}
	rendering_ticks = amiga_clock_ticks() - rendering_started;
	world_colors =
		smoke_region_colors((legacy_u16)rect_windshield.top, (legacy_u16)rect_windshield.bottom);
	dashboard_colors = smoke_region_colors((legacy_u16)dashbmp_y, AMIGA_HEIGHT);
	ok &= smoke_check((legacy_u16)state.game_frame == initial_frame + SMOKE_REPLAY_TICKS,
					  "recorded simulation did not advance three frames");
	ok &= smoke_check(state.playerstate.car_position.lx != 0 &&
						  state.playerstate.car_position.lz != 0,
					  "player world position was not initialized");
	ok &= smoke_check(world_colors >= 4U, "rendered world is blank or lacks geometry");
	ok &= smoke_check(dashboard_colors >= 4U, "rendered dashboard is blank");
	for (legacy_u32 offset = 0; offset < AMIGA_SCREEN_BYTES; ++offset) {
		hash = (hash ^ amiga_video_pixels()[offset]) * 16777619UL;
	}
	highwater = (legacy_u32)(mmgr_high_water_segment - mmgr_arena_start_segment) * 16UL;
	capacity = (legacy_u32)(mmgr_arena_end_segment - mmgr_arena_start_segment) * 16UL;
	printf("Amiga smoke: frame=%u world_colors=%u dashboard_colors=%u checksum=%08lx\n",
		   (unsigned)(legacy_u16)state.game_frame, (unsigned)world_colors,
		   (unsigned)dashboard_colors, (unsigned long)hash);
	printf("Amiga memory: resource_high_water=%lu arena_capacity=%lu free_chip=%lu bytes\n",
		   (unsigned long)highwater, (unsigned long)capacity, (unsigned long)AvailMem(MEMF_CHIP));
	printf("Amiga timing: load_ms=%lu render_physics_ms=%lu frames=%u average_ms_per_frame=%lu\n",
		   (unsigned long)loading_ticks * 10UL, (unsigned long)rendering_ticks * 10UL,
		   (unsigned)(SMOKE_REPLAY_TICKS + 1U),
		   (unsigned long)rendering_ticks * 10UL / (SMOKE_REPLAY_TICKS + 1U));
	ok &= smoke_check(smoke_write_picture(), "AMIGASMOKE.PPM export failed");
	printf("Amiga smoke total_ms=%lu\n", (unsigned long)(amiga_clock_ticks() - started) * 10UL);
finished:
	fflush(stdout);
	if (cars_ready) {
		smoke_progress("release race resources");
		free_player_cars();
	}
	smoke_progress("shutdown audio and game exit handlers");
	call_exitlist();
	smoke_progress("game exit handlers complete");
	if (ok) {
		puts("AMIGA SMOKE PASS");
	}
	return ok ? 0 : 1;
}
