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
#include "../c/trackdata_layout.h"
#include "../c/track_objects.h"
#include "../c/physics_internal.h"

#undef memcpy
#undef strlen
#undef printf

/* The legacy include path shadows the host math header. */
extern double sqrt(double);
extern double fabs(double);

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
	unsigned int cursor = 0;
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

static double region_cost(double px, double pz, double ax, double az, double bx, double bz)
{
	return sqrt((px - ax) * (px - ax) + (pz - az) * (pz - az)) +
		   sqrt((px - bx) * (px - bx) + (pz - bz) * (pz - bz));
}

static int region_contains(const struct GENGHOST_REGION *region, double x, double z)
{
	return x >= region->min_x && x <= region->max_x && z >= region->min_z && z <= region->max_z;
}

static double edge_cost(const struct GENGHOST_REGION *region, unsigned int edge, double t,
						double ax, double az, double bx, double bz)
{
	double x = edge < 2U ? (edge ? region->max_x : region->min_x)
						 : region->min_x + t * (region->max_x - region->min_x);
	double z = edge < 2U ? region->min_z + t * (region->max_z - region->min_z)
						 : (edge == 3U ? region->max_z : region->min_z);
	return region_cost(x, z, ax, az, bx, bz);
}

/* Independent numerical oracle: sample every edge densely, then refine the
 * best sample's neighbourhood by convex one-dimensional minimization. This
 * deliberately does not reproduce the planner's clipping or weighted formula. */
static double region_oracle(const struct GENGHOST_REGION *region, double ax, double az, double bx,
							double bz)
{
	if (region_contains(region, ax, az) || region_contains(region, bx, bz)) {
		return region_cost(ax, az, ax, az, bx, bz);
	}
	const unsigned int samples = 512U;
	double best = DBL_MAX;
	for (unsigned int edge = 0U; edge < 4U; ++edge) {
		unsigned int index = 0U;
		double edge_best = DBL_MAX;
		for (unsigned int sample = 0U; sample <= samples; ++sample) {
			double cost = edge_cost(region, edge, (double)sample / samples, ax, az, bx, bz);
			if (cost < edge_best) {
				edge_best = cost;
				index = sample;
			}
		}
		double low = index ? (double)(index - 1U) / samples : 0.0;
		double high = index < samples ? (double)(index + 1U) / samples : 1.0;
		for (unsigned int iteration = 0U; iteration < 64U; ++iteration) {
			double a = low + (high - low) / 3.0, b = high - (high - low) / 3.0;
			if (edge_cost(region, edge, a, ax, az, bx, bz) <
				edge_cost(region, edge, b, ax, az, bx, bz)) {
				high = b;
			} else {
				low = a;
			}
		}
		double refined = edge_cost(region, edge, (low + high) * 0.5, ax, az, bx, bz);
		if (refined < edge_best) {
			edge_best = refined;
		}
		if (edge_best < best) {
			best = edge_best;
		}
	}
	return best;
}

static void region_expect(const struct GENGHOST_REGION *region, double ax, double az, double bx,
						  double bz, double expected_x, double expected_z)
{
	double x, z;
	genghost_region_crossing(region, ax, az, bx, bz, &x, &z);
	assert(fabs(x - expected_x) < 1e-9 && fabs(z - expected_z) < 1e-9);
	assert(x >= region->min_x - 1e-9 && x <= region->max_x + 1e-9);
	assert(z >= region->min_z - 1e-9 && z <= region->max_z + 1e-9);
}

