/* Regression fingerprints captured from the original routines before extraction. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifndef VIEWER_SOURCE
#define VIEWER_SOURCE "../c/replay_viewer.c"
#endif
#include VIEWER_SOURCE
#undef memcpy
#undef printf

static struct CARSTATE ghost_fixture;
static legacy_s16 ghost_fixture_active;
static legacy_u32 ghost_end_calls;
static legacy_s16 opponent_view_disabled;
static legacy_u8 supersight_reset_pending;
static legacy_u32 input_polls, input_exit_poll, fps_expire_poll, fps_expiry_checks;
#ifdef RESTUNTS_SDL3
#define TEST_SEEK_CHECKPOINT_INTERVAL 60U
#define TEST_SEEK_RECORDING_FRAMES 1000U
#define TEST_SEEK_START_FRAME 501U
static legacy_u8 controller_menu_request, controller_scrub_testing;
static legacy_u16 controller_scrub_polls;
legacy_s16 input_combined_flags;
static struct RECTANGLE controller_scrub_text_bounds;

void copy_string(legacy_s8 *destination, legacy_s8 far *source)
{
	while ((*destination++ = *source++) != 0) {
	}
}

legacy_s16 font_centered_text_x(const legacy_s8 *text)
{
	(void)text;
	return 0;
}

struct RECTANGLE *intro_draw_text(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 color,
								  legacy_s16 shadow_color)
{
	(void)text;
	(void)x;
	(void)y;
	(void)color;
	(void)shadow_color;
	assert(controller_scrub_testing != 0);
	return &controller_scrub_text_bounds;
}

void rect_union(struct RECTANGLE *first, struct RECTANGLE *second, struct RECTANGLE *result)
{
	(void)first;
	(void)second;
	(void)result;
	assert(controller_scrub_testing != 0);
}

legacy_u8 sdl3_controller_take_menu_request(void)
{
	legacy_u8 request = controller_menu_request;
	controller_menu_request = 0;
	return request;
}

legacy_s16 input_update_controller_camera(void)
{
	return 0;
}

void sdl3_input_set_replay_active(legacy_u8 active)
{
	(void)active;
}

static legacy_u8 touch_seek_testing;
static legacy_s16 touch_seek_direction;
static legacy_u16 touch_seek_target;
static legacy_u32 touch_seek_restores, touch_seek_updates;

void sdl3_touch_set_replay_active(legacy_u8 active)
{
	(void)active;
}

legacy_s16 sdl3_touch_take_seek(void)
{
	legacy_s16 direction = touch_seek_direction;
	touch_seek_direction = 0;
	return direction;
}
#endif

legacy_s16 frame_fps_expire_idle(void)
{
	fps_expiry_checks++;
	if (fps_expire_poll != 0 && input_polls >= fps_expire_poll) {
		fps_expire_poll = 0;
		return 1;
	}
	return 0;
}

legacy_s16 input_checking(legacy_s16 delta)
{
	assert(delta == 1);
	assert(++input_polls <= input_exit_poll);
	return input_polls == input_exit_poll ? KEY_F2 : 0;
}

void camera_select_mode(legacy_s8 mode)
{
	cameramode = mode;
}

legacy_s16 handle_ingame_kb_shortcuts(legacy_s16 key)
{
	assert(key == KEY_F2);
	return 1;
}

legacy_s16 mouse_multi_hittest(legacy_s16 count, const struct BUTTON_AREA *buttons)
{
	(void)count;
	(void)buttons;
	return REPLAY_NO_SELECTION;
}

legacy_s16 kb_get_key_state(legacy_s16 scancode)
{
	assert(scancode == REPLAY_CUSTOM_CAMERA_MODIFIER_SCAN_CODE);
	return 0;
}

legacy_u32 timer_get_delta_alt(void)
{
	return 1;
}

void sprite_select_screen(void)
{
}

legacy_s16 input_do_checking(legacy_s16 delta)
{
#ifdef RESTUNTS_SDL3
	if (controller_scrub_testing != 0) {
		assert((input_combined_flags & INPUT_ACTION_BUTTON_MASK) != 0);
		controller_scrub_polls++;
		if (controller_scrub_polls == 1) {
			assert(delta != REPLAY_INPUT_SETTLE_DELTA);
			controller_menu_request = 1;
		}
		return 0;
	}
#endif
	(void)delta;
	assert(0 && "Unexpected replay seek");
	return 0;
}

void restore_gamestate(legacy_u16 target)
{
#ifdef RESTUNTS_SDL3
	assert(touch_seek_testing && supersight_reset_pending);
	assert(is_in_replay != 0 && game_replay_mode == REPLAY_MODE_PLAYBACK);
	supersight_reset_pending = 0;
	touch_seek_target = target;
	touch_seek_restores++;
	state.game_frame = target / TEST_SEEK_CHECKPOINT_INTERVAL * TEST_SEEK_CHECKPOINT_INTERVAL;
	elapsed_time2 = (legacy_u16)state.game_frame;
#else
	(void)target;
	assert(0 && "Unexpected replay restore");
#endif
}

void update_gamestate(void)
{
#ifdef RESTUNTS_SDL3
	assert(touch_seek_testing && is_in_replay != 0);
	assert(elapsed_time2 == touch_seek_target && (legacy_u16)state.game_frame < touch_seek_target);
	state.game_frame++;
	touch_seek_updates++;
#else
	assert(0 && "Unexpected replay update");
#endif
}

legacy_u32 timer_wait_ticks(legacy_u32 ticks)
{
	(void)ticks;
	assert(0 && "Unexpected replay restart");
	return 0;
}

void frame_supersight_reset(void)
{
	supersight_reset_pending = 1;
}

struct CARSTATE *ghost_car_state(void)
{
	return ghost_fixture_active != 0 ? &ghost_fixture : 0;
}

void ghost_end_race(void)
{
	ghost_end_calls++;
}
void ghost_check_track(void)
{
}

static legacy_u32 trace_hash;
static legacy_u32 scenario, dialog_count, save_count, write_count, check_count;
static legacy_s16 menu_action;
static legacy_s16 restart_confirmation_answer;
static legacy_u32 restart_questions, restart_initializations;
static legacy_s8 exit_confirmation_text[] = "Exit to Dos]}[ No [ Yes ]";
static legacy_s8 *restart_exit_text;
static const legacy_s8 *restart_expected_text;
#define TEST_RESTART_INPUT_BUFFER_SIZE 128U
#define TEST_RESTART_INPUT_PATTERN 165U
static struct SHAPE2D shapes[23];
static legacy_u8 track_bytes[901];

static void hash_word(legacy_u16 value)
{
	trace_hash = (trace_hash ^ value) * 16777619UL;
}
static void event(legacy_u16 id)
{
	hash_word(id);
	hash_word(is_in_replay);
	hash_word(game_replay_mode);
	hash_word(replay_recording_flags);
	hash_word(elapsed_time2);
	hash_word(gameconfig.game_recordedframes);
	hash_word(g_is_busy);
}
void mouse_draw_opaque_check(void)
{
	event(1);
}
void mouse_draw_transparent_check(void)
{
	event(2);
}
void shape2d_rle_copy_at_position(struct SHAPE2D far *shape)
{
	event(3);
	hash_word((legacy_u16)(shape - shapes));
}
void format_frame_as_string(legacy_s8 *text, legacy_s16 frame, legacy_s16 fraction)
{
	event(4);
	hash_word(frame);
	hash_word(fraction);
	text[0] = '0';
	text[1] = 0;
}
void font_set_colors(legacy_s16 color, legacy_s16 background)
{
	event(5);
	hash_word(color);
	hash_word(background);
}
void font_set_fontdef2(void far *data)
{
	(void)data;
	event(6);
}
void font_set_fontdef(void)
{
	event(7);
}
void font_draw_text_opaque(const legacy_s8 *text, legacy_s16 x, legacy_s16 y)
{
	event(8);
	hash_word(text[0]);
	hash_word(x);
	hash_word(y);
}
void sprite_fill_rect(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
					  legacy_s16 color)
{
	event(9);
	hash_word(x);
	hash_word(y);
	hash_word(width);
	hash_word(height);
	hash_word(color);
}
void sprite_draw_rect_outline(legacy_s16 x1, legacy_s16 y1, legacy_s16 x2, legacy_s16 y2,
							  legacy_s16 color)
{
	event(10);
	hash_word(x1);
	hash_word(y1);
	hash_word(x2);
	hash_word(y2);
	hash_word(color);
}
void audio_carstate(void)
{
	event(11);
}
void check_input(void)
{
	event(12);
	check_count++;
}
void mouse_minmax_position(legacy_s16 enabled)
{
	event(13);
	hash_word(enabled);
}
void init_game_state_with_frame_rate_byte(legacy_u16 rate)
{
	assert(supersight_reset_pending != 0);
	assert(restart_questions == 1 && restart_confirmation_answer == REPLAY_DIALOG_CONFIRMED_CHOICE);
	restart_initializations++;
	supersight_reset_pending = 0;
	event(14);
	hash_word(rate);
	state.game_frame = 0;
}
void update_crash_state(legacy_s16 crash, legacy_s16 car)
{
	event(15);
	hash_word(crash);
	hash_word(car);
	state.game_end_event = crash;
}
legacy_s8 far *locate_text_res(legacy_s8 far *resource, const legacy_s8 *name)
{
	(void)resource;
	event(16);
	hash_word(name[0]);
	hash_word(name[1]);
	hash_word(name[2]);
	if (name == exit_to_dos_dialog_id) {
		assert(resource == mainresptr);
		return restart_exit_text;
	}
	return (legacy_s8 *)name;
}
legacy_u16 show_dialog(legacy_s16 type, legacy_s16 save, void far *text, legacy_u16 x, legacy_u16 y,
					   legacy_s16 color, legacy_s16 *disabled, legacy_s16 initial)
{
	legacy_u32 count = text == replay_pause_menu_id ? 8 : 5;
	(void)text;
	event(17);
	hash_word(type);
	hash_word(save);
	hash_word(x);
	hash_word(y);
	hash_word(color);
	hash_word(initial);
	if (disabled != 0) {
		if (text == replay_mode_options_dialog_id) {
			opponent_view_disabled = disabled[REPLAY_MODE_ACTION_FOLLOW_OPPONENT];
		}
		for (legacy_u32 index = 0; index < count; index++) {
			hash_word(disabled[index]);
		}
	}
	dialog_count++;
	if (text == replay_pause_menu_id) {
		return menu_action;
	}
	if (menu_action == REPLAY_PAUSE_ACTION_RESTART) {
		assert(type == DIALOG_TYPE_MENU && save == DIALOG_SAVE_BACKGROUND);
		assert(x == DIALOG_AUTO_POSITION && y == DIALOG_AUTO_POSITION);
		assert(color == dialog_border_color && disabled == 0 &&
			   initial == REPLAY_DIALOG_INITIAL_CHOICE);
		assert(strncmp((const legacy_char *)text, (const legacy_char *)restart_expected_text,
					   REPLAY_RESTART_DIALOG_CAPACITY) == 0);
		assert(restart_initializations == 0 && supersight_reset_pending == 0);
		restart_questions++;
		return (legacy_u16)restart_confirmation_answer;
	}
	if (menu_action == REPLAY_PAUSE_ACTION_DISPLAY_OPTIONS) {
		return scenario % 7 - 1;
	}
	return (scenario / 4) % 4 - 1;
}
legacy_s8 do_fileselect_dialog(legacy_s8 *directory, legacy_s8 *filename, legacy_s8 *extension,
							   legacy_s8 far *prompt)
{
	(void)directory;
	(void)filename;
	(void)extension;
	(void)prompt;
	event(18);
	return scenario & 1;
}
legacy_s16 do_savefile_dialog(legacy_s8 *directory, legacy_s8 *filename, legacy_s8 far *prompt)
{
	(void)directory;
	(void)filename;
	(void)prompt;
	event(19);
	return save_count++ == 0 && (scenario & 1);
}
void file_build_path(const legacy_s8 *directory, const legacy_s8 *name, const legacy_s8 *ext,
					 legacy_s8 *path)
{
	(void)directory;
	(void)name;
	(void)ext;
	event(20);
	path[0] = 'x';
	path[1] = 0;
}
const legacy_s8 *file_find(const legacy_s8 *query)
{
	event(21);
	return scenario & 2 ? query : 0;
}
legacy_s16 file_write_replay(const legacy_s8 *path)
{
	(void)path;
	event(22);
	write_count++;
	return (scenario >> 2) & 1;
}
legacy_s16 file_load_replay(const legacy_s8 *directory, const legacy_s8 *name)
{
	(void)directory;
	(void)name;
	event(23);
	if (scenario & 2) {
		gameconfig.game_playercarid[3] ^= 1;
	}
	if (scenario & 4) {
		gameconfig.game_opponenttype ^= 1;
	}
	if (scenario & 8) {
		gameconfig.game_opponentcarid[3] ^= 1;
	}
	gameconfig.game_framespersec = (scenario & 16) ? 0x128a : 20;
	return (scenario >> 5) & 1;
}
void show_waiting(void)
{
	event(24);
}
legacy_s16 track_setup(void)
{
	event(25);
	if (scenario & 64) {
		track_bytes[900] ^= 1;
	}
	return 0;
}
void ensure_file_exists(legacy_s16 argument)
{
	event(26);
	hash_word(argument);
}
void load_opponent_data(void)
{
	event(27);
}
void free_player_cars(void)
{
	event(28);
}
legacy_s16 setup_player_cars(void)
{
	event(29);
	return 0;
}
void init_game_state(legacy_s16 mode)
{
	assert(supersight_reset_pending != 0);
	supersight_reset_pending = 0;
	event(30);
	hash_word(mode);
	hash_word(framespersec);
}
void show_graphic_levels_menu(void)
{
	event(31);
}

static void reset_viewer(void)
{
	supersight_reset_pending = 0;
	restart_questions = restart_initializations = ghost_end_calls = 0;
	restart_confirmation_answer = REPLAY_DIALOG_CONFIRMED_CHOICE;
	restart_exit_text = exit_confirmation_text;
	restart_expected_text = (const legacy_s8 *)"Re-start driving?]}[ No [ Yes ]";
	memset(&state, 0, sizeof(state));
	memset(&gameconfig, 0, sizeof(gameconfig));
	memset(replay_controls_drawn, 0, 2 * sizeof(replay_controls_drawn[0]));
	memset(replay_control_active, 0, sizeof(replay_control_active));
	memset(replay_control_active_cache, 0, sizeof(replay_control_active_cache));
	memset(replay_camera_mode_cache, 0, sizeof(replay_camera_mode_cache));
	memset(replay_selection_cache, 0, sizeof(replay_selection_cache));
	memset(replay_displayed_time_cache, 0, sizeof(replay_displayed_time_cache));
	memset(replay_recorded_position_cache, 0, sizeof(replay_recorded_position_cache));
	memset(replay_current_position_cache, 0, sizeof(replay_current_position_cache));
	for (legacy_u32 index = 0; index < 23; index++) {
		rplyshapes[index] = &shapes[index];
	}
	dialog_count = save_count = write_count = check_count = 0;
	track_element_map = track_bytes;
	memset(track_bytes, 0, sizeof(track_bytes));
	game_replay_mode = REPLAY_MODE_PAUSED;
	is_in_replay = 0;
	dashboard_buffer_index = 0;
	cameramode = CAMERA_MODE_CUSTOM;
	replay_selected_control = REPLAY_CONTROL_PLAY;
	elapsed_time1 = 0;
	elapsed_time2 = 100;
	gameconfig.game_recordedframes = 100;
	gameconfig.game_framespersec = 20;
	state.game_frame = 60;
	state.game_frame_in_sec = 3;
	state.game_end_event = 2;
	dashb_toggle = 0;
	show_penalty_counter = 1;
	followOpponentFlag = 1;
	replay_playback_speed = REPLAY_PLAYBACK_FAST;
	replaybar_toggle = 1;
	race_exit_request = 0;
	g_is_busy = 0;
	kbormouse = 1;
	mouse_driving_enabled = 1;
	passed_security = 1;
	replay_recording_flags = REPLAY_RECORDING_ACTIVE_FLAG;
	replay_overflow_acknowledged_word = 0x5a01;
	waitflag = 0;
	framespersec = 20;
	full_redraw_frames_remaining = 0;
	video_page_count = 2;
	configured_frame_rate = 20;
}
static void hash_viewer_state(void)
{
	event(100);
	hash_word(state.game_frame);
	hash_word(state.game_frame_in_sec);
	hash_word(state.game_end_event);
	hash_word(dashb_toggle);
	hash_word(show_penalty_counter);
	hash_word(followOpponentFlag);
	hash_word(cameramode);
	hash_word(replay_playback_speed);
	hash_word(race_exit_request);
	hash_word(replay_overflow_acknowledged_word);
	hash_word(waitflag);
	hash_word(framespersec);
	hash_word(full_redraw_frames_remaining);
	hash_word(kbormouse);
	hash_word(dialog_count);
	hash_word(save_count);
	hash_word(write_count);
	hash_word(replaybar_toggle);
	hash_word(replay_selected_control);
	for (legacy_u32 index = 0; index < 2; index++) {
		hash_word(replay_controls_drawn[index]);
		hash_word(replay_camera_mode_cache[index]);
		hash_word(replay_selection_cache[index]);
		hash_word(replay_displayed_time_cache[index]);
		hash_word(replay_recorded_position_cache[index]);
		hash_word(replay_current_position_cache[index]);
	}
	for (legacy_u32 index = 0; index < 18; index++) {
		hash_word(replay_control_active_cache[index]);
	}
	for (legacy_u32 index = 0; index < 9; index++) {
		hash_word(replay_control_active[index]);
	}
}
static legacy_u32 menu_fingerprint(void)
{
	trace_hash = 2166136261UL;
	for (menu_action = -1; menu_action <= 7; menu_action++) {
		for (scenario = 0; scenario < 256; scenario++) {
			reset_viewer();
			replay_recording_flags = scenario & 15;
			passed_security = (scenario >> 4) & 1;
			state.playerstate.car_crashBmpFlag = (scenario >> 5) & 1;
			gameconfig.game_opponenttype = (scenario >> 6) & 1;
			if (scenario & 128) {
				gameconfig.game_recordedframes = 0;
				elapsed_time1 = 10;
			}
			replay_pause_menu();
			hash_viewer_state();
			assert(g_is_busy == 0);
		}
	}
	return trace_hash;
}
static legacy_u32 draw_fingerprint(void)
{
	trace_hash = 2166136261UL;
	for (legacy_u32 index = 0; index < 256; index++) {
		reset_viewer();
		gameconfig.game_recordedframes = index % 3 ? 65535 : 0;
		elapsed_time1 = index * 257;
		for (legacy_u32 tick = 0; tick < 8; tick++) {
			dashboard_buffer_index = tick & 1;
			cameramode = (index + tick / 2) & 3;
			replay_selected_control = tick % 3 ? tick % 9 : REPLAY_NO_SELECTION;
			replay_control_active[tick % 7] ^= 1;
			replay_controls_draw(index * 257 + tick, tick * 8192);
			hash_viewer_state();
			replay_controls_draw(index * 257 + tick, tick * 8192);
			hash_viewer_state();
		}
	}
	return trace_hash;
}
static void test_pause_cleanup(void)
{
	reset_viewer();
	menu_action = REPLAY_PAUSE_ACTION_CONTINUE;
	scenario = 0;
	elapsed_time2 = 20;
	replay_pause_menu();
	assert(game_replay_mode == REPLAY_MODE_PAUSED &&
		   replay_recording_flags == REPLAY_RECORDING_ACTIVE_FLAG);
	assert(elapsed_time2 == 20 && gameconfig.game_recordedframes == 100 && check_count == 1);
	reset_viewer();
	menu_action = REPLAY_PAUSE_ACTION_CONTINUE;
	scenario = 8;
	elapsed_time2 = 20;
	replay_pause_menu();
	assert(game_replay_mode == REPLAY_MODE_LIVE && is_in_replay == 0 && check_count == 2);
	assert(replay_recording_flags ==
		   (REPLAY_RECORDING_ACTIVE_FLAG | REPLAY_RECORDING_MODIFIED_FLAG));
	assert(elapsed_time2 == 60 && gameconfig.game_recordedframes == 60);
	assert(cameramode == CAMERA_MODE_COCKPIT && followOpponentFlag == 0 &&
		   show_penalty_counter == 0);
	reset_viewer();
	menu_action = REPLAY_PAUSE_ACTION_RESTART;
	scenario = 0;
	replay_pause_menu();
	assert(game_replay_mode == REPLAY_MODE_LIVE && check_count == 3);
	assert(restart_questions == 1 && restart_initializations == 1 && dialog_count == 2);
	assert(elapsed_time2 == 0 && gameconfig.game_recordedframes == 0);
	assert(replay_overflow_acknowledged_word == 0x5a00);
}

static void test_restart_confirmation_cancel(void)
{
	static const legacy_s16 answers[] = {0, -1, 2};
	static legacy_s8 input_bytes[TEST_RESTART_INPUT_BUFFER_SIZE];
	legacy_s8 far *saved_input_buffer = replay_input_buffer;
	for (legacy_u16 index = 0; index < sizeof(answers) / sizeof(answers[0]); index++) {
		reset_viewer();
		menu_action = REPLAY_PAUSE_ACTION_RESTART;
		restart_confirmation_answer = answers[index];
		replay_recording_flags = REPLAY_RECORDING_ACTIVE_FLAG | REPLAY_RECORDING_MODIFIED_FLAG |
								 REPLAY_RECORDING_RESTARTABLE_FLAG;
		replay_input_buffer = input_bytes;
		memset(input_bytes, TEST_RESTART_INPUT_PATTERN, sizeof(input_bytes));
		memcpy(replay_filename, "KEEP.RPL", sizeof("KEEP.RPL"));
		ghost_fixture_active = 1;
		memset(&ghost_fixture, TEST_RESTART_INPUT_PATTERN, sizeof(ghost_fixture));
		struct GAMESTATE saved_state = state;
		struct GAMEINFO saved_config = gameconfig;
		struct CARSTATE saved_ghost = ghost_fixture;
		replay_pause_menu();
		assert(restart_questions == 1 && restart_initializations == 0 && dialog_count == 2);
		assert(game_replay_mode == REPLAY_MODE_PAUSED && is_in_replay != 0 && check_count == 1);
		assert(memcmp(&state, &saved_state, sizeof(state)) == 0);
		assert(memcmp(&gameconfig, &saved_config, sizeof(gameconfig)) == 0);
		assert(ghost_fixture_active == 1 && ghost_end_calls == 0);
		assert(memcmp(&ghost_fixture, &saved_ghost, sizeof(ghost_fixture)) == 0);
		assert(replay_recording_flags ==
			   (REPLAY_RECORDING_ACTIVE_FLAG | REPLAY_RECORDING_MODIFIED_FLAG |
				REPLAY_RECORDING_RESTARTABLE_FLAG));
		assert(elapsed_time1 == 0 && elapsed_time2 == 100);
		assert(replay_overflow_acknowledged_word == 0x5a01 && race_exit_request == 0);
		assert(cameramode == CAMERA_MODE_CUSTOM && replay_playback_speed == REPLAY_PLAYBACK_FAST);
		assert(memcmp(replay_filename, "KEEP.RPL", sizeof("KEEP.RPL")) == 0);
		assert(replay_input_buffer == input_bytes);
		for (legacy_u16 byte = 0; byte < sizeof(input_bytes); byte++) {
			assert((legacy_u8)input_bytes[byte] == TEST_RESTART_INPUT_PATTERN);
		}
	}
	replay_input_buffer = saved_input_buffer;
	ghost_fixture_active = 0;
}

static void test_restart_confirmation_localized(void)
{
	static legacy_s8 localized_text[] = "Zum DOS zurueck]}[ Nein [ Ja ]";
	reset_viewer();
	menu_action = REPLAY_PAUSE_ACTION_RESTART;
	restart_exit_text = localized_text;
	restart_expected_text = (const legacy_s8 *)"Re-start driving?]}[ Nein [ Ja ]";
	replay_pause_menu();
	assert(restart_questions == 1 && restart_initializations == 1 &&
		   game_replay_mode == REPLAY_MODE_LIVE);
	assert(memcmp(localized_text, "Zum DOS zurueck]}[ Nein [ Ja ]", sizeof(localized_text)) == 0);
}

static void test_save_cleanup(void)
{
	reset_viewer();
	menu_action = REPLAY_PAUSE_ACTION_SAVE;
	scenario = 1;
	replay_pause_menu();
	assert(save_count == 1 && write_count == 1 && check_count == 1 && g_is_busy == 0);
	reset_viewer();
	menu_action = REPLAY_PAUSE_ACTION_SAVE;
	scenario = 0;
	replay_pause_menu();
	assert(save_count == 1 && write_count == 0 && check_count == 1 && g_is_busy == 0);
	reset_viewer();
	menu_action = REPLAY_PAUSE_ACTION_SAVE;
	scenario = 3;
	replay_pause_menu();
	/* The pause menu and overwrite question are each displayed once. */
	assert(save_count == 1 && write_count == 0 && dialog_count == 2 && g_is_busy == 0);
	reset_viewer();
	menu_action = REPLAY_PAUSE_ACTION_SAVE;
	scenario = 5;
	replay_pause_menu();
	assert(save_count == 2 && write_count == 1 && dialog_count == 2 && g_is_busy == 0);
}

