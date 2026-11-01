#include <conio.h>

/* Include the complete entry module so this test uses the game's real startup
 * and retains its code/data footprint, including the menu and intro paths. */
#define run_main_menu_loop race_memory_original_main_menu_loop
#include "../c/restunts.c"
#include "../c/startup.c"
#undef run_main_menu_loop

#include "frame/frame_internal.h"
#include "../c/shape2d/shape2d_internal.h"
#include "../c/video/video_pages.h"

static legacy_u32 race_memory_free_bytes;

#define RACE_MEMORY_MARKER_WIDTH 4U
#define RACE_MEMORY_MARKER_X (GAME_SCREEN_WIDTH - RACE_MEMORY_MARKER_WIDTH)
#define RACE_MEMORY_MARKER_Y (GAME_SCREEN_HEIGHT - 1)
#define RACE_MEMORY_VGA_CRTC_INDEX_PORT 0x3D4U
#define RACE_MEMORY_VGA_CRTC_DATA_PORT 0x3D5U
#define RACE_MEMORY_VGA_SEGMENT 0xA000U

static legacy_u8 race_memory_read_crtc(legacy_u8 index)
{
	outp(RACE_MEMORY_VGA_CRTC_INDEX_PORT, index);
	return (legacy_u8)inp(RACE_MEMORY_VGA_CRTC_DATA_PORT);
}

static legacy_s16 race_memory_check_scanout(void)
{
	/* Read the VGA color CRTC independently of the renderer's port constants.
	 * VRAM readback alone cannot catch programming the monochrome CRTC. */
	if ((race_memory_read_crtc(0x14U) & 0x40U) != 0 ||
		(race_memory_read_crtc(0x17U) & 0x40U) == 0 || race_memory_read_crtc(0x13U) != 40U) {
		return 1;
	}
	legacy_u16 expected =
		(dos_memory_pointer_segment(screen_sprite.sprite_bitmapptr) - RACE_MEMORY_VGA_SEGMENT) *
		16U;
	legacy_u16 actual =
		((legacy_u16)race_memory_read_crtc(0x0CU) << 8) | race_memory_read_crtc(0x0DU);
	return actual != expected;
}

static legacy_s16 race_memory_check_marker(const legacy_u8 *expected)
{
	static struct {
		struct SHAPE2D header;
		legacy_u8 pixels[RACE_MEMORY_MARKER_WIDTH];
	} capture = {{RACE_MEMORY_MARKER_WIDTH, 1, 0, 0, 0, 0, {0, 0, 0, 0}}, {0, 0, 0, 0}};
	sprite_clear_shape_alt(&capture.header, RACE_MEMORY_MARKER_X, RACE_MEMORY_MARKER_Y);
	for (legacy_u16 pixel = 0; pixel < RACE_MEMORY_MARKER_WIDTH; pixel++) {
		if (capture.pixels[pixel] != expected[pixel]) {
			return 1;
		}
	}
	return 0;
}

static legacy_s16 race_memory_draw(void)
{
	legacy_u8 markers[2][RACE_MEMORY_MARKER_WIDTH];
	legacy_u8 valid_pages = 0;
	init_game_state_with_frame_rate(configured_frame_rate);
	cameramode = CAMERA_MODE_COCKPIT;
	game_replay_mode = REPLAY_MODE_PAUSED;
	dashboard_visible = 1;
	dashb_toggle = 1;
	height_above_replaybar = GAME_SCREEN_HEIGHT;
	dashbmp_y_copy = dashbmp_y;
	roofbmpheight_copy = roofbmpheight;
	rect_windshield.left = 0;
	rect_windshield.right = GAME_SCREEN_WIDTH;
	rect_windshield.top = roofbmpheight;
	rect_windshield.bottom = dashbmp_y;
	set_projection(35, dashbmp_y / 6, GAME_SCREEN_WIDTH, dashbmp_y);
	fps_display_enabled = 1;
	frame_fps_reset();
	for (legacy_s16 pass = 0; pass < 3; pass++) {
		supersight_enabled = pass == 1;
		frame_supersight_reset();
		init_rect_arrays();
		full_redraw_frames_remaining = video_page_count;
		/* Visit both pages three times: change the gear knob and needles, hide
		 * the knob, then reuse the stored dashboard pixels on the same page. */
		for (legacy_s16 step = 0; step < 6; step++) {
			legacy_u8 page = (legacy_u8)frame_buffer_index;
			dashboard_buffer_index = frame_buffer_index;
			sprite_select_mcga_backbuffer();
			if (video_pages_is_target(drawing_sprite.sprite_bitmapptr) == 0 ||
				((valid_pages & (1U << page)) != 0 && race_memory_check_marker(markers[page]))) {
				return 1;
			}
			if (full_redraw_frames_remaining != 0) {
				sprite_clear_target(0);
				setup_car_shapes(DASHBOARD_OPERATION_REDRAW_STATIC);
			}
			update_frame(frame_buffer_index, &rect_windshield);
			frame_present(&rect_windshield);
			frame_fps_present_roof();
			state.playerstate.car_knob_x = simd_player.knob_points[pass + 1].px;
			state.playerstate.car_knob_y = simd_player.knob_points[pass + 1].py;
			state.playerstate.car_changing_gear = step < 2;
			state.playerstate.car_gear_change_delay = step < 2;
			state.playerstate.car_rev_speed = (legacy_u16)((pass * 6 + step) * 5) << 8;
			state.playerstate.car_currpm = (pass * 6 + step) * 300;
			sprite_set_target_clip_bounds(0, GAME_SCREEN_WIDTH, dashbmp_y_copy,
										  height_above_replaybar);
			setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
			sprite_set_target_clip_bounds(0, GAME_SCREEN_WIDTH, 0, GAME_SCREEN_HEIGHT);
			mouse_draw_opaque_check();
			frame_fps_draw_text();
			mouse_draw_transparent_check();
			/* Four adjacent pixels cover every VGA plane at the end of the page. */
			for (legacy_u16 pixel = 0; pixel < RACE_MEMORY_MARKER_WIDTH; pixel++) {
				markers[page][pixel] = (legacy_u8)(32 + (pass * 6 + step) * 4 + pixel);
				sprite_putpixel_clipped(RACE_MEMORY_MARKER_X + pixel, RACE_MEMORY_MARKER_Y,
										markers[page][pixel]);
			}
			valid_pages |= 1U << page;
			mouse_draw_opaque_check();
			sprite_present_mcga_backbuffer();
			sprite_select_screen();
			if (race_memory_check_marker(markers[page]) != 0 || race_memory_check_scanout() != 0) {
				return 1;
			}
			frame_buffer_index ^= 1;
			dashboard_buffer_index = frame_buffer_index;
			mouse_draw_transparent_check();
			frame_fps_record_presented();
			if (full_redraw_frames_remaining != 0) {
				full_redraw_frames_remaining--;
			}
		}
	}
	return 0;
}

