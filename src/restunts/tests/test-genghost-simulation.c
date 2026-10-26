#include <assert.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../genghost/simulation.h"
#include "../genghost/guide.h"
#include "../genghost/routes.h"
#include "../c/externs.h"
#include "../c/game_input.h"
#include "../c/state_internal.h"
#include "../c/race_route.h"
#include "../c/trackdata_layout.h"
#include "../c/track_objects.h"
#include "../c/track_collision.h"
#include "../c/physics_internal.h"

#undef memcpy
#undef strlen
#undef printf

/* The legacy include path shadows the host math header. */
extern legacy_f64 sqrt(legacy_f64);
extern legacy_f64 fabs(legacy_f64);

/* Nearby samples on a twisting stunt can share position, yaw and pitch while
 * banking in opposite directions. Phase matching must retain the roll. */
static void test_guide_roll(void)
{
	struct CARSTATE original = state.playerstate;
	memset(&state.playerstate, 0, sizeof(state.playerstate));
	state.playerstate.car_transmission = TRANSMISSION_AUTOMATIC;
	state.playerstate.car_rotate.z = 128;
	struct GENGHOST_GUIDE_POINT points[2] = {0};
	points[0].input = INPUT_BRAKE_FLAG;
	points[1].position.lx = 64;
	points[1].rotation.z = 128;
	points[1].input = INPUT_ACCELERATE_FLAG;
	legacy_uint cursor = 0;
	assert(genghost_guide_step(points, 2U, &cursor, 0.0) == INPUT_ACCELERATE_FLAG);
	assert(cursor == 2U);
	/* Signed angular distance wraps across zero instead of rejecting an
	 * otherwise correct point at 1023 against a current roll of zero. */
	state.playerstate.car_rotate.z = 0;
	points[0].rotation.z = 12;
	points[1].rotation.z = 1023;
	cursor = 0;
	assert(genghost_guide_step(points, 2U, &cursor, 0.0) == INPUT_ACCELERATE_FLAG);
	assert(cursor == 2U);
	state.playerstate = original;
	puts("Guide phase distinguishes banked stunt samples and wrapped roll.");
}

static legacy_f64 region_cost(legacy_f64 px, legacy_f64 pz, legacy_f64 ax, legacy_f64 az,
							  legacy_f64 bx, legacy_f64 bz)
{
	return sqrt((px - ax) * (px - ax) + (pz - az) * (pz - az)) +
		   sqrt((px - bx) * (px - bx) + (pz - bz) * (pz - bz));
}

static legacy_int region_contains(const struct GENGHOST_REGION *region, legacy_f64 x, legacy_f64 z)
{
	return x >= region->min_x && x <= region->max_x && z >= region->min_z && z <= region->max_z;
}

static legacy_f64 edge_cost(const struct GENGHOST_REGION *region, legacy_uint edge, legacy_f64 t,
							legacy_f64 ax, legacy_f64 az, legacy_f64 bx, legacy_f64 bz)
{
	legacy_f64 x = edge < 2U ? (edge ? region->max_x : region->min_x)
							 : region->min_x + t * (region->max_x - region->min_x);
	legacy_f64 z = edge < 2U ? region->min_z + t * (region->max_z - region->min_z)
							 : (edge == 3U ? region->max_z : region->min_z);
	return region_cost(x, z, ax, az, bx, bz);
}

/* Independent numerical oracle: sample every edge densely, then refine the
 * best sample's neighbourhood by convex one-dimensional minimization. This
 * deliberately does not reproduce the planner's clipping or weighted formula. */
static legacy_f64 region_oracle(const struct GENGHOST_REGION *region, legacy_f64 ax, legacy_f64 az,
								legacy_f64 bx, legacy_f64 bz)
{
	if (region_contains(region, ax, az) || region_contains(region, bx, bz)) {
		return region_cost(ax, az, ax, az, bx, bz);
	}
	const legacy_uint samples = 512U;
	legacy_f64 best = DBL_MAX;
	for (legacy_uint edge = 0U; edge < 4U; ++edge) {
		legacy_uint index = 0U;
		legacy_f64 edge_best = DBL_MAX;
		for (legacy_uint sample = 0U; sample <= samples; ++sample) {
			legacy_f64 cost = edge_cost(region, edge, (legacy_f64)sample / samples, ax, az, bx, bz);
			if (cost < edge_best) {
				edge_best = cost;
				index = sample;
			}
		}
		legacy_f64 low = index ? (legacy_f64)(index - 1U) / samples : 0.0;
		legacy_f64 high = index < samples ? (legacy_f64)(index + 1U) / samples : 1.0;
		for (legacy_uint iteration = 0U; iteration < 64U; ++iteration) {
			legacy_f64 a = low + (high - low) / 3.0, b = high - (high - low) / 3.0;
			if (edge_cost(region, edge, a, ax, az, bx, bz) <
				edge_cost(region, edge, b, ax, az, bx, bz)) {
				high = b;
			} else {
				low = a;
			}
		}
		legacy_f64 refined = edge_cost(region, edge, (low + high) * 0.5, ax, az, bx, bz);
		if (refined < edge_best) {
			edge_best = refined;
		}
		if (edge_best < best) {
			best = edge_best;
		}
	}
	return best;
}

static void region_expect(const struct GENGHOST_REGION *region, legacy_f64 ax, legacy_f64 az,
						  legacy_f64 bx, legacy_f64 bz, legacy_f64 expected_x,
						  legacy_f64 expected_z)
{
	legacy_f64 x, z;
	genghost_region_crossing(region, ax, az, bx, bz, &x, &z);
	assert(fabs(x - expected_x) < 1e-9 && fabs(z - expected_z) < 1e-9);
	assert(x >= region->min_x - 1e-9 && x <= region->max_x + 1e-9);
	assert(z >= region->min_z - 1e-9 && z <= region->max_z + 1e-9);
}

static legacy_f64 region_random(legacy_u32 *seed)
{
	*seed = *seed * UINT32_C(1664525) + UINT32_C(1013904223);
	return (legacy_f64)*seed / 4294967296.0;
}

