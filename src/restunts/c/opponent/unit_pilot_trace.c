#include "unit_pilot.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include <math.h>
#include <string.h>
#include "unit_pilot_internal.h"
#include "unit_opponent.h"
#include "../track/trackdata_layout.h"
#include "../externs.h"

void unit_pilot_capture_failure(legacy_s16 failure)
{
	unit_pilot_state->stats.last_failure_frame = unit_pilot_state->frame;
	unit_pilot_state->stats.last_failure_exit_variant = unit_pilot_state->exit_variant;
	unit_pilot_state->stats.last_failure_ramp_approach = unit_pilot_state->ramp_approach;
	unit_pilot_state->stats.last_failure_secondary_ramp_approach =
		unit_pilot_state->secondary_ramp_approach;
	unit_pilot_state->stats.last_failure_secondary_piece = unit_pilot_state->secondary_piece;
	unit_pilot_state->stats.last_failure_secondary_speed = unit_pilot_state->secondary_speed;
	unit_pilot_state->stats.last_failure_barrel_offset = unit_pilot_state->barrel_offset;
	unit_pilot_state->stats.last_failure_barrel_progress = unit_pilot_state->barrel_trial_progress;
	struct UNIT_EXIT_PROFILE exit = {0};
	if (unit_pilot_state->trial) {
		exit = unit_pilot_exit_profile();
	}
	unit_pilot_state->stats.last_failure_exit_fraction = exit.fraction;
	unit_pilot_state->stats.last_failure_exit_speed = exit.speed;
	unit_pilot_state->failure_history_count =
		unit_pilot_state->step_count < UNIT_PILOT_FAILURE_HISTORY ? unit_pilot_state->step_count
																  : UNIT_PILOT_FAILURE_HISTORY;
	for (legacy_u32 before = 0; before < unit_pilot_state->failure_history_count; before++) {
		unit_pilot_state->failure_history[before] =
			unit_pilot_state->steps[unit_pilot_state->step_count - 1U - before].car;
	}
	unit_pilot_state->failure_entry_count = 0;
	if (unit_pilot_state->trial && unit_pilot_state->has_entry) {
		legacy_u32 count = unit_pilot_state->step_count - unit_pilot_state->entry_step;
		unit_pilot_state->failure_entry_count =
			count < UNIT_PILOT_FAILURE_HISTORY ? count : UNIT_PILOT_FAILURE_HISTORY;
		for (legacy_u32 after = 0; after < unit_pilot_state->failure_entry_count; after++) {
			unit_pilot_state->failure_entry_history[after] =
				unit_pilot_state->steps[unit_pilot_state->entry_step + after].car;
		}
	}
	unit_pilot_state->failed_car = state.opponentstate;
	unit_pilot_state->failed_entry = unit_pilot_state->entry;
	unit_pilot_state->failed_has_entry = unit_pilot_state->trial && unit_pilot_state->has_entry;
	unit_pilot_state->failed_speed = unit_pilot_state->speed;
	unit_pilot_state->failed_piece = unit_pilot_state->stats.last_piece;
	unit_pilot_state->stats.failed_trials++;
	unit_pilot_state->stats.last_failure = failure;
}

legacy_int unit_pilot_last_pulse(struct UNIT_PILOT_PULSE_DIAGNOSTIC *diagnostic)
{
	return unit_pilot_state && unit_pilot_pulses_last_trial(unit_pilot_state->pulses, diagnostic);
}

void unit_pilot_stats(struct UNIT_PILOT_STATS *stats)
{
	if (!stats) {
		return;
	}
	memset(stats, 0, sizeof(*stats));
	if (unit_pilot_state) {
		*stats = unit_pilot_state->stats;
	}
}

