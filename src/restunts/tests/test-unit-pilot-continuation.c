#include "test-unit-pilot-fixture.h"
#include "../c/opponent/unit_pilot_internal.h"

static struct UNIT_PLANNER source;

static void begin_prefix(void)
{
	reset_fixture(1);
	finish_event = 1;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(55);
	struct UNIT_PILOT_STATS stats;
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == 55 && stats.accepted == 1 && !stats.completed_lap);
	assert(unit_pilot_finish());
	assert(unit_pilot_continuation_pending());
}

static void test_append_and_isolation(void)
{
	begin_prefix();
	struct UNIT_PILOT_STATS stats;
	struct CSRPL_OPPONENT_COMMAND command;
	struct CARSTATE next;
	struct GAMESTATE before = state;
	assert(unit_pilot_continue_work(64));
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == 55);
	assert(unit_pilot_command(&state.opponentstate, 100, &command));
	assert(command.route_point == 0 && unit_pilot_future_car(1, &next));
	assert(!unit_pilot_future_car(60, &next));
	preview_input_mask = 0xff;
	assert(unit_pilot_continue_work(1));
	assert(!preview_input_mask && !preview_input_value);
	assert_live_unchanged(&before);
	assert(unit_pilot_continue_work(64));
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == 56);
	assert(unit_pilot_future_car(1, &state.opponentstate));
	before = state;
	assert(unit_pilot_command(&state.opponentstate, 101, &command));
	assert(command.route_point == 1);
	assert(!unit_pilot_continue_work(UNIT_PILOT_CONTINUATION_TICKS));
	assert_live_unchanged(&before);
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == 70 && stats.completed_lap && stats.matched_commands == 2);
	assert(!unit_pilot_continuation_pending() && unit_pilot_future_car(60, &next));
	assert(unit_pilot_future_car(1, &state.opponentstate));
	assert(unit_pilot_command(&state.opponentstate, 102, &command));
	assert(command.route_point == 2);
}

static void test_cancel_divergence(void)
{
	struct CSRPL_OPPONENT_COMMAND command;
	struct CARSTATE next;
	struct UNIT_PILOT_STATS stats;
	for (legacy_u32 kind = 0; kind < 3; kind++) {
		begin_prefix();
		assert(unit_pilot_command(&state.opponentstate, 100, &command));
		assert(unit_pilot_continue_work(1));
		assert(unit_pilot_future_car(kind == 1 ? 2 : 1, &next));
		if (kind == 0) {
			next.car_position.lx++;
		}
		assert(!unit_pilot_command(&next, kind == 2 ? 102 : 101, &command));
		assert(!unit_pilot_continuation_pending() && !unit_pilot_continue_work(64));
		unit_pilot_stats(&stats);
		assert(stats.native_ticks == 56);
		assert(!unit_pilot_future_car(1, &next));
	}
	begin_prefix();
	assert(unit_pilot_command(&state.opponentstate, 100, &command));
	state.playerstate.car_position = state.opponentstate.car_position;
	struct GAMESTATE before = state;
	preview_input_mask = preview_input_value = 0xff;
	unit_pilot_continuation_cancel();
	assert(!preview_input_mask && !preview_input_value);
	assert(!unit_pilot_continuation_pending() && !unit_pilot_future_car(1, &next));
	assert_live_unchanged(&before);
}

static void test_bounded_work_and_cleanup(void)
{
	begin_prefix();
	struct CSRPL_OPPONENT_COMMAND command;
	struct UNIT_PILOT_STATS stats;
	assert(unit_pilot_command(&state.opponentstate, 100, &command));
	clock_step = 4000000;
	assert(unit_pilot_continue_work(1000));
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == 56);
	unit_pilot_stop();
	preview_input_mask = 0xff;
	unit_pilot_stop();
	assert(!preview_input_mask);
	preview_input_mask = 0xff;
	assert(!unit_pilot_finish() && !preview_input_mask);
	preview_input_mask = 0xff;
	unit_pilot_recheck_cancel();
	assert(!preview_input_mask);
	reset_fixture(0);
	assert(unit_pilot_begin(&source));
	unit_pilot_work(8);
	assert(unit_pilot_finish() && !unit_pilot_continuation_pending());
	unit_pilot_stop();
}

