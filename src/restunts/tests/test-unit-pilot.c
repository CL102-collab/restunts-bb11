#include "test-unit-pilot-fixture.h"
#include "../c/opponent/unit_pilot_internal.h"

#define TEST_PREVIEW_HUMAN_POSITION 100
#define TEST_PREVIEW_HUMAN_SPEED 20

enum TEST_PREVIEW_MODE {
	TEST_PREVIEW_STARTUP,
	TEST_PREVIEW_STOPPED,
	TEST_PREVIEW_ROLLING,
	TEST_PREVIEW_MODE_COUNT
};

static void assert_preview_human_absent(void)
{
	const struct CARSTATE *human = &unit_pilot_state->run.state.playerstate;
	assert(human->car_position.lx < 0 && human->car_position.ly < 0 && human->car_position.lz < 0);
	assert(memcmp(&human->car_previous_position, &human->car_position,
				  sizeof(human->car_position)) == 0);
	assert(human->car_actual_speed == 0 && human->car_rev_speed == 0);
}

static void test_preview_excludes_human_body(void)
{
	for (legacy_u8 mode = TEST_PREVIEW_STARTUP; mode < TEST_PREVIEW_MODE_COUNT; mode++) {
		struct UNIT_PLANNER source = {0};
		reset_fixture(1);
		state.playerstate.car_position.lx =
			(legacy_s32)(TEST_PREVIEW_HUMAN_POSITION * UNIT_POSITION_SCALE);
		state.playerstate.car_position.lz = state.playerstate.car_position.lx;
		state.playerstate.car_previous_position = state.playerstate.car_position;
		state.playerstate.car_previous_position.lz--;
		state.playerstate.car_actual_speed =
			(legacy_u16)(TEST_PREVIEW_HUMAN_SPEED * UNIT_SPEED_SCALE);
		state.playerstate.car_rev_speed = state.playerstate.car_actual_speed;
		struct GAMESTATE before = state;
		assert(unit_pilot_begin(&source));
		assert_preview_human_absent();
		assert_live_unchanged(&before);
		if (mode == TEST_PREVIEW_STOPPED) {
			assert(unit_pilot_recheck_begin(&source, 0, 1));
		} else if (mode == TEST_PREVIEW_ROLLING) {
			assert(unit_pilot_recheck_rolling_begin(&source, 0, 1));
		}
		assert_preview_human_absent();
		assert_live_unchanged(&before);
		/* The fixture's native step also rejects any in-track human body. */
		unit_pilot_work(mode == TEST_PREVIEW_STOPPED ? UNIT_PILOT_WAIT_TICKS + 1U : 1U);
		assert_preview_human_absent();
		assert_live_unchanged(&before);
		unit_pilot_stop();
		assert_live_unchanged(&before);
	}
}