static void test_regions(void)
{
	const struct GENGHOST_REGION square = {0.0, 10.0, 0.0, 10.0};
	region_expect(&square, -5.0, 5.0, 15.0, 5.0, 10.0, 5.0);
	region_expect(&square, 15.0, 5.0, -5.0, 5.0, 0.0, 5.0);
	region_expect(&square, -5.0, -5.0, 15.0, 15.0, 10.0, 10.0);
	region_expect(&square, 5.0, 5.0, 15.0, 5.0, 10.0, 5.0);
	region_expect(&square, -5.0, 5.0, 5.0, 5.0, 5.0, 5.0);
	region_expect(&square, 2.0, 3.0, 7.0, 8.0, 7.0, 8.0);
	region_expect(&square, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0);
	region_expect(&square, -5.0, 15.0, -5.0, 15.0, 0.0, 10.0);
	region_expect(&square, -5.0, 0.0, 15.0, 0.0, 10.0, 0.0);
	region_expect(&square, -5.0, 12.0, 15.0, 12.0, 5.0, 10.0);
	region_expect(&square, -3.0, 2.0, -7.0, 8.0, 0.0, 3.8);
	region_expect(&square, -5.0, -2.0, -4.0, -7.0, 0.0, 0.0);
	/* Tiny movement across a boundary must not be mistaken for a parallel line. */
	region_expect(&square, 5.0, 9.99999, 5.0, 10.00001, 5.0, 10.0);
	region_expect(&square, 9.99999, 5.0, 10.00001, 5.0, 10.0, 5.0);
	const struct GENGHOST_REGION line = {3.0, 3.0, 0.0, 10.0};
	region_expect(&line, 0.0, 1.0, 8.0, 9.0, 3.0, 4.0);
	region_expect(&line, 3.0, -5.0, 3.0, 15.0, 3.0, 10.0);
	region_expect(&line, 0.0, 4.0, 0.0, 4.0, 3.0, 4.0);
	const struct GENGHOST_REGION point = {3.0, 3.0, 7.0, 7.0};
	region_expect(&point, 0.0, 0.0, 9.0, 4.0, 3.0, 7.0);

	legacy_u32 seed = UINT32_C(0x6a09e667);
	for (legacy_uint trial = 0U; trial < 1000U; ++trial) {
		struct GENGHOST_REGION region;
		region.min_x = (region_random(&seed) - 0.5) * 61440.0;
		region.min_z = (region_random(&seed) - 0.5) * 61440.0;
		region.max_x = region.min_x + (trial % 17U ? 2048.0 * region_random(&seed) : 0.0);
		region.max_z = region.min_z + (trial % 19U ? 2048.0 * region_random(&seed) : 0.0);
		legacy_f64 ax = region.min_x + (region_random(&seed) - 0.5) * 8192.0;
		legacy_f64 az = region.min_z + (region_random(&seed) - 0.5) * 8192.0;
		legacy_f64 bx = region.max_x + (region_random(&seed) - 0.5) * 8192.0;
		legacy_f64 bz = region.max_z + (region_random(&seed) - 0.5) * 8192.0;
		if (trial % 23U == 0U) {
			bx = ax;
		}
		if (trial % 29U == 0U) {
			bz = az;
		}
		legacy_f64 x, z;
		genghost_region_crossing(&region, ax, az, bx, bz, &x, &z);
		assert(x >= region.min_x - 1e-8 && x <= region.max_x + 1e-8);
		assert(z >= region.min_z - 1e-8 && z <= region.max_z + 1e-8);
		legacy_f64 expected = region_oracle(&region, ax, az, bx, bz);
		legacy_f64 actual = region_cost(x, z, ax, az, bx, bz);
		assert(fabs(actual - expected) <= 2e-6);
		/* A quarter-turn and reflection leave the geometric optimum unchanged.
		 * Check costs rather than coordinates because a straight crossing has
		 * multiple minimizers; reversing A/B also changes the preferred exit. */
		const struct GENGHOST_REGION rotated = {-region.max_z, -region.min_z, region.min_x,
												region.max_x};
		genghost_region_crossing(&rotated, -az, ax, -bz, bx, &x, &z);
		assert(fabs(region_cost(x, z, -az, ax, -bz, bx) - expected) <= 2e-6);
		const struct GENGHOST_REGION reflected = {-region.max_x, -region.min_x, region.min_z,
												  region.max_z};
		genghost_region_crossing(&reflected, -ax, az, -bx, bz, &x, &z);
		assert(fabs(region_cost(x, z, -ax, az, -bx, bz) - expected) <= 2e-6);
		genghost_region_crossing(&region, bx, bz, ax, az, &x, &z);
		assert(fabs(region_cost(x, z, ax, az, bx, bz) - expected) <= 2e-6);
	}
	puts("Tile-region shortest paths agree with 1000 independent numerical cases.");
}

/* Use a real winning prefix, then isolate its nearest native stunt geometry.
 * This keeps neighboring stunts from masking which proposal the rule gates.
 * No vehicle state, car dimensions, track placement, or physics is fabricated. */
static void controller_view(const legacy_u8 *inputs, legacy_uint count, legacy_uint prefix,
							legacy_int model, legacy_int include_exit, struct GENGHOST_ROUTES *set,
							struct GENGHOST_ROUTE *view)
{
	assert(prefix < count);
	for (legacy_uint frame = 0; frame < prefix; ++frame) {
		assert(genghost_sim_step(inputs[frame]) == 0);
	}
	legacy_char error[256];
	assert(genghost_routes_build(set, error, sizeof(error)) == 0);
	const struct GENGHOST_ROUTE *selected = NULL;
	size_t selected_index = 0U;
	legacy_f64 nearest = DBL_MAX;
	for (size_t r = 0U; r < set->count; ++r) {
		const struct GENGHOST_ROUTE *route = &set->routes[r];
		for (size_t i = 0U; i < route->point_count; ++i) {
			const struct GENGHOST_WAYPOINT *point = &route->points[i];
			if (point->physical_model != model) {
				continue;
			}
			legacy_f64 x = point->x - state.playerstate.car_position.lx / 64.0;
			legacy_f64 z = point->z - state.playerstate.car_position.lz / 64.0;
			legacy_f64 distance = x * x + z * z;
			if (distance < nearest) {
				nearest = distance;
				selected = route;
				selected_index = i;
			}
		}
	}
	assert(selected != NULL && nearest < 1024.0 * 1024.0);
	legacy_int piece = selected->points[selected_index].piece;
	size_t first = selected_index, end = selected_index + 1U;
	while (first && selected->points[first - 1U].piece == piece) {
		--first;
	}
	while (end < selected->point_count && selected->points[end].piece == piece) {
		++end;
	}
	size_t first_piece = selected->points[first].piece_index;
	if (include_exit && end < selected->point_count) {
		legacy_int exit_piece = selected->points[end].piece;
		while (end < selected->point_count && selected->points[end].piece == exit_piece) {
			++end;
		}
	}
	memset(view, 0, sizeof(*view));
	view->pieces = &selected->pieces[selected->points[selected_index].piece_index];
	view->piece_count = selected->points[end - 1U].piece_index - first_piece + 1U;
	view->point_count = end - first;
	assert(view->point_count >= 2U);
	view->points = calloc(view->point_count, sizeof(*view->points));
	assert(view->points != NULL);
	for (size_t i = 0U; i < view->point_count; ++i) {
		view->points[i] = selected->points[first + i];
		view->points[i].distance -= selected->points[first].distance;
		view->points[i].piece_index -= first_piece;
	}
	view->length = view->points[view->point_count - 1U].distance;
}