static legacy_s16 race_memory_check(void)
{
	init_main_input_state();
	set_default_car();
	_memcpy(gameconfig.game_playercarid, "DIA3", 4);
	_memcpy(gameconfig.game_opponentcarid, "CSIL", 4);
	gameconfig.game_opponenttype = 1;
	detail_level = 0;
	configured_frame_rate = GAME_FRAME_RATE_NORMAL;
	framespersec = GAME_FRAME_RATE_NORMAL;
	strcpy(gameconfig.game_trackname, "DEFAULT");
	idle_expired = 0;
	file_build_path(track_directory, gameconfig.game_trackname, ".trk", g_path_buf);
	file_read_fatal(g_path_buf, track_element_map);
	main_menu_backup_track();
	if (main_menu_prepare_track() != 1) {
		return 1;
	}

	cvxptr = mmgr_alloc_resbytes("cvx", sizeof(struct GAMESTATE) * GAMESTATE_CHECKPOINT_COUNT);
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
	gameconfig.game_recordedframes = 0;
	show_waiting();
	legacy_s16 result = setup_player_cars();
	race_memory_free_bytes = mmgr_get_res_ofs_diff_scaled();
	if (result == 0) {
		/* Load the actual cockpit while keeping both complete render pages in
		 * VGA memory. Replay dump tools cannot cover this allocation lifetime. */
		if (video_pages_is_active() == 0 || video_uses_page_flipping == 0 ||
			video_page_count != 2 || render_window_sprite != 0 || stdaresptr == 0 ||
			stdbresptr == 0 || dashboard_instrument_sprite == 0 || frame_buffer_index != 0 ||
			dashboard_buffer_index != 0 || race_memory_check_scanout() != 0) {
			result = 1;
		} else {
			result = race_memory_draw();
		}
	}
	free_player_cars();
	if (video_uses_page_flipping != 0 || video_page_count != 1 ||
		drawing_sprite.sprite_bitmapptr != screen_sprite.sprite_bitmapptr) {
		result = 1;
	}
	mmgr_release(cvxptr);
	return result;
}

legacy_s16 run_main_menu_loop(legacy_s16 argc, legacy_s8 *argv[])
{
	init_full_game(argc, argv);
	legacy_s16 result = race_memory_check();
	if (result == 0) {
		/* Repeat the complete resource lifetime, including shared window release. */
		legacy_u32 first_free_bytes = race_memory_free_bytes;
		result = race_memory_check();
		if (race_memory_free_bytes < first_free_bytes) {
			result = 1;
		}
	}
	shutdown_dos_game();
	legacy_s8 remaining[] = "Free bytes after race allocation: ";
	dos_write_stdout(remaining, sizeof(remaining) - 1);
	legacy_s8 digits[11];
	legacy_u16 length = 0;
	do {
		digits[length++] = (legacy_s8)('0' + race_memory_free_bytes % 10UL);
		race_memory_free_bytes /= 10UL;
	} while (race_memory_free_bytes != 0);
	while (length != 0) {
		dos_write_stdout(&digits[--length], 1);
	}
	dos_write_stdout("\r\n", 2);
	if (result == 0) {
		legacy_s8 success[] = "DIA3 / CSIL race memory check passed\r\n";
		dos_write_stdout(success, sizeof(success) - 1);
	} else {
		legacy_s8 failure[] = "DIA3 / CSIL race memory check failed\r\n";
		dos_write_stdout(failure, sizeof(failure) - 1);
	}
	return result;
}