static void test_barrel_search(void)
{
	struct UNIT_PLANNER source = {0};
	struct UNIT_PILOT_STATS stats;
	reset_fixture(1);
	reject_barrel_offset = finish_event = 1;
	assert(unit_pilot_begin(&source));
	for (legacy_u32 slice = 0; slice < 500; slice++) {
		unit_pilot_work(64);
	}
	unit_pilot_stats(&stats);
	/* Every baseline speed, braking profile and negative-quarter offset fails before the
	 * positive-quarter offset succeeds at its second speed candidate. */
	assert(stats.completed_lap && stats.accepted == 1 && stats.failed_trials == 626 &&
		   stats.trials == 627);
	assert(speeds[1] == 99.0 && barrel_offsets[1] == 0.25 && preview_barrel_offset == 0.0);
	state.playerstate.car_position.lx = state.playerstate.car_position.lz = -1;
	source.calls = 50000;
	assert(unit_pilot_recheck_begin(&source, 50000, 1));
	unit_pilot_work(64);
	unit_pilot_work(64);
	assert(unit_pilot_recheck_status(50032) == 1);
	unit_pilot_stats(&stats);
	assert(stats.failed_trials == 626 && stats.trials == 628);
	/* An altered approach can invalidate the old successful lateral line.
	 * Keep centerline in the search after warming the old line. */
	required_barrel_offset = 0.0;
	waited_frames = 0;
	source.calls = 52000;
	assert(unit_pilot_recheck_begin(&source, 52000, 1));
	for (legacy_u32 slice = 0; slice < 500; slice++) {
		unit_pilot_work(64);
	}
	assert(unit_pilot_recheck_status(52032) == 1);
	unit_pilot_stats(&stats);
	assert(stats.failed_trials == 651 && stats.trials == 654);
	assert(barrel_offsets[1] == 0.0);
	reset_fixture(1);
	reject_barrel_offset = finish_event = 1;
	state.playerstate.car_position.lx = state.playerstate.car_position.lz = -1;
	assert(unit_pilot_begin(&source));
	source.calls = 60000;
	assert(unit_pilot_recheck_begin(&source, 60000, 1));
	for (legacy_u32 tick = 0; tick < 30000; tick++) {
		unit_pilot_work(1);
		unit_pilot_stats(&stats);
		if (stats.failed_trials == 603 && last_native_speed == 98.0 &&
			preview_barrel_offset == -0.25) {
			break;
		}
	}
	assert(stats.failed_trials == 603 && last_native_speed == 98.0);
	assert(unit_pilot_recheck_status(60033) == -1);
	/* A fresh native wait forecast must retain both the line and speed index,
	 * without repeating the 25 failed baseline trials or shifting old poses. */
	waited_frames = 0;
	source.calls = 61000;
	assert(unit_pilot_recheck_begin(&source, 61000, 1));
	unit_pilot_work(33);
	assert(last_native_speed == 98.0 && preview_barrel_offset == -0.25);
	unit_pilot_stats(&stats);
	assert(stats.failed_trials == 603);
	unit_pilot_recheck_cancel();
	assert(preview_barrel_offset == 0.0);
}

static void test_barrel_late_braking(void)
{
	struct UNIT_PLANNER source = {0};
	struct UNIT_PILOT_STATS stats;
	reset_fixture(1);
	reject_barrel_phase = finish_event = 1;
	assert(unit_pilot_begin(&source));
	for (legacy_u32 slice = 0; slice < 300; slice++) {
		unit_pilot_work(64);
	}
	unit_pilot_stats(&stats);
	assert(stats.completed_lap && stats.accepted == 1 && stats.failed_trials == 426 &&
		   stats.trials == 427);
	assert(speeds[1] == 99.0 && exits[1].fraction == 0.875);
	assert(fabs(exits[1].speed - 89.1) < 0.000001 && barrel_offsets[1] == 0.0);
	state.playerstate.car_position.lx = state.playerstate.car_position.lz = -1;
	speeds[1] = 100.0;
	exits[1].speed = 90.0;
	source.calls = 70000;
	assert(unit_pilot_recheck_begin(&source, 70000, 1));
	unit_pilot_work(64);
	unit_pilot_work(64);
	assert(unit_pilot_recheck_status(70032) == 1);
	unit_pilot_stats(&stats);
	assert(stats.failed_trials == 427 && stats.trials == 429);
	assert(speeds[1] == 99.0 && fabs(exits[1].speed - 89.1) < 0.000001);
	/* A warm phase is validated again, but an altered approach can require
	 * the original center/no-phase policy instead. It remains in the set. */
	required_exit_fraction = required_exit_ratio = 0.0;
	waited_frames = 0;
	source.calls = 72000;
	assert(unit_pilot_recheck_begin(&source, 72000, 1));
	for (legacy_u32 slice = 0; slice < 100; slice++) {
		unit_pilot_work(64);
	}
	assert(unit_pilot_recheck_status(72032) == 1);
	unit_pilot_stats(&stats);
	assert(stats.failed_trials == 452 && stats.trials == 455);
	assert(exits[1].fraction == 0.0 && exits[1].speed == 0.0);
	reset_fixture(1);
	reject_barrel_phase = finish_event = 1;
	state.playerstate.car_position.lx = state.playerstate.car_position.lz = -1;
	assert(unit_pilot_begin(&source));
	source.calls = 80000;
	assert(unit_pilot_recheck_begin(&source, 80000, 1));
	for (legacy_u32 tick = 0; tick < 15000; tick++) {
		unit_pilot_work(1);
		unit_pilot_stats(&stats);
		if (stats.failed_trials == 26 && last_native_speed == 99.0 &&
			preview_exit_fraction == 0.90625) {
			break;
		}
	}
	assert(stats.failed_trials == 26 && preview_exit_fraction == 0.90625);
	assert(unit_pilot_recheck_status(80033) == -1);
	waited_frames = 0;
	source.calls = 81000;
	assert(unit_pilot_recheck_begin(&source, 81000, 1));
	unit_pilot_work(33);
	assert(last_native_speed == 99.0 && preview_exit_fraction == 0.90625);
	assert(fabs(preview_exit_speed - 84.15) < 0.000001 && preview_barrel_offset == 0.0);
	unit_pilot_stats(&stats);
	assert(stats.failed_trials == 26);
	unit_pilot_recheck_cancel();
	assert(preview_exit_fraction == 0.0 && preview_barrel_offset == 0.0);
}