static void test_controller_snapshot(const legacy_u8 *inputs, legacy_uint count, legacy_uint prefix,
									 legacy_int model)
{
	struct GENGHOST_ROUTES set;
	struct GENGHOST_ROUTE view;
	controller_view(inputs, count, prefix, model, 0, &set, &view);
	struct GENGHOST_SNAPSHOT saved, after;
	genghost_sim_capture(&saved);
	struct GENGHOST_POLICY policy;
	genghost_policy_default(&policy);
	for (size_t i = 0U; i < GENGHOST_POLICY_SEGMENTS; ++i) {
		policy.segment[i].stunt_mode = 1U;
	}
	for (legacy_int collision = 0; collision <= 1; ++collision) {
		struct GENGHOST_DRIVER driver;
		genghost_driver_reset(&driver);
		(void)genghost_driver_step(&view, &policy, &driver, 1, collision, 0);
		legacy_int allowed = collision || model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A ||
							 model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B;
		assert(driver.tactic_ticks == (legacy_uint)allowed);
		genghost_sim_capture(&after);
		assert(memcmp(&saved, &after, sizeof(saved)) == 0);
	}
	/* The horizontal red-wall bypass is geometric and must remain a proposal
	 * under either collision setting. It must not target other stunt models,
	 * depend on power gear, or change any native checkpoint state. */
	for (size_t i = 0U; i < GENGHOST_POLICY_SEGMENTS; ++i) {
		policy.segment[i].stunt_mode = GENGHOST_STUNT_CORKSCREW_CUT;
	}
	for (legacy_int powergear = 0; powergear <= 1; ++powergear) {
		struct GENGHOST_DRIVER collision_reference;
		genghost_driver_reset(&collision_reference);
		legacy_u8 reference_input =
			genghost_driver_step(&view, &policy, &collision_reference, powergear, 0, 0);
		assert(collision_reference.tactic_ticks ==
			   (legacy_uint)(model == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT));
		struct GENGHOST_DRIVER legacy_driver;
		genghost_driver_reset(&legacy_driver);
		assert(genghost_driver_step(&view, &policy, &legacy_driver, powergear, 1, 0) ==
			   reference_input);
		assert(memcmp(&legacy_driver, &collision_reference, sizeof(legacy_driver)) == 0);
		genghost_sim_capture(&after);
		assert(memcmp(&saved, &after, sizeof(saved)) == 0);
	}
	/* OWOOT suppresses shortcut genes. A narrow car's central slalom passage
	 * is a road maneuver; all other choices remain identical to road following. */
	struct GENGHOST_DRIVER reference;
	genghost_driver_reset(&reference);
	genghost_policy_default(&policy);
	legacy_u8 expected = genghost_driver_step(&view, &policy, &reference, 1, 1, 1);
	for (legacy_uint mode = 1U; mode <= GENGHOST_STUNT_HUMAN_ALT; ++mode) {
		for (size_t i = 0U; i < GENGHOST_POLICY_SEGMENTS; ++i) {
			policy.segment[i].stunt_mode = mode;
		}
		for (legacy_int collision = 0; collision <= 1; ++collision) {
			struct GENGHOST_DRIVER driver;
			genghost_driver_reset(&driver);
			legacy_u8 actual = genghost_driver_step(&view, &policy, &driver, 1, collision, 1);
			if (model == PHYSICAL_MODEL_SLALOM && mode == GENGHOST_STUNT_HUMAN &&
				genghost_slalom_center_fits()) {
				assert(driver.tactic_ticks <= 1U);
				assert(!(actual & ~(INPUT_DRIVING_MASK | INPUT_ACTION_BUTTON_MASK)));
			} else {
				assert(actual == expected);
				assert(memcmp(&driver, &reference, sizeof(driver)) == 0);
			}
			genghost_sim_capture(&after);
			assert(memcmp(&saved, &after, sizeof(saved)) == 0);
		}
	}
	if (model == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT) {
		free(view.points);
		genghost_routes_free(&set);
		printf("Winning prefix %u: horizontal bypass respects collision settings and OWOOT "
			   "without changing native state.\n",
			   prefix);
		return;
	}
	/* Test the gate against native collisions with the same winning input
	 * suffix and the original car body. Both orientations of the loop and
	 * slalom passage crash with collision fixes; the UD core stays clear.
	 * Only this diagnostic comparison changes a rule at a checkpoint. */
	const legacy_uint suffix_length = 32U;
	assert(prefix + suffix_length < count);
	struct GAMESTATE reference_states[32];
	for (legacy_uint frame = 0U; frame < suffix_length; ++frame) {
		assert(genghost_sim_step(inputs[prefix + frame]) == 0);
		reference_states[frame] = state;
	}
	genghost_sim_restore(&saved);
	legacy_s8 *fixed[] = {(legacy_s8 *)"controller-check", (legacy_s8 *)"--lc:off"};
	configure_legacy_collision(2, fixed);
	legacy_int fixed_crash = 0;
	legacy_int clear_core =
		model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A || model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B;
	for (legacy_uint frame = 0U; frame < suffix_length; ++frame) {
		legacy_int event = genghost_sim_step(inputs[prefix + frame]);
		fixed_crash |= event == 1;
		if (clear_core) {
			assert(event == 0);
			assert(memcmp(&state, &reference_states[frame], sizeof(state)) == 0);
		}
	}
	assert(fixed_crash == !clear_core);
	legacy_s8 *legacy[] = {(legacy_s8 *)"controller-check", (legacy_s8 *)"--lc:on"};
	configure_legacy_collision(2, legacy);
	genghost_sim_restore(&saved);
	free(view.points);
	genghost_routes_free(&set);
	printf("Winning prefix %u: native stunt %d controller gates preserve the complete physics "
		   "snapshot.\n",
		   prefix, model);
}

/* Check real proposal endpoints in object coordinates, independently of the
 * planner's recipe selection. Fixed-collision passages must fit the actual
 * car inside a clear corridor or wholly beyond its solid side wall. */
static void catalog_clearance(const struct GENGHOST_ROUTE *view,
							  const struct GENGHOST_WAYPOINT *target, legacy_int model)
{
	if (target->piece != view->points[0].piece) {
		return; /* A launched car can target the following native route piece. */
	}
	legacy_int piece = view->points[0].piece;
	const struct TRACKOBJECT *object = &trkObjectList[(legacy_u8)track_route_element_ids[piece]];
	legacy_f64 x = target->x - track_object_base_x(object, (legacy_u8)track_route_columns[piece]);
	legacy_f64 z = target->z - track_object_base_z(object, (legacy_u8)track_route_rows[piece]);
	legacy_f64 local_x = object->ss_rotY == 0	  ? x
						 : object->ss_rotY == 256 ? -z
						 : object->ss_rotY == 512 ? -x
												  : z;
	legacy_f64 half_width = fabs((legacy_f64)simd_player.collide_points[0].px);
	for (legacy_uint wheel = 0U; wheel < 4U; ++wheel) {
		legacy_f64 width = fabs(simd_player.wheel_coords[wheel].x / 64.0);
		if (width > half_width) {
			half_width = width;
		}
	}
	legacy_f64 lateral = fabs(local_x);
	if (model == PHYSICAL_MODEL_SLALOM) {
		/* Native box point 0 stores width; point 1 stores length and radius.
		 * A narrow car can fit the central opening with collision fixes too. */
		assert(lateral + half_width < SLALOM_POLE_INNER_X ||
			   lateral >= SLALOM_POLE_OUTER_X + half_width);
	} else if (model == PHYSICAL_MODEL_TUNNEL) {
		assert(lateral + half_width <= 120.0 || lateral >= 270.0 + half_width);
	} else if (model == PHYSICAL_MODEL_PIPE_ENTRANCE) {
		assert(lateral + half_width <= 115.0 || lateral >= 164.0 + half_width);
	} else if (model == PHYSICAL_MODEL_PIPE) {
		assert(lateral + half_width <= 164.0 || lateral >= 164.0 + half_width);
	}
}

static void test_quarter_turn(legacy_f64 x, legacy_f64 z, legacy_int angle, legacy_f64 *rx,
							  legacy_f64 *rz)
{
	switch ((angle & 1023) / 256) {
		case 1:
			*rx = z;
			*rz = -x;
			break;
		case 2:
			*rx = -x;
			*rz = -z;
			break;
		case 3:
			*rx = -z;
			*rz = x;
			break;
		default:
			*rx = x;
			*rz = z;
			break;
	}
}

/* Find an aligned native heading independently of the planner's local-angle
 * convention: its forward direction must become positive car-local z. */
static legacy_s16 slalom_heading(legacy_int rotation, legacy_f64 direction)
{
	legacy_f64 wx, wz;
	test_quarter_turn(0.0, direction * 512.0, rotation, &wx, &wz);
	struct VECTOR forward = {(legacy_s16)wx, 0, (legacy_s16)wz}, local;
	for (legacy_s16 heading = 0; heading < 1024; heading += 256) {
		struct MATRIX *matrix = mat_rot_zxy(0, 0, heading, MATRIX_ROTATION_ORDER_YXZ);
		mat_mul_vector(&forward, matrix, &local);
		if (local.z > 0 && local.x == 0) {
			return heading;
		}
	}
	assert(!"Cardinal slalom direction has no native heading");
	return 0;
}

static void slalom_position(legacy_f64 ox, legacy_f64 oz, legacy_int rotation, legacy_f64 x,
							legacy_f64 z, legacy_s16 heading)
{
	legacy_f64 wx, wz;
	test_quarter_turn(x, z, rotation, &wx, &wz);
	state.playerstate.car_position.lx = (legacy_s32)((ox + wx) * 64.0);
	state.playerstate.car_position.lz = (legacy_s32)((oz + wz) * 64.0);
	state.playerstate.car_position.ly = 0;
	state.playerstate.car_rotate = (struct VECTOR){heading, 0, 0};
	state.playerstate.car_actual_speed = 160U * 256U;
	state.playerstate.car_sumSurfAllWheels = 4;
	state.playerstate.car_sumSurfFrontWheels = 2;
	state.playerstate.car_sumSurfRearWheels = 2;
}

