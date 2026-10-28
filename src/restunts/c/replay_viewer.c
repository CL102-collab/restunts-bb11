#include "fileio.h"
#include "game_input.h"
#include "math.h"
#include "memmgr.h"
#include "menu_internal.h"
#include "race_resources.h"
#include "replay.h"
#include "replay_viewer.h"
#include "ghost.h"
#include "replay_viewer_internal.h"
#include "resource.h"
#include "shape2d.h"
#include "timing.h"
#include "ui_dialog.h"
#include "ui_input.h"
#include "ui_text.h"
#include "crash_state.h"
#include "track_objects.h"
#include "camera.h"
#include "video_frame.h"
#include "race_graphics.h"
#include "car_audio.h"
#include "externs.h"
#include "keyboard.h"
#include "frame_internal.h"
#ifdef RESTUNTS_AMIGA
#include "../platform/amiga/amiga.h"
#endif
#ifdef RESTUNTS_SDL3
#include "extended_replay.h"
#include "extended_replay_runtime.h"
#include "extended_track.h"
#include "extended_file.h"
#include "../platform/sdl3/touch_game.h"
#include "../platform/sdl3/controller_game.h"
#endif

#define REPLAY_PLAYER_COUNT 2U
#define REPLAY_CONTROL_COUNT 9U
#define REPLAY_ACTION_CONTROL_COUNT 7U
#define REPLAY_LAST_ACTION_CONTROL (REPLAY_ACTION_CONTROL_COUNT - 1U)
#define REPLAY_CONTROL_PLAYER_STRIDE REPLAY_PLAYER_COUNT
#define REPLAY_NO_SELECTION 255U

#define REPLAY_SHAPE_BACKGROUND 0U
#define REPLAY_SHAPE_CAMERA_FIRST 1U
#define REPLAY_SHAPE_CONTROL_INACTIVE_FIRST 5U
#define REPLAY_SHAPE_CONTROL_ACTIVE_FIRST 14U

#define REPLAY_TOTAL_TIME_X 216
#define REPLAY_CURRENT_TIME_X 152
#define REPLAY_TIME_Y 187
#define REPLAY_TIMELINE_X 154
#define REPLAY_TIMELINE_Y 177
#define REPLAY_TIMELINE_WIDTH 116
#define REPLAY_TIMELINE_POSITION_RANGE 110U
#define REPLAY_TIMELINE_HEIGHT 6
#define REPLAY_TIMELINE_CURSOR_EDGE_OFFSET 5
#define REPLAY_TIMELINE_CURSOR_RIGHT_X (REPLAY_TIMELINE_X + REPLAY_TIMELINE_CURSOR_EDGE_OFFSET)
#define REPLAY_TIMELINE_CURSOR_BOTTOM_Y (REPLAY_TIMELINE_Y + REPLAY_TIMELINE_CURSOR_EDGE_OFFSET)
#define REPLAY_WAITING_TEXT_Y 100
#define REPLAY_TIME_INCLUDE_FRACTION 1

#define REPLAY_PAUSE_OPTION_COUNT 8U
#define REPLAY_MODE_OPTION_COUNT 5U
#define CAR_ID_LENGTH 4U
#define TRACK_SKYBOX_ELEMENT_INDEX 900U
#define REPLAY_LOAD_WAIT_VALUE 150

#define REPLAY_SCRUB_ACCELERATION_DIVISOR 50L
#define REPLAY_SCRUB_INITIAL_SPEED 3
#define REPLAY_SCRUB_FIXED_SCALE 20L
#define REPLAY_SCRUB_MAX_SPEED 100
#define REPLAY_INPUT_SETTLE_DELTA 1000
#define REPLAY_RESTART_WAIT_TICKS 50UL

#define REPLAY_CUSTOM_CAMERA_MIN_DISTANCE 120
#define REPLAY_CUSTOM_CAMERA_MAX_DISTANCE 1500
#define REPLAY_TRACK_CAMERA_MAX_HEIGHT 900
#define REPLAY_CUSTOM_CAMERA_ELEVATION_LIMIT CUSTOM_CAMERA_ELEVATION_LIMIT
#define REPLAY_CUSTOM_CAMERA_MODIFIER_SCAN_CODE 29

