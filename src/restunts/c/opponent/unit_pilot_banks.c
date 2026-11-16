#include "unit_pilot.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include <math.h>
#include <string.h>
#include "unit_pilot_internal.h"
#include "../externs.h"
#include "../track/trackdata_layout.h"
#include "../extended/extended_unit_penalty.h"

#define UNIT_BANK_STABLE_TICKS 4U
#define UNIT_BANK_BRAKING_RESERVE 0.5
#define UNIT_BANK_MINIMUM_BRAKING 1.0

static void unit_pilot_bank_approach(void)
{
	struct UNIT_PILOT_BANK *bank = &unit_pilot_state->bank;
	const struct UNIT_ROUTE *route = unit_pilot_state->route;
	legacy_f64 speed = state.opponentstate.car_actual_speed / UNIT_SPEED_SCALE;
	legacy_f64 target = route->points[bank->first].fallback_speed_limit;
	legacy_f64 braking =
		fmax(route->limits.braking * UNIT_BANK_BRAKING_RESERVE, UNIT_BANK_MINIMUM_BRAKING);
	legacy_f64 required =
		fmax(UNIT_NATIVE_APPROACH_TILES * TRACK_TILE_SIZE,
			 fmax(0.0, speed * speed - target * target) * UNIT_WORLD_PER_MPH / (2.0 * braking) +
				 route->limits.half_length);
	if (required == bank->approach_distance) {
		return;
	}
	bank->approach_distance = required;
	legacy_f64 distance = 0.0;
	bank->approach = bank->first;
	while (bank->approach && distance < required) {
		const struct UNIT_ROUTE_POINT *a = &route->points[bank->approach - 1U];
		const struct UNIT_ROUTE_POINT *b = &route->points[bank->approach];
		distance += hypot(b->x - a->x, b->z - a->z);
		bank->approach--;
	}
}

static legacy_int unit_pilot_bank_find(void)
{
	struct UNIT_PILOT_BANK *bank = &unit_pilot_state->bank;
	const struct UNIT_ROUTE *route = unit_pilot_state->route;
	legacy_u32 first = bank->search;
	while (first < route->finish_begin && !route->points[first].bank_support) {
		first++;
	}
	if (first >= route->finish_begin) {
		bank->search = route->finish_begin;
		bank->first = bank->approach = route->count;
		return 0;
	}
	/* A live recheck may start partway through the same continuous bank. */
	while (first && route->points[first - 1U].bank_support) {
		first--;
	}
	bank->first = bank->last = first;
	while (bank->last + 1U < route->finish_begin && route->points[bank->last + 1U].bank_support) {
		bank->last++;
	}
	bank->search = bank->last + 1U;
	bank->receiver_first = bank->last + 1U;
	/* An adjoining ordered maneuver supplies its own solver and native proof.
	 * Await supported road after it without changing that maneuver's grouping. */
	while (bank->receiver_first < route->count &&
		   route->points[bank->receiver_first].stunt != UNIT_STUNT_NONE &&
		   !route->points[bank->receiver_first].clearance_only) {
		bank->receiver_first++;
	}
	bank->receiver_last = bank->receiver_first;
	while (bank->receiver_last + 1U < route->count &&
		   route->points[bank->receiver_last + 1U].route_index ==
			   route->points[bank->receiver_first].route_index) {
		bank->receiver_last++;
	}
	bank->approach_distance = 0.0;
	unit_pilot_bank_approach();
	return 1;
}

void unit_pilot_bank_reset(legacy_u32 cursor)
{
	struct UNIT_PILOT_BANK *bank = &unit_pilot_state->bank;
	memset(bank, 0, sizeof(*bank));
	bank->search = cursor;
	bank->first = bank->approach = unit_pilot_state->route->count;
	bank->observed_steps = unit_pilot_state->step_count;
	unit_pilot_state->approach_bank = *bank;
}

