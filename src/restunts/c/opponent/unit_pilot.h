#ifndef RESTUNTS_UNIT_PILOT_H
#define RESTUNTS_UNIT_PILOT_H

#include "unit_planner.h"

#define UNIT_PILOT_CONTINUATION_TICKS 128U

struct CARSTATE;
struct UNIT_PILOT_PULSE_DIAGNOSTIC;
struct CSRPL_OPPONENT_COMMAND;
struct UNIT_PILOT_STATS {
	legacy_u32 native_ticks, trials, accepted, failed_trials, last_failure_frame;
	legacy_u32 matched_commands, rejected_matches, first_mismatch_frame;
	legacy_u16 first_mismatch_offset, last_failure_barrel_progress;
	legacy_u8 mismatch_actual, mismatch_expected, last_failure_exit_variant;
	legacy_u8 last_failure_ramp_approach, last_failure_secondary_ramp_approach;
	legacy_s16 last_piece, last_failure, last_failure_secondary_piece;
	legacy_f64 last_failure_secondary_speed, last_failure_barrel_offset;
	legacy_f64 last_failure_exit_fraction, last_failure_exit_speed;
	legacy_u8 active, completed_lap, exhausted;
};

/* Main-thread native previews. Every chunk restores physics, clocks, replay
 * and presentation before returning to the progress-screen event loop. */
legacy_int unit_pilot_begin(struct UNIT_PLANNER *source);
void unit_pilot_work(legacy_u32 maximum_ticks);
/* Borrowed frozen seed remains valid until stop; adopt before first live plan. */
struct UNIT_PLANNER *unit_pilot_finish(void);
void unit_pilot_stop(void);
/* Continue a native preview only behind exact cached controls. Startup work
 * is armed by finish; a live recheck is armed by its first exact handoff.
 * The caller pauses its pure planner worker for at most two 64-tick/4ms
 * chunks per fresh cached frame, with an 8ms guard between chunks. */
legacy_int unit_pilot_continuation_pending(void);
legacy_int unit_pilot_continue_work(legacy_u32 maximum_ticks);
void unit_pilot_continuation_cancel(void);
void unit_pilot_stats(struct UNIT_PILOT_STATS *stats);
legacy_int unit_pilot_last_pulse(struct UNIT_PILOT_PULSE_DIAGNOSTIC *diagnostic);
legacy_int unit_pilot_expected_entry(legacy_s16 piece, struct CARSTATE *entry, legacy_f64 *speed);

legacy_int unit_pilot_last_failure_entry(struct CARSTATE *car);
legacy_int unit_pilot_failure_entry_car(legacy_u32 after, struct CARSTATE *car);
legacy_int unit_pilot_failure_car(legacy_u32 before, struct CARSTATE *car);
legacy_int unit_pilot_last_failure(struct CARSTATE *car, legacy_f64 *speed, legacy_s16 *piece);

/* Exact native-state handoff; controller still checks the human before use. */
legacy_int unit_pilot_command(const struct CARSTATE *car, legacy_u32 frame,
							  struct CSRPL_OPPONENT_COMMAND *command);
legacy_int unit_pilot_future_car(legacy_u32 ahead, struct CARSTATE *car);
legacy_int unit_pilot_next_stunt(const struct CARSTATE *car, legacy_s16 *piece,
								 legacy_f64 *distance);
/* Additional live stopping runway; native capture/group boundaries stay unchanged. */
legacy_int unit_pilot_next_stunt_with_lead(const struct CARSTATE *car, legacy_s16 *piece,
										   legacy_f64 *distance, legacy_f64 lead);
/* Caller pauses the live worker and waits for a grounded stop before starting.
 * Work shares the bounded startup chunk API. Status is -1 failed, 0 pending,
 * or 1 verified at its fixed future handoff frame. Missing that frame requires
 * a new native wait forecast; cached commands never translate or move the car. */
legacy_int unit_pilot_recheck_begin(struct UNIT_PLANNER *source, legacy_u32 frame,
									legacy_s16 piece);
legacy_int unit_pilot_recheck_status(legacy_u32 frame);
void unit_pilot_recheck_cancel(void);

#endif