static void test_barrel_combined_profile(void)
{
	struct UNIT_PLANNER source = {0};
	struct UNIT_PILOT_STATS stats;
	reset_fixture(1);
	reject_barrel_combination = finish_event = 1;
	assert(unit_pilot_begin(&source));
	for (legacy_u32 slice = 0; slice < 200; slice++) {
		unit_pilot_work(64);
	}
	unit_pilot_stats(&stats);
	assert(stats.completed_lap && stats.accepted == 1 && stats.failed_trials == 226 &&
		   stats.trials == 227);
	assert(speeds[1] == 99.0 && barrel_offsets[1] == 0.25 && exits[1].fraction == 0.921875);
	assert(fabs(exits[1].speed - 84.15) < 0.000001);
	state.playerstate.car_position.lx = state.playerstate.car_position.lz = -1;
	source.calls = 90000;
	assert(unit_pilot_recheck_begin(&source, 90000, 1));
	unit_pilot_work(64);
	unit_pilot_work(64);
	assert(unit_pilot_recheck_status(90032) == 1);
	unit_pilot_stats(&stats);
	assert(stats.failed_trials == 226 && stats.trials == 228);
}

static void test_barrel_refinement(void)
{
	struct UNIT_PLANNER source = {0};
	struct UNIT_PILOT_STATS stats;
	reset_fixture(1);
	reject_barrel_refinement = finish_event = 1;
	assert(unit_pilot_begin(&source));
	for (legacy_u32 slice = 0; slice < 100; slice++) {
		unit_pilot_work(64);
	}
	unit_pilot_stats(&stats);
	assert(stats.completed_lap && stats.trials == 26 && stats.failed_trials == 25);
	assert(speeds[1] == 99.5 && barrel_offsets[1] == 0.0 && exits[1].fraction == 0.0);
	reset_fixture(1);
	reject_barrel_refinement = finish_event = 1;
	required_refined_speed = 100.5;
	state.playerstate.car_position.lx = state.playerstate.car_position.lz = -1;
	assert(unit_pilot_begin(&source));
	source.calls = 100000;
	assert(unit_pilot_recheck_begin(&source, 100000, 1));
	for (legacy_u32 tick = 0; tick < 5000; tick++) {
		unit_pilot_work(1);
		unit_pilot_stats(&stats);
		if (stats.failed_trials == 25 && last_native_speed == 99.5) {
			break;
		}
	}
	assert(stats.failed_trials == 25 && last_native_speed == 99.5);
	assert(unit_pilot_recheck_status(100033) == -1);
	waited_frames = 0;
	source.calls = 101000;
	assert(unit_pilot_recheck_begin(&source, 101000, 1));
	unit_pilot_work(33);
	assert(last_native_speed == 99.5);
	unit_pilot_work(64);
	unit_pilot_work(64);
	assert(unit_pilot_recheck_status(101032) == 1);
	unit_pilot_stats(&stats);
	assert(stats.failed_trials == 26 && fabs(speeds[1] - 100.5) < 0.000001);
}

