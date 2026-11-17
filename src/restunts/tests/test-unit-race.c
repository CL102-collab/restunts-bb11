#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "../genghost/simulation.h"
#include "../genghost/extended_simulation.h"
#include "../c/driving_rules.h"
#include "../c/externs.h"
#include "../c/game_input.h"
#include "../c/crash_state.h"
#include "../c/owoot/owoot.h"
#include "../c/state_internal.h"
#include "../c/race/race_route.h"
#include "../c/track/track_objects.h"
#include "../c/opponent/opponent.h"
#include "../c/opponent/unit_opponent.h"
#include "../c/opponent/unit_collision.h"
#include "../c/opponent/unit_pilot.h"
#include "../c/opponent/unit_learning_cache.h"
#include "../c/opponent/unit_pilot_pulses.h"
#include "../c/opponent/unit_opponent_internal.h"
#include "../c/extended/extended_track.h"
#include "../c/extended/extended_owoot_internal.h"
#include "../c/extended/extended_replay_runtime.h"

#undef memcpy
#undef strlen
#undef printf

#define UNIT_BENCHMARK_DEFAULT_FRAMES 12000U
#define UNIT_BENCHMARK_MAXIMUM_TRACK_BYTES (16U * 1024U * 1024U)
#define UNIT_BENCHMARK_WORLD_SCALE 64.0
#define UNIT_BENCHMARK_SPEED_SCALE 256.0
#define UNIT_BENCHMARK_PARK_POSITION 128L
#define UNIT_BENCHMARK_PARK_SCALE 64L
#define UNIT_BENCHMARK_CORNER_ANGLE 8
#define UNIT_BENCHMARK_SPEED_MINIMUM 1U
#define UNIT_BENCHMARK_HASH_OFFSET UINT64_C(14695981039346656037)
#define UNIT_BENCHMARK_HASH_PRIME UINT64_C(1099511628211)
#define UNIT_BENCHMARK_DEBUG_POINTS 5000U
#define UNIT_BENCHMARK_FAILURE_TRACE_POINTS 128U
/* Serialized roll momentum word from extended_stateply.c. */
#define UNIT_BENCHMARK_ROLL_VELOCITY_WORD 0U
#define UNIT_BENCHMARK_HUMAN_DISTANCE 1024.0
#define UNIT_BENCHMARK_HUMAN_LATERAL 200.0
#define UNIT_BENCHMARK_HUMAN_START_SECONDS 2U
#define UNIT_BENCHMARK_GRID_DRIVE_FRAMES 60U
#define UNIT_BENCHMARK_BLOCKED_GAP 128.0
#define UNIT_BENCHMARK_SLOW_HUMAN_SPEED 20.0
#define UNIT_BENCHMARK_SLOW_HUMAN_HYSTERESIS 2.0
#define UNIT_BENCHMARK_PASS_CLEARANCE 16.0
#define UNIT_BENCHMARK_TRAFFIC_HEIGHT 90.0
#define UNIT_BENCHMARK_TRAFFIC_STALL_SECONDS 5U
#define UNIT_BENCHMARK_OVERTAKE_SECONDS 20U
#define UNIT_BENCHMARK_MERGE_LATERAL 24.0
#define UNIT_BENCHMARK_MERGE_YAW 0.15
#define UNIT_BENCHMARK_MERGE_STABLE_DIVISOR 2U
#define UNIT_BENCHMARK_SIDE_COMMIT_DISTANCE 24.0
#define UNIT_BENCHMARK_MAX_HUMAN_DISTANCE (4.0 * TRACK_TILE_SIZE)
#define UNIT_BENCHMARK_NO_HUMAN_DISTANCE 1000000.0
#define UNIT_BENCHMARK_NS_PER_SECOND UINT64_C(1000000000)
#define UNIT_BENCHMARK_NS_PER_MICROSECOND 1000.0
#define UNIT_BENCHMARK_NS_PER_MILLISECOND 1000000.0
#define UNIT_BENCHMARK_MAX_PLANNER_YIELD_MS 50U
#define UNIT_BENCHMARK_STALL_SPEED 3.0
#define UNIT_BENCHMARK_STALL_START_SECONDS 2U
#define UNIT_BENCHMARK_STALL_SECONDS 1U
#define UNIT_BENCHMARK_PLANNING_HOLD_TICKS                                                         \
	(UNIT_PILOT_WAIT_TICKS + UNIT_OPPONENT_RECHECK_SETTLED_TICKS)
#define UNIT_BENCHMARK_STALL_PROGRESS_LENGTHS 2.0
#define UNIT_BENCHMARK_STALL_TEST_DISTANCE 100.0
#define UNIT_BENCHMARK_FINISH_TEST_OFFSET 256.0
#define UNIT_BENCHMARK_REPLAY_RULE_ARGUMENTS 8U

struct UNIT_BENCHMARK_STALL {
	struct VECTORLONG anchor;
	legacy_u32 credited_cursor, credit_used, planning_frames;
	legacy_u32 raw_frames, max_raw_frames, unexplained_frames, max_unexplained_frames;
	legacy_u8 have_credit;
};

static void benchmark_stall_observe(struct UNIT_BENCHMARK_STALL *stall, legacy_u8 eligible,
									legacy_u8 stopped, const struct UNIT_PLANNING_HOLD *hold,
									const struct VECTORLONG *position, legacy_f64 progress_distance)
{
	if (!eligible || !stopped) {
		stall->raw_frames = stall->unexplained_frames = 0;
		return;
	}
	stall->raw_frames++;
	if (stall->raw_frames > stall->max_raw_frames) {
		stall->max_raw_frames = stall->raw_frames;
	}
	legacy_f64 dx = ((legacy_f64)position->lx - stall->anchor.lx) / UNIT_BENCHMARK_WORLD_SCALE;
	legacy_f64 dy = ((legacy_f64)position->ly - stall->anchor.ly) / UNIT_BENCHMARK_WORLD_SCALE;
	legacy_f64 dz = ((legacy_f64)position->lz - stall->anchor.lz) / UNIT_BENCHMARK_WORLD_SCALE;
	if (hold->active && (!stall->have_credit || (hold->route_cursor > stall->credited_cursor &&
												 hypot(hypot(dx, dy), dz) >= progress_distance))) {
		/* Toggling the hold, retrying or changing its target cannot replenish
		 * the allowance. A later guide cursor also needs actual body travel. */
		stall->anchor = *position;
		stall->credited_cursor = hold->route_cursor;
		stall->credit_used = 0;
		stall->have_credit = 1;
	}
	if (hold->active && stall->credit_used < UNIT_BENCHMARK_PLANNING_HOLD_TICKS) {
		stall->credit_used++;
		stall->planning_frames++;
		/* A legitimate hold pauses, rather than erases, an unexplained stop. */
		return;
	}
	stall->unexplained_frames++;
	if (stall->unexplained_frames > stall->max_unexplained_frames) {
		stall->max_unexplained_frames = stall->unexplained_frames;
	}
}

static legacy_int benchmark_stall_failed(const struct UNIT_BENCHMARK_STALL *stall,
										 legacy_u16 frame_rate)
{
	return stall->max_unexplained_frames >= UNIT_BENCHMARK_STALL_SECONDS * frame_rate;
}

static void benchmark_stall_test_ticks(struct UNIT_BENCHMARK_STALL *stall,
									   const struct UNIT_PLANNING_HOLD *hold,
									   const struct VECTORLONG *position, legacy_u32 ticks)
{
	for (legacy_u32 tick = 0; tick < ticks; tick++) {
		benchmark_stall_observe(stall, 1, 1, hold, position, UNIT_BENCHMARK_STALL_TEST_DISTANCE);
	}
}

static void test_stall_classification(void)
{
	for (legacy_u16 frame_rate = GAME_FRAME_RATE_LOW; frame_rate <= GAME_FRAME_RATE_NORMAL;
		 frame_rate += GAME_FRAME_RATE_LOW) {
		legacy_u32 limit = UNIT_BENCHMARK_STALL_SECONDS * frame_rate;
		struct UNIT_BENCHMARK_STALL stall = {0};
		struct UNIT_PLANNING_HOLD hold = {.piece = 1};
		struct VECTORLONG position = {0};
		benchmark_stall_test_ticks(&stall, &hold, &position, limit);
		assert(benchmark_stall_failed(&stall, frame_rate) && stall.planning_frames == 0);

		stall = (struct UNIT_BENCHMARK_STALL){0};
		hold.active = 1;
		benchmark_stall_test_ticks(&stall, &hold, &position, UNIT_BENCHMARK_PLANNING_HOLD_TICKS);
		assert(!benchmark_stall_failed(&stall, frame_rate));
		assert(stall.planning_frames == UNIT_BENCHMARK_PLANNING_HOLD_TICKS);
		assert(stall.max_raw_frames == UNIT_BENCHMARK_PLANNING_HOLD_TICKS &&
			   stall.max_unexplained_frames == 0);
		benchmark_stall_test_ticks(&stall, &hold, &position, limit);
		assert(benchmark_stall_failed(&stall, frame_rate));
		assert(stall.max_raw_frames == UNIT_BENCHMARK_PLANNING_HOLD_TICKS + limit &&
			   stall.max_unexplained_frames == limit);

		/* A retry after apparent motion still shares the spent allowance.
		 * Neither a new target nor a cursor change at the same position renews it. */
		stall = (struct UNIT_BENCHMARK_STALL){0};
		benchmark_stall_test_ticks(&stall, &hold, &position, UNIT_BENCHMARK_PLANNING_HOLD_TICKS);
		hold.active = 0;
		benchmark_stall_observe(&stall, 1, 0, &hold, &position, UNIT_BENCHMARK_STALL_TEST_DISTANCE);
		hold.active = 1;
		hold.piece++;
		hold.route_cursor++;
		benchmark_stall_test_ticks(&stall, &hold, &position, limit);
		assert(benchmark_stall_failed(&stall, frame_rate));
		assert(stall.planning_frames == UNIT_BENCHMARK_PLANNING_HOLD_TICKS);

		/* Travel without advancing through the immutable guide is insufficient. */
		stall = (struct UNIT_BENCHMARK_STALL){0};
		benchmark_stall_test_ticks(&stall, &hold, &position, UNIT_BENCHMARK_PLANNING_HOLD_TICKS);
		position.ly += UNIT_BENCHMARK_STALL_TEST_DISTANCE * UNIT_BENCHMARK_WORLD_SCALE;
		benchmark_stall_test_ticks(&stall, &hold, &position, limit);
		assert(benchmark_stall_failed(&stall, frame_rate));

		/* Real 3-D travel together with forward guide progress permits the
		 * next bounded recheck, at either supported frame rate. */
		stall = (struct UNIT_BENCHMARK_STALL){0};
		benchmark_stall_test_ticks(&stall, &hold, &position, UNIT_BENCHMARK_PLANNING_HOLD_TICKS);
		position.ly += UNIT_BENCHMARK_STALL_TEST_DISTANCE * UNIT_BENCHMARK_WORLD_SCALE;
		hold.route_cursor++;
		benchmark_stall_observe(&stall, 1, 0, &hold, &position, UNIT_BENCHMARK_STALL_TEST_DISTANCE);
		benchmark_stall_test_ticks(&stall, &hold, &position, UNIT_BENCHMARK_PLANNING_HOLD_TICKS);
		assert(!benchmark_stall_failed(&stall, frame_rate));
		assert(stall.planning_frames == 2U * UNIT_BENCHMARK_PLANNING_HOLD_TICKS);

		/* Do not let a credited interval erase preceding unexplained ticks. */
		stall = (struct UNIT_BENCHMARK_STALL){0};
		hold.active = 0;
		benchmark_stall_test_ticks(&stall, &hold, &position, limit / 2U);
		hold.active = 1;
		benchmark_stall_test_ticks(&stall, &hold, &position, UNIT_BENCHMARK_PLANNING_HOLD_TICKS);
		hold.active = 0;
		benchmark_stall_test_ticks(&stall, &hold, &position, limit - limit / 2U);
		assert(benchmark_stall_failed(&stall, frame_rate));
	}
}

