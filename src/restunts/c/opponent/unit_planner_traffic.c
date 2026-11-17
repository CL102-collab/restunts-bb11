#include "unit_planner_internal.h"
#include "unit_collision.h"

#include <math.h>

static legacy_f64 distance(legacy_f64 x, legacy_f64 z)
{
	return sqrt(x * x + z * z);
}

static legacy_int ordered_stunt(const struct UNIT_ROUTE_POINT *point)
{
	return point->stunt != UNIT_STUNT_NONE && !point->clearance_only;
}

static legacy_f64 unit_planner_forecast_height(const struct UNIT_VEHICLE *vehicle, legacy_f64 time)
{
	return vehicle->y + vehicle->vy * time -
		   (vehicle->airborne ? 0.5 * UNIT_PLANNER_WORLD_GRAVITY * time * time : 0.0);
}

/* The rollout may release a clearance preference only for a human intersecting
 * the centered route corridor. Mandatory maneuvers and gate apertures stay constrained. */
legacy_u8 unit_planner_clearance_corridor_blocked(const struct UNIT_ROUTE_POINT *points,
												  legacy_u32 count,
												  const struct UNIT_CAR_LIMITS *limits,
												  const struct UNIT_REQUEST *request)
{
	legacy_f64 speed =
		fmax(UNIT_PLANNER_MIN_SPEED,
			 fmin(limits->max_speed,
				  request->unit.speed + limits->acceleration * UNIT_PLANNER_ROLLOUT_SECONDS * 0.5));
	legacy_f64 reach = speed * UNIT_PLANNER_WORLD_PER_MPH * UNIT_PLANNER_ROLLOUT_SECONDS +
					   UNIT_PLANNER_MAX_LOOKAHEAD + request->unit.half_length +
					   request->player.half_length;
	if (distance(request->unit.x - request->player.x, request->unit.z - request->player.z) >
		reach) {
		return 0;
	}
	legacy_u32 first = request->cursor ? request->cursor - 1U : 0U;
	legacy_f64 traveled = 0.0, ride = request->unit.y - points[request->cursor].y;
	for (legacy_u32 index = first; index + 1U < count && traveled <= reach &&
								   index - first < UNIT_PLANNER_LOCAL_SAMPLE_SCAN_LIMIT;
		 index++) {
		const struct UNIT_ROUTE_POINT *a = &points[index], *b = &points[index + 1U];
		if (ordered_stunt(a) || ordered_stunt(b)) {
			break;
		}
		legacy_f64 dx = b->x - a->x, dz = b->z - a->z, span = distance(dx, dz);
		if (span <= UNIT_PLANNER_MIN_DISTANCE) {
			continue;
		}
		legacy_f64 time = fmin(UNIT_PLANNER_ROLLOUT_SECONDS,
							   (traveled + span * 0.5) / (speed * UNIT_PLANNER_WORLD_PER_MPH));
		legacy_f64 px = request->player.x + request->player.vx * time;
		legacy_f64 pz = request->player.z + request->player.vz * time;
		legacy_f64 along = ((px - a->x) * dx + (pz - a->z) * dz) / span;
		legacy_f64 angle = request->player.yaw - atan2(dx, dz);
		legacy_f64 length = request->unit.half_length + UNIT_PLANNER_COLLISION_MARGIN +
							unit_collision_projected_extent(request->player.half_length,
															request->player.half_width, angle);
		legacy_f64 width = request->unit.half_width + UNIT_PLANNER_COLLISION_MARGIN +
						   unit_collision_projected_extent(request->player.half_width,
														   request->player.half_length, angle);
		legacy_f64 lateral = fabs((px - a->x) * dz - (pz - a->z) * dx) / span;
		if (along >= -length && along <= span + length && lateral < width) {
			legacy_f64 fraction = unit_planner_clamp(along / span, 0.0, 1.0);
			legacy_f64 height = a->y + (b->y - a->y) * fraction + ride;
			legacy_f64 player_y = unit_planner_forecast_height(&request->player, time);
			if (a->height_unspecified || b->height_unspecified ||
				(height >= fmin(request->player.y, player_y) - UNIT_PLANNER_COLLISION_HEIGHT &&
				 height <= fmax(request->player.y, player_y) + UNIT_PLANNER_COLLISION_HEIGHT)) {
				return 1;
			}
		}
		traveled += span;
	}
	return 0;
}
