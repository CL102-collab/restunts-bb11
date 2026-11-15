#include "unit_pilot.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include <math.h>
#include "unit_pilot_internal.h"
#include "unit_opponent.h"
#include "../math/math.h"
#include "../externs.h"

#define UNIT_PILOT_RAMP_WINDOWS 3U
#define UNIT_PILOT_BARREL_LINES 7U
#define UNIT_PILOT_BARREL_BRAKE_FIRST 2U
#define UNIT_PILOT_BARREL_PHASES 5U
#define UNIT_PILOT_BARREL_RATIOS 3U
#define UNIT_PILOT_BARREL_COMBO_PHASES 2U
#define UNIT_PILOT_BARREL_COMBO_LINES 4U
#define UNIT_PILOT_BARREL_COMBO_FIRST (UNIT_PILOT_BARREL_BRAKE_FIRST + UNIT_PILOT_BARREL_PHASES)
#define UNIT_PILOT_BARREL_BRAKE_REST_FIRST                                                         \
	(UNIT_PILOT_BARREL_COMBO_FIRST + UNIT_PILOT_BARREL_COMBO_PHASES * UNIT_PILOT_BARREL_COMBO_LINES)
#define UNIT_PILOT_BARREL_TRIM_FIRST                                                               \
	(UNIT_PILOT_BARREL_BRAKE_REST_FIRST +                                                          \
	 UNIT_PILOT_BARREL_PHASES * (UNIT_PILOT_BARREL_RATIOS - 1U))
#define UNIT_PILOT_BARREL_VARIANTS (UNIT_PILOT_BARREL_TRIM_FIRST + UNIT_PILOT_BARREL_LINES - 1U)
#define UNIT_PILOT_PROFILE_EPSILON 0.000001
#define UNIT_PILOT_REFINEMENT_COUNT 8U
#define UNIT_PILOT_NEAR_ROTATION (ANGLE_FULL_TURN - ANGLE_FULL_TURN / 16)
#define UNIT_PILOT_CLOSE_ROTATION (ANGLE_FULL_TURN - ANGLE_FULL_TURN / 32)
#define UNIT_PILOT_LOCAL_COUNT 6U
#define UNIT_PILOT_LOCAL_TRIM_FIRST (UNIT_PILOT_CANDIDATES + UNIT_PILOT_REFINEMENT_COUNT)
#define UNIT_PILOT_LOCAL_PHASE_FIRST (UNIT_PILOT_LOCAL_TRIM_FIRST + UNIT_PILOT_LOCAL_COUNT)
#define UNIT_PILOT_LOCAL_COMBO_FIRST (UNIT_PILOT_LOCAL_PHASE_FIRST + UNIT_PILOT_LOCAL_COUNT)
#define UNIT_PILOT_LOCAL_COMBO_COUNT 4U
#define UNIT_PILOT_LOCAL_TRIM_STEP (1.0 / 16.0)
#define UNIT_PILOT_LOCAL_PHASE_STEP (1.0 / 64.0)

static const legacy_f64 unit_pilot_barrel_offsets[UNIT_PILOT_BARREL_LINES] = {
	0.0, -0.25, 0.25, -0.5, 0.5, -0.75, 0.75};

static const legacy_f64 unit_pilot_speed_offsets[UNIT_PILOT_CANDIDATES] = {
	0.0,   -0.01, 0.01,	 -0.02, 0.02,  -0.04, 0.04,	 -0.08, 0.08,  -0.16, 0.16,	 -0.24, 0.24,
	-0.32, 0.32,  -0.40, 0.40,	-0.50, 0.50,  -0.60, 0.60,	-0.70, 0.70,  -0.80, 0.80};

static legacy_u8 unit_pilot_ramp_window(legacy_u32 point, legacy_u8 variant)
{
	static const legacy_u8 windows[UNIT_PILOT_RAMP_WINDOWS] = {2, 3, 4};
	const struct UNIT_ROUTE *route = unit_pilot_state->route;
	if (point >= route->count) {
		return 0;
	}
	const struct UNIT_ROUTE_POINT *entry = &route->points[point];
	if (entry->stunt != UNIT_STUNT_RAMP) {
		return 0;
	}
	legacy_u8 known = windows[0];
	if (route->verified_ramp_approaches && entry->route_index >= 0 &&
		(legacy_u32)entry->route_index < route->verified_count) {
		legacy_u8 saved = route->verified_ramp_approaches[entry->route_index];
		if (saved >= windows[0] && saved <= windows[UNIT_PILOT_RAMP_WINDOWS - 1U]) {
			known = saved;
		}
	}
	return !variant ? known : (windows[variant] == known ? windows[0] : windows[variant]);
}

