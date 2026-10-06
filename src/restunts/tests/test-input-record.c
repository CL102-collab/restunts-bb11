/* Regression fingerprints captured from the original routines before extraction. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef RESTUNTS_SDL3
#include "../c/frame_adaptive.h"
#endif
#ifndef INPUT_SOURCE
#define INPUT_SOURCE "../c/game_input.c"
#endif
#ifndef RECORD_SOURCE
#define RECORD_SOURCE "../c/replay_record.c"
#endif
#include INPUT_SOURCE
#include RECORD_SOURCE
#undef memcpy
#undef strcmp
#undef strlen

static struct CARSTATE ghost_fixture;
static legacy_s16 ghost_fixture_active;

struct CARSTATE *ghost_car_state(void)
{
	return ghost_fixture_active != 0 ? &ghost_fixture : 0;
}

static legacy_u32 trace_hash, random_state = 1;
static legacy_u32 fps_reset_count;
static legacy_u32 supersight_reset_count;
#define TEST_SUPERSIGHT_STATUS_CAPACITY 16
static legacy_u32 supersight_status_count;
static legacy_s8 supersight_status[TEST_SUPERSIGHT_STATUS_CAPACITY];
#ifdef RESTUNTS_SDL3
static legacy_u16 touch_shift_flags;

legacy_u16 sdl3_touch_take_shift_flags(void)
{
	legacy_u16 flags = touch_shift_flags;
	touch_shift_flags = 0;
	return flags;
}

static legacy_u32 hires_enabled_transitions;
static legacy_u32 render_timing_reset_count;
void frame_render_timing_reset(void)
{
	render_timing_reset_count++;
}
static legacy_s32 test_hires_enabled, test_hires_scale = HIRES_SCALE;

void hires_set_enabled(legacy_s32 enabled)
{
	if (test_hires_enabled != enabled) {
		test_hires_enabled = enabled;
		test_hires_scale = HIRES_SCALE;
		hires_enabled_transitions++;
	}
}

legacy_s32 hires_render_scale(void)
{
	return test_hires_scale;
}

void hires_set_render_scale(legacy_s32 scale)
{
	test_hires_scale = scale;
}
#endif

void frame_supersight_show_status(const legacy_s8 *name)
{
	size_t length = strlen((const legacy_char *)name);
	assert(length < sizeof(supersight_status));
	memcpy(supersight_status, name, length + 1);
	supersight_status_count++;
}

void frame_supersight_reset(void)
{
	supersight_reset_count++;
#ifdef RESTUNTS_SDL3
	frame_adaptive_restart(&frame_adaptive);
#endif
}

void frame_fps_reset(void)
{
	fps_reset_count++;
}
static legacy_u16 keyboard_char, joystick_flags;
static legacy_s16 key_states[128], mouse_samples[4][3], joystick_axis;
static legacy_u32 mouse_sample_index;
static legacy_u8 joystick_enabled, segments_match, nested_callback;
static legacy_s8 replay_bytes[12000], response_table[256];
static struct GAMESTATE snapshots[41];
static struct AUDIO_CAR_STATE audio_samples[AUDIO_CAR_STATE_RECORD_COUNT];
#ifdef RESTUNTS_SDL3
static legacy_u8 controller_mode_set;
static enum SDL3_DRIVING_INPUT controller_mode;
static legacy_s16 controller_x, controller_pedals, controller_shifts, controller_camera;
static legacy_u16 joystick_menu_hold_polls, joystick_menu_polls;

enum SDL3_DRIVING_INPUT sdl3_input_driving_mode(void)
{
	if (controller_mode_set != 0) {
		return controller_mode;
	}
	return mouse_driving_enabled != 0 ? SDL3_DRIVING_MOUSE
		   : joystick_enabled != 0	  ? SDL3_DRIVING_JOYSTICK
									  : SDL3_DRIVING_KEYBOARD;
}

legacy_s16 sdl3_joystick_menu_flags(void)
{
	joystick_menu_polls++;
	if (joystick_menu_hold_polls != 0) {
		joystick_menu_hold_polls--;
		return INPUT_PRIMARY_ACTION_FLAG;
	}
	return joystick_flags;
}

legacy_s16 sdl3_controller_driving_flags(void)
{
	legacy_s16 flags = controller_pedals | controller_shifts;
	controller_shifts = 0;
	return flags;
}

legacy_s16 sdl3_controller_scaled_x(void)
{
	return controller_x;
}

legacy_s16 sdl3_controller_camera_flags(void)
{
	legacy_s16 flags = controller_camera;
	controller_camera = 0;
	return flags;
}
#endif

static void hash_word(legacy_u16 value)
{
	trace_hash = (trace_hash ^ value) * 16777619UL;
}

static legacy_u16 random_word(void)
{
	random_state = random_state * 1664525UL + 1013904223UL;
	return (legacy_u16)(random_state >> 16);
}

legacy_s16 dos_kb_get_char(void)
{
	hash_word(1);
	return keyboard_char;
}
legacy_s16 kb_read_char(void)
{
	return keyboard_char;
}
legacy_s16 kb_get_key_state(legacy_s16 key)
{
	hash_word(0x100 | key);
	return key_states[key];
}
legacy_s16 dos_get_joy_flags(void)
{
	hash_word(2);
#ifdef RESTUNTS_SDL3
	if (controller_mode_set != 0 && joystick_enabled == 0) {
		return 0;
	}
#endif
	return joystick_flags;
}
legacy_u8 dos_joystick_is_enabled(void)
{
	hash_word(3);
	return joystick_enabled;
}
legacy_s16 dos_joystick_get_scaled_axis(legacy_u16 axis)
{
	hash_word(0x200 | axis);
	return joystick_axis;
}
legacy_s16 dos_data_stack_segments_match(void)
{
	hash_word(4);
	return segments_match;
}
void dos_mouse_get_state(legacy_s16 *buttons, legacy_s16 *x, legacy_s16 *y)
{
	legacy_u32 index = mouse_sample_index;
	hash_word(5);
	*buttons = mouse_samples[index][0];
	*x = mouse_samples[index][1];
	*y = mouse_samples[index][2];
	if (mouse_sample_index < 3) {
		mouse_sample_index++;
	}
}
void mouse_draw_opaque(void)
{
	hash_word(6);
}
void mouse_draw_transparent(void)
{
	hash_word(7);
}
void dos_mouse_set_minmax(legacy_s16 x1, legacy_s16 y1, legacy_s16 x2, legacy_s16 y2)
{
	hash_word(8);
	hash_word(x1);
	hash_word(y1);
	hash_word(x2);
	hash_word(y2);
}
void dos_mouse_set_position(legacy_s16 x, legacy_s16 y)
{
	hash_word(9);
	hash_word(x);
	hash_word(y);
}
void select_mouse_driving(void)
{
	hash_word(10);
	mouse_driving_enabled ^= 1;
}
void init_game_state_with_frame_rate_byte(legacy_u16 rate)
{
	hash_word(11);
	hash_word(rate);
}
void update_crash_state(legacy_s16 event, legacy_s16 car)
{
	hash_word(12);
	hash_word(event);
	hash_word(car);
	state.game_end_event = event;
}
void audio_carstate(void)
{
	hash_word(13);
	hash_word(is_in_replay);
}
void audio_apply_car_state_sample(const legacy_u8 far *sample, legacy_s16 interval)
{
	hash_word(14);
	hash_word((legacy_u16)(sample - (const legacy_u8 *)audio_samples));
	hash_word(interval);
	if (nested_callback != 0) {
		nested_callback = 0;
		frame_callback();
	}
}
legacy_u32 timer_get_delta_alt(void)
{
	hash_word(15);
	return 3;
}
void sprite_fill_rect(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
					  legacy_s16 color)
{
	hash_word(16);
	hash_word(x);
	hash_word(y);
	hash_word(width);
	hash_word(height);
	hash_word(color);
}

void far *__fmemcpy(void far *destination, const void far *source, legacy_u16 count)
{
	return memcpy(destination, source, count);
}

static void reset_inputs(void)
{
	memset(&state, 0, sizeof(state));
	memset(&gameconfig, 0, sizeof(gameconfig));
	memset(key_states, 0, sizeof(key_states));
	memset(mouse_samples, 0, sizeof(mouse_samples));
	keyboard_char = joystick_flags = 0;
	supersight_enabled = fps_display_enabled = 0;
	fps_reset_count = supersight_reset_count = supersight_status_count = 0;
	supersight_status[0] = 0;
#ifdef RESTUNTS_SDL3
	touch_shift_flags = 0;
	controller_mode_set = 0;
	joystick_menu_hold_polls = joystick_menu_polls = 0;
	controller_x = controller_pedals = controller_shifts = controller_camera = 0;
	frame_adaptive_reset(&frame_adaptive);
	render_timing_reset_count = 0;
	hires_enabled_transitions = 0;
	test_hires_enabled = 0;
	test_hires_scale = HIRES_SCALE;
#endif
	video_page_count = 2;
	full_redraw_frames_remaining = 0;
	mouse_sample_index = 0;
	joystick_axis = 0;
	joystick_enabled = 0;
	segments_match = 1;
	nested_callback = 0;
	mouse_xpos = mouse_ypos = mouse_butstate = 0;
	mouse_driving_enabled = 0;
	input_elapsed_frames = input_mouse_repeat_at = input_joystick_repeat_at = 0;
	input_mouse_idle_frames = input_combined_flags = input_joystick_flags = 0;
	input_new_joystick_flags = input_joystick_keycode = input_mouse_keycode = 0;
	input_mouse_previous_x = input_mouse_previous_y = input_mouse_previous_buttons = 0;
	mouse_transparent_mode = mouse_background_dirty = kbormouse = h_key_toggle = 0;
	frame_callback_active = 0;
	frame_callback_count = 0;
	audio_car_state_interval = 0;
	audio_car_state_read_index = audio_car_state_write_index = 0;
	audio_car_state_records = audio_samples;
	audio_previous_replay_mode = 255;
	frame_callback_countdown = 1;
	slow_replay_countdown = 1;
	timer_ticks_per_frame = 6;
	framespersec = 20;
	game_replay_mode = REPLAY_MODE_LIVE;
	is_in_replay = 0;
	race_exit_request = recording_limit_warning_requested = 0;
	passed_security = 1;
	race_start_sequence_state = RACE_START_SEQUENCE_INACTIVE;
	elapsed_time1 = elapsed_time2 = 0;
	replay_overflow_acknowledged_word = 0;
	replay_playback_speed = REPLAY_PLAYBACK_NORMAL;
	replay_recording_flags = 0;
	replay_input_buffer = replay_bytes;
	cvxptr = snapshots;
	steerWhlRespTable_ptr = response_table;
	memset(input_steering_history, 0, sizeof(input_steering_history));
	memset(input_steering_history_valid, 0, sizeof(input_steering_history_valid));
	input_steering_value = 0;
}

static void hash_input_state(legacy_s16 result)
{
	hash_word(result);
	hash_word(input_elapsed_frames);
	hash_word(input_mouse_repeat_at);
	hash_word(input_joystick_repeat_at);
	hash_word(input_mouse_idle_frames);
	hash_word(input_combined_flags);
	hash_word(input_joystick_flags);
	hash_word(input_new_joystick_flags);
	hash_word(input_joystick_keycode);
	hash_word(input_mouse_keycode);
	hash_word(kbormouse);
	hash_word(mouse_xpos);
	hash_word(mouse_ypos);
	hash_word(mouse_butstate);
}

static legacy_u32 input_fingerprint(void)
{
	trace_hash = 2166136261UL;
	static const legacy_s16 deltas[] = {-32768, -1, 0, 1, 20, 21, 500, 501, 10000, 20001, 32767};
	for (legacy_u32 sample = 0; sample < 2048; sample++) {
		reset_inputs();
		input_elapsed_frames = deltas[sample % 11];
		input_mouse_repeat_at = deltas[(sample / 11) % 11];
		input_joystick_repeat_at = deltas[(sample / 121) % 11];
		mouse_transparent_mode = sample & 1;
		mouse_background_dirty = (sample >> 1) & 1;
		for (legacy_u32 step = 0; step < 4; step++) {
			keyboard_char = random_word() % 3 == 0 ? KEY_ESCAPE : 0;
			joystick_flags = random_word() & 63;
			for (legacy_u32 i = 0; i < 10; i++) {
				key_states[input_key_scancodes[i]] = random_word() % 8 == 0;
			}
			mouse_samples[step][0] = random_word() & 3;
			mouse_samples[step][1] = random_word() % 320;
			mouse_samples[step][2] = random_word() % 200;
			hash_input_state(input_checking(deltas[(sample + step) % 11]));
		}
	}
	return trace_hash;
}

static legacy_u32 scrollbar_fingerprint(void)
{
	trace_hash = 2166136261UL;
	for (legacy_u32 horizontal = 0; horizontal < 2; horizontal++) {
		for (legacy_u32 operation = 0; operation < 3; operation++) {
			for (legacy_u32 position = 0; position < 8; position++) {
				for (legacy_u32 drag = 0; drag < 3; drag++) {
					reset_inputs();
					legacy_s16 x = 20;
					mouse_xpos = x + (horizontal ? position * 15 : 2);
					legacy_s16 y = 30;
					mouse_ypos = y + (horizontal ? 2 : position * 15);
					mouse_samples[0][0] = 1;
					mouse_samples[1][0] = 0;
					mouse_samples[0][1] = mouse_samples[1][1] =
						mouse_xpos + (horizontal ? (legacy_s32)drag * 70 - 70 : 0);
					mouse_samples[0][2] = mouse_samples[1][2] =
						mouse_ypos + (horizontal ? 0 : (legacy_s32)drag * 70 - 70);
					legacy_s16 result = scrollbar_update(operation, x, horizontal ? 120 : 6, y,
														 horizontal ? 6 : 120, 3, 2, 10);
					hash_input_state(result);
				}
			}
		}
	}
	return trace_hash;
}

static void configure_record_sample(legacy_u32 sample)
{
	reset_inputs();
	framespersec = sample & 1 ? 10 : 20;
	game_replay_mode = sample % 3;
	elapsed_time1 = sample & 2 ? 600 : 0;
	elapsed_time2 = sample & 4 ? 12000 : (sample & 8 ? 11999 : sample % 30);
	gameconfig.game_recordedframes = elapsed_time2 + (sample & 16 ? 1 : 0);
	state.game_frame = elapsed_time2;
	passed_security = (sample >> 5) & 1;
	race_exit_request = (sample >> 6) & 1;
	state.game_end_event = sample & 128 ? 2 : 0;
	mouse_driving_enabled = (sample >> 8) & 1;
	joystick_enabled = (sample >> 9) & 1;
	joystick_axis = sample % 67 - 33;
	replay_overflow_acknowledged_word = sample & 1024 ? 0x5501 : 0x5500;
	key_states[KEY_SCAN_GEAR_UP] = sample & 1;
	key_states[KEY_SCAN_GEAR_DOWN] = sample & 2;
	mouse_samples[0][0] = sample % 4;
	mouse_samples[0][1] = sample % 321;
	for (legacy_u32 i = 0; i < 12000; i++) {
		replay_bytes[i] = (legacy_s8)(i * 31 + sample);
	}
	for (legacy_u32 i = 0; i < 41; i++) {
		memset(&snapshots[i], 0x5a, sizeof(snapshots[i]));
		snapshots[i].game_frame = i * 300;
	}
}

static void hash_record_state(void)
{
	hash_word(elapsed_time1);
	hash_word(elapsed_time2);
	hash_word(gameconfig.game_recordedframes);
	hash_word(replay_overflow_acknowledged_word);
	hash_word(recording_limit_warning_requested);
	hash_word(state.game_frame);
	hash_word(state.game_end_event);
	hash_word(race_exit_request);
	hash_word(is_in_replay);
	hash_word(frame_callback_count);
	hash_word(frame_callback_countdown);
	hash_word(slow_replay_countdown);
	hash_word(audio_car_state_interval);
	hash_word(audio_car_state_read_index);
	hash_word(frame_callback_active);
	for (legacy_u32 i = 0; i < 12000; i++) {
		hash_word((legacy_u8)replay_bytes[i]);
	}
	for (legacy_u32 i = 0; i < 41; i++) {
		hash_word(snapshots[i].game_frame);
	}
	for (legacy_u32 i = 0; i < 64; i++) {
		hash_word(input_steering_history[i]);
		hash_word(input_steering_history_valid[i]);
	}
}

static legacy_u32 record_fingerprint(void)
{
	trace_hash = 2166136261UL;
	for (legacy_u32 sample = 0; sample < 2048; sample++) {
		configure_record_sample(sample);
		replay_update_input_tick(sample % 7 == 0);
		hash_record_state();
	}
	return trace_hash;
}

static legacy_u32 callback_fingerprint(void)
{
	trace_hash = 2166136261UL;
	for (legacy_u32 sample = 0; sample < 512; sample++) {
		configure_record_sample(sample);
		elapsed_time1 = 0;
		elapsed_time2 = 20;
		gameconfig.game_recordedframes = 25;
		state.game_frame_in_sec = sample & 1 ? 2 : 0;
		state.game_frames_per_sec = 1;
		audio_car_state_read_index = sample & 2 ? AUDIO_CAR_STATE_RECORD_COUNT - 1 : 0;
		audio_car_state_write_index = 1;
		audio_car_state_interval = 5;
		replay_playback_speed = (legacy_u8)((sample % 3) - 1);
		segments_match = (sample >> 2) & 1;
		nested_callback = (sample >> 3) & 1;
		is_in_replay = (sample >> 4) & 1;
		for (legacy_u32 tick = 0; tick < 8; tick++) {
			frame_callback();
		}
		hash_record_state();
	}
	return trace_hash;
}

/* All key values exercise the pause fallback as well as the recognized shortcuts. */
static legacy_u32 shortcut_fingerprint(void)
{
	trace_hash = 2166136261UL;
	for (legacy_u32 mode = 0; mode < 3; mode++) {
		for (legacy_u32 key = 0; key < 65536UL; key++) {
			reset_inputs();
			game_replay_mode = mode;
			gameconfig.game_opponenttype = key & 1;
			cameramode = key & 3;
			dashb_toggle = replaybar_toggle = followOpponentFlag = 0;
			hash_word(handle_ingame_kb_shortcuts(LEGACY_S16_FROM_BITS(key)));
			hash_word(game_replay_mode);
			hash_word(race_exit_request);
			hash_word(cameramode);
			hash_word(dashb_toggle);
			hash_word(replaybar_toggle);
			hash_word(h_key_toggle);
			hash_word(followOpponentFlag);
			hash_word(mouse_driving_enabled);
			hash_word(race_start_sequence_state);
		}
	}
	return trace_hash;
}

