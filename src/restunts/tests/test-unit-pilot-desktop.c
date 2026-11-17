#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_timer.h>
#include "../c/externs.h"
#include "../c/crash_state.h"
#include "../c/game_input.h"
#include "../c/owoot/owoot.h"
#include "../c/car/car_audio.h"
#include "../c/opponent/unit_opponent.h"
#include "../c/opponent/unit_opponent_internal.h"
#include "../c/opponent/unit_pilot.h"
#include "../c/opponent/unit_pilot_controls.h"
#include "../c/race/race_stats.h"
#include "../c/extended/extended_physics_snapshot.h"
#include "../c/extended/extended_race_stats.h"

#undef memcpy

#define PILOT_TEST_HASH_OFFSET 2166136261U
#define PILOT_TEST_HASH_PRIME 16777619U
#define PILOT_TEST_INPUT_COUNT 8U
#define PILOT_TEST_GSTATE_COUNT 11U
#define PILOT_TEST_WORK_LIMIT 128U
#define PILOT_TEST_PREPARE_LIMIT_MS 5000U
#define PILOT_TEST_SENTINEL 731U
#define PILOT_TEST_RECHECK_FRAME 70000U
#define PILOT_TEST_WAIT_TICKS 32U
#define PILOT_TEST_COMMAND_ERROR (-1)
#define PILOT_TEST_NATIVE_CHUNK 64U
#define PILOT_TEST_PREFIX_TICKS 2048U
#define PILOT_TEST_STEP_DELAY_MS 5U
#define PILOT_TEST_STEERING_SENTINEL 17

enum PILOT_TEST_CONTINUATION_SCENARIO {
	PILOT_TEST_HUMAN_CANCEL,
	PILOT_TEST_STATE_MISMATCH,
	PILOT_TEST_CONTINUE_ERROR,
	PILOT_TEST_CONTINUE_CRASH,
	PILOT_TEST_RECHECK_CONTINUE,
	PILOT_TEST_CONTINUE_SCENARIOS
};

static legacy_u8 pilot_test_active, pilot_test_leave_input_override, pilot_test_capture_entry;
static struct CARSTATE pilot_test_entry;
static legacy_s16 pilot_test_event;
static legacy_u8 pilot_test_slow_step;
static legacy_u32 pilot_test_steps, pilot_test_wait_steps, pilot_test_audio_calls;

legacy_s16 __real_unit_opponent_pilot_step(struct UNIT_PLANNER *planner, legacy_u32 frame,
										   legacy_s16 piece, legacy_f64 speed);
legacy_s16 __real_unit_opponent_pilot_wait_step(legacy_u32 frame);
void __real_audio_play_car_events(legacy_u8 flags, legacy_s16 channel);
void __real_audio_play_crash_and_stop_engine(legacy_s16 channel);
void __real_audio_carstate(void);

static legacy_s16 pilot_test_native_result(legacy_s16 result)
{
	if (!pilot_test_active) {
		return result;
	}
	assert(result >= 0);
	if (pilot_test_leave_input_override) {
		unit_opponent_pilot_input_override(INPUT_PEDAL_MASK | INPUT_STEERING_MASK,
										   INPUT_BRAKE_FLAG | INPUT_STEER_LEFT_FLAG);
	}
	if (pilot_test_event <= CRASH_EVENT_NONE) {
		return pilot_test_event ? pilot_test_event : result;
	}
	/* Exercise actual desktop crash/finish callbacks inside the trial,
	 * including both cars' result statistics and engine-audio suppression. */
	state.playerstate.car_crashBmpFlag = CRASH_EVENT_NONE;
	state.opponentstate.car_crashBmpFlag = CRASH_EVENT_NONE;
	update_crash_state(pilot_test_event, PLAYER_CAR_INDEX);
	update_crash_state(pilot_test_event, OPPONENT_CAR_INDEX);
	return state.opponentstate.car_crashBmpFlag;
}

