#include "externs.h"
#include "game_input.h"
#include "keyboard.h"
#include "memmgr.h"
#include "platform.h"
#include "replay_record.h"
#include "timing.h"
#include "crash_state.h"
#include "car_audio.h"
#ifdef RESTUNTS_SDL3
#include "../platform/sdl3/touch_game.h"
#include "../platform/sdl3/controller_game.h"
#endif

#define INPUT_STEERING_HISTORY_SIZE 64U
#define INPUT_STEERING_HISTORY_MASK 63U
#define AUDIO_REPLAY_MODE_UNINITIALIZED 255U
#define JOYSTICK_STEERING_TABLE_SIZE 34U
#define AUDIO_STATE_CHUNK_NAME_SIZE 12U
#define FRAME_CALLBACK_TIMER_TICKS 10UL
#define REPLAY_SLOW_CALLBACK_DIVISOR 2U
#define REPLAY_SECURITY_GRACE_SECONDS 4U
#define MOUSE_HORIZONTAL_CENTER 160
#define MOUSE_STEERING_DEAD_ZONE 18
#define MOUSE_LEFT_BUTTON_MASK 1U
#define MOUSE_RIGHT_BUTTON_MASK 2U
#define KEY_SCAN_GEAR_UP 30
#define KEY_SCAN_GEAR_DOWN 44
#define REPLAY_TOTAL_LIMIT_SECONDS 1500U
#define REPLAY_INPUT_BUFFER_FRAME_COUNT 12000U
#define REPLAY_HISTORY_SHIFT_SECONDS 30U
#define STEERING_RESPONSE_SPEED_SHIFT 10U
#define STEERING_RESPONSE_SPEED_MASK 252U
#define STEERING_RESPONSE_TABLE_OFFSET 1U
#define STEERING_RESPONSE_RECENTER_SHIFT 2U

static legacy_u8 input_steering_history[INPUT_STEERING_HISTORY_SIZE];
static legacy_u8 input_steering_history_valid[INPUT_STEERING_HISTORY_SIZE];
legacy_u16 frame_callback_count;
static legacy_u8 frame_callback_active;
legacy_s16 audio_car_state_read_index;
legacy_s16 audio_car_state_write_index;
legacy_s16 audio_car_state_interval;
struct AUDIO_CAR_STATE far *audio_car_state_records;
legacy_u8 audio_previous_replay_mode = AUDIO_REPLAY_MODE_UNINITIALIZED;
static const legacy_s8 far joystick_steering_table[JOYSTICK_STEERING_TABLE_SIZE] = {
	0,	0,	0,	0,	0,	0,	4,	8,	12, 16, 20, 24,	 28,  32,  36,	40,	 44,
	48, 52, 56, 60, 64, 68, 72, 76, 84, 90, 98, 106, 114, 121, 127, 127, 127};
static legacy_s8 input_steering_value;

void audio_allocate_car_state_records(void)
{
	static const legacy_s8 chunk_name[AUDIO_STATE_CHUNK_NAME_SIZE] = {'a', 'u', 'd', 'i', 'o', 's',
																	  't', 'a', 't', 'e', 0,   0};
	audio_car_state_records = (struct AUDIO_CAR_STATE far *)mmgr_alloc_resbytes(
		chunk_name, (legacy_s32)AUDIO_CAR_STATE_RECORD_COUNT * AUDIO_CAR_STATE_RECORD_SIZE);
	legacy_u8 far *bytes = (legacy_u8 far *)audio_car_state_records;
	for (legacy_u16 index = 0; index < AUDIO_CAR_STATE_RECORD_COUNT * AUDIO_CAR_STATE_RECORD_SIZE;
		 index++) {
		bytes[index] = 0;
	}
}

void set_frame_callback(void)
{
	frame_callback_count = 0;
	timer_reg_callback(&frame_callback);
	frame_callback_active = 0;
}

void remove_frame_callback(void)
{
	timer_wait_ticks(FRAME_CALLBACK_TIMER_TICKS);
	timer_remove_callback(&frame_callback);
}