static void test_two_chunk_frame_budget(void)
{
	static const legacy_u32 limits[] = {65U, UNIT_PILOT_CONTINUATION_TICKS, 1000U};
	struct CSRPL_OPPONENT_COMMAND command;
	struct UNIT_PILOT_STATS stats;
	for (legacy_u32 index = 0; index < sizeof(limits) / sizeof(limits[0]); index++) {
		begin_prefix();
		finish_event = 0;
		struct GAMESTATE before = state;
		assert(unit_pilot_command(&state.opponentstate, 100, &command));
		assert(unit_pilot_continue_work(0));
		assert(unit_pilot_continue_work(limits[index]));
		legacy_u32 expected = limits[index] < UNIT_PILOT_CONTINUATION_TICKS
								  ? limits[index]
								  : UNIT_PILOT_CONTINUATION_TICKS;
		unit_pilot_stats(&stats);
		assert(stats.native_ticks == 55U + expected);
		assert_live_unchanged(&before);
		assert(unit_pilot_continue_work(UNIT_PILOT_CONTINUATION_TICKS));
		unit_pilot_stats(&stats);
		assert(stats.native_ticks == 55U + expected);
	}
	begin_prefix();
	finish_event = 0;
	assert(unit_pilot_command(&state.opponentstate, 100, &command));
	/* Each clock read charges 2ms. The first inner chunk stops at 4ms;
	 * envelope and scheduling overhead bring total elapsed time to 8ms,
	 * so a second native chunk must not start. */
	clock_step = 2000000;
	assert(unit_pilot_continue_work(UNIT_PILOT_CONTINUATION_TICKS));
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == 57);
	assert(unit_pilot_continue_work(UNIT_PILOT_CONTINUATION_TICKS));
	unit_pilot_stats(&stats);
	assert(stats.native_ticks == 57);
	unit_pilot_stop();
}

static void test_pulse_retry_and_expired_handoff(void)
{
	reset_fixture(1);
	for (legacy_u32 index = 40; index <= 44; index++) {
		points[index].route_index = 1;
		points[index].stunt = UNIT_STUNT_BARREL;
	}
	for (legacy_u32 index = 45; index < route.count; index++) {
		points[index].route_index = index < 49 ? 0 : (index < 51 ? 2 : (index < 57 ? 3 : 0));
	}
	points[49].stunt = points[50].stunt = UNIT_STUNT_RAMP;
	ramp_approaches[2] = 3;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(43);
	assert(unit_pilot_state->trial && unit_pilot_state->frame == 43);
	unit_pilot_state->secondary_candidate = 1;
	unit_pilot_start_trial();
	assert(unit_pilot_state->secondary_speed == 99.0);
	/* Seed the adapter from this measured scalar attempt, then exercise the
	 * manager's pulse branch without repeating the scalar grid in this test. */
	unit_pilot_pulses_finish_trial(unit_pilot_state->pulses,
								   &unit_pilot_state->run.state.opponentstate,
								   unit_pilot_state->frame, 0);
	assert(unit_pilot_pulses_next(unit_pilot_state->pulses, &unit_pilot_state->pulse_profile));
	unit_pilot_start_trial();
	struct UNIT_PILOT_PULSE_PROFILE profile = unit_pilot_state->pulse_profile;
	legacy_u32 candidate = unit_pilot_state->candidate;
	reject_everything = 1;
	struct GAMESTATE before = state;
	unit_pilot_work(1);
	assert_live_unchanged(&before);
	assert(unit_pilot_pulses_active(unit_pilot_state->pulses));
	assert(unit_pilot_state->candidate == candidate && unit_pilot_state->stats.failed_trials == 1);
	assert(unit_pilot_state->frame == unit_pilot_state->approach_frame);
	assert(unit_pilot_state->step_count == unit_pilot_state->approach_steps);
	profile = unit_pilot_state->pulse_profile;
	assert(unit_pilot_state->speed == profile.speed &&
		   unit_pilot_state->barrel_offset == profile.barrel_offset);
	assert(preview_exit_fraction == profile.exit_fraction &&
		   preview_exit_speed == profile.exit_speed);
	assert(!preview_input_mask && preview_secondary_speed == 99.0);
	reject_everything = 0;
	finish_event = 1;
	unit_pilot_work(64);
	assert(unit_pilot_state->stats.completed_lap && unit_pilot_state->stats.accepted == 1);
	assert(speeds[1] == profile.speed && barrel_offsets[1] == profile.barrel_offset);
	assert(speeds[2] == profile.secondary_speed && profile.secondary_speed == 99.0);
	assert(ramp_approaches[2] == profile.secondary_ramp_approach &&
		   profile.secondary_ramp_approach == 3);
	assert(exits[1].fraction == profile.exit_fraction && exits[1].speed == profile.exit_speed);
	assert(unit_pilot_finish());
	/* The same accepted policy must be revalidated if its scheduled future
	 * handoff expires; a stale cached trajectory must never be released. */
	unit_pilot_state->recheck = 1;
	unit_pilot_state->recheck_status = 1;
	unit_pilot_state->handoff_frame = 32;
	assert(unit_pilot_recheck_status(33) == -1);
	assert(unit_pilot_state->resume_pending && unit_pilot_state->resume_pulses);
	state.playerstate.car_position.lx = state.playerstate.car_position.lz = -64;
	before = state;
	assert(unit_pilot_recheck_begin(&source, 0, 1));
	unit_pilot_work(33);
	assert_live_unchanged(&before);
	assert(unit_pilot_pulses_active(unit_pilot_state->pulses));
	assert(unit_pilot_state->frame == 33);
	assert(profile.speed == unit_pilot_state->pulse_profile.speed &&
		   profile.barrel_offset == unit_pilot_state->pulse_profile.barrel_offset &&
		   profile.exit_fraction == unit_pilot_state->pulse_profile.exit_fraction &&
		   profile.exit_speed == unit_pilot_state->pulse_profile.exit_speed &&
		   profile.secondary_speed == unit_pilot_state->pulse_profile.secondary_speed &&
		   profile.secondary_ramp_approach ==
			   unit_pilot_state->pulse_profile.secondary_ramp_approach);
	assert(!preview_input_mask);
	unit_pilot_stop();
}

