#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/opponent/unit_planner.h"
#include "../c/opponent/unit_planner_internal.h"

#ifndef RESTUNTS_UNIT_SYNC_ONLY
#include <SDL3/SDL_timer.h>
#endif

#define TEST_POINT_COUNT 40U
#define TEST_HALF_WIDTH 160.0
#define TEST_POINT_SPACING 256.0
#define TEST_MAX_SPEED 245.0
#define TEST_PI 3.14159265358979323846
#define TEST_WAIT_LIMIT_MS 2000U
#define TEST_WORLD_PER_MPH (22.0 / 3.0)
#define TEST_HUMAN_CASES 8U
#define TEST_HUMAN_SLOW 2U
#define TEST_HUMAN_CROSSING 3U
#define TEST_HUMAN_AIRBORNE 4U
#define TEST_HUMAN_OVERLAP 5U
#define TEST_HUMAN_FAR 6U
#define TEST_HUMAN_INVALID 7U
#define TEST_HUMAN_GEOMETRIES 6U
#define TEST_HUMAN_CURVE 1U
#define TEST_HUMAN_CLEARANCE 2U
#define TEST_HUMAN_STUNT 3U
#define TEST_HUMAN_RAMP 4U
#define TEST_HUMAN_DESCENT 5U
#define TEST_HUMAN_RAMP_POINTS 2U
#define TEST_HUMAN_DISTANCE 350.0
#define TEST_HUMAN_HEIGHT 450.0
#define TEST_HUMAN_GRADE 50.0
#define TEST_HUMAN_SLOW_SPEED 12.0
#define TEST_HUMAN_CROSSING_SPEED 64.0
#define TEST_HUMAN_FAR_DISTANCE 1000000000.0
#define TEST_WORKER_UNIT_SPEED 70.0
#define TEST_WORKER_REFRESH_SPEED 130.0
#define TEST_WORKER_LANE 0.8
#define TEST_WORKER_CENTER_CANDIDATE 3U
#define TEST_POSITION_TOLERANCE 0.01
#define TEST_FIXED_FIRST_WORKERS 2
#define TEST_FIXED_SECOND_WORKERS 4
#define TEST_FIXED_WORK_CALLS 8U
#define TEST_FIXED_DELAY_MS 1U

static struct UNIT_CAR_LIMITS car_limits(void)
{
	struct UNIT_CAR_LIMITS limits = {0};
	limits.max_speed = TEST_MAX_SPEED;
	limits.acceleration = 25.0;
	limits.braking = 65.0;
	limits.lateral_acceleration = 40.0;
	limits.half_width = 45.0;
	limits.half_length = 90.0;
	limits.surface_grip[1] = 1.0;
	limits.surface_grip[2] = 0.75;
	limits.surface_grip[3] = 0.40;
	limits.surface_grip[4] = 0.45;
	return limits;
}

static struct UNIT_VEHICLE vehicle(void)
{
	struct UNIT_VEHICLE result = {0};
	result.half_width = 45.0;
	result.half_length = 90.0;
	return result;
}

static void straight(struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT])
{
	memset(points, 0, sizeof(*points) * TEST_POINT_COUNT);
	for (legacy_u32 index = 0; index < TEST_POINT_COUNT; index++) {
		points[index].z = index * TEST_POINT_SPACING;
		points[index].half_width = TEST_HALF_WIDTH;
		points[index].route_index = (legacy_s16)(index / 2U);
		points[index].route_point = (legacy_u16)(index % 2U);
		points[index].surface = 1U;
	}
}

static void corner(struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT], legacy_u8 surface)
{
	straight(points);
	for (legacy_u32 index = 0; index < TEST_POINT_COUNT; index++) {
		legacy_f64 angle = fmin(index, 8U) * TEST_PI / 16.0;
		points[index].x = 600.0 * (1.0 - cos(angle));
		points[index].z = 600.0 * sin(angle);
		if (index > 8U) {
			points[index].x += (index - 8U) * TEST_POINT_SPACING;
		}
		points[index].surface = surface;
	}
}

static void finish_sync_preparation(struct UNIT_PLANNER *planner)
{
	struct UNIT_PLANNER_STATS stats;
	for (legacy_u32 attempt = 0; attempt < TEST_POINT_COUNT; attempt++) {
		unit_planner_work(planner, 1U);
		unit_planner_stats(planner, &stats);
		if (stats.route_ready) {
			assert(stats.opening_ready);
			unit_planner_finish_startup(planner);
			return;
		}
	}
	assert(0 && "bounded preparation failed to produce a route");
}

static void test_preparation_and_copy(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_PLANNER *planner = unit_planner_create(points, TEST_POINT_COUNT, &limits, 1U, -1);
	assert(planner);
	struct UNIT_PLANNER_STATS stats;
	unit_planner_work(planner, 1U);
	unit_planner_stats(planner, &stats);
	assert(!stats.opening_ready && !stats.route_ready);
	assert(!stats.opening_candidates && !stats.route_candidates);
	finish_sync_preparation(planner);
	unit_planner_finish_startup(planner);
	memset(points, 0, sizeof(points));
	limits.max_speed = 1.0;
	struct UNIT_VEHICLE unit = vehicle();
	struct UNIT_PLAN plan;
	assert(unit_planner_plan(planner, &unit, NULL, 0U, 1U, &plan));
	assert(plan.valid && !plan.brake && plan.target_speed > 200.0 && plan.target_z > 0.0);
	assert(!plan.from_worker);
	unit_planner_stats(planner, &stats);
	assert(!stats.workers && stats.live_candidates > 0U && stats.live_candidates <= 3U);
	unit.z = (TEST_POINT_COUNT - 1U) * TEST_POINT_SPACING + 80.0;
	unit.speed = 100.0;
	assert(unit_planner_plan(planner, &unit, NULL, TEST_POINT_COUNT - 1U, 100U, &plan));
	assert(!plan.brake && plan.target_z > unit.z && plan.target_speed > 200.0);
	unit_planner_destroy(planner);
}

static void learning_samples(struct UNIT_PLANNER *planner, legacy_u64 count,
							 struct UNIT_PLANNER_LEARNING *state)
{
	for (legacy_u32 work = 0U; work < TEST_POINT_COUNT * TEST_POINT_COUNT; work++) {
		assert(unit_planner_export_learning(planner, state));
		if (state->completed_samples >= count) {
			return;
		}
		unit_planner_work(planner, 1U);
	}
	assert(0 && "bounded learning did not complete its samples");
}

static void assert_learning_equal(const struct UNIT_PLANNER_LEARNING *a,
								  const struct UNIT_PLANNER_LEARNING *b)
{
	assert(a->lane == b->lane && a->apex == b->apex && a->lookahead == b->lookahead);
	assert(a->score == b->score && a->ready == b->ready && a->seed == b->seed);
	assert(a->next_sample == b->next_sample && a->completed_samples == b->completed_samples);
}