static void frame_callback_play_audio(void)
{
	audio_car_state_interval = LEGACY_S16_WRAP_ADD(audio_car_state_interval, 1);
	if (audio_car_state_interval >= timer_ticks_per_frame &&
		audio_car_state_read_index != audio_car_state_write_index) {
		audio_apply_car_state_sample(
			(legacy_u8 far *)&audio_car_state_records[audio_car_state_read_index],
			audio_car_state_interval);
		audio_car_state_interval = 0;
		audio_car_state_read_index = LEGACY_S16_WRAP_ADD(audio_car_state_read_index, 1);
		if (audio_car_state_read_index == AUDIO_CAR_STATE_RECORD_COUNT) {
			audio_car_state_read_index = 0;
		}
	}
}

static void frame_callback_advance_recording(void)
{
	frame_callback_countdown = (legacy_u8)(frame_callback_countdown - 1U);
	if (frame_callback_countdown == 0) {
		frame_callback_countdown = (legacy_u8)timer_ticks_per_frame;
		frame_callback_count = LEGACY_U16_WRAP_ADD(frame_callback_count, 1U);
		if (game_replay_mode == REPLAY_MODE_PLAYBACK &&
			LEGACY_S8_FROM_BITS(replay_playback_speed) == REPLAY_PLAYBACK_SLOW) {
			slow_replay_countdown = (legacy_u8)(slow_replay_countdown - 1U);
			if (slow_replay_countdown == 0) {
				replay_update_input_tick(0);
				slow_replay_countdown = REPLAY_SLOW_CALLBACK_DIVISOR;
			}
		} else {
			if (game_replay_mode == REPLAY_MODE_PLAYBACK &&
				LEGACY_S8_FROM_BITS(replay_playback_speed) == REPLAY_PLAYBACK_FAST) {
				replay_update_input_tick(0);
			}
			replay_update_input_tick(0);
		}
	}
}

void frame_callback(void)
{
	if (dos_data_stack_segments_match() == 0 || frame_callback_active != 0) {
		return;
	}

	frame_callback_active = 1;
	frame_callback_play_audio();
	if (race_exit_request == 0 && recording_limit_warning_requested == 0 &&
		(is_in_replay == 0 || game_replay_mode != REPLAY_MODE_PLAYBACK)) {
		if (game_replay_mode == REPLAY_MODE_LIVE &&
			LEGACY_S16_FROM_BITS(state.game_frame_in_sec) >=
				LEGACY_S16_FROM_BITS(state.game_frames_per_sec)) {
			is_in_replay = 1;
			audio_carstate();
		} else {
			frame_callback_advance_recording();
		}
	}

	frame_callback_active--;
}

static legacy_s16 replay_read_mouse_input(void)
{
	dos_mouse_get_state(&mouse_butstate, &mouse_xpos, &mouse_ypos);
	legacy_s16 steering = LEGACY_S16_WRAP_SUB(mouse_xpos, MOUSE_HORIZONTAL_CENTER);
	if (steering > -MOUSE_STEERING_DEAD_ZONE && steering < MOUSE_STEERING_DEAD_ZONE) {
		steering = 0;
	} else if (steering > 0) {
		steering = LEGACY_S16_WRAP_SUB(steering, MOUSE_STEERING_DEAD_ZONE);
	} else {
		steering = LEGACY_S16_WRAP_ADD(steering, MOUSE_STEERING_DEAD_ZONE);
	}
	input_steering_value = LEGACY_S8_FROM_BITS(steering);
	legacy_s16 input_flags;
	if (((legacy_u16)mouse_butstate & MOUSE_LEFT_BUTTON_MASK) != 0) {
		input_flags = INPUT_BRAKE_FLAG;
	} else if (((legacy_u16)mouse_butstate & MOUSE_RIGHT_BUTTON_MASK) != 0) {
		input_flags = INPUT_ACCELERATE_FLAG;
	} else {
		input_flags = 0;
	}
	return input_flags;
}

