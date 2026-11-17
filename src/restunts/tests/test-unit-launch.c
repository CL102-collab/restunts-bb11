#include <assert.h>
#include <math.h>
#include "../c/opponent/unit_launch.h"
#include "../c/opponent/unit_opponent_internal.h"
#include "../c/extended/extended_owoot_internal.h"

/* Front guide targets can already lie on the receiver while the physical car
 * is still airborne. Exercise the phase contract without a planner or timer. */
#define TEST_DIRECTION_COUNT 4U
#define TEST_RUNUP_PREFIX 4U
#define TEST_WORLD_TILE 1024.0
#define TEST_PIECE_COUNT 2U
#define TEST_RECEIVER_SAMPLES 4U
#define TEST_RAMP_SAMPLES 2U
#define TEST_BARREL_SAMPLES 3U
#define TEST_PREFIX_SAMPLES 2U
#define TEST_PROTECTED_SAMPLES 10U
#define TEST_MAXIMUM_POINTS (TEST_PREFIX_SAMPLES + TEST_PROTECTED_SAMPLES + TEST_RECEIVER_SAMPLES)
#define TEST_PROTECTED_PIECE 1
#define TEST_STAGED_SAMPLE_SPACING 200.0
#define TEST_EARLY_GUIDE_DISTANCE 64.0
#define TEST_ENTRY_SPEED 35.0
#define TEST_EXIT_SPEED 120.0
#define TEST_CAR_MAXIMUM_SPEED 140.0
#define TEST_EXCESSIVE_EXIT_SPEED 190.0
#define TEST_OVERRIDE_ENTRY_SPEED 42.0
#define TEST_OVERRIDE_EXIT_SPEED 110.0
#define TEST_LIP_DISTANCE 668.0
#define TEST_BARREL_FLIGHT_GUIDE 1024.0
#define TEST_RECEIVER_DISTANCE 2048.0
#define TEST_RECEIVER_SAMPLE_SPACING 10.0
#define TEST_BEFORE_RECEIVER 50.0
#define TEST_AFTER_RECEIVER 20.0
#define TEST_APPROACH_DISTANCE 100.0
#define TEST_CURVED_APPROACH_DISTANCE 500.0
#define TEST_BARREL_LOOKAHEAD 260.0
#define TEST_BARREL_NARROW_WIDTH 50.0
#define TEST_BARREL_CAR_HALF_WIDTH 19.0
#define TEST_BARREL_USABLE_WIDTH 27.0
#define TEST_BARREL_ENTRY_USABLE_WIDTH 97.0
#define TEST_BARREL_TRIM_CASES 4U
#define TEST_BARREL_EXIT_FRACTION 0.9375
#define TEST_ORIGIN_X 30000.0
#define TEST_ORIGIN_Z 70000.0
#define TEST_ROAD_HEIGHT 900.0
#define TEST_ROAD_HALF_WIDTH 120.0
#define TEST_LAUNCH_SPEED 100.0
#define TEST_VALIDATED_SPEED 93.0
#define TEST_PLANNER_SPEED 43.0
#define TEST_TARGET_DISTANCE 2400.0
#define TEST_TARGET_LATERAL 83.0
#define TEST_TARGET_HEIGHT 1250.0

struct GAMESTATE state;
legacy_s16 owoot_enabled;
static struct UNIT_ROUTE_POINT points[TEST_MAXIMUM_POINTS];
static legacy_f64 verified[TEST_PIECE_COUNT];
static struct UNIT_EXIT_PROFILE exits[TEST_PIECE_COUNT];
static struct UNIT_ROUTE route;
static legacy_u32 launch_samples;
static legacy_u8 direction;

static void world_position(legacy_f64 along, legacy_f64 lateral, legacy_f64 *x, legacy_f64 *z)
{
	switch (direction) {
		case 0:
			*x = TEST_ORIGIN_X + along;
			*z = TEST_ORIGIN_Z + lateral;
			break;
		case 1:
			*x = TEST_ORIGIN_X - lateral;
			*z = TEST_ORIGIN_Z + along;
			break;
		case 2:
			*x = TEST_ORIGIN_X - along;
			*z = TEST_ORIGIN_Z - lateral;
			break;
		default:
			*x = TEST_ORIGIN_X + lateral;
			*z = TEST_ORIGIN_Z - along;
			break;
	}
}

static void reset_fixture(legacy_u8 stunt, legacy_u8 heading)
{
	state = (struct GAMESTATE){0};
	owoot_enabled = 0;
	route = (struct UNIT_ROUTE){0};
	direction = heading;
	launch_samples = stunt == UNIT_STUNT_BARREL ? TEST_BARREL_SAMPLES : TEST_RAMP_SAMPLES;
	for (legacy_u32 piece = 0; piece < TEST_PIECE_COUNT; piece++) {
		verified[piece] = 0.0;
	}
	if (stunt != UNIT_STUNT_RAMP) {
		verified[0] = TEST_VALIDATED_SPEED;
	}
	route.points = points;
	route.count = launch_samples + TEST_RECEIVER_SAMPLES;
	route.finish_begin = route.count;
	route.verified_speeds = verified;
	route.verified_count = TEST_PIECE_COUNT;
	for (legacy_u32 index = 0; index < route.count; index++) {
		struct UNIT_ROUTE_POINT *point = &points[index];
		*point = (struct UNIT_ROUTE_POINT){0};
		legacy_f64 along = index == 0	? 0.0
						   : index == 1 ? TEST_LIP_DISTANCE
										: TEST_BARREL_FLIGHT_GUIDE;
		if (index >= launch_samples) {
			along =
				TEST_RECEIVER_DISTANCE + (index - launch_samples) * TEST_RECEIVER_SAMPLE_SPACING;
		}
		world_position(along, 0.0, &point->x, &point->z);
		point->y = TEST_ROAD_HEIGHT;
		point->half_width = TEST_ROAD_HALF_WIDTH;
		point->surface = CAR_SURFACE_PAVED;
		point->speed_limit = TEST_LAUNCH_SPEED;
		point->route_index = index < launch_samples ? 0 : 1;
		point->route_point = (legacy_u16)(index < launch_samples ? index : index - launch_samples);
		point->stunt = index < launch_samples ? stunt : UNIT_STUNT_NONE;
	}
}

