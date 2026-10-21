#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../genghost/state_search.h"
#include "../genghost/parallel.h"
#include "../c/externs.h"
#include "../c/car_speed.h"
#include "../c/game_input.h"
#include "../c/owoot_road.h"
#include "../c/track_objects.h"
#include "../c/track_collision.h"
#include "../c/trackdata_layout.h"

#undef memcpy
#undef strlen
#undef printf

/* Transform a fixture point with the native element's cardinal orientation.
 * The collision implementation performs the inverse transform on queries. */
static struct VECTOR world_point(int rotation, int x, int y, int z)
{
	int old_x = x;
	switch (rotation & 1023) {
		case 256:
			x = z;
			z = -old_x;
			break;
		case 512:
			x = -x;
			z = -z;
			break;
		case 768:
			x = -z;
			z = old_x;
			break;
	}
	return (struct VECTOR){(legacy_s16)(11 * 1024 + x), (legacy_s16)y, (legacy_s16)(10 * 1024 + z)};
}

static int wheel_on_road(int rotation, int x, int z)
{
	struct VECTOR vertices[8];
	for (unsigned int rim = 0; rim < 2U; ++rim) {
		for (unsigned int i = 0; i < 4U; ++i) {
			vertices[rim * 4U + i] = world_point(
				rotation, x + (rim ? 5 : -5), i == 0U || i == 3U ? 0 : 20, z + (i < 2U ? -5 : 5));
		}
	}
	return track_road_overlaps_wheel(vertices, 4, 0);
}

/* The green interior of a chicane uses paved native grip, while OWOOT's
 * geometric road test correctly rejects it. Exercise actual native element
 * definitions and collision queries; no planner metadata supplies the answer. */
static void test_fast_grass(void)
{
	unsigned char original[REPLAY_TRACK_SIZE];
	memcpy(original, track_element_map, sizeof(original));
	struct GENGHOST_SNAPSHOT before;
	genghost_sim_capture(&before);
	unsigned int checked = 0U, models = 0U;
	for (unsigned int tile = 1U; tile < 215U; ++tile) {
		const struct TRACKOBJECT *object = &trkObjectList[tile];
		int model = object->ss_physicalModel;
		if ((model != PHYSICAL_MODEL_CHICANE_RIGHT_LEFT &&
			 model != PHYSICAL_MODEL_CHICANE_LEFT_RIGHT) ||
			object->ss_surfaceType != 0 || object->ss_multiTileFlag != 3) {
			continue;
		}
		memset(track_element_map, 0, sizeof(original));
		track_element_map[terrainrows[10] + 10] = (unsigned char)tile;
		track_element_map[terrainrows[9] + 10] = TRACK_TILE_CONTINUATION_SOUTH;
		track_element_map[terrainrows[10] + 11] = TRACK_TILE_CONTINUATION_EAST;
		track_element_map[terrainrows[9] + 11] = TRACK_TILE_CONTINUATION_SOUTHEAST;
		int rotation = object->ss_rotY;
		struct VECTOR grass = world_point(rotation, 0, 0, 500);
		struct VECTOR road =
			world_point(rotation, model == PHYSICAL_MODEL_CHICANE_RIGHT_LEFT ? -350 : 350, 0, 500);
		struct VECTOR ordinary_grass = world_point(rotation, 2500, 0, 500);
		build_track_object(&road, &road);
		assert(current_surf_type == CAR_SURFACE_PAVED);
		build_track_object(&grass, &grass);
		assert(current_surf_type == CAR_SURFACE_PAVED);
		assert(!wheel_on_road(rotation, 0, 500));
		assert(
			wheel_on_road(rotation, model == PHYSICAL_MODEL_CHICANE_RIGHT_LEFT ? -350 : 350, 500));
		build_track_object(&ordinary_grass, &ordinary_grass);
		assert(current_surf_type == CAR_SURFACE_GRASS);
		++checked;
		models |= 1U << (unsigned int)(model - PHYSICAL_MODEL_CHICANE_RIGHT_LEFT);
	}
	memcpy(track_element_map, original, sizeof(original));
	genghost_sim_restore(&before);
	assert(checked >= 2U && models == 3U);
	printf("%u native chicane variants: green interior has paved grip, but fails OWOOT.\n",
		   checked);
}

static unsigned int test_maneuver_rules(const unsigned char *inputs, unsigned int prefix)
{
	genghost_sim_reset();
	struct GENGHOST_ROUTES routes;
	char error[256];
	assert(genghost_routes_build(&routes, error, sizeof(error)) == 0);
	struct GENGHOST_POLICY policy;
	genghost_policy_default(&policy);
	unsigned int proposals = 0U, chicane = 0U;
	for (size_t r = 0; r < routes.count; ++r) {
		struct GENGHOST_DRIVER driver;
		genghost_sim_reset();
		genghost_driver_reset(&driver);
		for (unsigned int i = 0; i < prefix; ++i) {
			assert(genghost_sim_step(inputs[i]) == 0);
			genghost_driver_observe(&routes.routes[r], &driver);
		}
		for (unsigned int flags = 0; flags < 8U; ++flags) {
			int pg = (flags & 1U) != 0U, lc = (flags & 2U) != 0U;
			int owoot = (flags & 4U) != 0U;
			struct GENGHOST_SNAPSHOT before, after;
			genghost_sim_capture(&before);
			struct GENGHOST_MANEUVER actions[GENGHOST_MANEUVER_CAPACITY];
			size_t count = genghost_maneuvers(&routes.routes[r], &driver, &policy, pg, lc, owoot,
											  actions, GENGHOST_MANEUVER_CAPACITY);
			assert(count > 0U && count <= GENGHOST_MANEUVER_CAPACITY);
			assert(actions[0].kind == GENGHOST_MANEUVER_FOLLOW);
			unsigned int landings = 0U;
			for (size_t i = 0; i < count; ++i) {
				const struct GENGHOST_MANEUVER *action = &actions[i];
				assert(action->horizon_frames > 0U &&
					   action->horizon_frames <= GENGHOST_MAX_FRAMES);
				assert(pg || action->kind != GENGHOST_MANEUVER_POWERGEAR);
				if (owoot) {
					assert(action->kind != GENGHOST_MANEUVER_CHICANE_GRASS);
					assert(action->kind != GENGHOST_MANEUVER_SLALOM_JUMP);
					assert(action->kind != GENGHOST_MANEUVER_TILE_TOUCH);
					assert(action->kind != GENGHOST_MANEUVER_STUNT);
					assert(action->kind != GENGHOST_MANEUVER_STUNT_ALTERNATE);
					if (action->kind == GENGHOST_MANEUVER_SLALOM_CENTER) {
						assert(action->road_only && genghost_slalom_center_fits());
					}
					if (action->kind == GENGHOST_MANEUVER_POWERGEAR) {
						assert(action->gene.stunt_mode == 0U && action->gene.line_mode == 0U);
						assert(action->gene.shortcut_skip == 0U);
					}
				}
				struct GENGHOST_DRIVER child = driver;
				unsigned char input = genghost_maneuver_step(&routes.routes[r], &policy, &child,
															 action, 0U, pg, lc, owoot);
				assert(!(input & ~(INPUT_DRIVING_MASK | INPUT_ACTION_BUTTON_MASK)));
				++proposals;
				chicane += action->kind == GENGHOST_MANEUVER_CHICANE_GRASS;
				landings += action->kind == GENGHOST_MANEUVER_LANDING;
			}
			if (!owoot && state.playerstate.car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE &&
				state.playerstate.car_actual_speed > 0U) {
				assert(landings == 3U);
			}
			genghost_sim_capture(&after);
			assert(memcmp(&before, &after, sizeof(before)) == 0);
		}
	}
	genghost_routes_free(&routes);
	printf("%u maneuver proposals preserve native state and respect rule gates (%u fast-grass).\n",
		   proposals, chicane);
	return chicane;
}