static void test_learning_resume_and_native_profiles(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	corner(points, 1U);
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_PLANNER *first = unit_planner_create(points, TEST_POINT_COUNT, &limits, 0U, -1);
	assert(first);
	struct UNIT_PLANNER_LEARNING saved, continued, resumed;
	learning_samples(first, 7U, &saved);
	assert(saved.ready && saved.completed_samples == 7U && saved.next_sample == 7U);
	struct UNIT_PLANNER *second = unit_planner_create(points, TEST_POINT_COUNT, &limits, 0U, -1);
	assert(second && unit_planner_import_learning(second, &saved));
	learning_samples(first, 12U, &continued);
	learning_samples(second, 12U, &resumed);
	/* An independent race resumes the next RNG sample rather than repeating
	 * the beginning. Re-evaluating its warm incumbent cannot reset progress. */
	assert_learning_equal(&continued, &resumed);
	assert(continued.score <= saved.score);
	struct UNIT_PLANNER_PROFILE active, same, sampled, other;
	assert(unit_planner_export_profile(first, &active));
	struct UNIT_PLANNER *candidate = unit_planner_clone_candidate(first, 41U);
	struct UNIT_PLANNER *repeat = unit_planner_clone_candidate(first, 41U);
	struct UNIT_PLANNER *next = unit_planner_clone_candidate(first, 42U);
	assert(candidate && repeat && next);
	assert(unit_planner_export_profile(candidate, &sampled));
	assert(unit_planner_export_profile(repeat, &same));
	assert(unit_planner_export_profile(next, &other));
	assert(sampled.lane == same.lane && sampled.apex == same.apex &&
		   sampled.lookahead == same.lookahead);
	assert(sampled.lane != other.lane || sampled.apex != other.apex);
	assert(unit_planner_export_profile(first, &same));
	assert(active.lane == same.lane && active.apex == same.apex);
	/* Selecting an explicitly native-validated incumbent must not overwrite
	 * the separately retained best geometric proposal or search position. */
	assert(unit_planner_adopt_profiles(second, candidate));
	assert(unit_planner_export_learning(second, &resumed));
	assert_learning_equal(&continued, &resumed);
	assert(unit_planner_export_profile(second, &same));
	assert(sampled.lane == same.lane && sampled.apex == same.apex);
	/* A native incumbent may have been cloned before geometric startup
	 * completed. Adopting its active readiness cannot erase learned search. */
	struct UNIT_PLANNER *early = unit_planner_create(points, TEST_POINT_COUNT, &limits, 0U, -1);
	assert(early && unit_planner_adopt_profiles(second, early));
	assert(unit_planner_export_learning(second, &resumed));
	assert_learning_equal(&continued, &resumed);
	unit_planner_destroy(early);
	assert(!unit_planner_import_learning(second, &saved));
	struct UNIT_PLANNER_LEARNING invalid = saved;
	invalid.score = NAN;
	assert(!unit_planner_import_learning(first, &invalid));
	assert(unit_planner_clone_learning(first, &invalid) == NULL);
	invalid = saved;
	invalid.completed_samples = invalid.next_sample + 1U;
	assert(!unit_planner_import_learning(first, &invalid));
	invalid = saved;
	invalid.lane = 1.0;
	assert(!unit_planner_import_learning(first, &invalid));
	struct UNIT_PLANNER_PROFILE invalid_profile = {0.0, 0.0, 0.0};
	assert(unit_planner_clone_profile(first, &invalid_profile) == NULL);
	assert(!unit_planner_export_learning(NULL, &saved));
	assert(!unit_planner_export_profile(first, NULL));
	/* An unready snapshot has no geometric proposal, but still resumes its
	 * RNG/counters without discarding a separately stored native incumbent. */
	struct UNIT_PLANNER_LEARNING pending = {0};
	pending.seed = continued.seed;
	pending.next_sample = continued.next_sample;
	pending.completed_samples = continued.completed_samples;
	assert(unit_planner_import_learning(first, &pending));
	assert(unit_planner_export_learning(first, &resumed));
	assert(!resumed.ready && resumed.lane == 0.0 && resumed.apex == 0.0 &&
		   resumed.lookahead == 1.0 && resumed.score == 0.0);
	assert(resumed.seed == pending.seed && resumed.next_sample == pending.next_sample &&
		   resumed.completed_samples == pending.completed_samples);
	pending.lane = NAN;
	assert(!unit_planner_import_learning(first, &pending));
	unit_planner_destroy(candidate);
	unit_planner_destroy(repeat);
	unit_planner_destroy(next);
	unit_planner_destroy(first);
	unit_planner_destroy(second);
}

static void test_interrupted_preparation(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_PLANNER *planner = unit_planner_create(points, TEST_POINT_COUNT, &limits, 1U, -1);
	assert(planner);
	unit_planner_work(planner, 1U);
	struct UNIT_PLANNER_STATS stats;
	unit_planner_stats(planner, &stats);
	assert(!stats.opening_ready && !stats.route_ready);
	/* A deadline can interrupt the first full-route candidate. The conservative
	 * guide still provides immediate bounded live guidance everywhere. */
	unit_planner_finish_startup(planner);
	struct UNIT_VEHICLE unit = vehicle();
	unit.z = points[20].z;
	struct UNIT_PLAN plan;
	assert(unit_planner_plan(planner, &unit, NULL, 20U, 1U, &plan));
	assert(plan.valid && !plan.brake && plan.target_z > unit.z && plan.target_speed > 200.0);
	unit_planner_stats(planner, &stats);
	assert(!stats.route_ready && !stats.workers &&
		   stats.live_candidates <= UNIT_PLANNER_SYNC_CANDIDATES);
	unit_planner_destroy(planner);
}

static legacy_f64 corner_speed(legacy_u8 surface)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	corner(points, surface);
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_PLANNER *planner = unit_planner_create(points, TEST_POINT_COUNT, &limits, 1U, -1);
	assert(planner);
	finish_sync_preparation(planner);
	struct UNIT_VEHICLE unit = vehicle();
	unit.speed = 70.0;
	struct UNIT_PLAN plan;
	assert(unit_planner_plan(planner, &unit, NULL, 0U, 1U, &plan));
	assert(plan.valid && plan.target_speed > 10.0 && plan.target_speed < 100.0);
	legacy_f64 result = plan.target_speed;
	unit_planner_destroy(planner);
	return result;
}

static void test_curvature_and_ice(void)
{
	legacy_f64 paved = corner_speed(1U);
	legacy_f64 icy = corner_speed(3U);
	assert(icy < paved * 0.8);
}

static void test_banked_corner_speed(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	corner(points, 1U);
	struct UNIT_CAR_LIMITS limits = car_limits();
	limits.wheelbase = 62.0;
	limits.lateral_acceleration = 111.5;
	const legacy_f64 radius_scale = 1400.0 / 600.0;
	for (legacy_u32 index = 0; index < TEST_POINT_COUNT; index++) {
		points[index].x *= radius_scale;
		points[index].z *= radius_scale;
		points[index].fallback_speed_limit = 80.0;
	}
	struct UNIT_REQUEST request = {0};
	request.cursor = 3U;
	request.unit = vehicle();
	request.unit.x = points[request.cursor].x;
	request.unit.z = points[request.cursor].z;
	request.unit.yaw = 3.0 * TEST_PI / 16.0;
	request.unit.speed = 150.0;
	request.native_preview = 1U;
	struct UNIT_PROFILE profile = {0.0, 0.0, 1.0};
	struct UNIT_ROLLOUT flat, banked, mirrored, fallback;
	assert(
		unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 0U, &profile, &request, 0U, &flat));
	for (legacy_u32 index = 0; index < TEST_POINT_COUNT; index++) {
		points[index].banked_corner = 1U;
	}
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 0U, &profile, &request, 0U,
								&banked));
	/* The native bank contributes steering before the tires reach their grip
	 * limit. A flat-road envelope must not cap a certified banked bend. */
	assert(banked.plan.target_speed > flat.plan.target_speed);
	assert(banked.plan.target_speed <= limits.max_speed);
	request.native_preview = 0U;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 0U, &profile, &request, 0U,
								&fallback));
	assert(fallback.plan.target_speed <= 80.0);
	request.native_preview = 1U;
	request.unit.x = -request.unit.x;
	request.unit.yaw = -request.unit.yaw;
	for (legacy_u32 index = 0; index < TEST_POINT_COUNT; index++) {
		points[index].x = -points[index].x;
	}
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 0U, &profile, &request, 0U,
								&mirrored));
	assert(fabs(banked.plan.target_speed - mirrored.plan.target_speed) < 0.01);
	assert(fabs(banked.score - mirrored.score) < 0.01);
}

