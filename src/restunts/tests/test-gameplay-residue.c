#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/game_input.h"
#include "../c/car/car_speed.h"
#include "../c/crash_state.h"
#include "../c/physics_internal.h"
#include "../c/state_internal.h"
#include "../c/residue.h"
#include "../c/track/track_objects.h"

#undef memcpy

static struct GAMESTATE checkpoints[GAMESTATE_CHECKPOINT_COUNT];
static legacy_s8 inputs[256];
static legacy_s8 last_input;
static legacy_u32 player_steps;
static legacy_u32 audio_updates;

void audio_carstate(void)
{
	audio_updates++;
}

void update_car_speed(legacy_s8 input, legacy_s16 car_index, struct CARSTATE *carstate,
					  struct SIMD *simd)
{
	assert(car_index == PLAYER_CAR_INDEX);
	assert(carstate == &state.playerstate && simd == &simd_player);
	last_input = input;
}

void update_player_steering_input(legacy_s8 input)
{
	(void)input;
}

void update_player_state(struct CARSTATE *player, struct SIMD *player_simd,
						 struct CARSTATE *opponent, struct SIMD *opponent_simd,
						 legacy_s16 car_index)
{
	assert(player == &state.playerstate && player_simd == &simd_player);
	assert(opponent == &state.opponentstate && opponent_simd == &simd_opponent);
	assert(car_index == PLAYER_CAR_INDEX);
	/* An aborted wall scan leaves the fourth contact distance from update_grip.
	 * Exercise the real suspension consumer with that retained stack word. */
	update_wheel_suspension(player, legacy_execution_residue.grip_stack_words[3], 3);
	player_steps++;
}

void update_opponent_tick(void)
{
	assert(0);
}

void update_crash_particles(void)
{
	assert(0);
}

void update_crash_state(legacy_s16 event, legacy_s16 car_index)
{
	(void)event;
	(void)car_index;
	assert(0);
}

legacy_s16 get_track_route_point(legacy_s16 route, struct VECTOR *target, legacy_s16 point,
								 legacy_s8 *speed)
{
	(void)route;
	(void)target;
	(void)point;
	(void)speed;
	assert(0);
	return 0;
}

void get_kevinrandom_seed(legacy_s8 *seed)
{
	memset(seed, 23, GAMESTATE_RANDOM_SEED_SIZE);
}

void far *__fmemcpy(void far *destination, const void far *source, legacy_u16 count)
{
	return memcpy(destination, source, count);
}

static void prepare_tick(legacy_s16 frame)
{
	memset(&state, 0, sizeof(state));
	memset(&gameconfig, 0, sizeof(gameconfig));
	memset(&simd_player, 0, sizeof(simd_player));
	memset(inputs, 0, sizeof(inputs));
	state.game_frame = frame;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	state.playerstate.car_suspension_deflection[3] = 256;
	/* Keep route guidance outside the track; it is independent of this handoff. */
	state.playerstate.car_position.lx = -65536L;
	state.playerstate.car_route_index = ROUTE_INDEX_NONE;
	checkpoint_frame_interval = 20;
	framespersec = GAME_FRAME_RATE_NORMAL;
	trackside_camera_count = 0;
	replay_input_buffer = inputs;
	cvxptr = checkpoints;
	game_replay_mode = REPLAY_MODE_LIVE;
	race_start_sequence_state = RACE_START_SEQUENCE_INACTIVE;
	player_steps = 0;
	last_input = -1;
}

static void assert_suspension(legacy_s16 expected)
{
	assert(player_steps == 1);
	assert(state.playerstate.car_suspension_deflection[3] == expected);
}

static void test_caller_context_and_default(void)
{
	prepare_tick(169);
	update_gamestate_with_legacy_si(5);
	assert_suspension(261);
	prepare_tick(169);
	update_gamestate_with_legacy_si(0);
	assert_suspension(128);
	prepare_tick(169);
	update_gamestate_with_legacy_si(80);
	assert_suspension(336);
	prepare_tick(169);
	update_gamestate_with_legacy_si(5);
	assert_suspension(261);
	prepare_tick(169);
	update_gamestate();
	assert_suspension(336);
	prepare_tick(169);
	update_player_tick(INPUT_NONE);
	assert_suspension(336);
}

static void test_checkpoint_context_is_local(void)
{
	prepare_tick(160);
	update_gamestate_with_legacy_si(5);
	assert_suspension(264);
	assert(checkpoints[8].game_frame == 160);
	assert(checkpoints[8].playerstate.car_suspension_deflection[3] == 256);
	assert(state.game_frame == 161);
	state.playerstate.car_suspension_deflection[3] = 256;
	player_steps = 0;
	update_gamestate_with_legacy_si(5);
	assert_suspension(261);
	prepare_tick(0);
	update_gamestate_with_legacy_si(80);
	assert_suspension(128);
	prepare_tick(169);
	checkpoint_frame_interval = 0;
	update_gamestate_with_legacy_si(5);
	assert_suspension(128);
	assert(checkpoints[0].game_frame == 169);
	prepare_tick(169);
	update_player_tick(INPUT_NONE);
	assert_suspension(336);
}

static void test_start_sequence_uses_start_line_distance(void)
{
	static const legacy_s8 expected_inputs[] = {INPUT_BRAKE_FLAG, INPUT_ACCELERATE_FLAG,
												INPUT_NONE};

	static const legacy_u16 speeds[] = {1, 1, 1280};
	static const legacy_s16 distances[] = {5, 256, 256};
	for (legacy_u32 index = 0; index < 3; index++) {
		prepare_tick(160);
		state.game_inputmode = GAME_INPUT_MODE_WAITING;
		state.playerstate.car_rev_speed = speeds[index];
		game_replay_mode = REPLAY_MODE_PAUSED;
		race_start_sequence_state = RACE_START_SEQUENCE_AUTO_DRIVE;
		start_flag_animation = 450;
		start_finish_row = 0;
		start_finish_column = 0;
		track_row_centers[0] = distances[index];
		track_column_centers[0] = 0;
		track_angle = 0;
		update_gamestate_with_legacy_si(5);
		assert_suspension(index == 0 ? 261 : 384);
		assert(last_input == expected_inputs[index]);
	}
}

static void test_silent_playback_preserves_physics(void)
{
	prepare_tick(169);
	audio_updates = 0;
	update_gamestate();
	struct GAMESTATE expected = state;
	assert(audio_updates == 1);
	prepare_tick(169);
	audio_updates = 0;
	update_gamestate_silent();
	assert(audio_updates == 0);
	assert(memcmp(&state, &expected, sizeof(state)) == 0);
	prepare_tick(160);
	cvxptr = 0;
	audio_updates = 0;
	update_gamestate_silent();
	assert(audio_updates == 0);
	assert_suspension(264);
	/* Long low-rate source recordings can outlast the live checkpoint array. */
	prepare_tick(11999);
	cvxptr = 0;
	update_gamestate_silent();
	assert(state.game_frame == 12000);
}

int main(void)
{
	test_silent_playback_preserves_physics();
	test_caller_context_and_default();
	test_checkpoint_context_is_local();
	test_start_sequence_uses_start_line_distance();
	puts("Gameplay caller residue and suspension handoff passed.");
	return 0;
}