/* Geometry-only fixtures retain the native car dimensions and element table.
 * Cardinal rotations and reverse approaches must project the same actual
 * movement line, even when a preceding launch would otherwise fill the action
 * list. The from-start test below establishes physical feasibility separately. */
static void test_slalom_proposal_geometry(void)
{
	struct GENGHOST_SNAPSHOT saved;
	genghost_sim_capture(&saved);
	legacy_s8 saved_ids[2] = {track_route_element_ids[0], track_route_element_ids[1]};
	legacy_s8 saved_columns[2] = {track_route_columns[0], track_route_columns[1]};
	legacy_s8 saved_rows[2] = {track_route_rows[0], track_route_rows[1]};
	unsigned int checked = 0U, loop_tile = 0U;
	for (unsigned int tile = 1U; tile < 215U; ++tile) {
		if (trkObjectList[tile].ss_physicalModel == PHYSICAL_MODEL_LOOP) {
			loop_tile = tile;
			break;
		}
	}
	assert(loop_tile != 0U);
	for (unsigned int tile = 1U; tile < 215U; ++tile) {
		const struct TRACKOBJECT *object = &trkObjectList[tile];
		if (object->ss_physicalModel != PHYSICAL_MODEL_SLALOM || object->ss_surfaceType != 0) {
			continue;
		}
		for (int direction = -1; direction <= 1; direction += 2) {
			struct GENGHOST_WAYPOINT points[8] = {0};
			struct GENGHOST_ROUTE route = {0};
			route.points = points;
			route.point_count = 8U;
			route.length = 2600.0;
			track_route_element_ids[0] = (legacy_s8)loop_tile;
			track_route_element_ids[1] = (legacy_s8)tile;
			track_route_columns[0] = track_route_columns[1] = 10;
			track_route_rows[0] = track_route_rows[1] = 10;
			double ox = track_object_base_x(object, 10);
			double oz = track_object_base_z(object, 10);
			for (size_t point = 0; point < 8U; ++point) {
				int z = point < 4U ? -1500 + (int)point * 300 : -512 + (int)(point - 4U) * 341;
				struct VECTOR placed = world_point(object->ss_rotY, 0, 0, direction * z);
				points[point].x = ox + placed.x - 11 * 1024;
				points[point].z = oz + placed.z - 10 * 1024;
				points[point].piece = point < 4U ? 0 : 1;
				points[point].piece_index = point < 4U ? 0 : 1;
				points[point].physical_model =
					point < 4U ? PHYSICAL_MODEL_LOOP : PHYSICAL_MODEL_SLALOM;
				points[point].distance = point < 4U ? point * 300.0 : 1000.0 + (point - 4U) * 341.0;
				points[point].speed = 65.0;
			}
			struct VECTOR here = world_point(object->ss_rotY, 58, 0, direction * -420);
			struct VECTOR previous = world_point(object->ss_rotY, 57, 0, direction * -450);
			state.playerstate.car_position.lx = (legacy_s32)((ox + here.x - 11 * 1024) * 64.0);
			state.playerstate.car_position.lz = (legacy_s32)((oz + here.z - 10 * 1024) * 64.0);
			state.playerstate.car_previous_position.lx =
				(legacy_s32)((ox + previous.x - 11 * 1024) * 64.0);
			state.playerstate.car_previous_position.lz =
				(legacy_s32)((oz + previous.z - 10 * 1024) * 64.0);
			state.playerstate.car_actual_speed = 150 * 256;
			struct GENGHOST_DRIVER driver = {0};
			driver.progress = 600.0;
			struct GENGHOST_POLICY policy;
			genghost_policy_default(&policy);
			for (unsigned int flags = 0U; flags < 8U; ++flags) {
				int airborne = (flags & 1U) != 0U, collision = (flags & 2U) != 0U;
				int owoot = (flags & 4U) != 0U;
				state.playerstate.car_sumSurfAllWheels = airborne ? CAR_WHEEL_CONTACT_NONE : 4;
				struct GENGHOST_SNAPSHOT before, after;
				genghost_sim_capture(&before);
				struct GENGHOST_MANEUVER actions[GENGHOST_MANEUVER_CAPACITY];
				size_t count = genghost_maneuvers(&route, &driver, &policy, 1, collision, owoot,
												  actions, GENGHOST_MANEUVER_CAPACITY);
				unsigned int centers = 0U, jumps = 0U, landings = 0U;
				for (size_t i = 0U; i < count; ++i) {
					const struct GENGHOST_MANEUVER *action = &actions[i];
					if (action->kind == GENGHOST_MANEUVER_SLALOM_CENTER) {
						++centers;
						assert(action->gene.lateral_offset == 0.0 &&
							   action->gene.slide_brake == 0.0);
						assert(action->gene.stunt_mode == GENGHOST_STUNT_SLALOM_CENTER);
						/* The preceding loop retains its ordinary approach.
						 * A dedicated center proposal must reach the slalom
						 * instead of selecting the generic loop/ramp bypass. */
						struct GENGHOST_WAYPOINT target;
						double protected_end = 0.0;
						assert(genghost_stunt_target(&route, &driver, &action->gene, 150.0,
													 collision, &target, &protected_end));
						assert(target.physical_model == PHYSICAL_MODEL_SLALOM && target.piece == 1);
						assert(protected_end == points[7].distance);
					}
					if (action->kind == GENGHOST_MANEUVER_SLALOM_JUMP) {
						++jumps;
						double dx = state.playerstate.car_position.lx -
									(double)state.playerstate.car_previous_position.lx;
						double dz = state.playerstate.car_position.lz -
									(double)state.playerstate.car_previous_position.lz;
						double tx = action->exit.x - state.playerstate.car_position.lx / 64.0;
						double tz = action->exit.z - state.playerstate.car_position.lz / 64.0;
						double cross = dx * tz - dz * tx;
						assert(cross > -0.01 && cross < 0.01);
						assert(dx * tx + dz * tz > 0.0);
						assert(action->physical_model == PHYSICAL_MODEL_SLALOM);
					}
					landings += action->kind == GENGHOST_MANEUVER_LANDING;
				}
				assert(centers == (!owoot || genghost_slalom_center_fits() ? 1U : 0U));
				assert(jumps == (!owoot && airborne ? 1U : 0U));
				assert(landings == (!owoot && airborne ? 3U : 0U));
				genghost_sim_capture(&after);
				assert(memcmp(&before, &after, sizeof(before)) == 0);
				++checked;
			}
		}
	}
	for (unsigned int i = 0U; i < 2U; ++i) {
		track_route_element_ids[i] = saved_ids[i];
		track_route_columns[i] = saved_columns[i];
		track_route_rows[i] = saved_rows[i];
	}
	genghost_sim_restore(&saved);
	assert(checked >= 32U);
	printf(
		"%u slalom rotation/contact/rule cases retain center, flight and landing alternatives.\n",
		checked);
}