void unit_pilot_commit_steps(legacy_u32 steps)
{
	struct UNIT_PILOT_BANK *bank = &unit_pilot_state->bank;
	if (steps > unit_pilot_state->step_count) {
		steps = unit_pilot_state->step_count;
	}
	if (bank->pending) {
		if (steps > bank->deferred_steps) {
			bank->deferred_steps = steps;
		}
		if (steps > bank->hold_steps) {
			steps = bank->hold_steps;
		}
	}
	if (steps > unit_pilot_state->committed_steps) {
		unit_pilot_state->committed_steps = steps;
	}
}

void unit_pilot_bank_before_step(legacy_u32 cursor)
{
	struct UNIT_PILOT_BANK *bank = &unit_pilot_state->bank;
	/* Hard trials rewind physics and controls to their captured approach. A
	 * later failed trial must neither retain future support proof nor lose an
	 * older admission cap when its native candidate is retried. */
	if (unit_pilot_state->step_count < bank->observed_steps) {
		*bank = unit_pilot_state->approach_bank;
	}
	bank->observed_steps = unit_pilot_state->step_count;
	if (bank->pending) {
		return;
	}
	if (bank->first == unit_pilot_state->route->count && !unit_pilot_bank_find()) {
		return;
	}
	unit_pilot_bank_approach();
	if (cursor >= bank->approach) {
		bank->pending = 1;
		bank->hold_steps = unit_pilot_state->committed_steps;
		bank->deferred_steps = unit_pilot_state->committed_steps;
		bank->stable_ticks = 0;
	}
}

static legacy_int unit_pilot_bank_receiver(legacy_u32 cursor)
{
	const struct UNIT_PILOT_BANK *bank = &unit_pilot_state->bank;
	const struct UNIT_ROUTE *route = unit_pilot_state->route;
	if (bank->receiver_last >= route->count || cursor < bank->receiver_last ||
		!bank->receiver_last || !unit_pilot_grounded()) {
		return 0;
	}
	const struct UNIT_ROUTE_POINT *a = &route->points[bank->receiver_last - 1U];
	const struct UNIT_ROUTE_POINT *b = &route->points[bank->receiver_last];
	legacy_f64 x = state.opponentstate.car_position.lx / UNIT_POSITION_SCALE;
	legacy_f64 z = state.opponentstate.car_position.lz / UNIT_POSITION_SCALE;
	if ((x - b->x) * (b->x - a->x) + (z - b->z) * (b->z - a->z) < 0.0 ||
		extended_unit_penalty_recovery_required()) {
		return 0;
	}
	const struct EXTENDED_UNIT_PENALTY *observed = &extended_replay_clocks.unit_penalty;
	if (!observed->initialized) {
		return 1;
	}
	for (legacy_u32 point = bank->receiver_first; point <= cursor && point < route->count;
		 point++) {
		if (route->points[point].route_index == observed->route.previous_route) {
			return 1;
		}
	}
	return 0;
}

void unit_pilot_bank_after_step(legacy_u32 cursor)
{
	struct UNIT_PILOT_BANK *bank = &unit_pilot_state->bank;
	bank->observed_steps = unit_pilot_state->step_count;
	if (!bank->pending) {
		return;
	}
	bank->stable_ticks = unit_pilot_bank_receiver(cursor) ? bank->stable_ticks + 1U : 0U;
	if (bank->stable_ticks < UNIT_BANK_STABLE_TICKS) {
		return;
	}
	bank->stable_ticks = 0;
	/* Overlapping admission windows can share the same withheld prefix. This
	 * never combines solver groups or exposes the next unproved bank's entry. */
	if (unit_pilot_bank_find() && cursor >= bank->approach) {
		return;
	}
	bank->pending = 0;
	/* Support reached inside a hard trial is provisional until that entire
	 * trial succeeds. Its retry must retain the older withheld approach. */
	if (!unit_pilot_state->trial) {
		unit_pilot_commit_steps(bank->deferred_steps);
	}
}

void unit_pilot_bank_finish(void)
{
	/* A complete clean native lap already proves every command through the
	 * finish, including legal airborne finishes. Imported laps use this same
	 * whole-lap authority and never expose an unfinished bank prefix. */
	unit_pilot_state->bank.pending = 0;
	unit_pilot_commit_steps(unit_pilot_state->step_count);
}

#endif