#define REPLAY_DIRECTION_ANGLE_SHIFT 8U
#define REPLAY_DIRECTION_MASK 3U
#define REPLAY_DIALOG_INITIAL_CHOICE 0
#define REPLAY_DIALOG_ACCEPTED_MINIMUM 1
#define REPLAY_DIALOG_CONFIRMED_CHOICE 1
#define REPLAY_RESTART_DIALOG_CAPACITY 512U

#define REPLAY_FILE_CHECK_ARGUMENT 2

#define REPLAY_BAR_HIDDEN_STATE (-1)
#define REPLAY_FIRST_FRAME 0
#define REPLAY_SINGLE_FRAME_DELTA 1

enum REPLAY_DIRECTION {
	REPLAY_DIRECTION_UP = 0,
	REPLAY_DIRECTION_RIGHT = 1,
	REPLAY_DIRECTION_DOWN = 2,
	REPLAY_DIRECTION_LEFT = 3
};

enum REPLAY_PAUSE_ACTION {
	REPLAY_PAUSE_ACTION_FINISH = 1,
	REPLAY_PAUSE_ACTION_RESTART = 2,
	REPLAY_PAUSE_ACTION_CONTINUE = 3,
	REPLAY_PAUSE_ACTION_LOAD = 4,
	REPLAY_PAUSE_ACTION_SAVE = 5,
	REPLAY_PAUSE_ACTION_DISPLAY_OPTIONS = 6,
	REPLAY_PAUSE_ACTION_EXIT = 7
};

enum REPLAY_MODE_ACTION {
	REPLAY_MODE_ACTION_DASHBOARD = 0,
	REPLAY_MODE_ACTION_BAR = 1,
	REPLAY_MODE_ACTION_CAMERA = 2,
	REPLAY_MODE_ACTION_DETAIL = 3,
	REPLAY_MODE_ACTION_FOLLOW_OPPONENT = 4
};

enum REPLAY_SAVE_STATUS {
	REPLAY_SAVE_CANCELLED = -1,
	REPLAY_SAVE_RETRY = 0,
	REPLAY_SAVE_READY = 1
};

static legacy_u32 replay_current_frame(void)
{
#ifdef RESTUNTS_SDL3
	return extended_replay_frame();
#else
	return (legacy_u16)state.game_frame;
#endif
}

static legacy_u32 replay_input_frame(void)
{
#ifdef RESTUNTS_SDL3
	return extended_replay_input_frame();
#else
	return elapsed_time2;
#endif
}

static legacy_u32 replay_recorded_frames(void)
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
	if (extended_replay_is_active()) {
		extended_replay_format_runtime_time(&resID_byte1, frame, fraction);
		return;
	}
#endif
	format_frame_as_string(&resID_byte1, LEGACY_S16_FROM_BITS((legacy_u16)frame), fraction);
}

static void replay_controls_select(legacy_u8 selection)
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
	if (extended_replay_is_active()) {
		displayed_time = current_frame;
	}
#endif
	if (
#ifdef RESTUNTS_SDL3
		extended_replay_is_active() ||
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
	if (extended_replay_is_active()) {
		recorded_position = (legacy_s16)extended_replay_timeline_position(
			recorded_frame, replay_recorded_frames(), REPLAY_TIMELINE_POSITION_RANGE);
		current_position = (legacy_s16)extended_replay_timeline_position(
			current_frame, replay_recorded_frames(), REPLAY_TIMELINE_POSITION_RANGE);
	} else
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

static void replay_controls_draw(legacy_u32 recorded_frame, legacy_u32 current_frame)
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

static void replay_draw_waiting(void)
{
	copy_string(&resID_byte1, locate_text_res(gameresptr, "wai"));
	struct RECTANGLE *text_rectangle =
		intro_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1), REPLAY_WAITING_TEXT_Y,
						dialog_fnt_colour, 0);
	if (slow_video_mgmt_copy != 0) {
		rect_union(alternate_frame_rects, text_rectangle, alternate_frame_rects);
	}
}