static void test_event_priority(void)
{
	reset_inputs();
	keyboard_char = 'a';
	joystick_flags = 63;
	for (legacy_u32 index = 0; index < 4; index++) {
		mouse_samples[index][0] = 3;
	}
	assert(input_checking(0) == 'a');
	keyboard_char = 0;
	assert(input_checking(0) == KEY_ENTER);
	assert(input_checking(0) == KEY_SPACE);
	assert(input_checking(0) == 0);
	reset_inputs();
	key_states[input_key_scancodes[0]] = 1;
	joystick_flags = INPUT_ACCELERATE_FLAG;
#ifdef RESTUNTS_SDL3
	assert(get_kb_or_joy_flags() == (INPUT_PRIMARY_ACTION_FLAG | INPUT_ACCELERATE_FLAG));
#else
	assert(get_kb_or_joy_flags() == INPUT_PRIMARY_ACTION_FLAG);
#endif
	reset_inputs();
	joystick_flags = INPUT_PRIMARY_ACTION_FLAG;
	assert(input_checking(0) == KEY_SPACE);
	assert(input_checking(INPUT_REPEAT_DELAY_FRAMES) == 0);
	assert(input_checking(1) == KEY_SPACE);
}

static void test_recording_input_modes(void)
{
	static const legacy_s8 steering[] = {-1, 0, 0, 0, 0, 0, 1};
	static const legacy_s16 positions[] = {141, 142, 143, 160, 177, 178, 179};
	for (legacy_u32 index = 0; index < 7; index++) {
		reset_inputs();
		mouse_driving_enabled = joystick_enabled = 1;
		mouse_samples[0][0] = 3;
		mouse_samples[0][1] = positions[index];
		replay_update_input_tick(0);
		assert(replay_bytes[0] == INPUT_BRAKE_FLAG);
		assert(LEGACY_S8_FROM_BITS(input_steering_history[0]) == steering[index]);
		assert(input_steering_history_valid[0] == 1);
	}
	reset_inputs();
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	gameconfig.game_recordedframes = 5;
	replay_bytes[0] = 63;
	replay_update_input_tick(0);
	assert(elapsed_time2 == 1 && gameconfig.game_recordedframes == 5 && replay_bytes[0] == 63);
	replay_update_input_tick(1);
	assert(elapsed_time2 == 2 && gameconfig.game_recordedframes == 6 && replay_bytes[1] == 0);
}

