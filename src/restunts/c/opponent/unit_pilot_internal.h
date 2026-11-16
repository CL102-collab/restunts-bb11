#ifndef RESTUNTS_UNIT_PILOT_INTERNAL_H
#define RESTUNTS_UNIT_PILOT_INTERNAL_H

#include "unit_pilot.h"
#include "unit_opponent_internal.h"
#include "unit_pilot_pulses.h"
#include "../extended/extended_physics_snapshot.h"

#define UNIT_PILOT_FAILURE_MEMORY (-4)
#define UNIT_PILOT_MINIMUM_SPEED 20.0
#define UNIT_PILOT_CANDIDATES 25U
#define UNIT_PILOT_EXIT_PHASES 3U
#define UNIT_PILOT_EXIT_LEVELS 4U
#define UNIT_PILOT_EXIT_VARIANTS (UNIT_PILOT_EXIT_PHASES * UNIT_PILOT_EXIT_LEVELS)

#define UNIT_PILOT_MATCH_WINDOW 64U
#define UNIT_PILOT_FAILURE_HISTORY 128U

/* Native snapshots omit desktop state. Suppress irreversible audio and result
 * capture before each preview, and restore this separate UI envelope too. */
struct UNIT_PILOT_ENVELOPE {
	legacy_s32 distance;
	legacy_u16 frame, recorded_frames;
	legacy_s16 impact, jumps, opponent_end, opponent_finish, player_end;
	legacy_s16 penalty, top_speed, total_finish;
	legacy_s8 replay, recording_flags;
	legacy_u8 replay_mode;
#ifndef RESTUNTS_HEADLESS
	struct EXTENDED_REPLAY_CLOCKS race_stats;
#endif
};

struct UNIT_PILOT_ENTRY {
	struct CARSTATE car;
	legacy_f64 speed;
	legacy_u8 valid;
};

struct UNIT_PILOT_STEP {
	struct CARSTATE car;
	struct CSRPL_OPPONENT_COMMAND command;
};

struct UNIT_PILOT_BANK {
	legacy_f64 approach_distance;
	legacy_u32 search, first, last, receiver_first, receiver_last, approach;
	legacy_u32 hold_steps, deferred_steps, observed_steps, stable_ticks;
	legacy_u8 pending;
};

struct UNIT_PILOT {
	struct EXTENDED_PHYSICS_SNAPSHOT live, run, approach;
	struct UNIT_PILOT_ENVELOPE envelope;
	struct UNIT_PILOT_STATS stats;
	struct UNIT_PILOT_ENTRY *entries;
	struct UNIT_PILOT_STEP *steps;
	struct UNIT_PILOT_BANK bank, approach_bank;
	legacy_u32 step_count, step_capacity, committed_steps, approach_steps, next_step;
	legacy_u32 learned_replay_count;
	/* Only fresh preparation previews can produce a portable complete lap. */
	legacy_u8 learning_full_start;
	legacy_u32 last_live_frame, run_start_frame, penalty_baseline;
	legacy_u32 wait_remaining, handoff_frame, resume_candidate;
	legacy_s8 recheck_status;
	legacy_u8 recheck, recheck_rolling, continuation, continuation_matched, continuation_has_frame;
	legacy_u32 continuation_frame;
	const struct UNIT_ROUTE *route;
	struct UNIT_PLANNER *seed, *planner, *approach_planner;
	struct UNIT_PILOT_PULSES *pulses;
	struct UNIT_PILOT_PULSE_PROFILE pulse_profile;
	legacy_u8 resume_pulses;
	struct CARSTATE entry, secondary_entry, failed_entry, failed_car;
	struct CARSTATE failure_history[UNIT_PILOT_FAILURE_HISTORY];
	struct CARSTATE failure_entry_history[UNIT_PILOT_FAILURE_HISTORY];
	legacy_u32 failure_history_count, failure_entry_count, entry_step;
	legacy_f64 failed_speed;
	legacy_s16 failed_piece;
	legacy_u32 frame, approach_frame, first, last, approach_cursor;
	legacy_u32 next_search, candidate, stable_ticks, first_last, receiver_last;
	legacy_u8 exit_variant, resume_exit_variant, resume_pending;
	legacy_u8 secondary_candidate, resume_secondary_candidate;
	legacy_u8 ramp_variant, ramp_approach, resume_ramp_variant;
	legacy_u8 secondary_ramp_variant, secondary_ramp_approach, resume_secondary_ramp_variant;
	legacy_u8 barrel_variant, resume_barrel_variant, barrel_reached, resume_barrel_reached;
	legacy_f64 barrel_offset, barrel_best_speed, resume_barrel_best_speed;
	legacy_u16 barrel_trial_progress, barrel_best_progress, resume_barrel_best_progress;
	legacy_s16 secondary_piece;
	legacy_u32 secondary_first;
	legacy_f64 secondary_speed;
	legacy_f64 exit_speed;
	legacy_f64 speed;
	legacy_u8 trial, has_entry, has_secondary_entry, failed_has_entry;
};

extern struct UNIT_PILOT *unit_pilot_state;
legacy_int unit_pilot_grounded(void);
void unit_pilot_bank_reset(legacy_u32 cursor);
void unit_pilot_bank_before_step(legacy_u32 cursor);
void unit_pilot_bank_after_step(legacy_u32 cursor);
void unit_pilot_commit_steps(legacy_u32 steps);
void unit_pilot_bank_finish(void);
void unit_pilot_exhaust(legacy_s16 failure);
void unit_pilot_fail_trial(legacy_s16 failure);
void unit_pilot_capture_failure(legacy_s16 failure);
struct UNIT_EXIT_PROFILE unit_pilot_exit_profile(void);
void unit_pilot_start_trial(void);
void unit_pilot_restore_overrides(void);
legacy_int unit_pilot_next_barrel(void);
legacy_u32 unit_pilot_candidate_limit(void);

#endif