static void test_barrel_local_controls(void)
{
	static const legacy_f64 trims[] = {-0.3125, -0.25, -0.3125};
	static const legacy_f64 phases[] = {0.90625, 0.90234375, 0.890625};
	static const legacy_u32 failures[] = {33, 39, 45};
	for (legacy_u32 choice = 0; choice < 3; choice++) {
		struct UNIT_PLANNER source = {0};
		struct UNIT_PILOT_STATS stats;
		reset_fixture(1);
		reject_barrel_local = finish_event = 1;
		required_local_trim = trims[choice];
		required_local_phase = phases[choice];
		speeds[1] = 100.0;
		barrel_offsets[1] = -0.25;
		exits[1].fraction = 0.90625;
		exits[1].speed = 85.0;
		state.playerstate.car_position.lx = state.playerstate.car_position.lz = -1;
		assert(unit_pilot_begin(&source));
		legacy_u32 frame = 110000 + choice * 1000;
		source.calls = frame;
		assert(unit_pilot_recheck_begin(&source, frame, 1));
		for (legacy_u32 tick = 0; tick < 5000; tick++) {
			unit_pilot_work(1);
			unit_pilot_stats(&stats);
			if (!stats.active || (choice == 2 && preview_barrel_offset == required_local_trim &&
								  preview_exit_fraction == required_local_phase)) {
				break;
			}
		}
		if (choice == 2) {
			assert(unit_pilot_recheck_status(frame + 33) == -1);
			frame += 100;
			waited_frames = 0;
			source.calls = frame;
			assert(unit_pilot_recheck_begin(&source, frame, 1));
			unit_pilot_work(33);
			assert(preview_barrel_offset == required_local_trim &&
				   preview_exit_fraction == required_local_phase);
			unit_pilot_work(64);
			unit_pilot_work(64);
		}
		assert(unit_pilot_recheck_status(frame + 32) == 1);
		unit_pilot_stats(&stats);
		assert(stats.accepted == 1 && stats.failed_trials == failures[choice]);
		assert(barrel_offsets[1] == required_local_trim &&
			   exits[1].fraction == required_local_phase);
		assert(exits[1].speed == 85.0 && speeds[1] == 100.0);
	}
}

