#ifndef RESTUNTS_UNIT_OPPONENT_H
#define RESTUNTS_UNIT_OPPONENT_H

#include "../legacy.h"

/* SDL3-only lifecycle. Preparation owns immutable route/car snapshots; the
 * caller owns the ten-second clock and event handling. */
legacy_int unit_opponent_is_selected(void);
legacy_int unit_opponent_prepare_begin(void);
/* Negative worker count verifies the single-threaded platform fallback. */
legacy_int unit_opponent_prepare_begin_with_workers(legacy_s32 requested_workers);
void unit_opponent_prepare_work(void);
/* Returns zero if a complete route could not be prepared; releases partial work. */
legacy_int unit_opponent_prepare_end(void);
void unit_opponent_stop(void);
void unit_opponent_tick(void);

/* Settling precedes the fixed native recheck handoff, in race ticks. */
#define UNIT_OPPONENT_RECHECK_SETTLED_TICKS 4U

struct UNIT_PLANNING_HOLD {
	legacy_u32 route_cursor;
	legacy_s16 piece;
	legacy_u8 active;
};
/* Main-thread diagnostic for the most recently issued live command. Preview,
 * cached driving and replay commands never claim a stopped planning hold. */
void unit_opponent_planning_hold(struct UNIT_PLANNING_HOLD *hold);

/* Diagnostics for persisted planning; playback never updates these counters. */
struct UNIT_LEARNING_STATS {
	legacy_u64 loaded_samples, loaded_native_samples, search_samples, native_samples;
	legacy_u32 best_frames;
	legacy_u8 enabled, loaded, saved;
};
void unit_opponent_learning_stats(struct UNIT_LEARNING_STATS *stats);

struct UNIT_ROUTE;
struct UNIT_PLANNER;
struct UNIT_PLANNER_STATS;
struct CSRPL_OPPONENT_COMMAND;
void unit_opponent_planner_stats(struct UNIT_PLANNER_STATS *stats);
/* Commands actually issued from live worker proposals, excluding preview/cache. */
legacy_u64 unit_opponent_worker_commands(void);
/* Main-thread preview only: caller snapshots all globals and advances clocks
 * and checkpoint observers. This API never records command history. */
const struct UNIT_ROUTE *unit_opponent_pilot_route(void);
legacy_s16 unit_opponent_pilot_step(struct UNIT_PLANNER *planner, legacy_u32 frame,
									legacy_s16 override_piece, legacy_f64 override_speed);
/* Revalidate a stored learning candidate through native physics. The caller
 * owns the isolated preview snapshot and clock/checkpoint advancement. */
legacy_s16 unit_opponent_pilot_replay_step(legacy_u32 frame,
										   const struct CSRPL_OPPONENT_COMMAND *command);
/* Output of the most recent preview step, without recording replay history. */
void unit_opponent_pilot_last_command(struct CSRPL_OPPONENT_COMMAND *command);
/* Forecast the exact pending-recheck brake/steering command without recording. */
legacy_s16 unit_opponent_pilot_wait_step(legacy_u32 frame);
void unit_opponent_pilot_accept(legacy_s16 piece, legacy_f64 speed);
void unit_opponent_pilot_exit_override(legacy_s16 piece, legacy_f64 fraction, legacy_f64 speed);
void unit_opponent_pilot_secondary_override(legacy_s16 piece, legacy_f64 speed);
/* One preview tick; mask uses complete INPUT_STEERING_MASK / INPUT_PEDAL_MASK groups. */
void unit_opponent_pilot_input_override(legacy_u8 mask, legacy_u8 value);
/* Barrel-axis offset as a fraction of its usable native guide width, [-1,1]. */
void unit_opponent_pilot_barrel_override(legacy_s16 piece, legacy_f64 fraction);
void unit_opponent_pilot_accept_barrel(legacy_s16 piece, legacy_f64 fraction);
/* Native-validated RAMP run-up, two through four tiles; clear override with piece -1. */
void unit_opponent_pilot_ramp_override(legacy_s16 piece, legacy_u8 tiles);
void unit_opponent_pilot_accept_ramp(legacy_s16 piece, legacy_u8 tiles);
void unit_opponent_pilot_accept_exit(legacy_s16 piece, legacy_f64 fraction, legacy_f64 speed);

#endif
