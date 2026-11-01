/* Exercise live rewind with the real interrupt-driven replay recorder. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../c/race/race.c"
#include "../c/race/race_rewind.c"
#include "../c/race/race_presentation.c"
#include "../c/replay/replay_record.c"
#undef memcpy

static legacy_s8 recorded_inputs[12000], original_inputs[12000];
static struct GAMESTATE checkpoints[21];
static legacy_s16 held_q, held_control, live_input, interrupts_disabled;
static legacy_u32 pending_ticks;
static legacy_u32 timer_reads, audio_updates, restores, simulation_updates;
static legacy_u32 renderer_resets;

void frame_renderer_reset(void)
{
	renderer_resets++;
}
static legacy_u16 event_frame;
static legacy_s8 event_kind;

legacy_s16 kb_get_key_state(legacy_s16 scan_code)
{
	if (scan_code == 0x1D) {
		return held_control;
	}
	return scan_code == RACE_REWIND_SCAN_CODE ? held_q : 0;
}
legacy_u32 timer_get_delta_alt(void)
{
	legacy_u32 elapsed = pending_ticks;
	pending_ticks = 0;
	timer_reads++;
	return elapsed;
}
void dos_interrupts_disable(void)
{
	assert(interrupts_disabled == 0);
	interrupts_disabled = 1;
}
void dos_interrupts_enable(void)
{
	assert(interrupts_disabled == 1);
	assert(elapsed_time2 == (legacy_u16)state.game_frame);
	assert(is_in_replay == (game_replay_mode == REPLAY_MODE_PLAYBACK));
	interrupts_disabled = 0;
}
void audio_carstate(void)
{
	assert(interrupts_disabled == 0);
	audio_updates++;
}
legacy_s16 dos_data_stack_segments_match(void)
{
	return 1;
}
void audio_apply_car_state_sample(const legacy_u8 far *sample, legacy_s16 interval)
{
	(void)sample;
	(void)interval;
	assert(0);
}
legacy_u8 dos_joystick_is_enabled(void)
{
	return 0;
}
legacy_s16 joystick_get_scaled_x(void)
{
	assert(0);
	return 0;
}
void dos_mouse_get_state(legacy_s16 *buttons, legacy_s16 *x, legacy_s16 *y)
{
	(void)buttons;
	(void)x;
	(void)y;
	assert(0);
}
legacy_s16 get_kb_or_joy_flags(void)
{
	assert(game_replay_mode == REPLAY_MODE_LIVE);
	return live_input;
}
void update_crash_state(legacy_s16 event, legacy_s16 car)
{
	(void)event;
	(void)car;
	assert(0);
}
void far *__fmemcpy(void far *destination, const void far *source, legacy_u16 count)
{
	return memcpy(destination, source, count);
}

/* Deterministic stand-in physics makes replayed input, both cars, penalties,
 * and end-event timing observable independently of the rewind calculation. */