static legacy_s8 replay_choose_pause_action(void)
{
	is_in_replay = 1;
	audio_carstate();
	replay_controls_select(REPLAY_CONTROL_PAUSE);
	replay_controls_draw(replay_current_frame(), replay_current_frame());
	legacy_s16 options[REPLAY_PAUSE_OPTION_COUNT];
	for (legacy_u16 index = 0; index < REPLAY_PAUSE_OPTION_COUNT; index++) {
		options[index] = 0;
	}
	if (state.playerstate.car_crashBmpFlag != CRASH_EVENT_NONE) {
		options[REPLAY_PAUSE_ACTION_CONTINUE] = 1;
	}
	if (replay_recorded_frames() == 0 || elapsed_time1 != 0) {
		options[REPLAY_PAUSE_ACTION_SAVE] = 1;
	}
	if (passed_security == 0) {
		options[REPLAY_PAUSE_ACTION_RESTART] = 1;
		options[REPLAY_PAUSE_ACTION_CONTINUE] = 1;
	}
	if (((legacy_u8)replay_recording_flags & REPLAY_RECORDING_RESTARTABLE_FLAG) == 0) {
		options[REPLAY_PAUSE_ACTION_FINISH] = 1;
	}
	full_redraw_frames_remaining = (legacy_u8)video_page_count;
	return LEGACY_S8_FROM_BITS(show_dialog(
		DIALOG_TYPE_MENU, DIALOG_NO_BACKGROUND_SAVE,
		locate_text_res(gameresptr, replay_pause_menu_id), DIALOG_AUTO_POSITION,
		DIALOG_AUTO_POSITION, dialog_border_color, options, REPLAY_DIALOG_INITIAL_CHOICE));
}

static legacy_s16 replay_confirm_restart(void)
{
	static const legacy_s8 prompt[] = "Re-start driving?]}";
	legacy_s8 question[REPLAY_RESTART_DIALOG_CAPACITY];
	legacy_s8 far *choices = locate_text_res(mainresptr, exit_to_dos_dialog_id);
	while (*choices != 0 && *choices != '[') {
		choices++;
	}
	if (*choices == 0) {
		return 0;
	}

	legacy_u16 length = 0;
	for (; length < sizeof(prompt) - 1U; length++) {
		question[length] = prompt[length];
	}
	while (*choices != 0 && length + 1U < sizeof(question)) {
		question[length++] = *choices++;
	}
	if (*choices != 0) {
		return 0;
	}
	question[length] = 0;
	return show_dialog(DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, question, DIALOG_AUTO_POSITION,
					   DIALOG_AUTO_POSITION, dialog_border_color, 0,
					   REPLAY_DIALOG_INITIAL_CHOICE) == REPLAY_DIALOG_CONFIRMED_CHOICE;
}

static void replay_restart_recording(void)
{
	check_input();
	frame_supersight_reset();
#ifdef RESTUNTS_SDL3
	if (extended_replay_is_active()) {
		extended_replay_runtime_begin();
	}
#endif
	init_game_state_with_frame_rate_byte(configured_frame_rate);
	elapsed_time2 = 0;
	gameconfig.game_recordedframes = 0;
	replay_overflow_acknowledged_word =
		LEGACY_S16_FROM_BITS(LEGACY_U16_REPLACE_LOW_BYTE(replay_overflow_acknowledged_word, 0U));
	replay_recording_flags = REPLAY_RECORDING_ACTIVE_FLAG;
}

