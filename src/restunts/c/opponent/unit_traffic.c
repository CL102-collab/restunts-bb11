#include "unit_traffic.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include <math.h>
#include "unit_opponent_internal.h"
#include "../track/trackdata_layout.h"

legacy_int unit_collision_brake(const struct UNIT_ROUTE *route, const struct UNIT_VEHICLE *unit,
								const struct UNIT_VEHICLE *player)
{
	if (fabs(unit->y - player->y) > UNIT_COLLISION_HEIGHT_CLEARANCE) {
		return 0;
	}
	legacy_f64 dx = player->x - unit->x, dz = player->z - unit->z;
	legacy_f64 sine = sin(unit->yaw), cosine = cos(unit->yaw);
	legacy_f64 ahead = dx * sine + dz * cosine;
	legacy_f64 lateral = dx * cosine - dz * sine;
	legacy_f64 relative_yaw = player->yaw - unit->yaw;
	legacy_f64 player_length = fabs(cos(relative_yaw)) * player->half_length +
							   fabs(sin(relative_yaw)) * player->half_width;
	legacy_f64 player_width = fabs(cos(relative_yaw)) * player->half_width +
							  fabs(sin(relative_yaw)) * player->half_length;
	legacy_f64 extent = unit->half_length + player_length + UNIT_COLLISION_MARGIN;
	legacy_f64 width = unit->half_width + player_width + UNIT_COLLISION_MARGIN;
	legacy_f64 closing = (unit->vx - player->vx) * sine + (unit->vz - player->vz) * cosine;
	legacy_f64 stopping =
		closing > 0 ? closing * UNIT_COLLISION_REACTION_SECONDS +
						  closing * closing / (2.0 * route->limits.braking * UNIT_WORLD_PER_MPH)
					: 0;
	return ahead > 0 && ahead < extent + stopping && fabs(lateral) < width;
}

legacy_int unit_landing_brake(const struct UNIT_ROUTE *route, legacy_u32 cursor,
							  const struct UNIT_VEHICLE *unit, const struct UNIT_VEHICLE *player)
{
	if (unit->airborne || cursor >= route->count) {
		return 0;
	}
	legacy_u32 first = cursor;
	while (first && route->points[first - 1U].route_index == route->points[first].route_index) {
		first--;
	}
	legacy_f64 approach = 0.0;
	for (; first + 1U < route->count && approach < 2.0 * TRACK_TILE_SIZE; first++) {
		const struct UNIT_ROUTE_POINT *entry = &route->points[first];
		if (entry->stunt != UNIT_STUNT_BARREL && entry->stunt != UNIT_STUNT_RAMP) {
			approach += hypot(route->points[first + 1U].x - entry->x,
							  route->points[first + 1U].z - entry->z);
			continue;
		}
		legacy_u32 last = first;
		while (last + 1U < route->count &&
			   route->points[last + 1U].route_index == entry->route_index) {
			last++;
		}
		if (last + 2U >= route->count) {
			return 0;
		}
		const struct UNIT_ROUTE_POINT *lip = &route->points[last];
		/* Once committed, the only physical path is the jump. The forecast is
		 * used while the car can still wait on its approach. */
		if ((unit->x - entry->x) * (lip->x - entry->x) +
				(unit->z - entry->z) * (lip->z - entry->z) >=
			0.0) {
			return 0;
		}
		const struct UNIT_ROUTE_POINT *landing = &route->points[last + 1U];
		legacy_f64 speed = fmax(UNIT_MINIMUM_MOVING_SPEED,
								(unit->speed + fmax(entry->speed_floor, entry->speed_limit)) * 0.5);
		legacy_f64 time =
			hypot(landing->x - unit->x, landing->z - unit->z) / (speed * UNIT_WORLD_PER_MPH);
		legacy_f64 predicted_x = player->x + player->vx * time;
		legacy_f64 predicted_z = player->z + player->vz * time;
		legacy_f64 minimum_y = player->y, maximum_y = player->y;
		if (player->airborne) {
			legacy_f64 predicted_y =
				player->y + player->vy * time -
				0.5 * UNIT_TRAFFIC_LANDING_GRAVITY(UNIT_POSITION_SCALE) * time * time;
			minimum_y = fmin(minimum_y, predicted_y);
			maximum_y = fmax(maximum_y, predicted_y);
			if (player->vy > 0.0) {
				maximum_y =
					fmax(maximum_y,
						 player->y + player->vy * player->vy /
										 (2.0 * UNIT_TRAFFIC_LANDING_GRAVITY(UNIT_POSITION_SCALE)));
			}
		}
		legacy_f64 clearance = hypot(unit->half_width, unit->half_length) +
							   hypot(player->half_width, player->half_length) +
							   UNIT_COLLISION_MARGIN;
		legacy_f64 corridor = 0.0;
		for (legacy_u32 point = last + 1U;
			 point + 1U < route->count && corridor < 2.0 * TRACK_TILE_SIZE; point++) {
			const struct UNIT_ROUTE_POINT *a = &route->points[point];
			const struct UNIT_ROUTE_POINT *b = &route->points[point + 1U];
			legacy_f64 dx = b->x - a->x, dz = b->z - a->z;
			legacy_f64 squared = dx * dx + dz * dz;
			legacy_f64 fraction =
				squared > 1.0
					? unit_bounded(((predicted_x - a->x) * dx + (predicted_z - a->z) * dz) /
									   squared,
								   0.0, 1.0)
					: 0.0;
			legacy_f64 y = a->y + (b->y - a->y) * fraction;
			if (hypot(predicted_x - a->x - dx * fraction, predicted_z - a->z - dz * fraction) <
					clearance &&
				y > minimum_y - UNIT_COLLISION_HEIGHT_CLEARANCE &&
				y < maximum_y + UNIT_COLLISION_HEIGHT_CLEARANCE) {
				return 1;
			}
			corridor += sqrt(squared);
		}
		return 0;
	}
	return 0;
}

#endif