/* Optional integration fixture: inputs must reach the first DEFAULT slalom
 * from the actual start without editing native state. Both cars and collision
 * settings use exactly the same proposal code, with no car-name dispatch. */
static void test_slalom_continuation(const struct GENGHOST_OPTIONS *options,
									 const unsigned char *inputs, unsigned int prefix, int jump)
{
	assert(!jump || !options->owoot);
	genghost_sim_reset();
	struct GENGHOST_ROUTES routes;
	char error[256];
	assert(genghost_routes_build(&routes, error, sizeof(error)) == 0);
	assert(routes.count > 0U);
	const struct GENGHOST_ROUTE *route = &routes.routes[0];
	struct GENGHOST_DRIVER driver;
	genghost_driver_reset(&driver);
	for (unsigned int frame = 0U; frame < prefix; ++frame) {
		assert(genghost_sim_step(inputs[frame]) == 0);
		genghost_driver_observe(route, &driver);
	}
	assert((state.playerstate.car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE) == jump);
	struct GENGHOST_POLICY policy;
	genghost_policy_default(&policy);
	struct GENGHOST_MANEUVER actions[GENGHOST_MANEUVER_CAPACITY];
	size_t count =
		genghost_maneuvers(route, &driver, &policy, options->powergear, options->collision,
						   options->owoot, actions, GENGHOST_MANEUVER_CAPACITY);
	size_t chosen = count;
	for (size_t i = 0U; i < count; ++i) {
		if (actions[i].kind ==
			(jump ? GENGHOST_MANEUVER_SLALOM_JUMP : GENGHOST_MANEUVER_SLALOM_CENTER)) {
			chosen = i;
			break;
		}
	}
	assert(chosen < count);
	const struct GENGHOST_MANEUVER *action = &actions[chosen];
	int piece = route->points[driver.waypoint].piece;
	if (action->kind == GENGHOST_MANEUVER_SLALOM_CENTER) {
		piece = action->approach.piece;
	} else {
		piece = action->exit.piece;
	}
	const struct TRACKOBJECT *object =
		&trkObjectList[(unsigned char)track_route_element_ids[piece]];
	assert(object->ss_physicalModel == PHYSICAL_MODEL_SLALOM);
	double ox = track_object_base_x(object, (unsigned char)track_route_columns[piece]);
	double oz = track_object_base_z(object, (unsigned char)track_route_rows[piece]);
	struct VECTOR axis = world_point(object->ss_rotY, 0, 0, 1);
	double dx = axis.x - 11 * 1024, dz = axis.z - 10 * 1024;
	if (dx * action->exit_heading_x + dz * action->exit_heading_z < 0.0) {
		dx = -dx;
		dz = -dz;
	}
	struct GENGHOST_RESULT before, after;
	genghost_sim_result(&before);
	unsigned int speed = state.playerstate.car_actual_speed;
	int cleared = 0;
	unsigned int ticks = 0U;
	for (; ticks < action->horizon_frames; ++ticks) {
		unsigned char input =
			genghost_maneuver_step(route, &policy, &driver, action, ticks, options->powergear,
								   options->collision, options->owoot);
		assert(!(input & INPUT_BRAKE_FLAG));
		assert(genghost_sim_step(input) == 0);
		genghost_driver_observe(route, &driver);
		genghost_sim_result(&after);
		assert(!after.crashed && after.penalty_frames == before.penalty_frames);
		assert(state.playerstate.car_actual_speed + 2U * 256U >= speed);
		double along = (state.playerstate.car_position.lx / 64.0 - ox) * dx +
					   (state.playerstate.car_position.lz / 64.0 - oz) * dz;
		if (along > SLALOM_POLE_FAR_Z + simd_player.collide_points[1].px + 20.0) {
			cleared = 1;
			break;
		}
	}
	assert(cleared);
	assert(state.playerstate.car_actual_speed >= speed);
	printf("Native %s slalom continuation clears both stones in %u frames: %.2f -> %.2f mph, no "
		   "brakes or added penalty.\n",
		   jump ? "airborne" : "center", ticks + 1U, speed / 256.0,
		   state.playerstate.car_actual_speed / 256.0);
	genghost_routes_free(&routes);
}

static void same_reply(struct GENGHOST_STATE_REPLY *first, struct GENGHOST_STATE_REPLY *second)
{
	/* Physical work varies with checkpoint reuse; candidate identity, inputs,
	 * scoring, controller state and native features must not. */
	first->simulated_frames = second->simulated_frames = 0U;
	assert(memcmp(first, second, sizeof(*first)) == 0);
}

