#ifndef RESTUNTS_GENGHOST_STATE_SEARCH_H
#define RESTUNTS_GENGHOST_STATE_SEARCH_H

#include "search.h"
#include "maneuvers.h"

enum GENGHOST_STATE_TASK_KIND {
	GENGHOST_STATE_TASK_MANEUVER = 0,
	GENGHOST_STATE_TASK_POLICY,
	GENGHOST_STATE_TASK_INPUT_INTERVAL,
	GENGHOST_STATE_TASK_REFERENCE
};

/* Requests contain portable replay inputs, never native pointers. The executor
 * reconstructs a full simulator checkpoint in its own process. */
struct GENGHOST_STATE_TASK {
	legacy_u64 id;
	legacy_u32 route_index, route_variant;
	legacy_uint prefix_length, action_index, variation;
	/* 0: maneuver; 1: replay the prefix and continue a complete driving policy;
	 * 2: change a short input interval and replay the remainder exactly;
	 * 3: retime the supplied native reference after a maneuver. */
	legacy_uint kind, first, width, replacement;
	legacy_uint reference_length, reference_first;
	legacy_u8 prefix[GENGHOST_MAX_FRAMES];
	legacy_u8 reference[GENGHOST_MAX_FRAMES];
};

struct GENGHOST_STATE_FEATURES {
	legacy_s32 x, y, z;
	legacy_int heading, pitch, roll, speed, gear, airborne, powergear;
	legacy_int route, confirmed_route, previous_route, route_status, confirmations;
	legacy_uint frames, penalty_frames;
	legacy_f64 progress, off_route, route_length;
};

struct GENGHOST_STATE_REPLY {
	legacy_u64 id, simulated_frames;
	legacy_uint length, available, action_count, action_kind, action_ticks;
	legacy_uint powergear_ticks, switch_count, tile_active, stalled_frames, transition_stop;
	struct GENGHOST_RESULT race;
	struct GENGHOST_DRIVER driver;
	struct GENGHOST_STATE_FEATURES features;
	legacy_u8 inputs[GENGHOST_MAX_FRAMES];
};

struct GENGHOST_STATE_EXECUTOR;
struct GENGHOST_STATE_EXECUTOR *genghost_state_executor_create(const struct GENGHOST_OPTIONS *);
void genghost_state_executor_destroy(struct GENGHOST_STATE_EXECUTOR *);
legacy_int genghost_state_execute(const void *request, void *reply, void *executor);

typedef legacy_int (*genghost_state_batch_function)(void *context,
													const struct GENGHOST_STATE_TASK *tasks,
													struct GENGHOST_STATE_REPLY *replies,
													size_t count);

/* The batch callback may use independent native worker processes. Results are
 * consumed in task order, so scheduling/core count does not change fixed-budget
 * search. NULL uses the same executor locally. */
legacy_int genghost_state_search(const struct GENGHOST_OPTIONS *options,
								 const legacy_u8 *seed_inputs, legacy_uint seed_length,
								 struct GENGHOST_SEARCH_RESULT *result, genghost_save_function save,
								 void *save_context, genghost_state_batch_function batch,
								 void *batch_context);

#endif