static void place_car(legacy_f64 along, legacy_int grounded, legacy_s16 roll)
{
	legacy_f64 x, z;
	world_position(along, 0.0, &x, &z);
	struct CARSTATE *car = &state.opponentstate;
	car->car_position.lx = (legacy_s32)(x * UNIT_POSITION_SCALE);
	car->car_position.ly = (legacy_s32)(TEST_ROAD_HEIGHT * UNIT_POSITION_SCALE);
	car->car_position.lz = (legacy_s32)(z * UNIT_POSITION_SCALE);
	car->car_rotate.z = roll;
	car->car_sumSurfAllWheels =
		grounded ? CARSTATE_WHEEL_COUNT * CAR_SURFACE_PAVED : CAR_WHEEL_CONTACT_NONE;
	for (legacy_u32 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		car->car_surfaceWhl[wheel] = grounded ? CAR_SURFACE_PAVED : CAR_WHEEL_CONTACT_NONE;
	}
}

static struct UNIT_PLAN planner_target(legacy_u8 brake)
{
	struct UNIT_PLAN plan = {0};
	world_position(TEST_TARGET_DISTANCE, TEST_TARGET_LATERAL, &plan.target_x, &plan.target_z);
	plan.target_y = TEST_TARGET_HEIGHT;
	plan.target_speed = TEST_PLANNER_SPEED;
	plan.route_cursor = route.count - 1U;
	plan.valid = 1;
	plan.brake = brake;
	return plan;
}

static void assert_target_unchanged(const struct UNIT_PLAN *actual,
									const struct UNIT_PLAN *expected)
{
	assert(actual->target_x == expected->target_x);
	assert(actual->target_y == expected->target_y);
	assert(actual->target_z == expected->target_z);
	assert(actual->route_cursor == expected->route_cursor);
}

static void check_dense_receiver(legacy_u8 stunt, legacy_u8 heading)
{
	reset_fixture(stunt, heading);
	place_car(TEST_RECEIVER_DISTANCE - TEST_BEFORE_RECEIVER, 0, 0);
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_STOP), original = plan;
	/* Native route advancement consumes the first two receiver guides, 50 and
	 * 60 units ahead, inside its 64-unit reach radius. The third target is thus
	 * last + 3 even though the car has not physically reached the receiver. */
	legacy_u32 cursor = launch_samples + 2U;
	legacy_int phase = unit_launch_guidance(&route, cursor, &plan, ROUTE_INDEX_NONE, 0.0);
	assert(phase == (stunt == UNIT_STUNT_RAMP ? UNIT_LAUNCH_GUIDED : UNIT_LAUNCH_NEUTRAL));
	assert(plan.target_speed ==
		   (stunt == UNIT_STUNT_RAMP ? TEST_LAUNCH_SPEED : TEST_VALIDATED_SPEED));
	assert(plan.brake == UNIT_PLAN_BRAKE_NONE);
	if (stunt == UNIT_STUNT_RAMP) {
		assert_target_unchanged(&plan, &original);
	}
}

static void check_receiver_contact(legacy_u8 stunt, legacy_u8 heading)
{
	reset_fixture(stunt, heading);
	legacy_u32 cursor = launch_samples + 2U;
	place_car(TEST_RECEIVER_DISTANCE + TEST_AFTER_RECEIVER, 0, 0);
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_NONE);
	legacy_int phase = unit_launch_guidance(&route, cursor, &plan, ROUTE_INDEX_NONE, 0.0);
	assert(phase == (stunt == UNIT_STUNT_RAMP ? UNIT_LAUNCH_GUIDED : UNIT_LAUNCH_NEUTRAL));

	/* Crossing the receiver plane in air is insufficient. Native road contact
	 * permits a banked landing without requiring world-upright orientation. */
	place_car(TEST_RECEIVER_DISTANCE + TEST_AFTER_RECEIVER, 1, ANGLE_EIGHTH_TURN);
	plan = planner_target(UNIT_PLAN_BRAKE_STOP);
	struct UNIT_PLAN original = plan;
	phase = unit_launch_guidance(&route, cursor, &plan, ROUTE_INDEX_NONE, 0.0);
	assert(phase == UNIT_LAUNCH_NONE);
	assert(plan.target_speed == original.target_speed && plan.brake == original.brake);
	assert_target_unchanged(&plan, &original);
	/* Grass alone does not prove a landing. A real partial road contact must
	 * release ramps early enough to brake for the next piece; barrels still
	 * retain neutral roll policy until their full landing is supported. */
	for (legacy_u32 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		state.opponentstate.car_surfaceWhl[wheel] = CAR_SURFACE_GRASS;
	}
	plan = original;
	assert(unit_launch_guidance(&route, cursor, &plan, ROUTE_INDEX_NONE, 0.0) ==
		   (stunt == UNIT_STUNT_RAMP ? UNIT_LAUNCH_GUIDED : UNIT_LAUNCH_NEUTRAL));
	place_car(TEST_RECEIVER_DISTANCE + TEST_AFTER_RECEIVER, 0, 0);
	state.opponentstate.car_surfaceWhl[0] = CAR_SURFACE_DIRT;
	plan = original;
	assert(unit_launch_guidance(&route, cursor, &plan, ROUTE_INDEX_NONE, 0.0) ==
		   (stunt == UNIT_STUNT_RAMP ? UNIT_LAUNCH_NONE : UNIT_LAUNCH_NEUTRAL));
	place_car(TEST_RECEIVER_DISTANCE + TEST_AFTER_RECEIVER, 1, ANGLE_EIGHTH_TURN);
	if (stunt == UNIT_STUNT_BARREL) {
		state.opponentstate.car_surfaceWhl[0] = CAR_WHEEL_CONTACT_NONE;
		plan = original;
		assert(unit_launch_guidance(&route, cursor, &plan, ROUTE_INDEX_NONE, 0.0) ==
			   UNIT_LAUNCH_NEUTRAL);
		place_car(TEST_RECEIVER_DISTANCE + TEST_AFTER_RECEIVER, 1, ANGLE_EIGHTH_TURN);
		owoot_enabled = 1;
		state.opponentstate.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] = 1;
		plan = original;
		assert(unit_launch_guidance(&route, cursor, &plan, ROUTE_INDEX_NONE, 0.0) ==
			   UNIT_LAUNCH_NEUTRAL);
		state.opponentstate.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] = 0;
		plan = original;
		assert(unit_launch_guidance(&route, cursor, &plan, ROUTE_INDEX_NONE, 0.0) ==
			   UNIT_LAUNCH_NONE);
	}
}