#ifdef RESTUNTS_SDL3
#define TEST_CONTROLLER_AXIS 16
#define TEST_CONTROLLER_TARGET_ANGLE 44
#define TEST_CONTROLLER_RESPONSE 8

static void test_controller_recording(void)
{
	reset_inputs();
	controller_mode_set = 1;
	controller_mode = SDL3_DRIVING_CONTROLLER;
	controller_x = TEST_CONTROLLER_AXIS;
	controller_shifts = INPUT_SHIFT_UP_FLAG;
	key_states[(legacy_u16)KEY_RIGHT >> LEGACY_BYTE_BITS] = 1;
	key_states[(legacy_u16)KEY_UP >> LEGACY_BYTE_BITS] = 1;
	replay_update_input_tick(0);
	assert(replay_bytes[0] == (INPUT_ACCELERATE_FLAG | INPUT_SHIFT_UP_FLAG));
	assert(input_steering_history_valid[0] != 0);
	assert(LEGACY_S8_FROM_BITS(input_steering_history[0]) == TEST_CONTROLLER_TARGET_ANGLE);
	response_table[STEERING_RESPONSE_TABLE_OFFSET] = TEST_CONTROLLER_RESPONSE;
	/* A later source takeover cannot erase already sampled analog steering. */
	controller_mode = SDL3_DRIVING_KEYBOARD;
	replay_apply_analog_steering_history();
	controller_mode = SDL3_DRIVING_CONTROLLER;
	assert(replay_bytes[0] ==
		   (INPUT_ACCELERATE_FLAG | INPUT_SHIFT_UP_FLAG | INPUT_STEER_RIGHT_FLAG));
	assert(input_steering_history_valid[0] == 0);
	replay_update_input_tick(0);
	assert(replay_bytes[1] == INPUT_ACCELERATE_FLAG);

	/* Keyboard steering replaces the target while controller pedals still work.
	 * A reused ring slot cannot apply an older analog target to the new sample. */
	controller_mode = SDL3_DRIVING_KEYBOARD;
	controller_pedals = INPUT_BRAKE_FLAG;
	key_states[(legacy_u16)KEY_UP >> LEGACY_BYTE_BITS] = 0;
	elapsed_time2 = INPUT_STEERING_HISTORY_SIZE;
	input_steering_history_valid[0] = 1;
	replay_update_input_tick(0);
	assert(replay_bytes[INPUT_STEERING_HISTORY_SIZE] ==
		   (INPUT_BRAKE_FLAG | INPUT_STEER_RIGHT_FLAG));
	assert(input_steering_history_valid[0] == 0);

	/* Leaving a joystick selected cannot restore its steering after a keyboard
	 * release; its pedals still contribute independently. */
	joystick_enabled = 1;
	joystick_flags = INPUT_STEER_LEFT_FLAG | INPUT_ACCELERATE_FLAG;
	key_states[(legacy_u16)KEY_RIGHT >> LEGACY_BYTE_BITS] = 0;
	controller_pedals = 0;
	assert(get_kb_or_joy_flags() == INPUT_ACCELERATE_FLAG);
}