legacy_int unit_pilot_expected_entry(legacy_s16 piece, struct CARSTATE *entry, legacy_f64 *speed)
{
	if (!unit_pilot_state || piece < 0 ||
		(legacy_u32)piece >= unit_pilot_state->route->verified_count ||
		!unit_pilot_state->entries[piece].valid) {
		return 0;
	}
	if (entry) {
		*entry = unit_pilot_state->entries[piece].car;
	}
	if (speed) {
		*speed = unit_pilot_state->entries[piece].speed;
	}
	return 1;
}
legacy_int unit_pilot_last_failure_entry(struct CARSTATE *car)
{
	if (!unit_pilot_state || !car || !unit_pilot_state->failed_has_entry) {
		return 0;
	}
	*car = unit_pilot_state->failed_entry;
	return 1;
}

legacy_int unit_pilot_failure_entry_car(legacy_u32 after, struct CARSTATE *car)
{
	if (!unit_pilot_state || !car || after >= unit_pilot_state->failure_entry_count) {
		return 0;
	}
	*car = unit_pilot_state->failure_entry_history[after];
	return 1;
}

legacy_int unit_pilot_failure_car(legacy_u32 before, struct CARSTATE *car)
{
	if (!unit_pilot_state || !car || !unit_pilot_state->stats.failed_trials ||
		before > unit_pilot_state->failure_history_count) {
		return 0;
	}
	*car = before ? unit_pilot_state->failure_history[before - 1U] : unit_pilot_state->failed_car;
	return 1;
}

legacy_int unit_pilot_last_failure(struct CARSTATE *car, legacy_f64 *speed, legacy_s16 *piece)
{
	if (!unit_pilot_state || !unit_pilot_state->stats.failed_trials) {
		return 0;
	}
	if (car) {
		*car = unit_pilot_state->failed_car;
	}
	if (speed) {
		*speed = unit_pilot_state->failed_speed;
	}
	if (piece) {
		*piece = unit_pilot_state->failed_piece;
	}
	return 1;
}
legacy_int unit_pilot_command(const struct CARSTATE *car, legacy_u32 frame,
							  struct CSRPL_OPPONENT_COMMAND *command)
{
	if (!unit_pilot_state || !car || !command || !unit_pilot_state->committed_steps) {
		return 0;
	}
	if (unit_pilot_state->recheck && !unit_pilot_state->next_step &&
		frame != unit_pilot_state->handoff_frame) {
		return 0;
	}
	if (frame < unit_pilot_state->last_live_frame) {
		unit_pilot_state->next_step = 0;
	}
	legacy_u32 end = unit_pilot_state->next_step + UNIT_PILOT_MATCH_WINDOW;
	if (end > unit_pilot_state->committed_steps) {
		end = unit_pilot_state->committed_steps;
	}
	for (legacy_u32 index = unit_pilot_state->next_step; index < end; index++) {
		if (memcmp(car, &unit_pilot_state->steps[index].car, sizeof(*car)) == 0) {
			legacy_int handoff = unit_pilot_state->recheck && !unit_pilot_state->next_step &&
								 unit_pilot_state->recheck_status > 0;
			if (handoff && index != 0) {
				break;
			}
			if (unit_pilot_state->continuation &&
				(index != unit_pilot_state->next_step ||
				 (unit_pilot_state->continuation_has_frame &&
				  frame != unit_pilot_state->continuation_frame + 1U))) {
				break;
			}
			/* Only the exact future-state handoff can extend a live recheck.
			 * Its paused native branch now predicts beyond the accepted exit
			 * while the actual car follows that already committed prefix. */
			if (handoff) {
				unit_pilot_state->recheck = 0;
				unit_pilot_state->continuation =
					!unit_pilot_state->stats.completed_lap && !unit_pilot_state->stats.exhausted;
			}
			if (unit_pilot_state->continuation) {
				unit_pilot_state->continuation_matched = unit_pilot_state->continuation_has_frame =
					1;
				unit_pilot_state->continuation_frame = frame;
			}
			*command = unit_pilot_state->steps[index].command;
			unit_pilot_state->next_step = index + 1U;
			unit_pilot_state->last_live_frame = frame;
			unit_pilot_state->stats.matched_commands++;
			return 1;
		}
	}
	unit_pilot_state->stats.rejected_matches++;
	if (unit_pilot_state->stats.first_mismatch_offset == sizeof(struct CARSTATE) &&
		unit_pilot_state->next_step < unit_pilot_state->committed_steps) {
		const legacy_u8 *actual = (const legacy_u8 *)car;
		const legacy_u8 *expected =
			(const legacy_u8 *)&unit_pilot_state->steps[unit_pilot_state->next_step].car;
		for (legacy_u16 offset = 0; offset < sizeof(*car); offset++) {
			if (actual[offset] != expected[offset]) {
				unit_pilot_state->stats.first_mismatch_frame = frame;
				unit_pilot_state->stats.first_mismatch_offset = offset;
				unit_pilot_state->stats.mismatch_actual = actual[offset];
				unit_pilot_state->stats.mismatch_expected = expected[offset];
				break;
			}
		}
	}
	if (unit_pilot_state->continuation) {
		unit_pilot_continuation_cancel();
	}
	return 0;
}

