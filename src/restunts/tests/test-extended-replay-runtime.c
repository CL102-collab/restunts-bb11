#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../c/extended/extended_replay_runtime.h"
#include "../c/extended/extended_replay_ui.h"
#include "../c/ui/ui_text.h"
#include "../c/externs.h"
#undef memcpy
#undef strcmp

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
#define TEST_TIME_BUFFER_CAPACITY 32U
#define TEST_TIME_LOW_FRAME_RATE 10U
#define TEST_TIME_HIGH_FRAME_RATE 20U
#define TEST_CHECKPOINT_EVENT_FRAME 66000U
#define TEST_CHECKPOINT_OBJECT_INDEX 37U
#define TEST_CHECKPOINT_VISITED_BYTE (TEST_CHECKPOINT_OBJECT_INDEX / 8U)
#define TEST_CHECKPOINT_VISITED_MASK (1U << (TEST_CHECKPOINT_OBJECT_INDEX % 8U))

static void test_replay_time_formatting(void)
{
	static const legacy_u16 frame_rates[] = {TEST_TIME_LOW_FRAME_RATE, TEST_TIME_HIGH_FRAME_RATE};
	static const struct {
		legacy_u16 seconds;
		legacy_u16 fractional_frame;
		const legacy_char *expected[2];
	} cases[] = {{0U, 0U, {" 0:00.00", " 0:00.00"}},
				 {316U, 9U, {" 5:16.40", " 5:16.45"}},
				 {599U, 19U, {" 9:59.90", " 9:59.95"}},
				 {600U, 0U, {"10:00.00", "10:00.00"}}};
	legacy_s8 extended_text[TEST_TIME_BUFFER_CAPACITY] = "untouched";
	assert(!extended_replay_ui_format_time(extended_text, 0U, 1));
	assert(strcmp((const legacy_char *)extended_text, "untouched") == 0);
	assert(!extended_replay_ui_format_current_time(extended_text));
	assert(strcmp((const legacy_char *)extended_text, "untouched") == 0);

	extended_replay_runtime_begin();
	for (legacy_u16 rate = 0U; rate < sizeof(frame_rates) / sizeof(frame_rates[0]); rate++) {
		framespersec = frame_rates[rate];
		for (legacy_u16 index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
			legacy_u32 frame =
				cases[index].seconds * (legacy_u32)framespersec +
				cases[index].fractional_frame * framespersec / TEST_TIME_HIGH_FRAME_RATE;
			for (legacy_s16 fraction = 0; fraction <= 1; fraction++) {
				legacy_s8 classic_text[TEST_TIME_BUFFER_CAPACITY];
				format_frame_as_string(classic_text, LEGACY_S16_FROM_BITS((legacy_u16)frame),
									   fraction);
				assert(extended_replay_ui_format_time(extended_text, frame, fraction));
				assert(strcmp((const legacy_char *)extended_text,
							  (const legacy_char *)classic_text) == 0);
				if (fraction != 0) {
					assert(strcmp((const legacy_char *)extended_text,
								  cases[index].expected[rate]) == 0);
					extended_replay_clocks.frame = frame;
					assert(extended_replay_ui_format_current_time(extended_text));
					assert(strcmp((const legacy_char *)extended_text,
								  (const legacy_char *)classic_text) == 0);
				}

				/* Other callers retain the shared runtime formatter's unpadded strings. */
				legacy_s8 runtime_text[TEST_TIME_BUFFER_CAPACITY];
				extended_replay_format_runtime_time(runtime_text, frame, fraction);
				const legacy_s8 *unpadded = classic_text + (classic_text[0] == ' ' ? 1U : 0U);
				assert(strcmp((const legacy_char *)runtime_text, (const legacy_char *)unpadded) ==
					   0);
			}
		}
	}

	static const struct {
		legacy_u32 frame;
		legacy_u16 frame_rate;
		const legacy_char *whole_seconds;
		const legacy_char *hundredths;
	} long_cases[] = {{72005U, TEST_TIME_HIGH_FRAME_RATE, "60:00", "60:00.25"},
					  {60000U, TEST_TIME_LOW_FRAME_RATE, "100:00", "100:00.00"},
					  {7200000U, TEST_TIME_HIGH_FRAME_RATE, "6000:00", "6000:00.00"},
					  {LEGACY_U32_MAX, TEST_TIME_LOW_FRAME_RATE, "7158278:49", "7158278:49.50"},
					  {LEGACY_U32_MAX, TEST_TIME_HIGH_FRAME_RATE, "3579139:24", "3579139:24.75"}};
	for (legacy_u16 index = 0U; index < sizeof(long_cases) / sizeof(long_cases[0]); index++) {
		framespersec = long_cases[index].frame_rate;
		for (legacy_s16 fraction = 0; fraction <= 1; fraction++) {
			const legacy_char *expected =
				fraction != 0 ? long_cases[index].hundredths : long_cases[index].whole_seconds;
			assert(
				extended_replay_ui_format_time(extended_text, long_cases[index].frame, fraction));
			assert(strcmp((const legacy_char *)extended_text, expected) == 0);
			legacy_s8 runtime_text[TEST_TIME_BUFFER_CAPACITY];
			extended_replay_format_runtime_time(runtime_text, long_cases[index].frame, fraction);
			assert(strcmp((const legacy_char *)runtime_text, expected) == 0);
		}
	}
	framespersec = TEST_TIME_HIGH_FRAME_RATE;
	extended_replay_runtime_clear();
}

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
	if (extended_replay_frame() == TEST_CHECKPOINT_EVENT_FRAME) {
		extended_replay_clocks.checkpoints.initialized = 1;
		extended_replay_clocks.checkpoints.visited[PLAYER_CAR_INDEX][TEST_CHECKPOINT_VISITED_BYTE] =
			TEST_CHECKPOINT_VISITED_MASK;
		extended_replay_clocks.checkpoints.remaining[PLAYER_CAR_INDEX][0] = 1U;
		extended_replay_clocks.checkpoints.previous[PLAYER_CAR_INDEX] =
			state.playerstate.car_position;
		extended_replay_clocks.penalty = 80000U;
		extended_replay_clocks.player_finish = 146000U;
	}
}