static void test_raw_joystick_menu_release(void)
{
	reset_inputs();
	controller_mode_set = 1;
	controller_mode = SDL3_DRIVING_KEYBOARD;
	joystick_flags = INPUT_PRIMARY_ACTION_FLAG;
	assert(joystick_enabled == 0 && get_kb_or_joy_flags() == 0);
	assert(input_checking(0) == KEY_SPACE);
	assert((input_combined_flags & INPUT_PRIMARY_ACTION_FLAG) != 0);
	joystick_flags = 0;
	joystick_menu_polls = 0;
	joystick_menu_hold_polls = 2;
	check_input();
	assert(joystick_menu_hold_polls == 0 && joystick_menu_polls > 2);
}

static void test_controller_camera(void)
{
	reset_inputs();
	custom_camera.azimuth_angle = CUSTOM_CAMERA_INITIAL_AZIMUTH_ANGLE;
	custom_camera.elevation_angle = CUSTOM_CAMERA_INITIAL_ELEVATION_ANGLE;
	cameramode = CAMERA_MODE_COCKPIT;
	controller_camera = INPUT_STEER_RIGHT_FLAG | INPUT_ACCELERATE_FLAG;
	assert(input_update_controller_camera() == 0);
	assert(custom_camera.azimuth_angle == CUSTOM_CAMERA_INITIAL_AZIMUTH_ANGLE);
	cameramode = CAMERA_MODE_CUSTOM;
	assert(input_update_controller_camera() == 0);
	controller_camera = INPUT_STEER_RIGHT_FLAG | INPUT_ACCELERATE_FLAG;
	assert(input_update_controller_camera() != 0);
	assert(custom_camera.azimuth_angle == CUSTOM_CAMERA_INITIAL_AZIMUTH_ANGLE + CAMERA_ANGLE_STEP);
	assert(custom_camera.elevation_angle ==
		   CUSTOM_CAMERA_INITIAL_ELEVATION_ANGLE + CAMERA_ANGLE_STEP);
	custom_camera.elevation_angle = CUSTOM_CAMERA_ELEVATION_LIMIT - CAMERA_ANGLE_STEP;
	controller_camera = INPUT_ACCELERATE_FLAG;
	assert(input_update_controller_camera() == 0);
	assert(custom_camera.elevation_angle == CUSTOM_CAMERA_ELEVATION_LIMIT - CAMERA_ANGLE_STEP);
}