static void test_ghost_view_display_option(void)
{
	reset_viewer();
	menu_action = REPLAY_PAUSE_ACTION_DISPLAY_OPTIONS;
	scenario = REPLAY_MODE_ACTION_FOLLOW_OPPONENT + 1;
	gameconfig.game_opponenttype = 0;
	ghost_fixture_active = 1;
	followOpponentFlag = 0;
	replay_display_options();
	assert(opponent_view_disabled == 0 && followOpponentFlag == 1);
	replay_display_options();
	assert(opponent_view_disabled == 0 && followOpponentFlag == 0);
	ghost_fixture_active = 0;
	scenario = 0;
	replay_display_options();
	assert(opponent_view_disabled == 1 && followOpponentFlag == 0);
}

static void test_paused_replay_fps_refresh(void)
{
	reset_viewer();
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	is_in_replay = 1;
	input_polls = fps_expiry_checks = 0;
	input_exit_poll = 10;
	fps_expire_poll = 3;
	replay_handle_input();
	assert(input_polls == 3 && fps_expiry_checks == 3);
	/* Once the value is zero, remain in the input loop until real camera input. */
	replay_handle_input();
	assert(input_polls == 10 && fps_expiry_checks == 9);
	/* Playing replays already return to present another camera frame. */
	is_in_replay = 0;
	input_polls = fps_expiry_checks = 0;
	replay_handle_input();
	assert(input_polls == 1 && fps_expiry_checks == 0);
}