/* These are isolated geometry contracts, not synthetic completed races. Keep
 * the real car dimensions and native slalom endpoints, rotate/reverse the whole
 * setup, and ask for a clear high-speed corridor through both stone footprints.
 * In particular the rear is still beside stone 2 after the spline has ended. */
static void test_slalom_center_guard(const struct GENGHOST_ROUTE *view, legacy_int model)
{
	if (model != PHYSICAL_MODEL_SLALOM) {
		return;
	}
	legacy_int piece = view->points[0].piece;
	struct TRACKOBJECT *object = &trkObjectList[(legacy_u8)track_route_element_ids[piece]];
	struct TRACKOBJECT original_object = *object;
	struct SIMD original_simd = simd_player;
	struct GENGHOST_SNAPSHOT saved, positioned, after;
	genghost_sim_capture(&saved);
	legacy_f64 ox = track_object_base_x(object, (legacy_u8)track_route_columns[piece]);
	legacy_f64 oz = track_object_base_z(object, (legacy_u8)track_route_rows[piece]);
	size_t last = 0U;
	while (last + 1U < view->point_count && view->points[last + 1U].piece == piece) {
		++last;
	}
	legacy_f64 ax, az, bx, bz;
	test_quarter_turn(view->points[0].x - ox, view->points[0].z - oz, -object->ss_rotY, &ax, &az);
	test_quarter_turn(view->points[last].x - ox, view->points[last].z - oz, -object->ss_rotY, &bx,
					  &bz);
	legacy_f64 half_width = fabs((legacy_f64)simd_player.collide_points[0].px);
	legacy_f64 half_length = fabs((legacy_f64)simd_player.collide_points[1].px);
	for (legacy_uint wheel = 0U; wheel < 4U; ++wheel) {
		legacy_f64 width = fabs(simd_player.wheel_coords[wheel].x / 64.0);
		legacy_f64 length = fabs(simd_player.wheel_coords[wheel].z / 64.0);
		if (width > half_width) {
			half_width = width;
		}
		if (length > half_length) {
			half_length = length;
		}
	}
	for (legacy_int rotation = 0; rotation < 1024; rotation += 256) {
		object->ss_rotY = (legacy_s16)rotation;
		for (legacy_int reverse = 0; reverse <= 1; ++reverse) {
			struct GENGHOST_WAYPOINT points[2] = {view->points[0], view->points[last]};
			test_quarter_turn(reverse ? bx : ax, reverse ? bz : az, rotation, &points[0].x,
							  &points[0].z);
			test_quarter_turn(reverse ? ax : bx, reverse ? az : bz, rotation, &points[1].x,
							  &points[1].z);
			for (legacy_uint i = 0U; i < 2U; ++i) {
				points[i].x += ox;
				points[i].z += oz;
				points[i].distance = i * 1024.0;
			}
			struct GENGHOST_ROUTE route = *view;
			route.points = points;
			route.point_count = 2U;
			route.length = 1024.0;
			legacy_f64 direction = (reverse ? az - bz : bz - az) > 0.0 ? 1.0 : -1.0;
			legacy_s16 heading = slalom_heading(rotation, direction);
			struct GENGHOST_POLICY policy;
			struct GENGHOST_DRIVER driver;
			genghost_policy_default(&policy);
			genghost_driver_reset(&driver);
			policy.segment[0].stunt_mode = GENGHOST_STUNT_HUMAN;
			for (legacy_int collision = 0; collision <= 1; ++collision) {
				assert(genghost_stunt_recipe(model, collision)->kind ==
					   GENGHOST_RECIPE_SLALOM_CENTER);
				struct GENGHOST_WAYPOINT target;
				legacy_f64 protected_end, local_x, local_z;
				if (half_width < SLALOM_POLE_INNER_X - 1.0) {
					const legacy_f64 positions[] = {-500.0, 0.0,
													SLALOM_POLE_FAR_Z + half_length - 2.0};
					for (size_t sample = 0U; sample < sizeof(positions) / sizeof(positions[0]);
						 ++sample) {
						slalom_position(ox, oz, rotation, 0.0, direction * positions[sample],
										heading);
						genghost_sim_capture(&positioned);
						for (legacy_int phase = -1; phase <= 1; ++phase) {
							policy.segment[0].lateral_offset = phase;
							assert(genghost_stunt_target(&route, &driver, &policy.segment[0], 400.0,
														 collision, &target, &protected_end));
							test_quarter_turn(target.x - ox, target.z - oz, -rotation, &local_x,
											  &local_z);
							assert(fabs(local_x) < 1e-9);
							assert(direction * local_z > positions[sample]);
							assert(target.speed > 65.0 && target.speed <= 250.0);
							genghost_sim_capture(&after);
							assert(memcmp(&positioned, &after, sizeof(after)) == 0);
						}
						/* The same truly clear center is legal under OWOOT; this
						 * must not retain the old artificial slalom braking cap. */
						for (size_t segment = 0U; segment < GENGHOST_POLICY_SEGMENTS; ++segment) {
							policy.segment[segment].stunt_mode = GENGHOST_STUNT_HUMAN;
							policy.segment[segment].lateral_offset = 0.0;
						}
						struct GENGHOST_DRIVER road_driver;
						genghost_driver_reset(&road_driver);
						legacy_u8 road_input =
							genghost_driver_step(&route, &policy, &road_driver, 0, collision, 1);
						assert(road_driver.tactic_ticks == 1U);
						assert(!(road_input & INPUT_BRAKE_FLAG));
						genghost_sim_capture(&after);
						assert(memcmp(&positioned, &after, sizeof(after)) == 0);
					}
				}
				policy.segment[0].lateral_offset = 0.0;
				if (half_width < SLALOM_POLE_INNER_X - 1.0) {
					/* Prepare the center while there is still road before stone 1.
					 * The proposed alignment target must leave room for the front. */
					slalom_position(ox, oz, rotation, 80.0, -direction * 600.0, heading);
					genghost_sim_capture(&positioned);
					assert(genghost_stunt_target(&route, &driver, &policy.segment[0], 400.0,
												 collision, &target, &protected_end));
					test_quarter_turn(target.x - ox, target.z - oz, -rotation, &local_x, &local_z);
					assert(fabs(local_x) < 1e-9);
					assert(direction * local_z + half_length < -SLALOM_POLE_FAR_Z);
					genghost_sim_capture(&after);
					assert(memcmp(&positioned, &after, sizeof(after)) == 0);
				}
				/* A late transverse approach inside the stunt has no aligned
				 * clearance yet. Reject the fast-center proposal or retain an
				 * outside continuation; native search may explore recovery. */
				for (legacy_int side = -1; side <= 1; side += 2) {
					slalom_position(ox, oz, rotation, side * 80.0, 0.0, heading);
					genghost_sim_capture(&positioned);
					if (genghost_stunt_target(&route, &driver, &policy.segment[0], 400.0, collision,
											  &target, &protected_end)) {
						test_quarter_turn(target.x - ox, target.z - oz, -rotation, &local_x,
										  &local_z);
						assert(side * local_x >= SLALOM_POLE_OUTER_X + half_width);
					}
					genghost_sim_capture(&after);
					assert(memcmp(&positioned, &after, sizeof(after)) == 0);
				}
				if (!collision && half_length >= SLALOM_POLE_INNER_X) {
					/* A narrow width alone cannot admit a car that is sideways
					 * between the barriers under corrected solid collisions. */
					slalom_position(ox, oz, rotation, 0.0, 0.0,
									(legacy_s16)((heading + 256) & 1023));
					genghost_sim_capture(&positioned);
					assert(!genghost_stunt_target(&route, &driver, &policy.segment[0], 400.0,
												  collision, &target, &protected_end));
					genghost_sim_capture(&after);
					assert(memcmp(&positioned, &after, sizeof(after)) == 0);
				}
				/* Past both the whole car and an exit margin, never turn back
				 * toward the slalom's shorter/wavy opponent spline. */
				slalom_position(ox, oz, rotation, 0.0,
								direction * (SLALOM_POLE_FAR_Z + half_length + 200.0), heading);
				assert(!genghost_stunt_target(&route, &driver, &policy.segment[0], 400.0, collision,
											  &target, &protected_end));
			}
			/* Car width is resource data, not a codename rule. A wider body
			 * cannot use the central gap under corrected solid collisions. */
			simd_player.collide_points[0].px = SLALOM_POLE_INNER_X + 8;
			slalom_position(ox, oz, rotation, 0.0, -direction * 750.0, heading);
			genghost_sim_capture(&positioned);
			struct GENGHOST_WAYPOINT target;
			legacy_f64 protected_end;
			if (genghost_stunt_target(&route, &driver, &policy.segment[0], 400.0, 0, &target,
									  &protected_end)) {
				legacy_f64 local_x, local_z;
				test_quarter_turn(target.x - ox, target.z - oz, -rotation, &local_x, &local_z);
				assert(fabs(local_x) >= SLALOM_POLE_OUTER_X + simd_player.collide_points[0].px);
			}
			genghost_sim_capture(&after);
			assert(memcmp(&positioned, &after, sizeof(after)) == 0);
			assert(!genghost_slalom_center_fits());
			struct GENGHOST_POLICY road_policy;
			struct GENGHOST_DRIVER road_driver, center_driver;
			genghost_policy_default(&road_policy);
			genghost_driver_reset(&road_driver);
			genghost_driver_reset(&center_driver);
			legacy_u8 road_input =
				genghost_driver_step(&route, &road_policy, &road_driver, 0, 0, 1);
			for (size_t segment = 0U; segment < GENGHOST_POLICY_SEGMENTS; ++segment) {
				road_policy.segment[segment].stunt_mode = GENGHOST_STUNT_HUMAN;
			}
			assert(genghost_driver_step(&route, &road_policy, &center_driver, 0, 0, 1) ==
				   road_input);
			assert(memcmp(&road_driver, &center_driver, sizeof(road_driver)) == 0);
			genghost_sim_capture(&after);
			assert(memcmp(&positioned, &after, sizeof(after)) == 0);
			simd_player = original_simd;
		}
	}
	*object = original_object;
	genghost_sim_restore(&saved);
	genghost_sim_capture(&after);
	assert(memcmp(&saved, &after, sizeof(saved)) == 0);
	puts("Slalom center corridors, late-entry guards, wide bodies and rear clearance passed in all "
		 "orientations.");
}

