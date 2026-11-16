#include "test-unit-pilot-fixture.h"
#include "../c/opponent/unit_pilot_internal.h"

static struct UNIT_PLANNER source;

#define TEST_GROUP_PRIMARY_FIRST 40U
#define TEST_GROUP_CONNECTOR_FIRST 56U
#define TEST_GROUP_CONNECTOR_PIECE 2
#define TEST_GROUP_NEXT_FIRST 80U
#define TEST_GROUP_PIECE_SAMPLES 2U
#define TEST_RECEIVER_CLEARANCE_FIRST 48U
#define TEST_RECEIVER_PENDING_TICKS 55U
#define TEST_RECEIVER_OWNER 3
#define TEST_RECEIVER_STABLE_TICKS 4U
#define TEST_BANK_ENTRY 40U
#define TEST_BANK_SAMPLES 2U
#define TEST_BANK_FAILURE_TICK 50U
#define TEST_RETRY_BANK_ENTRY 30U
#define TEST_RETRY_BEFORE_FAILURE 45U
#define TEST_BANK_RECEIVER_END 55U
#define TEST_BANK_RECHECK_BOUND 100U
#define TEST_BANK_UNSUPPORTED_EXIT_TICKS 16U
#define TEST_LEARNING_LATER_FIRST 90U
#define TEST_LEARNING_LATER_RECEIVER_LAST 97U
#define TEST_LEARNING_LIVE_PROGRESS 100U

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
	assert(unit_pilot_finish() && unit_pilot_continuation_pending());
	unit_pilot_stop();
}

static void test_ordinary_prefix(void)
{
	reset_fixture(0);
	assert(unit_pilot_begin(&source));
	unit_pilot_work(8);
	assert(unit_pilot_state->committed_steps == 8);
	assert(unit_pilot_finish() && unit_pilot_continuation_pending());
	struct CSRPL_OPPONENT_COMMAND command;
	assert(unit_pilot_command(&state.opponentstate, 100, &command));
	struct GAMESTATE before = state;
	assert(unit_pilot_continue_work(1));
	assert(unit_pilot_state->committed_steps == 9);
	assert_live_unchanged(&before);
	/* A late ordinary failure must retain all earlier safe commands, while
	 * excluding the command that caused the crash from the usable prefix. */
	reset_fixture(0);
	fail_ordinary_at = 12;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(64);
	assert(unit_pilot_state->stats.exhausted && unit_pilot_state->committed_steps == 11);
	assert(unit_pilot_finish() && !unit_pilot_continuation_pending());
	assert(unit_pilot_command(&state.opponentstate, 100, &command));
	struct CARSTATE future;
	assert(unit_pilot_future_car(10, &future));
	assert(!unit_pilot_future_car(11, &future));
	unit_pilot_stop();
}

static void test_rolling_recheck(void)
{
	reset_fixture(1);
	state.opponentstate.car_actual_speed =
		(legacy_u16)(UNIT_PILOT_MINIMUM_SPEED * UNIT_SPEED_SCALE);
	state.playerstate.car_position.lx = state.playerstate.car_position.lz = -64;
	assert(unit_pilot_begin(&source));
	source.calls = 100;
	assert(unit_pilot_recheck_rolling_begin(&source, 100, 1));
	assert(!unit_pilot_state->wait_remaining && unit_pilot_state->approach_cursor > 0);
	unit_pilot_work(8);
	assert(unit_pilot_recheck_status(100) == 1 && unit_pilot_state->committed_steps == 8);
	assert(!waited_frames && !unit_pilot_state->trial);
	struct CSRPL_OPPONENT_COMMAND command;
	assert(!unit_pilot_command(&state.opponentstate, 101, &command));
	assert(unit_pilot_command(&state.opponentstate, 100, &command));
	assert(command.route_point == 100 && unit_pilot_continuation_pending());
	assert(unit_pilot_continue_work(1));
	assert(unit_pilot_state->committed_steps == 9);
	/* A missed moving handoff cannot reuse a search from an old car pose. */
	unit_pilot_state->resume_pending = 1;
	source.calls = 200;
	assert(unit_pilot_recheck_rolling_begin(&source, 200, 1));
	assert(!unit_pilot_state->resume_pending);
	unit_pilot_work(8);
	assert(unit_pilot_recheck_status(201) == -1 && !unit_pilot_state->resume_pending);
	/* Already entering a stunt supplies no certified ordinary prefix. */
	state.opponentstate.car_reserved_route_word1 = 40;
	state.opponentstate.car_position.lz = 4000 * 64;
	source.calls = 300;
	assert(unit_pilot_recheck_rolling_begin(&source, 300, 1));
	unit_pilot_work(1);
	assert(unit_pilot_state->trial && !unit_pilot_state->committed_steps);
	assert(unit_pilot_recheck_status(300) == 0);
	assert(!unit_pilot_command(&state.opponentstate, 300, &command));
	unit_pilot_stop();
	source.calls = 0;
}

