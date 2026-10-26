#ifndef RESTUNTS_GENGHOST_SEARCH_H
#define RESTUNTS_GENGHOST_SEARCH_H

#include <signal.h>
#include <stdint.h>
#include "simulation.h"
#include "routes.h"

struct GENGHOST_SEARCH_RESULT;
typedef legacy_int (*genghost_progress_function)(const struct GENGHOST_SEARCH_RESULT *, void *);

enum GENGHOST_STRATEGY { GENGHOST_STRATEGY_LEGACY = 0, GENGHOST_STRATEGY_STATE = 1 };

struct GENGHOST_OPTIONS {
	legacy_f64 seconds;
	legacy_u64 evaluations;
	legacy_u32 seed;
	legacy_int powergear, collision, left_bias, dual_way_switching, owoot, amiga_grass;
	legacy_int chicane_fast_grass;
	/* Optional worker heartbeat/cancellation; never runs concurrent physics. */
	genghost_progress_function progress;
	void *progress_context;
	enum GENGHOST_STRATEGY strategy;
	/* Refine a completed compatible replay; never restart track-only discovery. */
	legacy_int fine_tune;
	/* Restrict fine-tuning to shift buttons; preserve the complete input timeline. */
	legacy_int fine_tune_gears_only;
};

struct GENGHOST_SEARCH_RESULT {
	legacy_u8 inputs[GENGHOST_MAX_FRAMES];
	legacy_uint length;
	struct GENGHOST_RESULT race;
	legacy_u64 evaluations, simulated_frames;
	legacy_u64 policy_evaluations, input_evaluations, guided_evaluations, guided_improvements;
	legacy_u64 tile_evaluations, tile_improvements;
	legacy_u64 tactic_evaluations, tactic_improvements;
	legacy_u64 dualway_pairs, dualway_evaluations, dualway_improvements;
	legacy_u64 state_evaluations, state_frontier_peak, state_duplicates, state_task_hash;
	legacy_u64 state_pg_evaluations, state_pg_states;
	legacy_u64 state_section_evaluations, state_section_starts;
	legacy_u64 state_rescue_starts, state_diversity_preserved, state_transition_stops;
	legacy_u64 state_macro_evaluations, state_input_evaluations, state_chicane_evaluations;
	legacy_uint seed_total_frames;
	legacy_int seed_finished;
	size_t routes, routes_attempted, routes_finished;
	legacy_f64 elapsed_seconds;
};

extern volatile sig_atomic_t genghost_interrupted;
/* The optional path remains owned by the caller. A nonempty regular file
 * requests the same graceful cancellation as SIGINT, preserving saved ghosts. */
void genghost_set_stop_file(const legacy_char *path);
legacy_int genghost_stop_requested(void);
typedef legacy_int (*genghost_save_function)(const struct GENGHOST_SEARCH_RESULT *, void *);

/* Move one existing up/down shift pulse earlier or later, preserving its
 * duration and every other input channel. Proposals cycle through all pulses
 * at offsets of +/-1, 2, 4, 8, 16 and 32 frames. Reject moves outside the lap
 * or across another pulse. first/end delimit the changed half-open interval. */
legacy_int genghost_shift_inputs(legacy_u8 *inputs, legacy_uint length, legacy_u64 proposal,
								 legacy_uint *first, legacy_uint *end);

/* Every candidate uses original player input and the real simulator. The save
 * callback is invoked only after a fresh, complete, successful resimulation. */
legacy_int genghost_search(const struct GENGHOST_OPTIONS *options, const legacy_u8 *seed_inputs,
						   legacy_uint seed_length, struct GENGHOST_SEARCH_RESULT *result,
						   genghost_save_function save, void *save_context);

#endif