/* Synthetic approach positions exercise a geometry guard, never a simulated
 * race result. Use the native object's own endpoints and real car dimensions,
 * rotate the entire local configuration, and reverse the route. A shortcut
 * must not turn through a tunnel/pipe side wall in either direction, or
 * steer a ground-level car onto a solid deck above it. */
static void test_corridor_guard(const struct GENGHOST_ROUTE *view, legacy_int model)
{
	if (model != PHYSICAL_MODEL_TUNNEL && model != PHYSICAL_MODEL_PIPE_ENTRANCE &&
		model != PHYSICAL_MODEL_PIPE && model != PHYSICAL_MODEL_SOLID_ROAD) {
		return;
	}
	legacy_int piece = view->points[0].piece;
	struct TRACKOBJECT *object = &trkObjectList[(legacy_u8)track_route_element_ids[piece]];
	struct TRACKOBJECT original_object = *object;
	struct GENGHOST_SNAPSHOT saved, positioned, after;
	genghost_sim_capture(&saved);
	legacy_f64 ox = track_object_base_x(object, (legacy_u8)track_route_columns[piece]);
	legacy_f64 oz = track_object_base_z(object, (legacy_u8)track_route_rows[piece]);
	size_t last = 0U;
	while (last + 1U < view->point_count && view->points[last + 1U].piece == piece) {
		++last;
	}
	legacy_f64 ax, az, bx, bz;
	test_quarter_turn(view->points[0].x - ox, view->points[0].z - oz, -object->ss_rotY, &ax, &az);
	test_quarter_turn(view->points[last].x - ox, view->points[last].z - oz, -object->ss_rotY, &bx,
					  &bz);
	for (legacy_int rotation = 0; rotation < 1024; rotation += 256) {
		object->ss_rotY = (legacy_s16)rotation;
		for (legacy_int reverse = 0; reverse <= 1; ++reverse) {
			struct GENGHOST_WAYPOINT points[2] = {view->points[0], view->points[last]};
			test_quarter_turn(reverse ? bx : ax, reverse ? bz : az, rotation, &points[0].x,
							  &points[0].z);
			test_quarter_turn(reverse ? ax : bx, reverse ? az : bz, rotation, &points[1].x,
							  &points[1].z);
			for (legacy_uint i = 0U; i < 2U; ++i) {
				points[i].x += ox;
				points[i].z += oz;
				points[i].distance = i * 1024.0;
			}
			struct GENGHOST_ROUTE route = *view;
			route.points = points;
			route.point_count = 2U;
			route.length = 1024.0;
			for (legacy_int side = -1; side <= 1; side += 2) {
				legacy_f64 wx, wz;
				test_quarter_turn(side * 400.0, 0.0, rotation, &wx, &wz);
				state.playerstate.car_position.lx = (legacy_s32)((ox + wx) * 64.0);
				state.playerstate.car_position.lz = (legacy_s32)((oz + wz) * 64.0);
				if (model == PHYSICAL_MODEL_SOLID_ROAD) {
					state.playerstate.car_position.ly =
						(legacy_s32)((view->points[0].y - 450.0) * 64.0);
				}
				genghost_sim_capture(&positioned);
				struct GENGHOST_POLICY policy;
				struct GENGHOST_DRIVER driver;
				genghost_policy_default(&policy);
				genghost_driver_reset(&driver);
				policy.segment[0].stunt_mode = GENGHOST_STUNT_HUMAN;
				struct GENGHOST_WAYPOINT target;
				legacy_f64 protected_end;
				legacy_int planned = genghost_stunt_target(&route, &driver, &policy.segment[0],
														   400.0, 0, &target, &protected_end);
				assert(planned == (model == PHYSICAL_MODEL_SOLID_ROAD));
				if (planned) {
					legacy_f64 local_x, local_z;
					test_quarter_turn(target.x - ox, target.z - oz, -rotation, &local_x, &local_z);
					assert(side * local_x > 120.0 && !target.height_specified);
				}
				policy.segment[0].stunt_mode = GENGHOST_STUNT_HUMAN_ALT;
				assert(genghost_stunt_target(&route, &driver, &policy.segment[0], 400.0, 0, &target,
											 &protected_end));
				legacy_f64 local_x, local_z;
				test_quarter_turn(target.x - ox, target.z - oz, -rotation, &local_x, &local_z);
				assert(side * local_x > (model == PHYSICAL_MODEL_TUNNEL		  ? 270.0
										 : model == PHYSICAL_MODEL_SOLID_ROAD ? 120.0
																			  : 164.0));
				genghost_sim_capture(&after);
				assert(memcmp(&positioned, &after, sizeof(after)) == 0);
			}
		}
	}
	*object = original_object;
	genghost_sim_restore(&saved);
	genghost_sim_capture(&after);
	assert(memcmp(&saved, &after, sizeof(saved)) == 0);
}