static void execute_and_compare(const struct GENGHOST_OPTIONS *options,
								struct GENGHOST_STATE_EXECUTOR *cached,
								const struct GENGHOST_STATE_TASK *task,
								struct GENGHOST_STATE_REPLY *actual)
{
	struct GENGHOST_STATE_REPLY expected = {0};
	assert(genghost_state_execute(task, actual, cached) == 0);
	assert(actual->id == task->id && actual->available && actual->length > task->prefix_length);
	assert(memcmp(actual->inputs, task->prefix, task->prefix_length) == 0);
	struct GENGHOST_SNAPSHOT actual_state, expected_state, replayed_state;
	genghost_sim_capture(&actual_state);
	struct GENGHOST_STATE_EXECUTOR *fresh = genghost_state_executor_create(options);
	assert(fresh != NULL);
	assert(genghost_state_execute(task, &expected, fresh) == 0);
	genghost_sim_capture(&expected_state);
	genghost_state_executor_destroy(fresh);
	same_reply(actual, &expected);
	assert(memcmp(&actual_state, &expected_state, sizeof(actual_state)) == 0);
	struct GENGHOST_RESULT replayed;
	if (actual->transition_stop) {
		assert(task->kind == 0U && actual->action_ticks >= 4U);
		assert(genghost_sim_evaluate(actual->inputs, actual->length - 1U, &replayed) == 0);
		int airborne = state.playerstate.car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE;
		int ready = options->powergear && genghost_maneuver_powergear_ready();
		assert(genghost_sim_step(actual->inputs[actual->length - 1U]) >= 0);
		int now_airborne = state.playerstate.car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE;
		int now_ready = options->powergear && genghost_maneuver_powergear_ready();
		assert(airborne != now_airborne || (!ready && now_ready));
		genghost_sim_result(&replayed);
	} else {
		assert(genghost_sim_evaluate(actual->inputs, actual->length, &replayed) == 0);
	}
	genghost_sim_capture(&replayed_state);
	assert(memcmp(&actual_state, &replayed_state, sizeof(actual_state)) == 0);
	assert(memcmp(&actual->race, &replayed, sizeof(replayed)) == 0);
}

static void test_executor(const struct GENGHOST_OPTIONS *options, const unsigned char *inputs,
						  unsigned int prefix)
{
	struct GENGHOST_STATE_EXECUTOR *cached = genghost_state_executor_create(options);
	assert(cached != NULL);
	struct GENGHOST_STATE_TASK task = {0};
	task.prefix_length = prefix;
	memcpy(task.prefix, inputs, prefix);
	unsigned int transitions = 0U;
	for (unsigned int trial = 0U; trial < 6U; ++trial) {
		/* Same length and long common prefix, but a different final steering
		 * input. A cache keyed by length/progress rather than full inputs fails. */
		task.id = trial + 1U;
		task.action_index = trial & 1U;
		task.prefix[prefix - 1U] = inputs[prefix - 1U] ^ (trial & 2U ? INPUT_STEER_LEFT_FLAG : 0U);
		struct GENGHOST_STATE_REPLY actual = {0};
		execute_and_compare(options, cached, &task, &actual);
		transitions += actual.transition_stop != 0U;
	}
	genghost_state_executor_destroy(cached);
	printf("Cached/fresh workers and full replay agree in every state byte for six branches (%u "
		   "event stops).\n",
		   transitions);
}

static void test_guided_executor(const struct GENGHOST_OPTIONS *options,
								 const unsigned char *inputs, unsigned int count,
								 unsigned int prefix)
{
	assert(prefix + 1U < count);
	struct GENGHOST_STATE_EXECUTOR *cached = genghost_state_executor_create(options);
	assert(cached != NULL);
	struct GENGHOST_STATE_TASK task = {0};
	task.kind = 3U;
	task.prefix_length = prefix;
	task.reference_first = prefix;
	task.reference_length = count;
	for (unsigned int trial = 0U; trial < 5U; ++trial) {
		task.id = 100U + trial;
		task.action_index = trial == 4U ? 1U : 0U;
		memcpy(task.prefix, inputs, prefix);
		memcpy(task.reference, inputs, count);
		/* Reference length and cursor stay fixed while its actual input bytes
		 * change. Guide reconstruction must not accidentally use a previous
		 * demonstration or leave the engine at the end of that demonstration. */
		if (trial == 1U) {
			task.reference[prefix + 1U] ^= INPUT_STEER_LEFT_FLAG;
		} else if (trial == 2U) {
			task.reference[prefix - 1U] ^= INPUT_STEER_RIGHT_FLAG;
		} else if (trial == 3U) {
			task.prefix[prefix - 1U] ^= INPUT_STEER_LEFT_FLAG;
		}
		struct GENGHOST_STATE_REPLY actual = {0};
		execute_and_compare(options, cached, &task, &actual);
		if (trial == 0U || trial == 4U) {
			/* An unchanged native trajectory must keep every control byte, even
			 * through a jump or collision-dependent stunt. Its finish/crash is
			 * decided by the selected native rules, not the reference's header. */
			assert(actual.length <= count);
			assert(memcmp(actual.inputs, inputs, actual.length) == 0);
		}
	}
	genghost_state_executor_destroy(cached);
	puts("Guided continuation preserves exact reference inputs and native state across five cache "
		 "changes.");
}

/* Use the native drivetrain itself as an oracle. A readiness classifier is
 * useful only if its opportunities include real positive unsigned-divide
 * acceleration, not merely a high gear, a launch, or a speed threshold. */
static void test_powergear(const unsigned char *inputs, unsigned int count)
{
	legacy_s8 *on[] = {(legacy_s8 *)"test", (legacy_s8 *)"--pg:on"};
	legacy_s8 *off[] = {(legacy_s8 *)"test", (legacy_s8 *)"--pg:off"};
	unsigned int opportunities = 0U, boosts = 0U, first_boost = GENGHOST_MAX_FRAMES;
	genghost_sim_reset();
	configure_powergear_bug(2, on);
	for (unsigned int frame = 0; frame < count; ++frame) {
		struct GENGHOST_SNAPSHOT before, after;
		genghost_sim_capture(&before);
		int ready = genghost_maneuver_powergear_ready();
		genghost_sim_capture(&after);
		assert(memcmp(&before, &after, sizeof(before)) == 0);
		if (ready) {
			assert(!state.playerstate.car_changing_gear);
			assert(state.playerstate.car_sumSurfRearWheels != CAR_WHEEL_CONTACT_NONE);
			struct CARSTATE original = state.playerstate, corrected = state.playerstate;
			update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &original, &simd_player);
			genghost_sim_restore(&before);
			configure_powergear_bug(2, off);
			update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &corrected, &simd_player);
			genghost_sim_restore(&before);
			configure_powergear_bug(2, on);
			++opportunities;
			if (original.car_actual_speed > corrected.car_actual_speed) {
				++boosts;
				if (first_boost == GENGHOST_MAX_FRAMES) {
					first_boost = frame;
				}
			}
		}
		assert(genghost_sim_step(inputs[frame]) >= 0);
	}
	assert(opportunities > 0U && boosts > 0U);
	struct GENGHOST_RESULT race;
	genghost_sim_result(&race);
	assert(race.finished && first_boost < race.finish_frame);
	printf("%u PG opportunities include %u native acceleration advantages; first at frame %u.\n",
		   opportunities, boosts, first_boost);
}