static void check_ramp_approach_target(legacy_u8 heading)
{
	reset_fixture(UNIT_STUNT_RAMP, heading);
	place_car(-TEST_APPROACH_DISTANCE, 1, 0);
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_NONE), original = plan;
	assert(unit_launch_guidance(&route, 0, &plan, ROUTE_INDEX_NONE, 0.0) == UNIT_LAUNCH_NONE);
	assert(plan.target_speed == TEST_LAUNCH_SPEED);
	assert_target_unchanged(&plan, &original);
}

static void check_banked_ramp_projection(legacy_u8 heading)
{
	reset_fixture(UNIT_STUNT_RAMP, heading);
	place_car(TEST_APPROACH_DISTANCE, 1, ANGLE_EIGHTH_TURN);
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_COLLISION), original = plan;
	assert(unit_launch_guidance(&route, 1, &plan, ROUTE_INDEX_NONE, 0.0) == UNIT_LAUNCH_AXIS);
	assert_target_unchanged(&plan, &original);
	assert(plan.brake == UNIT_PLAN_BRAKE_COLLISION);
	/* Source provenance starts before its inset first guide. The physical
	 * supported lead-in needs the same bank correction before that sample. */
	place_car(-TEST_APPROACH_DISTANCE, 1, ANGLE_EIGHTH_TURN);
	plan = original;
	assert(unit_launch_guidance(&route, 0, &plan, ROUTE_INDEX_NONE, 0.0) == UNIT_LAUNCH_AXIS);
	assert_target_unchanged(&plan, &original);
	state.opponentstate.car_route_index = 1;
	plan = original;
	assert(unit_launch_guidance(&route, 0, &plan, ROUTE_INDEX_NONE, 0.0) == UNIT_LAUNCH_NONE);
	state.opponentstate.car_route_index = 0;
	/* Last native guide lies before the physical lip; supported guidance
	 * must continue beyond that sample until actual receiving contact. */
	place_car(TEST_LIP_DISTANCE + TEST_APPROACH_DISTANCE, 1, ANGLE_EIGHTH_TURN);
	plan = original;
	assert(unit_launch_guidance(&route, 1, &plan, ROUTE_INDEX_NONE, 0.0) == UNIT_LAUNCH_AXIS);
	assert_target_unchanged(&plan, &original);
	state.opponentstate.car_surfaceWhl[0] = CAR_WHEEL_CONTACT_NONE;
	plan = original;
	assert(unit_launch_guidance(&route, 1, &plan, ROUTE_INDEX_NONE, 0.0) == UNIT_LAUNCH_GUIDED);
	assert_target_unchanged(&plan, &original);
	place_car(TEST_APPROACH_DISTANCE, 1, 0);
	plan = original;
	assert(unit_launch_guidance(&route, 1, &plan, ROUTE_INDEX_NONE, 0.0) == UNIT_LAUNCH_GUIDED);
	assert_target_unchanged(&plan, &original);
}