static void test_recording_versions(void)
{
	static const legacy_u16 versions[] = {CSRPL_MIN_VERSION, CSRPL_VERSION,
										  TEST_FUTURE_REPLAY_VERSION, LEGACY_U16_MAX};
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
	test_replay_time_formatting();
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
	struct EXTENDED_CHECKPOINT_PROGRESS final_checkpoints = extended_replay_clocks.checkpoints;
	assert(final_checkpoints.initialized);
	assert(final_checkpoints.visited[PLAYER_CAR_INDEX][TEST_CHECKPOINT_VISITED_BYTE] ==
		   TEST_CHECKPOINT_VISITED_MASK);
	assert(extended_replay_seek(65999U));
	assert(!extended_replay_clocks.checkpoints.initialized);
	assert(extended_replay_clocks.checkpoints
			   .visited[PLAYER_CAR_INDEX][TEST_CHECKPOINT_VISITED_BYTE] == 0U);
	assert(extended_replay_clocks.penalty == 0U);
	assert(extended_replay_seek(100003U));
	assert(random_seed == final_seed && state.playerstate.car_position.lx == final_position);
	assert(memcmp(&extended_replay_clocks.checkpoints, &final_checkpoints,
				  sizeof(final_checkpoints)) == 0);
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
	struct EXTENDED_CHECKPOINT_PROGRESS suspended_checkpoints = extended_replay_clocks.checkpoints;
	extended_replay_runtime_suspend(&saved);
	assert(!extended_replay_is_active());
	assert(!extended_replay_clocks.checkpoints.initialized);
	assert(memcmp(&saved.clocks.checkpoints, &suspended_checkpoints,
				  sizeof(suspended_checkpoints)) == 0);
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
	assert(memcmp(&extended_replay_clocks.checkpoints, &suspended_checkpoints,
				  sizeof(suspended_checkpoints)) == 0);
	assert(extended_replay_version() == CSRPL_VERSION);
	assert(extended_replay_driving_rules() == DRIVING_RULE_KNOWN_MASK);
	assert(extended_replay_recorded_frames() == 70001U);
	assert(extended_replay_inputs()[70000U] == 23U);
	assert(extended_replay_seek(0U));
	assert(extended_replay_frame() == 0U && extended_replay_clocks.penalty == 0U);
	assert(!extended_replay_clocks.checkpoints.initialized);
	assert(extended_replay_clocks.checkpoints
			   .visited[PLAYER_CAR_INDEX][TEST_CHECKPOINT_VISITED_BYTE] == 0U);
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