#ifdef RESTUNTS_SDL3
static void test_controller_pause_menu(void)
{
	reset_viewer();
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	is_in_replay = 1;
	menu_action = 0;
	input_polls = 0;
	controller_menu_request = 1;
	replay_handle_input();
	assert(dialog_count == 1 && input_polls == 0);
	assert(controller_menu_request == 0 && check_count == 1);
}

static void test_controller_menu_during_scrub(void)
{
	static void (*const actions[])(void) = {replay_fast_forward, replay_rewind};
	for (legacy_u32 index = 0; index < sizeof(actions) / sizeof(actions[0]); index++) {
		reset_viewer();
		game_replay_mode = REPLAY_MODE_PLAYBACK;
		gameconfig.game_recordedframes = TEST_SEEK_RECORDING_FRAMES;
		elapsed_time2 = TEST_SEEK_START_FRAME;
		state.game_frame = TEST_SEEK_START_FRAME;
		input_combined_flags = INPUT_SECONDARY_ACTION_FLAG;
		menu_action = 0;
		controller_menu_request = 0;
		controller_scrub_testing = touch_seek_testing = 1;
		controller_scrub_polls = 0;
		actions[index]();
		assert(dialog_count == 1 && controller_menu_request == 0);
		assert(controller_scrub_polls > 1 && touch_seek_restores != 0);
		assert((input_combined_flags & INPUT_ACTION_BUTTON_MASK) != 0);
		assert((legacy_u16)state.game_frame == elapsed_time2);
		controller_scrub_testing = touch_seek_testing = 0;
	}
}