static void check_ramp_runup_window(legacy_u8 stunt, legacy_u8 heading)
{
	reset_fixture(stunt, heading);
	route.limits.max_speed = TEST_CAR_MAXIMUM_SPEED;
	for (legacy_u32 point = route.count; point > 0; point--) {
		points[point - 1U + TEST_RUNUP_PREFIX] = points[point - 1U];
	}
	route.count += TEST_RUNUP_PREFIX;
	route.finish_begin = route.count;
	for (legacy_u32 point = 0; point < TEST_RUNUP_PREFIX; point++) {
		points[point] = (struct UNIT_ROUTE_POINT){0};
		world_position(-TEST_WORLD_TILE * (TEST_RUNUP_PREFIX - point), 0.0, &points[point].x,
					   &points[point].z);
		points[point].route_index = (legacy_s16)(TEST_PIECE_COUNT + point);
	}
	place_car(-TEST_WORLD_TILE * TEST_RUNUP_PREFIX + TEST_APPROACH_DISTANCE, 1, 0);
	state.opponentstate.car_route_index = TEST_PIECE_COUNT;
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_NONE), original = plan;
	assert(unit_launch_guidance(&route, 0, &plan, 0, TEST_OVERRIDE_EXIT_SPEED) == UNIT_LAUNCH_NONE);
	assert_target_unchanged(&plan, &original);
	assert(plan.target_speed == TEST_PLANNER_SPEED);
	if (stunt != UNIT_STUNT_RAMP) {
		return;
	}
	legacy_u8 accepted[TEST_PIECE_COUNT] = {0};
	route.verified_ramp_approaches = accepted;
	for (legacy_u8 tiles = UNIT_RAMP_APPROACH_MINIMUM; tiles <= UNIT_RAMP_APPROACH_MAXIMUM;
		 tiles++) {
		struct UNIT_LAUNCH_POLICY policy = {0, ROUTE_INDEX_NONE, TEST_OVERRIDE_EXIT_SPEED};
		policy.ramps[0] = (struct UNIT_RAMP_APPROACH){0, tiles};
		for (legacy_u32 cursor = 0; cursor < TEST_RUNUP_PREFIX; cursor++) {
			place_car(-TEST_WORLD_TILE * (TEST_RUNUP_PREFIX - cursor) + TEST_APPROACH_DISTANCE, 1,
					  0);
			state.opponentstate.car_route_index = (legacy_s16)(TEST_PIECE_COUNT + cursor);
			plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
			original = plan;
			(void)unit_launch_guidance_policy(&route, cursor, &plan, &policy);
			assert(plan.target_speed == (TEST_RUNUP_PREFIX - cursor <= tiles
											 ? TEST_OVERRIDE_EXIT_SPEED
											 : TEST_PLANNER_SPEED));
			assert(plan.brake == UNIT_PLAN_BRAKE_COLLISION);
			assert_target_unchanged(&plan, &original);
		}
		/* Accepted guidance restores its validated run-up without a preview;
		 * another piece's private candidate cannot replace that window. */
		accepted[0] = tiles;
		legacy_u8 private_tiles = UNIT_RAMP_APPROACH_MINIMUM;
		policy.primary_piece = 1;
		policy.ramps[0] = (struct UNIT_RAMP_APPROACH){1, private_tiles};
		for (legacy_u32 cursor = 0; cursor < TEST_RUNUP_PREFIX; cursor++) {
			place_car(-TEST_WORLD_TILE * (TEST_RUNUP_PREFIX - cursor), 1, 0);
			plan = planner_target(UNIT_PLAN_BRAKE_NONE);
			(void)unit_launch_guidance_policy(&route, cursor, &plan, &policy);
			assert(plan.target_speed ==
				   (TEST_RUNUP_PREFIX - cursor <= tiles ? TEST_LAUNCH_SPEED : TEST_PLANNER_SPEED));
		}
		accepted[0] = 0;
	}
	/* A grouped secondary ramp keeps its own independent approach window. */
	struct UNIT_LAUNCH_POLICY grouped = {1, 0, TEST_ENTRY_SPEED, TEST_OVERRIDE_EXIT_SPEED};
	grouped.ramps[0] = (struct UNIT_RAMP_APPROACH){1, UNIT_RAMP_APPROACH_MINIMUM};
	grouped.ramps[1] = (struct UNIT_RAMP_APPROACH){0, UNIT_RAMP_APPROACH_MAXIMUM};
	accepted[0] = UNIT_RAMP_APPROACH_MINIMUM;
	place_car(-TEST_WORLD_TILE * TEST_RUNUP_PREFIX, 1, 0);
	plan = planner_target(UNIT_PLAN_BRAKE_NONE);
	(void)unit_launch_guidance_policy(&route, 0, &plan, &grouped);
	assert(plan.target_speed == TEST_OVERRIDE_EXIT_SPEED);
	assert(accepted[0] == UNIT_RAMP_APPROACH_MINIMUM);
	grouped.ramps[1].tiles = 0;
	plan = planner_target(UNIT_PLAN_BRAKE_NONE);
	(void)unit_launch_guidance_policy(&route, 0, &plan, &grouped);
	assert(plan.target_speed == TEST_PLANNER_SPEED);
	route.verified_ramp_approaches = NULL;
}

static void check_barrel_approach_target(legacy_u8 heading)
{
	reset_fixture(UNIT_STUNT_BARREL, heading);
	place_car(-TEST_CURVED_APPROACH_DISTANCE, 1, 0);
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_COLLISION), original = plan;
	assert(unit_launch_guidance(&route, 0, &plan, ROUTE_INDEX_NONE, 0.0) == UNIT_LAUNCH_NONE);
	assert_target_unchanged(&plan, &original);
	assert(plan.brake == UNIT_PLAN_BRAKE_COLLISION);

	place_car(-TEST_APPROACH_DISTANCE, 1, 0);
	state.opponentstate.car_actual_speed = (legacy_u16)(TEST_LAUNCH_SPEED * UNIT_SPEED_SCALE);
	plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
	assert(unit_launch_guidance(&route, 0, &plan, ROUTE_INDEX_NONE, 0.0) == UNIT_LAUNCH_NONE);
	legacy_f64 target_x, target_z;
	world_position(TEST_BARREL_LOOKAHEAD - TEST_APPROACH_DISTANCE, 0.0, &target_x, &target_z);
	assert(plan.target_x == target_x && plan.target_z == target_z);
	assert(plan.target_y == TEST_ROAD_HEIGHT && plan.route_cursor == 0);
	assert(plan.brake == UNIT_PLAN_BRAKE_COLLISION);
}

static void check_grounded_barrel_axis(legacy_u8 heading)
{
	reset_fixture(UNIT_STUNT_BARREL, heading);
	place_car(TEST_APPROACH_DISTANCE, 1, ANGLE_EIGHTH_TURN);
	state.opponentstate.car_actual_speed = (legacy_u16)(TEST_LAUNCH_SPEED * UNIT_SPEED_SCALE);
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
	assert(unit_launch_guidance(&route, 1, &plan, ROUTE_INDEX_NONE, 0.0) == UNIT_LAUNCH_AXIS);
	legacy_f64 target_x, target_z;
	world_position(TEST_APPROACH_DISTANCE + TEST_BARREL_LOOKAHEAD, 0.0, &target_x, &target_z);
	assert(plan.target_x == target_x && plan.target_z == target_z);
	assert(plan.brake == UNIT_PLAN_BRAKE_COLLISION);
	state.opponentstate.car_surfaceWhl[0] = CAR_WHEEL_CONTACT_NONE;
	plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
	assert(unit_launch_guidance(&route, 1, &plan, ROUTE_INDEX_NONE, 0.0) == UNIT_LAUNCH_NEUTRAL);
	assert(plan.brake == UNIT_PLAN_BRAKE_COLLISION);
}