static void configure_runup(legacy_u32 next, legacy_u8 stunt)
{
	reset_fixture(1);
	route.finish_begin = route.count - 1U;
	for (legacy_u32 index = 0; index < route.count; index++) {
		points[index].z = index * 128.0;
		if (index >= 42) {
			points[index].route_index = index >= next && index <= next + 1U ? 3 : 2;
		}
	}
	if (next < route.finish_begin) {
		points[next].stunt = points[next + 1U].stunt = stunt;
	}
}

static void test_ramp_runup_and_group_boundaries(void)
{
	static const legacy_u8 kinds[] = {UNIT_STUNT_BARREL, UNIT_STUNT_PROTECTED, UNIT_STUNT_RAMP};
	for (legacy_u32 kind = 0; kind < sizeof(kinds) / sizeof(kinds[0]); kind++) {
		configure_runup(route.count, UNIT_STUNT_NONE);
		points[40].stunt = points[41].stunt = kinds[kind];
		assert(unit_pilot_begin(&source));
		unit_pilot_work(1);
		assert(unit_pilot_state->approach_cursor == (kinds[kind] == UNIT_STUNT_RAMP ? 8U : 16U));
		struct CARSTATE car = state.opponentstate;
		car.car_reserved_route_word1 = 40;
		legacy_u32 tiles = kinds[kind] == UNIT_STUNT_RAMP ? 4U : 3U;
		car.car_position.lz = (5120 - tiles * TRACK_TILE_SIZE) * 64;
		legacy_f64 distance;
		legacy_s16 piece;
		assert(unit_pilot_next_stunt(&car, &piece, &distance));
		assert(piece == 1 && distance == tiles * TRACK_TILE_SIZE);
		car.car_position.lz -= 64;
		assert(!unit_pilot_next_stunt(&car, &piece, &distance));
		/* Live braking lead extends discovery without changing native capture.
		 * The allowance stays explicit, so a slowing car can retain its target. */
		assert(unit_pilot_next_stunt_with_lead(&car, &piece, &distance, 1.0));
		assert(piece == 1 && distance == tiles * TRACK_TILE_SIZE + 1.0);
		car.car_position.lz -= TRACK_TILE_SIZE * 64;
		assert(!unit_pilot_next_stunt_with_lead(&car, &piece, &distance, TRACK_TILE_SIZE));
		assert(unit_pilot_next_stunt_with_lead(&car, &piece, &distance, TRACK_TILE_SIZE + 1.0));
		assert(!unit_pilot_next_stunt_with_lead(&car, &piece, &distance, -1.0));
		assert(!unit_pilot_next_stunt_with_lead(&car, &piece, &distance, NAN));
	}
	/* A ramp exactly four tiles after the previous protected end shares
	 * its approach; an ordinary protected piece at that distance does not. */
	configure_runup(73, UNIT_STUNT_RAMP);
	assert(unit_pilot_begin(&source));
	unit_pilot_work(1);
	assert(unit_pilot_state->last == 74 && unit_pilot_state->approach_cursor == 16);
	configure_runup(74, UNIT_STUNT_RAMP);
	assert(unit_pilot_begin(&source));
	unit_pilot_work(1);
	assert(unit_pilot_state->last == 41);
	configure_runup(70, UNIT_STUNT_PROTECTED);
	assert(unit_pilot_begin(&source));
	unit_pilot_work(1);
	assert(unit_pilot_state->last == 41);
	/* The later ramp needs its launch plan before the barrel's shorter
	 * approach starts. Validate the whole group from that earlier snapshot. */
	configure_runup(42, UNIT_STUNT_RAMP);
	assert(unit_pilot_begin(&source));
	unit_pilot_work(1);
	assert(unit_pilot_state->last == 43 && unit_pilot_state->approach_cursor == 10);
	unit_pilot_work(16);
	assert(unit_pilot_state->trial && unit_pilot_state->approach_steps == 10);
	assert(unit_pilot_state->approach_steps >= unit_pilot_state->committed_steps);
	reject_everything = 1;
	unit_pilot_work(1);
	assert(unit_pilot_state->step_count == 10 && unit_pilot_state->committed_steps == 0);
	unit_pilot_stop();
}