static double region_random(uint32_t *seed)
{
	*seed = *seed * UINT32_C(1664525) + UINT32_C(1013904223);
	return (double)*seed / 4294967296.0;
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

	uint32_t seed = UINT32_C(0x6a09e667);
	for (unsigned int trial = 0U; trial < 1000U; ++trial) {
		struct GENGHOST_REGION region;
		region.min_x = (region_random(&seed) - 0.5) * 61440.0;
		region.min_z = (region_random(&seed) - 0.5) * 61440.0;
		region.max_x = region.min_x + (trial % 17U ? 2048.0 * region_random(&seed) : 0.0);
		region.max_z = region.min_z + (trial % 19U ? 2048.0 * region_random(&seed) : 0.0);
		double ax = region.min_x + (region_random(&seed) - 0.5) * 8192.0;
		double az = region.min_z + (region_random(&seed) - 0.5) * 8192.0;
		double bx = region.max_x + (region_random(&seed) - 0.5) * 8192.0;
		double bz = region.max_z + (region_random(&seed) - 0.5) * 8192.0;
		if (trial % 23U == 0U) {
			bx = ax;
		}
		if (trial % 29U == 0U) {
			bz = az;
		}
		double x, z;
		genghost_region_crossing(&region, ax, az, bx, bz, &x, &z);
		assert(x >= region.min_x - 1e-8 && x <= region.max_x + 1e-8);
		assert(z >= region.min_z - 1e-8 && z <= region.max_z + 1e-8);
		double expected = region_oracle(&region, ax, az, bx, bz);
		double actual = region_cost(x, z, ax, az, bx, bz);
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
static void test_controller_snapshot(const unsigned char *inputs, unsigned int count,
									 unsigned int prefix, int model)
{
	assert(prefix < count);
	for (unsigned int frame = 0; frame < prefix; ++frame) {
		assert(genghost_sim_step(inputs[frame]) == 0);
	}
	struct GENGHOST_ROUTES set;
	char error[256];
	assert(genghost_routes_build(&set, error, sizeof(error)) == 0);
	const struct GENGHOST_ROUTE *selected = NULL;
	size_t selected_index = 0U;
	double nearest = DBL_MAX;
	for (size_t r = 0U; r < set.count; ++r) {
		const struct GENGHOST_ROUTE *route = &set.routes[r];
		for (size_t i = 0U; i < route->point_count; ++i) {
			const struct GENGHOST_WAYPOINT *point = &route->points[i];
			if (point->physical_model != model) {
				continue;
			}
			double x = point->x - state.playerstate.car_position.lx / 64.0;
			double z = point->z - state.playerstate.car_position.lz / 64.0;
			double distance = x * x + z * z;
			if (distance < nearest) {
				nearest = distance;
				selected = route;
				selected_index = i;
			}
		}
	}
	assert(selected != NULL && nearest < 1024.0 * 1024.0);
	int piece = selected->points[selected_index].piece;
	size_t first = selected_index, end = selected_index + 1U;
	while (first && selected->points[first - 1U].piece == piece) {
		--first;
	}
	while (end < selected->point_count && selected->points[end].piece == piece) {
		++end;
	}
	struct GENGHOST_ROUTE view = {0};
	view.pieces = &piece;
	view.piece_count = 1U;
	view.point_count = end - first;
	assert(view.point_count >= 2U);
	view.points = calloc(view.point_count, sizeof(*view.points));
	assert(view.points != NULL);
	for (size_t i = 0U; i < view.point_count; ++i) {
		view.points[i] = selected->points[first + i];
		view.points[i].distance -= selected->points[first].distance;
		view.points[i].piece_index = 0U;
	}
	view.length = view.points[view.point_count - 1U].distance;
	struct GENGHOST_SNAPSHOT saved, after;
	genghost_sim_capture(&saved);
	struct GENGHOST_POLICY policy;
	genghost_policy_default(&policy);
	for (size_t i = 0U; i < GENGHOST_POLICY_SEGMENTS; ++i) {
		policy.segment[i].stunt_mode = 1U;
	}
	for (int collision = 0; collision <= 1; ++collision) {
		struct GENGHOST_DRIVER driver;
		genghost_driver_reset(&driver);
		(void)genghost_driver_step(&view, &policy, &driver, 1, collision, 0);
		int allowed = collision || model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A ||
					  model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B;
		assert(driver.tactic_ticks == (unsigned int)allowed);
		genghost_sim_capture(&after);
		assert(memcmp(&saved, &after, sizeof(saved)) == 0);
	}
	/* OWOOT must make every stunt gene inert, including launch and median
     * choices. Compare both inputs and optimizer state, not just a flag. */
	struct GENGHOST_DRIVER reference;
	genghost_driver_reset(&reference);
	genghost_policy_default(&policy);
	unsigned char expected = genghost_driver_step(&view, &policy, &reference, 1, 1, 1);
	for (unsigned int mode = 1U; mode <= 4U; ++mode) {
		for (size_t i = 0U; i < GENGHOST_POLICY_SEGMENTS; ++i) {
			policy.segment[i].stunt_mode = mode;
		}
		for (int collision = 0; collision <= 1; ++collision) {
			struct GENGHOST_DRIVER driver;
			genghost_driver_reset(&driver);
			assert(genghost_driver_step(&view, &policy, &driver, 1, collision, 1) == expected);
			assert(memcmp(&driver, &reference, sizeof(driver)) == 0);
			genghost_sim_capture(&after);
			assert(memcmp(&saved, &after, sizeof(saved)) == 0);
		}
	}
	/* Test the gate against native collisions with the same winning input
	 * suffix and the original car body. Both orientations of the loop and
	 * slalom passage crash with collision fixes; the UD core stays clear.
	 * Only this diagnostic comparison changes a rule at a checkpoint. */
	const unsigned int suffix_length = 32U;
	assert(prefix + suffix_length < count);
	struct GAMESTATE reference_states[32];
	for (unsigned int frame = 0U; frame < suffix_length; ++frame) {
		assert(genghost_sim_step(inputs[prefix + frame]) == 0);
		reference_states[frame] = state;
	}
	genghost_sim_restore(&saved);
	legacy_s8 *fixed[] = {(legacy_s8 *)"controller-check", (legacy_s8 *)"--lc:off"};
	configure_legacy_collision(2, fixed);
	int fixed_crash = 0;
	int clear_core =
		model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A || model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B;
	for (unsigned int frame = 0U; frame < suffix_length; ++frame) {
		int event = genghost_sim_step(inputs[prefix + frame]);
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

/* A selector filter such as word%4 accidentally removes one residue class
 * when the number of source/target pairs is divisible by four. Separate recipe
 * choice from the full selector and verify the resulting Cartesian indices. */
static void test_dualway_selectors(void)
{
	const unsigned int counts[] = {4U, 8U, 12U};
	for (size_t test = 0U; test < sizeof(counts) / sizeof(counts[0]); ++test) {
		unsigned int total = counts[test];
		for (uint32_t gate = 4U; gate <= 12U; gate += 4U) {
			unsigned int visits[12] = {0};
			for (uint64_t word = 3U; word < 3U + 32U * total; ++word) {
				uint64_t selected = genghost_dualway_selector(gate, word);
				assert(selected >= 3U);
				++visits[(selected - 3U) % total];
			}
			for (unsigned int ordinal = 0U; ordinal < total; ++ordinal) {
				assert(visits[ordinal] == 32U);
			}
		}
	}
	unsigned int recipes[3] = {0};
	for (uint64_t word = 0U; word < 12U; ++word) {
		uint64_t selected = genghost_dualway_selector(0U, word);
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
static void place_on_piece(int piece)
{
	state.playerstate.car_position.lx =
		((legacy_s32)(unsigned char)track_route_columns[piece] * 1024 + 512) * 64;
	state.playerstate.car_position.lz =
		((legacy_s32)(29 - (unsigned char)track_route_rows[piece]) * 1024 + 512) * 64;
}

static int test_dualway_routes(void)
{
	if (track_pieces_counter != 128 || track_primary_route_links[9] != 10 ||
		track_alternate_route_links[9] != 59 || track_primary_route_links[127] != 58 ||
		track_route_columns[0] != 7 || track_route_rows[0] != 24) {
		return 0;
	}
	struct GENGHOST_ROUTES set;
	char error[256];
	assert(genghost_routes_build(&set, error, sizeof(error)) == 0);
	assert(set.count == 2U && set.dualway_pairs == 6164U);
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
		double finish_distance = genghost_route_piece_distance(base, finish);
		assert(finish_distance > base->length * 0.9);
		assert(finish_distance == base->points[first_finish].distance);
		for (uint64_t ordinal = 0U; ordinal < 3082U; ++ordinal) {
			struct GENGHOST_POLICY policy;
			genghost_policy_default(&policy);
			for (size_t i = 0; i < GENGHOST_POLICY_SEGMENTS; ++i) {
				policy.segment[i].dualway_choice = ordinal + 3U;
			}
			struct GENGHOST_ROUTE route;
			assert(genghost_route_policy(&set, route_index, &policy, &route, error,
										 sizeof(error)) == 1);
			assert(route.switch_count == 1U);
			int source = route_index ? 60 + (int)(ordinal / 46U) : 11 + (int)(ordinal / 67U);
			int target = route_index ? 11 + (int)(ordinal % 46U) : 60 + (int)(ordinal % 67U);
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
	 * The same destination costs 65 skipped elements before a private A tile
	 * is confirmed, and receives the legacy wraparound marker afterward. */
	for (int confirmed = 9; confirmed <= 11; confirmed += 2) {
		genghost_sim_reset();
		state.game_startcol = state.game_startcol2 = -1;
		state.game_startrow = state.game_startrow2 = -1;
		place_on_piece(125);
		legacy_s16 current = (legacy_s16)confirmed, skipped = 0;
		assert(detect_penalty(&current, &skipped) != 0);
		assert(current == 125);
		assert(skipped == (confirmed == 9 ? 65 : -1));
	}
	genghost_routes_free(&set);
	genghost_sim_reset();
	puts("All 6164 dual-way pairs, protected tile touches, and native penalty distinction passed.");
	return 1;
}

/* Resource-backed integration check. Run from a game-data directory:
 * test-genghost-simulation source.rpl normalized.rpl [physics switches]
 * repldump normalized.rpl 1 [the same physics switches]
 * Compare normalized.rpl.states with normalized.BNI: every serialized state
 * must match. Source 10 Hz inputs are deliberately normalized to 20 Hz so this
 * also exercises extraction without importing the source recording's timing. */
legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[])
{
	if (argc == 2 && strcmp((const char *)argv[1], "--geometry-only") == 0) {
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
	unsigned char recording[REPLAY_INPUT_OFFSET + GENGHOST_MAX_FRAMES + 1];
	FILE *source = fopen((const char *)argv[1], "rb");
	assert(source != NULL);
	size_t size = fread(recording, 1, sizeof(recording), source);
	assert(!ferror(source) && fclose(source) == 0);
	assert(size >= REPLAY_INPUT_OFFSET);
	struct GAMEINFO config;
	replay_gameinfo_decode(&config, recording);
	assert(config.game_opponenttype == 0);
	assert(config.game_recordedframes <= GENGHOST_MAX_FRAMES);
	assert(size == REPLAY_INPUT_OFFSET + config.game_recordedframes);
	unsigned int count = config.game_recordedframes;
	const unsigned char *inputs = recording + REPLAY_INPUT_OFFSET;
	assert(genghost_sim_init(recording + REPLAY_GAMEINFO_SIZE,
							 (const char *)config.game_playercarid, config.game_playertransmission,
							 argc, (char **)argv) == 0);

	if (argc == 5 && strcmp((const char *)argv[2], "--controller-check") == 0) {
		test_controller_snapshot(inputs, count,
								 (unsigned int)strtoul((const char *)argv[3], NULL, 10),
								 (int)strtol((const char *)argv[4], NULL, 10));
		return 0;
	}
	if (argc >= 3 && strcmp((const char *)argv[2], "--dualway-check") == 0) {
		assert(test_dualway_routes());
		return 0;
	}

	/* A discarded branch changes collision caches, RNG (on impact), retained
	 * legacy stack words and penalty state. Restoring must undo all of it. */
	struct GENGHOST_SNAPSHOT checkpoint, expected, actual;
	for (unsigned int frame = 0; frame < 137; frame++) {
		assert(genghost_sim_step(frame < count ? inputs[frame] : INPUT_ACCELERATE_FLAG) >= 0);
	}
	genghost_sim_capture(&checkpoint);
	for (unsigned int frame = 137; frame < 377; frame++) {
		assert(genghost_sim_step(frame < count ? inputs[frame] : INPUT_ACCELERATE_FLAG) >= 0);
	}
	genghost_sim_capture(&expected);
	genghost_sim_restore(&checkpoint);
	for (unsigned int frame = 0; frame < 911; frame++) {
		assert(genghost_sim_step(INPUT_ACCELERATE_FLAG | INPUT_STEER_LEFT_FLAG) >= 0);
	}
	genghost_sim_restore(&checkpoint);
	for (unsigned int frame = 137; frame < 377; frame++) {
		assert(genghost_sim_step(frame < count ? inputs[frame] : INPUT_ACCELERATE_FLAG) >= 0);
	}
	genghost_sim_capture(&actual);
	assert(memcmp(&expected, &actual, sizeof(expected)) == 0);

	char track_name[REPLAY_TRACK_NAME_SIZE + 1];
	memcpy(track_name, config.game_trackname, REPLAY_TRACK_NAME_SIZE);
	track_name[REPLAY_TRACK_NAME_SIZE] = 0;
	assert(genghost_sim_write_replay((const char *)argv[2], track_name, inputs, count) == 0);
	char *state_path = malloc(strlen((const char *)argv[2]) + sizeof(".states"));
	assert(state_path != NULL);
	sprintf(state_path, "%s.states", (const char *)argv[2]);
	FILE *output = fopen(state_path, "wb");
	assert(output != NULL);
	free(state_path);
	struct GENGHOST_GUIDE_POINT *guide = calloc(count, sizeof(*guide));
	assert(guide != NULL);
	unsigned char serialized[GAMESTATE_SERIALIZED_SIZE];
	LEGACY_WRITE_U16_LE(serialized, count);
	assert(fwrite(serialized, 1, 2, output) == 2);
	genghost_sim_reset();
	for (unsigned int frame = 0; frame < count; frame++) {
		genghost_guide_capture(&guide[frame], inputs[frame]);
		assert(genghost_sim_step(inputs[frame]) >= 0);
		assert(gamestate_serialize(serialized, &state) == sizeof(serialized));
		assert(fwrite(serialized, 1, sizeof(serialized), output) == sizeof(serialized));
	}
	assert(fclose(output) == 0);
	genghost_sim_capture(&expected);
	genghost_sim_reset();
	for (unsigned int frame = 0; frame < count; frame++) {
		assert(genghost_sim_step(inputs[frame]) >= 0);
	}
	genghost_sim_capture(&actual);
	assert(memcmp(&expected, &actual, sizeof(expected)) == 0);
	/* Following an unmodified demonstrated trajectory must preserve every input
	 * and the complete final simulator state, including through jumps and slides. */
	genghost_sim_reset();
	unsigned int cursor = 0;
	for (unsigned int frame = 0; frame < count; ++frame) {
		unsigned char guided = genghost_guide_step(guide, count, &cursor, 0.5);
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