static void test_failed_pilot_recovery(const struct GENGHOST_OPTIONS *options)
{
	struct GENGHOST_OPTIONS bounded = *options;
	bounded.evaluations = 2501U;
	bounded.seed = 1U;
	bounded.seconds = 0.0;
	struct GENGHOST_SEARCH_RESULT result = {0};
	/* The recording supplies its map and car only. No human inputs enter the
	 * search, so a section start before the first finish must come from an
	 * autonomous pilot that failed later in its run. */
	int status = genghost_state_search(&bounded, NULL, 0U, &result, NULL, NULL, NULL, NULL);
	assert(status == 0 || status == 2);
	assert(result.evaluations == bounded.evaluations);
	assert(!result.seed_finished && !result.seed_total_frames);
	assert(result.state_rescue_starts > 0U && result.state_section_evaluations > 0U);
	if (result.race.finished) {
		struct GENGHOST_RESULT replayed;
		assert(genghost_sim_evaluate(result.inputs, result.length, &replayed) == 0);
		assert(memcmp(&result.race, &replayed, sizeof(replayed)) == 0);
	}
	printf("Map-only search expanded %llu failed-pilot sections without human input seeds.\n",
		   (unsigned long long)result.state_rescue_starts);
}

/* The batch hook exposes actual scheduled work without replacing native
 * physics. Reference hashes remember verified incumbents, including older
 * section references that remain in flight after a newer lap is found. */
#define FINE_TUNE_TEST_BUDGET 1201U
struct fine_tune_observer {
	struct GENGHOST_STATE_EXECUTOR *executor;
	struct GENGHOST_SEARCH_RESULT *result;
	unsigned int initial_total, last_total, saves, batches;
	const unsigned char *fixed_inputs;
	unsigned int fixed_length;
	unsigned int kinds[4];
	unsigned int reference_lengths[2U * FINE_TUNE_TEST_BUDGET];
	uint64_t reference_hashes[2U * FINE_TUNE_TEST_BUDGET];
};

static uint64_t fine_tune_input_hash(const unsigned char *inputs, unsigned int count)
{
	uint64_t hash = UINT64_C(14695981039346656037);
	for (unsigned int n = 0U; n < count; ++n) {
		hash = (hash ^ inputs[n]) * UINT64_C(1099511628211);
	}
	return hash;
}

static int fine_tune_known_reference(const struct fine_tune_observer *observer,
									 const unsigned char *inputs, unsigned int count)
{
	uint64_t hash = fine_tune_input_hash(inputs, count);
	for (unsigned int n = 0U; n < observer->saves * 2U; ++n) {
		if (observer->reference_lengths[n] == count && observer->reference_hashes[n] == hash) {
			return 1;
		}
	}
	return 0;
}

static int fine_tune_save(const struct GENGHOST_SEARCH_RESULT *result, void *context)
{
	struct fine_tune_observer *observer = context;
	struct GENGHOST_RESULT replayed;
	struct GENGHOST_SNAPSHOT before;
	if (observer->fixed_inputs) {
		assert(result->length == observer->fixed_length);
		for (unsigned int frame = 0U; frame < result->length; ++frame) {
			assert((result->inputs[frame] & ~INPUT_ACTION_BUTTON_MASK) ==
				   (observer->fixed_inputs[frame] & ~INPUT_ACTION_BUTTON_MASK));
		}
	}
	/* Observing an accepted replay must not change a worker's hidden collision
	 * state or the search's current section checkpoint. */
	genghost_sim_capture(&before);
	assert(genghost_sim_evaluate(result->inputs, result->length, &replayed) == 0);
	assert(replayed.finished && memcmp(&replayed, &result->race, sizeof(replayed)) == 0);
	genghost_sim_restore(&before);
	assert(observer->saves < FINE_TUNE_TEST_BUDGET);
	if (observer->saves) {
		assert(replayed.total_frames < observer->last_total);
	} else {
		assert(replayed.total_frames == observer->initial_total);
	}
	unsigned int slot = observer->saves * 2U;
	observer->reference_lengths[slot] = replayed.finish_frame;
	observer->reference_hashes[slot] = fine_tune_input_hash(result->inputs, replayed.finish_frame);
	observer->reference_lengths[slot + 1U] = result->length;
	observer->reference_hashes[slot + 1U] = fine_tune_input_hash(result->inputs, result->length);
	observer->last_total = replayed.total_frames;
	++observer->saves;
	return 0;
}

static int fine_tune_batch(void *context, const struct GENGHOST_STATE_TASK *tasks,
						   struct GENGHOST_STATE_REPLY *replies, size_t count)
{
	struct fine_tune_observer *observer = context;
	++observer->batches;
	assert(observer->executor != NULL);
	for (size_t n = 0U; n < count; ++n) {
		const struct GENGHOST_STATE_TASK *task = &tasks[n];
		assert(task->kind < 4U && task->kind != 1U);
		if (observer->fixed_inputs) {
			assert(task->kind == 2U && task->prefix_length == observer->fixed_length);
			for (unsigned int frame = 0U; frame < task->prefix_length; ++frame) {
				assert((task->prefix[frame] & ~INPUT_ACTION_BUTTON_MASK) ==
					   (observer->fixed_inputs[frame] & ~INPUT_ACTION_BUTTON_MASK));
			}
		}
		++observer->kinds[task->kind];
		if (task->kind == 0U) {
			/* A seeded launch section may legitimately begin at frame zero.
			 * Fresh grid beams have no incumbent section start. */
			assert(observer->result->state_section_starts > 0U);
		} else if (task->kind == 2U) {
			/* Local edits may already be applied in the request. The inputs
			 * before the declared edit still belong to the verified parent. */
			assert(task->first <= task->prefix_length && task->first <= observer->result->length);
			assert(memcmp(task->prefix, observer->result->inputs, task->first) == 0);
		} else {
			assert(task->reference_length > 0U && task->reference_first <= task->reference_length);
			assert(fine_tune_known_reference(observer, task->reference, task->reference_length));
		}
		assert(genghost_state_execute(task, &replies[n], observer->executor) == 0);
	}
	return 0;
}