static legacy_s16 replay_continue_recording(void)
{
	if (((legacy_u8)replay_recording_flags & REPLAY_RECORDING_MODIFIED_FLAG) != 0) {
		replay_recording_flags = REPLAY_RECORDING_ACTIVE_FLAG | REPLAY_RECORDING_MODIFIED_FLAG;
	} else if (replay_recorded_frames() != replay_input_frame()) {
		legacy_s16 dialog_result = LEGACY_S16_FROM_BITS(show_dialog(
			DIALOG_TYPE_MENU, DIALOG_NO_BACKGROUND_SAVE,
			locate_text_res(gameresptr, replay_continue_dialog_id), DIALOG_AUTO_POSITION,
			DIALOG_AUTO_POSITION, performGraphColor, 0, REPLAY_DIALOG_INITIAL_CHOICE));
		if (dialog_result < REPLAY_DIALOG_ACCEPTED_MINIMUM) {
			return 0;
		}
		replay_recording_flags = REPLAY_RECORDING_ACTIVE_FLAG | REPLAY_RECORDING_MODIFIED_FLAG;
	} else {
		replay_recording_flags = REPLAY_RECORDING_ACTIVE_FLAG;
	}
#ifdef RESTUNTS_SDL3
	if (extended_replay_is_active()) {
		(void)extended_replay_branch(extended_replay_frame());
	} else
#endif
	{
		elapsed_time2 = (legacy_u16)state.game_frame;
		gameconfig.game_recordedframes = (legacy_u16)state.game_frame;
	}
	return 1;
}

static void replay_resume_live(void)
{
	replay_filename[0] = 0;
	camera_select_mode(CAMERA_MODE_COCKPIT);
	dashb_toggle = 1;
	show_penalty_counter = 0;
	followOpponentFlag = 0;
	game_replay_mode = REPLAY_MODE_LIVE;
#ifdef RESTUNTS_SDL3
	sdl3_input_set_replay_active(0);
#endif
	state.game_end_event = 0;
	state.game_frame_in_sec = 0;
	replay_playback_speed = REPLAY_PLAYBACK_NORMAL;
	replay_controls_select(REPLAY_CONTROL_PLAY);
	is_in_replay = 0;
	mouse_minmax_position(LEGACY_S8_FROM_BITS(mouse_driving_enabled));
	check_input();
	kbormouse = 0;
}

static void replay_reload_changed_resources(const struct GAMEINFO *saved_config,
											legacy_u8 saved_track)
{
	legacy_u8 current_track;
#ifdef RESTUNTS_SDL3
	current_track = extended_track_active() ? 0U : track_element_map[TRACK_SKYBOX_ELEMENT_INDEX];
#else
	current_track = track_element_map[TRACK_SKYBOX_ELEMENT_INDEX];
#endif
	legacy_s16 resources_changed = current_track != saved_track;
	for (legacy_u16 index = 0; index < CAR_ID_LENGTH; index++) {
		if (saved_config->game_playercarid[index] != gameconfig.game_playercarid[index]) {
			resources_changed = 1;
		}
	}
	if (saved_config->game_opponenttype != gameconfig.game_opponenttype) {
		resources_changed = 1;
	} else if (gameconfig.game_opponenttype != 0) {
		legacy_s16 opponent_changed = 0;
		for (legacy_u16 index = 0; index < CAR_ID_LENGTH; index++) {
			if (saved_config->game_opponentcarid[index] != gameconfig.game_opponentcarid[index]) {
				resources_changed = 1;
				opponent_changed = 1;
			}
		}
		if (opponent_changed == 0) {
			ensure_file_exists(REPLAY_FILE_CHECK_ARGUMENT);
			load_opponent_data();
		}
	}
	if (resources_changed != 0) {
		free_player_cars();
		setup_player_cars();
	}
}

