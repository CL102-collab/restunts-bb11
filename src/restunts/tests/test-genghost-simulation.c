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

#undef memcpy
#undef strlen
#undef printf

/* The legacy include path shadows the host math header. */
extern double sqrt(double);
extern double fabs(double);

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
