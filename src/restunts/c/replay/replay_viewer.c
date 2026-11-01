#include "replay_viewer_private.h"

static legacy_s16 replay_try_zoom(legacy_u16 input)
{
	if (input == '-') {
		if (cameramode == CAMERA_MODE_TRACKSIDE) {
			if (camera_track_height_offset <= 0) {
				return 0;
			}
			camera_track_height_offset =
				LEGACY_S16_WRAP_SUB(camera_track_height_offset, CAMERA_ZOOM_STEP);
		} else {
			if (custom_camera.distance >= REPLAY_CUSTOM_CAMERA_MAX_DISTANCE) {
				return 0;
			}
			custom_camera.distance = LEGACY_S16_WRAP_ADD(custom_camera.distance, CAMERA_ZOOM_STEP);
		}
	} else {
		if (cameramode == CAMERA_MODE_TRACKSIDE) {
			if (camera_track_height_offset >= REPLAY_TRACK_CAMERA_MAX_HEIGHT) {
				return 0;
			}
			camera_track_height_offset =
				LEGACY_S16_WRAP_ADD(camera_track_height_offset, CAMERA_ZOOM_STEP);
		} else {
			if (custom_camera.distance <= REPLAY_CUSTOM_CAMERA_MIN_DISTANCE) {
				return 0;
			}
			custom_camera.distance = LEGACY_S16_WRAP_SUB(custom_camera.distance, CAMERA_ZOOM_STEP);
		}
	}
	return 1;
}

static legacy_u16 replay_pan_input(void)
{
	legacy_s16 y_delta = LEGACY_S16_WRAP_SUB(
		LEGACY_S16_SAR(LEGACY_S16_WRAP_ADD(replay_pan_button_top, replay_pan_button_bottom), 1U),
		(legacy_s16)mouse_ypos);
	legacy_s16 x_delta = LEGACY_S16_WRAP_SUB(
		(legacy_s16)mouse_xpos,
		LEGACY_S16_SAR(LEGACY_S16_WRAP_ADD(replay_pan_button_left, replay_pan_button_right), 1U));
	legacy_u16 angle = (legacy_u16)polarAngle(x_delta, y_delta);
	legacy_u16 input = 0;
	switch (((angle + ANGLE_EIGHTH_TURN) >> REPLAY_DIRECTION_ANGLE_SHIFT) & REPLAY_DIRECTION_MASK) {
		case REPLAY_DIRECTION_UP:
			input = KEY_UP;
			break;
		case REPLAY_DIRECTION_RIGHT:
			input = KEY_RIGHT;
			break;
		case REPLAY_DIRECTION_DOWN:
			input = KEY_DOWN;
			break;
		case REPLAY_DIRECTION_LEFT:
			input = KEY_LEFT;
			break;
	}
	return input;
}

static legacy_u16 replay_read_control_input(legacy_s16 delta)
{
	legacy_u16 input = (legacy_u16)input_checking(delta);
	legacy_u8 hit = (legacy_u8)mouse_multi_hittest(
		(legacy_u8)(game_camera_buttons_count[(legacy_u8)cameramode] + 1U), game_camera_buttons);
	if (hit != REPLAY_NO_SELECTION) {
		if (hit != replay_selected_control && input == 0) {
			input = 1;
		}
		replay_selected_control = hit;
		if ((input == KEY_ENTER || input == KEY_SPACE) &&
			replay_selected_control >= REPLAY_CONTROL_ZOOM) {
			if (replay_selected_control == REPLAY_CONTROL_ZOOM) {
				legacy_s16 midpoint = LEGACY_S16_SAR(
					LEGACY_S16_WRAP_ADD(replay_zoom_button_top, replay_zoom_button_bottom), 1U);
				input = midpoint < mouse_ypos ? KEY_DOWN : KEY_UP;
			} else {
				input = replay_pan_input();
			}
		}
	} else {
		hit = (legacy_u8)mouse_multi_hittest(1, &replay_hidden_bar_camera_button);
		if (hit == 0 && (input == KEY_ENTER || input == KEY_SPACE)) {
			input = 'c';
		}
	}
	return input;
}

static legacy_u16 replay_adjust_custom_camera(legacy_u16 *input)
{
	if (camera_adjust_custom_direction(*input) != 0) {
		return 1;
	}
	if (*input != '+' && *input != '-') {
		*input = 0;
	}
	return 0;
}