static void replay_load_recording(void)
{
#ifdef RESTUNTS_SDL3
	legacy_s8 saved_recording_flags = replay_recording_flags;
#endif
	replay_recording_flags = 0;
	audio_carstate();
	if (
#ifdef RESTUNTS_SDL3
		do_format_fileselect_dialog(replay_directory, replay_filename_input, ".rpl",
#else
		do_fileselect_dialog(replay_directory, replay_filename_input, ".rpl",
#endif
									locate_text_res(mainresptr, "rep")) == 0) {
		return;
	}
	waitflag = REPLAY_LOAD_WAIT_VALUE;
	show_waiting();
	struct GAMEINFO saved_config = gameconfig;
	legacy_u8 saved_track;
#ifdef RESTUNTS_SDL3
	saved_track = extended_track_active() ? 0U : track_element_map[TRACK_SKYBOX_ELEMENT_INDEX];
#else
	saved_track = track_element_map[TRACK_SKYBOX_ELEMENT_INDEX];
#endif
	if ((legacy_u8)
#ifdef RESTUNTS_SDL3
			file_load_replay_format(replay_directory, replay_filename_input,
									file_dialog_selected_extension())
#else
			file_load_replay(replay_directory, replay_filename_input)
#endif
		!= 0) {
#ifdef RESTUNTS_SDL3
		if (strcmp(file_dialog_selected_extension(), ".csrpl") == 0) {
			replay_recording_flags = saved_recording_flags;
			show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
						(void *)extended_file_error(), DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION,
						dialog_border_color, 0, 0);
			return;
		}
#endif
		gameconfig.game_recordedframes = 0;
	}
	dashb_toggle = 0;
	track_setup();
	ghost_end_race();
	ghost_check_track();
	replay_reload_changed_resources(&saved_config, saved_track);
	framespersec =
		(legacy_s16)LEGACY_S8_FROM_BITS(LEGACY_U16_LOW_BYTE(gameconfig.game_framespersec));
	frame_supersight_reset();
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
}

static legacy_s8 replay_choose_save_path(void)
{
	legacy_s8 save_status = REPLAY_SAVE_RETRY;
	if (do_savefile_dialog(replay_directory, replay_filename_input,
						   locate_text_res(mainresptr, replay_save_prompt_id)) == 0) {
		save_status = REPLAY_SAVE_CANCELLED;
	} else {
		const legacy_s8 *extension = replay_file_extension;
#ifdef RESTUNTS_SDL3
		if (extended_replay_is_active()) {
			extension = ".csrpl";
		}
#endif
		file_build_path(replay_directory, replay_filename_input, extension, g_path_buf);
		save_status = REPLAY_SAVE_READY;
		g_is_busy = 1;
		if (file_find(g_path_buf) != 0) {
			legacy_s16 dialog_result = LEGACY_S16_FROM_BITS(show_dialog(
				DIALOG_TYPE_MENU, DIALOG_NO_BACKGROUND_SAVE,
				locate_text_res(mainresptr, replay_overwrite_dialog_id), DIALOG_AUTO_POSITION,
				DIALOG_AUTO_POSITION, performGraphColor, 0, REPLAY_DIALOG_INITIAL_CHOICE));
			if (dialog_result == REPLAY_SAVE_CANCELLED) {
				save_status = REPLAY_SAVE_CANCELLED;
			} else if (dialog_result == REPLAY_SAVE_RETRY) {
				save_status = REPLAY_SAVE_RETRY;
			}
		}
		g_is_busy = 0;
	}
	return save_status;
}

static void replay_save_recording(void)
{
	audio_carstate();
	for (;;) {
		legacy_s8 save_status = replay_choose_save_path();
		if (save_status != REPLAY_SAVE_READY) {
			break;
		}
		if ((legacy_u8)file_write_replay(g_path_buf) == 0) {
			break;
		}
		show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_NO_BACKGROUND_SAVE,
					locate_text_res(mainresptr, replay_save_error_message_id), DIALOG_AUTO_POSITION,
					DIALOG_AUTO_POSITION, performGraphColor, 0, REPLAY_DIALOG_INITIAL_CHOICE);
	}
}

