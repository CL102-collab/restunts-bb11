#ifndef RESTUNTS_GENGHOST_STATE_SEARCH_H
#define RESTUNTS_GENGHOST_STATE_SEARCH_H

#include "search.h"
#include "maneuvers.h"

/* Requests contain portable replay inputs, never native pointers. The executor
 * reconstructs a full simulator checkpoint in its own process. */
struct GENGHOST_STATE_TASK {
	uint64_t id;
	uint32_t route_index, route_variant;
	unsigned int prefix_length, action_index, variation;
	/* 0: maneuver; 1: replay the prefix and continue a complete driving policy;
	 * 2: change a short input interval and replay the remainder exactly;
	 * 3: retime the supplied native reference after a maneuver. */
	unsigned int kind, first, width, replacement;
	unsigned int reference_length, reference_first;
	unsigned char prefix[GENGHOST_MAX_FRAMES];
	unsigned char reference[GENGHOST_MAX_FRAMES];
};

struct GENGHOST_STATE_FEATURES {
	int32_t x, y, z;
	int heading, pitch, roll, speed, gear, airborne, powergear;
	int route, confirmed_route, previous_route, route_status, confirmations;
	unsigned int frames, penalty_frames;
	double progress, off_route, route_length;
};

struct GENGHOST_STATE_REPLY {
	uint64_t id, simulated_frames;
	unsigned int length, available, action_count, action_kind, action_ticks;
	unsigned int powergear_ticks, switch_count, tile_active, stalled_frames, transition_stop;
	struct GENGHOST_RESULT race;
	struct GENGHOST_DRIVER driver;
	struct GENGHOST_STATE_FEATURES features;
	unsigned char inputs[GENGHOST_MAX_FRAMES];
};

struct GENGHOST_STATE_EXECUTOR;
struct GENGHOST_STATE_EXECUTOR *genghost_state_executor_create(const struct GENGHOST_OPTIONS *);
void genghost_state_executor_destroy(struct GENGHOST_STATE_EXECUTOR *);
int genghost_state_execute(const void *request, void *reply, void *executor);

typedef int (*genghost_state_batch_function)(void *context, const struct GENGHOST_STATE_TASK *tasks,
											 struct GENGHOST_STATE_REPLY *replies, size_t count);

/* The batch callback may use independent native worker processes. Results are
 * consumed in task order, so scheduling/core count does not change fixed-budget
 * search. NULL uses the same executor locally. */
int genghost_state_search(const struct GENGHOST_OPTIONS *options, const unsigned char *seed_inputs,
						  unsigned int seed_length, struct GENGHOST_SEARCH_RESULT *result,
						  genghost_save_function save, void *save_context,
						  genghost_state_batch_function batch, void *batch_context);

#endif