static legacy_u16 replay_activate_selected_control(void)
{
	if (replay_selected_control > REPLAY_LAST_ACTION_CONTROL) {
		return 0;
	}
	switch (replay_selected_control) {
		case REPLAY_CONTROL_FAST_FORWARD:
			replay_fast_forward();
			return 1;
		case REPLAY_CONTROL_REWIND:
			replay_rewind();
			return 1;
		case REPLAY_CONTROL_FAST_PLAY:
			replay_controls_select(REPLAY_CONTROL_FAST_PLAY);
			replay_playback_speed = REPLAY_PLAYBACK_FAST;
			is_in_replay = 0;
			break;
		case REPLAY_CONTROL_PLAY:
			replay_playback_speed = REPLAY_PLAYBACK_NORMAL;
			replay_controls_select(REPLAY_CONTROL_PLAY);
			is_in_replay = 0;
			break;
		case REPLAY_CONTROL_PAUSE:
			is_in_replay = 1;
			audio_carstate();
			replay_controls_select(REPLAY_CONTROL_PAUSE);
			replay_controls_draw(replay_current_frame(), replay_current_frame());
			break;
		case REPLAY_CONTROL_RESTART:
			is_in_replay = 1;
			audio_carstate();
			replay_controls_select(REPLAY_CONTROL_RESTART);
			replay_controls_draw(replay_current_frame(), replay_current_frame());
			frame_renderer_reset();
			restore_gamestate(REPLAY_FIRST_FRAME);
			(void)timer_wait_ticks(REPLAY_RESTART_WAIT_TICKS);
			replay_controls_select(REPLAY_CONTROL_PAUSE);
			replay_controls_draw(replay_current_frame(), replay_current_frame());
			return 1;
		case REPLAY_CONTROL_MENU:
			replay_pause_menu();
			return 1;
	}
	return 0;
}

static legacy_u16 replay_handle_control_input(legacy_u16 input)
{
	legacy_u8 next_selection;

	switch (input) {
		case KEY_ENTER:
		case KEY_SPACE:
			return replay_activate_selected_control();

		case KEY_ESCAPE:
			replay_pause_menu();
			return 1;

		case KEY_LEFT:
			next_selection = replay_control_left_neighbor[replay_selected_control];
			if (next_selection <= game_camera_buttons_count[(legacy_u8)cameramode]) {
				replay_selected_control = next_selection;
			}
			break;

		case KEY_RIGHT:
			replay_selected_control = replay_control_right_neighbor[replay_selected_control];
			break;

		case KEY_UP:
			if (replay_selected_control == REPLAY_CONTROL_ZOOM) {
				if (replay_try_zoom('+') != 0) {
					return 1;
				}
				break;
			}
			replay_selected_control = replay_control_up_neighbor[replay_selected_control];
			break;

		case KEY_DOWN:
			if (replay_selected_control == REPLAY_CONTROL_ZOOM) {
				if (replay_try_zoom('-') != 0) {
					return 1;
				}
				break;
			}
			replay_selected_control = replay_control_down_neighbor[replay_selected_control];
			break;
	}
	return 0;
}

static void replay_refresh_playback_controls(void)
{
	if (replaybar_enabled == 0) {
		is_in_replay_copy = (legacy_s8)REPLAY_BAR_HIDDEN_STATE;
		viewport_bottom_cache = -1;
	}
	if (is_in_replay != 0 &&
		(replay_legacy_play_active != 0 || replay_legacy_fast_play_active != 0)) {
		replay_controls_select(REPLAY_CONTROL_PAUSE);
	}
	replay_controls_draw(replay_current_frame(), replay_current_frame());
}

static legacy_s16 replay_handle_camera_input(legacy_u16 *input)
{
	legacy_u8 custom_camera_active = 0;
	if (kb_get_key_state(REPLAY_CUSTOM_CAMERA_MODIFIER_SCAN_CODE) != 0 ||
		(replay_selected_control == REPLAY_CONTROL_PAN &&
		 ((legacy_u8)input_combined_flags & INPUT_ACTION_BUTTON_MASK) != 0)) {
		custom_camera_active = 1;
	}
	if (custom_camera_active != 0) {
		if (replay_adjust_custom_camera(input) != 0) {
			return 1;
		}
	}

	if ((*input == '-' || *input == '+') && replay_try_zoom(*input) != 0) {
		return 1;
	}
	return 0;
}

#ifdef RESTUNTS_SDL3
static legacy_s64 replay_touch_seek_target(void)
{
	legacy_s16 direction = sdl3_touch_take_seek();
	legacy_s64 target = replay_input_frame();
	if (direction != 0) {
		target += direction * REPLAY_TOUCH_SKIP_SECONDS * gameconfig.game_framespersec;
	} else if (kbormouse != 0 && (mouse_butstate & REPLAY_TOUCH_LEFT_BUTTON) != 0 &&
			   mouse_xpos >= REPLAY_TIMELINE_X &&
			   mouse_xpos < REPLAY_TIMELINE_X + REPLAY_TIMELINE_WIDTH &&
			   mouse_ypos >= REPLAY_TIMELINE_Y &&
			   mouse_ypos < REPLAY_TIMELINE_Y + REPLAY_TIMELINE_HEIGHT) {
		target = (legacy_s64)(mouse_xpos - REPLAY_TIMELINE_X) * replay_recorded_frames() /
				 REPLAY_TIMELINE_POSITION_RANGE;
	} else {
		return REPLAY_TOUCH_NO_SEEK;
	}
	if (target < REPLAY_FIRST_FRAME) {
		target = REPLAY_FIRST_FRAME;
	} else if (target > replay_recorded_frames()) {
		target = replay_recorded_frames();
	}
	return target;
}
#endif