static void replay_display_options(void)
{
	legacy_s16 mode_options[REPLAY_MODE_OPTION_COUNT];
	for (legacy_u16 index = 0; index < REPLAY_MODE_OPTION_COUNT; index++) {
		mode_options[index] = 0;
	}
	if (gameconfig.game_opponenttype == 0 && ghost_car_state() == 0) {
		mode_options[REPLAY_MODE_ACTION_FOLLOW_OPPONENT] = 1;
	}
	legacy_s8 menu_result = LEGACY_S8_FROM_BITS(show_dialog(
		DIALOG_TYPE_MENU, DIALOG_NO_BACKGROUND_SAVE,
		locate_text_res(gameresptr, replay_mode_options_dialog_id), DIALOG_AUTO_POSITION,
		DIALOG_AUTO_POSITION, dialog_border_color, mode_options, REPLAY_DIALOG_INITIAL_CHOICE));
	switch (menu_result) {
		case REPLAY_MODE_ACTION_DASHBOARD:
			dashb_toggle ^= 1;
			break;
		case REPLAY_MODE_ACTION_BAR:
			replaybar_toggle ^= 1;
			break;
		case REPLAY_MODE_ACTION_CAMERA:
			camera_select_mode((legacy_s8)(((legacy_u8)cameramode + 1U) & CAMERA_MODE_MASK));
			break;
		case REPLAY_MODE_ACTION_DETAIL:
			show_graphic_levels_menu();
			break;
		case REPLAY_MODE_ACTION_FOLLOW_OPPONENT:
			followOpponentFlag ^= 1;
			break;
	}
}

static void replay_pause_menu(void)
{
#ifdef RESTUNTS_SDL3
	sdl3_touch_set_replay_active(0);
#endif
	switch (replay_choose_pause_action()) {
		case REPLAY_PAUSE_ACTION_FINISH:
			update_crash_state(CRASH_EVENT_EXIT, PLAYER_CAR_INDEX);
			race_exit_request = REPLAY_EXIT_REQUESTED;
			break;
		case REPLAY_PAUSE_ACTION_RESTART:
			if (replay_confirm_restart() != 0) {
				replay_restart_recording();
				replay_resume_live();
			}
			break;
		case REPLAY_PAUSE_ACTION_CONTINUE:
			if (replay_continue_recording() != 0) {
				replay_resume_live();
			}
			break;
		case REPLAY_PAUSE_ACTION_LOAD:
			replay_load_recording();
			break;
		case REPLAY_PAUSE_ACTION_SAVE:
			replay_save_recording();
			break;
		case REPLAY_PAUSE_ACTION_DISPLAY_OPTIONS:
			replay_display_options();
			break;
		case REPLAY_PAUSE_ACTION_EXIT:
			update_crash_state(CRASH_EVENT_EXIT, PLAYER_CAR_INDEX);
			replay_recording_flags = 0;
			race_exit_request = REPLAY_EXIT_REQUESTED;
			break;
	}
	check_input();
#ifdef RESTUNTS_SDL3
	sdl3_touch_set_replay_active(1);
#endif
}

static legacy_s32 replay_scrub_accumulate(legacy_s32 accumulated, legacy_s16 speed,
										  legacy_u16 delta)
{
	legacy_s16 increment = LEGACY_S16_WRAP_MUL(LEGACY_S16_FROM_BITS(delta), speed);
	return LEGACY_S32_WRAP_ADD_S16(accumulated, increment);
}

static legacy_s16 replay_scrub_speed(legacy_s32 accumulated)
{
	legacy_s32 quotient = LEGACY_S32_DIV_OR_ZERO(accumulated, REPLAY_SCRUB_ACCELERATION_DIVISOR);
	return LEGACY_S16_WRAP_ADD(LEGACY_S16_FROM_BITS((legacy_u16)quotient),
							   REPLAY_SCRUB_INITIAL_SPEED);
}

static legacy_u16 replay_scrub_amount(legacy_s32 accumulated)
{
	return (legacy_u16)LEGACY_S32_DIV_OR_ZERO(accumulated, REPLAY_SCRUB_FIXED_SCALE);
}

static legacy_s32 replay_scrub_begin(legacy_u8 selection)
{
	is_in_replay = 1;
	audio_carstate();
	replay_controls_select(selection);
	(void)timer_get_delta_alt();
	return REPLAY_SCRUB_FIXED_SCALE;
}

static legacy_s32 replay_scrub_advance(legacy_s32 accumulated, legacy_u16 *delta)
{
	legacy_s16 speed = replay_scrub_speed(accumulated);
	if (speed > REPLAY_SCRUB_MAX_SPEED) {
		speed = REPLAY_SCRUB_MAX_SPEED;
	}
	*delta = (legacy_u16)timer_get_delta_alt();
	return replay_scrub_accumulate(accumulated, speed, *delta);
}