legacy_s16 __wrap_unit_opponent_pilot_step(struct UNIT_PLANNER *planner, legacy_u32 frame,
										   legacy_s16 piece, legacy_f64 speed)
{
	if (pilot_test_active && pilot_test_capture_entry) {
		/* Capture the actual state after native waiting, before the first
		 * cached command. The live race remains inside the manager envelope. */
		pilot_test_entry = state.opponentstate;
		pilot_test_capture_entry = 0;
	}
	legacy_s16 result = __real_unit_opponent_pilot_step(planner, frame, piece, speed);
	if (pilot_test_active) {
		pilot_test_steps++;
		/* Force the native pump to yield after one step even on fast hosts.
		 * Prefix preparation must count completed ticks, not pump calls. */
		if (pilot_test_slow_step) {
			SDL_Delay(PILOT_TEST_STEP_DELAY_MS);
		}
	}
	return pilot_test_native_result(result);
}

legacy_s16 __wrap_unit_opponent_pilot_wait_step(legacy_u32 frame)
{
	legacy_s16 result = __real_unit_opponent_pilot_wait_step(frame);
	if (pilot_test_active) {
		pilot_test_wait_steps++;
	}
	return pilot_test_native_result(result);
}

void __wrap_audio_play_car_events(legacy_u8 flags, legacy_s16 channel)
{
	if (pilot_test_active) {
		pilot_test_audio_calls++;
	} else {
		__real_audio_play_car_events(flags, channel);
	}
}
void __wrap_audio_play_crash_and_stop_engine(legacy_s16 channel)
{
	if (pilot_test_active) {
		pilot_test_audio_calls++;
	} else {
		__real_audio_play_crash_and_stop_engine(channel);
	}
}
void __wrap_audio_carstate(void)
{
	if (pilot_test_active) {
		pilot_test_audio_calls++;
	} else {
		__real_audio_carstate();
	}
}

struct PILOT_TEST_OBSERVATION {
	struct EXTENDED_PHYSICS_SNAPSHOT physics;
	struct EXTENDED_REPLAY_CLOCKS statistics;
	struct GAMEINFO config;
	legacy_s32 gstate[PILOT_TEST_GSTATE_COUNT];
	const void *inputs, *commands, *history;
	legacy_u32 input_count, command_count, history_count;
	legacy_u32 replay_hash;
	legacy_u16 version, rules;
	legacy_u32 flags;
	legacy_u8 replay_mode;
	legacy_s8 in_replay, recording_flags;
	legacy_s8 audio_ready, player_audio_flags, opponent_audio_flags;
	legacy_s16 audio_read, audio_write;
};