static void check_barrel_trim(legacy_u8 heading)
{
	reset_fixture(UNIT_STUNT_BARREL, heading);
	legacy_f64 offsets[TEST_PIECE_COUNT] = {0.5, 0.0}, candidate = -0.75;
	route.verified_barrel_offsets = offsets;
	route.limits.half_width = TEST_BARREL_CAR_HALF_WIDTH;
	points[1].half_width = TEST_BARREL_NARROW_WIDTH;
	place_car(TEST_LIP_DISTANCE - TEST_BARREL_LOOKAHEAD, 1, ANGLE_EIGHTH_TURN);
	state.opponentstate.car_actual_speed = (legacy_u16)(TEST_LAUNCH_SPEED * UNIT_SPEED_SCALE);
	struct UNIT_LAUNCH_POLICY policy = {0, ROUTE_INDEX_NONE, 0.0, 0.0, NULL, &candidate};
	for (legacy_u32 scenario = 0; scenario < TEST_BARREL_TRIM_CASES; scenario++) {
		legacy_f64 expected = scenario == 0	  ? -0.75
							  : scenario == 1 ? 0.5
							  : scenario == 2 ? 1.0
											  : 0.0;
		policy.primary_piece = scenario == 1 ? 1 : 0;
		candidate = scenario < 2 ? -0.75 : 2.0;
		if (scenario == 3) {
			route.limits.half_width = TEST_ROAD_HALF_WIDTH;
		}
		struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
		assert(unit_launch_guidance_policy(&route, 1, &plan, &policy) == UNIT_LAUNCH_AXIS);
		legacy_f64 target_x, target_z;
		world_position(TEST_LIP_DISTANCE, -expected * TEST_BARREL_USABLE_WIDTH, &target_x,
					   &target_z);
		assert(plan.target_x == target_x && plan.target_z == target_z);
		assert(plan.brake == UNIT_PLAN_BRAKE_COLLISION && offsets[0] == 0.5);
	}
	/* An existing avoidance lane blends toward the bounded axis; offsets
	 * never add together. Equal coordinate endpoints also prove unchanged
	 * longitudinal target in all four headings. */
	route.limits.half_width = TEST_BARREL_CAR_HALF_WIDTH;
	policy.primary_piece = 0;
	place_car(-TEST_CURVED_APPROACH_DISTANCE, 1, 0);
	for (legacy_u32 side = 0; side < 2U; side++) {
		candidate = side ? 0.75 : -0.75;
		struct UNIT_PLAN blended = planner_target(UNIT_PLAN_BRAKE_COLLISION), original = blended;
		legacy_f64 bx, bz;
		world_position(TEST_TARGET_DISTANCE, -candidate * TEST_BARREL_ENTRY_USABLE_WIDTH, &bx, &bz);
		assert(unit_launch_guidance_policy(&route, 0, &blended, &policy) == UNIT_LAUNCH_NONE);
		assert(blended.target_x >= (original.target_x < bx ? original.target_x : bx));
		assert(blended.target_x <= (original.target_x > bx ? original.target_x : bx));
		assert(blended.target_z >= (original.target_z < bz ? original.target_z : bz));
		assert(blended.target_z <= (original.target_z > bz ? original.target_z : bz));
		assert(blended.brake == UNIT_PLAN_BRAKE_COLLISION);
	}
	place_car(TEST_APPROACH_DISTANCE, 0, ANGLE_EIGHTH_TURN);
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_COLLISION), original = plan;
	assert(unit_launch_guidance_policy(&route, 1, &plan, &policy) == UNIT_LAUNCH_NEUTRAL);
	assert_target_unchanged(&plan, &original);
}

static void check_barrel_width_following(legacy_u8 heading)
{
	reset_fixture(UNIT_STUNT_BARREL, heading);
	route.limits.half_width = TEST_BARREL_CAR_HALF_WIDTH;
	points[1].half_width = TEST_BARREL_NARROW_WIDTH;
	static const legacy_f64 fractions[] = {0.25, 0.5, 1.0};
	/* Linear projected widths102.5,85,50 minus the fixed23-unit clearance. */
	static const legacy_f64 usable[] = {79.5, 62.0, TEST_BARREL_USABLE_WIDTH};
	legacy_f64 trim = 0.5;
	struct UNIT_LAUNCH_POLICY policy = {0, ROUTE_INDEX_NONE, 0.0, 0.0, NULL, &trim};
	for (legacy_u32 sample = 0; sample < sizeof(fractions) / sizeof(fractions[0]); sample++) {
		legacy_f64 target = TEST_LIP_DISTANCE * fractions[sample], x, z;
		place_car(target - TEST_BARREL_LOOKAHEAD, 1, ANGLE_EIGHTH_TURN);
		state.opponentstate.car_actual_speed = (legacy_u16)(TEST_LAUNCH_SPEED * UNIT_SPEED_SCALE);
		struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
		trim = 0.5;
		(void)unit_launch_guidance_policy(&route, 1, &plan, &policy);
		world_position(target, -trim * usable[sample], &x, &z);
		assert(plan.target_x == x && plan.target_z == z);
		assert(plan.brake == UNIT_PLAN_BRAKE_COLLISION);
		trim = 0.0;
		plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
		(void)unit_launch_guidance_policy(&route, 1, &plan, &policy);
		world_position(target, 0.0, &x, &z);
		assert(plan.target_x == x && plan.target_z == z);
	}
}

static void check_barrel_receiving_heading(legacy_u8 heading)
{
	reset_fixture(UNIT_STUNT_BARREL, heading);
	route.limits.half_width = TEST_BARREL_CAR_HALF_WIDTH;
	points[1].half_width = TEST_BARREL_NARROW_WIDTH;
	legacy_f64 trim = 0.75;
	struct UNIT_LAUNCH_POLICY policy = {0, ROUTE_INDEX_NONE, 0.0, 0.0, NULL, &trim};
	place_car(TEST_LIP_DISTANCE - 1.0 / UNIT_POSITION_SCALE, 1, ANGLE_EIGHTH_TURN);
	state.opponentstate.car_actual_speed = (legacy_u16)(TEST_LAUNCH_SPEED * UNIT_SPEED_SCALE);
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
	assert(unit_launch_guidance_policy(&route, 1, &plan, &policy) == UNIT_LAUNCH_AXIS);
	legacy_f64 expected_x, expected_z;
	world_position(TEST_LIP_DISTANCE - 1.0 / UNIT_POSITION_SCALE + TEST_BARREL_LOOKAHEAD, 0.0,
				   &expected_x, &expected_z);
	/* At release the ray points down the receiving line to within one native
	 * position unit, instead of retaining the strip's twenty-unit offset. */
	assert(fabs(plan.target_x - expected_x) < 1.0 / UNIT_POSITION_SCALE);
	assert(fabs(plan.target_z - expected_z) < 1.0 / UNIT_POSITION_SCALE);
	assert(plan.brake == UNIT_PLAN_BRAKE_COLLISION);
	trim = 0.0;
	plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
	assert(unit_launch_guidance_policy(&route, 1, &plan, &policy) == UNIT_LAUNCH_AXIS);
	assert(plan.target_x == expected_x && plan.target_z == expected_z);
}