#ifdef RESTUNTS_SDL3
static void replay_extended_scrub(int direction)
{
	(void)replay_scrub_begin(direction > 0 ? REPLAY_CONTROL_FAST_FORWARD : REPLAY_CONTROL_REWIND);
	uint32_t origin = extended_replay_input_frame();
	uint32_t limit = direction > 0 ? extended_replay_recorded_frames() - origin : origin;
	uint64_t accumulated = 0U, held = 0U;
	uint32_t target = origin;
	int menu_requested = 0;
	while (((legacy_u8)input_combined_flags & INPUT_ACTION_BUTTON_MASK) != 0U) {
		if (sdl3_controller_take_menu_request() != 0) {
			menu_requested = 1;
			break;
		}
		uint32_t delta = timer_get_delta_alt();
		held += delta;
		uint64_t speed = held > 1000U ? 6U : 3U;
		accumulated += (uint64_t)delta * speed;
		if (accumulated > (uint64_t)limit * REPLAY_SCRUB_FIXED_SCALE) {
			accumulated = (uint64_t)limit * REPLAY_SCRUB_FIXED_SCALE;
		}
		uint32_t amount = (uint32_t)(accumulated / REPLAY_SCRUB_FIXED_SCALE);
		target = direction > 0 ? origin + amount : origin - amount;
		replay_controls_draw(extended_replay_frame(), target);
		input_do_checking((legacy_s16)(delta > LEGACY_S16_MAX ? LEGACY_S16_MAX : delta));
	}
	frame_supersight_reset();
	replay_controls_select(REPLAY_CONTROL_PAUSE);
	replay_draw_waiting();
	(void)extended_replay_seek(target);
	replay_controls_draw(extended_replay_frame(), target);
	input_do_checking(REPLAY_INPUT_SETTLE_DELTA);
	if (menu_requested) {
		replay_pause_menu();
	}
}
#endif

static void replay_fast_forward(void)
{
#ifdef RESTUNTS_SDL3
	if (extended_replay_is_active()) {
		replay_extended_scrub(1);
		return;
	}
#endif
	legacy_s32 accumulated = replay_scrub_begin(REPLAY_CONTROL_FAST_FORWARD);
	legacy_u16 amount;
	legacy_u16 delta;
	legacy_u16 remaining;
#ifdef RESTUNTS_SDL3
	legacy_u8 menu_requested = 0;
#endif
	while (((legacy_u8)input_combined_flags & INPUT_ACTION_BUTTON_MASK) != 0) {
#ifdef RESTUNTS_SDL3
		if (sdl3_controller_take_menu_request() != 0) {
			menu_requested = 1;
			break;
		}
#endif
		accumulated = replay_scrub_advance(accumulated, &delta);
		remaining = LEGACY_U16_WRAP_SUB(gameconfig.game_recordedframes, elapsed_time2);
		amount = replay_scrub_amount(accumulated);
		if (amount > remaining) {
			accumulated = LEGACY_S32_WRAP_MUL((legacy_s32)remaining, REPLAY_SCRUB_FIXED_SCALE);
		}
		amount = replay_scrub_amount(accumulated);
		replay_controls_draw(state.game_frame, LEGACY_U16_WRAP_ADD(elapsed_time2, amount));
		input_do_checking(LEGACY_S16_FROM_BITS(delta));
	}

	remaining = LEGACY_U16_WRAP_SUB(gameconfig.game_recordedframes, elapsed_time2);
	amount = replay_scrub_amount(accumulated);
	if (amount > remaining) {
		accumulated = LEGACY_S32_WRAP_MUL((legacy_s32)remaining, REPLAY_SCRUB_FIXED_SCALE);
		amount = remaining;
	}
	legacy_u16 target = LEGACY_U16_WRAP_ADD(elapsed_time2, amount);
	if (LEGACY_S16_FROM_BITS(target) > LEGACY_S16_FROM_BITS(gameconfig.game_recordedframes)) {
		target = gameconfig.game_recordedframes;
	}
	frame_supersight_reset();
	restore_gamestate(target);
	elapsed_time2 = target;
	replay_controls_select(REPLAY_CONTROL_PAUSE);
	replay_draw_waiting();
	while ((legacy_u16)state.game_frame != elapsed_time2) {
		update_gamestate();
		replay_controls_draw(state.game_frame, elapsed_time2);
	}
	input_do_checking(REPLAY_INPUT_SETTLE_DELTA);
#ifdef RESTUNTS_SDL3
	if (menu_requested != 0) {
		replay_pause_menu();
	}
#endif
}