static void test_clearance_preserves_ordinary_planning(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	corner(points, 1U);
	struct UNIT_CAR_LIMITS limits = car_limits();
	limits.wheelbase = 62.0;
	struct UNIT_REQUEST request = {0};
	request.cursor = 3U;
	request.unit = vehicle();
	request.unit.x = points[request.cursor].x;
	request.unit.z = points[request.cursor].z;
	request.unit.yaw = 3.0 * TEST_PI / 16.0;
	request.unit.speed = 70.0;
	request.native_preview = 1U;
	struct UNIT_PROFILE profile = {0.0, 0.0, 1.0};
	struct UNIT_ROLLOUT ordinary, clearance, maneuver;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 0U, &profile, &request, 0U,
								&ordinary));
	for (legacy_u32 index = 0; index < TEST_POINT_COUNT; index++) {
		points[index].stunt = UNIT_STUNT_PROTECTED;
		points[index].clearance_only = 1U;
	}
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 0U, &profile, &request, 0U,
								&clearance));
	/* Requiring a native support/roof check does not remove curvature or
	 * braking constraints, nor truncate the ordinary driving horizon. */
	assert(fabs(ordinary.plan.target_speed - clearance.plan.target_speed) < 0.01);
	assert(fabs(ordinary.score - clearance.score) < 0.01);
	for (legacy_u32 index = 0; index < TEST_POINT_COUNT; index++) {
		points[index].clearance_only = 0U;
	}
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 0U, &profile, &request, 0U,
								&maneuver));
	assert(maneuver.plan.target_speed > clearance.plan.target_speed);
}

static void assert_plan_equal(const struct UNIT_PLAN *a, const struct UNIT_PLAN *b)
{
	assert(a->target_x == b->target_x && a->target_y == b->target_y);
	assert(a->target_z == b->target_z && a->target_speed == b->target_speed);
	assert(a->route_cursor == b->route_cursor && a->generation == b->generation);
	assert(a->brake == b->brake && a->valid == b->valid && a->from_worker == b->from_worker);
	assert(!a->overtaking && !b->overtaking);
}

static void test_human_independent_plans(void)
{
	for (legacy_u8 strict = 0U; strict <= 1U; strict++) {
		for (legacy_u8 geometry = 0U; geometry < TEST_HUMAN_GEOMETRIES; geometry++) {
			struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
			straight(points);
			if (geometry == TEST_HUMAN_CURVE) {
				corner(points, 1U);
			}
			for (legacy_u32 index = 0U; index < TEST_POINT_COUNT; index++) {
				if (geometry == TEST_HUMAN_CLEARANCE) {
					points[index].y = TEST_HUMAN_HEIGHT;
					points[index].height_unspecified = 1U;
					points[index].align_center = UNIT_ALIGN_CLEARANCE;
					points[index].stunt = UNIT_STUNT_PROTECTED;
					points[index].clearance_only = 1U;
				} else if (geometry == TEST_HUMAN_STUNT) {
					points[index].stunt = UNIT_STUNT_PROTECTED;
					points[index].align_center = UNIT_ALIGN_MANEUVER;
				} else if (geometry == TEST_HUMAN_RAMP && index < TEST_HUMAN_RAMP_POINTS) {
					points[index].stunt = UNIT_STUNT_RAMP;
					points[index].height_unspecified = 1U;
				} else if (geometry == TEST_HUMAN_DESCENT) {
					points[index].y = fmax(0.0, TEST_HUMAN_HEIGHT - index * TEST_HUMAN_GRADE);
				}
			}
			struct UNIT_CAR_LIMITS limits = car_limits();
			struct UNIT_PLANNER *absent =
				unit_planner_create(points, TEST_POINT_COUNT, &limits, strict, -1);
			struct UNIT_PLANNER *observed =
				unit_planner_create(points, TEST_POINT_COUNT, &limits, strict, -1);
			assert(absent && observed);
			finish_sync_preparation(absent);
			finish_sync_preparation(observed);
			struct UNIT_VEHICLE unit = vehicle();
			unit.y = points[0].y;
			unit.speed = TEST_WORKER_UNIT_SPEED;
			unit.vz = unit.speed * TEST_WORLD_PER_MPH;
			struct UNIT_VEHICLE humans[TEST_HUMAN_CASES];
			for (legacy_u32 index = 0U; index < TEST_HUMAN_CASES; index++) {
				humans[index] = vehicle();
				humans[index].y = unit.y;
				humans[index].z = TEST_HUMAN_DISTANCE;
			}
			humans[TEST_HUMAN_SLOW].speed = TEST_HUMAN_SLOW_SPEED;
			humans[TEST_HUMAN_SLOW].vz = TEST_HUMAN_SLOW_SPEED * TEST_WORLD_PER_MPH;
			humans[TEST_HUMAN_CROSSING].yaw = TEST_PI * 0.5;
			humans[TEST_HUMAN_CROSSING].vx = TEST_HUMAN_CROSSING_SPEED;
			humans[TEST_HUMAN_AIRBORNE].airborne = 1U;
			humans[TEST_HUMAN_AIRBORNE].y += TEST_HUMAN_HEIGHT;
			humans[TEST_HUMAN_AIRBORNE].vy = -TEST_HUMAN_CROSSING_SPEED;
			humans[TEST_HUMAN_OVERLAP] = unit;
			humans[TEST_HUMAN_FAR].x = TEST_HUMAN_FAR_DISTANCE;
			humans[TEST_HUMAN_INVALID].x = humans[TEST_HUMAN_INVALID].y = NAN;
			humans[TEST_HUMAN_INVALID].z = humans[TEST_HUMAN_INVALID].speed = NAN;
			humans[TEST_HUMAN_INVALID].half_width = -1.0;
			for (legacy_u32 index = 0U; index < TEST_HUMAN_CASES; index++) {
				struct UNIT_PLAN expected, actual;
				const struct UNIT_VEHICLE *human = index ? &humans[index] : NULL;
				assert(unit_planner_plan(absent, &unit, NULL, 0U, index + 1U, &expected));
				assert(unit_planner_plan(observed, &unit, human, 0U, index + 1U, &actual));
				/* Identical work and UNIT state must produce identical decisions,
				 * even when the ignored human data overlaps or is nonfinite. */
				assert_plan_equal(&expected, &actual);
			}
			unit_planner_destroy(absent);
			unit_planner_destroy(observed);
		}
	}
}