static void test_finish_crossing(void)
{
	struct UNIT_ROUTE route = {0};
	assert(unit_route_build(&route));
	struct UNIT_ROUTE_POINT finish = route.points[route.finish_begin];
	unit_route_free(&route);
	route.points = &finish;
	route.count = 1U;
	legacy_f64 center_x = (legacy_u8)start_finish_column * TRACK_TILE_SIZE + TRACK_TILE_HALF_SIZE;
	legacy_f64 center_z =
		(extended_track_height() - (legacy_u8)start_finish_row - 1) * TRACK_TILE_SIZE +
		TRACK_TILE_HALF_SIZE;
	legacy_f64 sine = sin(track_angle * UNIT_ANGLE_RADIANS);
	legacy_f64 cosine = cos(track_angle * UNIT_ANGLE_RADIANS);
	struct CARSTATE car = {0};
	car.car_lap_count = 1;
	car.car_previous_position.lx = (legacy_s32)llround(
		(center_x - sine * UNIT_BENCHMARK_FINISH_TEST_OFFSET) * UNIT_POSITION_SCALE);
	car.car_previous_position.lz = (legacy_s32)llround(
		(center_z - cosine * UNIT_BENCHMARK_FINISH_TEST_OFFSET) * UNIT_POSITION_SCALE);
	car.car_position.lx = (legacy_s32)llround(
		(center_x + sine * UNIT_BENCHMARK_FINISH_TEST_OFFSET) * UNIT_POSITION_SCALE);
	car.car_position.lz = (legacy_s32)llround(
		(center_z + cosine * UNIT_BENCHMARK_FINISH_TEST_OFFSET) * UNIT_POSITION_SCALE);
	car.car_position.ly =
		(legacy_s32)((finish.y + UNIT_BENCHMARK_FINISH_TEST_OFFSET) * UNIT_POSITION_SCALE);
	/* Native timing accepts an airborne crossing; an explicitly placed
	 * finish on a stacked custom track must still reject the wrong level. */
	finish.height_unspecified = 1U;
	assert(unit_route_finished(&route, &car));
	finish.height_unspecified = 0U;
	assert(!unit_route_finished(&route, &car));
	car.car_position.ly = (legacy_s32)(finish.y * UNIT_POSITION_SCALE);
	assert(unit_route_finished(&route, &car));
	/* Height-independent timing must retain the crossing direction and width. */
	finish.height_unspecified = 1U;
	struct VECTORLONG previous = car.car_previous_position;
	car.car_previous_position = car.car_position;
	car.car_position = previous;
	assert(!unit_route_finished(&route, &car));
	car.car_position = car.car_previous_position;
	car.car_previous_position = previous;
	legacy_f64 lateral = finish.half_width + UNIT_BENCHMARK_FINISH_TEST_OFFSET;
	car.car_position.lx += (legacy_s32)llround(cosine * lateral * UNIT_POSITION_SCALE);
	car.car_position.lz -= (legacy_s32)llround(sine * lateral * UNIT_POSITION_SCALE);
	assert(!unit_route_finished(&route, &car));
}

enum UNIT_BENCHMARK_HUMAN {
	UNIT_BENCHMARK_HUMAN_PARKED,
	UNIT_BENCHMARK_HUMAN_STATIONARY,
	UNIT_BENCHMARK_HUMAN_ACCELERATING,
	UNIT_BENCHMARK_HUMAN_CROSSING,
	UNIT_BENCHMARK_HUMAN_GRID,
	UNIT_BENCHMARK_HUMAN_GRID_THEN_PARK,
	UNIT_BENCHMARK_HUMAN_BLOCKED,
	UNIT_BENCHMARK_HUMAN_SLOW
};

struct UNIT_BENCHMARK_TRAFFIC {
	legacy_f64 ahead, lateral, rear_clearance, road_lateral;
	legacy_f64 human_peak_speed, slow_speed_sum;
	legacy_u32 blocked_frames, max_blocked_frames, contact_frames, side_changes;
	legacy_u32 pass_frame, merge_frame, merge_stable, slow_frames;
	legacy_s8 side;
	legacy_u8 passed, merged;
};

static legacy_f64 benchmark_body_extent(const struct SIMD *simd, const struct CARSTATE *car,
										legacy_f64 axis)
{
	legacy_f64 width = fabs((legacy_f64)simd->collide_points[0].px);
	legacy_f64 length = fabs((legacy_f64)simd->collide_points[1].px);
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		width = fmax(width, fabs(simd->wheel_coords[wheel].x / UNIT_POSITION_SCALE));
		length = fmax(length, fabs(simd->wheel_coords[wheel].z / UNIT_POSITION_SCALE));
	}
	return unit_collision_projected_extent(length, width,
										   -car->car_rotate.x * UNIT_ANGLE_RADIANS - axis);
}

static legacy_f64 benchmark_number(const legacy_char *text)
{
	legacy_char *end = NULL;
	legacy_f64 value = strtod(text, &end);
	assert(end != text && *end == '\0' && isfinite(value));
	return value;
}

static void benchmark_traffic_observe(struct UNIT_BENCHMARK_TRAFFIC *traffic, legacy_u32 frame,
									  legacy_u16 frame_rate, legacy_int slow, legacy_f64 axis,
									  legacy_f64 road_x, legacy_f64 road_z)
{
	const struct CARSTATE *unit = &state.opponentstate, *human = &state.playerstate;
	legacy_f64 forward_x = sin(axis), forward_z = cos(axis);
	legacy_f64 dx = (unit->car_position.lx - human->car_position.lx) / UNIT_POSITION_SCALE;
	legacy_f64 dz = (unit->car_position.lz - human->car_position.lz) / UNIT_POSITION_SCALE;
	legacy_f64 unit_x = unit->car_position.lx / UNIT_POSITION_SCALE;
	legacy_f64 unit_z = unit->car_position.lz / UNIT_POSITION_SCALE;
	legacy_f64 human_speed = human->car_actual_speed / UNIT_SPEED_SCALE;
	legacy_f64 length = benchmark_body_extent(&simd_opponent, unit, axis) +
						benchmark_body_extent(&simd_player, human, axis);
	traffic->ahead = dx * forward_x + dz * forward_z;
	traffic->lateral = dx * forward_z - dz * forward_x;
	traffic->rear_clearance = traffic->ahead - length;
	traffic->road_lateral = (unit_x - road_x) * forward_z - (unit_z - road_z) * forward_x;
	traffic->human_peak_speed = fmax(traffic->human_peak_speed, human_speed);
	if (slow && frame >= UNIT_BENCHMARK_HUMAN_START_SECONDS * frame_rate) {
		traffic->slow_speed_sum += human_speed;
		traffic->slow_frames++;
	}
	if (unit->car_collision_latch == CAR_COLLISION_LATCH_SET ||
		human->car_collision_latch == CAR_COLLISION_LATCH_SET) {
		traffic->contact_frames++;
	}
	legacy_int same_level = fabs((unit->car_position.ly - human->car_position.ly) /
								 UNIT_POSITION_SCALE) < UNIT_BENCHMARK_TRAFFIC_HEIGHT;
	if (!traffic->passed && same_level && traffic->rear_clearance > UNIT_BENCHMARK_PASS_CLEARANCE) {
		traffic->passed = 1;
		traffic->pass_frame = frame;
	}
	if (!traffic->passed && same_level && fabs(traffic->ahead) < TRACK_TILE_SIZE &&
		fabs(traffic->lateral) > UNIT_BENCHMARK_SIDE_COMMIT_DISTANCE) {
		legacy_s8 side = traffic->lateral > 0.0 ? 1 : -1;
		if (traffic->side && side != traffic->side) {
			traffic->side_changes++;
		}
		traffic->side = side;
	}
	legacy_f64 moving_speed = hypot(unit->car_position.lx - unit->car_previous_position.lx,
									unit->car_position.lz - unit->car_previous_position.lz) /
							  UNIT_POSITION_SCALE * frame_rate / UNIT_WORLD_PER_MPH;
	if (frame >= UNIT_BENCHMARK_STALL_START_SECONDS * frame_rate && !traffic->passed &&
		same_level && hypot(dx, dz) < TRACK_TILE_SIZE &&
		moving_speed < UNIT_BENCHMARK_STALL_SPEED) {
		traffic->blocked_frames++;
		if (traffic->blocked_frames > traffic->max_blocked_frames) {
			traffic->max_blocked_frames = traffic->blocked_frames;
		}
	} else {
		traffic->blocked_frames = 0;
	}
	/* Dedicated overtake cases use the initial straight's road axis. A merge
	 * requires rear-body clearance and a settled return to that road line. */
	legacy_f64 yaw = remainder(-unit->car_rotate.x * UNIT_ANGLE_RADIANS - axis,
							   2.0 * ANGLE_HALF_TURN * UNIT_ANGLE_RADIANS);
	if (traffic->passed && same_level && traffic->rear_clearance > UNIT_BENCHMARK_PASS_CLEARANCE &&
		fabs(traffic->road_lateral) <= UNIT_BENCHMARK_MERGE_LATERAL &&
		fabs(yaw) <= UNIT_BENCHMARK_MERGE_YAW) {
		traffic->merge_stable++;
	} else {
		traffic->merge_stable = 0;
	}
	if (!traffic->merged &&
		traffic->merge_stable >= frame_rate / UNIT_BENCHMARK_MERGE_STABLE_DIVISOR) {
		traffic->merged = 1;
		traffic->merge_frame = frame;
	}
}