static void assert_touch_seek(legacy_u16 expected)
{
	assert(replay_touch_seek() != 0);
	assert(is_in_replay != 0 && replay_control_active[REPLAY_CONTROL_PAUSE] != 0);
	assert(state.game_frame == expected && elapsed_time2 == expected);
	assert(touch_seek_restores == 1);
	assert(touch_seek_updates == expected % TEST_SEEK_CHECKPOINT_INTERVAL);
	/* A held seeker at the same point must not reconstruct the replay again. */
	if (mouse_butstate != 0) {
		assert(replay_touch_seek() != 0);
		assert(touch_seek_restores == 1);
	} else {
		assert(replay_touch_seek() == 0);
	}
}

static void test_touch_seek_reconstruction(void)
{
	static const legacy_u8 rates[] = {GAME_FRAME_RATE_LOW, GAME_FRAME_RATE_NORMAL};
	static const legacy_s16 directions[] = {-1, 1};
	for (legacy_u32 rate = 0; rate < sizeof(rates) / sizeof(rates[0]); rate++) {
		for (legacy_u32 dir = 0; dir < sizeof(directions) / sizeof(directions[0]); dir++) {
			reset_viewer();
			touch_seek_testing = 1;
			touch_seek_restores = touch_seek_updates = 0;
			game_replay_mode = REPLAY_MODE_PLAYBACK;
			gameconfig.game_framespersec = rates[rate];
			gameconfig.game_recordedframes = TEST_SEEK_RECORDING_FRAMES;
			elapsed_time2 = TEST_SEEK_START_FRAME;
			mouse_butstate = 0;
			touch_seek_direction = directions[dir];
			assert_touch_seek(
				(legacy_u16)(TEST_SEEK_START_FRAME +
							 directions[dir] * REPLAY_TOUCH_SKIP_SECONDS * rates[rate]));
		}
	}
	reset_viewer();
	touch_seek_restores = touch_seek_updates = 0;
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	gameconfig.game_recordedframes = TEST_SEEK_RECORDING_FRAMES;
	mouse_butstate = REPLAY_TOUCH_LEFT_BUTTON;
	mouse_xpos = REPLAY_TIMELINE_X + REPLAY_TIMELINE_POSITION_RANGE / 2;
	mouse_ypos = REPLAY_TIMELINE_Y;
	assert_touch_seek(TEST_SEEK_RECORDING_FRAMES / 2);
	touch_seek_testing = 0;
}
#endif

legacy_int main(void)
{
	legacy_u32 menu = menu_fingerprint();
	legacy_u32 draw = draw_fingerprint();
	test_pause_cleanup();
	test_restart_confirmation_cancel();
	test_restart_confirmation_localized();
	test_save_cleanup();
	test_ghost_view_display_option();
	test_paused_replay_fps_refresh();
#ifdef RESTUNTS_SDL3
	test_touch_seek_reconstruction();
	test_controller_pause_menu();
	test_controller_menu_during_scrub();
#endif
#ifdef REPLAY_MENU_BASELINE
	printf("%08" LEGACY_PRIx32 " %08" LEGACY_PRIx32 "\n", menu, draw);
#else
	assert(menu == 0x5b094968UL);
	assert(draw == 0x9b2a836aUL);
#endif
	return 0;
}