static legacy_u32 pilot_test_hash(legacy_u32 hash, const void *data, size_t length)
{
	const legacy_u8 *bytes = data;
	for (size_t index = 0; index < length; index++) {
		hash = (hash ^ bytes[index]) * PILOT_TEST_HASH_PRIME;
	}
	return hash;
}
static void pilot_test_observe(struct PILOT_TEST_OBSERVATION *result)
{
	memset(result, 0, sizeof(*result));
	extended_physics_capture(&result->physics);
	result->statistics = extended_race_stats;
	result->config = gameconfig;
	result->gstate[0] = gState_frame;
	result->gstate[1] = gState_travDist;
	result->gstate[2] = gState_total_finish_time;
	result->gstate[3] = gState_opponent_finish_time;
	result->gstate[4] = gState_pEndFrame;
	result->gstate[5] = gState_oEndFrame;
	result->gstate[6] = gState_penalty;
	result->gstate[7] = gState_impactSpeed;
	result->gstate[8] = gState_topSpeed;
	result->gstate[9] = gState_jumpCount;
	result->gstate[10] = penalty_time;
	result->replay_mode = game_replay_mode;
	result->in_replay = is_in_replay;
	result->recording_flags = replay_recording_flags;
	result->audio_ready = audio_car_state_ready;
	result->player_audio_flags = audio_player_car_flags;
	result->opponent_audio_flags = audio_opponent_car_flags;
	result->audio_read = audio_car_state_read_index;
	result->audio_write = audio_car_state_write_index;
	/* Inspect ownership and opaque history through the existing lossless
	 * suspend/resume interface, without production-only debugging hooks. */
	struct EXTENDED_REPLAY_SUSPENDED saved;
	extended_replay_runtime_suspend(&saved);
	result->inputs = saved.recording.inputs;
	result->commands = saved.commands.data;
	result->history = saved.history.checkpoints;
	result->input_count = saved.recording.count;
	result->command_count = saved.commands.count;
	result->history_count = saved.history.count;
	result->version = saved.version;
	result->rules = saved.driving_rules;
	result->flags = saved.flags;
	legacy_u32 hash =
		pilot_test_hash(PILOT_TEST_HASH_OFFSET, saved.recording.inputs, saved.recording.count);
	hash = pilot_test_hash(hash, saved.commands.data,
						   (size_t)saved.commands.count * CSRPL_OPPONENT_COMMAND_SIZE);
	for (legacy_u32 index = 0; index < saved.history.count; index++) {
		hash = pilot_test_hash(hash, &saved.history.checkpoints[index].frame,
							   sizeof(saved.history.checkpoints[index].frame));
		hash =
			pilot_test_hash(hash, saved.history.checkpoints[index].state, saved.history.state_size);
	}
	result->replay_hash = hash;
	extended_replay_runtime_resume(&saved);
	/* Resume normally synchronizes the packed replay counters. This observer
	 * must retain even intentionally inconsistent live sentinel values. */
	extended_physics_restore(&result->physics);
	gameconfig = result->config;
}

static void pilot_test_assert_unchanged(const struct PILOT_TEST_OBSERVATION *expected)
{
	struct PILOT_TEST_OBSERVATION actual;
	pilot_test_observe(&actual);
	assert(memcmp(&actual, expected, sizeof(actual)) == 0);
	assert(pilot_test_audio_calls == 0);
}
static void pilot_test_recheck(struct UNIT_PLANNER *seed, const struct UNIT_ROUTE *route,
							   const struct PILOT_TEST_OBSERVATION *expected)
{
	/* Only the recheck scheduling marker is synthetic. These previews still
	 * execute real braking, steering, collisions, checkpoints and native clocks
	 * on the initialized desktop circuit; no manager or physics mocks apply. */
	legacy_u8 saved_stunt = route->points[0].stunt;
	route->points[0].stunt = UNIT_STUNT_PROTECTED;
	legacy_s16 piece = route->points[0].route_index;
	pilot_test_active = 1;
	pilot_test_event = CRASH_EVENT_NONE;
	pilot_test_steps = pilot_test_wait_steps = pilot_test_audio_calls = 0;
	assert(!unit_pilot_recheck_begin(NULL, PILOT_TEST_RECHECK_FRAME, piece));
	pilot_test_assert_unchanged(expected);
	assert(!unit_pilot_recheck_begin(seed, PILOT_TEST_RECHECK_FRAME, ROUTE_INDEX_NONE));
	pilot_test_assert_unchanged(expected);
	unit_pilot_stop();
	pilot_test_assert_unchanged(expected);

	static const legacy_s16 events[] = {CRASH_EVENT_NONE, PILOT_TEST_COMMAND_ERROR,
										CRASH_EVENT_COLLISION, CRASH_EVENT_FINISH};
	for (legacy_u16 scenario = 0; scenario < sizeof(events) / sizeof(events[0]); scenario++) {
		pilot_test_event = events[scenario];
		pilot_test_steps = pilot_test_wait_steps = pilot_test_audio_calls = 0;
		assert(unit_pilot_recheck_begin(seed, PILOT_TEST_RECHECK_FRAME, piece));
		pilot_test_assert_unchanged(expected);
		assert(unit_pilot_recheck_status(PILOT_TEST_RECHECK_FRAME) == 0);
		pilot_test_assert_unchanged(expected);
		legacy_u32 ticks = pilot_test_event == CRASH_EVENT_NONE ? PILOT_TEST_WAIT_TICKS + 1U : 1U;
		for (legacy_u32 tick = 0; tick < ticks; tick++) {
			unit_pilot_work(1U);
			pilot_test_assert_unchanged(expected);
		}
		struct UNIT_PILOT_STATS stats;
		unit_pilot_stats(&stats);
		assert(stats.native_ticks == ticks);
		if (pilot_test_event == CRASH_EVENT_NONE) {
			assert(pilot_test_wait_steps == PILOT_TEST_WAIT_TICKS && pilot_test_steps == 1U);
			assert(stats.active && !stats.failed_trials);
			/* Expiring an unconsumed handoff aborts only the private preview. */
			assert(unit_pilot_recheck_status(PILOT_TEST_RECHECK_FRAME + ticks) == -1);
		} else {
			assert(pilot_test_wait_steps == 1U && pilot_test_steps == 0);
			assert(stats.failed_trials == 1U && stats.last_failure == pilot_test_event);
			assert(unit_pilot_recheck_status(PILOT_TEST_RECHECK_FRAME) == -1);
		}
		pilot_test_assert_unchanged(expected);
		unit_pilot_stop();
		pilot_test_assert_unchanged(expected);
	}
	pilot_test_active = 0;
	route->points[0].stunt = saved_stunt;
}

