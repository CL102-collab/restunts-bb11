#ifndef RESTUNTS_GENGHOST_SEARCH_H
#define RESTUNTS_GENGHOST_SEARCH_H

#include <signal.h>
#include <stdint.h>
#include "simulation.h"
#include "routes.h"

struct GENGHOST_OPTIONS {
	double seconds;
	uint64_t evaluations;
	uint32_t seed;
	int powergear, collision, left_bias, owoot;
};

struct GENGHOST_SEARCH_RESULT {
	unsigned char inputs[GENGHOST_MAX_FRAMES];
	unsigned int length;
	struct GENGHOST_RESULT race;
	uint64_t evaluations, simulated_frames;
	uint64_t policy_evaluations, input_evaluations, guided_evaluations, guided_improvements;
	uint64_t tile_evaluations, tile_improvements;
	uint64_t tactic_evaluations, tactic_improvements;
	uint64_t dualway_pairs, dualway_evaluations, dualway_improvements;
	unsigned int seed_total_frames;
	int seed_finished;
	size_t routes, routes_attempted, routes_finished;
	double elapsed_seconds;
};

extern volatile sig_atomic_t genghost_interrupted;
typedef int (*genghost_save_function)(const struct GENGHOST_SEARCH_RESULT *, void *);

/* Every candidate uses original player input and the real simulator. The save
 * callback is invoked only after a fresh, complete, successful resimulation. */
int genghost_search(const struct GENGHOST_OPTIONS *options, const unsigned char *seed_inputs,
					unsigned int seed_length, struct GENGHOST_SEARCH_RESULT *result,
					genghost_save_function save, void *save_context);

#endif