static void replay_rewind(void)
{
#ifdef RESTUNTS_SDL3
	if (extended_replay_is_active()) {
		replay_extended_scrub(-1);
		return;
	}
#endif
	legacy_s32 accumulated = replay_scrub_begin(REPLAY_CONTROL_REWIND);
	legacy_u16 amount;
	legacy_u16 delta;
#ifdef RESTUNTS_SDL3
	legacy_u8 menu_requested = 0;
#endif
	while (((legacy_u8)input_combined_flags & INPUT_ACTION_BUTTON_MASK) != 0) {
#ifdef RESTUNTS_SDL3
		if (sdl3_controller_take_menu_request() != 0) {
			menu_requested = 1;
			break;
		}
#endif
		accumulated = replay_scrub_advance(accumulated, &delta);
		amount = replay_scrub_amount(accumulated);
		if (amount > elapsed_time2) {
			accumulated = LEGACY_S32_WRAP_MUL((legacy_s32)elapsed_time2, REPLAY_SCRUB_FIXED_SCALE);
		}
		amount = replay_scrub_amount(accumulated);
		replay_controls_draw(state.game_frame, LEGACY_U16_WRAP_SUB(elapsed_time2, amount));
		input_do_checking(LEGACY_S16_FROM_BITS(delta));
	}

	amount = replay_scrub_amount(accumulated);
	if (amount > elapsed_time2) {
		amount = elapsed_time2;
	}
	replay_controls_select(REPLAY_CONTROL_PAUSE);
	if (amount != 0) {
		replay_draw_waiting();
		legacy_u16 target = LEGACY_U16_WRAP_SUB(elapsed_time2, amount);
		frame_supersight_reset();
		restore_gamestate(target);
		elapsed_time2 = target;
		legacy_s16 frames_to_catch_up =
			LEGACY_S16_WRAP_SUB(LEGACY_S16_FROM_BITS(target), state.game_frame);
		legacy_s16 frames_remaining = frames_to_catch_up;
		while ((legacy_u16)state.game_frame != elapsed_time2) {
			update_gamestate();
			frames_remaining = LEGACY_S16_WRAP_SUB(frames_remaining, REPLAY_SINGLE_FRAME_DELTA);
			legacy_u16 displayed_frame = LEGACY_U16_WRAP_ADD(
				elapsed_time2, replay_rewind_interpolate(amount, (legacy_u16)frames_remaining,
														 (legacy_u16)frames_to_catch_up));
			replay_controls_draw(displayed_frame, elapsed_time2);
			input_do_checking(REPLAY_SINGLE_FRAME_DELTA);
		}
	}
	replay_controls_draw(replay_current_frame(), replay_current_frame());
	input_do_checking(REPLAY_INPUT_SETTLE_DELTA);
#ifdef RESTUNTS_SDL3
	if (menu_requested != 0) {
		replay_pause_menu();
	}
#endif
}

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
			frame_supersight_reset();
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
#define REPLAY_TOUCH_SKIP_SECONDS 10L
#define REPLAY_TOUCH_LEFT_BUTTON 1U
#define REPLAY_TOUCH_NO_SEEK (-1L)

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
	frame_supersight_reset();
	if (extended_replay_is_active()) {
		(void)extended_replay_seek((uint32_t)target);
		replay_controls_draw(extended_replay_frame(), (uint32_t)target);
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
		if (extended_replay_is_active()) {
			replay_controls_draw(extended_replay_frame(), extended_replay_frame());
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