static void check_barrel_exit_profile(legacy_u8 heading)
{
	reset_fixture(UNIT_STUNT_BARREL, heading);
	route.verified_exits = exits;
	legacy_f64 offsets[TEST_PIECE_COUNT] = {0.5, 0.0};
	route.verified_barrel_offsets = offsets;
	route.limits.half_width = TEST_BARREL_CAR_HALF_WIDTH;
	points[1].half_width = TEST_BARREL_NARROW_WIDTH;
	state.opponentstate.car_actual_speed = (legacy_u16)(TEST_LAUNCH_SPEED * UNIT_SPEED_SCALE);
	route.limits.max_speed = TEST_CAR_MAXIMUM_SPEED;
	exits[0] = (struct UNIT_EXIT_PROFILE){TEST_BARREL_EXIT_FRACTION, TEST_OVERRIDE_ENTRY_SPEED};
	legacy_f64 threshold = TEST_LIP_DISTANCE * TEST_BARREL_EXIT_FRACTION;
	place_car(threshold - TEST_EARLY_GUIDE_DISTANCE, 1, 0);
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
	(void)unit_launch_guidance(&route, TEST_BARREL_SAMPLES - 1U, &plan, ROUTE_INDEX_NONE, 0.0);
	assert(plan.target_speed == TEST_VALIDATED_SPEED && plan.brake == UNIT_PLAN_BRAKE_COLLISION);
	place_car(threshold, 1, 0);
	plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
	(void)unit_launch_guidance(&route, TEST_BARREL_SAMPLES - 1U, &plan, ROUTE_INDEX_NONE, 0.0);
	assert(plan.target_speed == TEST_OVERRIDE_ENTRY_SPEED &&
		   plan.brake == UNIT_PLAN_BRAKE_COLLISION);
	/* The accepted lateral line and physical speed phase compose without
	 * changing each other's policy or the native collision brake. */
	assert(offsets[0] == 0.5);
	struct UNIT_PLAN trimmed = plan;
	plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
	(void)unit_launch_guidance(&route, TEST_BARREL_SAMPLES - 1U, &plan, ROUTE_INDEX_NONE, 0.0);
	assert_target_unchanged(&plan, &trimmed);
	const struct UNIT_EXIT_PROFILE disabled = {0};
	plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
	(void)unit_launch_guidance_profile(&route, TEST_BARREL_SAMPLES - 1U, &plan, 0,
									   TEST_VALIDATED_SPEED, &disabled);
	assert(plan.target_speed == TEST_VALIDATED_SPEED && plan.brake == UNIT_PLAN_BRAKE_COLLISION);
}

static void check_human_collision_brake(legacy_u8 stunt, legacy_u8 heading)
{
	reset_fixture(stunt, heading);
	place_car(TEST_APPROACH_DISTANCE, 1, 0);
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
	(void)unit_launch_guidance(&route, 1U, &plan, 0, TEST_VALIDATED_SPEED);
	assert(plan.brake == UNIT_PLAN_BRAKE_COLLISION);
	if (stunt != UNIT_STUNT_PROTECTED) {
		place_car(TEST_RECEIVER_DISTANCE - TEST_BEFORE_RECEIVER, 0, 0);
		plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
		(void)unit_launch_guidance(&route, launch_samples + 2U, &plan, 0, TEST_VALIDATED_SPEED);
		assert(plan.brake == UNIT_PLAN_BRAKE_COLLISION);
	}
}

static void check_clearance_speed_ceiling(void)
{
	reset_fixture(UNIT_STUNT_PROTECTED, 0);
	place_car(TEST_APPROACH_DISTANCE, 1, 0);
	points[0].clearance_only = 1U;
	/* A certified clearance speed must not defeat braking for the next bend. */
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_STOP), original = plan;
	(void)unit_launch_guidance(&route, 1U, &plan, ROUTE_INDEX_NONE, 0.0);
	assert(plan.target_speed == TEST_PLANNER_SPEED && plan.brake == UNIT_PLAN_BRAKE_STOP);
	assert_target_unchanged(&plan, &original);
	plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
	plan.target_speed = TEST_EXIT_SPEED;
	(void)unit_launch_guidance(&route, 1U, &plan, ROUTE_INDEX_NONE, 0.0);
	assert(plan.target_speed == TEST_VALIDATED_SPEED && plan.brake == UNIT_PLAN_BRAKE_COLLISION);
	/* A later certified exit may relax the ceiling without overriding a lower
	 * planner target or retaining the earlier entry ceiling. */
	route.verified_exits = exits;
	route.limits.max_speed = TEST_CAR_MAXIMUM_SPEED;
	exits[0] = (struct UNIT_EXIT_PROFILE){0.50, TEST_EXIT_SPEED};
	place_car(TEST_LIP_DISTANCE, 1, 0);
	plan = planner_target(UNIT_PLAN_BRAKE_NONE);
	plan.target_speed = TEST_CAR_MAXIMUM_SPEED;
	(void)unit_launch_guidance(&route, 1U, &plan, ROUTE_INDEX_NONE, 0.0);
	assert(plan.target_speed == TEST_EXIT_SPEED);
	plan = planner_target(UNIT_PLAN_BRAKE_NONE);
	(void)unit_launch_guidance(&route, 1U, &plan, ROUTE_INDEX_NONE, 0.0);
	assert(plan.target_speed == TEST_PLANNER_SPEED);
	/* Mandatory sustaining-speed guidance retains its established policy. */
	points[0].clearance_only = 0U;
	route.verified_exits = NULL;
	plan = planner_target(UNIT_PLAN_BRAKE_STOP);
	(void)unit_launch_guidance(&route, 1U, &plan, ROUTE_INDEX_NONE, 0.0);
	assert(plan.target_speed == TEST_VALIDATED_SPEED && plan.brake == UNIT_PLAN_BRAKE_NONE);
}

