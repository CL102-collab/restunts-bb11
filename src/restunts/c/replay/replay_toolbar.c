#include "replay_viewer_private.h"

legacy_u32 replay_current_frame(void)
{
#ifdef RESTUNTS_SDL3
	return extended_replay_frame();
#else
	return (legacy_u16)state.game_frame;
#endif
}

legacy_u32 replay_input_frame(void)
{
#ifdef RESTUNTS_SDL3
	return extended_replay_input_frame();
#else
	return elapsed_time2;
#endif
}

legacy_u32 replay_recorded_frames(void)
{
#ifdef RESTUNTS_SDL3
	return extended_replay_recorded_frames();
#else
	return gameconfig.game_recordedframes;
#endif
}

static void replay_format_time(legacy_u32 frame, legacy_s16 fraction)
{
#ifdef RESTUNTS_SDL3
	if (extended_replay_ui_format_time(&resID_byte1, frame, fraction)) {
		return;
	}
#endif
	format_frame_as_string(&resID_byte1, LEGACY_S16_FROM_BITS((legacy_u16)frame), fraction);
}

void replay_controls_select(legacy_u8 selection)
{
	for (legacy_u16 index = 0; index < REPLAY_CONTROL_COUNT; index++) {
		replay_control_active[index] = 0;
	}
	replay_control_active[selection] = 1;
}

static void replay_controls_prepare_background(legacy_u16 buffer_index)
{
	if (replay_controls_drawn[buffer_index] == 0) {
		replay_controls_drawn[buffer_index] = 1;
		replay_camera_mode_cache[buffer_index] = REPLAY_NO_SELECTION;
		replay_selection_cache[buffer_index] = REPLAY_NO_SELECTION;
		for (legacy_u16 index = 0; index < REPLAY_CONTROL_COUNT; index++) {
			replay_control_active_cache[buffer_index + index * REPLAY_CONTROL_PLAYER_STRIDE] = 0;
		}
		mouse_draw_opaque_check();
		shape2d_rle_copy_at_position(rplyshapes[REPLAY_SHAPE_BACKGROUND]);
		replay_displayed_time_cache[buffer_index] = -1;
		replay_recorded_position_cache[buffer_index] = -1;
		replay_format_time(replay_recorded_frames() + elapsed_time1, REPLAY_TIME_INCLUDE_FRACTION);
		font_set_colors(dialog_fnt_colour, 0);
		font_set_fontdef2(fontledresptr);
		font_draw_text_opaque(&resID_byte1, REPLAY_TOTAL_TIME_X, REPLAY_TIME_Y);
		font_set_fontdef();
	}
}

static void replay_controls_draw_time(legacy_u16 buffer_index, legacy_u32 current_frame)
{
	legacy_u32 displayed_time = (legacy_u16)(current_frame + elapsed_time1);
#ifdef RESTUNTS_SDL3
	displayed_time = extended_replay_ui_displayed_time(current_frame, displayed_time);
#endif
	if (
#ifdef RESTUNTS_SDL3
		extended_replay_ui_redraw_time() ||
#endif
		(legacy_u16)replay_displayed_time_cache[buffer_index] != displayed_time) {
		replay_displayed_time_cache[buffer_index] = (legacy_s16)displayed_time;
		replay_format_time(displayed_time, REPLAY_TIME_INCLUDE_FRACTION);
		font_set_colors(dialog_fnt_colour, 0);
		mouse_draw_opaque_check();
		font_set_fontdef2(fontledresptr);
		font_draw_text_opaque(&resID_byte1, REPLAY_CURRENT_TIME_X, REPLAY_TIME_Y);
		font_set_fontdef();
	}
}

static void replay_controls_draw_camera(legacy_u16 buffer_index)
{
	if (replay_camera_mode_cache[buffer_index] != (legacy_u8)cameramode) {
		replay_camera_mode_cache[buffer_index] = (legacy_u8)cameramode;
		replay_recorded_position_cache[buffer_index] = -1;
		mouse_draw_opaque_check();
		shape2d_rle_copy_at_position(rplyshapes[REPLAY_SHAPE_CAMERA_FIRST + (legacy_u8)cameramode]);
		if (LEGACY_S8_FROM_BITS(replay_selected_control) >
			LEGACY_S8_FROM_BITS(game_camera_buttons_count[(legacy_u8)cameramode])) {
			replay_selected_control = game_camera_buttons_count[(legacy_u8)cameramode];
		}
		if (replay_selection_cache[buffer_index] > REPLAY_LAST_ACTION_CONTROL) {
			replay_selection_cache[buffer_index] = REPLAY_NO_SELECTION;
		}
	}
}

