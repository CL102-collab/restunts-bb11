#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../c/extended_replay_runtime.h"
#include "../c/externs.h"
#undef memcpy

struct GAMESTATE state;
struct GAMEINFO gameconfig;
legacy_u16 elapsed_time1, elapsed_time2, framespersec = 20U;
legacy_u8 race_exit_request;
static uint32_t random_seed;

void get_kevinrandom_seed(legacy_s8 *seed)
{
	memset(seed, 0, GAMESTATE_RANDOM_SEED_SIZE);
	memcpy(seed, &random_seed, sizeof(random_seed));
}

void init_kevinrandom(const legacy_s8 *seed)
{
	memcpy(&random_seed, seed, sizeof(random_seed));
}

void init_game_state(legacy_s16 mode)
{
	memset(&state, 0, sizeof(state));
	random_seed = 17U;
	extended_replay_reset_simulation(mode == GAMESTATE_INIT_RESET_CHECKPOINTS);
}

void update_gamestate(void)
{
	extended_replay_before_step(1);
	uint8_t input = extended_replay_input_at(extended_replay_frame());
	random_seed = random_seed * 1664525U + 1013904223U;
	state.playerstate.car_position.lx =
		(legacy_s32)((uint32_t)state.playerstate.car_position.lx + input + (random_seed & 31U));
	extended_replay_after_step();
	if (extended_replay_frame() == 66000U) {
		extended_replay_clocks.penalty = 80000U;
		extended_replay_clocks.player_finish = 146000U;
	}
}

int main(void)
{
	extended_replay_runtime_begin();
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
	assert(extended_replay_is_active());
	for (uint32_t frame = 0U; frame < 100003U; frame++) {
		assert(extended_replay_record_input((uint8_t)(frame & 63U)));
	}
	assert(extended_replay_recorded_frames() == 100003U);
	assert(extended_replay_input_frame() == 100003U);
	assert(extended_replay_inputs()[65536U] == 0U);
	while (extended_replay_frame() < extended_replay_input_frame()) {
		update_gamestate();
	}
	assert(extended_replay_frame() == 100003U);
	uint32_t final_seed = random_seed;
	legacy_s32 final_position = state.playerstate.car_position.lx;
	assert(extended_replay_clocks.player_finish == 146000U);
	assert(extended_replay_seek(65999U));
	assert(extended_replay_clocks.penalty == 0U);
	assert(extended_replay_seek(100003U));
	assert(random_seed == final_seed && state.playerstate.car_position.lx == final_position);
	assert(extended_replay_clocks.player_finish == 146000U);
	assert(extended_replay_clocks.penalty == 80000U);
	assert(!extended_replay_seek(100004U));
	assert(extended_replay_seek(70000U));
	assert(extended_replay_branch(70000U));
	assert(extended_replay_recorded_frames() == 70000U);
	assert(extended_replay_record_input(17U));
	assert(extended_replay_inputs()[70000U] == 17U);
	assert(extended_replay_replace_input(70000U, 23U));
	assert(extended_replay_input_at(70000U) == 23U);
	assert(!extended_replay_replace_input(70001U, 23U));
	assert(extended_replay_seek(70001U));
	struct GAMESTATE saved_state = state;
	struct EXTENDED_REPLAY_SUSPENDED saved;
	extended_replay_runtime_suspend(&saved);
	assert(!extended_replay_is_active());
	uint8_t ghost_inputs[3] = {1U, 2U, 3U};
	struct CSRPL_VIEW ghost;
	memset(&ghost, 0, sizeof(ghost));
	ghost.inputs = ghost_inputs;
	ghost.metadata.frame_count = 3U;
	assert(extended_replay_runtime_load(&ghost));
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
	assert(extended_replay_seek(3U));
	assert(extended_replay_recorded_frames() == 3U);
	state = saved_state;
	extended_replay_runtime_resume(&saved);
	assert(extended_replay_is_active());
	assert(extended_replay_frame() == 70001U);
	assert(extended_replay_recorded_frames() == 70001U);
	assert(extended_replay_inputs()[70000U] == 23U);
	assert(extended_replay_seek(0U));
	assert(extended_replay_frame() == 0U && extended_replay_clocks.penalty == 0U);
	extended_replay_runtime_clear();
	extended_replay_runtime_clear();
	assert(!extended_replay_is_active());
	state.game_frame = 123;
	elapsed_time2 = 125U;
	gameconfig.game_recordedframes = 126U;
	assert(extended_replay_frame() == 123U);
	assert(extended_replay_input_frame() == 125U);
	assert(extended_replay_recorded_frames() == 126U);
	return 0;
}