static legacy_s16 replay_read_joystick_input(void)
{
#ifdef RESTUNTS_SDL3
	legacy_s16 axis = sdl3_input_driving_mode() == SDL3_DRIVING_CONTROLLER
						  ? sdl3_controller_scaled_x()
						  : joystick_get_scaled_x();
	legacy_s8 mapped_steering = LEGACY_S8_FROM_BITS(axis);
#else
	legacy_s8 mapped_steering = LEGACY_S8_FROM_BITS(joystick_get_scaled_x());
#endif
	input_steering_value = mapped_steering;
	if (mapped_steering > 0) {
		input_steering_value = joystick_steering_table[(legacy_u8)mapped_steering];
	} else if (mapped_steering < 0) {
		input_steering_value = LEGACY_S8_FROM_BITS(
			(legacy_u8)(0U -
						(legacy_u8)
							joystick_steering_table[(legacy_u8)(0U - (legacy_u8)mapped_steering)]));
	}
	legacy_s16 input_flags =
		(legacy_s16)((legacy_u16)get_kb_or_joy_flags() & INPUT_NON_STEERING_MASK);
	return input_flags;
}

static legacy_s16 replay_read_live_input(void)
{
	if (passed_security == 0 && race_start_sequence_state == RACE_START_SEQUENCE_INACTIVE &&
		(legacy_u16)state.game_frame >
			LEGACY_U16_WRAP_MUL(framespersec, REPLAY_SECURITY_GRACE_SECONDS)) {
		update_crash_state(CRASH_EVENT_COLLISION, PLAYER_CAR_INDEX);
	}

	legacy_s16 input_flags;
#ifdef RESTUNTS_SDL3
	enum SDL3_DRIVING_INPUT mode = sdl3_input_driving_mode();
	if (mode != SDL3_DRIVING_KEYBOARD) {
		if (mode == SDL3_DRIVING_MOUSE) {
#else
	if (mouse_driving_enabled != 0 || dos_joystick_is_enabled() != 0) {
		if (mouse_driving_enabled != 0) {
#endif
			input_flags = replay_read_mouse_input();
		} else {
			input_flags = replay_read_joystick_input();
		}
		legacy_u16 history_index = (legacy_u16)elapsed_time2 & INPUT_STEERING_HISTORY_MASK;
		input_steering_history[history_index] = (legacy_u8)input_steering_value;
		input_steering_history_valid[history_index] = 1;
	} else {
		input_flags = get_kb_or_joy_flags();
#ifdef RESTUNTS_SDL3
		input_steering_history_valid[(legacy_u16)elapsed_time2 & INPUT_STEERING_HISTORY_MASK] = 0;
#endif
	}

	if (kb_get_key_state(KEY_SCAN_GEAR_UP) != 0) {
		input_flags = (legacy_s16)((legacy_u16)input_flags | INPUT_SHIFT_UP_FLAG);
	}
	if (kb_get_key_state(KEY_SCAN_GEAR_DOWN) != 0) {
		input_flags = (legacy_s16)((legacy_u16)input_flags | INPUT_SHIFT_DOWN_FLAG);
	}
#ifdef RESTUNTS_SDL3
	/* Steering belongs to one device; pedals and gear requests remain independent. */
	input_flags |= get_kb_or_joy_flags() & INPUT_NON_STEERING_MASK;
	input_flags |= sdl3_controller_driving_flags() & INPUT_NON_STEERING_MASK;
	input_flags |= sdl3_touch_take_shift_flags();
#endif
	return input_flags;
}

static void replay_shift_recording_history(void)
{
	legacy_u16 recording_chunk = LEGACY_U16_WRAP_MUL(REPLAY_HISTORY_SHIFT_SECONDS, framespersec);
	legacy_s16 snapshot_count =
		LEGACY_S16_WRAP_SUB(LEGACY_S16_FROM_BITS(LEGACY_U16_DIV_OR_ZERO(
								REPLAY_INPUT_BUFFER_FRAME_COUNT, recording_chunk)),
							1);
	for (legacy_s16 snapshot_index = 0; snapshot_index < snapshot_count; snapshot_index++) {
		cvxptr[snapshot_index + 1].game_frame =
			LEGACY_S16_WRAP_SUB(cvxptr[snapshot_index + 1].game_frame, recording_chunk);
		fmemcpy(&cvxptr[snapshot_index], &cvxptr[snapshot_index + 1], sizeof(struct GAMESTATE));
	}
	for (legacy_u16 input_index = 0;
		 input_index < (legacy_u16)(REPLAY_INPUT_BUFFER_FRAME_COUNT - recording_chunk);
		 input_index++) {
		replay_input_buffer[input_index] = replay_input_buffer[input_index + recording_chunk];
	}
	elapsed_time2 = LEGACY_U16_WRAP_SUB(elapsed_time2, recording_chunk);
	gameconfig.game_recordedframes =
		LEGACY_U16_WRAP_SUB(gameconfig.game_recordedframes, recording_chunk);
	elapsed_time1 = LEGACY_U16_WRAP_ADD(elapsed_time1, recording_chunk);
	state.game_frame = LEGACY_S16_WRAP_SUB(state.game_frame, recording_chunk);
}

void replay_update_input_tick(legacy_s16 force_neutral_input)
{
	legacy_s16 input_flags;
	if (force_neutral_input != 0) {
		input_flags = 0;
	} else if (game_replay_mode == REPLAY_MODE_PLAYBACK) {
		if (gameconfig.game_recordedframes > elapsed_time2) {
			elapsed_time2++;
			return;
		}
		if (race_exit_request != 0) {
			return;
		}
		is_in_replay = 1;
		audio_carstate();
		race_exit_request = 1;
		return;
	} else if (race_exit_request == 0 && state.game_end_event == 0 &&
			   game_replay_mode != REPLAY_MODE_PAUSED) {
		input_flags = replay_read_live_input();
	} else {
		input_flags = 0;
	}

	legacy_u16 recording_limit = LEGACY_U16_WRAP_MUL(REPLAY_TOTAL_LIMIT_SECONDS, framespersec);
	legacy_u16 elapsed_total = LEGACY_U16_WRAP_ADD(elapsed_time2, elapsed_time1);
	if (recording_limit <= elapsed_total) {
		update_crash_state(CRASH_EVENT_EXIT, PLAYER_CAR_INDEX);
		race_exit_request = 1;
		return;
	}

	if (elapsed_time2 == REPLAY_INPUT_BUFFER_FRAME_COUNT) {
		if (elapsed_time1 == 0 && LEGACY_U16_LOW_BYTE(replay_overflow_acknowledged_word) == 0) {
			replay_overflow_acknowledged_word = LEGACY_S16_FROM_BITS(
				LEGACY_U16_REPLACE_LOW_BYTE(replay_overflow_acknowledged_word, 1U));
			recording_limit_warning_requested = 1;
			return;
		}

		replay_shift_recording_history();
	}

	replay_input_buffer[elapsed_time2] = (legacy_u8)input_flags;
	elapsed_time2++;
	gameconfig.game_recordedframes++;
}

void replay_apply_analog_steering_history(void)
{
	legacy_u16 frame = state.game_frame;
	legacy_u16 history_index = frame & INPUT_STEERING_HISTORY_MASK;
	if (input_steering_history_valid[history_index] == 0) {
		return;
	}

	legacy_s16 target_angle = LEGACY_S8_FROM_BITS(input_steering_history[history_index]);
	legacy_s16 steering_angle = state.playerstate.car_steeringAngle;
	legacy_u16 speed_index = (state.playerstate.car_actual_speed >> STEERING_RESPONSE_SPEED_SHIFT) &
							 STEERING_RESPONSE_SPEED_MASK;
	legacy_s8 *response_table = steerWhlRespTable_ptr;
	legacy_s16 response = response_table[speed_index + STEERING_RESPONSE_TABLE_OFFSET];
	if ((steering_angle < target_angle && steering_angle < -1) ||
		(steering_angle > target_angle && steering_angle > 1)) {
		response = LEGACY_S8_FROM_BITS(
			(legacy_u8)((legacy_u8)response << STEERING_RESPONSE_RECENTER_SHIFT));
	}

	legacy_s16 adjusted_angle;
	legacy_u8 action = 0;
	if (steering_angle > target_angle) {
		adjusted_angle = LEGACY_S16_WRAP_SUB(steering_angle, response);
		if (adjusted_angle >= target_angle) {
			action = INPUT_STEER_LEFT_FLAG;
		}
	} else if (steering_angle < target_angle) {
		adjusted_angle = LEGACY_S16_WRAP_ADD(steering_angle, response);
		if (adjusted_angle <= target_angle) {
			action = INPUT_STEER_RIGHT_FLAG;
		}
	}
	if (action != 0) {
		replay_input_buffer[frame] |= action;
	}
	input_steering_history_valid[history_index] = 0;
}