static void test_stunt_catalog(const legacy_u8 *inputs, legacy_uint count, legacy_uint prefix,
							   legacy_int model)
{
	struct GENGHOST_ROUTES set;
	struct GENGHOST_ROUTE view;
	controller_view(inputs, count, prefix, model, 1, &set, &view);
	struct GENGHOST_SNAPSHOT saved, after;
	genghost_sim_capture(&saved);
	test_corridor_guard(&view, model);
	test_slalom_center_guard(&view, model);
	assert(genghost_stunt_catalog_model(model));
	legacy_uint proposals[2] = {0};
	const legacy_f64 lookaheads[] = {160.0, 400.0, 900.0};
	for (legacy_int collision = 0; collision <= 1; ++collision) {
		assert(genghost_stunt_recipe(model, collision) != NULL);
		for (legacy_uint mode = GENGHOST_STUNT_HUMAN; mode <= GENGHOST_STUNT_HUMAN_ALT; ++mode) {
			for (legacy_int offset = -2; offset <= 2; offset += 2) {
				struct GENGHOST_POLICY policy;
				genghost_policy_default(&policy);
				for (size_t i = 0U; i < GENGHOST_POLICY_SEGMENTS; ++i) {
					policy.segment[i].stunt_mode = mode;
					policy.segment[i].lateral_offset = offset;
				}
				struct GENGHOST_DRIVER driver;
				genghost_driver_reset(&driver);
				for (size_t look = 0U; look < sizeof(lookaheads) / sizeof(lookaheads[0]); ++look) {
					struct GENGHOST_WAYPOINT target;
					legacy_f64 protected_end = 0.0;
					legacy_int planned =
						genghost_stunt_target(&view, &driver, &policy.segment[0], lookaheads[look],
											  collision, &target, &protected_end);
					assert(planned == 0 || planned == 1);
					if (planned) {
						++proposals[collision];
						assert(target.x > -4096.0 && target.x < 34816.0);
						assert(target.z > -4096.0 && target.z < 34816.0);
						assert(target.speed > 0.0 && target.speed <= 250.0);
						assert(protected_end >= 0.0 && protected_end <= view.length + 0.001);
						if (!collision) {
							catalog_clearance(&view, &target, model);
						}
					}
					genghost_sim_capture(&after);
					assert(memcmp(&saved, &after, sizeof(saved)) == 0);
				}
				legacy_u8 input = genghost_driver_step(&view, &policy, &driver, 1, collision, 0);
				assert(!(input & ~(INPUT_DRIVING_MASK | INPUT_ACTION_BUTTON_MASK)));
				assert((input & INPUT_PEDAL_MASK) != INPUT_PEDAL_MASK);
				assert((input & INPUT_STEERING_MASK) != INPUT_STEERING_MASK);
				genghost_sim_capture(&after);
				assert(memcmp(&saved, &after, sizeof(saved)) == 0);
			}
		}
	}
	assert(proposals[0] && proposals[1]);
	/* OWOOT suppresses shortcut modes under either collision regime. Only
	 * a physically narrow slalom center remains available as legal road driving. */
	for (legacy_int powergear = 0; powergear <= 1; ++powergear) {
		struct GENGHOST_POLICY policy;
		struct GENGHOST_DRIVER reference;
		genghost_policy_default(&policy);
		genghost_driver_reset(&reference);
		legacy_u8 expected = genghost_driver_step(&view, &policy, &reference, powergear, 0, 1);
		for (legacy_uint mode = 1U; mode <= GENGHOST_STUNT_HUMAN_ALT; ++mode) {
			for (size_t i = 0U; i < GENGHOST_POLICY_SEGMENTS; ++i) {
				policy.segment[i].stunt_mode = mode;
			}
			for (legacy_int collision = 0; collision <= 1; ++collision) {
				struct GENGHOST_DRIVER driver;
				genghost_driver_reset(&driver);
				legacy_u8 actual =
					genghost_driver_step(&view, &policy, &driver, powergear, collision, 1);
				if (model == PHYSICAL_MODEL_SLALOM && mode == GENGHOST_STUNT_HUMAN &&
					genghost_slalom_center_fits()) {
					assert(driver.tactic_ticks <= 1U);
					assert(!(actual & ~(INPUT_DRIVING_MASK | INPUT_ACTION_BUTTON_MASK)));
				} else {
					assert(actual == expected);
					assert(memcmp(&driver, &reference, sizeof(driver)) == 0);
				}
				genghost_sim_capture(&after);
				assert(memcmp(&saved, &after, sizeof(saved)) == 0);
			}
		}
	}
	free(view.points);
	genghost_routes_free(&set);
	printf("Winning prefix %u model %d: both stunt recipes, fixed clearance, OWOOT and read-only "
		   "native state passed.\n",
		   prefix, model);
}

/* A selector filter such as word%4 accidentally removes one residue class
 * when the number of source/target pairs is divisible by four. Separate recipe
 * choice from the full selector and verify the resulting Cartesian indices. */
static void test_dualway_selectors(void)
{
	const legacy_uint counts[] = {4U, 8U, 12U};
	for (size_t test = 0U; test < sizeof(counts) / sizeof(counts[0]); ++test) {
		legacy_uint total = counts[test];
		for (legacy_u32 gate = 4U; gate <= 12U; gate += 4U) {
			legacy_uint visits[12] = {0};
			for (legacy_u64 word = 3U; word < 3U + 32U * total; ++word) {
				legacy_u64 selected = genghost_dualway_selector(gate, word);
				assert(selected >= 3U);
				++visits[(selected - 3U) % total];
			}
			for (legacy_uint ordinal = 0U; ordinal < total; ++ordinal) {
				assert(visits[ordinal] == 32U);
			}
		}
	}
	legacy_uint recipes[3] = {0};
	for (legacy_u64 word = 0U; word < 12U; ++word) {
		legacy_u64 selected = genghost_dualway_selector(0U, word);
		assert(selected < 3U);
		++recipes[selected];
	}
	assert(recipes[0] == 4U && recipes[1] == 4U && recipes[2] == 4U);
	assert(genghost_dualway_selector(4U, UINT64_MAX) == UINT64_MAX);
	puts("Dual-way mutation reaches every pair residue and all three special recipes.");
}

/* DEFAULT has two long paths with a shared split tile (10/59) and merge
 * footprint (57/127). These are useful counterexamples to treating distinct
 * graph IDs as proof that a private branch tile was visited. */
static void place_on_piece(legacy_int piece)
{
	state.playerstate.car_position.lx =
		((legacy_s32)(legacy_u8)track_route_columns[piece] * 1024 + 512) * 64;
	state.playerstate.car_position.lz =
		((legacy_s32)(29 - (legacy_u8)track_route_rows[piece]) * 1024 + 512) * 64;
}