static int fine_tune_search(const struct GENGHOST_OPTIONS *options, const unsigned char *inputs,
							unsigned int count, struct GENGHOST_SEARCH_RESULT *result,
							struct fine_tune_observer *observer)
{
	observer->result = result;
	return options->strategy == GENGHOST_STRATEGY_STATE
			   ? genghost_state_search(options, inputs, count, result, fine_tune_save, observer,
									   fine_tune_batch, observer)
			   : genghost_search(options, inputs, count, result, fine_tune_save, observer);
}

/* Moving shift timing must preserve the pulse and every unrelated input,
 * including controls at the old and new pulse boundaries. */
static void test_shift_inputs(void)
{
	enum { LENGTH = 128 };
	const unsigned int mask = INPUT_SHIFT_UP_FLAG | INPUT_SHIFT_DOWN_FLAG;
	const unsigned int starts[] = {40U, 88U}, widths[] = {3U, 4U};
	const unsigned int flags[] = {INPUT_SHIFT_UP_FLAG, INPUT_SHIFT_DOWN_FLAG};
	static const int offsets[] = {-1, 1, -2, 2, -4, 4, -8, 8, -16, 16, -32, 32};
	unsigned char original[LENGTH + 2U], actual[LENGTH + 2U], expected[LENGTH + 2U];
	original[0] = 0xA5U;
	original[LENGTH + 1U] = 0x5AU;
	for (unsigned int frame = 0U; frame < LENGTH; ++frame) {
		original[frame + 1U] = (unsigned char)(frame % 16U);
	}
	for (unsigned int event = 0U; event < 2U; ++event) {
		for (unsigned int frame = starts[event]; frame < starts[event] + widths[event]; ++frame) {
			original[frame + 1U] |= (unsigned char)flags[event];
		}
	}
	for (unsigned int proposal = 0U; proposal < 24U; ++proposal) {
		unsigned int event = proposal % 2U;
		unsigned int old_start = starts[event];
		unsigned int new_start = (unsigned int)((int)old_start + offsets[proposal / 2U]);
		memcpy(actual, original, sizeof(actual));
		memcpy(expected, original, sizeof(expected));
		for (unsigned int n = 0U; n < widths[event]; ++n) {
			expected[old_start + n + 1U] &= (unsigned char)~mask;
		}
		for (unsigned int n = 0U; n < widths[event]; ++n) {
			expected[new_start + n + 1U] |= (unsigned char)flags[event];
		}
		unsigned int first = LENGTH, end = 0U;
		assert(genghost_shift_inputs(actual + 1U, LENGTH, proposal, &first, &end) == 1);
		assert(memcmp(actual, expected, sizeof(actual)) == 0);
		assert(first <= old_start && first <= new_start && end <= LENGTH);
		assert(end >= old_start + widths[event] && end >= new_start + widths[event]);
		for (unsigned int frame = 0U; frame < LENGTH; ++frame) {
			assert((actual[frame + 1U] & ~mask) == (original[frame + 1U] & ~mask));
		}
	}

	unsigned char edges[16], changed[16];
	for (unsigned int frame = 0U; frame < sizeof(edges); ++frame) {
		edges[frame] = (unsigned char)(frame % 16U);
	}
	edges[0] |= INPUT_SHIFT_UP_FLAG;
	edges[1] |= INPUT_SHIFT_UP_FLAG;
	edges[12] |= INPUT_SHIFT_DOWN_FLAG;
	edges[13] |= INPUT_SHIFT_DOWN_FLAG;
	unsigned int first = 0U, end = 0U;
	/* Frame-zero pulses are eligible, while an earlier launch and a later
	 * pulse beyond the input suffix are rejected without touching controls. */
	memcpy(changed, edges, sizeof(changed));
	assert(genghost_shift_inputs(changed, sizeof(changed), 0U, &first, &end) == 0);
	assert(memcmp(changed, edges, sizeof(changed)) == 0);
	assert(genghost_shift_inputs(changed, sizeof(changed), 23U, &first, &end) == 0);
	assert(memcmp(changed, edges, sizeof(changed)) == 0);
	assert(genghost_shift_inputs(changed, sizeof(changed), 2U, &first, &end) == 1);
	assert(!(changed[0] & mask) && (changed[1] & mask) == INPUT_SHIFT_UP_FLAG &&
		   (changed[2] & mask) == INPUT_SHIFT_UP_FLAG && !(changed[3] & mask));
	for (unsigned int frame = 0U; frame < sizeof(changed); ++frame) {
		assert((changed[frame] & ~mask) == (edges[frame] & ~mask));
	}

	/* Leave the clear tick separating neighboring shifts intact: merging
	 * held buttons can suppress a later native gear change. */
	for (unsigned int frame = 0U; frame < sizeof(edges); ++frame) {
		edges[frame] &= (unsigned char)~mask;
	}
	edges[4] |= INPUT_SHIFT_UP_FLAG;
	edges[5] |= INPUT_SHIFT_UP_FLAG;
	edges[7] |= INPUT_SHIFT_DOWN_FLAG;
	edges[8] |= INPUT_SHIFT_DOWN_FLAG;
	memcpy(changed, edges, sizeof(changed));
	assert(genghost_shift_inputs(changed, sizeof(changed), 2U, &first, &end) == 0);
	assert(memcmp(changed, edges, sizeof(changed)) == 0);
	assert(genghost_shift_inputs(changed, sizeof(changed), 1U, &first, &end) == 0);
	assert(memcmp(changed, edges, sizeof(changed)) == 0);
	/* Large proposals may neither cross nor overwrite a neighboring pulse. */
	assert(genghost_shift_inputs(changed, sizeof(changed), 10U, &first, &end) == 0);
	assert(memcmp(changed, edges, sizeof(changed)) == 0);

	for (unsigned int frame = 0U; frame < sizeof(edges); ++frame) {
		edges[frame] &= (unsigned char)~mask;
	}
	memcpy(changed, edges, sizeof(changed));
	assert(genghost_shift_inputs(changed, sizeof(changed), 0U, &first, &end) == 0);
	assert(memcmp(changed, edges, sizeof(changed)) == 0);
	assert(genghost_shift_inputs(changed, 0U, 0U, &first, &end) == 0);
	assert(memcmp(changed, edges, sizeof(changed)) == 0);
	puts("Shift refinement moves up/down pulses early and late without changing other controls.");
}