static void test_clearance_alignment(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	for (legacy_u32 index = 0U; index < TEST_POINT_COUNT; index++) {
		points[index].align_center = UNIT_ALIGN_CLEARANCE;
		points[index].stunt = UNIT_STUNT_PROTECTED;
		points[index].clearance_only = 1U;
	}
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_REQUEST request = {0};
	request.unit = vehicle();
	request.unit.speed = TEST_WORKER_UNIT_SPEED;
	request.native_preview = 1U;
	struct UNIT_PROFILE profile = {TEST_WORKER_LANE, 0.0, 1.0};
	struct UNIT_ROLLOUT centered, reserved;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 0U, &profile, &request, 0U,
								&centered));
	assert(fabs(centered.plan.target_x) < TEST_POSITION_TOLERANCE && !centered.plan.brake);
	/* Legacy reserved traffic fields cannot relax track clearance alignment. */
	profile.clearance_passing = 1U;
	profile.overtake_offset = TEST_HUMAN_DISTANCE;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 0U, &profile, &request, 0U,
								&reserved));
	assert_plan_equal(&centered.plan, &reserved.plan);
	assert(centered.score == reserved.score);
}

static void test_validation(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	struct UNIT_CAR_LIMITS limits = car_limits();
	assert(!unit_planner_create(points, 1U, &limits, 0U, -1));
	points[1].x = NAN;
	assert(!unit_planner_create(points, TEST_POINT_COUNT, &limits, 0U, -1));
	points[1].x = 0.0;
	points[1].fallback_speed_limit = NAN;
	assert(!unit_planner_create(points, TEST_POINT_COUNT, &limits, 0U, -1));
	points[1].fallback_speed_limit = -1.0;
	assert(!unit_planner_create(points, TEST_POINT_COUNT, &limits, 0U, -1));
	points[1].fallback_speed_limit = 0.0;
	limits.braking = 0.0;
	assert(!unit_planner_create(points, TEST_POINT_COUNT, &limits, 0U, -1));
	unit_planner_destroy(NULL);
}

static void test_complete_route_rebuild(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_PLANNER *planner = unit_planner_create(points, 8U, &limits, 1U, -1);
	assert(planner);
	unit_planner_work(planner, 1U);
	struct UNIT_PLANNER_STATS stats;
	unit_planner_stats(planner, &stats);
	assert(stats.opening_ready);
	assert(!unit_planner_rebuild(planner, points, 1U));
	unit_planner_stats(planner, &stats);
	assert(stats.opening_ready);
	planner = unit_planner_rebuild(planner, points, TEST_POINT_COUNT);
	assert(planner);
	unit_planner_stats(planner, &stats);
	assert(!stats.opening_ready && !stats.route_ready && !stats.workers);
	for (legacy_u32 attempt = 0U; attempt < TEST_POINT_COUNT && !stats.route_ready; attempt++) {
		unit_planner_work(planner, 1U);
		unit_planner_stats(planner, &stats);
	}
	assert(stats.opening_ready && stats.route_ready);
	/* A changed late piece invalidates readiness even if the first four pieces
	 * are identical. Its speed and line constraints must be included anew. */
	points[TEST_POINT_COUNT - 1U].height_unspecified = 1U;
	planner = unit_planner_rebuild(planner, points, TEST_POINT_COUNT);
	assert(planner);
	unit_planner_stats(planner, &stats);
	assert(!stats.opening_ready);
	finish_sync_preparation(planner);
	assert(!unit_planner_rebuild(planner, points, TEST_POINT_COUNT));
	unit_planner_destroy(planner);
}

static void test_pilot_clones(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	corner(points, 1U);
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_PLANNER *source = unit_planner_create(points, TEST_POINT_COUNT, &limits, 1U, -1);
	assert(source);
	finish_sync_preparation(source);
	struct UNIT_VEHICLE unit = vehicle();
	struct UNIT_PLAN first, second;
	assert(unit_planner_plan(source, &unit, NULL, 0U, 1U, &first));
	struct UNIT_PLANNER *a = unit_planner_clone_sync(source);
	assert(a);
	struct UNIT_PLANNER *b = unit_planner_clone_sync(a);
	assert(b);
	assert(!unit_planner_adopt_profiles(source, a));
	struct UNIT_PLANNER *destination =
		unit_planner_create(points, TEST_POINT_COUNT, &limits, 1U, -1);
	assert(destination && unit_planner_adopt_profiles(destination, a));
	assert(!unit_planner_restore_continuation(destination, a));
	unit_planner_destroy(destination);
	points[0].x += 1.0;
	destination = unit_planner_create(points, TEST_POINT_COUNT, &limits, 1U, -1);
	assert(destination && !unit_planner_adopt_profiles(destination, a));
	unit_planner_destroy(destination);
	unit_planner_destroy(source);
	for (legacy_u32 frame = 0U; frame < 8U; frame++) {
		assert(unit_planner_plan(a, &unit, NULL, 0U, frame, &first));
		assert(unit_planner_plan(b, &unit, NULL, 0U, frame, &second));
		assert(first.target_x == second.target_x && first.target_z == second.target_z);
		assert(first.target_speed == second.target_speed && first.brake == second.brake);
		unit.speed += 2.0;
		unit.z += 10.0;
	}
	struct UNIT_PLANNER *continuation = unit_planner_clone_continuation(a);
	assert(continuation);
	for (legacy_u32 frame = 8U; frame < 16U; frame++) {
		assert(unit_planner_plan(a, &unit, NULL, 0U, frame, &first));
		assert(unit_planner_plan(continuation, &unit, NULL, 0U, frame, &second));
		assert(first.target_x == second.target_x && first.target_z == second.target_z);
		assert(first.target_speed == second.target_speed && first.brake == second.brake);
		unit.speed += 2.0;
		unit.z += 10.0;
	}
	struct UNIT_PLANNER_STATS stats;
	unit_planner_stats(a, &stats);
	assert(unit_planner_restore_continuation(b, a));
	assert(unit_planner_restore_continuation(b, b));
	for (legacy_u32 frame = 16U; frame < 20U; frame++) {
		assert(unit_planner_plan(a, &unit, NULL, 0U, frame, &first));
		assert(unit_planner_plan(b, &unit, NULL, 0U, frame, &second));
		assert(first.target_x == second.target_x && first.target_z == second.target_z);
		assert(first.target_speed == second.target_speed && first.brake == second.brake);
		unit.speed += 2.0;
		unit.z += 10.0;
	}
	assert(!stats.workers && stats.opening_ready && stats.route_ready);
	assert(unit_planner_pause_live(a) && unit_planner_pause_live(a));
	assert(unit_planner_resume_live(a) && unit_planner_resume_live(a));
	unit_planner_stats(a, &stats);
	assert(!stats.workers);
	assert(!unit_planner_clone_sync(NULL));
	unit_planner_destroy(continuation);
	unit_planner_destroy(a);
	unit_planner_destroy(b);
}

static void test_physical_corridor(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	struct UNIT_CAR_LIMITS limits = car_limits();
	limits.half_width = 19.0;
	limits.half_length = 42.0;
	for (legacy_u32 index = 0U; index < TEST_POINT_COUNT; index++) {
		points[index].half_width = 120.0;
	}
	struct UNIT_REQUEST request = {0};
	request.unit = vehicle();
	request.unit.half_width = limits.half_width;
	request.unit.half_length = limits.half_length;
	request.unit.speed = 80.0;
	struct UNIT_PROFILE profile = {-0.8, 0.0, 1.0};
	struct UNIT_ROLLOUT inside, outside, forbidden;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &profile, &request, 0U,
								&inside));
	request.unit.x = 150.0;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &profile, &request, 0U,
								&outside));
	assert(outside.score > inside.score + 1000.0);
	for (legacy_u32 index = 0U; index < TEST_POINT_COUNT; index++) {
		points[index].shortcut_safe = 1U;
	}
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &profile, &request, 0U,
								&forbidden));
	assert(fabs(forbidden.score - outside.score) < 0.01);
}