static void test_touch_shift_recording(void)
{
	static const legacy_u16 shifts[] = {INPUT_SHIFT_UP_FLAG, INPUT_SHIFT_DOWN_FLAG};
	for (legacy_u32 index = 0; index < sizeof(shifts) / sizeof(shifts[0]); index++) {
		reset_inputs();
		touch_shift_flags = shifts[index];
		replay_update_input_tick(0);
		assert(replay_bytes[0] == shifts[index]);
		replay_update_input_tick(0);
		assert(replay_bytes[1] == 0);
	}
}
#endif

static void test_ghost_view_shortcut(void)
{
	for (legacy_u8 mode = REPLAY_MODE_LIVE; mode <= REPLAY_MODE_PLAYBACK; mode++) {
		reset_inputs();
		game_replay_mode = mode;
		gameconfig.game_opponenttype = 0;
		followOpponentFlag = 0;
		ghost_fixture_active = 0;
		assert(handle_ingame_kb_shortcuts('t') == 1);
		assert(followOpponentFlag == 0);
		ghost_fixture_active = 1;
		assert(handle_ingame_kb_shortcuts('t') == 1);
		assert(followOpponentFlag == 1);
		assert(handle_ingame_kb_shortcuts('t') == 1);
		assert(followOpponentFlag == 0);
		assert(gameconfig.game_opponenttype == 0 && game_replay_mode == mode);
		assert(handle_ingame_kb_shortcuts('t') == 1);
		assert(followOpponentFlag == 1);
		ghost_fixture_active = 0;
		assert(handle_ingame_kb_shortcuts('t') == 1);
		assert(followOpponentFlag == 0);
	}
	ghost_fixture_active = 0;
}

#define TEST_CAMERA_CLOSE_DISTANCE 150
#define TEST_CAMERA_NORMAL_DISTANCE 210
#define TEST_CAMERA_FAR_DISTANCE 300
#define TEST_CAMERA_CHASE_ELEVATION 48
#define TEST_CAMERA_CHASE_AZIMUTH 512