static void unit_pilot_apply_ramp_windows(legacy_s16 piece)
{
	unit_opponent_pilot_ramp_override(ROUTE_INDEX_NONE, 0);
	if (piece < 0) {
		return;
	}
	if (unit_pilot_state->ramp_approach) {
		unit_opponent_pilot_ramp_override(piece, unit_pilot_state->ramp_approach);
	}
	if (unit_pilot_state->secondary_piece >= 0 && unit_pilot_state->secondary_ramp_approach) {
		unit_opponent_pilot_ramp_override(unit_pilot_state->secondary_piece,
										  unit_pilot_state->secondary_ramp_approach);
	}
}

static struct UNIT_EXIT_PROFILE unit_pilot_barrel_profile(legacy_u8 variant, legacy_f64 *offset)
{
	static const legacy_f64 phases[UNIT_PILOT_BARREL_PHASES] = {0.90625, 0.921875, 0.9375, 0.875,
																0.75};
	static const legacy_f64 ratios[UNIT_PILOT_BARREL_RATIOS] = {0.85, 0.90, 0.95};
	static const legacy_f64 trims[UNIT_PILOT_BARREL_COMBO_LINES] = {-0.25, 0.25, -0.75, 0.75};
	struct UNIT_EXIT_PROFILE profile = {0};
	const struct UNIT_ROUTE *route = unit_pilot_state->route;
	legacy_s16 piece = route->points[unit_pilot_state->first].route_index;
	*offset = 0.0;
	if (!variant && piece >= 0 && (legacy_u32)piece < route->verified_count) {
		if (route->verified_barrel_offsets) {
			*offset = route->verified_barrel_offsets[piece];
		}
		if (route->verified_exits && route->verified_speeds[piece] > 0.0) {
			profile = route->verified_exits[piece];
			profile.speed *= unit_pilot_state->speed / route->verified_speeds[piece];
		}
	} else if (variant && variant < UNIT_PILOT_BARREL_BRAKE_FIRST) {
		*offset = unit_pilot_barrel_offsets[variant - 1U];
	} else if (variant >= UNIT_PILOT_BARREL_BRAKE_FIRST &&
			   variant < UNIT_PILOT_BARREL_COMBO_FIRST) {
		profile.fraction = phases[variant - UNIT_PILOT_BARREL_BRAKE_FIRST];
		profile.speed = unit_pilot_state->speed * ratios[0];
	} else if (variant >= UNIT_PILOT_BARREL_COMBO_FIRST &&
			   variant < UNIT_PILOT_BARREL_BRAKE_REST_FIRST) {
		legacy_u8 index = variant - UNIT_PILOT_BARREL_COMBO_FIRST;
		profile.fraction = phases[index % UNIT_PILOT_BARREL_COMBO_PHASES];
		profile.speed = unit_pilot_state->speed * ratios[0];
		*offset = trims[index / UNIT_PILOT_BARREL_COMBO_PHASES];
	} else if (variant >= UNIT_PILOT_BARREL_BRAKE_REST_FIRST &&
			   variant < UNIT_PILOT_BARREL_TRIM_FIRST) {
		legacy_u8 index = variant - UNIT_PILOT_BARREL_BRAKE_REST_FIRST;
		profile.fraction = phases[index % UNIT_PILOT_BARREL_PHASES];
		profile.speed = unit_pilot_state->speed * ratios[1U + index / UNIT_PILOT_BARREL_PHASES];
	} else if (variant >= UNIT_PILOT_BARREL_TRIM_FIRST) {
		*offset = unit_pilot_barrel_offsets[variant - UNIT_PILOT_BARREL_TRIM_FIRST + 1U];
	}
	return profile;
}