static void test_dense_graded_geometry(void)
{
	const legacy_u32 point_count = 2048U, cursor = 256U;
	struct UNIT_ROUTE_POINT *points = calloc(point_count, sizeof(*points));
	assert(points);
	for (legacy_u32 index = 0U; index < point_count; index++) {
		points[index].x = round(index * 0.031);
		points[index].z = index * 8.0;
		points[index].y = round(points[index].z * 0.17);
		points[index].half_width = TEST_HALF_WIDTH;
		points[index].route_index = (legacy_s16)(index / 128U);
		points[index].route_point = (legacy_u16)(index % 128U);
		points[index].surface = 1U;
	}
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_PLANNER *planner = unit_planner_create(points, point_count, &limits, 1U, -1);
	assert(planner);
	unit_planner_finish_startup(planner);
	struct UNIT_VEHICLE unit = vehicle();
	unit.x = points[cursor].x;
	unit.y = points[cursor].y;
	unit.z = points[cursor].z;
	unit.speed = 100.0;
	struct UNIT_PLAN plan;
	assert(unit_planner_plan(planner, &unit, NULL, cursor, 1U, &plan));
	assert(!plan.brake && plan.target_speed > 150.0 && plan.target_z > unit.z);
	unit_planner_destroy(planner);
	free(points);
}

static void test_stunt_approach(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	for (legacy_u32 index = 0U; index < TEST_POINT_COUNT; index++) {
		points[index].surface = 3U;
		points[index].align_center = UNIT_ALIGN_MANEUVER;
	}
	points[10].stunt = UNIT_STUNT_PROTECTED;
	points[10].half_width = 31.0;
	points[10].speed_floor = 90.0;
	points[10].speed_limit = 105.0;
	struct UNIT_CAR_LIMITS limits = car_limits();
	limits.half_width = 19.0;
	struct UNIT_REQUEST request = {0};
	request.cursor = 7U;
	request.unit = vehicle();
	request.unit.z = 1800.0;
	request.unit.speed = 60.0;
	struct UNIT_PROFILE centered = {0.0, 0.0, 1.0}, optimized = {-0.8, 0.4, 0.75};
	struct UNIT_ROLLOUT moving, stopped, aligned;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &centered, &request, 0U,
								&moving));
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &centered, &request, 20U,
								&stopped));
	assert(moving.score < stopped.score && !moving.plan.brake);
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &optimized, &request, 0U,
								&aligned));
	assert(fabs(moving.plan.target_x - aligned.plan.target_x) < 0.01);
	assert(fabs(moving.plan.target_z - aligned.plan.target_z) < 0.01);
	assert(fabs(moving.score - aligned.score) < 0.01);
	request.cursor = 9U;
	request.unit.z = 2450.0;
	request.unit.speed = 100.0;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &centered, &request, 13U,
								&moving));
	assert(moving.plan.target_speed > 80.0 && !moving.plan.brake);
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &centered, &request, 20U,
								&stopped));
	assert(stopped.plan.brake == UNIT_PLAN_BRAKE_STOP && stopped.plan.target_speed == 0.0);
}

static void test_gate_alignment_preserves_open_neighbors(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	points[0].align_center = UNIT_ALIGN_GATE;
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_REQUEST request = {0};
	request.unit = vehicle();
	request.unit.speed = 40.0;
	struct UNIT_PROFILE centered = {0.0, 0.0, 1.0}, offset = {0.8, 0.0, 1.0};
	struct UNIT_ROLLOUT straight_line, open_exit, aligned;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &centered, &request, 0U,
								&straight_line));
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &offset, &request, 0U,
								&open_exit));
	assert(fabs(straight_line.plan.target_x) < 0.01);
	assert(fabs(open_exit.plan.target_x) > 1.0);
	/* An overlapping maneuver approach takes precedence over the gate's
	 * passing allowance and keeps every unmarked neighbor centered too. */
	points[0].align_center |= UNIT_ALIGN_MANEUVER;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &offset, &request, 0U,
								&aligned));
	assert(fabs(aligned.plan.target_x) < 0.01);
	assert(fabs(aligned.score - straight_line.score) < 0.01);
	/* With only gate constraints, each marked sample still stays centered. */
	for (legacy_u32 index = 0U; index < TEST_POINT_COUNT; index++) {
		points[index].align_center = UNIT_ALIGN_GATE;
	}
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &offset, &request, 0U,
								&aligned));
	assert(fabs(aligned.plan.target_x) < 0.01);
}

static void test_fallback_speed_policy(void)
{
	const legacy_f64 fallback_speed = 80.0, approach_speed = 160.0;
	const legacy_u32 first_limited = 8U, approach = 2U;
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	for (legacy_u32 index = first_limited; index < TEST_POINT_COUNT; index++) {
		points[index].fallback_speed_limit = fallback_speed;
	}
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_PLANNER *live = unit_planner_create(points, TEST_POINT_COUNT, &limits, 0U, -1);
	assert(live);
	finish_sync_preparation(live);
	struct UNIT_PLANNER *native = unit_planner_clone_sync(live);
	assert(native);
	struct UNIT_VEHICLE unit = vehicle();
	unit.z = points[approach].z;
	unit.speed = approach_speed;
	struct UNIT_PLAN conservative, verified;
	assert(!points[approach].fallback_speed_limit);
	assert(unit_planner_plan(live, &unit, NULL, approach, 1U, &conservative));
	assert(unit_planner_plan(native, &unit, NULL, approach, 1U, &verified));
	/* The distant fallback cap must propagate backwards through braking
	 * distance; a native preview can still prove the faster original policy. */
	assert(conservative.target_speed > fallback_speed);
	assert(conservative.target_speed < approach_speed && verified.target_speed > approach_speed);
	unit.z = points[first_limited].z;
	assert(unit_planner_plan(live, &unit, NULL, first_limited, 2U, &conservative));
	assert(conservative.target_speed <= fallback_speed);
	assert(unit_planner_restore_continuation(native, live));
	assert(unit_planner_plan(native, &unit, NULL, first_limited, 2U, &verified));
	assert(verified.target_speed > approach_speed);
	/* Adopting a proven native line must not turn the live fallback planner
	 * into a native preview or disable its conservative guide limits. */
	struct UNIT_PLANNER *adopted = unit_planner_create(points, TEST_POINT_COUNT, &limits, 0U, -1);
	assert(adopted && unit_planner_adopt_profiles(adopted, native));
	assert(unit_planner_plan(adopted, &unit, NULL, first_limited, 1U, &conservative));
	assert(conservative.target_speed <= fallback_speed);
	/* OWOOT keeps the original strict driving policy. Temporary no-shortcut
	 * recovery under ordinary rules must still use the fallback limits. */
	struct UNIT_PLANNER *strict = unit_planner_create(points, TEST_POINT_COUNT, &limits, 1U, -1);
	assert(strict);
	unit_planner_finish_startup(strict);
	assert(unit_planner_plan(strict, &unit, NULL, first_limited, 1U, &verified));
	assert(verified.target_speed > approach_speed);
	unit.shortcuts_forbidden = 1U;
	assert(unit_planner_plan(live, &unit, NULL, first_limited, 3U, &conservative));
	assert(conservative.target_speed <= fallback_speed);
	unit_planner_destroy(strict);
	unit_planner_destroy(adopted);
	unit_planner_destroy(native);
	unit_planner_destroy(live);
}