static void test_ramp_window_search(void)
{
	static const legacy_u8 cases[][3] = {{0, 3, 27}, {3, 2, 27}, {4, 2, 52}, {0, 2, 2}};
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		reset_fixture(1);
		points[40].stunt = points[41].stunt = UNIT_STUNT_RAMP;
		ramp_approaches[1] = cases[index][0];
		required_ramp_approach = cases[index][1];
		reject_ramp_approach = finish_event = 1;
		assert(unit_pilot_begin(&source));
		unit_pilot_work(1);
		assert(preview_ramp_approaches[1] == (cases[index][0] ? cases[index][0] : 2));
		assert(ramp_approaches[1] == cases[index][0]);
		for (legacy_u32 chunk = 0; chunk < 8 && unit_pilot_state->stats.active; chunk++) {
			unit_pilot_work(64);
		}
		assert(unit_pilot_state->stats.completed_lap &&
			   unit_pilot_state->stats.trials == cases[index][2]);
		assert(ramp_approaches[1] == cases[index][1] && speeds[1] == 99.0);
		assert(unit_pilot_state->ramp_approach == cases[index][1]);
	}
	reset_fixture(1);
	points[40].stunt = points[41].stunt = UNIT_STUNT_RAMP;
	reject_ramp_approach = 1;
	required_ramp_approach = 5;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(64);
	unit_pilot_work(64);
	assert(unit_pilot_state->stats.exhausted && unit_pilot_state->stats.trials == 75);
	assert(ramp_approaches[1] == 0);
	unit_pilot_stop();
}

static void test_ramp_window_deadline_resume(void)
{
	reset_fixture(1);
	points[40].stunt = points[41].stunt = UNIT_STUNT_RAMP;
	reject_ramp_approach = 1;
	state.playerstate.car_position.lx = state.playerstate.car_position.lz = -64;
	assert(unit_pilot_recheck_begin(&source, 0, 1));
	unit_pilot_work(57);
	assert(unit_pilot_state->ramp_variant == 1 && preview_ramp_approaches[1] == 3);
	assert(ramp_approaches[1] == 0);
	assert(unit_pilot_recheck_status(33) == -1);
	assert(unit_pilot_state->resume_pending && unit_pilot_state->resume_ramp_variant == 1);
	waited_frames = 0;
	assert(unit_pilot_recheck_begin(&source, 0, 1));
	unit_pilot_work(33);
	assert(unit_pilot_state->ramp_variant == 1 && preview_ramp_approaches[1] == 3);
	unit_opponent_pilot_ramp_override(ROUTE_INDEX_NONE, 0);
	unit_pilot_work(1);
	assert(preview_ramp_approaches[1] == 3);
	unit_pilot_stop();
	assert(!preview_ramp_approaches[1]);
}