static struct UNIT_EXIT_PROFILE unit_pilot_current_barrel_profile(legacy_f64 *offset)
{
	static const legacy_f64 trims[UNIT_PILOT_LOCAL_COUNT] = {-1.0 / 16.0, 1.0 / 16.0, -1.0 / 8.0,
															 1.0 / 8.0,	  -1.0 / 4.0, 1.0 / 4.0};
	static const legacy_f64 phases[UNIT_PILOT_LOCAL_COUNT] = {
		-1.0 / 256.0, 1.0 / 256.0, -1.0 / 128.0, 1.0 / 128.0, -1.0 / 64.0, 1.0 / 64.0};
	struct UNIT_EXIT_PROFILE profile =
		unit_pilot_barrel_profile(unit_pilot_state->barrel_variant, offset);
	legacy_u32 candidate = unit_pilot_state->candidate;
	legacy_f64 phase_delta = 0.0;
	if (candidate >= UNIT_PILOT_LOCAL_TRIM_FIRST && candidate < UNIT_PILOT_LOCAL_PHASE_FIRST) {
		*offset += trims[candidate - UNIT_PILOT_LOCAL_TRIM_FIRST];
	} else if (candidate >= UNIT_PILOT_LOCAL_PHASE_FIRST &&
			   candidate < UNIT_PILOT_LOCAL_COMBO_FIRST) {
		phase_delta = phases[candidate - UNIT_PILOT_LOCAL_PHASE_FIRST];
	} else if (candidate >= UNIT_PILOT_LOCAL_COMBO_FIRST) {
		/* The independent probes already test center and axis neighbors of
		 * this 3x3 neighborhood; only its four coupled corners remain. */
		legacy_u32 index = candidate - UNIT_PILOT_LOCAL_COMBO_FIRST;
		*offset += index % 2U ? UNIT_PILOT_LOCAL_TRIM_STEP : -UNIT_PILOT_LOCAL_TRIM_STEP;
		phase_delta = index / 2U ? UNIT_PILOT_LOCAL_PHASE_STEP : -UNIT_PILOT_LOCAL_PHASE_STEP;
	}
	*offset = unit_bounded(*offset, -1.0, 1.0);
	if (profile.fraction > 0.0 && phase_delta != 0.0) {
		profile.fraction = unit_bounded(profile.fraction + phase_delta, UNIT_PILOT_PROFILE_EPSILON,
										1.0 - UNIT_PILOT_PROFILE_EPSILON);
	}
	return profile;
}

legacy_u32 unit_pilot_candidate_limit(void)
{
	if (!unit_pilot_state->trial || unit_pilot_state->first >= unit_pilot_state->route->count ||
		unit_pilot_state->route->points[unit_pilot_state->first].stunt != UNIT_STUNT_BARREL ||
		unit_pilot_state->barrel_best_progress < UNIT_PILOT_NEAR_ROTATION) {
		return UNIT_PILOT_CANDIDATES;
	}
	if (unit_pilot_state->barrel_best_progress < UNIT_PILOT_CLOSE_ROTATION) {
		return UNIT_PILOT_LOCAL_TRIM_FIRST;
	}
	legacy_f64 offset;
	struct UNIT_EXIT_PROFILE profile =
		unit_pilot_barrel_profile(unit_pilot_state->barrel_variant, &offset);
	return profile.fraction > 0.0 ? UNIT_PILOT_LOCAL_COMBO_FIRST + UNIT_PILOT_LOCAL_COMBO_COUNT
								  : UNIT_PILOT_LOCAL_PHASE_FIRST;
}

legacy_int unit_pilot_next_barrel(void)
{
	legacy_f64 known_offset;
	struct UNIT_EXIT_PROFILE known = unit_pilot_barrel_profile(0, &known_offset);
	for (legacy_u8 variant = unit_pilot_state->barrel_variant + 1U;
		 variant < UNIT_PILOT_BARREL_VARIANTS; variant++) {
		legacy_f64 offset;
		struct UNIT_EXIT_PROFILE profile = unit_pilot_barrel_profile(variant, &offset);
		if (offset == known_offset && profile.fraction == known.fraction &&
			fabs(profile.speed - known.speed) <= UNIT_PILOT_PROFILE_EPSILON) {
			continue;
		}
		unit_pilot_state->barrel_variant = variant;
		unit_pilot_state->barrel_best_progress = 0;
		unit_pilot_state->barrel_best_speed = 0.0;
		return 1;
	}
	return 0;
}