static legacy_int test_dualway_routes(void)
{
	if (track_pieces_counter != 128 || track_primary_route_links[9] != 10 ||
		track_alternate_route_links[9] != 59 || track_primary_route_links[127] != 58 ||
		track_route_columns[0] != 7 || track_route_rows[0] != 24) {
		return 0;
	}
	struct GENGHOST_ROUTES set;
	legacy_char error[256];
	assert(genghost_routes_build(&set, error, sizeof(error)) == 0);
	assert(set.count == 2U && set.dualway_pairs == 6164U);
	/* Endpoint recipes depend on private branch ranks, not DEFAULT node IDs.
	 * Assert their native realization here, including the winner's third-private
	 * departure to penultimate arrival, and retain old shortest/near profiles. */
	static const legacy_int expected_departures[2][6] = {{11, 35, 11, 12, 13, 13},
														 {60, 76, 60, 61, 62, 62}};
	static const legacy_int expected_arrivals[2][6] = {{126, 118, 124, 125, 125, 126},
													   {56, 16, 54, 55, 55, 56}};
	for (size_t route_index = 0; route_index < set.count; ++route_index) {
		for (legacy_uint seed = 0U; seed < 6U; ++seed) {
			struct GENGHOST_POLICY policy;
			memset(&policy, 0, sizeof(policy));
			genghost_policy_default(&policy);
			for (size_t i = 0; i < GENGHOST_POLICY_SEGMENTS; ++i) {
				policy.segment[i].dualway_choice = 1U + seed % 2U;
			}
			struct GENGHOST_POLICY before;
			memcpy(&before, &policy, sizeof(policy));
			genghost_dualway_seed_policy(&set, route_index, &policy, seed);
			if (seed < 2U) {
				assert(memcmp(&policy, &before, sizeof(policy)) == 0);
			}
			struct GENGHOST_ROUTE candidate;
			assert(genghost_route_policy(&set, route_index, &policy, &candidate, error,
										 sizeof(error)) == 1);
			size_t crossing = 0U;
			while (crossing + 1U < candidate.piece_count &&
				   candidate.portals[crossing + 1U] != 2U) {
				++crossing;
			}
			assert(crossing + 1U < candidate.piece_count);
			assert(candidate.pieces[crossing] == expected_departures[route_index][seed]);
			assert(candidate.pieces[crossing + 1U] == expected_arrivals[route_index][seed]);
			genghost_route_release(&candidate);
		}
	}

	for (size_t route_index = 0U; route_index < 2U; ++route_index) {
		/* The repeated finish is native piece zero again, but its distance
		 * belongs at the end of the lap. Looking up by native ID returns the
		 * start distance and corrupts switch costs when routes merge there. */
		const struct GENGHOST_ROUTE *base = &set.routes[route_index];
		size_t finish = base->piece_count - 1U, first_finish = base->point_count - 1U;
		assert(base->pieces[0] == 0 && base->pieces[finish] == 0);
		assert(base->points[first_finish].piece_index == finish);
		while (first_finish && base->points[first_finish - 1U].piece_index == finish) {
			--first_finish;
		}
		assert(genghost_route_piece_distance(base, 0U) == 0.0);
		legacy_f64 finish_distance = genghost_route_piece_distance(base, finish);
		assert(finish_distance > base->length * 0.9);
		assert(finish_distance == base->points[first_finish].distance);
		for (legacy_u64 ordinal = 0U; ordinal < 3082U; ++ordinal) {
			struct GENGHOST_POLICY policy;
			genghost_policy_default(&policy);
			for (size_t i = 0; i < GENGHOST_POLICY_SEGMENTS; ++i) {
				policy.segment[i].dualway_choice = ordinal + 3U;
			}
			struct GENGHOST_ROUTE route;
			assert(genghost_route_policy(&set, route_index, &policy, &route, error,
										 sizeof(error)) == 1);
			assert(route.switch_count == 1U);
			legacy_int source =
				route_index ? 60 + (legacy_int)(ordinal / 46U) : 11 + (legacy_int)(ordinal / 67U);
			legacy_int target =
				route_index ? 11 + (legacy_int)(ordinal % 46U) : 60 + (legacy_int)(ordinal % 67U);
			size_t portal = 0U;
			while (portal < route.piece_count && route.pieces[portal] != source) {
				++portal;
			}
			assert(portal + 1U < route.piece_count && route.pieces[portal + 1U] == target);
			assert(route.portals[portal] == 1U && route.portals[portal + 1U] == 2U);
			struct GENGHOST_DRIVER driver;
			genghost_driver_reset(&driver);
			place_on_piece(target);
			genghost_driver_observe(&route, &driver);
			assert(driver.next_portal == portal);
			place_on_piece(source);
			genghost_driver_observe(&route, &driver);
			assert(driver.next_portal == portal + 1U);
			place_on_piece(target);
			genghost_driver_observe(&route, &driver);
			assert(driver.next_portal > portal + 1U);
			genghost_route_release(&route);
		}
	}
	/* Read the actual native penalty search, not an optimizer approximation.
	 * Before choosing a branch, the destination costs 65 skipped elements.
	 * Once private A is confirmed, the legacy mode wraps past the finish;
	 * fixed mode keeps A confirmed and recognizes the other branch as off-route.
	 * Configuration must survive simulator resets and branch restoration. */
	for (legacy_int switching = 0; switching <= 1; ++switching) {
		legacy_s8 *flags[] = {(legacy_s8 *)"test",
							  (legacy_s8 *)(switching ? "--dws:on" : "--dws:off")};
		configure_dual_way_switching(2, flags);
		for (legacy_int confirmed = 9; confirmed <= 11; confirmed += 2) {
			genghost_sim_reset();
			state.game_startcol = state.game_startcol2 = -1;
			state.game_startrow = state.game_startrow2 = -1;
			place_on_piece(125);
			legacy_s16 current = (legacy_s16)confirmed, skipped = 0;
			assert(detect_penalty(&current, &skipped) != 0);
			if (!switching && confirmed == 11) {
				assert(current == confirmed && skipped == PENALTY_ROUTE_OUTSIDE_TRACK);
			} else {
				assert(current == 125);
				assert(skipped == (confirmed == 9 ? 65 : PENALTY_ROUTE_FINISH_REACHED));
			}
		}
	}
	genghost_routes_free(&set);
	genghost_sim_reset();
	puts("All 6164 dual-way pairs, protected tile touches, and native penalty distinction passed.");
	return 1;
}

/* Replace only the grounded uphill shift decisions in the fastest ZCT079
 * recording. Matching every native state rejects an early shift that loses
 * speed during the clutch transition and misses the power-gear window. */
static void test_uphill_powergear(const legacy_u8 *inputs, legacy_uint count)
{
	assert(count > 300U && count <= GENGHOST_MAX_FRAMES);
	legacy_u8 *reference = malloc((size_t)count * GAMESTATE_SERIALIZED_SIZE);
	assert(reference != NULL);
	for (legacy_uint frame = 0U; frame < count; ++frame) {
		assert(genghost_sim_step(inputs[frame]) >= 0);
		assert(gamestate_serialize(reference + (size_t)frame * GAMESTATE_SERIALIZED_SIZE, &state) ==
			   GAMESTATE_SERIALIZED_SIZE);
	}
	genghost_sim_reset();
	struct GENGHOST_ROUTES routes;
	legacy_char error[256];
	assert(genghost_routes_build(&routes, error, sizeof(error)) == 0);
	assert(routes.count > 0U);
	struct GENGHOST_POLICY policy;
	genghost_policy_default(&policy);
	struct GENGHOST_DRIVER driver;
	genghost_driver_reset(&driver);
	struct GENGHOST_SNAPSHOT before, after;
	legacy_uint shifts = 0U;
	const legacy_uint shift_mask = INPUT_SHIFT_UP_FLAG | INPUT_SHIFT_DOWN_FLAG;
	for (legacy_uint frame = 0U; frame < count; ++frame) {
		genghost_sim_capture(&before);
		struct GENGHOST_DRIVER original_driver = driver;
		legacy_u8 control = genghost_driver_step(&routes.routes[0], &policy, &driver, 1, 1, 0);
		genghost_sim_capture(&after);
		assert(memcmp(&before, &after, sizeof(before)) == 0);
		if (frame == 261U || frame == 266U) {
			assert(state.playerstate.car_sumSurfRearWheels != CAR_WHEEL_CONTACT_NONE);
			assert(state.playerstate.car_pseudoGravity < -12);
			assert((control & shift_mask) == INPUT_SHIFT_UP_FLAG);
			/* Compare controller flags at the same real state, without changing
			 * the engine's rules or stepping a counterfactual simulation. */
			struct GENGHOST_DRIVER fixed_driver = original_driver;
			legacy_u8 fixed =
				genghost_driver_step(&routes.routes[0], &policy, &fixed_driver, 0, 1, 0);
			assert(!(fixed & INPUT_SHIFT_UP_FLAG));
			struct GENGHOST_DRIVER road_driver = original_driver;
			legacy_u8 road =
				genghost_driver_step(&routes.routes[0], &policy, &road_driver, 1, 1, 1);
			assert((road & shift_mask) == INPUT_SHIFT_UP_FLAG);
			genghost_sim_capture(&after);
			assert(memcmp(&before, &after, sizeof(before)) == 0);
		}
		legacy_u8 input = inputs[frame];
		if (frame >= 240U && frame < 300U) {
			if (control & INPUT_SHIFT_UP_FLAG) {
				assert(frame == 261U || frame == 266U);
				++shifts;
			}
			input = (legacy_u8)((input & ~shift_mask) | (control & shift_mask));
		}
		assert(genghost_sim_step(input) >= 0);
		legacy_u8 serialized[GAMESTATE_SERIALIZED_SIZE];
		assert(gamestate_serialize(serialized, &state) == sizeof(serialized));
		assert(memcmp(serialized, reference + (size_t)frame * GAMESTATE_SERIALIZED_SIZE,
					  sizeof(serialized)) == 0);
	}
	struct GENGHOST_RESULT result;
	genghost_sim_result(&result);
	assert(shifts == 2U && result.finished && result.total_frames == 625U);
	assert(result.penalty_frames == 0U);
	free(reference);
	genghost_routes_free(&routes);
	puts("Grounded uphill shifts match all winning ZCT079 states; PG-off and OWOOT gates passed.");
}