static const struct CUSTOM_CAMERA test_camera_presets[] = {
	{TEST_CAMERA_CLOSE_DISTANCE, TEST_CAMERA_CHASE_ELEVATION, TEST_CAMERA_CHASE_AZIMUTH},
	{TEST_CAMERA_NORMAL_DISTANCE, TEST_CAMERA_CHASE_ELEVATION, TEST_CAMERA_CHASE_AZIMUTH},
	{TEST_CAMERA_FAR_DISTANCE, TEST_CAMERA_CHASE_ELEVATION, TEST_CAMERA_CHASE_AZIMUTH}};
static const struct CUSTOM_CAMERA test_camera_originals[] = {
	{CUSTOM_CAMERA_INITIAL_DISTANCE, CUSTOM_CAMERA_INITIAL_ELEVATION_ANGLE,
	 CUSTOM_CAMERA_INITIAL_AZIMUTH_ANGLE},
	{CUSTOM_CAMERA_INITIAL_DISTANCE + CAMERA_ZOOM_STEP,
	 CUSTOM_CAMERA_INITIAL_ELEVATION_ANGLE + CAMERA_ANGLE_STEP,
	 CUSTOM_CAMERA_INITIAL_AZIMUTH_ANGLE - CAMERA_ANGLE_STEP}};

static void assert_custom_camera(const struct CUSTOM_CAMERA *expected, legacy_s8 dashboard)
{
	assert(custom_camera.distance == expected->distance);
	assert(custom_camera.elevation_angle == expected->elevation_angle);
	assert(custom_camera.azimuth_angle == expected->azimuth_angle);
	assert(dashb_toggle == dashboard);
}

static void test_custom_camera_cycle(void)
{
	for (legacy_u8 mode = REPLAY_MODE_LIVE; mode <= REPLAY_MODE_PLAYBACK; mode++) {
		for (legacy_s8 dashboard = 0; dashboard <= 1; dashboard++) {
			for (legacy_u32 original = 0;
				 original < sizeof(test_camera_originals) / sizeof(test_camera_originals[0]);
				 original++) {
				reset_inputs();
				camera_select_mode(CAMERA_MODE_COCKPIT);
				game_replay_mode = mode;
				followOpponentFlag = replaybar_toggle = 1;
				custom_camera = test_camera_originals[original];
				dashb_toggle = dashboard;
				assert(handle_ingame_kb_shortcuts(KEY_F3) == 1);
				assert(cameramode == CAMERA_MODE_CUSTOM);
				assert_custom_camera(&test_camera_originals[original], dashboard);
				for (legacy_u32 preset = 0;
					 preset < sizeof(test_camera_presets) / sizeof(test_camera_presets[0]);
					 preset++) {
					assert(handle_ingame_kb_shortcuts(KEY_F3) == 1);
					assert_custom_camera(&test_camera_presets[preset], 0);
					assert(cameramode == CAMERA_MODE_CUSTOM && game_replay_mode == mode);
					assert(followOpponentFlag == 1 && replaybar_toggle == 1);
					/* Manual camera edits and D remain usable. The next F3 selects
					 * an absolute preset, and wrapping restores the original view. */
					custom_camera = test_camera_originals[original];
					assert(handle_ingame_kb_shortcuts('D') == 1);
					assert(dashb_toggle == 1);
				}
				assert(handle_ingame_kb_shortcuts(KEY_F3) == 1);
				assert_custom_camera(&test_camera_originals[original], dashboard);
				assert(cameramode == CAMERA_MODE_CUSTOM && game_replay_mode == mode);
			}
		}
	}
	camera_select_mode(CAMERA_MODE_COCKPIT);
}

static void test_custom_camera_switches(void)
{
	static const struct {
		legacy_s16 key;
		legacy_s8 mode;
	} shortcuts[] = {{KEY_F1, CAMERA_MODE_COCKPIT},
					 {KEY_F2, CAMERA_MODE_FOLLOW},
					 {KEY_F4, CAMERA_MODE_TRACKSIDE},
					 {'c', CAMERA_MODE_TRACKSIDE},
					 {'C', CAMERA_MODE_TRACKSIDE}};
	const struct CUSTOM_CAMERA *original = &test_camera_originals[1];
	for (legacy_u8 mode = REPLAY_MODE_LIVE; mode <= REPLAY_MODE_PLAYBACK; mode++) {
		for (legacy_s8 dashboard = 0; dashboard <= 1; dashboard++) {
			for (legacy_u32 shortcut = 0; shortcut < sizeof(shortcuts) / sizeof(shortcuts[0]);
				 shortcut++) {
				for (legacy_u32 preset = 0;
					 preset < sizeof(test_camera_presets) / sizeof(test_camera_presets[0]);
					 preset++) {
					reset_inputs();
					camera_select_mode(CAMERA_MODE_COCKPIT);
					game_replay_mode = mode;
					custom_camera = *original;
					dashb_toggle = dashboard;
					assert(handle_ingame_kb_shortcuts(KEY_F3) == 1);
					for (legacy_u32 press = 0; press <= preset; press++) {
						assert(handle_ingame_kb_shortcuts(KEY_F3) == 1);
					}
					assert_custom_camera(&test_camera_presets[preset], 0);
					assert(handle_ingame_kb_shortcuts(shortcuts[shortcut].key) == 1);
					if (mode == REPLAY_MODE_PAUSED &&
						(shortcuts[shortcut].key == 'c' || shortcuts[shortcut].key == 'C')) {
						/* C keeps its existing paused-race behavior. */
						assert(cameramode == CAMERA_MODE_CUSTOM);
						assert_custom_camera(&test_camera_presets[preset], 0);
						camera_select_mode(shortcuts[shortcut].mode);
					}
					assert(cameramode == shortcuts[shortcut].mode && game_replay_mode == mode);
					assert_custom_camera(original, dashboard);
					assert(handle_ingame_kb_shortcuts(KEY_F3) == 1);
					assert_custom_camera(original, dashboard);
					assert(handle_ingame_kb_shortcuts(KEY_F3) == 1);
					assert_custom_camera(&test_camera_presets[0], 0);
					/* Re-selecting the current camera preserves the active preset. */
					camera_select_mode(CAMERA_MODE_CUSTOM);
					assert_custom_camera(&test_camera_presets[0], 0);
					camera_select_mode(shortcuts[shortcut].mode);
					assert_custom_camera(original, dashboard);
				}
			}
		}
	}
}

