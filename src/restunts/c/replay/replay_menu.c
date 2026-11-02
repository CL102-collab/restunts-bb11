#include "replay_viewer_private.h"
#ifdef RESTUNTS_SDL3
#include "../extended/extended_file_dialog.h"
#endif

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
	frame_renderer_reset();
#ifdef RESTUNTS_SDL3
	(void)extended_replay_ui_restart();
#endif
	init_game_state_with_frame_rate_byte(configured_frame_rate);
	elapsed_time2 = 0;
	gameconfig.game_recordedframes = 0;
	replay_overflow_acknowledged_word =
		LEGACY_S16_FROM_BITS(LEGACY_U16_REPLACE_LOW_BYTE(replay_overflow_acknowledged_word, 0U));
	replay_recording_flags = REPLAY_RECORDING_ACTIVE_FLAG;
	camera_select_mode(CAMERA_MODE_COCKPIT);
	dashb_toggle = 1;
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
	if (!extended_replay_ui_branch())
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
	current_track = extended_menu_horizon(track_element_map[TRACK_SKYBOX_ELEMENT_INDEX]);
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
	saved_track = extended_menu_horizon(track_element_map[TRACK_SKYBOX_ELEMENT_INDEX]);
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
		if (extended_replay_ui_load_error(saved_recording_flags)) {
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
	frame_renderer_reset();
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
}

static legacy_s8 replay_choose_save_path(void)
{
	legacy_s8 save_status = REPLAY_SAVE_RETRY;
	legacy_s16 chosen;
#ifdef RESTUNTS_SDL3
	if (extended_replay_is_active()) {
		chosen = do_savefile_dialog_limit(replay_directory, replay_filename_input,
										  locate_text_res(mainresptr, replay_save_prompt_id),
										  EXTENDED_FILE_STEM_MAX_LENGTH);
	} else
#endif
	{
		chosen = do_savefile_dialog(replay_directory, replay_filename_input,
									locate_text_res(mainresptr, replay_save_prompt_id));
	}
	if (chosen == 0) {
		save_status = REPLAY_SAVE_CANCELLED;
	} else {
		const legacy_s8 *extension = replay_file_extension;
#ifdef RESTUNTS_SDL3
		extension = extended_replay_ui_extension(extension);
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

void replay_pause_menu(void)
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