struct UNIT_EXIT_PROFILE unit_pilot_exit_profile(void)
{
	if (unit_pilot_pulses_active(unit_pilot_state->pulses)) {
		struct UNIT_EXIT_PROFILE pulse = {unit_pilot_state->pulse_profile.exit_fraction,
										  unit_pilot_state->pulse_profile.exit_speed};
		return pulse;
	}
	struct UNIT_EXIT_PROFILE profile = {0};
	if (unit_pilot_state->route->points[unit_pilot_state->first].stunt == UNIT_STUNT_BARREL) {
		legacy_f64 offset;
		return unit_pilot_current_barrel_profile(&offset);
	}
	if (unit_pilot_state->exit_variant) {
		legacy_u8 phase = (unit_pilot_state->exit_variant - 1U) % UNIT_PILOT_EXIT_PHASES + 1U;
		legacy_u8 level = (unit_pilot_state->exit_variant - 1U) / UNIT_PILOT_EXIT_PHASES + 1U;
		profile.fraction = phase / (UNIT_PILOT_EXIT_PHASES + 1.0);
		profile.speed =
			unit_pilot_state->speed + (unit_pilot_state->exit_speed - unit_pilot_state->speed) *
										  level / UNIT_PILOT_EXIT_LEVELS;
	}
	return profile;
}

void unit_pilot_restore_overrides(void)
{
	unit_opponent_pilot_input_override(0, 0);
	struct UNIT_EXIT_PROFILE exit = {0};
	legacy_s16 piece = ROUTE_INDEX_NONE;
	if (unit_pilot_state->trial) {
		piece = unit_pilot_state->stats.last_piece;
		exit = unit_pilot_exit_profile();
	}
	unit_opponent_pilot_exit_override(piece, exit.fraction, exit.speed);
	unit_opponent_pilot_barrel_override(piece, unit_pilot_state->barrel_offset);
	unit_pilot_apply_ramp_windows(piece);
	unit_opponent_pilot_secondary_override(
		unit_pilot_state->trial ? unit_pilot_state->secondary_piece : ROUTE_INDEX_NONE,
		unit_pilot_state->trial ? unit_pilot_state->secondary_speed : 0.0);
}