static void pilot_test_assert_no_input_override(void)
{
	struct CSRPL_OPPONENT_COMMAND command = {0};
	command.input = INPUT_ACCELERATE_FLAG;
	command.steering = PILOT_TEST_STEERING_SENTINEL;
	struct CSRPL_OPPONENT_COMMAND expected = command;
	unit_pilot_controls_apply(&state.opponentstate, 1U, 0, &command);
	assert(memcmp(&command, &expected, sizeof(command)) == 0);
}

static void pilot_test_prepare_prefix(struct UNIT_PLANNER *seed,
									  const struct PILOT_TEST_OBSERVATION *expected)
{
	pilot_test_event = CRASH_EVENT_NONE;
	pilot_test_steps = pilot_test_wait_steps = pilot_test_audio_calls = 0;
	assert(unit_pilot_begin(seed));
	pilot_test_assert_unchanged(expected);
	struct UNIT_PILOT_STATS stats = {0};
	while (stats.native_ticks < PILOT_TEST_PREFIX_TICKS && !stats.accepted) {
		legacy_u32 remaining = PILOT_TEST_PREFIX_TICKS - stats.native_ticks;
		unit_pilot_work(remaining < PILOT_TEST_NATIVE_CHUNK ? remaining : PILOT_TEST_NATIVE_CHUNK);
		unit_pilot_stats(&stats);
		pilot_test_assert_unchanged(expected);
		assert(stats.active && !stats.completed_lap);
	}
	assert(stats.accepted && !stats.failed_trials);
	assert(unit_pilot_finish());
	assert(unit_pilot_continuation_pending());
	pilot_test_assert_unchanged(expected);
	pilot_test_assert_no_input_override();
}