static void test_fine_tune(const struct GENGHOST_OPTIONS *options, const unsigned char *inputs,
						   unsigned int count)
{
	struct GENGHOST_RESULT original, unfinished;
	assert(genghost_sim_evaluate(inputs, count, &original) == 0 && original.finished);
	assert(genghost_sim_evaluate(inputs, 1U, &unfinished) == 0 && !unfinished.finished);
	for (int strategy = GENGHOST_STRATEGY_LEGACY; strategy <= GENGHOST_STRATEGY_STATE; ++strategy) {
		struct GENGHOST_OPTIONS bounded = *options;
		bounded.fine_tune = 1;
		bounded.strategy = (enum GENGHOST_STRATEGY)strategy;
		bounded.evaluations = 1U;
		bounded.seconds = 0.0;
		bounded.seed = 1U;
		const unsigned char *invalid_inputs[] = {NULL, NULL, inputs, inputs};
		const unsigned int invalid_lengths[] = {0U, 1U, 0U, 1U};
		for (unsigned int invalid = 0U; invalid < 4U; ++invalid) {
			struct GENGHOST_SEARCH_RESULT result = {0};
			struct fine_tune_observer observer = {0};
			assert(fine_tune_search(&bounded, invalid_inputs[invalid], invalid_lengths[invalid],
									&result, &observer) < 0);
			assert(!observer.saves && !observer.batches && !result.race.finished);
		}
		struct GENGHOST_SEARCH_RESULT retained = {0};
		struct fine_tune_observer retention = {0};
		retention.initial_total = original.total_frames;
		assert(fine_tune_search(&bounded, inputs, count, &retained, &retention) == 0);
		assert(retention.saves == 1U && !retention.batches && retained.evaluations == 1U);
		assert(retained.seed_finished && retained.seed_total_frames == original.total_frames);
		assert(retained.race.finished && retained.race.total_frames == original.total_frames &&
			   retained.race.finish_frame == original.finish_frame &&
			   retained.race.penalty_frames == original.penalty_frames);
		assert(retained.length >= original.finish_frame &&
			   memcmp(retained.inputs, inputs, original.finish_frame) == 0);

		struct GENGHOST_SEARCH_RESULT refined = {0};
		struct fine_tune_observer refinement = {0};
		refinement.initial_total = original.total_frames;
		bounded.evaluations = strategy == GENGHOST_STRATEGY_STATE ? FINE_TUNE_TEST_BUDGET : 129U;
		if (strategy == GENGHOST_STRATEGY_STATE) {
			refinement.executor = genghost_state_executor_create(&bounded);
			assert(refinement.executor != NULL);
		}
		assert(fine_tune_search(&bounded, inputs, count, &refined, &refinement) == 0);
		genghost_state_executor_destroy(refinement.executor);
		assert(refined.evaluations == bounded.evaluations && refinement.saves > 0U);
		assert(refined.race.finished && refined.race.total_frames <= original.total_frames);
		assert(refined.race.total_frames == refinement.last_total);
		if (strategy == GENGHOST_STRATEGY_STATE) {
			assert(refinement.kinds[0] && refinement.kinds[2] && refinement.kinds[3]);
			assert(!refinement.kinds[1] && !refined.state_rescue_starts);
			assert(refined.state_section_starts && refined.state_section_evaluations);
		}
		printf("Fine-tune strategy %d rejects missing/unfinished seeds, retains budget-one inputs, "
			   "and verifies %u strictly improving incumbent updates over %llu evaluations.\n",
			   strategy, refinement.saves - 1U, (unsigned long long)refined.evaluations);
	}
}

/* A real automatic lap supplies both upshifts and downshifts. Their manual
 * timing can improve without retiming a single driving input or tail frame. */
static void test_gears_only_search(const struct GENGHOST_OPTIONS *options,
								   const unsigned char *inputs, unsigned int count)
{
	unsigned char manual[GENGHOST_MAX_FRAMES];
	memcpy(manual, inputs, count);
	struct GENGHOST_RESULT baseline;
	assert(genghost_sim_manual_inputs(manual, count, &baseline) == 0 && baseline.finished);
	for (int strategy = GENGHOST_STRATEGY_LEGACY; strategy <= GENGHOST_STRATEGY_STATE; ++strategy) {
		struct GENGHOST_OPTIONS bounded = *options;
		bounded.fine_tune = bounded.fine_tune_gears_only = 1;
		bounded.strategy = (enum GENGHOST_STRATEGY)strategy;
		bounded.evaluations = 257U;
		bounded.seconds = 0.0;
		bounded.seed = 1U;
		struct GENGHOST_SEARCH_RESULT result = {0};
		struct fine_tune_observer observer = {0};
		observer.initial_total = baseline.total_frames;
		observer.fixed_inputs = manual;
		observer.fixed_length = count;
		if (strategy == GENGHOST_STRATEGY_STATE) {
			observer.executor = genghost_state_executor_create(&bounded);
			assert(observer.executor != NULL);
		}
		assert(fine_tune_search(&bounded, manual, count, &result, &observer) == 0);
		genghost_state_executor_destroy(observer.executor);
		assert(result.evaluations == bounded.evaluations);
		assert(result.race.finished && result.race.total_frames < baseline.total_frames);
		assert(observer.saves > 1U && result.length == count);
		assert(!result.policy_evaluations && !result.guided_evaluations &&
			   !result.state_section_evaluations && !result.state_macro_evaluations);
		printf(
			"Gear-only strategy %d improves %u to %u frames with an unchanged driving timeline.\n",
			strategy, baseline.total_frames, result.race.total_frames);
	}
}

static int fine_tune_cancel_save(const struct GENGHOST_SEARCH_RESULT *result, void *context)
{
	const struct GENGHOST_RESULT *original = context;
	struct GENGHOST_RESULT replayed;
	assert(result->seed_finished && result->seed_total_frames == original->total_frames);
	assert(result->race.finished && result->race.total_frames == original->total_frames &&
		   result->race.finish_frame == original->finish_frame &&
		   result->race.penalty_frames == original->penalty_frames);
	assert(genghost_sim_evaluate(result->inputs, result->length, &replayed) == 0);
	assert(memcmp(&result->race, &replayed, sizeof(replayed)) == 0);
	return 0;
}