void unit_pilot_start_trial(void)
{
	/* Alternate lower/higher launch speeds around the physical guide. Native
	 * contact and penalty checks decide whether a whole approach is usable. */
	static const legacy_f64 refinement[UNIT_PILOT_REFINEMENT_COUNT] = {-0.005, 0.005, -0.01, 0.01,
																	   -0.02,  0.02,  -0.04, 0.04};
	legacy_f64 adjustment = 0.0;
	if (unit_pilot_state->candidate < UNIT_PILOT_CANDIDATES) {
		adjustment = unit_pilot_speed_offsets[unit_pilot_state->candidate];
	} else if (unit_pilot_state->candidate < UNIT_PILOT_LOCAL_TRIM_FIRST) {
		adjustment = refinement[unit_pilot_state->candidate - UNIT_PILOT_CANDIDATES];
	}
	legacy_s16 piece = unit_pilot_state->route->points[unit_pilot_state->first].route_index;
	legacy_f64 baseline = unit_pilot_state->route->points[unit_pilot_state->first].speed_limit;
	/* A stale handoff can require another actual-state check after waiting.
	 * Revalidate the last successful speed first instead of repeating search. */
	if (piece >= 0 && (legacy_u32)piece < unit_pilot_state->route->verified_count &&
		unit_pilot_state->route->verified_speeds[piece] > 0.0) {
		baseline = unit_pilot_state->route->verified_speeds[piece];
	}
	if (unit_pilot_state->candidate >= UNIT_PILOT_CANDIDATES) {
		baseline = unit_pilot_state->barrel_best_speed;
	}
	unit_pilot_state->speed =
		unit_bounded(baseline * (1.0 + adjustment),
					 fmin(UNIT_PILOT_MINIMUM_SPEED, unit_pilot_state->route->limits.max_speed),
					 unit_pilot_state->route->limits.max_speed);
	if (unit_pilot_pulses_active(unit_pilot_state->pulses)) {
		unit_pilot_state->speed = unit_pilot_state->pulse_profile.speed;
	}
	unit_pilot_state->stats.last_piece = piece;
	unit_pilot_state->ramp_approach =
		unit_pilot_ramp_window(unit_pilot_state->first, unit_pilot_state->ramp_variant);
	unit_pilot_state->secondary_ramp_approach = unit_pilot_ramp_window(
		unit_pilot_state->secondary_first, unit_pilot_state->secondary_ramp_variant);
	struct UNIT_EXIT_PROFILE exit = unit_pilot_exit_profile();
	unit_opponent_pilot_exit_override(piece, exit.fraction, exit.speed);
	legacy_s16 secondary = unit_pilot_state->secondary_piece;
	unit_pilot_state->secondary_speed = 0.0;
	if (secondary >= 0 && (legacy_u32)secondary < unit_pilot_state->route->verified_count &&
		unit_pilot_state->secondary_candidate) {
		legacy_f64 baseline = unit_pilot_state->route->verified_speeds[secondary];
		if (baseline <= 0.0) {
			baseline =
				unit_pilot_state->route->points[unit_pilot_state->secondary_first].speed_limit;
		}
		unit_pilot_state->secondary_speed = unit_bounded(
			baseline * (1.0 + unit_pilot_speed_offsets[unit_pilot_state->secondary_candidate]),
			fmin(UNIT_PILOT_MINIMUM_SPEED, unit_pilot_state->route->limits.max_speed),
			unit_pilot_state->route->limits.max_speed);
	}
	if (unit_pilot_pulses_active(unit_pilot_state->pulses)) {
		unit_pilot_state->secondary_speed = unit_pilot_state->pulse_profile.secondary_speed;
		unit_pilot_state->secondary_ramp_approach =
			unit_pilot_state->pulse_profile.secondary_ramp_approach;
	}
	unit_pilot_apply_ramp_windows(piece);
	unit_opponent_pilot_secondary_override(secondary, unit_pilot_state->secondary_speed);
	if (unit_pilot_pulses_active(unit_pilot_state->pulses)) {
		unit_pilot_state->barrel_offset = unit_pilot_state->pulse_profile.barrel_offset;
	} else {
		(void)unit_pilot_current_barrel_profile(&unit_pilot_state->barrel_offset);
	}
	unit_opponent_pilot_barrel_override(piece, unit_pilot_state->barrel_offset);
	unit_pilot_state->stats.trials++;
	unit_pilot_state->stable_ticks = 0;
	unit_pilot_state->barrel_trial_progress = 0;
	unit_pilot_state->has_entry = 0;
	unit_pilot_state->has_secondary_entry = 0;
	unit_pilot_state->trial = 1;
	struct UNIT_PILOT_PULSE_PROFILE profile = {unit_pilot_state->speed,
											   unit_pilot_state->barrel_offset,
											   exit.fraction,
											   exit.speed,
											   unit_pilot_state->secondary_speed,
											   unit_pilot_state->secondary_ramp_approach};
	unit_pilot_pulses_start_trial(unit_pilot_state->pulses, unit_pilot_state->frame, &profile);
}