static legacy_u64 state_hash(void)
{
	legacy_u8 bytes[GAMESTATE_SERIALIZED_SIZE];
	legacy_s8 seed[GAMESTATE_RANDOM_SEED_SIZE];
	assert(gamestate_serialize(bytes, &state) == sizeof(bytes));
	get_kevinrandom_seed(seed);
	legacy_u64 hash = UNIT_BENCHMARK_HASH_OFFSET;
	for (size_t index = 0; index < sizeof(bytes); index++) {
		hash = (hash ^ bytes[index]) * UNIT_BENCHMARK_HASH_PRIME;
	}
	for (size_t index = 0; index < sizeof(seed); index++) {
		hash = (hash ^ (legacy_u8)seed[index]) * UNIT_BENCHMARK_HASH_PRIME;
	}
	/* input_frame is the live writer cursor; silent playback has no writer. */
	const struct EXTENDED_REPLAY_CLOCKS *clocks = &extended_replay_clocks;
	const struct RACE_PENALTY_STATE *rules = &clocks->unit_penalty.route;
	const legacy_u32 fields[] = {clocks->frame,
								 clocks->player_finish,
								 clocks->opponent_finish,
								 clocks->player_end,
								 clocks->penalty,
								 clocks->unit_penalty.frames,
								 clocks->unit_penalty.initialized,
								 (legacy_u32)rules->confirmed_route,
								 (legacy_u32)rules->previous_route,
								 (legacy_u32)rules->last_skipped,
								 (legacy_u32)rules->minimum_column,
								 (legacy_u32)rules->maximum_column,
								 (legacy_u32)rules->minimum_row,
								 (legacy_u32)rules->maximum_row,
								 (legacy_u32)rules->status,
								 (legacy_u32)rules->confirmations,
								 (legacy_u32)rules->laps,
								 clocks->checkpoints.initialized};
	for (size_t index = 0; index < sizeof(fields) / sizeof(fields[0]); index++) {
		hash = (hash ^ fields[index]) * UNIT_BENCHMARK_HASH_PRIME;
	}
	for (legacy_u32 car = 0; car < EXTENDED_CHECKPOINT_CAR_COUNT; car++) {
		for (legacy_u32 index = 0; index < EXTENDED_CHECKPOINT_VISITED_BYTES; index++) {
			hash = (hash ^ clocks->checkpoints.visited[car][index]) * UNIT_BENCHMARK_HASH_PRIME;
		}
		for (legacy_u32 color = 0; color < EXTENDED_CHECKPOINT_COLOR_COUNT; color++) {
			hash = (hash ^ clocks->checkpoints.remaining[car][color]) * UNIT_BENCHMARK_HASH_PRIME;
		}
		hash =
			(hash ^ (legacy_u32)clocks->checkpoints.previous[car].lx) * UNIT_BENCHMARK_HASH_PRIME;
		hash =
			(hash ^ (legacy_u32)clocks->checkpoints.previous[car].ly) * UNIT_BENCHMARK_HASH_PRIME;
		hash =
			(hash ^ (legacy_u32)clocks->checkpoints.previous[car].lz) * UNIT_BENCHMARK_HASH_PRIME;
	}
	return hash;
}

/* Follow the opponent through the native packed player penalty path without
 * changing the live race. Compare this separate history with UNIT accounting. */
static void shadow_penalty_step(struct GAMESTATE *shadow, legacy_u32 *penalty_frames)
{
	struct GENGHOST_SNAPSHOT live;
	genghost_sim_capture(&live);
	state = *shadow;
	state.playerstate = live.state.opponentstate;
	state.game_frame = live.state.game_frame;
	extended_replay_clocks.penalty = *penalty_frames;
	legacy_s8 previous_status = state.game_player_route_status;
	update_player_route_penalty();
	update_player_route_guidance(previous_status);
	*shadow = state;
	*penalty_frames = extended_replay_is_active() ? extended_replay_clocks.penalty
												  : (legacy_u16)state.game_penalty;
	genghost_sim_restore(&live);
}

static void debug_pilot_pulse(void)
{
	static legacy_u32 previous_attempts;
	struct UNIT_PILOT_PULSE_DIAGNOSTIC diagnostic;
	if (!unit_pilot_last_pulse(&diagnostic) || !diagnostic.active) {
		previous_attempts = 0;
		return;
	}
	if (diagnostic.attempts == previous_attempts) {
		return;
	}
	previous_attempts = diagnostic.attempts;
	const struct EXTENDED_BARREL_PARAMETERS *parameters = &diagnostic.parameters;
	fprintf(stderr,
			"pilot_pulse=%u frame=%u span=%u bank=%u fitness=%.3f progress=%u "
			"begin=%u duration=%u gap=%u counter=%u pedal_begin=%u pedal_duration=%u "
			"side=%u pedal=%u air_brake=%u relative_speed=%.6f relative_lateral=%.6f "
			"profile_speed=%.6f profile_offset=%.6f profile_exit_fraction=%.6f "
			"profile_exit_speed=%.6f profile_secondary_speed=%.6f "
			"profile_secondary_ramp_approach=%u\n",
			diagnostic.attempts, genghost_sim_frame(), diagnostic.span, diagnostic.bank,
			diagnostic.fitness, diagnostic.progress, parameters->begin, parameters->duration,
			parameters->gap, parameters->counter, parameters->pedal_begin,
			parameters->pedal_duration, parameters->side, parameters->pedal, parameters->air_brake,
			parameters->speed, parameters->lateral, diagnostic.profile.speed,
			diagnostic.profile.barrel_offset, diagnostic.profile.exit_fraction,
			diagnostic.profile.exit_speed, diagnostic.profile.secondary_speed,
			diagnostic.profile.secondary_ramp_approach);
}

static void debug_pilot_failure(legacy_u32 *previous_failures, FILE *trace, FILE *entry_trace)
{
	debug_pilot_pulse();
	struct UNIT_PILOT_STATS stats;
	unit_pilot_stats(&stats);
	if (stats.failed_trials == *previous_failures) {
		return;
	}
	*previous_failures = stats.failed_trials;
	struct CARSTATE car;
	legacy_f64 target;
	legacy_s16 piece;
	if (unit_pilot_last_failure(&car, &target, &piece)) {
		fprintf(stderr,
				"pilot_failure=%u frame=%u piece=%d event=%d target=%.3f exit_variant=%u "
				"secondary_piece=%d secondary_speed=%.3f barrel_offset=%.3f "
				"exit_fraction=%.3f exit_speed=%.3f barrel_progress=%u ramp_approach=%u "
				"secondary_ramp_approach=%u "
				"speed=%.3f last_speed=%.3f "
				"x=%.3f y=%.3f z=%.3f route=%d.%d yaw=%d pitch=%d roll=%d contact=%d\n",
				stats.failed_trials, genghost_sim_frame(), piece, stats.last_failure, target,
				stats.last_failure_exit_variant, stats.last_failure_secondary_piece,
				stats.last_failure_secondary_speed, stats.last_failure_barrel_offset,
				stats.last_failure_exit_fraction, stats.last_failure_exit_speed,
				stats.last_failure_barrel_progress, stats.last_failure_ramp_approach,
				stats.last_failure_secondary_ramp_approach,
				car.car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE,
				car.car_lastspeed / UNIT_BENCHMARK_SPEED_SCALE,
				car.car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
				car.car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
				car.car_position.lz / UNIT_BENCHMARK_WORLD_SCALE, car.car_route_index,
				car.car_route_point_index, car.car_rotate.x, car.car_rotate.y, car.car_rotate.z,
				car.car_sumSurfAllWheels);
		struct CARSTATE entry;
		if (unit_pilot_last_failure_entry(&entry)) {
			fprintf(stderr,
					"pilot_guide_entry=%u frame=%u piece=%d target=%.3f speed=%.3f "
					"x=%.3f y=%.3f z=%.3f yaw=%d pitch=%d roll=%d steering=%d "
					"velocity_heading_offset=%d contact=%d "
					"vx=%.3f vy=%.3f vz=%.3f\n",
					stats.failed_trials, genghost_sim_frame(), piece, target,
					entry.car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE,
					entry.car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
					entry.car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
					entry.car_position.lz / UNIT_BENCHMARK_WORLD_SCALE, entry.car_rotate.x,
					entry.car_rotate.y, entry.car_rotate.z, entry.car_steeringAngle,
					entry.car_velocity_heading_offset, entry.car_sumSurfAllWheels,
					(entry.car_position.lx - entry.car_previous_position.lx) * framespersec /
						UNIT_BENCHMARK_WORLD_SCALE,
					(entry.car_position.ly - entry.car_previous_position.ly) * framespersec /
						UNIT_BENCHMARK_WORLD_SCALE,
					(entry.car_position.lz - entry.car_previous_position.lz) * framespersec /
						UNIT_BENCHMARK_WORLD_SCALE);
		}
		if (entry_trace != NULL) {
			for (legacy_u32 after = 0; after < UNIT_BENCHMARK_FAILURE_TRACE_POINTS; after++) {
				if (!unit_pilot_failure_entry_car(after, &entry)) {
					break;
				}
				fprintf(entry_trace,
						"%u,%u,%u,%d,%.3f,%u,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%"
						"d,%d,%d,%d,%u,%.3f,%d,%d,%.3f,%.3f,%u,%u\n",
						stats.failed_trials, genghost_sim_frame(), after, piece, target,
						stats.last_failure_exit_variant, stats.last_failure_secondary_piece,
						stats.last_failure_secondary_speed,
						entry.car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
						entry.car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
						entry.car_position.lz / UNIT_BENCHMARK_WORLD_SCALE,
						entry.car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE, entry.car_route_index,
						entry.car_route_point_index, entry.car_rotate.x, entry.car_rotate.y,
						entry.car_rotate.z, entry.car_sumSurfAllWheels, entry.car_steeringAngle,
						entry.car_velocity_heading_offset, entry.car_is_accelerating,
						entry.car_is_braking,
						entry.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE],
						entry.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_TARGET],
						(legacy_u16)entry.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_LAUNCH],
						stats.last_failure_barrel_offset,
						entry.car_reserved_contact_state[UNIT_BENCHMARK_ROLL_VELOCITY_WORD],
						entry.car_sumSurfRearWheels, stats.last_failure_exit_fraction,
						stats.last_failure_exit_speed, stats.last_failure_ramp_approach,
						stats.last_failure_secondary_ramp_approach);
			}
		}
		if (trace != NULL) {
			for (legacy_s32 before = UNIT_BENCHMARK_FAILURE_TRACE_POINTS; before >= 0; before--) {
				if (!unit_pilot_failure_car((legacy_u32)before, &car)) {
					continue;
				}
				fprintf(trace,
						"%u,%u,%u,%d,%d,%.3f,%u,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%"
						"d,%d,%d,%d,%u,%.3f,%d,%d,%.3f,%.3f,%u,%u\n",
						stats.failed_trials, genghost_sim_frame(),
						stats.last_failure_frame - (legacy_u32)before, before, piece, target,
						stats.last_failure_exit_variant, stats.last_failure_secondary_piece,
						stats.last_failure_secondary_speed,
						car.car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
						car.car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
						car.car_position.lz / UNIT_BENCHMARK_WORLD_SCALE,
						car.car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE, car.car_route_index,
						car.car_route_point_index, car.car_rotate.x, car.car_rotate.y,
						car.car_rotate.z, car.car_sumSurfAllWheels, car.car_steeringAngle,
						car.car_is_accelerating, car.car_is_braking,
						car.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE],
						car.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_TARGET],
						(legacy_u16)car.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_LAUNCH],
						stats.last_failure_barrel_offset,
						car.car_reserved_contact_state[UNIT_BENCHMARK_ROLL_VELOCITY_WORD],
						car.car_sumSurfRearWheels, stats.last_failure_exit_fraction,
						stats.last_failure_exit_speed, stats.last_failure_ramp_approach,
						stats.last_failure_secondary_ramp_approach);
			}
		}
	}
}