static void pilot_test_prepare_rechecked_prefix(struct UNIT_PLANNER *seed, legacy_s16 piece,
												const struct PILOT_TEST_OBSERVATION *expected)
{
	pilot_test_event = CRASH_EVENT_NONE;
	pilot_test_steps = pilot_test_wait_steps = pilot_test_audio_calls = 0;
	pilot_test_capture_entry = 1;
	assert(unit_pilot_recheck_begin(seed, PILOT_TEST_RECHECK_FRAME, piece));
	pilot_test_assert_unchanged(expected);
	struct UNIT_PILOT_STATS stats = {0};
	while (stats.native_ticks < PILOT_TEST_PREFIX_TICKS && !stats.accepted) {
		legacy_u32 remaining = PILOT_TEST_PREFIX_TICKS - stats.native_ticks;
		unit_pilot_work(remaining < PILOT_TEST_NATIVE_CHUNK ? remaining : PILOT_TEST_NATIVE_CHUNK);
		unit_pilot_stats(&stats);
		pilot_test_assert_unchanged(expected);
		assert(!stats.exhausted && !stats.completed_lap);
	}
	assert(stats.accepted && !stats.failed_trials && !pilot_test_capture_entry);
	assert(pilot_test_wait_steps == PILOT_TEST_WAIT_TICKS);
	assert(unit_pilot_recheck_status(PILOT_TEST_RECHECK_FRAME) == 0);
	assert(unit_pilot_recheck_status(PILOT_TEST_RECHECK_FRAME + PILOT_TEST_WAIT_TICKS) == 1);
	/* Recheck acceptance alone cannot advance speculative native work. The
	 * controller must first consume its exact future handoff command. */
	assert(!unit_pilot_continuation_pending());
	legacy_u32 before = pilot_test_steps;
	(void)unit_pilot_continue_work(PILOT_TEST_NATIVE_CHUNK);
	assert(pilot_test_steps == before);
	pilot_test_assert_unchanged(expected);
	pilot_test_assert_no_input_override();
}

static void pilot_test_continuation(struct UNIT_PLANNER *seed, const struct UNIT_ROUTE *route,
									const struct PILOT_TEST_OBSERVATION *expected)
{
	legacy_u8 saved_stunt = route->points[0].stunt;
	route->points[0].stunt = UNIT_STUNT_PROTECTED;
	pilot_test_active = 1;
	static const legacy_s16 events[PILOT_TEST_CONTINUE_SCENARIOS] = {
		CRASH_EVENT_NONE, CRASH_EVENT_NONE, PILOT_TEST_COMMAND_ERROR, CRASH_EVENT_COLLISION,
		CRASH_EVENT_NONE};
	for (legacy_u16 scenario = 0; scenario < sizeof(events) / sizeof(events[0]); scenario++) {
		legacy_u32 frame = expected->physics.clocks.frame;
		struct CARSTATE first = state.opponentstate;
		if (scenario == PILOT_TEST_RECHECK_CONTINUE) {
			pilot_test_prepare_rechecked_prefix(seed, route->points[0].route_index, expected);
			first = pilot_test_entry;
			frame = PILOT_TEST_RECHECK_FRAME + PILOT_TEST_WAIT_TICKS;
		} else {
			pilot_test_slow_step = scenario == PILOT_TEST_HUMAN_CANCEL;
			pilot_test_prepare_prefix(seed, expected);
			pilot_test_slow_step = 0;
		}
		struct CSRPL_OPPONENT_COMMAND command;
		assert(unit_pilot_command(&first, frame, &command));
		assert(unit_pilot_continuation_pending());
		struct CARSTATE next;
		assert(unit_pilot_future_car(1U, &next));
		pilot_test_assert_unchanged(expected);
		legacy_u32 before = pilot_test_steps;
		pilot_test_event = events[scenario];
		pilot_test_leave_input_override = 1;
		(void)unit_pilot_continue_work(1U);
		pilot_test_leave_input_override = 0;
		assert(pilot_test_steps == before + 1U);
		pilot_test_assert_unchanged(expected);
		pilot_test_assert_no_input_override();
		/* Each native chunk consumes its exact-state match. A repeated pump
		 * without a newly accepted live command cannot advance the preview. */
		before = pilot_test_steps;
		(void)unit_pilot_continue_work(PILOT_TEST_NATIVE_CHUNK);
		assert(pilot_test_steps == before);
		pilot_test_assert_unchanged(expected);
		if (pilot_test_event == CRASH_EVENT_NONE) {
			assert(unit_pilot_continuation_pending());
			assert(unit_pilot_command(&next, frame + 1U, &command));
			(void)unit_pilot_continue_work(PILOT_TEST_INPUT_COUNT);
			assert(pilot_test_steps > before &&
				   pilot_test_steps <= before + PILOT_TEST_INPUT_COUNT);
			pilot_test_assert_unchanged(expected);
			pilot_test_assert_no_input_override();
			if (scenario == PILOT_TEST_STATE_MISMATCH) {
				/* A changed car pose cannot authorize another native chunk. */
				assert(unit_pilot_future_car(1U, &next));
				next.car_position.lx++;
				assert(!unit_pilot_command(&next, frame + 2U, &command));
				assert(!unit_pilot_continuation_pending());
				before = pilot_test_steps;
				(void)unit_pilot_continue_work(PILOT_TEST_NATIVE_CHUNK);
				assert(pilot_test_steps == before);
				pilot_test_assert_unchanged(expected);
			}
		}
		/* Human avoidance cancels the remaining cached path. Cancellation
		 * must preserve the changed human pose and every other live field. */
		state.playerstate.car_position = state.opponentstate.car_position;
		state.playerstate.car_previous_position = state.playerstate.car_position;
		struct PILOT_TEST_OBSERVATION nearby;
		pilot_test_observe(&nearby);
		unit_opponent_pilot_input_override(INPUT_PEDAL_MASK | INPUT_STEERING_MASK,
										   INPUT_BRAKE_FLAG | INPUT_STEER_LEFT_FLAG);
		if (scenario == PILOT_TEST_HUMAN_CANCEL || scenario == PILOT_TEST_RECHECK_CONTINUE) {
			assert(unit_pilot_continuation_pending());
		}
		unit_pilot_continuation_cancel();
		assert(!unit_pilot_continuation_pending());
		pilot_test_assert_unchanged(&nearby);
		pilot_test_assert_no_input_override();
		before = pilot_test_steps;
		assert(!unit_pilot_continue_work(PILOT_TEST_NATIVE_CHUNK));
		assert(pilot_test_steps == before && !unit_pilot_future_car(1U, &next));
		pilot_test_assert_unchanged(&nearby);
		extended_physics_restore(&expected->physics);
		pilot_test_assert_unchanged(expected);
		unit_pilot_stop();
		pilot_test_assert_unchanged(expected);
	}
	route->points[0].stunt = saved_stunt;
	pilot_test_active = 0;
}