static void reset_staged_fixture(void)
{
	reset_fixture(UNIT_STUNT_PROTECTED, 0);
	route.count = TEST_MAXIMUM_POINTS;
	route.finish_begin = route.count;
	route.verified_exits = exits;
	route.limits.max_speed = TEST_CAR_MAXIMUM_SPEED;
	for (legacy_u32 piece = 0; piece < TEST_PIECE_COUNT; piece++) {
		verified[piece] = 0.0;
		exits[piece] = (struct UNIT_EXIT_PROFILE){0};
	}
	verified[TEST_PROTECTED_PIECE] = TEST_ENTRY_SPEED;
	for (legacy_u32 index = 0; index < route.count; index++) {
		struct UNIT_ROUTE_POINT *point = &points[index];
		*point = (struct UNIT_ROUTE_POINT){0};
		world_position(index * TEST_STAGED_SAMPLE_SPACING, 0.0, &point->x, &point->z);
		point->y = TEST_ROAD_HEIGHT;
		point->speed_limit = TEST_LAUNCH_SPEED;
		point->half_width = TEST_ROAD_HALF_WIDTH;
		if (index < TEST_PREFIX_SAMPLES) {
			point->route_index = 0;
		} else if (index < TEST_PREFIX_SAMPLES + TEST_PROTECTED_SAMPLES) {
			point->route_index = TEST_PROTECTED_PIECE;
			point->route_point = (legacy_u16)(index - TEST_PREFIX_SAMPLES);
			point->stunt = UNIT_STUNT_PROTECTED;
		} else {
			point->route_index = TEST_PIECE_COUNT;
		}
	}
}

static void check_staged_exit_boundaries(void)
{
	static const legacy_f64 fractions[] = {0.25, 0.50, 0.75};
	/* Ten ordered samples have nine intervals: the first sample at or beyond
	 * each requested fraction is 3, 5 or 7, relative to this piece's first. */
	static const legacy_u32 thresholds[] = {3U, 5U, 7U};
	for (legacy_u32 scenario = 0; scenario < sizeof(fractions) / sizeof(fractions[0]); scenario++) {
		reset_staged_fixture();
		exits[TEST_PROTECTED_PIECE] =
			(struct UNIT_EXIT_PROFILE){fractions[scenario], TEST_EXIT_SPEED};
		for (legacy_u32 cursor = TEST_PREFIX_SAMPLES - 1U;
			 cursor < TEST_PREFIX_SAMPLES + TEST_PROTECTED_SAMPLES; cursor++) {
			place_car(cursor * TEST_STAGED_SAMPLE_SPACING, 1, 0);
			struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_NONE), original = plan;
			(void)unit_launch_guidance(&route, cursor, &plan, ROUTE_INDEX_NONE, 0.0);
			assert(plan.target_speed == (cursor < TEST_PREFIX_SAMPLES + thresholds[scenario]
											 ? TEST_ENTRY_SPEED
											 : TEST_EXIT_SPEED));
			assert_target_unchanged(&plan, &original);
		}
		/* A front guide may have advanced past the transition while the physical
		 * car has not reached it. Neither target index may enable the exit early. */
		legacy_u32 transition = TEST_PREFIX_SAMPLES + thresholds[scenario];
		place_car(transition * TEST_STAGED_SAMPLE_SPACING - TEST_EARLY_GUIDE_DISTANCE, 1, 0);
		for (legacy_u32 cursor = transition; cursor <= transition + 1U; cursor++) {
			struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_NONE);
			(void)unit_launch_guidance(&route, cursor, &plan, ROUTE_INDEX_NONE, 0.0);
			assert(plan.target_speed == TEST_ENTRY_SPEED);
		}
		place_car(transition * TEST_STAGED_SAMPLE_SPACING, 1, 0);
		struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_NONE);
		(void)unit_launch_guidance(&route, transition + 1U, &plan, ROUTE_INDEX_NONE, 0.0);
		assert(plan.target_speed == TEST_EXIT_SPEED);
	}
	reset_staged_fixture();
	exits[TEST_PROTECTED_PIECE] = (struct UNIT_EXIT_PROFILE){0.50, TEST_EXCESSIVE_EXIT_SPEED};
	legacy_u32 last = TEST_PREFIX_SAMPLES + TEST_PROTECTED_SAMPLES - 1U;
	place_car(last * TEST_STAGED_SAMPLE_SPACING, 1, 0);
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_NONE);
	(void)unit_launch_guidance(&route, last, &plan, ROUTE_INDEX_NONE, 0.0);
	assert(plan.target_speed == TEST_CAR_MAXIMUM_SPEED);
}

