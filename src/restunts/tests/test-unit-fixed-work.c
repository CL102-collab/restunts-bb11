#include "test-unit-pilot-fixture.h"
#include "../c/opponent/unit_pilot_internal.h"
#include <stdio.h>

#if !RESTUNTS_UNIT_TEST_FIXED_WORK
#error This regression requires fixed native work quotas.
#endif

#define TEST_FIXED_PUMP_TICKS 64U
#define TEST_FIXED_CONTINUATION_TICKS 128U
#define TEST_FIXED_REQUEST_TICKS 1000U
#define TEST_FIXED_CLOCK_STEP_NS 1000000000ULL
#define TEST_FIXED_FIRST_FRAME 100U
#define TEST_FIXED_FAILURE_TICK 5U

static struct UNIT_PLANNER source;

static void test_native_quotas_and_exact_horizons(void)
{
	reset_fixture(0);
	/* Every clock read exceeds the interactive deadlines. Explicit work must
	 * still certify its full bounded quota without touching the live race. */
	clock_step = TEST_FIXED_CLOCK_STEP_NS;
	struct GAMESTATE before = state;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(TEST_FIXED_REQUEST_TICKS);
	struct UNIT_PILOT_STATS stats;
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == TEST_FIXED_PUMP_TICKS && stats.active);
	assert(unit_pilot_state->committed_steps == TEST_FIXED_PUMP_TICKS);
	assert_live_unchanged(&before);
	assert(unit_pilot_finish() && unit_pilot_continuation_pending());
	assert(unit_pilot_continue_work(TEST_FIXED_REQUEST_TICKS));
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == TEST_FIXED_PUMP_TICKS);
	struct CSRPL_OPPONENT_COMMAND command;
	assert(unit_pilot_command(&state.opponentstate, TEST_FIXED_FIRST_FRAME, &command));
	assert(command.route_point == 0);
	struct CARSTATE future;
	assert(unit_pilot_future_car(TEST_FIXED_PUMP_TICKS - 1U, &future));
	assert(!unit_pilot_future_car(TEST_FIXED_PUMP_TICKS, &future));
	assert(unit_pilot_continue_work(TEST_FIXED_REQUEST_TICKS));
	legacy_u32 total = TEST_FIXED_PUMP_TICKS + TEST_FIXED_CONTINUATION_TICKS;
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == total && stats.matched_commands == 1U);
	assert(unit_pilot_state->committed_steps == total);
	assert(unit_pilot_future_car(total - 1U, &future));
	assert((legacy_u16)future.car_reserved_route_word1 == total - 1U);
	assert(!unit_pilot_future_car(total, &future));
	assert_live_unchanged(&before);
	/* Work is armed by a newly matched race frame, never by repeated calls. */
	assert(unit_pilot_continue_work(TEST_FIXED_REQUEST_TICKS));
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == total);
	assert(unit_pilot_future_car(1U, &state.opponentstate));
	before = state;
	assert(unit_pilot_command(&state.opponentstate, TEST_FIXED_FIRST_FRAME + 1U, &command));
	assert(command.route_point == 1U && unit_pilot_continue_work(1U));
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == total + 1U && stats.matched_commands == 2U);
	assert_live_unchanged(&before);
	unit_pilot_stop();
}

static void test_native_failure_still_limits_admission(void)
{
	reset_fixture(0);
	clock_step = TEST_FIXED_CLOCK_STEP_NS;
	fail_ordinary_at = TEST_FIXED_FAILURE_TICK;
	struct GAMESTATE before = state;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(TEST_FIXED_REQUEST_TICKS);
	struct UNIT_PILOT_STATS stats;
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == TEST_FIXED_FAILURE_TICK);
	assert(stats.exhausted && !stats.active && stats.failed_trials == 1U);
	assert(unit_pilot_state->committed_steps == TEST_FIXED_FAILURE_TICK - 1U);
	assert_live_unchanged(&before);
	assert(unit_pilot_finish() && !unit_pilot_continuation_pending());
	struct CSRPL_OPPONENT_COMMAND command;
	assert(unit_pilot_command(&state.opponentstate, TEST_FIXED_FIRST_FRAME, &command));
	struct CARSTATE future;
	assert(unit_pilot_future_car(TEST_FIXED_FAILURE_TICK - 2U, &future));
	assert(!unit_pilot_future_car(TEST_FIXED_FAILURE_TICK - 1U, &future));
	assert(!unit_pilot_continue_work(TEST_FIXED_REQUEST_TICKS));
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == TEST_FIXED_FAILURE_TICK);
	assert_live_unchanged(&before);
	unit_pilot_stop();
}

legacy_int main(void)
{
	test_native_quotas_and_exact_horizons();
	test_native_failure_still_limits_admission();
	puts("UNIT fixed native work tests passed");
	return 0;
}