legacy_int main(void)
{
	test_preview_excludes_human_body();
	struct UNIT_PLANNER source = {0};
	struct UNIT_PILOT_STATS stats;
	reset_fixture(0);
	struct GAMESTATE before = state;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(0);
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == 0);
	unit_pilot_work(1000);
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == 64 && stats.active);
	assert_live_unchanged(&before);
	clock_step = 1000000;
	unit_pilot_work(64);
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == 68);
	assert_live_unchanged(&before);
	assert(unit_pilot_finish() != NULL);
	unit_pilot_work(1);
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == 68 && !stats.active);
	reset_fixture(1);
	reject_fast = finish_event = 1;
	before = state;
	assert(unit_pilot_begin(&source));
	for (legacy_u32 slice = 0; slice < 100; slice++) {
		unit_pilot_work(1);
		assert_live_unchanged(&before);
	}
	unit_pilot_stats(&stats);
	assert(stats.completed_lap && !stats.active && !stats.exhausted);
	assert(stats.failed_trials == 1 && stats.trials == 2 && stats.accepted == 1);
	assert(accepts == 1 && speeds[1] == 99.0 && resets == 2);
	struct CARSTATE entry;
	legacy_f64 speed;
	assert(unit_pilot_expected_entry(1, &entry, &speed));
	assert(speed == 99.0 && entry.car_reserved_route_word1 == 40);
	assert(!unit_pilot_expected_entry(2, NULL, NULL));
	struct CSRPL_OPPONENT_COMMAND command;
	assert(unit_pilot_command(&state.opponentstate, 100, &command));
	assert(command.route_point == 0);
	struct CARSTATE future;
	assert(unit_pilot_future_car(1, &future));
	assert(future.car_reserved_route_word1 == 1);
	assert(!unit_pilot_future_car(0xffffffffU, &future));
	struct CARSTATE mismatch = state.opponentstate;
	mismatch.car_position.lx++;
	assert(!unit_pilot_command(&mismatch, 101, &command));
	reset_fixture(1);
	for (legacy_u32 index = 50; index < route.count; index++) {
		points[index].route_index = index < 52 ? 3 : 2;
	}
	points[40].stunt = points[41].stunt = UNIT_STUNT_PROTECTED;
	points[50].stunt = points[51].stunt = UNIT_STUNT_RAMP;
	points[50].speed_floor = 120.0;
	reject_group = finish_event = 1;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(46);
	unit_pilot_stats(&stats);
	assert(stats.accepted == 0);
	unit_pilot_work(64);
	unit_pilot_work(64);
	unit_pilot_stats(&stats);
	assert(stats.completed_lap && stats.accepted == 1 && stats.trials == 2 &&
		   stats.failed_trials == 1);
	assert(exits[1].fraction == 0.25 && exits[1].speed == 105.0);
	reset_fixture(1);
	points[40].stunt = points[41].stunt = UNIT_STUNT_PROTECTED;
	for (legacy_u32 index = 50; index < route.count; index++) {
		points[index].route_index = index < 52 ? 3 : (index < 58 ? 2 : 0);
	}
	points[50].stunt = points[51].stunt = UNIT_STUNT_PROTECTED;
	reject_secondary = finish_event = 1;
	assert(unit_pilot_begin(&source));
	for (legacy_u32 slice = 0; slice < 4; slice++) {
		unit_pilot_work(64);
	}
	unit_pilot_stats(&stats);
	assert(stats.completed_lap && stats.accepted == 1 && stats.trials == 2 &&
		   stats.failed_trials == 1);
	assert(accepts == 2 && speeds[1] == 100.0 && speeds[3] == 99.0);
	assert(unit_pilot_expected_entry(3, &entry, &speed));
	assert(speed == 99.0 && entry.car_reserved_route_word1 == 50);
	assert(unit_pilot_last_failure_entry(&entry));
	assert(entry.car_reserved_route_word1 == 40);
	assert(unit_pilot_failure_entry_car(0, &entry));
	assert(entry.car_reserved_route_word1 == 40);
	assert(unit_pilot_failure_entry_car(1, &entry));
	assert(entry.car_reserved_route_word1 == 41);
	assert(!unit_pilot_failure_entry_car(128, &entry));
	assert(preview_secondary_speed == 0.0);
	reset_fixture(1);
	reject_receiver = finish_event = 1;
	assert(unit_pilot_begin(&source));
	for (legacy_u32 slice = 0; slice < 4; slice++) {
		unit_pilot_work(64);
	}
	unit_pilot_stats(&stats);
	assert(stats.completed_lap && stats.accepted == 1 && stats.failed_trials == 1);
	assert(speeds[1] == 99.0);
	reset_fixture(1);
	hold_launch = 1;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(46);
	unit_pilot_stats(&stats);
	assert(stats.accepted == 0);
	hold_launch = 0;
	unit_pilot_work(10);
	unit_pilot_stats(&stats);
	assert(stats.accepted == 1);
	reset_fixture(1);
	observed_owner = 1;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(46);
	unit_pilot_stats(&stats);
	assert(stats.accepted == 0);
	observed_owner = 2;
	unit_pilot_work(10);
	unit_pilot_stats(&stats);
	assert(stats.accepted == 1);
	reset_fixture(1);
	reject_everything = 1;
	before = state;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(64);
	unit_pilot_stats(&stats);
	assert(stats.exhausted && !stats.active && !stats.completed_lap);
	assert(stats.failed_trials == 25 && stats.trials == 25 && accepts == 0);
	assert(unit_pilot_failure_car(0, &entry));
	assert(entry.car_crashBmpFlag == CRASH_EVENT_COLLISION);
	assert(unit_pilot_failure_car(1, &entry));
	assert(entry.car_crashBmpFlag == CRASH_EVENT_NONE);
	assert(!unit_pilot_failure_car(129, &entry));
	assert_live_unchanged(&before);
	reset_fixture(0);
	injected_penalty = 1;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(1);
	unit_pilot_stats(&stats);
	assert(stats.exhausted && stats.accepted == 0 && stats.last_failure == -2);
	reset_fixture(1);
	state.playerstate.car_position.lx = state.playerstate.car_position.lz = -1;
	before = state;
	assert(unit_pilot_begin(&source));
	speeds[1] = 99.0;
	reject_fast = 1;
	source.calls = 40000;
	injected_penalty = 5;
	assert(unit_pilot_recheck_begin(&source, 40000, 1));
	assert(unit_pilot_recheck_status(40000) == 0);
	unit_pilot_work(64);
	unit_pilot_work(64);
	assert(unit_pilot_recheck_status(40001) == 0);
	assert(unit_pilot_recheck_status(40032) == 1);
	unit_pilot_stats(&stats);
	assert(stats.trials == 1 && stats.failed_trials == 0);
	assert_live_unchanged(&before);
	struct CARSTATE waited = state.opponentstate;
	waited.car_position.lx = -32;
	waited.car_previous_position.lx = -31;
	assert(!unit_pilot_command(&waited, 40031, &command));
	waited.car_position.lx++;
	assert(!unit_pilot_command(&waited, 40032, &command));
	waited.car_position.lx--;
	assert(unit_pilot_command(&waited, 40032, &command));
	assert(command.route_point == 40032);
	assert(unit_pilot_future_car(1, &future) && future.car_position.lx == -32);
	assert(unit_pilot_recheck_begin(&source, 41000, 1));
	assert(unit_pilot_recheck_status(41033) == -1);
	assert(!unit_pilot_command(&waited, 41033, &command));
	struct CARSTATE near_entry = state.opponentstate;
	near_entry.car_reserved_route_word1 = 40;
	near_entry.car_position.lz = 3900 * 64;
	legacy_s16 next_piece;
	legacy_f64 distance;
	assert(unit_pilot_next_stunt(&near_entry, &next_piece, &distance));
	assert(next_piece == 1 && distance == 100.0);
	near_entry.car_position.lz = (4000 - 3 * TRACK_TILE_SIZE) * 64;
	assert(unit_pilot_next_stunt(&near_entry, &next_piece, &distance));
	assert(distance == 3 * TRACK_TILE_SIZE);
	near_entry.car_position.lz -= 64;
	assert(!unit_pilot_next_stunt(&near_entry, &next_piece, &distance));
	near_entry.car_position.lz = 3900 * 64;
	near_entry.car_reserved_route_word1 = 41;
	assert(unit_pilot_next_stunt(&near_entry, &next_piece, &distance));
	assert(distance == 0.0);
	assert(unit_pilot_recheck_begin(&source, 42000, 1));
	unit_pilot_recheck_cancel();
	unit_pilot_stats(&stats);
	legacy_u32 canceled_ticks = stats.native_ticks;
	unit_pilot_work(64);
	unit_pilot_stats(&stats);
	assert(!stats.active && stats.native_ticks == canceled_ticks);
	assert(unit_pilot_recheck_status(42000) == -1);
	assert(unit_pilot_recheck_status(42033) == -1);
	assert(!unit_pilot_command(&waited, 42032, &command));
	test_barrel_search();
	test_barrel_late_braking();
	test_barrel_combined_profile();
	test_barrel_refinement();
	test_barrel_local_controls();
	unit_pilot_stop();
	assert(!unit_pilot_begin(NULL));
	return 0;
}