static void test_receiving_road_geometry(void)
{
	reset_fixture(1);
	native_road_support = 0;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(55);
	/* Surface flags and horizontal provenance can still refer to a different
	 * deck. Road support must also overlap actual native road geometry. */
	assert(!unit_pilot_state->stats.accepted && unit_pilot_state->trial);
	/* Match OWOOT: one geometric overlap plus all four native road contacts
	 * permits legitimate banked and graded landings. */
	native_road_support = 1;
	unit_pilot_work(4);
	assert(unit_pilot_state->stats.accepted == 1);
	unit_pilot_stop();
}

static void test_clearance_receiving_road(void)
{
	for (legacy_u8 clearance = 0; clearance <= 1U; clearance++) {
		reset_fixture(1);
		native_road_support = 0;
		assert(unit_pilot_begin(&source));
		unit_pilot_work(TEST_RECEIVER_PENDING_TICKS);
		assert(unit_pilot_state->trial && !unit_pilot_state->stats.accepted);
		/* The car has advanced onto a separately planned road-clearance
		 * section while native receiving-contact proof was pending. */
		for (legacy_u32 point = TEST_RECEIVER_CLEARANCE_FIRST; point < route.finish_begin;
			 point++) {
			points[point].stunt = UNIT_STUNT_PROTECTED;
			points[point].clearance_only = clearance;
		}
		observed_owner = TEST_RECEIVER_OWNER;
		unit_pilot_work(TEST_RECEIVER_STABLE_TICKS);
		assert(!unit_pilot_state->stats.accepted);
		/* Clearance classification alone never supplies geometric support.
		 * A mandatory loop/barrel still cannot serve as the receiving road. */
		native_road_support = 1;
		unit_pilot_work(TEST_RECEIVER_STABLE_TICKS);
		assert(unit_pilot_state->stats.accepted == clearance);
	}
	unit_pilot_stop();
}

static void set_ordinary_bank(void)
{
	for (legacy_u32 point = TEST_BANK_ENTRY; point < TEST_BANK_ENTRY + TEST_BANK_SAMPLES; point++) {
		points[point].bank_support = 1;
		points[point].height_unspecified = 1;
	}
}

static void test_bank_controls_wait_for_supported_receiver(void)
{
	for (legacy_u8 checked = 0; checked <= 1U; checked++) {
		reset_fixture(0);
		if (checked) {
			set_ordinary_bank();
		}
		native_road_support = 0;
		assert(unit_pilot_begin(&source));
		unit_pilot_work(TEST_RECEIVER_PENDING_TICKS);
		assert(unit_pilot_state->step_count == TEST_RECEIVER_PENDING_TICKS);
		struct CARSTATE bank_entry = unit_pilot_state->steps[TEST_BANK_ENTRY].car;
		struct CSRPL_OPPONENT_COMMAND command;
		assert(!unit_pilot_state->trial && !unit_pilot_state->stats.accepted);
		if (!checked) {
			/* Ordinary previews used to release a non-crashing bank departure
			 * before proving support on the receiving road. */
			assert(unit_pilot_state->committed_steps == TEST_RECEIVER_PENDING_TICKS);
			assert(unit_pilot_command(&bank_entry, TEST_BANK_ENTRY, &command));
		} else {
			assert(unit_pilot_state->committed_steps > 0);
			assert(unit_pilot_state->committed_steps < TEST_BANK_ENTRY);
			assert(!unit_pilot_command(&bank_entry, TEST_BANK_ENTRY, &command));
			/* Paved flags and horizontal progress cannot certify an off-deck
			 * landing. Admission must wait for actual receiving-road support. */
			native_road_support = 1;
			unit_pilot_work(TEST_RECEIVER_STABLE_TICKS);
			assert(!unit_pilot_state->trial && !unit_pilot_state->stats.accepted);
			assert(unit_pilot_state->committed_steps == unit_pilot_state->step_count);
			assert(unit_pilot_command(&bank_entry, TEST_BANK_ENTRY, &command));
		}
		assert(command.route_point == TEST_BANK_ENTRY);
		unit_pilot_stop();
	}
	reset_fixture(0);
	set_ordinary_bank();
	native_road_support = 0;
	fail_ordinary_at = TEST_BANK_FAILURE_TICK;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(TEST_RECEIVER_PENDING_TICKS);
	assert(unit_pilot_state->stats.exhausted);
	assert(unit_pilot_state->committed_steps > 0);
	assert(unit_pilot_state->committed_steps < TEST_BANK_ENTRY);
	assert(unit_pilot_finish() && !unit_pilot_continuation_pending());
	struct CARSTATE bank_entry = unit_pilot_state->steps[TEST_BANK_ENTRY].car;
	struct CSRPL_OPPONENT_COMMAND command;
	assert(!unit_pilot_command(&bank_entry, TEST_BANK_ENTRY, &command));
	unit_pilot_stop();
}