void test_unit_pilot_desktop(void)
{
	assert((legacy_u8)gameconfig.game_opponenttype == CSRPL_UNIT_OPPONENT_TYPE);
	assert(extended_replay_is_active());
	assert(unit_opponent_prepare_begin());
	/* Threaded preparation publishes asynchronously; polling a fixed number
	 * of times can finish before a startup worker receives CPU time. */
	legacy_u64 started = SDL_GetTicks();
	while (!unit_opponent_pilot_route() && SDL_GetTicks() - started < PILOT_TEST_PREPARE_LIMIT_MS) {
		unit_opponent_prepare_work();
		SDL_Delay(1U);
	}
	const struct UNIT_ROUTE *route = unit_opponent_pilot_route();
	if (!route) {
		struct UNIT_PLANNER_STATS stats;
		unit_opponent_planner_stats(&stats);
		fprintf(stderr,
				"Desktop pilot route preparation timed out: workers=%u opening=%u route=%u\n",
				stats.workers, stats.opening_ready, stats.route_ready);
	}
	assert(route);
	struct UNIT_PLANNER *seed =
		unit_planner_create(route->points, route->count, &route->limits, owoot_enabled != 0, -1);
	assert(seed);
	unit_planner_work(seed, PILOT_TEST_WORK_LIMIT);
	unit_planner_finish_startup(seed);
	for (legacy_u32 frame = 0; frame < PILOT_TEST_INPUT_COUNT; frame++) {
		assert(extended_replay_record_input(INPUT_BRAKE_FLAG));
		struct CSRPL_OPPONENT_COMMAND command = {0};
		command.input = INPUT_BRAKE_FLAG;
		assert(extended_replay_record_opponent_command(frame, &command));
	}
	extended_replay_before_step(1);
	/* Distinct live values make accidental result copying and replay-clock
	 * synchronization observable, even before the actual race has started. */
	game_replay_mode = REPLAY_MODE_LIVE;
	is_in_replay = 0;
	replay_recording_flags = 0;
	gameconfig.game_recordedframes = PILOT_TEST_SENTINEL;
	gState_frame = PILOT_TEST_SENTINEL;
	gState_travDist = PILOT_TEST_SENTINEL + 1;
	gState_total_finish_time = PILOT_TEST_SENTINEL + 2;
	gState_opponent_finish_time = PILOT_TEST_SENTINEL + 3;
	gState_pEndFrame = PILOT_TEST_SENTINEL + 4;
	gState_oEndFrame = PILOT_TEST_SENTINEL + 5;
	gState_penalty = PILOT_TEST_SENTINEL + 6;
	gState_impactSpeed = PILOT_TEST_SENTINEL + 7;
	gState_topSpeed = PILOT_TEST_SENTINEL + 8;
	gState_jumpCount = PILOT_TEST_SENTINEL + 9;
	memset(&extended_race_stats, 0, sizeof(extended_race_stats));
	extended_race_stats.frame = PILOT_TEST_SENTINEL;
	extended_race_stats.opponent_finish = PILOT_TEST_SENTINEL + 1;
	legacy_s8 saved_audio_ready = audio_car_state_ready;
	audio_car_state_ready = 1;
	struct PILOT_TEST_OBSERVATION expected;
	pilot_test_observe(&expected);
	assert(expected.input_count == PILOT_TEST_INPUT_COUNT);
	assert(expected.command_count == PILOT_TEST_INPUT_COUNT && expected.history_count);
	static const legacy_s16 events[] = {CRASH_EVENT_NONE, CRASH_EVENT_COLLISION,
										CRASH_EVENT_FINISH};
	for (legacy_u16 scenario = 0; scenario < sizeof(events) / sizeof(events[0]); scenario++) {
		pilot_test_event = events[scenario];
		pilot_test_steps = pilot_test_audio_calls = 0;
		pilot_test_active = 1;
		assert(unit_pilot_begin(seed));
		pilot_test_assert_unchanged(&expected);
		for (legacy_u32 work = 0; work < PILOT_TEST_INPUT_COUNT; work++) {
			unit_pilot_work(1);
			pilot_test_assert_unchanged(&expected);
		}
		assert(pilot_test_steps > 0 && pilot_test_steps <= PILOT_TEST_INPUT_COUNT);
		struct UNIT_PILOT_STATS stats;
		unit_pilot_stats(&stats);
		assert(stats.native_ticks == pilot_test_steps);
		if (pilot_test_event == CRASH_EVENT_NONE) {
			assert(pilot_test_steps == PILOT_TEST_INPUT_COUNT && stats.active);
		} else if (pilot_test_event == CRASH_EVENT_FINISH) {
			assert(stats.completed_lap && !stats.active);
		} else {
			assert(stats.last_failure == CRASH_EVENT_COLLISION && !stats.active);
		}
		(void)unit_pilot_finish();
		pilot_test_assert_unchanged(&expected);
		unit_pilot_stop();
		pilot_test_assert_unchanged(&expected);
		pilot_test_active = 0;
	}
	pilot_test_recheck(seed, route, &expected);
	pilot_test_continuation(seed, route, &expected);
	audio_car_state_ready = saved_audio_ready;
	unit_planner_destroy(seed);
	unit_opponent_stop();
	puts("Desktop UNIT pilot/recheck/continuation chunks preserve physics, replay history, audio "
		 "and "
		 "results.");
}