#ifdef RESTUNTS_SDL3
static legacy_s16 replay_touch_seek(void)
{
	legacy_s64 target = replay_touch_seek_target();
	if (target == REPLAY_TOUCH_NO_SEEK) {
		return 0;
	}
	if (target == replay_input_frame() && target == replay_current_frame() && is_in_replay != 0) {
		return 1;
	}
	is_in_replay = 1;
	audio_carstate();
	replay_controls_select(REPLAY_CONTROL_PAUSE);
	frame_renderer_reset();
	if (extended_replay_ui_seek_draw((legacy_u32)target)) {
		return 1;
	}

	restore_gamestate((legacy_u16)target);
	elapsed_time2 = (legacy_u16)target;
	while ((legacy_u16)state.game_frame != elapsed_time2) {
		update_gamestate();
	}
	replay_controls_draw(state.game_frame, elapsed_time2);
	return 1;
}
#endif

static void replay_handle_input(void)
{
#ifdef RESTUNTS_SDL3
	sdl3_input_set_replay_active(1);
#endif
	if (LEGACY_S8_FROM_BITS(replay_selected_control) >
			LEGACY_S8_FROM_BITS(game_camera_buttons_count[(legacy_u8)cameramode]) &&
		cameramode != CAMERA_MODE_CUSTOM) {
		replay_selected_control = game_camera_buttons_count[(legacy_u8)cameramode];
	}
	sprite_select_screen();
	if (video_uses_page_flipping != 0) {
		dashboard_buffer_index = frame_buffer_index ^ 1;
	}

	legacy_u16 input;
	for (;;) {
#ifdef RESTUNTS_SDL3
		if (sdl3_controller_take_menu_request() != 0) {
			replay_pause_menu();
			return;
		}
		if (input_update_controller_camera() != 0) {
			return;
		}
#endif
		legacy_s16 delta = LEGACY_S16_FROM_BITS((legacy_u16)timer_get_delta_alt());
		input = replay_read_control_input(delta);
#ifdef RESTUNTS_SDL3
		if (replay_touch_seek() != 0) {
			return;
		}
#endif

		if (input != 0 && input != KEY_ESCAPE &&
			(legacy_u8)handle_ingame_kb_shortcuts(input) != 0) {
			return;
		}
		if (is_in_replay == 0 && input == 0) {
			if (replaybar_enabled != 0) {
				replay_controls_draw(replay_current_frame(), replay_current_frame());
			}
			return;
		}
		replay_refresh_playback_controls();

		if (replay_handle_camera_input(&input) != 0) {
			return;
		}

		if (replay_handle_control_input(input) != 0) {
			return;
		}
		if (is_in_replay != 0 && frame_fps_expire_idle() != 0) {
			return;
		}

		replay_controls_draw(replay_current_frame(), replay_current_frame());
	}
}

void loop_game(legacy_s16 operation, legacy_s16 recorded_frame, legacy_s16 current_frame)
{
	if (operation == REPLAY_LOOP_LOAD_RESOURCES) {
		locate_many_resources((legacy_s8 far *)sdgameresptr, replay_control_shape_ids,
							  (legacy_s8 far **)rplyshapes);
		replay_controls_select(REPLAY_CONTROL_PAUSE);
		return;
	}
	if (operation == REPLAY_LOOP_DRAW_CONTROLS) {
#ifdef RESTUNTS_SDL3
		if (extended_replay_ui_draw_current()) {
			return;
		}
#endif
		replay_controls_draw((legacy_u16)recorded_frame, (legacy_u16)current_frame);
		return;
	}
	if (operation == REPLAY_LOOP_SELECT_CONTROL) {
		replay_controls_select((legacy_u8)recorded_frame);
		return;
	}
#ifdef RESTUNTS_SDL3
	if (operation == REPLAY_LOOP_OPEN_MENU) {
		replay_pause_menu();
		return;
	}
#endif
	if (operation != REPLAY_LOOP_HANDLE_INPUT) {
		return;
	}

#ifdef RESTUNTS_AMIGA
	/* Paused replay controls and their save/load dialogs draw between input
	 * polls without returning to the normal race presentation loop. */
	legacy_u8 saved_refresh = amiga_video_set_auto_refresh(1);
#endif
	replay_handle_input();
#ifdef RESTUNTS_AMIGA
	amiga_video_set_auto_refresh(saved_refresh);
#endif
}