static void test_fine_tune_parallel_cancel(const struct GENGHOST_OPTIONS *options,
										   const unsigned char *recording, size_t size,
										   const struct GAMEINFO *config)
{
	struct GENGHOST_OPTIONS bounded = *options;
	bounded.fine_tune = 1;
	bounded.strategy = GENGHOST_STRATEGY_LEGACY;
	bounded.evaluations = 129U;
	bounded.seconds = 0.0;
	bounded.seed = 1U;
	struct GENGHOST_RESULT original;
	assert(genghost_sim_evaluate(recording + REPLAY_INPUT_OFFSET, config->game_recordedframes,
								 &original) == 0 &&
		   original.finished);
	struct GENGHOST_SEARCH_RESULT result = {0};
	struct GENGHOST_PARALLEL_REPORT report = {0};
	char car[5] = {0};
	memcpy(car, config->game_playercarid, 4U);
	/* Cancellation before startup must still retain the supplied finished lap.
	 * A nonexistent executable makes any attempted child launch fail, so a
	 * successful result with zero worker records proves the launch was skipped. */
	sig_atomic_t previous_interrupt = genghost_interrupted;
	genghost_interrupted = 1;
	int status =
		genghost_parallel_search("./must-not-launch-fine-tune-worker", "fine-tune-cancel-test.RPL",
								 ".", recording, size, car, config->game_playertransmission, 4U,
								 &bounded, &result, &report, fine_tune_cancel_save, &original);
	genghost_interrupted = previous_interrupt;
	assert(status == 0 && result.race.finished);
	assert(result.seed_finished && result.seed_total_frames == original.total_frames);
	assert(result.race.total_frames == original.total_frames &&
		   result.race.finish_frame == original.finish_frame &&
		   result.race.penalty_frames == original.penalty_frames);
	assert(result.length >= original.finish_frame &&
		   memcmp(result.inputs, recording + REPLAY_INPUT_OFFSET, original.finish_frame) == 0);
	assert(report.cores_requested == 4U && report.cores_used == 0U &&
		   report.winner_worker == UINT_MAX);
	struct GENGHOST_WORKER_STATS empty = {0};
	for (unsigned int n = 0U; n < GENGHOST_MAX_CORES; ++n) {
		assert(memcmp(&report.workers[n], &empty, sizeof(empty)) == 0);
	}
	fine_tune_cancel_save(&result, &original);
	puts("Pre-start parallel cancellation retains the verified fine-tune seed without launching "
		 "any workers.");
}

legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[])
{
	assert(argc >= 3);
	unsigned char recording[REPLAY_INPUT_OFFSET + GENGHOST_MAX_FRAMES + 1U];
	FILE *source = fopen((const char *)argv[1], "rb");
	assert(source != NULL);
	size_t size = fread(recording, 1, sizeof(recording), source);
	assert(!ferror(source) && fclose(source) == 0);
	assert(size >= REPLAY_INPUT_OFFSET);
	struct GAMEINFO config;
	replay_gameinfo_decode(&config, recording);
	assert(config.game_opponenttype == 0);
	assert(size == REPLAY_INPUT_OFFSET + config.game_recordedframes);
	unsigned int prefix = (unsigned int)strtoul((const char *)argv[2], NULL, 10);
	assert(prefix > 0U && prefix < config.game_recordedframes);
	assert(genghost_sim_init(recording + REPLAY_GAMEINFO_SIZE,
							 (const char *)config.game_playercarid, config.game_playertransmission,
							 argc, (char **)argv) == 0);
	struct GENGHOST_OPTIONS options = {0};
	options.powergear = options.collision = options.left_bias = options.dual_way_switching = 1;
	int expect_chicane = 0, expect_powergear = 0, expect_landing = 0, recovery = 0;
	int slalom = 0, fine_tune = 0;
	for (int i = 3; i < argc; ++i) {
		const char *flag = (const char *)argv[i];
		if (strcmp(flag, "--test-fine-tune") == 0) {
			fine_tune = 1;
		} else if (strcmp(flag, "--expect-slalom-center") == 0) {
			slalom = 1;
		} else if (strcmp(flag, "--expect-slalom-jump") == 0) {
			slalom = 2;
		} else if (strcmp(flag, "--expect-chicane") == 0) {
			expect_chicane = 1;
		} else if (strcmp(flag, "--expect-powergear") == 0) {
			expect_powergear = 1;
		} else if (strcmp(flag, "--expect-landing") == 0) {
			expect_landing = 1;
		} else if (strcmp(flag, "--search-recovery") == 0) {
			recovery = 1;
		} else if (strcmp(flag, "--pg:off") == 0) {
			options.powergear = 0;
		} else if (strcmp(flag, "--lc:off") == 0) {
			options.collision = 0;
		} else if (strcmp(flag, "--lcb:off") == 0) {
			options.left_bias = 0;
		} else if (strcmp(flag, "--dws:off") == 0) {
			options.dual_way_switching = 0;
		} else if (strcmp(flag, "--owoot") == 0) {
			options.owoot = 1;
		}
	}
	if (fine_tune) {
		test_shift_inputs();
		test_fine_tune(&options, recording + REPLAY_INPUT_OFFSET, config.game_recordedframes);
		test_fine_tune_parallel_cancel(&options, recording, size, &config);
		test_gears_only_search(&options, recording + REPLAY_INPUT_OFFSET,
							   config.game_recordedframes);
		return 0;
	}
	if (recovery) {
		test_failed_pilot_recovery(&options);
		return 0;
	}
	test_slalom_proposal_geometry();
	if (slalom) {
		test_slalom_continuation(&options, recording + REPLAY_INPUT_OFFSET, prefix, slalom == 2);
		return 0;
	}
	test_fast_grass();
	unsigned int chicane = test_maneuver_rules(recording + REPLAY_INPUT_OFFSET, prefix);
	assert(!expect_chicane || chicane > 0U);
	assert(!expect_landing || state.playerstate.car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE);
	test_executor(&options, recording + REPLAY_INPUT_OFFSET, prefix);
	test_guided_executor(&options, recording + REPLAY_INPUT_OFFSET, config.game_recordedframes,
						 prefix);
	if (expect_powergear) {
		assert(options.powergear && !options.owoot);
		test_powergear(recording + REPLAY_INPUT_OFFSET, config.game_recordedframes);
	}
	return 0;
}