static void debug_pilot_entries(void)
{
	const struct UNIT_ROUTE *route = unit_opponent_pilot_route();
	if (!route) {
		return;
	}
	for (legacy_u32 piece = 0; piece < route->verified_count; piece++) {
		struct CARSTATE car;
		legacy_f64 speed;
		if (!unit_pilot_expected_entry((legacy_s16)piece, &car, &speed)) {
			continue;
		}
		fprintf(stderr,
				"pilot_entry=%u target=%.3f speed=%.3f x=%.3f y=%.3f z=%.3f "
				"yaw=%d pitch=%d roll=%d steering=%d gear=%d rpm=%d ramp_approach=%u\n",
				piece, speed, car.car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE,
				car.car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
				car.car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
				car.car_position.lz / UNIT_BENCHMARK_WORLD_SCALE, car.car_rotate.x,
				car.car_rotate.y, car.car_rotate.z, car.car_steeringAngle, car.car_current_gear,
				car.car_currpm,
				route->verified_ramp_approaches ? route->verified_ramp_approaches[piece] : 0);
	}
}

/* A callback budget fixes logical search work; milliseconds remain its safety
 * ceiling. Complete native readiness can be an explicitly earlier goal. */
static legacy_int benchmark_preparation(legacy_u64 began, legacy_u32 milliseconds, legacy_u32 steps,
										legacy_int until_ready, legacy_int pilot_debug,
										legacy_u32 *failures, FILE *trace, FILE *entry_trace,
										legacy_u64 *elapsed)
{
	legacy_u32 completed = 0;
	while (SDL_GetTicks() - began < milliseconds && (!steps || completed < steps)) {
		unit_opponent_prepare_work();
		completed++;
		if (pilot_debug) {
			debug_pilot_failure(failures, trace, entry_trace);
		}
		if (until_ready && unit_pilot_ready()) {
			break;
		}
		SDL_Delay(1U);
	}
	legacy_int ready = unit_pilot_ready();
	if (steps && completed < steps && !(until_ready && ready)) {
		fprintf(stderr, "preparation safety budget expired after %u of %u callbacks\n", completed,
				steps);
		unit_opponent_stop();
		return 0;
	}
	if (!unit_opponent_prepare_end()) {
		fputs("preparation did not produce a route\n", stderr);
		return 0;
	}
	if (elapsed) {
		*elapsed = SDL_GetTicks() - began;
	}
	struct UNIT_PILOT_STATS selected;
	unit_pilot_stats(&selected);
	printf("fixed_work=%u preparation_steps=%u selected_preview_native_ticks=%u "
		   "preparation_ready=%u\n",
		   RESTUNTS_UNIT_TEST_FIXED_WORK ? 1U : 0U, completed, selected.native_ticks,
		   ready ? 1U : 0U);
	return 1;
}

static legacy_u8 *read_track(const legacy_char *path, legacy_uint *size)
{
	FILE *source = fopen(path, "rb");
	assert(source != NULL);
	assert(fseek(source, 0, SEEK_END) == 0);
	legacy_s64 length = ftell(source);
	assert(length > 0 && (legacy_u64)length <= UNIT_BENCHMARK_MAXIMUM_TRACK_BYTES);
	assert(fseek(source, 0, SEEK_SET) == 0);
	legacy_u8 *data = malloc((size_t)length);
	assert(data != NULL);
	assert(fread(data, 1, (size_t)length, source) == (size_t)length);
	assert(fclose(source) == 0);
	*size = (legacy_uint)length;
	return data;
}

static void print_learning_stats(void)
{
	if (!unit_learning_cache_enabled()) {
		return;
	}
	struct UNIT_LEARNING_STATS learning;
	unit_opponent_learning_stats(&learning);
	printf("learning_enabled=%u learning_loaded=%u learning_saved=%u "
		   "learning_loaded_samples=%" LEGACY_PRIu64
		   " learning_loaded_native_samples=%" LEGACY_PRIu64
		   " learning_search_samples=%" LEGACY_PRIu64 " learning_native_samples=%" LEGACY_PRIu64
		   " learning_best_frames=%u\n",
		   learning.enabled, learning.loaded, learning.saved, learning.loaded_samples,
		   learning.loaded_native_samples, learning.search_samples, learning.native_samples,
		   learning.best_frames);
}

struct UNIT_SOURCE_REPLAY_OPTIONS {
	const legacy_char *path;
	legacy_u32 branch_frame, frame_limit, preparation_ms, preparation_steps, planner_yield_ms;
	legacy_int replay_only, planner_sync, prepare_until_plan, verify;
	legacy_int require_finish, require_clean, pilot_debug;
	FILE *trace, *pilot_trace, *pilot_entry_trace;
};

static void source_replay_trace(FILE *trace, const legacy_char *phase, legacy_u8 input,
								const struct CSRPL_OPPONENT_COMMAND *command)
{
	if (!trace) {
		return;
	}
	const struct CARSTATE *car = &state.opponentstate;
	const struct CARSTATE *player = &state.playerstate;
	fprintf(trace,
			"%s,%u,%.2f,%u,%u,%d,%d,%u,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
			"%d,%d,%d,%d,%d,%u,%u,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,",
			phase, genghost_sim_frame(),
			genghost_sim_frame() / (legacy_f64)genghost_sim_frame_rate(), input, command->input,
			command->steering, command->route_index, command->route_point,
			car->car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
			car->car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
			car->car_position.lz / UNIT_BENCHMARK_WORLD_SCALE,
			car->car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE, car->car_steeringAngle,
			car->car_route_index, car->car_route_point_index, car->car_crashBmpFlag,
			car->car_rotate.x, car->car_rotate.y, car->car_rotate.z, car->car_sumSurfAllWheels,
			car->car_sumSurfFrontWheels, car->car_sumSurfRearWheels, car->car_surfacegrip_sum,
			car->car_demandedGrip, car->car_velocity_heading_offset, car->car_is_accelerating,
			car->car_is_braking, extended_unit_penalty_total(), extended_replay_clocks.penalty,
			player->car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
			player->car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
			player->car_position.lz / UNIT_BENCHMARK_WORLD_SCALE,
			player->car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE, player->car_steeringAngle,
			player->car_route_index, player->car_route_point_index, player->car_crashBmpFlag,
			player->car_rotate.x, player->car_rotate.y, player->car_rotate.z,
			player->car_sumSurfAllWheels);
	legacy_u8 bytes[GAMESTATE_SERIALIZED_SIZE];
	assert(gamestate_serialize(bytes, &state) == sizeof(bytes));
	for (legacy_u32 index = 0; index < sizeof(bytes); index++) {
		fprintf(trace, "%02x", bytes[index]);
	}
	fputc('\n', trace);
}

