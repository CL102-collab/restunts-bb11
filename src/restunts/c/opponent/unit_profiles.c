#include "unit_profiles.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include <math.h>
#include "unit_opponent.h"
#include "unit_opponent_internal.h"
#include "unit_launch.h"

static legacy_s16 unit_preview_exit_piece = ROUTE_INDEX_NONE;
static struct UNIT_EXIT_PROFILE unit_preview_exit;
static legacy_s16 unit_preview_secondary_piece = ROUTE_INDEX_NONE;
static legacy_f64 unit_preview_secondary_speed;
static legacy_s16 unit_preview_barrel_piece = ROUTE_INDEX_NONE;
static legacy_f64 unit_preview_barrel_offset;
static struct UNIT_RAMP_APPROACH unit_preview_ramps[UNIT_RAMP_APPROACH_OVERRIDES];

void unit_profiles_reset(void)
{
	unit_preview_exit_piece = unit_preview_secondary_piece = unit_preview_barrel_piece =
		ROUTE_INDEX_NONE;
	unit_preview_exit = (struct UNIT_EXIT_PROFILE){0};
	unit_preview_secondary_speed = unit_preview_barrel_offset = 0.0;
	unit_opponent_pilot_ramp_override(ROUTE_INDEX_NONE, 0);
}

void unit_profiles_policy(legacy_int preview, legacy_s16 piece, legacy_f64 speed,
						  struct UNIT_LAUNCH_POLICY *policy)
{
	*policy = (struct UNIT_LAUNCH_POLICY){
		.primary_piece = piece,
		.secondary_piece = preview ? unit_preview_secondary_piece : ROUTE_INDEX_NONE,
		.primary_speed = speed,
		.secondary_speed = preview ? unit_preview_secondary_speed : 0.0,
		.exit = preview && piece == unit_preview_exit_piece ? &unit_preview_exit : NULL,
		.barrel_offset =
			preview && piece == unit_preview_barrel_piece ? &unit_preview_barrel_offset : NULL};
	if (preview) {
		for (legacy_u32 slot = 0; slot < UNIT_RAMP_APPROACH_OVERRIDES; slot++) {
			policy->ramps[slot] = unit_preview_ramps[slot];
		}
	}
}

void unit_opponent_pilot_accept(legacy_s16 piece, legacy_f64 speed)
{
	struct UNIT_ROUTE *route = unit_opponent_pilot_route_mutable();
	if (piece >= 0 && (legacy_u32)piece < route->verified_count && isfinite(speed) && speed > 0.0) {
		route->verified_speeds[piece] = fmin(speed, route->limits.max_speed);
	}
}

void unit_opponent_pilot_secondary_override(legacy_s16 piece, legacy_f64 speed)
{
	struct UNIT_ROUTE *route = unit_opponent_pilot_route_mutable();
	unit_preview_secondary_piece = piece;
	unit_preview_secondary_speed =
		isfinite(speed) ? unit_bounded(speed, 0.0, route->limits.max_speed) : 0.0;
}

void unit_opponent_pilot_exit_override(legacy_s16 piece, legacy_f64 fraction, legacy_f64 speed)
{
	struct UNIT_ROUTE *route = unit_opponent_pilot_route_mutable();
	unit_preview_exit_piece = piece;
	unit_preview_exit.fraction = isfinite(fraction) ? unit_bounded(fraction, 0.0, 1.0) : 0.0;
	unit_preview_exit.speed =
		isfinite(speed) ? unit_bounded(speed, 0.0, route->limits.max_speed) : 0.0;
}

void unit_opponent_pilot_barrel_override(legacy_s16 piece, legacy_f64 fraction)
{
	unit_preview_barrel_piece = piece;
	unit_preview_barrel_offset = isfinite(fraction) ? unit_bounded(fraction, -1.0, 1.0) : 0.0;
}

void unit_opponent_pilot_accept_barrel(legacy_s16 piece, legacy_f64 fraction)
{
	struct UNIT_ROUTE *route = unit_opponent_pilot_route_mutable();
	if (piece >= 0 && (legacy_u32)piece < route->verified_count && route->verified_barrel_offsets) {
		route->verified_barrel_offsets[piece] =
			isfinite(fraction) ? unit_bounded(fraction, -1.0, 1.0) : 0.0;
	}
}

void unit_opponent_pilot_ramp_override(legacy_s16 piece, legacy_u8 tiles)
{
	if (piece < 0) {
		for (legacy_u32 slot = 0; slot < UNIT_RAMP_APPROACH_OVERRIDES; slot++) {
			unit_preview_ramps[slot] = (struct UNIT_RAMP_APPROACH){ROUTE_INDEX_NONE, 0};
		}
		return;
	}
	for (legacy_u32 slot = 0; slot < UNIT_RAMP_APPROACH_OVERRIDES; slot++) {
		if (unit_preview_ramps[slot].piece == piece) {
			unit_preview_ramps[slot].tiles = tiles;
			return;
		}
	}
	for (legacy_u32 slot = 0; slot < UNIT_RAMP_APPROACH_OVERRIDES; slot++) {
		if (!unit_preview_ramps[slot].tiles) {
			unit_preview_ramps[slot] = (struct UNIT_RAMP_APPROACH){piece, tiles};
			return;
		}
	}
}

void unit_opponent_pilot_accept_ramp(legacy_s16 piece, legacy_u8 tiles)
{
	struct UNIT_ROUTE *route = unit_opponent_pilot_route_mutable();
	if (piece >= 0 && (legacy_u32)piece < route->verified_count &&
		route->verified_ramp_approaches && tiles >= UNIT_RAMP_APPROACH_MINIMUM &&
		tiles <= UNIT_RAMP_APPROACH_MAXIMUM) {
		route->verified_ramp_approaches[piece] = tiles;
	}
}

void unit_opponent_pilot_accept_exit(legacy_s16 piece, legacy_f64 fraction, legacy_f64 speed)
{
	struct UNIT_ROUTE *route = unit_opponent_pilot_route_mutable();
	if (piece >= 0 && (legacy_u32)piece < route->verified_count && route->verified_exits) {
		struct UNIT_EXIT_PROFILE *exit = &route->verified_exits[piece];
		exit->fraction = isfinite(fraction) ? unit_bounded(fraction, 0.0, 1.0) : 0.0;
		exit->speed = isfinite(speed) ? unit_bounded(speed, 0.0, route->limits.max_speed) : 0.0;
	}
}

#endif