/* Resource-backed integration check. Run from a game-data directory:
 * test-genghost-simulation source.rpl normalized.rpl [physics switches]
 * repldump normalized.rpl 1 [the same physics switches]
 * Compare normalized.rpl.states with normalized.BNI: every serialized state
 * must match. Source 10 Hz inputs are deliberately normalized to 20 Hz so this
 * also exercises extraction without importing the source recording's timing. */
legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[])
{
	if (argc == 2 && strcmp((const legacy_char *)argv[1], "--geometry-only") == 0) {
		test_regions();
		test_dualway_selectors();
		test_guide_roll();
		return 0;
	}
	if (argc < 3) {
		fprintf(stderr,
				"Usage: test-genghost-simulation SOURCE.RPL OUTPUT.RPL [physics options]\n");
		return 1;
	}
	legacy_u8 recording[REPLAY_INPUT_OFFSET + GENGHOST_MAX_FRAMES + 1];
	FILE *source = fopen((const legacy_char *)argv[1], "rb");
	assert(source != NULL);
	size_t size = fread(recording, 1, sizeof(recording), source);
	assert(!ferror(source) && fclose(source) == 0);
	assert(size >= REPLAY_INPUT_OFFSET);
	struct GAMEINFO config;
	replay_gameinfo_decode(&config, recording);
	assert(config.game_opponenttype == 0);
	assert(config.game_recordedframes <= GENGHOST_MAX_FRAMES);
	assert(size == REPLAY_INPUT_OFFSET + config.game_recordedframes);
	legacy_uint count = config.game_recordedframes;
	const legacy_u8 *inputs = recording + REPLAY_INPUT_OFFSET;
	assert(genghost_sim_init(recording + REPLAY_GAMEINFO_SIZE,
							 (const legacy_char *)config.game_playercarid,
							 config.game_playertransmission, argc, (legacy_char **)argv) == 0);

	if (argc == 5 && strcmp((const legacy_char *)argv[2], "--controller-check") == 0) {
		test_controller_snapshot(inputs, count,
								 (legacy_uint)strtoul((const legacy_char *)argv[3], NULL, 10),
								 (legacy_int)strtol((const legacy_char *)argv[4], NULL, 10));
		return 0;
	}
	if (argc == 5 && strcmp((const legacy_char *)argv[2], "--catalog-check") == 0) {
		test_stunt_catalog(inputs, count,
						   (legacy_uint)strtoul((const legacy_char *)argv[3], NULL, 10),
						   (legacy_int)strtol((const legacy_char *)argv[4], NULL, 10));
		return 0;
	}
	if (argc == 3 && strcmp((const legacy_char *)argv[2], "--uphill-check") == 0) {
		test_uphill_powergear(inputs, count);
		return 0;
	}
	if (argc >= 3 && strcmp((const legacy_char *)argv[2], "--dualway-check") == 0) {
		assert(test_dualway_routes());
		return 0;
	}

	/* A discarded branch changes collision caches, RNG (on impact), retained
	 * legacy stack words and penalty state. Restoring must undo all of it. */
	struct GENGHOST_SNAPSHOT checkpoint, expected, actual;
	for (legacy_uint frame = 0; frame < 137; frame++) {
		assert(genghost_sim_step(frame < count ? inputs[frame] : INPUT_ACCELERATE_FLAG) >= 0);
	}
	genghost_sim_capture(&checkpoint);
	for (legacy_uint frame = 137; frame < 377; frame++) {
		assert(genghost_sim_step(frame < count ? inputs[frame] : INPUT_ACCELERATE_FLAG) >= 0);
	}
	genghost_sim_capture(&expected);
	genghost_sim_restore(&checkpoint);
	for (legacy_uint frame = 0; frame < 911; frame++) {
		assert(genghost_sim_step(INPUT_ACCELERATE_FLAG | INPUT_STEER_LEFT_FLAG) >= 0);
	}
	genghost_sim_restore(&checkpoint);
	for (legacy_uint frame = 137; frame < 377; frame++) {
		assert(genghost_sim_step(frame < count ? inputs[frame] : INPUT_ACCELERATE_FLAG) >= 0);
	}
	genghost_sim_capture(&actual);
	assert(memcmp(&expected, &actual, sizeof(expected)) == 0);

	legacy_char track_name[REPLAY_TRACK_NAME_SIZE + 1];
	memcpy(track_name, config.game_trackname, REPLAY_TRACK_NAME_SIZE);
	track_name[REPLAY_TRACK_NAME_SIZE] = 0;
	assert(genghost_sim_write_replay((const legacy_char *)argv[2], track_name, inputs, count) == 0);
	legacy_char *state_path = malloc(strlen((const legacy_char *)argv[2]) + sizeof(".states"));
	assert(state_path != NULL);
	sprintf(state_path, "%s.states", (const legacy_char *)argv[2]);
	FILE *output = fopen(state_path, "wb");
	assert(output != NULL);
	free(state_path);
	struct GENGHOST_GUIDE_POINT *guide = calloc(count, sizeof(*guide));
	assert(guide != NULL);
	legacy_u8 serialized[GAMESTATE_SERIALIZED_SIZE];
	LEGACY_WRITE_U16_LE(serialized, count);
	assert(fwrite(serialized, 1, 2, output) == 2);
	genghost_sim_reset();
	for (legacy_uint frame = 0; frame < count; frame++) {
		genghost_guide_capture(&guide[frame], inputs[frame]);
		assert(genghost_sim_step(inputs[frame]) >= 0);
		assert(gamestate_serialize(serialized, &state) == sizeof(serialized));
		assert(fwrite(serialized, 1, sizeof(serialized), output) == sizeof(serialized));
	}
	assert(fclose(output) == 0);
	genghost_sim_capture(&expected);
	genghost_sim_reset();
	for (legacy_uint frame = 0; frame < count; frame++) {
		assert(genghost_sim_step(inputs[frame]) >= 0);
	}
	genghost_sim_capture(&actual);
	assert(memcmp(&expected, &actual, sizeof(expected)) == 0);
	/* Following an unmodified demonstrated trajectory must preserve every input
	 * and the complete final simulator state, including through jumps and slides. */
	genghost_sim_reset();
	legacy_uint cursor = 0;
	for (legacy_uint frame = 0; frame < count; ++frame) {
		legacy_u8 guided = genghost_guide_step(guide, count, &cursor, 0.5);
		assert(guided == inputs[frame]);
		assert(cursor == frame + 1U);
		assert(genghost_sim_step(guided) >= 0);
	}
	genghost_sim_capture(&actual);
	assert(memcmp(&expected, &actual, sizeof(expected)) == 0);
	free(guide);
	struct GENGHOST_RESULT result;
	genghost_sim_result(&result);
	printf(
		"Snapshot branching and replay reset agree for %u frames: finish=%d penalty=%u score=%u.\n",
		count, result.finished, result.penalty_frames, result.total_frames);
	return 0;
}