void unit_pilot_fail_trial(legacy_s16 failure)
{
	if (!unit_pilot_state->trial) {
		unit_pilot_state->stats.last_piece = state.opponentstate.car_route_index;
		unit_pilot_state->speed = 0.0;
	}
	unit_pilot_capture_failure(failure);
	if (unit_pilot_state->trial) {
		unit_pilot_pulses_finish_trial(unit_pilot_state->pulses, &state.opponentstate,
									   unit_pilot_state->frame, 0);
	}
	legacy_int pulse = unit_pilot_pulses_active(unit_pilot_state->pulses);
	if (!pulse) {
		if (unit_pilot_state->trial && unit_pilot_state->candidate < UNIT_PILOT_CANDIDATES &&
			unit_pilot_state->barrel_trial_progress > unit_pilot_state->barrel_best_progress) {
			unit_pilot_state->barrel_best_progress = unit_pilot_state->barrel_trial_progress;
			unit_pilot_state->barrel_best_speed = unit_pilot_state->speed;
		}
		unit_pilot_state->barrel_reached |= unit_pilot_state->trial && unit_pilot_state->has_entry;
		legacy_int exit_retry = unit_pilot_state->trial &&
								unit_pilot_state->first_last < unit_pilot_state->last &&
								unit_pilot_state->route->points[unit_pilot_state->first].stunt ==
									UNIT_STUNT_PROTECTED &&
								unit_pilot_state->exit_speed > unit_pilot_state->speed &&
								unit_pilot_state->exit_variant < UNIT_PILOT_EXIT_VARIANTS &&
								(unit_pilot_state->exit_variant ||
								 unit_route_cursor(unit_pilot_state->route, &state.opponentstate) >
									 unit_pilot_state->first_last);
		legacy_int secondary_reached =
			unit_pilot_state->trial && unit_pilot_state->secondary_piece >= 0 &&
			(unit_pilot_state->secondary_candidate || unit_pilot_state->secondary_ramp_variant ||
			 unit_route_cursor(unit_pilot_state->route, &state.opponentstate) >=
				 unit_pilot_state->secondary_first);
		/* First retain the established slow-entry/fast-exit search. A later
		 * ramp target cannot fix insufficient acceleration out of the primary. */
		legacy_int prefer_exit = exit_retry && unit_pilot_state->secondary_ramp_approach;
		legacy_int secondary_retry =
			secondary_reached && !prefer_exit &&
			unit_pilot_state->secondary_candidate + 1U < UNIT_PILOT_CANDIDATES;
		legacy_int window_retry =
			secondary_reached && !prefer_exit && unit_pilot_state->secondary_ramp_approach &&
			unit_pilot_state->secondary_ramp_variant + 1U < UNIT_PILOT_RAMP_WINDOWS;
		if (secondary_retry) {
			unit_pilot_state->secondary_candidate++;
		} else if (window_retry) {
			unit_pilot_state->secondary_ramp_variant++;
			unit_pilot_state->secondary_candidate = 0;
		} else if (exit_retry) {
			unit_pilot_state->exit_variant++;
			unit_pilot_state->secondary_candidate = unit_pilot_state->secondary_ramp_variant = 0;
		} else {
			unit_pilot_state->exit_variant = 0;
			unit_pilot_state->secondary_candidate = unit_pilot_state->secondary_ramp_variant = 0;
			unit_pilot_state->candidate++;
		}
		if (unit_pilot_state->trial &&
			unit_pilot_state->candidate >= unit_pilot_candidate_limit() &&
			unit_pilot_state->route->points[unit_pilot_state->first].stunt == UNIT_STUNT_RAMP &&
			unit_pilot_state->ramp_variant + 1U < UNIT_PILOT_RAMP_WINDOWS) {
			unit_pilot_state->ramp_variant++;
			unit_pilot_state->candidate = 0;
		}
		/* Preserve every baseline speed trial. Later launch profiles adjust
	 * contact dynamics only after reaching the barrel proved possible. */
		if (unit_pilot_state->trial &&
			unit_pilot_state->candidate >= unit_pilot_candidate_limit() &&
			unit_pilot_state->barrel_reached &&
			unit_pilot_state->route->points[unit_pilot_state->first].stunt == UNIT_STUNT_BARREL &&
			unit_pilot_next_barrel()) {
			unit_pilot_state->candidate = 0;
		}
	}
	if (!unit_pilot_state->trial || pulse ||
		unit_pilot_state->candidate >= unit_pilot_candidate_limit()) {
		if (!unit_pilot_state->trial ||
			!unit_pilot_pulses_next(unit_pilot_state->pulses, &unit_pilot_state->pulse_profile)) {
			unit_pilot_exhaust(failure);
			return;
		}
	}
	if (!unit_planner_restore_continuation(unit_pilot_state->planner,
										   unit_pilot_state->approach_planner)) {
		unit_pilot_exhaust(UNIT_PILOT_FAILURE_MEMORY);
		return;
	}
	extended_physics_restore(&unit_pilot_state->approach);
	unit_pilot_state->frame = unit_pilot_state->approach_frame;
	unit_pilot_state->step_count = unit_pilot_state->approach_steps;
	unit_pilot_start_trial();
}

#endif
