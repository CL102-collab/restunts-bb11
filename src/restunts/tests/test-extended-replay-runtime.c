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
static legacy_u16 configured_driving_rules;

legacy_u16 driving_rules_current(void)
{
	return configured_driving_rules;
}

#define TEST_FUTURE_REPLAY_VERSION 101U

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

static void test_recording_versions(void)
{
	static const legacy_u16 versions[] = {CSRPL_VERSION, TEST_FUTURE_REPLAY_VERSION,
										  LEGACY_U16_MAX};
	legacy_u8 inputs[] = {1U, 2U, 3U};
	for (size_t index = 0; index < sizeof(versions) / sizeof(versions[0]); index++) {
		for (legacy_u16 rules = 0; rules <= DRIVING_RULE_KNOWN_MASK; rules++) {
			configured_driving_rules = rules ^ DRIVING_RULE_KNOWN_MASK;
			struct CSRPL_VIEW replay = {0};
			replay.metadata.version = versions[index];
			replay.metadata.driving_rules = rules;
			replay.metadata.frame_count = sizeof(inputs);
			replay.inputs = inputs;
			assert(extended_replay_runtime_load(&replay));
			assert(extended_replay_version() == versions[index]);
			assert(extended_replay_driving_rules() == rules);
			assert(driving_rules_current() == (rules ^ DRIVING_RULE_KNOWN_MASK));
			init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
			assert(extended_replay_seek(1U));
			assert(extended_replay_branch(1U));
			assert(extended_replay_record_input(4U));
			assert(extended_replay_version() == versions[index]);
			assert(extended_replay_driving_rules() == rules);

			const legacy_u8 *recorded = extended_replay_inputs();
			struct EXTENDED_REPLAY_CLOCKS clocks = extended_replay_clocks;
			assert(!extended_replay_runtime_load(NULL));
			for (legacy_u16 old_version = 0; old_version < CSRPL_MIN_VERSION; old_version++) {
				replay.metadata.version = old_version;
				assert(!extended_replay_runtime_load(&replay));
				assert(extended_replay_version() == versions[index]);
				assert(extended_replay_driving_rules() == rules);
				assert(extended_replay_inputs() == recorded);
				assert(extended_replay_recorded_frames() == 2U);
				assert(memcmp(&extended_replay_clocks, &clocks, sizeof(clocks)) == 0);
			}
			replay.metadata.version = CSRPL_VERSION;
			replay.metadata.driving_rules = DRIVING_RULE_KNOWN_MASK + 1U;
			assert(!extended_replay_runtime_load(&replay));
			replay.metadata.driving_rules = rules;
			replay.inputs = NULL;
			assert(!extended_replay_runtime_load(&replay));
			assert(extended_replay_version() == versions[index]);
			assert(extended_replay_driving_rules() == rules);
			assert(extended_replay_inputs() == recorded);
			assert(extended_replay_recorded_frames() == 2U);
			assert(memcmp(&extended_replay_clocks, &clocks, sizeof(clocks)) == 0);

			struct EXTENDED_REPLAY_SUSPENDED saved;
			extended_replay_runtime_suspend(&saved);
			assert(saved.version == versions[index]);
			assert(saved.driving_rules == rules);
			assert(extended_replay_version() == 0U);
			assert(extended_replay_driving_rules() == 0U);
			extended_replay_runtime_begin();
			assert(extended_replay_version() == CSRPL_VERSION);
			assert(extended_replay_driving_rules() == driving_rules_current());
			extended_replay_runtime_resume(&saved);
			assert(saved.version == 0U && saved.driving_rules == 0U);
			assert(extended_replay_version() == versions[index]);
			assert(extended_replay_driving_rules() == rules);
			assert(extended_replay_inputs() == recorded);
			assert(extended_replay_recorded_frames() == 2U);
			assert(memcmp(&extended_replay_clocks, &clocks, sizeof(clocks)) == 0);
			assert(driving_rules_current() == (rules ^ DRIVING_RULE_KNOWN_MASK));
			extended_replay_runtime_begin();
			assert(extended_replay_version() == CSRPL_VERSION);
			assert(extended_replay_driving_rules() == driving_rules_current());
			extended_replay_runtime_clear();
			assert(extended_replay_version() == 0U);
			assert(extended_replay_driving_rules() == 0U);
		}
	}
}

int main(void)
{
	test_recording_versions();
	configured_driving_rules = DRIVING_RULE_KNOWN_MASK;
	extended_replay_runtime_begin();
	assert(extended_replay_version() == CSRPL_VERSION);
	assert(extended_replay_driving_rules() == DRIVING_RULE_KNOWN_MASK);
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
	ghost.metadata.version = TEST_FUTURE_REPLAY_VERSION;
	ghost.metadata.driving_rules = 0;
	ghost.metadata.frame_count = 3U;
	assert(extended_replay_runtime_load(&ghost));
	assert(extended_replay_version() == TEST_FUTURE_REPLAY_VERSION);
	assert(extended_replay_driving_rules() == 0U);
	assert(driving_rules_current() == DRIVING_RULE_KNOWN_MASK);
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
	assert(extended_replay_seek(3U));
	assert(extended_replay_recorded_frames() == 3U);
	state = saved_state;
	extended_replay_runtime_resume(&saved);
	assert(extended_replay_is_active());
	assert(extended_replay_frame() == 70001U);
	assert(extended_replay_version() == CSRPL_VERSION);
	assert(extended_replay_driving_rules() == DRIVING_RULE_KNOWN_MASK);
	assert(extended_replay_recorded_frames() == 70001U);
	assert(extended_replay_inputs()[70000U] == 23U);
	assert(extended_replay_seek(0U));
	assert(extended_replay_frame() == 0U && extended_replay_clocks.penalty == 0U);
	extended_replay_runtime_clear();
	extended_replay_runtime_clear();
	assert(!extended_replay_is_active());
	assert(extended_replay_version() == 0U);
	assert(extended_replay_driving_rules() == 0U);
	state.game_frame = 123;
	elapsed_time2 = 125U;
	gameconfig.game_recordedframes = 126U;
	assert(extended_replay_frame() == 123U);
	assert(extended_replay_input_frame() == 125U);
	assert(extended_replay_recorded_frames() == 126U);
	return 0;
}