legacy_int unit_pilot_future_car(legacy_u32 ahead, struct CARSTATE *car)
{
	if (!unit_pilot_state || !car || !unit_pilot_state->next_step ||
		ahead >= unit_pilot_state->committed_steps - (unit_pilot_state->next_step - 1U)) {
		return 0;
	}
	*car = unit_pilot_state->steps[unit_pilot_state->next_step - 1U + ahead].car;
	return 1;
}

legacy_int unit_pilot_next_stunt_with_lead(const struct CARSTATE *car, legacy_s16 *piece,
										   legacy_f64 *distance, legacy_f64 lead)
{
	const struct UNIT_ROUTE *route = unit_opponent_pilot_route();
	if (!route || !car) {
		return 0;
	}
	if (!isfinite(lead) || lead < 0.0) {
		return 0;
	}
	legacy_u32 cursor = unit_route_advance(route, car, unit_route_cursor(route, car));
	legacy_f64 approach = 0.0;
	if (cursor > 0 && cursor < route->count &&
		(route->points[cursor].stunt == UNIT_STUNT_NONE ||
		 route->points[cursor - 1U].route_index != route->points[cursor].route_index)) {
		const struct UNIT_ROUTE_POINT *point = &route->points[cursor];
		legacy_f64 dx = point->x - route->points[cursor - 1U].x;
		legacy_f64 dz = point->z - route->points[cursor - 1U].z;
		legacy_f64 length = hypot(dx, dz);
		if (length > 0.0) {
			approach = fmax(0.0, ((point->x - car->car_position.lx / UNIT_POSITION_SCALE) * dx +
								  (point->z - car->car_position.lz / UNIT_POSITION_SCALE) * dz) /
									 length);
		}
	}
	/* The controller only rechecks the nearby approach. Do not scan an entire
	 * large track when a cached command no longer matches and no stunt is near. */
	for (legacy_u32 index = cursor;
		 index < route->finish_begin &&
		 approach <= UNIT_NATIVE_RAMP_APPROACH_TILES * TRACK_TILE_SIZE + lead;
		 index++) {
		if (route->points[index].stunt != UNIT_STUNT_NONE) {
			if (approach > unit_pilot_approach_distance(route->points[index].stunt) + lead) {
				return 0;
			}
			if (piece) {
				*piece = route->points[index].route_index;
			}
			if (distance) {
				*distance = approach;
			}
			return 1;
		}
		if (index + 1U < route->count) {
			approach += hypot(route->points[index + 1U].x - route->points[index].x,
							  route->points[index + 1U].z - route->points[index].z);
		}
	}
	return 0;
}

legacy_int unit_pilot_next_stunt(const struct CARSTATE *car, legacy_s16 *piece,
								 legacy_f64 *distance)
{
	return unit_pilot_next_stunt_with_lead(car, piece, distance, 0.0);
}

#endif