static void check_staged_override_isolation(void)
{
	reset_staged_fixture();
	exits[TEST_PROTECTED_PIECE] = (struct UNIT_EXIT_PROFILE){0.25, TEST_EXIT_SPEED};
	const struct UNIT_EXIT_PROFILE disabled = {0};
	const struct UNIT_EXIT_PROFILE candidate = {0.75, TEST_OVERRIDE_EXIT_SPEED};
	legacy_u32 last = TEST_PREFIX_SAMPLES + TEST_PROTECTED_SAMPLES - 1U;
	place_car(last * TEST_STAGED_SAMPLE_SPACING, 1, 0);
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_NONE);
	(void)unit_launch_guidance_profile(&route, last, &plan, TEST_PROTECTED_PIECE,
									   TEST_OVERRIDE_ENTRY_SPEED, &disabled);
	assert(plan.target_speed == TEST_OVERRIDE_ENTRY_SPEED);
	assert(exits[TEST_PROTECTED_PIECE].fraction == 0.25 &&
		   exits[TEST_PROTECTED_PIECE].speed == TEST_EXIT_SPEED);
	assert(verified[TEST_PROTECTED_PIECE] == TEST_ENTRY_SPEED);

	/* A candidate belonging to another piece cannot disable this one's already
	 * verified exit; a matching candidate replaces its timing and speed. */
	plan = planner_target(UNIT_PLAN_BRAKE_NONE);
	(void)unit_launch_guidance_profile(&route, last, &plan, 0, TEST_OVERRIDE_ENTRY_SPEED,
									   &disabled);
	assert(plan.target_speed == TEST_EXIT_SPEED);
	place_car((TEST_PREFIX_SAMPLES + 5U) * TEST_STAGED_SAMPLE_SPACING, 1, 0);
	plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
	(void)unit_launch_guidance_profile(&route, TEST_PREFIX_SAMPLES + 5U, &plan,
									   TEST_PROTECTED_PIECE, TEST_OVERRIDE_ENTRY_SPEED, &candidate);
	assert(plan.target_speed == TEST_OVERRIDE_ENTRY_SPEED &&
		   plan.brake == UNIT_PLAN_BRAKE_COLLISION);
	place_car(last * TEST_STAGED_SAMPLE_SPACING, 1, 0);
	plan = planner_target(UNIT_PLAN_BRAKE_COLLISION);
	(void)unit_launch_guidance_profile(&route, last, &plan, TEST_PROTECTED_PIECE,
									   TEST_OVERRIDE_ENTRY_SPEED, &candidate);
	assert(plan.target_speed == TEST_OVERRIDE_EXIT_SPEED &&
		   plan.brake == UNIT_PLAN_BRAKE_COLLISION);
	plan = planner_target(UNIT_PLAN_BRAKE_NONE);
	(void)unit_launch_guidance(&route, last, &plan, ROUTE_INDEX_NONE, 0.0);
	assert(plan.target_speed == TEST_EXIT_SPEED);
}

static void check_secondary_override_isolation(void)
{
	reset_staged_fixture();
	const struct UNIT_EXIT_PROFILE disabled = {0};
	struct UNIT_LAUNCH_POLICY policy = {0, TEST_PROTECTED_PIECE, TEST_OVERRIDE_ENTRY_SPEED,
										TEST_OVERRIDE_EXIT_SPEED, &disabled};
	legacy_u32 cursor = TEST_PREFIX_SAMPLES + 1U;
	place_car(cursor * TEST_STAGED_SAMPLE_SPACING, 1, 0);
	struct UNIT_PLAN plan = planner_target(UNIT_PLAN_BRAKE_COLLISION), original = plan;
	(void)unit_launch_guidance_policy(&route, cursor, &plan, &policy);
	assert(plan.target_speed == TEST_OVERRIDE_EXIT_SPEED);
	assert(plan.brake == UNIT_PLAN_BRAKE_COLLISION);
	assert_target_unchanged(&plan, &original);
	assert(verified[TEST_PROTECTED_PIECE] == TEST_ENTRY_SPEED);

	/* The secondary candidate is private to its selected piece and cannot
	 * change the primary entry or overwrite the retained verified policy. */
	policy.primary_piece = TEST_PROTECTED_PIECE;
	plan = planner_target(UNIT_PLAN_BRAKE_NONE);
	(void)unit_launch_guidance_policy(&route, cursor, &plan, &policy);
	assert(plan.target_speed == TEST_OVERRIDE_ENTRY_SPEED);
	policy.primary_piece = ROUTE_INDEX_NONE;
	policy.secondary_piece = 0;
	plan = planner_target(UNIT_PLAN_BRAKE_NONE);
	(void)unit_launch_guidance_policy(&route, cursor, &plan, &policy);
	assert(plan.target_speed == TEST_ENTRY_SPEED);
	policy.secondary_piece = TEST_PROTECTED_PIECE;
	policy.secondary_speed = TEST_EXCESSIVE_EXIT_SPEED;
	plan = planner_target(UNIT_PLAN_BRAKE_NONE);
	(void)unit_launch_guidance_policy(&route, cursor, &plan, &policy);
	assert(plan.target_speed == TEST_CAR_MAXIMUM_SPEED);
	policy.secondary_speed = 0.0;
	plan = planner_target(UNIT_PLAN_BRAKE_NONE);
	(void)unit_launch_guidance_policy(&route, cursor, &plan, &policy);
	assert(plan.target_speed == TEST_ENTRY_SPEED);
}

legacy_int main(void)
{
	check_clearance_speed_ceiling();
	check_secondary_override_isolation();
	check_staged_exit_boundaries();
	check_staged_override_isolation();
	for (legacy_u8 heading = 0; heading < TEST_DIRECTION_COUNT; heading++) {
		check_dense_receiver(UNIT_STUNT_RAMP, heading);
		check_dense_receiver(UNIT_STUNT_BARREL, heading);
		check_receiver_contact(UNIT_STUNT_RAMP, heading);
		check_receiver_contact(UNIT_STUNT_BARREL, heading);
		check_ramp_approach_target(heading);
		check_banked_ramp_projection(heading);
		check_ramp_runup_window(UNIT_STUNT_RAMP, heading);
		check_ramp_runup_window(UNIT_STUNT_BARREL, heading);
		check_barrel_approach_target(heading);
		check_grounded_barrel_axis(heading);
		check_barrel_trim(heading);
		check_barrel_width_following(heading);
		check_barrel_receiving_heading(heading);
		check_barrel_exit_profile(heading);
		check_human_collision_brake(UNIT_STUNT_RAMP, heading);
		check_human_collision_brake(UNIT_STUNT_BARREL, heading);
		check_human_collision_brake(UNIT_STUNT_PROTECTED, heading);
	}
	return 0;
}