static void assert_supersight_status(const legacy_char *expected, legacy_u32 count)
{
	assert(supersight_status_count == count);
	assert(strcmp((const legacy_char *)supersight_status, expected) == 0);
}

static void test_display_shortcuts(void)
{
#ifdef RESTUNTS_SDL3
	const legacy_char *enabled_status = "Auto";
#else
	const legacy_char *enabled_status = "On";
#endif
	assert((legacy_u16)KEY_F11 == 0x8500U);
	assert((legacy_u16)KEY_F12 == 0x8600U);
	for (legacy_u8 mode = REPLAY_MODE_LIVE; mode <= REPLAY_MODE_PAUSED; mode++) {
		for (legacy_u16 camera = CAMERA_MODE_COCKPIT; camera < CAMERA_MODE_COUNT; camera++) {
			reset_inputs();
			game_replay_mode = mode;
			cameramode = camera;
			followOpponentFlag = 1;
			assert(supersight_enabled == 0 && fps_display_enabled == FRAME_FPS_DISPLAY_OFF);
			assert(handle_ingame_kb_shortcuts(KEY_F11) == 1);
			assert(fps_display_enabled == FRAME_FPS_DISPLAY_ON && supersight_enabled == 0);
			assert(fps_reset_count == 1);
			assert(supersight_status_count == 0);
			assert(full_redraw_frames_remaining == video_page_count);
			full_redraw_frames_remaining = 0;
			assert(handle_ingame_kb_shortcuts(KEY_F12) == 1);
			assert(fps_display_enabled == FRAME_FPS_DISPLAY_ON && supersight_enabled == 1);
			assert(supersight_reset_count == 1);
			assert_supersight_status(enabled_status, 1);
			assert(full_redraw_frames_remaining == video_page_count);
			assert(game_replay_mode == mode && cameramode == camera);
			assert(followOpponentFlag == 1);
			/* Camera changes and live/replay transitions retain the session toggles. */
			assert(handle_ingame_kb_shortcuts(KEY_F2) == 1);
			game_replay_mode = REPLAY_MODE_PLAYBACK;
			assert(fps_display_enabled == FRAME_FPS_DISPLAY_ON && supersight_enabled == 1);
			assert(handle_ingame_kb_shortcuts(KEY_F11) == 1);
			assert(fps_display_enabled == FRAME_FPS_DISPLAY_OFF && supersight_enabled == 1);
			assert(fps_reset_count == FRAME_FPS_DISPLAY_MODE_COUNT);
			assert(full_redraw_frames_remaining == video_page_count);
			assert_supersight_status(enabled_status, 1);
			assert(handle_ingame_kb_shortcuts(KEY_F12) == 1);
			assert(fps_display_enabled == FRAME_FPS_DISPLAY_OFF && supersight_enabled == 0);
			assert(supersight_reset_count == 2);
			assert_supersight_status("Off", 2);
			/* Both toggle directions request status while the FPS display is off. */
			assert(handle_ingame_kb_shortcuts(KEY_F12) == 1);
			assert(fps_display_enabled == FRAME_FPS_DISPLAY_OFF && supersight_enabled == 1);
			assert(supersight_reset_count == 3);
			assert_supersight_status(enabled_status, 3);
		}
	}
}

#ifdef RESTUNTS_SDL3
static legacy_u32 shift_f12_callbacks;
static legacy_u8 callback_interrupts_disabled;

void dos_interrupts_disable(void)
{
	assert(callback_interrupts_disabled == 0);
	callback_interrupts_disabled = 1;
}

void dos_interrupts_enable(void)
{
	assert(callback_interrupts_disabled != 0);
	callback_interrupts_disabled = 0;
}

static void shift_f12_callback(void)
{
	assert(callback_interrupts_disabled == 0);
	shift_f12_callbacks++;
}

static void test_shift_f12_callback(void)
{
	assert((legacy_u16)KEY_SHIFT_F12 == 0x8800U);
	assert(kb_parse_key(KEY_SHIFT_F12) == KEY_SHIFT_F12);
	kb_reg_callback(KEY_SHIFT_F12, shift_f12_callback);
	assert(kb_parse_key(KEY_SHIFT_F12) == 0 && shift_f12_callbacks == 1);
	assert(kb_parse_key(KEY_F12) == KEY_F12 && shift_f12_callbacks == 1);
	kb_remove_callback(KEY_SHIFT_F12);
	assert(kb_parse_key(KEY_SHIFT_F12) == KEY_SHIFT_F12 && shift_f12_callbacks == 1);
}