static void test_fallback_alignment_policy(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	for (legacy_u32 index = 0U; index < TEST_POINT_COUNT; index++) {
		points[index].align_center = UNIT_ALIGN_FALLBACK;
	}
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_REQUEST request = {0};
	request.unit = vehicle();
	request.unit.speed = 40.0;
	struct UNIT_PROFILE offset = {0.8, 0.0, 1.0};
	struct UNIT_ROLLOUT conservative, native;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 0U, &offset, &request, 0U,
								&conservative));
	request.native_preview = 1U;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 0U, &offset, &request, 0U,
								&native));
	assert(fabs(conservative.plan.target_x) < 0.01);
	assert(fabs(native.plan.target_x) > 1.0);
	request.native_preview = 0U;
	struct UNIT_ROLLOUT strict;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &offset, &request, 0U,
								&strict));
	assert(fabs(strict.plan.target_x - native.plan.target_x) < 0.01);
	request.unit.shortcuts_forbidden = 1U;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 0U, &offset, &request, 0U,
								&conservative));
	assert(fabs(conservative.plan.target_x) < 0.01);
	request.native_preview = 1U;
	/* A mandatory maneuver retains its centered approach in either mode. */
	points[0].align_center |= UNIT_ALIGN_MANEUVER;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 0U, &offset, &request, 0U,
								&native));
	assert(fabs(native.plan.target_x) < 0.01);
}

static void test_recovery_before_stunt(void)
{
	struct UNIT_ROUTE_POINT points[8] = {0};
	for (legacy_u32 index = 0U; index < 8U; index++) {
		points[index].z = index * 120.0;
		points[index].half_width = 120.0;
		points[index].surface = 3U;
		points[index].route_index = index < 4U ? 0 : 1;
		points[index].route_point = (legacy_u16)(index % 4U);
		points[index].align_center = UNIT_ALIGN_MANEUVER;
		if (index >= 4U) {
			points[index].stunt = UNIT_STUNT_PROTECTED;
			points[index].speed_floor = 70.0;
			points[index].speed_limit = 105.0;
		}
	}
	struct UNIT_CAR_LIMITS limits = {246.0, 73.0, 40.0, 111.5,
									 62.0,	19.0, 56.0, {0.0, 1.0, 0.75, 0.25, 0.45}};
	struct UNIT_REQUEST request = {0};
	request.cursor = 1U;
	request.unit.x = 93.0;
	request.unit.z = 100.0;
	request.unit.yaw = -0.9;
	request.unit.steering = 0.2;
	request.unit.steering_rate = 0.245;
	request.unit.half_width = limits.half_width;
	request.unit.half_length = limits.half_length;
	request.unit.recovering = request.unit.shortcuts_forbidden = 1U;
	struct UNIT_PROFILE profile = {0.0, 0.0, 1.0};
	struct UNIT_ROLLOUT crawl, stopped, fast;
	assert(unit_planner_rollout(points, 8U, &limits, 1U, &profile, &request, 0U, &fast));
	assert(unit_planner_rollout(points, 8U, &limits, 1U, &profile, &request, 18U, &stopped));
	assert(unit_planner_rollout(points, 8U, &limits, 1U, &profile, &request,
								UNIT_PLANNER_CRAWL_CANDIDATE, &crawl));
	/* Fast variants hit the recovery cap and cannot meet the future entrance
	 * floor. A gentle correction improves actual road clearance before entering
	 * that maneuver, instead of choosing an indefinite stop outside the line. */
	assert(stopped.score < fast.score && crawl.score < stopped.score);
	assert(!crawl.plan.brake && crawl.plan.target_speed > 0.0);
}

static void test_ordered_stunt_guidance(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	for (legacy_u32 index = 0; index < 5U; index++) {
		legacy_f64 angle = index * TEST_PI * 0.5;
		points[index].x = 0.0;
		points[index].y = 200.0 * (1.0 - cos(angle));
		points[index].z = 200.0 * sin(angle);
		points[index].stunt = UNIT_STUNT_PROTECTED;
		points[index].route_index = 0;
		points[index].route_point = (legacy_u16)index;
		points[index].speed_floor = 90.0;
		points[index].speed_limit = 125.0;
	}
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_PLANNER *planner = unit_planner_create(points, TEST_POINT_COUNT, &limits, 1U, -1);
	assert(planner);
	finish_sync_preparation(planner);
	struct UNIT_VEHICLE unit = vehicle();
	struct UNIT_PLAN plan;
	assert(unit_planner_plan(planner, &unit, NULL, 1U, 1U, &plan));
	assert(plan.route_cursor == 1U && plan.target_y > 0.0 && plan.target_y < 200.0);
	assert(plan.target_speed >= points[1].speed_floor && !plan.brake);
	unit.z = points[1].z;
	unit.y = points[1].y;
	assert(unit_planner_plan(planner, &unit, NULL, 2U, 2U, &plan));
	assert(plan.route_cursor == 2U && plan.target_y > 200.0 && plan.target_y < 400.0);
	unit.speed = 125.0;
	unit.z = points[1].z - 1.0;
	unit.y = points[1].y - 1.0;
	assert(unit_planner_plan(planner, &unit, NULL, 1U, 3U, &plan));
	assert(plan.route_cursor == 1U && plan.target_y > 300.0);
	assert(hypot(plan.target_y - unit.y, plan.target_z - unit.z) > 200.0);
	unit_planner_destroy(planner);
}

static void test_stunt_exit_continuation(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	points[0].y = 200.0;
	points[1].y = 100.0;
	points[1].z = 200.0;
	points[2].y = 150.0;
	points[2].z = 450.0;
	points[0].stunt = points[1].stunt = UNIT_STUNT_PROTECTED;
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_REQUEST request = {0};
	request.cursor = 1U;
	request.unit = vehicle();
	request.unit.y = 105.0;
	request.unit.z = 190.0;
	request.unit.speed = 100.0;
	struct UNIT_PROFILE profile = {0.0, 0.0, 1.0};
	struct UNIT_ROLLOUT result;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &profile, &request, 0U,
								&result));
	assert(result.plan.route_cursor == 1U && result.plan.target_z > 400.0);
	assert(result.plan.target_y > 100.0);
	points[2].stunt = UNIT_STUNT_RAMP;
	points[2].height_unspecified = 1U;
	points[2].y = -2000.0;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &profile, &request, 0U,
								&result));
	assert(result.plan.route_cursor == 1U && result.plan.target_z > 400.0);
	assert(result.plan.target_y == request.unit.y);
	points[2].stunt = UNIT_STUNT_PROTECTED;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, &limits, 1U, &profile, &request, 0U,
								&result));
	assert(result.plan.target_z == points[1].z);
}