static void simulate_frame(struct GAMESTATE *game)
{
	legacy_u16 input = (legacy_u8)recorded_inputs[game->game_frame];
	game->playerstate.car_position.lx += 3 + input;
	game->opponentstate.car_position.lz += 9 + (input & 3);
	game->game_penalty += input == INPUT_BRAKE_FLAG;
	game->game_frame++;
	if (event_frame != 0 && (legacy_u16)game->game_frame >= event_frame) {
		game->game_end_event = event_kind;
		game->playerstate.car_crashBmpFlag = event_kind;
		game->game_frame_in_sec = game->game_frame - event_frame;
		if (game->game_frame_in_sec > game->game_frames_per_sec) {
			game->game_frame_in_sec = game->game_frames_per_sec;
		}
	}
}
void update_gamestate(void)
{
	assert(interrupts_disabled == 0);
	simulate_frame(&state);
	simulation_updates++;
}
void restore_gamestate(legacy_u16 target)
{
	assert(game_replay_mode == REPLAY_MODE_PLAYBACK);
	assert(is_in_replay == 1);
	state = checkpoints[target / 600];
	elapsed_time2 = state.game_frame;
	restores++;
	assert(renderer_resets == restores);
}
static void assert_reconstructed(legacy_u16 target)
{
	struct GAMESTATE expected = checkpoints[0];
	while ((legacy_u16)expected.game_frame != target) {
		simulate_frame(&expected);
	}
	assert(memcmp(&state, &expected, sizeof(state)) == 0);
	assert(elapsed_time2 == target);
}
static void reset_race(legacy_u16 frame, legacy_u16 end_frame, legacy_s8 end_event)
{
	memset(&state, 0, sizeof(state));
	memset(&gameconfig, 0, sizeof(gameconfig));
	memset(input_steering_history_valid, 0, sizeof(input_steering_history_valid));
	held_q = 1;
	held_control = 0;
	live_input = INPUT_ACCELERATE_FLAG;
	interrupts_disabled = 0;
	pending_ticks = 987654;
	timer_reads = audio_updates = restores = simulation_updates = renderer_resets = 0;
	event_frame = end_frame;
	event_kind = end_event;
	for (legacy_u32 i = 0; i < sizeof(recorded_inputs); i++) {
		recorded_inputs[i] = i % 7 == 0 ? INPUT_BRAKE_FLAG : INPUT_ACCELERATE_FLAG;
	}
	memcpy(original_inputs, recorded_inputs, sizeof(recorded_inputs));
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	state.game_frames_per_sec = 40;
	checkpoints[0] = state;
	for (legacy_u32 i = 0; i < 11999; i++) {
		simulate_frame(&state);
		if (state.game_frame % 600 == 0) {
			checkpoints[state.game_frame / 600] = state;
		}
	}
	state = checkpoints[frame / 600];
	while ((legacy_u16)state.game_frame != frame) {
		simulate_frame(&state);
	}
	replay_input_buffer = recorded_inputs;
	cvxptr = checkpoints;
	gameconfig.game_recordedframes = frame + 2;
	elapsed_time2 = frame + 2;
	elapsed_time1 = 0;
	game_replay_mode = REPLAY_MODE_LIVE;
	is_in_replay = 0;
	idle_expired = race_exit_request = recording_limit_warning_requested = 0;
	mouse_driving_enabled = 0;
	race_start_sequence_state = RACE_START_SEQUENCE_INACTIVE;
	passed_security = 1;
	replay_recording_flags = REPLAY_RECORDING_ACTIVE_FLAG;
	replay_playback_speed = REPLAY_PLAYBACK_FAST;
	framespersec = 20;
	timer_ticks_per_frame = 5;
	frame_callback_countdown = 1;
	frame_callback_count = frame_callback_active = 0;
	audio_car_state_read_index = audio_car_state_write_index = audio_car_state_interval = 0;
}
static void hold_for(struct RACE_REWIND_STATE *rewind, legacy_u32 ticks)
{
	pending_ticks = ticks;
	race_update_rewind(rewind);
}
static void release_q(struct RACE_REWIND_STATE *rewind)
{
	held_q = 0;
	race_update_rewind(rewind);
	assert(rewind->active == 0);
	assert(game_replay_mode == REPLAY_MODE_LIVE && is_in_replay == 0);
	assert(gameconfig.game_recordedframes == (legacy_u16)state.game_frame);
	assert(elapsed_time2 == (legacy_u16)state.game_frame);
	assert(replay_recording_flags ==
		   (REPLAY_RECORDING_ACTIVE_FLAG | REPLAY_RECORDING_MODIFIED_FLAG));
	assert(replay_playback_speed == REPLAY_PLAYBACK_NORMAL);
	assert(frame_callback_countdown == timer_ticks_per_frame);
}
static void test_freeze_exact_seek_and_new_branch(void)
{
	struct RACE_REWIND_STATE rewind = {0};
	reset_race(1800, 0, CRASH_EVENT_NONE);
	race_update_rewind(&rewind);
	assert(rewind.active == 1 && rewind.origin_frame == 1800);
	assert(state.game_frame == 1800 && elapsed_time2 == 1800);
	assert(game_replay_mode == REPLAY_MODE_PLAYBACK && is_in_replay == 1);
	assert(gameconfig.game_recordedframes == 1802);
	assert(timer_reads == 1 && restores == 0 && simulation_updates == 0);
	assert(audio_updates == 1);
	for (legacy_u32 i = 0; i < 30; i++) {
		frame_callback();
	}
	assert(elapsed_time2 == 1800 && gameconfig.game_recordedframes == 1802);
	assert(memcmp(recorded_inputs, original_inputs, sizeof(recorded_inputs)) == 0);
	hold_for(&rewind, 6);
	assert(state.game_frame == 1800);
	hold_for(&rewind, 1);
	assert_reconstructed(1799);
	assert(restores != 0 && simulation_updates != 0);
	hold_for(&rewind, 13);
	assert_reconstructed(1797);
	assert(memcmp(recorded_inputs, original_inputs, sizeof(recorded_inputs)) == 0);
	release_q(&rewind);
	assert(audio_updates == 2 && race_exit_request == 0);
	live_input = INPUT_BRAKE_FLAG;
	for (legacy_u32 i = 0; i < 4; i++) {
		frame_callback();
	}
	assert(elapsed_time2 == 1797 && gameconfig.game_recordedframes == 1797);
	frame_callback();
	assert(elapsed_time2 == 1798 && gameconfig.game_recordedframes == 1798);
	assert(recorded_inputs[1797] == INPUT_BRAKE_FLAG);
	assert(memcmp(recorded_inputs, original_inputs, 1797) == 0);
	update_gamestate();
	held_q = 1;
	race_update_rewind(&rewind);
	assert(rewind.origin_frame == 1798);
	hold_for(&rewind, 20);
	assert_reconstructed(1795);
	release_q(&rewind);
}
static void test_acceleration_and_start_saturation(void)
{
	struct RACE_REWIND_STATE rewind = {0};
	reset_race(5000, 0, CRASH_EVENT_NONE);
	race_update_rewind(&rewind);
	hold_for(&rewind, 999);
	assert_reconstructed(4851);
	hold_for(&rewind, 2);
	assert_reconstructed(4850);
	hold_for(&rewind, 10);
	assert_reconstructed(4847);
	hold_for(&rewind, UINT32_MAX);
	assert_reconstructed(0);
	hold_for(&rewind, UINT32_MAX);
	assert_reconstructed(0);
	release_q(&rewind);
	held_q = 1;
	race_update_rewind(&rewind);
	hold_for(&rewind, UINT32_MAX);
	assert_reconstructed(0);
	release_q(&rewind);
}
static void test_recording_limit_warning(void)
{
	for (legacy_u32 move = 0; move < 2; move++) {
		struct RACE_REWIND_STATE rewind = {0};
		reset_race(11999, 0, CRASH_EVENT_NONE);
		simulate_frame(&state);
		elapsed_time2 = gameconfig.game_recordedframes = 12000;
		recording_limit_warning_requested = 1;
		replay_overflow_acknowledged_word = 0x5501;
		race_update_rewind(&rewind);
		if (move != 0) {
			hold_for(&rewind, 20);
			assert_reconstructed(11997);
		}
		release_q(&rewind);
		assert(recording_limit_warning_requested == (move == 0));
		assert(replay_overflow_acknowledged_word == (move == 0 ? 0x5501 : 0x5500));
	}
}
static void test_rewind_gates(void)
{
	for (legacy_u32 which = 0; which < 9; which++) {
		struct RACE_REWIND_STATE rewind = {0};
		reset_race(1800, 0, CRASH_EVENT_NONE);
		switch (which) {
			case 0:
				held_q = 0;
				break;
			case 1:
				game_replay_mode = REPLAY_MODE_PAUSED;
				break;
			case 2:
				game_replay_mode = REPLAY_MODE_PLAYBACK;
				break;
			case 3:
				idle_expired = 1;
				break;
			case 4:
				state.game_inputmode = GAME_INPUT_MODE_WAITING;
				break;
			case 5:
				race_exit_request = REPLAY_EXIT_REQUESTED;
				break;
			case 6:
				state.game_end_event = CRASH_EVENT_EXIT;
				break;
			case 7:
				race_exit_request = 1;
				break;
			case 8:
				held_control = 1;
				break;
		}
		race_update_rewind(&rewind);
		assert(rewind.active == 0 && state.game_frame == 1800);
		assert(timer_reads == 0 && audio_updates == 0 && restores == 0);
		assert(gameconfig.game_recordedframes == 1802 && elapsed_time2 == 1802);
	}
}
static void test_control_stops_rewind(void)
{
	struct RACE_REWIND_STATE rewind = {0};
	reset_race(1800, 0, CRASH_EVENT_NONE);
	race_update_rewind(&rewind);
	hold_for(&rewind, 20);
	assert_reconstructed(1797);

	held_control = 1;
	hold_for(&rewind, 20);
	assert(rewind.active == 0);
	assert(game_replay_mode == REPLAY_MODE_LIVE && is_in_replay == 0);
	assert_reconstructed(1797);
	assert(gameconfig.game_recordedframes == 1797);
	assert(timer_reads == 2 && restores == 1 && audio_updates == 2);

	hold_for(&rewind, 20);
	assert(rewind.active == 0);
	assert_reconstructed(1797);
	assert(timer_reads == 2 && restores == 1 && audio_updates == 2);

	held_control = 0;
	race_update_rewind(&rewind);
	assert(rewind.active == 1);
	hold_for(&rewind, 20);
	assert_reconstructed(1794);
	release_q(&rewind);
}
static void test_crash_and_finish_lifecycle(void)
{
	static const legacy_s8 events[] = {CRASH_EVENT_COLLISION, CRASH_EVENT_WATER,
									   CRASH_EVENT_FINISH};
	for (legacy_u32 i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
		struct RACE_REWIND_STATE rewind = {0};
		reset_race(1800, 1700, events[i]);
		race_exit_request = 1;
		race_update_rewind(&rewind);
		assert(rewind.active == 1 && race_exit_request == 0);
		hold_for(&rewind, 20);
		assert_reconstructed(1797);
		release_q(&rewind);
		assert(state.game_end_event == events[i]);
		assert(state.playerstate.car_crashBmpFlag == events[i]);
		assert(state.game_frame_in_sec == 40 && race_exit_request == 1);

		reset_race(1730, 1700, events[i]);
		race_update_rewind(&rewind);
		hold_for(&rewind, 20);
		assert_reconstructed(1727);
		release_q(&rewind);
		assert(state.game_end_event == events[i]);
		assert(state.game_frame_in_sec == 27 && race_exit_request == 0);

		held_q = 1;
		race_update_rewind(&rewind);
		hold_for(&rewind, 200);
		assert_reconstructed(1697);
		release_q(&rewind);
		assert(state.game_end_event == CRASH_EVENT_NONE);
		assert(state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE);
		assert(race_exit_request == 0 && state.game_frame_in_sec == 0);
	}
}
int main(void)
{
	test_freeze_exact_seek_and_new_branch();
	test_acceleration_and_start_saturation();
	test_recording_limit_warning();
	test_rewind_gates();
	test_control_stops_rewind();
	test_crash_and_finish_lifecycle();
	return 0;
}