static legacy_s16 source_replay_benchmark(const struct UNIT_SOURCE_REPLAY_OPTIONS *options,
										  const legacy_char *program)
{
	legacy_uint size;
	legacy_u8 *bytes = read_track(options->path, &size);
	struct CSRPL_VIEW replay;
	assert(csrpl_decode(bytes, size, &replay, NULL, 0));
	legacy_u16 rules = replay.metadata.driving_rules;
	legacy_char *arguments[UNIT_BENCHMARK_REPLAY_RULE_ARGUMENTS] = {0};
	legacy_int count = 0;
	arguments[count++] = (legacy_char *)program;
	arguments[count++] = (legacy_char *)(rules & DRIVING_RULE_POWER_GEAR ? "--pg:on" : "--pg:off");
	arguments[count++] =
		(legacy_char *)(rules & DRIVING_RULE_LEGACY_COLLISION ? "--lc:on" : "--lc:off");
	arguments[count++] =
		(legacy_char *)(rules & DRIVING_RULE_LEFT_CORNER_BIAS ? "--lcb:on" : "--lcb:off");
	arguments[count++] =
		(legacy_char *)(rules & DRIVING_RULE_DUAL_WAY_SWITCHING ? "--dws:on" : "--dws:off");
	arguments[count++] =
		(legacy_char *)(rules & DRIVING_RULE_CHICANE_FAST_GRASS ? "--cfg:on" : "--cfg:off");
	if (rules & DRIVING_RULE_AMIGA_GRASS) {
		arguments[count++] = (legacy_char *)"--amigagrass";
	}
	if (rules & DRIVING_RULE_OWOOT) {
		arguments[count++] = (legacy_char *)"--owoot:on";
	}
	genghost_sim_set_frame_rate(replay.metadata.frames_per_second);
	extended_genghost_sim_set_replay(&replay);
	assert(genghost_sim_init_track(replay.track_data, (legacy_uint)replay.track_size,
								   (const legacy_char *)replay.metadata.player_car_id,
								   replay.metadata.player_transmission, count, arguments) == 0);
	assert(driving_rules_current() == rules);
	assert(extended_replay_version() == replay.metadata.version);
	assert(memcmp(gameconfig.game_playercarid, replay.metadata.player_car_id, CSRPL_CAR_ID_SIZE) ==
		   0);
	assert(memcmp(gameconfig.game_opponentcarid, replay.metadata.opponent_car_id,
				  CSRPL_CAR_ID_SIZE) == 0);
	assert(gameconfig.game_playertransmission == replay.metadata.player_transmission);
	assert(gameconfig.game_opponenttransmission == replay.metadata.opponent_transmission);
	legacy_int branch = options->branch_frame != LEGACY_U32_MAX;
	assert(!options->replay_only || !branch);
	assert(!branch || (options->branch_frame <= replay.metadata.frame_count &&
					   options->branch_frame < options->frame_limit &&
					   replay.metadata.opponent_type == OPPONENT_UNIT));
	legacy_u32 limit =
		branch ? options->frame_limit
			   : (options->frame_limit < replay.metadata.frame_count ? options->frame_limit
																	 : replay.metadata.frame_count);
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	struct GENGHOST_SNAPSHOT initial;
	genghost_sim_capture(&initial);
	legacy_u64 *hashes = options->verify ? calloc(limit, sizeof(*hashes)) : NULL;
	assert(!options->verify || hashes);
	if (options->trace) {
		fputs("phase,frame,time,player_input,command_input,command_steering,command_route,command_"
			  "point,"
			  "x,y,z,speed,steering,route,point,event,yaw,pitch,roll,contact,front_contact,rear_"
			  "contact,"
			  "grip,demanded,velocity_heading_offset,throttle,brake,penalty_frames,player_penalty_"
			  "frames,"
			  "player_x,player_y,player_z,player_speed,player_steering,player_route,player_point,"
			  "player_event,"
			  "player_yaw,player_pitch,player_roll,player_contact,gamestate_hex\n",
			  options->trace);
		struct CSRPL_OPPONENT_COMMAND none = {0};
		source_replay_trace(options->trace, "recorded", INPUT_NONE, &none);
	}
	legacy_u32 frames = 0, failures = 0;
	legacy_int preparation_failed = 0;
	for (; frames < limit; frames++) {
		if (branch && frames == options->branch_frame) {
			/* The recorded prefix supplied every physics tick. Truncate only its
			 * future controls; preserve the exact state, clocks, revision and rules. */
			assert(extended_replay_branch(frames));
			game_replay_mode = REPLAY_MODE_LIVE;
			if (options->preparation_ms) {
				assert(unit_opponent_prepare_begin_with_workers(options->planner_sync ? -1 : 0));
				legacy_u64 began = SDL_GetTicks();
				if (!benchmark_preparation(began, options->preparation_ms,
										   options->preparation_steps, options->prepare_until_plan,
										   options->pilot_debug, &failures, options->pilot_trace,
										   options->pilot_entry_trace, NULL)) {
					preparation_failed = 1;
					break;
				}
			}
			printf("branch_frame=%u branch_x=%.3f branch_y=%.3f branch_z=%.3f branch_speed=%.3f\n",
				   frames, state.opponentstate.car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
				   state.opponentstate.car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
				   state.opponentstate.car_position.lz / UNIT_BENCHMARK_WORLD_SCALE,
				   state.opponentstate.car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE);
		}
		legacy_u8 input =
			frames < replay.metadata.frame_count ? replay.inputs[frames] : INPUT_BRAKE_FLAG;
		assert(genghost_sim_step(input) >= 0);
		struct CSRPL_OPPONENT_COMMAND command = {0};
		(void)extended_replay_opponent_command_at(frames, &command);
		source_replay_trace(options->trace,
							branch && frames >= options->branch_frame ? "live" : "recorded", input,
							&command);
		if (hashes) {
			hashes[frames] = state_hash();
		}
		if (options->pilot_debug && branch && frames >= options->branch_frame) {
			debug_pilot_failure(&failures, options->pilot_trace, options->pilot_entry_trace);
		}
		if (branch && frames >= options->branch_frame &&
			state.opponentstate.car_crashBmpFlag != CRASH_EVENT_NONE) {
			frames++;
			break;
		}
		if (branch && frames >= options->branch_frame && options->planner_yield_ms) {
			SDL_Delay(options->planner_yield_ms);
		}
	}
	legacy_u8 event = state.opponentstate.car_crashBmpFlag;
	legacy_u32 penalty = extended_unit_penalty_total();
	if (!preparation_failed) {
		printf("source_replay_version=%u source_replay_rules=%u player_car=%.*s "
			   "player_transmission=%u "
			   "opponent=%u opponent_car=%.*s opponent_transmission=%u frames=%u seconds=%.2f "
			   "frame_rate=%u "
			   "event=%u finished=%u penalty_frames=%u player_event=%u branch=%u\n",
			   replay.metadata.version, rules, CSRPL_CAR_ID_SIZE, replay.metadata.player_car_id,
			   replay.metadata.player_transmission, replay.metadata.opponent_type,
			   CSRPL_CAR_ID_SIZE, replay.metadata.opponent_car_id,
			   replay.metadata.opponent_transmission, frames,
			   frames / (legacy_f64)genghost_sim_frame_rate(), genghost_sim_frame_rate(), event,
			   event == CRASH_EVENT_FINISH, penalty, state.playerstate.car_crashBmpFlag, branch);
	}
	unit_opponent_stop();
	if (!preparation_failed) {
		print_learning_stats();
	}
	if (options->verify && !preparation_failed) {
		genghost_sim_restore(&initial);
		game_replay_mode = REPLAY_MODE_PLAYBACK;
		for (legacy_u32 frame = 0; frame < frames; frame++) {
			update_gamestate_silent();
			if (state_hash() != hashes[frame]) {
				fprintf(stderr, "Source replay state mismatch at frame %u\n", frame + 1U);
				free(hashes);
				free(bytes);
				return 1;
			}
		}
		printf("replay_verified_frames=%u\n", frames);
	}
	free(hashes);
	free(bytes);
	if (options->trace) {
		assert(fclose(options->trace) == 0);
	}
	if (options->pilot_trace) {
		assert(fclose(options->pilot_trace) == 0);
	}
	if (options->pilot_entry_trace) {
		assert(fclose(options->pilot_entry_trace) == 0);
	}
	return preparation_failed || (options->require_finish && event != CRASH_EVENT_FINISH) ||
				   (options->require_clean && penalty != 0)
			   ? 1
			   : 0;
}

legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 **argv)
{
	test_stall_classification();
	if (argc < 3) {
		fprintf(stderr,
				"Usage: test-unit-race [--data-dir DIR] TRACK 1..7 "
				"[--prepare-ms N] [--prepare-steps N] [--prepare-until-plan] [--frames N] [--trace "
				"FILE] [--verify-replay] "
				"[--planner-yield-ms N] [--planner-sync] [--require-worker] "
				"[--pilot-debug] [--pilot-trace FILE] [--pilot-entry-trace FILE] [--car ID] "
				"[--manual] [--frame-rate 10|20] [--require-finish] "
				"[--require-clean] [--require-no-stalls] [--human "
				"stationary|accelerating|crossing|grid|grid-then-park|blocked|slow] "
				"[--human-drive-frames N] [--human-distance N] [--human-offset N] "
				"[--require-overtake] [--require-no-contact] [--unit-cache-dir DIR] "
				"[--owoot:on]\n"
				"       test-unit-race [--data-dir DIR] --source-replay FILE "
				"[--replay-only | --branch-frame N] [--trace FILE] [--frames TOTAL]\n");
		return 1;
	}
	const legacy_char *source_replay =
		strcmp((legacy_char *)argv[1], "--source-replay") == 0 ? (legacy_char *)argv[2] : NULL;
	legacy_u32 branch_frame = LEGACY_U32_MAX;
	legacy_int replay_only = 0;
	legacy_u8 opponent =
		source_replay ? OPPONENT_UNIT : (legacy_u8)strtoul((legacy_char *)argv[2], NULL, 10);
	assert(opponent >= OPPONENT_FIRST && opponent <= OPPONENT_LAST);
	legacy_u32 frame_limit = UNIT_BENCHMARK_DEFAULT_FRAMES;
	legacy_u16 frame_rate = GENGHOST_FRAME_RATE;
	legacy_u32 preparation_ms = 0;
	legacy_u32 preparation_steps = 0;
	legacy_u32 planner_yield_ms = 0;
	legacy_int verify = 0;
	legacy_int route_debug = 0;
	legacy_int pilot_debug = 0;
	legacy_int require_finish = 0;
	legacy_int require_clean = 0;
	legacy_int require_no_stalls = 0;
	legacy_int require_worker = 0;
	legacy_int require_overtake = 0, require_no_contact = 0;
	legacy_f64 human_distance_option = 0.0, human_offset = 0.0;
	legacy_u8 human_distance_set = 0, human_offset_set = 0;
	legacy_int planner_sync = 0;
	legacy_int manual = 0;
	const legacy_char *car_id = "PMIN";
	legacy_int human = UNIT_BENCHMARK_HUMAN_PARKED;
	legacy_u32 human_drive_frames = UNIT_BENCHMARK_GRID_DRIVE_FRAMES;
	legacy_int prepare_until_plan = 0;
	FILE *trace = NULL;
	FILE *pilot_trace = NULL;
	FILE *pilot_entry_trace = NULL;
	for (legacy_s16 index = 3; index < argc; index++) {
		if (strcmp((legacy_char *)argv[index], "--source-replay") == 0 && index + 1 < argc) {
			source_replay = (legacy_char *)argv[++index];
		} else if (strcmp((legacy_char *)argv[index], "--replay-only") == 0) {
			replay_only = 1;
		} else if (strcmp((legacy_char *)argv[index], "--branch-frame") == 0 && index + 1 < argc) {
			branch_frame = (legacy_u32)strtoul((legacy_char *)argv[++index], NULL, 10);
		} else if (strcmp((legacy_char *)argv[index], "--frames") == 0 && index + 1 < argc) {
			frame_limit = (legacy_u32)strtoul((legacy_char *)argv[++index], NULL, 10);
		} else if (strcmp((legacy_char *)argv[index], "--frame-rate") == 0 && index + 1 < argc) {
			frame_rate = (legacy_u16)strtoul((legacy_char *)argv[++index], NULL, 10);
			assert(frame_rate == GAME_FRAME_RATE_LOW || frame_rate == GAME_FRAME_RATE_NORMAL);
		} else if (strcmp((legacy_char *)argv[index], "--planner-yield-ms") == 0 &&
				   index + 1 < argc) {
			planner_yield_ms = (legacy_u32)strtoul((legacy_char *)argv[++index], NULL, 10);
			assert(planner_yield_ms <= UNIT_BENCHMARK_MAX_PLANNER_YIELD_MS);
		} else if (strcmp((legacy_char *)argv[index], "--prepare-steps") == 0 && index + 1 < argc) {
			preparation_steps = (legacy_u32)strtoul((legacy_char *)argv[++index], NULL, 10);
			if (!preparation_steps) {
				fputs("preparation step budget must be positive\n", stderr);
				return 1;
			}
		} else if (strcmp((legacy_char *)argv[index], "--prepare-ms") == 0 && index + 1 < argc) {
			preparation_ms = (legacy_u32)strtoul((legacy_char *)argv[++index], NULL, 10);
		} else if (strcmp((legacy_char *)argv[index], "--pilot-entry-trace") == 0 &&
				   index + 1 < argc) {
			pilot_entry_trace = fopen((legacy_char *)argv[++index], "w");
			assert(pilot_entry_trace != NULL);
			pilot_debug = 1;
			fputs("failure,live_frame,after,piece,target,exit_variant,secondary_piece,secondary_"
				  "speed,x,y,z,speed,route,point,yaw,pitch,roll,contact,steering,velocity_heading_"
				  "offset,throttle,brake,jump_source,jump_target,jump_launch,barrel_offset,spin_"
				  "velocity,rear_contact,exit_fraction,exit_speed,ramp_approach,secondary_ramp_"
				  "approach\n",
				  pilot_entry_trace);
		} else if (strcmp((legacy_char *)argv[index], "--pilot-trace") == 0 && index + 1 < argc) {
			pilot_trace = fopen((legacy_char *)argv[++index], "w");
			assert(pilot_trace != NULL);
			pilot_debug = 1;
			fputs("failure,live_frame,native_frame,before,piece,target,exit_variant,secondary_"
				  "piece,secondary_speed,x,y,z,speed,"
				  "route,point,yaw,"
				  "pitch,roll,contact,steering,throttle,brake,jump_source,jump_target,jump_launch,"
				  "barrel_offset,spin_velocity,rear_contact,exit_fraction,exit_speed,ramp_approach,"
				  "secondary_ramp_approach\n",
				  pilot_trace);
		} else if (strcmp((legacy_char *)argv[index], "--trace") == 0 && index + 1 < argc) {
			trace = fopen((legacy_char *)argv[++index], "w");
			assert(trace != NULL);
		} else if (strcmp((legacy_char *)argv[index], "--verify-replay") == 0) {
			verify = 1;
		} else if (strcmp((legacy_char *)argv[index], "--human") == 0 && index + 1 < argc) {
			const legacy_char *mode = (legacy_char *)argv[++index];
			human = strcmp(mode, "stationary") == 0		  ? UNIT_BENCHMARK_HUMAN_STATIONARY
					: strcmp(mode, "accelerating") == 0	  ? UNIT_BENCHMARK_HUMAN_ACCELERATING
					: strcmp(mode, "crossing") == 0		  ? UNIT_BENCHMARK_HUMAN_CROSSING
					: strcmp(mode, "grid") == 0			  ? UNIT_BENCHMARK_HUMAN_GRID
					: strcmp(mode, "grid-then-park") == 0 ? UNIT_BENCHMARK_HUMAN_GRID_THEN_PARK
					: strcmp(mode, "blocked") == 0		  ? UNIT_BENCHMARK_HUMAN_BLOCKED
					: strcmp(mode, "slow") == 0			  ? UNIT_BENCHMARK_HUMAN_SLOW
														  : UNIT_BENCHMARK_HUMAN_PARKED;
			assert(human != UNIT_BENCHMARK_HUMAN_PARKED);
		} else if (strcmp((legacy_char *)argv[index], "--human-distance") == 0 &&
				   index + 1 < argc) {
			human_distance_option = benchmark_number((legacy_char *)argv[++index]);
			assert(human_distance_option > 0.0 &&
				   human_distance_option <= UNIT_BENCHMARK_MAX_HUMAN_DISTANCE);
			human_distance_set = 1;
		} else if (strcmp((legacy_char *)argv[index], "--human-offset") == 0 && index + 1 < argc) {
			human_offset = benchmark_number((legacy_char *)argv[++index]);
			assert(fabs(human_offset) <= TRACK_TILE_HALF_SIZE);
			human_offset_set = 1;
		} else if (strcmp((legacy_char *)argv[index], "--require-overtake") == 0) {
			require_overtake = 1;
		} else if (strcmp((legacy_char *)argv[index], "--require-no-contact") == 0) {
			require_no_contact = 1;
		} else if (strcmp((legacy_char *)argv[index], "--human-drive-frames") == 0 &&
				   index + 1 < argc) {
			human_drive_frames = (legacy_u32)strtoul((legacy_char *)argv[++index], NULL, 10);
		} else if (strcmp((legacy_char *)argv[index], "--unit-cache-dir") == 0 &&
				   index + 1 < argc) {
			assert(unit_learning_cache_set_directory(argv[++index]));
		} else if (strcmp((legacy_char *)argv[index], "--car") == 0 && index + 1 < argc) {
			car_id = (legacy_char *)argv[++index];
			assert(strlen(car_id) == CSRPL_CAR_ID_SIZE);
		} else if (strcmp((legacy_char *)argv[index], "--manual") == 0) {
			manual = 1;
		} else if (strcmp((legacy_char *)argv[index], "--planner-sync") == 0) {
			planner_sync = 1;
		} else if (strcmp((legacy_char *)argv[index], "--require-worker") == 0) {
			require_worker = 1;
		} else if (strcmp((legacy_char *)argv[index], "--require-clean") == 0) {
			require_clean = 1;
		} else if (strcmp((legacy_char *)argv[index], "--require-no-stalls") == 0) {
			require_no_stalls = 1;
		} else if (strcmp((legacy_char *)argv[index], "--require-finish") == 0) {
			require_finish = 1;
		} else if (strcmp((legacy_char *)argv[index], "--prepare-until-plan") == 0) {
			prepare_until_plan = 1;
		} else if (strcmp((legacy_char *)argv[index], "--pilot-debug") == 0) {
			pilot_debug = 1;
		} else if (strcmp((legacy_char *)argv[index], "--route-debug") == 0) {
			route_debug = 1;
		}
	}
	assert(frame_limit > 0 && frame_limit <= GENGHOST_MAX_FRAMES);
	if (preparation_steps && !preparation_ms) {
		fputs("preparation steps require a positive --prepare-ms safety budget\n", stderr);
		return 1;
	}
	if (preparation_steps && opponent != OPPONENT_UNIT) {
		fputs("preparation steps require UNIT\n", stderr);
		return 1;
	}
	if (preparation_steps && source_replay && (replay_only || branch_frame == LEGACY_U32_MAX)) {
		fputs("preparation steps require a live source-replay branch\n", stderr);
		return 1;
	}
	if (source_replay) {
		if (require_no_stalls || require_worker) {
			fputs("--source-replay does not support --require-no-stalls or --require-worker\n",
				  stderr);
			return 1;
		}
		if (require_overtake || require_no_contact || human_distance_set || human_offset_set ||
			human == UNIT_BENCHMARK_HUMAN_BLOCKED || human == UNIT_BENCHMARK_HUMAN_SLOW) {
			fputs("--source-replay does not support scripted overtake options\n", stderr);
			return 1;
		}
		struct UNIT_SOURCE_REPLAY_OPTIONS options = {.path = source_replay,
													 .branch_frame = branch_frame,
													 .frame_limit = frame_limit,
													 .preparation_ms = preparation_ms,
													 .preparation_steps = preparation_steps,
													 .planner_yield_ms = planner_yield_ms,
													 .replay_only = replay_only,
													 .planner_sync = planner_sync,
													 .prepare_until_plan = prepare_until_plan,
													 .verify = verify,
													 .require_finish = require_finish,
													 .require_clean = require_clean,
													 .pilot_debug = pilot_debug,
													 .trace = trace,
													 .pilot_trace = pilot_trace,
													 .pilot_entry_trace = pilot_entry_trace};
		return source_replay_benchmark(&options, (legacy_char *)argv[0]);
	}
	assert(!replay_only && branch_frame == LEGACY_U32_MAX);
	assert((!require_overtake && !require_no_contact && !human_distance_set && !human_offset_set) ||
		   human != UNIT_BENCHMARK_HUMAN_PARKED);
	assert(!require_overtake || opponent == OPPONENT_UNIT);
	legacy_uint track_size;
	legacy_u8 *track = read_track((legacy_char *)argv[1], &track_size);
	struct CSRPL_METADATA metadata = {0};
	metadata.version = CSRPL_VERSION;
	metadata.frames_per_second = frame_rate;
	metadata.player_transmission = TRANSMISSION_AUTOMATIC;
	metadata.opponent_transmission = manual ? TRANSMISSION_MANUAL : TRANSMISSION_AUTOMATIC;
	metadata.opponent_type = opponent;
	memcpy(metadata.player_car_id, car_id, CSRPL_CAR_ID_SIZE);
	memcpy(metadata.opponent_car_id, car_id, CSRPL_CAR_ID_SIZE);
	if (opponent == OPPONENT_UNIT) {
		memcpy(metadata.opponent_id, "UNIT", CSRPL_OPPONENT_ID_SIZE);
		metadata.flags = CSRPL_FLAG_OPPONENT_COMMANDS;
	}
	if (track_size == REPLAY_TRACK_SIZE) {
		metadata.flags |= CSRPL_FLAG_CLASSIC_TRACK;
	}
	genghost_sim_set_frame_rate(frame_rate);
	genghost_sim_set_replay_metadata(&metadata);
	assert(genghost_sim_init_track(track, track_size, car_id, TRANSMISSION_AUTOMATIC, argc,
								   (legacy_char **)argv) == 0);
	free(track);
	if (opponent == OPPONENT_UNIT) {
		test_finish_crossing();
	}
	if (route_debug) {
		struct UNIT_ROUTE route = {0};
		assert(unit_route_build(&route));
		fprintf(stderr,
				"limits max=%f accel=%f brake=%f lateral=%f width=%f length=%f "
				"wheelbase=%f native_grip=%u count=%u\n",
				route.limits.max_speed, route.limits.acceleration, route.limits.braking,
				route.limits.lateral_acceleration, route.limits.half_width,
				route.limits.half_length, route.wheelbase, simd_opponent.grip, route.count);
		for (legacy_u32 index = 0; index < route.count && index < UNIT_BENCHMARK_DEBUG_POINTS;
			 index++) {
			const struct UNIT_ROUTE_POINT *point = &route.points[index];
			legacy_u8 id = (legacy_u8)track_route_element_ids[point->route_index];
			struct CARSTATE native_point = {0};
			get_track_route_point(point->route_index, &native_point.car_route_target,
								  (legacy_s16)point->route_point, NULL);
			fprintf(
				stderr,
				"point=%u piece=%d.%u x=%f y=%f z=%f width=%f surface=%u speed=%f id=%u model=%d "
				"native_y=%d unspecified=%u stunt=%u\n",
				index, point->route_index, point->route_point, point->x, point->y, point->z,
				point->half_width, point->surface, point->speed_limit, id,
				trkObjectList[id].ss_physicalModel, native_point.car_route_target.y,
				point->height_unspecified, point->stunt);
		}
		unit_route_free(&route);
	}
	if (opponent == OPPONENT_UNIT && !extended_replay_is_active()) {
		extended_replay_runtime_begin();
		extended_replay_set_classic_track(!extended_track_active());
	}
	game_replay_mode = REPLAY_MODE_LIVE;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	/* Preserve the real starting grid for parallel-start checks; other
	 * scenarios place the human explicitly. UNIT keeps its native state. */
	legacy_int grid =
		human == UNIT_BENCHMARK_HUMAN_GRID || human == UNIT_BENCHMARK_HUMAN_GRID_THEN_PARK;
	assert(!grid || (!human_distance_set && !human_offset_set));
	if (!grid) {
		state.playerstate.car_position.lx =
			UNIT_BENCHMARK_PARK_POSITION * UNIT_BENCHMARK_PARK_SCALE;
		state.playerstate.car_position.lz =
			UNIT_BENCHMARK_PARK_POSITION * UNIT_BENCHMARK_PARK_SCALE;
		state.playerstate.car_previous_position = state.playerstate.car_position;
	}
	legacy_f64 forward_x = -sin(state.opponentstate.car_rotate.x * UNIT_ANGLE_RADIANS);
	legacy_f64 forward_z = cos(state.opponentstate.car_rotate.x * UNIT_ANGLE_RADIANS);
	legacy_f64 road_x = state.opponentstate.car_position.lx / UNIT_POSITION_SCALE;
	legacy_f64 road_z = state.opponentstate.car_position.lz / UNIT_POSITION_SCALE;
	legacy_f64 road_axis = atan2(forward_x, forward_z);
	legacy_int dedicated_traffic =
		human == UNIT_BENCHMARK_HUMAN_BLOCKED || human == UNIT_BENCHMARK_HUMAN_SLOW;
	if (dedicated_traffic || require_overtake) {
		struct UNIT_ROUTE route = {0};
		assert(unit_route_build(&route));
		const struct UNIT_ROUTE_POINT *point =
			&route.points[unit_route_cursor(&route, &state.opponentstate)];
		legacy_f64 lateral = (road_x - point->x) * forward_z - (road_z - point->z) * forward_x;
		road_x -= forward_z * lateral;
		road_z += forward_x * lateral;
		if (human == UNIT_BENCHMARK_HUMAN_BLOCKED) {
			/* Place a stopped road obstruction beyond the starting gantry,
			 * whose posts are a separate restriction on available passing room. */
			legacy_u32 sample = 0;
			for (; sample < route.finish_begin; sample++) {
				legacy_u8 id = (legacy_u8)track_route_element_ids[route.points[sample].route_index];
				if (trkObjectList[id].ss_physicalModel == PHYSICAL_MODEL_ROAD) {
					break;
				}
			}
			assert(sample < route.finish_begin);
			road_x = route.points[sample].x;
			road_z = route.points[sample].z;
			unit_route_store(&route, sample, &state.opponentstate);
		}
		unit_route_free(&route);
	}
	if (dedicated_traffic) {
		/* Begin stopped directly behind the centered human rather than on
		 * the native parallel grid. The captured replay includes this pose. */
		assert(state.opponentstate.car_actual_speed == CAR_SPEED_STOPPED);
		state.opponentstate.car_position.lx = (legacy_s32)llround(road_x * UNIT_POSITION_SCALE);
		state.opponentstate.car_position.lz = (legacy_s32)llround(road_z * UNIT_POSITION_SCALE);
		state.opponentstate.car_previous_position = state.opponentstate.car_position;
	}
	legacy_f64 placement_distance = UNIT_BENCHMARK_HUMAN_DISTANCE;
	if (human == UNIT_BENCHMARK_HUMAN_BLOCKED) {
		placement_distance =
			benchmark_body_extent(&simd_opponent, &state.opponentstate, road_axis) +
			benchmark_body_extent(&simd_player, &state.playerstate, road_axis) +
			UNIT_BENCHMARK_BLOCKED_GAP;
	}
	if (human_distance_set) {
		placement_distance = human_distance_option;
	}
	if (!grid && human != UNIT_BENCHMARK_HUMAN_PARKED) {
		legacy_f64 origin_x =
			dedicated_traffic ? road_x : state.opponentstate.car_position.lx / UNIT_POSITION_SCALE;
		legacy_f64 origin_z =
			dedicated_traffic ? road_z : state.opponentstate.car_position.lz / UNIT_POSITION_SCALE;
		legacy_f64 lateral =
			human_offset +
			(human == UNIT_BENCHMARK_HUMAN_CROSSING ? UNIT_BENCHMARK_HUMAN_LATERAL : 0.0);
		state.playerstate.car_position.lx =
			(legacy_s32)llround((origin_x + forward_x * placement_distance + forward_z * lateral) *
								UNIT_POSITION_SCALE);
		state.playerstate.car_position.lz =
			(legacy_s32)llround((origin_z + forward_z * placement_distance - forward_x * lateral) *
								UNIT_POSITION_SCALE);
		state.playerstate.car_position.ly = state.opponentstate.car_position.ly;
		state.playerstate.car_previous_position = state.playerstate.car_position;
		state.playerstate.car_rotate = state.opponentstate.car_rotate;
		if (human == UNIT_BENCHMARK_HUMAN_CROSSING) {
			state.playerstate.car_rotate.x =
				LEGACY_S16_WRAP_ADD(state.playerstate.car_rotate.x, ANGLE_QUARTER_TURN);
		}
	}
	struct GENGHOST_SNAPSHOT initial;
	genghost_sim_capture(&initial);
	struct GAMESTATE shadow = state;
	legacy_u32 shadow_penalty_frames = 0;
	legacy_u64 preparation_elapsed = 0;
	legacy_u32 pilot_failures = 0;
	if (opponent == OPPONENT_UNIT && preparation_ms != 0) {
		legacy_u64 began = SDL_GetTicks();
		assert(unit_opponent_prepare_begin_with_workers(planner_sync ? -1 : 0));
		if (!benchmark_preparation(began, preparation_ms, preparation_steps, prepare_until_plan,
								   pilot_debug, &pilot_failures, pilot_trace, pilot_entry_trace,
								   &preparation_elapsed)) {
			return 1;
		}
		if (pilot_debug) {
			debug_pilot_entries();
		}
	}
	legacy_u64 *hashes = verify ? calloc(frame_limit, sizeof(*hashes)) : NULL;
	assert(!verify || hashes != NULL);
	if (trace != NULL) {
		fputs("frame,time,x,y,z,speed,steering,route,point,event,grip,demanded,yaw,pitch,roll,"
			  "contact,penalty_frames,penalty_status,penalty_confirmations,penalty_last_skipped,"
			  "penalty_recovering,shortcuts_allowed,throttle,brake,human_x,human_y,human_z,"
			  "human_speed,human_event,human_input,relative_ahead,relative_lateral,rear_clearance,"
			  "road_lateral,traffic_contact,traffic_blocked_frames,pass_side,side_changes,"
			  "passed_human,merged_human,planning_hold,planning_piece,planning_cursor,"
			  "stall_frames,raw_stall_frames,planning_hold_frames\n",
			  trace);
	}
	legacy_u32 frames = 0, corner_frames = 0;
	legacy_f64 corner_speed_sum = 0, speed_sum = 0, peak_speed = 0;
	legacy_f64 closest_human = UNIT_BENCHMARK_NO_HUMAN_DISTANCE;
	struct UNIT_BENCHMARK_TRAFFIC traffic = {0};
	legacy_f64 human_start_x = state.playerstate.car_position.lx / UNIT_POSITION_SCALE;
	legacy_f64 human_start_z = state.playerstate.car_position.lz / UNIT_POSITION_SCALE;
	legacy_f64 progress_clearance =
		benchmark_body_extent(&simd_opponent, &state.opponentstate, road_axis) +
		benchmark_body_extent(&simd_player, &state.playerstate, road_axis) +
		UNIT_BENCHMARK_PASS_CLEARANCE;
	legacy_u8 traffic_progress = 0;
	legacy_u64 step_sum_ns = 0, step_max_ns = 0, pacing_ns = 0;
	legacy_u32 deadline_misses = 0;
	struct UNIT_BENCHMARK_STALL stall = {0};
	legacy_u64 started = SDL_GetTicksNS();
	for (; frames < frame_limit; frames++) {
		legacy_u8 human_input = INPUT_NONE;
		if (grid || ((human == UNIT_BENCHMARK_HUMAN_ACCELERATING ||
					  human == UNIT_BENCHMARK_HUMAN_CROSSING) &&
					 frames >= UNIT_BENCHMARK_HUMAN_START_SECONDS * frame_rate)) {
			human_input = INPUT_ACCELERATE_FLAG;
		}
		if (human == UNIT_BENCHMARK_HUMAN_GRID_THEN_PARK && frames >= human_drive_frames) {
			/* Park through ordinary recorded controls, so playback requires no
			 * external teleport or other scripted state mutation. */
			human_input = INPUT_BRAKE_FLAG;
		}
		if (human == UNIT_BENCHMARK_HUMAN_BLOCKED) {
			human_input = INPUT_BRAKE_FLAG;
		} else if (human == UNIT_BENCHMARK_HUMAN_SLOW) {
			legacy_f64 human_speed = state.playerstate.car_actual_speed / UNIT_SPEED_SCALE;
			if (human_speed < UNIT_BENCHMARK_SLOW_HUMAN_SPEED) {
				human_input = INPUT_ACCELERATE_FLAG;
			} else if (human_speed >
					   UNIT_BENCHMARK_SLOW_HUMAN_SPEED + UNIT_BENCHMARK_SLOW_HUMAN_HYSTERESIS) {
				human_input = INPUT_BRAKE_FLAG;
			}
		}
		legacy_u64 step_started = SDL_GetTicksNS();
		assert(genghost_sim_step(human_input) >= 0);
		legacy_u64 step_ns = SDL_GetTicksNS() - step_started;
		step_sum_ns += step_ns;
		if (step_ns > step_max_ns) {
			step_max_ns = step_ns;
		}
		if (step_ns > UNIT_BENCHMARK_NS_PER_SECOND / frame_rate) {
			deadline_misses++;
		}
		if (pilot_debug) {
			debug_pilot_failure(&pilot_failures, pilot_trace, pilot_entry_trace);
		}
		shadow_penalty_step(&shadow, &shadow_penalty_frames);
		if (opponent == OPPONENT_UNIT) {
			assert(extended_unit_penalty_total() == shadow_penalty_frames);
		}
		const struct CARSTATE *car = &state.opponentstate;
		legacy_f64 speed = car->car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE;
		traffic_progress |=
			(car->car_position.lx / UNIT_POSITION_SCALE - human_start_x) * forward_x +
				(car->car_position.lz / UNIT_POSITION_SCALE - human_start_z) * forward_z >
			progress_clearance;
		legacy_f64 human_dx =
			(car->car_position.lx - state.playerstate.car_position.lx) / UNIT_BENCHMARK_WORLD_SCALE;
		legacy_f64 human_dz =
			(car->car_position.lz - state.playerstate.car_position.lz) / UNIT_BENCHMARK_WORLD_SCALE;
		legacy_f64 human_distance = hypot(human_dx, human_dz);
		/* A nearby human may require a collision-avoidance wait. Intentional
		 * stunt rechecks receive one bounded allowance per physical progress. */
		struct UNIT_PLANNING_HOLD planning_hold = {0};
		if (opponent == OPPONENT_UNIT) {
			unit_opponent_planning_hold(&planning_hold);
		}
		legacy_f64 progress_distance =
			UNIT_BENCHMARK_STALL_PROGRESS_LENGTHS *
			benchmark_body_extent(&simd_opponent, car, -car->car_rotate.x * UNIT_ANGLE_RADIANS);
		benchmark_stall_observe(&stall,
								frames >= UNIT_BENCHMARK_STALL_START_SECONDS * frame_rate &&
									human_distance >= TRACK_TILE_SIZE,
								speed < UNIT_BENCHMARK_STALL_SPEED, &planning_hold,
								&car->car_position, progress_distance);
		if (human != UNIT_BENCHMARK_HUMAN_PARKED) {
			closest_human = fmin(closest_human, human_distance);
			benchmark_traffic_observe(&traffic, frames + 1U, frame_rate,
									  human == UNIT_BENCHMARK_HUMAN_SLOW, road_axis, road_x,
									  road_z);
		}
		speed_sum += speed;
		if (speed > peak_speed) {
			peak_speed = speed;
		}
		if (abs(car->car_steeringAngle) > UNIT_BENCHMARK_CORNER_ANGLE &&
			car->car_actual_speed > UNIT_BENCHMARK_SPEED_MINIMUM) {
			corner_speed_sum += speed;
			corner_frames++;
		}
		if (hashes != NULL) {
			hashes[frames] = state_hash();
		}
		if (trace != NULL) {
			fprintf(trace,
					"%u,%.2f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%u,%u,%d,%d,%d,%d,%u,%d,%d,%d,%d,%d,%"
					"d,%d",
					frames + 1U, (frames + 1U) / (legacy_f64)frame_rate,
					car->car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
					car->car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
					car->car_position.lz / UNIT_BENCHMARK_WORLD_SCALE, speed,
					car->car_steeringAngle, car->car_route_index, car->car_route_point_index,
					car->car_crashBmpFlag, car->car_surfacegrip_sum, car->car_demandedGrip,
					car->car_rotate.x, car->car_rotate.y, car->car_rotate.z,
					car->car_sumSurfAllWheels, shadow_penalty_frames,
					extended_replay_clocks.unit_penalty.route.status,
					extended_replay_clocks.unit_penalty.route.confirmations,
					extended_replay_clocks.unit_penalty.route.last_skipped,
					extended_unit_penalty_recovery_required(),
					(!owoot_enabled && extended_unit_penalty_shortcuts_allowed()),
					car->car_is_accelerating, car->car_is_braking);
			fprintf(trace,
					",%.3f,%.3f,%.3f,%.3f,%u,%u,%.3f,%.3f,%.3f,%.3f,%d,%u,%d,%u,%u,%u,"
					"%u,%d,%u,%u,%u,%u\n",
					state.playerstate.car_position.lx / UNIT_POSITION_SCALE,
					state.playerstate.car_position.ly / UNIT_POSITION_SCALE,
					state.playerstate.car_position.lz / UNIT_POSITION_SCALE,
					state.playerstate.car_actual_speed / UNIT_SPEED_SCALE,
					state.playerstate.car_crashBmpFlag, human_input, traffic.ahead, traffic.lateral,
					traffic.rear_clearance, traffic.road_lateral,
					car->car_collision_latch == CAR_COLLISION_LATCH_SET ||
						state.playerstate.car_collision_latch == CAR_COLLISION_LATCH_SET,
					traffic.blocked_frames, traffic.side, traffic.side_changes, traffic.passed,
					traffic.merged, planning_hold.active, planning_hold.piece,
					planning_hold.route_cursor, stall.unexplained_frames, stall.raw_frames,
					stall.planning_frames);
		}
		if (require_overtake &&
			(traffic.merged || frames + 1U >= UNIT_BENCHMARK_OVERTAKE_SECONDS * frame_rate)) {
			frames++;
			break;
		}
		if (car->car_crashBmpFlag != CRASH_EVENT_NONE) {
			frames++;
			break;
		}
		if (planner_yield_ms != 0) {
			legacy_u64 pacing_started = SDL_GetTicksNS();
			SDL_Delay(planner_yield_ms);
			pacing_ns += SDL_GetTicksNS() - pacing_started;
		}
	}
	legacy_u64 elapsed_ns = SDL_GetTicksNS() - started;
	legacy_u8 event = state.opponentstate.car_crashBmpFlag;
	legacy_u8 human_event = state.playerstate.car_crashBmpFlag;
	struct UNIT_PLANNER_STATS planner_stats;
	unit_opponent_planner_stats(&planner_stats);
	assert(!planner_sync || planner_stats.workers == 0);
	legacy_u64 worker_commands = unit_opponent_worker_commands();
	struct UNIT_PILOT_STATS pilot_stats;
	unit_pilot_stats(&pilot_stats);
	printf("opponent=%u frames=%u seconds=%.2f frame_rate=%u owoot=%u event=%u finished=%u "
		   "mean_mph=%.3f corner_mph=%.3f peak_mph=%.3f simulation_ms=%.3f "
		   "penalty_frames=%u preparation_ms=%.0f mean_step_us=%.3f max_step_us=%.3f "
		   "deadline_misses=%u pacing_ms=%.3f max_stall_frames=%u "
		   "max_unexplained_stall_frames=%u planning_hold_frames=%u\n",
		   opponent, frames, frames / (legacy_f64)frame_rate, frame_rate, owoot_enabled != 0, event,
		   event == CRASH_EVENT_FINISH, frames ? speed_sum / frames : 0,
		   corner_frames ? corner_speed_sum / corner_frames : 0, peak_speed,
		   (elapsed_ns - pacing_ns) / UNIT_BENCHMARK_NS_PER_MILLISECOND, shadow_penalty_frames,
		   (legacy_f64)preparation_elapsed,
		   frames ? step_sum_ns / UNIT_BENCHMARK_NS_PER_MICROSECOND / frames : 0,
		   step_max_ns / UNIT_BENCHMARK_NS_PER_MICROSECOND, deadline_misses,
		   pacing_ns / UNIT_BENCHMARK_NS_PER_MILLISECOND, stall.max_raw_frames,
		   stall.max_unexplained_frames, stall.planning_frames);
	if (opponent == OPPONENT_UNIT) {
		printf("planner_workers=%u opening_candidates=%.0f route_candidates=%.0f "
			   "live_candidates=%.0f worker_accepted=%.0f worker_commands_applied=%.0f\n",
			   planner_stats.workers, (legacy_f64)planner_stats.opening_candidates,
			   (legacy_f64)planner_stats.route_candidates,
			   (legacy_f64)planner_stats.live_candidates, (legacy_f64)planner_stats.worker_accepted,
			   (legacy_f64)worker_commands);
		printf("pilot_ticks=%u pilot_trials=%u pilot_accepted=%u pilot_failed=%u "
			   "pilot_piece=%d pilot_event=%d pilot_completed=%u pilot_exhausted=%u\n",
			   pilot_stats.native_ticks, pilot_stats.trials, pilot_stats.accepted,
			   pilot_stats.failed_trials, pilot_stats.last_piece, pilot_stats.last_failure,
			   pilot_stats.completed_lap, pilot_stats.exhausted);
		printf("pilot_matched=%u pilot_rejected=%u pilot_mismatch_frame=%u "
			   "pilot_mismatch_offset=%u pilot_mismatch_actual=%u pilot_mismatch_expected=%u\n",
			   pilot_stats.matched_commands, pilot_stats.rejected_matches,
			   pilot_stats.first_mismatch_frame, pilot_stats.first_mismatch_offset,
			   pilot_stats.mismatch_actual, pilot_stats.mismatch_expected);
		printf("penalty_status=%d penalty_confirmations=%d penalty_last_skipped=%d "
			   "penalty_recovering=%d penalty_confirmed_route=%d shortcuts_allowed=%d\n",
			   extended_replay_clocks.unit_penalty.route.status,
			   extended_replay_clocks.unit_penalty.route.confirmations,
			   extended_replay_clocks.unit_penalty.route.last_skipped,
			   extended_unit_penalty_recovery_required(),
			   extended_replay_clocks.unit_penalty.route.confirmed_route,
			   (!owoot_enabled && extended_unit_penalty_shortcuts_allowed()));
	}
	if (human != UNIT_BENCHMARK_HUMAN_PARKED) {
		printf("traffic_progress=%u\n", traffic_progress);
		printf("human=%d human_event=%u passed_human=%u closest_human=%.3f "
			   "human_speed_mph=%.3f merged_human=%u pass_frame=%u merge_frame=%u "
			   "relative_ahead=%.3f rear_clearance=%.3f road_lateral=%.3f "
			   "traffic_max_blocked_frames=%u traffic_contact_frames=%u pass_side=%d "
			   "pass_side_changes=%u human_peak_mph=%.3f human_slow_frames=%u "
			   "human_slow_mean_mph=%.3f\n",
			   human, human_event, traffic.passed, closest_human,
			   state.playerstate.car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE, traffic.merged,
			   traffic.pass_frame, traffic.merge_frame, traffic.ahead, traffic.rear_clearance,
			   traffic.road_lateral, traffic.max_blocked_frames, traffic.contact_frames,
			   traffic.side, traffic.side_changes, traffic.human_peak_speed, traffic.slow_frames,
			   traffic.slow_frames ? traffic.slow_speed_sum / traffic.slow_frames : 0.0);
	}
	if (trace != NULL) {
		assert(fclose(trace) == 0);
	}
	if (pilot_trace != NULL) {
		assert(fclose(pilot_trace) == 0);
	}
	if (pilot_entry_trace != NULL) {
		assert(fclose(pilot_entry_trace) == 0);
	}
	if (verify) {
		unit_opponent_stop();
		genghost_sim_restore(&initial);
		game_replay_mode = REPLAY_MODE_PLAYBACK;
		for (legacy_u32 frame = 0; frame < frames; frame++) {
			/* The saved inputs and UNIT command stream are replayed without a
			 * planner or a writer that might truncate the completed recording. */
			update_gamestate_silent();
			if (state_hash() != hashes[frame]) {
				fprintf(stderr, "Replay state mismatch at frame %u\n", frame + 1U);
				free(hashes);
				return 1;
			}
		}
		printf("replay_verified_frames=%u\n", frames);
	}
	free(hashes);
	unit_opponent_stop();
	if (opponent == OPPONENT_UNIT) {
		print_learning_stats();
	}
	/* Accelerating leaders may remain ahead. Preserve the old encounter's
	 * forward-progress requirement without reporting a pass at their vacated
	 * starting position. Stopped/slow cases require actual rear clearance. */
	legacy_u8 moving_leader =
		human == UNIT_BENCHMARK_HUMAN_ACCELERATING || human == UNIT_BENCHMARK_HUMAN_GRID;
	legacy_int failed_human =
		human != UNIT_BENCHMARK_HUMAN_PARKED &&
		((moving_leader ? !traffic_progress : !traffic.passed) || human_event != CRASH_EVENT_NONE ||
		 shadow_penalty_frames != 0 || (event != CRASH_EVENT_NONE && event != CRASH_EVENT_FINISH));
	return (require_finish && event != CRASH_EVENT_FINISH) ||
				   (require_clean && shadow_penalty_frames != 0) ||
				   (require_no_stalls && benchmark_stall_failed(&stall, frame_rate)) ||
				   (require_worker && (!worker_commands || planner_stats.workers > 1U)) ||
				   (require_overtake && (!traffic.merged || traffic.side_changes ||
										 traffic.max_blocked_frames >=
											 UNIT_BENCHMARK_TRAFFIC_STALL_SECONDS * frame_rate)) ||
				   (require_no_contact && traffic.contact_frames) || failed_human
			   ? 1
			   : 0;
}