static void test_bank_admission_survives_failed_hard_trial(void)
{
	reset_fixture(1);
	for (legacy_u32 point = TEST_RETRY_BANK_ENTRY;
		 point < TEST_RETRY_BANK_ENTRY + TEST_BANK_SAMPLES; point++) {
		points[point].bank_support = 1;
	}
	reject_receiver = 1;
	assert(unit_pilot_begin(&source));
	unit_pilot_work(TEST_RETRY_BEFORE_FAILURE);
	assert(unit_pilot_state->trial && !unit_pilot_state->stats.failed_trials);
	/* The bank's supported exit is reached inside a still-unproved barrel
	 * trial. It cannot release controls held from before that trial's start. */
	assert(!unit_pilot_state->bank.pending && unit_pilot_state->approach_bank.pending);
	legacy_u32 held = unit_pilot_state->approach_bank.hold_steps;
	assert(unit_pilot_state->committed_steps == held);
	native_road_support = 0;
	unit_pilot_work(1);
	assert(unit_pilot_state->stats.failed_trials == 1);
	assert(unit_pilot_state->step_count == unit_pilot_state->approach_steps);
	unit_pilot_work(1);
	assert(unit_pilot_state->bank.pending && !unit_pilot_state->bank.stable_ticks);
	assert(unit_pilot_state->bank.hold_steps == held);
	assert(unit_pilot_state->committed_steps == held);
	native_road_support = 1;
	unit_pilot_work(TEST_RECEIVER_PENDING_TICKS);
	assert(unit_pilot_state->stats.accepted == 1);
	assert(!unit_pilot_state->bank.pending);
	assert(unit_pilot_state->committed_steps == unit_pilot_state->step_count);
	unit_pilot_stop();
}

static void test_recheck_waits_for_receiving_bank_exit(void)
{
	reset_fixture(1);
	state.playerstate.car_position.lx = state.playerstate.car_position.lz = -64;
	for (legacy_u32 point = TEST_BANK_ENTRY + TEST_BANK_SAMPLES;
		 point < TEST_RECEIVER_CLEARANCE_FIRST; point++) {
		points[point].bank_support = 1;
	}
	for (legacy_u32 point = TEST_BANK_RECEIVER_END + 1U; point < route.count; point++) {
		points[point].route_index = 0;
	}
	assert(unit_pilot_begin(&source));
	assert(unit_pilot_recheck_begin(&source, 0, 1));
	for (legacy_u32 tick = 0; tick < TEST_BANK_RECHECK_BOUND && !unit_pilot_state->stats.accepted;
		 tick++) {
		unit_pilot_work(1);
	}
	assert(unit_pilot_state->stats.accepted == 1 && !unit_pilot_state->trial);
	assert(unit_pilot_state->bank.pending && unit_pilot_state->stats.active);
	assert(unit_pilot_state->recheck_status == 0);
	assert(unit_pilot_recheck_status(0) == 0);
	legacy_u32 held = unit_pilot_state->committed_steps;
	native_road_support = 0;
	unit_pilot_work(TEST_BANK_UNSUPPORTED_EXIT_TICKS);
	assert(unit_pilot_state->bank.pending && unit_pilot_state->stats.active);
	assert(unit_pilot_state->committed_steps == held);
	assert(unit_pilot_state->recheck_status == 0);
	assert(unit_pilot_recheck_status(0) == 0);
	native_road_support = 1;
	unit_pilot_work(TEST_RECEIVER_STABLE_TICKS);
	assert(!unit_pilot_state->bank.pending && !unit_pilot_state->stats.active);
	assert(unit_pilot_state->committed_steps > held);
	assert(unit_pilot_recheck_status(unit_pilot_state->handoff_frame) == 1);
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
	assert(unit_pilot_state->step_count == 10 && unit_pilot_state->committed_steps == 10);
	unit_pilot_stop();
}