static void test_ramp_exit_lookahead(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	points[0].stunt = points[1].stunt = UNIT_STUNT_RAMP;
	points[0].height_unspecified = points[1].height_unspecified = 1U;
	points[0].y = points[1].y = -2000.0;
	points[1].z = 668.0;
	points[2].z = 2048.0;
	points[3].z = 2716.0;
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_REQUEST request = {0};
	request.cursor = 1U;
	request.unit = vehicle();
	request.unit.x = -48.0;
	request.unit.y = 400.0;
	request.unit.z = 500.0;
	request.unit.speed = 110.0;
	struct UNIT_PROFILE profile = {0.0, 0.0, 1.0};
	struct UNIT_ROLLOUT result;
	for (legacy_u32 step = 0; step < 2U; step++) {
		/* The last native ramp guide precedes the physical lip. A lateral error
		 * must not turn this shrinking vertex distance into a sharp launch yaw. */
		assert(unit_planner_rollout(points, 4U, &limits, 1U, &profile, &request, 0U, &result));
		assert(result.plan.route_cursor == 1U);
		assert(result.plan.target_x == 0.0 && result.plan.target_z > points[1].z);
		assert(result.plan.target_z - request.unit.z >= 240.0);
		assert(result.plan.target_y == request.unit.y && !result.plan.brake);
		request.unit.z = 630.0;
	}
}

static legacy_f64 crest_speed(legacy_u8 unspecified)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	for (legacy_u32 index = 0; index < TEST_POINT_COUNT; index++) {
		points[index].y = fmin(index, 4U) * 100.0;
		points[index].height_unspecified = unspecified;
	}
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_PLANNER *planner = unit_planner_create(points, TEST_POINT_COUNT, &limits, 1U, -1);
	assert(planner);
	finish_sync_preparation(planner);
	struct UNIT_VEHICLE unit = vehicle();
	struct UNIT_PLAN plan;
	assert(unit_planner_plan(planner, &unit, NULL, 0U, 1U, &plan));
	legacy_f64 result = plan.target_speed;
	unit_planner_destroy(planner);
	return result;
}

#ifndef RESTUNTS_UNIT_SYNC_ONLY
#if !RESTUNTS_UNIT_TEST_FIXED_WORK
static void test_learning_worker_snapshots(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_PLANNER *planner = unit_planner_create(points, TEST_POINT_COUNT, &limits, 0U, 2);
	assert(planner);
	struct UNIT_PLANNER_LEARNING saved, current;
	legacy_u64 deadline = SDL_GetTicks() + TEST_WAIT_LIMIT_MS;
	do {
		unit_planner_work(planner, 2U);
		assert(unit_planner_export_learning(planner, &saved));
		assert(saved.completed_samples <= saved.next_sample);
		if (saved.ready && saved.completed_samples >= 8U) {
			break;
		}
		SDL_Delay(1U);
	} while (SDL_GetTicks() < deadline);
	assert(saved.ready && saved.completed_samples >= 8U);
	/* Import joins bounded in-flight jobs before restoring a coherent RNG
	 * snapshot, then restarts the worker pool at the saved next sample. */
	assert(unit_planner_import_learning(planner, &saved));
	assert(unit_planner_export_learning(planner, &current));
	assert(current.ready && current.next_sample >= saved.next_sample);
	assert(current.completed_samples >= saved.completed_samples);
	assert(current.completed_samples <= current.next_sample && current.score <= saved.score);
	unit_planner_destroy(planner);
}

#endif

static legacy_f64 adopt_worker_test_profile(struct UNIT_PLANNER *planner,
											const struct UNIT_ROUTE_POINT *points,
											const struct UNIT_CAR_LIMITS *limits)
{
	struct UNIT_PLANNER_PROFILE profile = {TEST_WORKER_LANE, 0.0, 1.0};
	struct UNIT_PLANNER *candidate = unit_planner_clone_profile(planner, &profile);
	assert(candidate && unit_planner_adopt_profiles(planner, candidate));
	unit_planner_destroy(candidate);
	struct UNIT_REQUEST request = {0};
	request.unit = vehicle();
	request.unit.speed = TEST_WORKER_UNIT_SPEED;
	request.unit.vz = request.unit.speed * TEST_WORLD_PER_MPH;
	struct UNIT_PROFILE active = {profile.lane, profile.apex, profile.lookahead};
	struct UNIT_ROLLOUT baseline, centered;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, limits, 1U, &active, &request, 0U,
								&baseline));
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, limits, 1U, &active, &request,
								TEST_WORKER_CENTER_CANDIDATE, &centered));
	assert(centered.score < baseline.score);
	legacy_f64 offset = baseline.plan.target_x;
	request.unit.x = offset;
	request.unit.speed = TEST_WORKER_REFRESH_SPEED;
	request.unit.vz = request.unit.speed * TEST_WORLD_PER_MPH;
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, limits, 1U, &active, &request, 0U,
								&baseline));
	assert(unit_planner_rollout(points, TEST_POINT_COUNT, limits, 1U, &active, &request,
								TEST_WORKER_CENTER_CANDIDATE, &centered));
	assert(baseline.score < centered.score);
	return offset;
}

static void wait_for_worker_batch(struct UNIT_PLANNER *planner, legacy_u64 previous_candidates)
{
	struct UNIT_PLANNER_STATS stats;
	legacy_u64 deadline = SDL_GetTicks() + TEST_WAIT_LIMIT_MS;
	do {
		unit_planner_stats(planner, &stats);
		assert(stats.workers == 1U);
		if (stats.live_candidates - previous_candidates >= UNIT_PLANNER_LIVE_CANDIDATES) {
			return;
		}
		SDL_Delay(1U);
	} while (SDL_GetTicks() < deadline);
	assert(0 && "live worker did not finish its candidate batch");
}