static void replay_controls_draw_timeline(legacy_u16 buffer_index, legacy_u32 recorded_frame,
										  legacy_u32 current_frame)
{
	legacy_s16 recorded_position, current_position;
#ifdef RESTUNTS_SDL3
	if (!extended_replay_ui_timeline(recorded_frame, current_frame, REPLAY_TIMELINE_POSITION_RANGE,
									 &recorded_position, &current_position))
#endif
	{
		recorded_position = (legacy_s16)replay_timeline_position((legacy_u16)recorded_frame,
																 gameconfig.game_recordedframes,
																 REPLAY_TIMELINE_POSITION_RANGE);
		current_position = (legacy_s16)replay_timeline_position((legacy_u16)current_frame,
																gameconfig.game_recordedframes,
																REPLAY_TIMELINE_POSITION_RANGE);
	}
	if (replay_recorded_position_cache[buffer_index] != recorded_position ||
		replay_current_position_cache[buffer_index] != current_position) {
		mouse_draw_opaque_check();
		replay_recorded_position_cache[buffer_index] = recorded_position;
		replay_current_position_cache[buffer_index] = current_position;
		sprite_fill_rect(REPLAY_TIMELINE_X, REPLAY_TIMELINE_Y, REPLAY_TIMELINE_WIDTH,
						 REPLAY_TIMELINE_HEIGHT, replay_timeline_background_color);
		sprite_fill_rect(LEGACY_S16_WRAP_ADD(REPLAY_TIMELINE_X, recorded_position),
						 REPLAY_TIMELINE_Y, REPLAY_TIMELINE_HEIGHT, REPLAY_TIMELINE_HEIGHT,
						 dialog_fnt_colour);
		sprite_draw_rect_outline(
			LEGACY_S16_WRAP_ADD(REPLAY_TIMELINE_X, current_position), REPLAY_TIMELINE_Y,
			LEGACY_S16_WRAP_ADD(REPLAY_TIMELINE_CURSOR_RIGHT_X, current_position),
			REPLAY_TIMELINE_CURSOR_BOTTOM_Y, replay_marker_color);
	}
}

static legacy_u8 replay_controls_changed(legacy_u16 buffer_index)
{
	legacy_u8 state_changed = replay_selection_cache[buffer_index] != replay_selected_control;
	if (state_changed == 0) {
		for (legacy_u16 index = 0; index < REPLAY_ACTION_CONTROL_COUNT; index++) {
			if (replay_control_active_cache[buffer_index + index * REPLAY_CONTROL_PLAYER_STRIDE] !=
				replay_control_active[index]) {
				state_changed = 1;
				break;
			}
		}
	}
	return state_changed;
}

static void replay_controls_draw_buttons(legacy_u16 buffer_index)
{
	mouse_draw_opaque_check();
	legacy_u8 previous_selection = replay_selection_cache[buffer_index];
	if (previous_selection != REPLAY_NO_SELECTION) {
		if (replay_control_active_cache[buffer_index +
										previous_selection * REPLAY_CONTROL_PLAYER_STRIDE] != 0) {
			shape2d_rle_copy_at_position(
				rplyshapes[REPLAY_SHAPE_CONTROL_ACTIVE_FIRST + previous_selection]);
		} else {
			shape2d_rle_copy_at_position(
				rplyshapes[REPLAY_SHAPE_CONTROL_INACTIVE_FIRST + previous_selection]);
		}
		replay_selection_cache[buffer_index] = REPLAY_NO_SELECTION;
	}
	for (legacy_u16 index = 0; index < REPLAY_ACTION_CONTROL_COUNT; index++) {
		if (replay_control_active[index] == 0 &&
			replay_control_active_cache[buffer_index + index * REPLAY_CONTROL_PLAYER_STRIDE] != 0) {
			shape2d_rle_copy_at_position(rplyshapes[REPLAY_SHAPE_CONTROL_INACTIVE_FIRST + index]);
			replay_control_active_cache[buffer_index + index * REPLAY_CONTROL_PLAYER_STRIDE] = 0;
		}
	}
	for (legacy_u16 index = 0; index < REPLAY_ACTION_CONTROL_COUNT; index++) {
		if (replay_control_active[index] != 0) {
			replay_control_active_cache[buffer_index + index * REPLAY_CONTROL_PLAYER_STRIDE] = 1;
			shape2d_rle_copy_at_position(rplyshapes[REPLAY_SHAPE_CONTROL_ACTIVE_FIRST + index]);
			replay_control_active_cache[buffer_index + index * REPLAY_CONTROL_PLAYER_STRIDE] = 1;
		}
	}
	replay_selection_cache[buffer_index] = replay_selected_control;
	if (replay_selected_control != REPLAY_NO_SELECTION) {
		sprite_draw_rect_outline(game_camera_buttons[replay_selected_control].x1,
								 game_camera_buttons[replay_selected_control].y1,
								 game_camera_buttons[replay_selected_control].x2,
								 game_camera_buttons[replay_selected_control].y2,
								 replay_marker_color);
	}
}

void replay_controls_draw(legacy_u32 recorded_frame, legacy_u32 current_frame)
{
	legacy_u16 buffer_index = (legacy_u8)dashboard_buffer_index;
	replay_controls_prepare_background(buffer_index);
	replay_controls_draw_time(buffer_index, current_frame);
	replay_controls_draw_camera(buffer_index);
	replay_controls_draw_timeline(buffer_index, recorded_frame, current_frame);
	if (replay_controls_changed(buffer_index) != 0) {
		replay_controls_draw_buttons(buffer_index);
	}
	mouse_draw_transparent_check();
}

void replay_draw_waiting(void)
{
	copy_string(&resID_byte1, locate_text_res(gameresptr, "wai"));
	struct RECTANGLE *text_rectangle =
		intro_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1), REPLAY_WAITING_TEXT_Y,
						dialog_fnt_colour, 0);
	if (slow_video_mgmt_copy != 0) {
		rect_union(alternate_frame_rects, text_rectangle, alternate_frame_rects);
	}
}