static void test_secondary_ramp_window_search(void)
{
	reset_fixture(1);
	points[40].stunt = points[41].stunt = UNIT_STUNT_RAMP;
	for (legacy_u32 index = 50; index < route.count; index++) {
		points[index].route_index = index < 52 ? 3 : (index < 58 ? 2 : 0);
	}
	points[50].stunt = points[51].stunt = UNIT_STUNT_RAMP;
	reject_secondary_ramp = finish_event = 1;
	assert(unit_pilot_begin(&source));
	for (legacy_u32 chunk = 0; chunk < 32 && unit_pilot_state->stats.active; chunk++) {
		unit_pilot_work(64);
		if (unit_pilot_state->trial) {
			assert(preview_ramp_approaches[1] == 2);
			assert(preview_ramp_approaches[3] == unit_pilot_state->secondary_ramp_approach);
		}
	}
	assert(unit_pilot_state->stats.completed_lap && unit_pilot_state->stats.trials == 27);
	assert(ramp_approaches[1] == 2 && ramp_approaches[3] == 3);
	assert(speeds[1] == 100.0 && speeds[3] == 99.0);
	unit_pilot_stop();
}

static void test_recheck_handoff_continuation(void)
{
	for (legacy_u8 cancel = 0; cancel < 2; cancel++) {
		reset_fixture(1);
		finish_event = 1;
		state.playerstate.car_position.lx = state.playerstate.car_position.lz = -64;
		assert(unit_pilot_begin(&source));
		unit_pilot_work(64);
		unit_pilot_work(64);
		assert(unit_pilot_state->stats.completed_lap);
		assert(unit_pilot_recheck_begin(&source, 0, 1));
		assert(!unit_pilot_state->stats.completed_lap);
		unit_pilot_work(64);
		unit_pilot_work(64);
		assert(unit_pilot_recheck_status(32) == 1 && unit_pilot_state->stats.accepted == 2);
		assert(!unit_pilot_continuation_pending() && unit_pilot_state->committed_steps == 50);
		legacy_u32 ticks = unit_pilot_state->stats.native_ticks;
		assert(!unit_pilot_continue_work(UNIT_PILOT_CONTINUATION_TICKS));
		assert(unit_pilot_state->stats.native_ticks == ticks);
		struct CSRPL_OPPONENT_COMMAND command;
		struct CARSTATE handoff = unit_pilot_state->steps[0].car;
		assert(!unit_pilot_command(&handoff, 31, &command));
		assert(!unit_pilot_command(&handoff, 33, &command));
		/* A later matching cached pose cannot replace the certified first
		 * command at the fixed handoff time. */
		assert(!unit_pilot_command(&unit_pilot_state->steps[1].car, 32, &command));
		assert(!unit_pilot_continuation_pending());
		state.opponentstate = handoff;
		struct GAMESTATE before = state;
		assert(unit_pilot_command(&state.opponentstate, 32, &command));
		assert(command.route_point == 32 && unit_pilot_continuation_pending());
		assert(!unit_pilot_state->recheck && !unit_pilot_state->stats.active);
		assert(unit_pilot_continue_work(1));
		assert_live_unchanged(&before);
		assert(unit_pilot_state->stats.native_ticks == ticks + 1U);
		assert(unit_pilot_continue_work(UNIT_PILOT_CONTINUATION_TICKS));
		assert(unit_pilot_state->stats.native_ticks == ticks + 1U);
		struct CARSTATE next;
		assert(unit_pilot_future_car(1, &next));
		if (cancel) {
			next.car_position.lx++;
			assert(!unit_pilot_command(&next, 33, &command));
			assert(!unit_pilot_continuation_pending() && !unit_pilot_future_car(1, &next));
			assert(!unit_pilot_continue_work(UNIT_PILOT_CONTINUATION_TICKS));
			assert(unit_pilot_state->stats.native_ticks == ticks + 1U);
		} else {
			state.opponentstate = next;
			before = state;
			assert(unit_pilot_command(&state.opponentstate, 33, &command));
			assert(command.route_point == 33);
			assert(!unit_pilot_continue_work(UNIT_PILOT_CONTINUATION_TICKS));
			assert_live_unchanged(&before);
			assert(unit_pilot_state->stats.completed_lap &&
				   unit_pilot_state->committed_steps == 70);
			assert(unit_pilot_future_car(60, &next));
		}
		unit_pilot_stop();
	}
}

legacy_int main(void)
{
	test_append_and_isolation();
	test_cancel_divergence();
	test_bounded_work_and_cleanup();
	test_two_chunk_frame_budget();
	test_pulse_retry_and_expired_handoff();
	test_ramp_runup_and_group_boundaries();
	test_ramp_window_search();
	test_ramp_window_deadline_resume();
	test_secondary_ramp_window_search();
	test_recheck_handoff_continuation();
	return 0;
}