#if RESTUNTS_UNIT_TEST_FIXED_WORK
static void test_fixed_startup_and_real_live_worker(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_PLANNER *first =
		unit_planner_create(points, TEST_POINT_COUNT, &limits, 1U, TEST_FIXED_FIRST_WORKERS);
	struct UNIT_PLANNER *second =
		unit_planner_create(points, TEST_POINT_COUNT, &limits, 1U, TEST_FIXED_SECOND_WORKERS);
	assert(first && second);
	struct UNIT_PLANNER_STATS first_stats, second_stats;
	unit_planner_stats(first, &first_stats);
	unit_planner_stats(second, &second_stats);
	assert(!first_stats.workers && !second_stats.workers);
	assert(!first_stats.route_ready && !second_stats.route_ready);
	struct UNIT_PLANNER_LEARNING a, b, delayed;
	assert(unit_planner_export_learning(first, &a));
	SDL_Delay(TEST_FIXED_DELAY_MS);
	assert(unit_planner_export_learning(second, &b));
	assert_learning_equal(&a, &b);
	for (legacy_u32 call = 0U; call < TEST_FIXED_WORK_CALLS; call++) {
		unit_planner_work(first, 1U);
		assert(unit_planner_export_learning(first, &a));
		SDL_Delay(TEST_FIXED_DELAY_MS);
		assert(unit_planner_export_learning(first, &delayed));
		assert_learning_equal(&a, &delayed);
		unit_planner_work(second, 1U);
		assert(unit_planner_export_learning(second, &b));
		assert_learning_equal(&a, &b);
		unit_planner_stats(first, &first_stats);
		unit_planner_stats(second, &second_stats);
		assert(!first_stats.workers && !second_stats.workers);
		assert(first_stats.route_candidates == second_stats.route_candidates);
	}
	assert(a.ready && a.completed_samples > 0U);
	/* Requested startup concurrency cannot change explicit preparation. The
	 * live worker remains real and must supply an actually applied proposal. */
	unit_planner_finish_startup(first);
	unit_planner_finish_startup(second);
	unit_planner_stats(first, &first_stats);
	unit_planner_stats(second, &second_stats);
	assert(first_stats.workers == 1U && second_stats.workers == 1U);
	legacy_f64 offset = adopt_worker_test_profile(first, points, &limits);
	struct UNIT_VEHICLE unit = vehicle();
	unit.speed = TEST_WORKER_UNIT_SPEED;
	unit.vz = unit.speed * TEST_WORLD_PER_MPH;
	legacy_u64 previous_candidates = first_stats.live_candidates;
	struct UNIT_PLAN plan;
	assert(unit_planner_plan(first, &unit, NULL, 0U, 1U, &plan));
	wait_for_worker_batch(first, previous_candidates);
	assert(unit_planner_plan(first, &unit, NULL, 0U, 2U, &plan));
	unit_planner_stats(first, &first_stats);
	assert(first_stats.worker_accepted && plan.from_worker);
	assert(!plan.brake && fabs(plan.target_x) < TEST_POSITION_TOLERANCE);
	assert(unit_planner_pause_live(first));
	struct UNIT_PLANNER *snapshot = unit_planner_clone_continuation(first);
	assert(snapshot);
	legacy_u64 saved_candidates = first_stats.live_candidates;
	assert(unit_planner_resume_live(first));
	unit.x = offset;
	unit.speed = TEST_WORKER_REFRESH_SPEED;
	unit.vz = unit.speed * TEST_WORLD_PER_MPH;
	assert(unit_planner_plan(first, &unit, NULL, 0U, 3U, &plan));
	assert(!plan.brake && !plan.from_worker);
	assert(fabs(plan.target_x - unit.x) < TEST_POSITION_TOLERANCE);
	unit_planner_stats(first, &first_stats);
	assert(first_stats.live_candidates == saved_candidates + UNIT_PLANNER_LIVE_CANDIDATES);
	assert(unit_planner_pause_live(first));
	assert(unit_planner_restore_continuation(first, snapshot));
	assert(unit_planner_resume_live(first));
	/* Restoring request identity must also restore its completion. Otherwise
	 * this synchronized getter waits for a duplicate batch after resume. */
	unit_planner_stats(first, &first_stats);
	assert(first_stats.workers == 1U && first_stats.live_candidates == saved_candidates);
	unit_planner_destroy(snapshot);
	unit_planner_destroy(first);
	unit_planner_destroy(second);
}
#else
static void test_worker_lifecycle(void)
{
	struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
	straight(points);
	struct UNIT_CAR_LIMITS limits = car_limits();
	struct UNIT_PLANNER *planner = unit_planner_create(points, TEST_POINT_COUNT, &limits, 1U, 2);
	assert(planner);
	struct UNIT_PLANNER_STATS stats;
	legacy_u64 deadline = SDL_GetTicks() + TEST_WAIT_LIMIT_MS;
	do {
		unit_planner_stats(planner, &stats);
		assert(stats.workers == 2U);
		if (stats.route_ready) {
			break;
		}
		SDL_Delay(1U);
	} while (SDL_GetTicks() < deadline);
	assert(stats.opening_ready && stats.route_ready);
	assert(!unit_planner_clone_continuation(planner));
	assert(!unit_planner_pause_live(planner) && !unit_planner_resume_live(planner));
	struct UNIT_PLANNER *pilot = unit_planner_clone_sync(planner);
	assert(pilot);
	unit_planner_stats(pilot, &stats);
	assert(!stats.workers && stats.opening_ready && stats.route_ready);
	unit_planner_stats(planner, &stats);
	assert(stats.workers == 2U);
	planner = unit_planner_rebuild(planner, points, TEST_POINT_COUNT);
	assert(planner);
	unit_planner_stats(planner, &stats);
	assert(stats.opening_ready && stats.workers == 2U);
	assert(unit_planner_adopt_profiles(planner, pilot));
	unit_planner_destroy(pilot);
	unit_planner_finish_startup(planner);
	unit_planner_stats(planner, &stats);
	assert(stats.workers == 1U);
	legacy_f64 offset = adopt_worker_test_profile(planner, points, &limits);
	struct UNIT_VEHICLE unit = vehicle();
	unit.speed = TEST_WORKER_UNIT_SPEED;
	unit.vz = unit.speed * TEST_WORLD_PER_MPH;
	struct UNIT_PLAN plan;
	assert(unit_planner_plan(planner, &unit, NULL, 0U, 1U, &plan));
	/* Preserve one current request until the real worker has evaluated every
	 * line. Its centered proposal beats the adopted offset line at this pose. */
	wait_for_worker_batch(planner, stats.live_candidates);
	assert(unit_planner_plan(planner, &unit, NULL, 0U, 2U, &plan));
	unit_planner_stats(planner, &stats);
	assert(stats.worker_accepted && plan.from_worker);
	assert(!plan.brake && fabs(plan.target_x) < TEST_POSITION_TOLERANCE);
	/* An old winning proposal must be re-evaluated against the current UNIT
	 * pose: on its adopted line, steering back to the center is now worse. */
	unit.x = offset;
	unit.speed = TEST_WORKER_REFRESH_SPEED;
	unit.vz = unit.speed * TEST_WORLD_PER_MPH;
	assert(unit_planner_plan(planner, &unit, NULL, 0U, 3U, &plan));
	assert(!plan.brake && !plan.from_worker);
	assert(fabs(plan.target_x - unit.x) < TEST_POSITION_TOLERANCE);
	assert(unit_planner_pause_live(planner) && unit_planner_pause_live(planner));
	unit_planner_stats(planner, &stats);
	assert(!stats.workers);
	pilot = unit_planner_clone_continuation(planner);
	assert(pilot);
	unit_planner_destroy(pilot);
	assert(unit_planner_resume_live(planner) && unit_planner_resume_live(planner));
	unit_planner_stats(planner, &stats);
	assert(stats.workers == 1U);
	unit_planner_destroy(planner);
}
#endif
#endif

legacy_int main(void)
{
	test_preparation_and_copy();
	test_learning_resume_and_native_profiles();
	test_interrupted_preparation();
	test_curvature_and_ice();
	test_banked_corner_speed();
	test_clearance_preserves_ordinary_planning();
	test_clearance_alignment();
	test_human_independent_plans();
	test_validation();
	test_complete_route_rebuild();
	test_pilot_clones();
	test_physical_corridor();
	test_dense_graded_geometry();
	test_stunt_approach();
	test_gate_alignment_preserves_open_neighbors();
	test_fallback_speed_policy();
	test_fallback_alignment_policy();
	test_recovery_before_stunt();
	test_ordered_stunt_guidance();
	test_stunt_exit_continuation();
	test_ramp_exit_lookahead();
	assert(crest_speed(0U) < crest_speed(1U) * 0.8);
#ifndef RESTUNTS_UNIT_SYNC_ONLY
#if RESTUNTS_UNIT_TEST_FIXED_WORK
	test_fixed_startup_and_real_live_worker();
#else
	test_learning_worker_snapshots();
	test_worker_lifecycle();
#endif
#endif
	puts("UNIT planner tests passed");
	return 0;
}