static void test_locked_display_shortcuts(void)
{
	static const struct {
		enum FRAME_ADAPTIVE_PRESET preset;
		legacy_s32 scale;
		const legacy_char *name;
	} presets[] = {{FRAME_ADAPTIVE_PRESET_FULL, FRAME_ADAPTIVE_FULL_SCALE, "Full"},
				   {FRAME_ADAPTIVE_PRESET_HIGH, FRAME_ADAPTIVE_FULL_SCALE, "High"},
				   {FRAME_ADAPTIVE_PRESET_MEDIUM, FRAME_ADAPTIVE_HALF_SCALE, "Medium"},
				   {FRAME_ADAPTIVE_PRESET_LOW, FRAME_ADAPTIVE_MINIMUM_SCALE, "Low"},
				   {FRAME_ADAPTIVE_PRESET_FULL, FRAME_ADAPTIVE_FULL_SCALE, "Full"}};
	for (legacy_u8 mode = REPLAY_MODE_LIVE; mode <= REPLAY_MODE_PAUSED; mode++) {
		for (legacy_u16 camera = CAMERA_MODE_COCKPIT; camera < CAMERA_MODE_COUNT; camera++) {
			for (legacy_u8 start_auto = 0; start_auto <= 1; start_auto++) {
				reset_inputs();
				game_replay_mode = mode;
				cameramode = camera;
				followOpponentFlag = 1;
				legacy_u32 status_count = 0;
				if (start_auto != 0) {
					assert(handle_ingame_kb_shortcuts(KEY_F12) == 1);
					assert(supersight_enabled == 1 &&
						   frame_adaptive.preset == FRAME_ADAPTIVE_PRESET_AUTO);
					assert_supersight_status("Auto", ++status_count);
					/* An already reduced auto stage still enters the first lock. */
					frame_adaptive.quality = FRAME_ADAPTIVE_SMALL_VIEW;
					test_hires_scale = FRAME_ADAPTIVE_MINIMUM_SCALE;
				}
				for (size_t preset = 0; preset < sizeof(presets) / sizeof(presets[0]); preset++) {
					full_redraw_frames_remaining = 0;
					assert(handle_ingame_kb_shortcuts(KEY_SHIFT_F12) == 1);
					assert(supersight_enabled == 1 && test_hires_enabled == 1);
					assert(frame_adaptive.preset == presets[preset].preset);
					assert(test_hires_scale == presets[preset].scale);
					assert(frame_adaptive_render_scale(&frame_adaptive) == presets[preset].scale);
					assert(full_redraw_frames_remaining == video_page_count);
					assert_supersight_status(presets[preset].name, ++status_count);
					/* Selecting locked modes never cycles the output mode, and their
					 * status is still requested with the FPS display switched off. */
					assert(hires_enabled_transitions == 1 && fps_display_enabled == 0);
					assert(game_replay_mode == mode && cameramode == camera &&
						   followOpponentFlag == 1);
				}
			}
		}
	}
	/* Plain F12 leaves every lock directly for Off, then returns to Auto. */
	for (size_t preset = 0; preset + 1 < sizeof(presets) / sizeof(presets[0]); preset++) {
		reset_inputs();
		for (size_t press = 0; press <= preset; press++) {
			assert(handle_ingame_kb_shortcuts(KEY_SHIFT_F12) == 1);
		}
		legacy_u32 status_count = supersight_status_count;
		assert(handle_ingame_kb_shortcuts(KEY_F12) == 1);
		assert(supersight_enabled == 0 && test_hires_enabled == 0);
		assert(test_hires_scale == HIRES_SCALE && hires_enabled_transitions == 2);
		assert_supersight_status("Off", ++status_count);
		assert(handle_ingame_kb_shortcuts(KEY_F12) == 1);
		assert(supersight_enabled == 1 && test_hires_enabled == 1);
		assert(frame_adaptive.preset == FRAME_ADAPTIVE_PRESET_AUTO);
		assert(frame_adaptive.quality == FRAME_ADAPTIVE_FULL_VIEW);
		assert(test_hires_scale == HIRES_SCALE && hires_enabled_transitions == 3);
		assert_supersight_status("Auto", ++status_count);
		assert(fps_display_enabled == 0);
	}
}
#endif

int main(void)
{
	legacy_u32 input_hash = input_fingerprint();
	legacy_u32 scrollbar_hash = scrollbar_fingerprint();
	legacy_u32 record_hash = record_fingerprint();
	legacy_u32 callback_hash = callback_fingerprint();
	legacy_u32 shortcut_hash = shortcut_fingerprint();
	test_event_priority();
	test_recording_input_modes();
	test_ghost_view_shortcut();
	test_display_shortcuts();
	test_custom_camera_cycle();
	test_custom_camera_switches();
#ifdef RESTUNTS_SDL3
	test_touch_shift_recording();
	test_controller_recording();
	test_controller_camera();
	test_raw_joystick_menu_release();
	test_shift_f12_callback();
	test_locked_display_shortcuts();
#endif
#ifdef INPUT_RECORD_BASELINE
	fprintf(stdout,
			"%08" LEGACY_PRIx32 " %08" LEGACY_PRIx32 " %08" LEGACY_PRIx32 " %08" LEGACY_PRIx32
			" %08" LEGACY_PRIx32 "\n",
			input_hash, scrollbar_hash, record_hash, callback_hash, shortcut_hash);
#else
#ifdef RESTUNTS_SDL3
	/* SDL samples independent menu devices and combines pedal sources. The
	 * additional device reads intentionally differ from the original DOS trace. */
	assert(input_hash == 0x2425c54cUL);
	assert(scrollbar_hash == 0xb5a5d357UL);
	assert(record_hash == 0x0942aa8eUL);
	assert(callback_hash == 0x61d0c8c0UL);
#else
	assert(input_hash == 0x2a5d4036UL);
	assert(scrollbar_hash == 0x207b3fe7UL);
	assert(record_hash == 0x41c4e48dUL);
	assert(callback_hash == 0x9bd7fd3eUL);
#endif
	/* New display shortcuts bypass the paused-race fallback. Classic keeps its
	 * original F11/F12 fingerprint; SDL additionally recognizes Shift+F12. */
#ifdef RESTUNTS_SDL3
	assert(shortcut_hash == 0xdecf17b2UL);
#else
	assert(shortcut_hash == 0xab8a7016UL);
#endif
#endif
	return 0;
}