static void test_clearance_group_boundaries(void)
{
	for (legacy_u8 clearance = 0; clearance <= 1U; clearance++) {
		configure_runup(TEST_GROUP_NEXT_FIRST, UNIT_STUNT_PROTECTED);
		for (legacy_u32 index = TEST_GROUP_PRIMARY_FIRST;
			 index < TEST_GROUP_PRIMARY_FIRST + TEST_GROUP_PIECE_SAMPLES; index++) {
			points[index].stunt = UNIT_STUNT_RAMP;
		}
		for (legacy_u32 index = TEST_GROUP_PRIMARY_FIRST + TEST_GROUP_PIECE_SAMPLES;
			 index < TEST_GROUP_NEXT_FIRST; index++) {
			points[index].route_index = 0;
		}
		for (legacy_u32 index = TEST_GROUP_CONNECTOR_FIRST;
			 index < TEST_GROUP_CONNECTOR_FIRST + TEST_GROUP_PIECE_SAMPLES; index++) {
			points[index].route_index = TEST_GROUP_CONNECTOR_PIECE;
			points[index].stunt = UNIT_STUNT_PROTECTED;
			points[index].clearance_only = clearance;
		}
		assert(unit_pilot_begin(&source));
		unit_pilot_work(1);
		/* The crest remains inside the jump's receiving trial. It must not
		 * bridge the four-tile span to an otherwise independent later loop.
		 * A mandatory middle stunt still retains the existing coupled search. */
		legacy_u32 last = clearance ? TEST_GROUP_CONNECTOR_FIRST : TEST_GROUP_NEXT_FIRST;
		assert(unit_pilot_state->last == last + TEST_GROUP_PIECE_SAMPLES - 1U);
	}
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

static void release_learning(struct UNIT_LEARNING_DATA *data)
{
	free(data->pieces);
	free(data->commands);
	memset(data, 0, sizeof(*data));
}

static void test_learning_incumbent_and_revalidation(void)
{
	struct UNIT_LEARNING_DATA data = {0};
	struct CSRPL_OPPONENT_COMMAND command;
	struct UNIT_PILOT_STATS stats;
	reset_fixture(0);
	learning_motion = finish_event = 1;
	struct GAMESTATE before = state;
	assert(unit_pilot_learning_begin(&source, &data));
	unit_pilot_learning_work(&source, 64);
	assert(!data.command_count && data.native_samples == 1);
	unit_pilot_learning_work(&source, 64);
	assert(data.best_frames == 127 && data.command_count == 127 && data.native_samples == 2);
	unit_pilot_learning_work(&source, 64);
	assert(data.best_frames == 64 && data.command_count == 64 && data.native_samples == 3);
	unit_pilot_learning_work(&source, 1);
	assert(data.best_frames == 64 && data.native_samples == 4);
	unit_pilot_learning_work(&source, 64);
	unit_pilot_learning_work(&source, 64);
	assert(data.best_frames == 64 && data.native_samples == 5);
	assert(unit_pilot_learning_finish());
	unit_pilot_stats(&stats);
	assert(stats.completed_lap && unit_pilot_state->step_count == 64);
	assert(unit_pilot_state->seed->profile.lane == 1.0);
	assert(unit_pilot_command(&state.opponentstate, 0, &command) && command.input == 1);
	assert_live_unchanged(&before);

	reset_fixture(0);
	learning_motion = finish_event = 1;
	assert(unit_pilot_learning_begin(&source, &data));
	unit_pilot_learning_work(&source, 1);
	assert(unit_pilot_learning_finish());
	assert(data.best_frames == 64 && !unit_pilot_continuation_pending());
	assert(!unit_pilot_command(&state.opponentstate, 0, &command));
	assert(!unit_pilot_state->committed_steps);
	assert_live_unchanged(&before);

	reset_fixture(0);
	learning_motion = finish_event = 1;
	assert(unit_pilot_learning_begin(&source, &data));
	assert(!unit_pilot_command(&state.opponentstate, 0, &command));
	unit_pilot_learning_work(&source, 1);
	assert(replayed_commands == 1 && !unit_pilot_command(&state.opponentstate, 0, &command));
	unit_pilot_learning_work(&source, 63);
	assert(replayed_commands == 64 && data.best_frames == 64 && data.native_samples == 6);
	unit_pilot_learning_work(&source, 1);
	assert(data.best_frames == 64 && data.native_samples == 6);
	assert(unit_pilot_learning_finish());
	assert(unit_pilot_state->stats.completed_lap);
	assert(unit_pilot_command(&state.opponentstate, 0, &command) && command.input == 1);
	assert_live_unchanged(&before);

	reset_fixture(0);
	learning_motion = finish_event = 1;
	rejected_replay_frame = 2;
	assert(unit_pilot_learning_begin(&source, &data));
	unit_pilot_learning_work(&source, 64);
	assert(replayed_commands == 2 && !data.best_frames && !data.command_count);
	assert(!unit_pilot_command(&state.opponentstate, 0, &command));
	unit_pilot_learning_finish();
	unit_pilot_stop();
	release_learning(&data);
}

static void test_learning_partial_and_native_continuation(void)
{
	struct UNIT_LEARNING_DATA data = {0};
	struct CSRPL_OPPONENT_COMMAND command;
	reset_fixture(1);
	finish_event = reject_fast = 1;
	data.pieces = calloc(route.verified_count, sizeof(*data.pieces));
	assert(data.pieces);
	data.piece_count = route.verified_count;
	data.pieces[1].speed = 100.0;
	data.pieces[3].speed = 150.0;
	assert(unit_pilot_learning_begin(&source, &data));
	unit_pilot_learning_work(&source, 55);
	assert(!data.command_count);
	assert(unit_pilot_learning_finish());
	assert(data.piece_count == route.verified_count && data.pieces[1].speed == 99.0);
	assert(unit_pilot_state->stats.failed_trials == 1);
	assert(data.pieces[3].speed == 150.0 && speeds[3] == 0.0);
	assert(!data.best_frames && !data.command_count && unit_pilot_continuation_pending());
	assert(unit_pilot_command(&state.opponentstate, 0, &command));
	assert(!unit_pilot_continue_work(UNIT_PILOT_CONTINUATION_TICKS));
	assert(data.best_frames == 70 && data.command_count == 70);
	unit_pilot_stop();
	release_learning(&data);
}

static void test_learning_recheck_suffix_is_not_a_lap(void)
{
	struct UNIT_LEARNING_DATA data = {0};
	struct CSRPL_OPPONENT_COMMAND command;
	reset_fixture(1);
	finish_event = 1;
	assert(unit_pilot_learning_begin(&source, &data));
	unit_pilot_learning_work(&source, 64);
	unit_pilot_learning_work(&source, 64);
	assert(unit_pilot_learning_finish());
	assert(data.best_frames == 70 && unit_pilot_state->learning_full_start);
	legacy_u32 full_frames = data.best_frames;
	struct CSRPL_OPPONENT_COMMAND *incumbent = data.commands;
	/* Resume beyond the starting grid so finishing this forecast would be a
	 * shorter suffix, despite the car legitimately reaching the finish. */
	state.opponentstate.car_reserved_route_word1 = 20;
	state.opponentstate.car_position.lz = 2000 * 64;
	state.playerstate.car_position.lx = state.playerstate.car_position.lz = -64;
	assert(unit_pilot_recheck_begin(&source, 0, 1));
	assert(!unit_pilot_state->learning_full_start);
	unit_pilot_work(64);
	assert(unit_pilot_recheck_status(32) == 1);
	state.opponentstate = unit_pilot_state->steps[0].car;
	struct GAMESTATE before = state;
	assert(unit_pilot_command(&state.opponentstate, 32, &command));
	assert(!unit_pilot_state->recheck && unit_pilot_continuation_pending());
	assert(!unit_pilot_continue_work(UNIT_PILOT_CONTINUATION_TICKS));
	assert(unit_pilot_state->stats.completed_lap && unit_pilot_state->step_count < full_frames);
	assert(data.best_frames == full_frames && data.command_count == full_frames);
	assert(data.commands == incumbent && data.commands[0].route_point == 0);
	assert_live_unchanged(&before);
	unit_pilot_stop();
	release_learning(&data);
}

static void test_learning_interrupted_continuation_progress(void)
{
	for (legacy_u8 save_before_recheck = 0; save_before_recheck < 2; save_before_recheck++) {
		struct UNIT_LEARNING_DATA data = {0};
		struct CSRPL_OPPONENT_COMMAND command;
		reset_fixture(1);
		finish_event = 1;
		route.finish_begin = route.count - 1U;
		points[TEST_LEARNING_LATER_FIRST].stunt = UNIT_STUNT_PROTECTED;
		points[TEST_LEARNING_LATER_FIRST + 1U].stunt = UNIT_STUNT_PROTECTED;
		for (legacy_u32 point = TEST_LEARNING_LATER_FIRST + 2U; point < route.count; point++) {
			points[point].route_index = point <= TEST_LEARNING_LATER_RECEIVER_LAST ? 0 : 2;
		}
		data.pieces = calloc(route.verified_count, sizeof(*data.pieces));
		assert(data.pieces);
		data.piece_count = route.verified_count;
		data.pieces[2].speed = 80.0;
		assert(unit_pilot_learning_begin(&source, &data));
		unit_pilot_learning_work(&source, TEST_RECEIVER_PENDING_TICKS);
		assert(unit_pilot_learning_finish());
		assert(data.pieces[1].speed == 100.0 && data.pieces[3].speed == 0.0);
		assert(data.pieces[2].speed == 80.0 && speeds[2] == 0.0);
		assert(unit_pilot_command(&state.opponentstate, 0, &command));
		struct GAMESTATE before = state;
		assert(unit_pilot_continue_work(TEST_LEARNING_LIVE_PROGRESS - TEST_RECEIVER_PENDING_TICKS));
		assert(unit_pilot_state->stats.accepted == 2 && !unit_pilot_state->stats.completed_lap);
		assert(speeds[3] == 100.0 && data.pieces[3].speed == 0.0);
		if (save_before_recheck) {
			unit_pilot_learning_save_progress();
			assert(data.pieces[3].speed == 100.0 && !data.command_count && !data.best_frames);
		}
		assert_live_unchanged(&before);
		state.playerstate.car_position.lx = state.playerstate.car_position.lz = -64;
		reject_fast = 1;
		assert(unit_pilot_recheck_begin(&source, 0, 3));
		/* Recheck initialization also saves genuine earlier progress before
		 * permanently clearing this preview's portable-lap eligibility. */
		assert(data.pieces[3].speed == 100.0 && !unit_pilot_state->learning_full_start);
		for (legacy_u32 chunk = 0; chunk < 3; chunk++) {
			unit_pilot_work(64);
		}
		assert(unit_pilot_recheck_status(32) == 1 && speeds[3] == 99.0);
		unit_pilot_learning_save_progress();
		assert(data.pieces[3].speed == 100.0 && data.pieces[2].speed == 80.0);
		assert(!data.command_count && !data.best_frames);
		unit_pilot_stop();
		release_learning(&data);
	}
}

legacy_int main(void)
{
	test_append_and_isolation();
	test_cancel_divergence();
	test_bounded_work_and_cleanup();
	test_ordinary_prefix();
	test_rolling_recheck();
	test_receiving_road_geometry();
	test_clearance_receiving_road();
	test_bank_controls_wait_for_supported_receiver();
	test_bank_admission_survives_failed_hard_trial();
	test_recheck_waits_for_receiving_bank_exit();
	test_two_chunk_frame_budget();
	test_pulse_retry_and_expired_handoff();
	test_ramp_runup_and_group_boundaries();
	test_clearance_group_boundaries();
	test_ramp_window_search();
	test_ramp_window_deadline_resume();
	test_secondary_ramp_window_search();
	test_recheck_handoff_continuation();
	test_learning_incumbent_and_revalidation();
	test_learning_partial_and_native_continuation();
	test_learning_recheck_suffix_is_not_a_lap();
	test_learning_interrupted_continuation_progress();
	return 0;
}
