#include "extended_unit_checkpoint.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include <math.h>
#include "../opponent/unit_opponent_internal.h"
#include "../externs.h"
#include "../track/track_objects.h"
#include "extended_checkpoint.h"
#include "checkpoint_shape.h"

#define UNIT_GATE_QUARTER_TURN_MASK 3U
#define UNIT_GATE_POINT_LIMIT 128U
#define UNIT_GATE_RUNOUT (CSTRK_TILE_WORLD_SIZE / 4.0)
#define UNIT_GATE_APERTURE_MARGIN 12.0
#define UNIT_GATE_ALIGN_FRACTION 0.5
#define UNIT_GATE_ALIGN_SPEED 40.0
#define UNIT_GATE_TURN_SPEED 20.0
#define UNIT_GATE_LOOKAHEAD_SECONDS 1.0
#define UNIT_GATE_TURN_LOOKAHEAD_SCALE 2.0
#define UNIT_GATE_TURN_FORWARD_FRACTION 0.25
#define UNIT_GATE_DIRECTION_EPSILON 0.000001

static const struct CSTRK_OBJECT *unit_checkpoint_object(const struct UNIT_ROUTE *route,
														 legacy_u32 cursor)
{
	if (!extended_track_active() || !route || !route->points || cursor >= route->count) {
		return NULL;
	}
	legacy_s16 piece = route->points[cursor].route_index;
	if (piece < 0 || piece >= track_pieces_counter) {
		return NULL;
	}
	const struct CSTRK_OBJECT *object = extended_track_object_for_id(
		(legacy_u8)track_route_columns[piece], (legacy_u8)track_route_rows[piece],
		(legacy_u8)track_route_element_ids[piece]);
	return object && cstrk_checkpoint_colors(object->legacy_id) ? object : NULL;
}

legacy_int unit_checkpoint_can_advance(const struct UNIT_ROUTE *route, legacy_u32 cursor)
{
	if (!route || !route->points || cursor >= route->count ||
		(cursor + 1U < route->count &&
		 route->points[cursor + 1U].route_index == route->points[cursor].route_index)) {
		return 1;
	}
	const struct CSTRK_OBJECT *object = unit_checkpoint_object(route, cursor);
	return !object || extended_checkpoint_passed(object, OPPONENT_CAR_INDEX);
}

static const struct CSTRK_OBJECT *unit_checkpoint_upcoming(const struct UNIT_ROUTE *route,
														   legacy_u32 *cursor)
{
	const struct CSTRK_OBJECT *object = unit_checkpoint_object(route, *cursor);
	if (object) {
		return object;
	}
	/* Inspect only the next piece, bounded by the native samples per piece.
	 * Gates beyond it are handled when their approach enters the live horizon. */
	legacy_s16 piece = route->points[*cursor].route_index;
	legacy_u32 next = *cursor;
	for (legacy_u32 count = 0; count < UNIT_GATE_POINT_LIMIT && next + 1U < route->count; count++) {
		next++;
		if (route->points[next].route_index != piece) {
			object = unit_checkpoint_object(route, next);
			if (object) {
				*cursor = next;
			}
			return object;
		}
	}
	return NULL;
}

static legacy_f64 unit_checkpoint_height(const struct CSTRK_TRACK *track,
										 const struct CSTRK_OBJECT *object, legacy_f64 x,
										 legacy_f64 z)
{
	legacy_f64 local_x = x - object->x * CSTRK_TILE_WORLD_SIZE;
	legacy_f64 local_y = (track->height - object->y) * CSTRK_TILE_WORLD_SIZE - z;
	return object->base_height * CSTRK_LEVEL_WORLD_HEIGHT +
		   (object->slope_x * local_x + object->slope_y * local_y) * CSTRK_LEVEL_WORLD_HEIGHT /
			   CSTRK_TILE_WORLD_SIZE;
}

static legacy_int unit_checkpoint_plan_crosses(const struct UNIT_ROUTE *route,
											   const struct CSTRK_TRACK *track,
											   const struct CSTRK_OBJECT *object,
											   const struct CARSTATE *car,
											   const struct UNIT_PLAN *plan, legacy_f64 center_x,
											   legacy_f64 center_z, legacy_f64 nx, legacy_f64 nz)
{
	if (!plan->valid) {
		return 0;
	}
	legacy_f64 x = car->car_position.lx / UNIT_POSITION_SCALE;
	legacy_f64 y = car->car_position.ly / UNIT_POSITION_SCALE;
	legacy_f64 z = car->car_position.lz / UNIT_POSITION_SCALE;
	legacy_f64 depth = (x - center_x) * nx + (z - center_z) * nz;
	legacy_f64 target_depth = (plan->target_x - center_x) * nx + (plan->target_z - center_z) * nz;
	if (depth == 0.0 || (depth < 0.0 ? target_depth < 0.0 : target_depth > 0.0)) {
		return 0;
	}
	legacy_f64 fraction = depth / (depth - target_depth);
	legacy_f64 cross_x = x + (plan->target_x - x) * fraction;
	legacy_f64 cross_z = z + (plan->target_z - z) * fraction;
	legacy_f64 lateral = (cross_x - center_x) * nz - (cross_z - center_z) * nx;
	legacy_f64 yaw = -car->car_rotate.x * UNIT_ANGLE_RADIANS;
	legacy_f64 line_yaw = atan2(plan->target_x - x, plan->target_z - z);
	legacy_f64 angle = atan2(sin(line_yaw - yaw), cos(line_yaw - yaw));
	legacy_f64 half_width = route->limits.half_width, half_length = route->limits.half_length;
	legacy_f64 width = half_width * fabs(nz * cos(yaw) + nx * sin(yaw)) +
					   half_length * fabs(nz * sin(yaw) - nx * cos(yaw));
	/* The rectangle's angular support changes at most by its corner radius
	 * per radian. This also bounds intermediate headings while lining up. */
	width += hypot(half_width, half_length) * fabs(angle);
	legacy_f64 roll = fabs(sin(car->car_rotate.z * UNIT_ANGLE_RADIANS));
	legacy_f64 pitch = fabs(sin(car->car_rotate.y * UNIT_ANGLE_RADIANS));
	legacy_f64 roof = fabs((legacy_f64)simd_opponent.collide_points[0].py);
	width += roof * (roll + pitch);
	if (fabs(lateral) + width + UNIT_GATE_APERTURE_MARGIN >= CHECKPOINT_GATE_INNER_HALF_WIDTH) {
		return 0;
	}
	legacy_f64 ride = y - unit_checkpoint_height(track, object, x, z);
	legacy_f64 cross_y = y + (plan->target_y + ride - y) * fraction;
	legacy_f64 height = cross_y - unit_checkpoint_height(track, object, cross_x, cross_z);
	legacy_f64 top = roof + half_width * roll + half_length * pitch;
	return height >= 0.0 && height + top < CHECKPOINT_GATE_SIGN_BOTTOM;
}

legacy_int unit_checkpoint_guide(const struct UNIT_ROUTE *route, legacy_u32 cursor,
								 const struct CARSTATE *car, struct UNIT_PLAN *plan)
{
	if (!route || !route->points || cursor >= route->count || !car || !plan) {
		return 0;
	}
	legacy_u32 gate_cursor = cursor;
	const struct CSTRK_OBJECT *object = unit_checkpoint_upcoming(route, &gate_cursor);
	if (!object || extended_checkpoint_passed(object, OPPONENT_CAR_INDEX)) {
		return 0;
	}
	const struct CSTRK_TRACK *track = extended_track_model();
	legacy_f64 center_x = (object->x + object->width * 0.5) * CSTRK_TILE_WORLD_SIZE;
	legacy_f64 center_z =
		(track->height - object->y - object->height * 0.5) * CSTRK_TILE_WORLD_SIZE;
	legacy_f64 x = car->car_position.lx / UNIT_POSITION_SCALE;
	legacy_f64 z = car->car_position.lz / UNIT_POSITION_SCALE;
	legacy_f64 speed = car->car_actual_speed / UNIT_SPEED_SCALE;
	if (gate_cursor != cursor &&
		hypot(x - center_x, z - center_z) >
			speed * UNIT_WORLD_PER_MPH * UNIT_GATE_LOOKAHEAD_SECONDS + UNIT_GATE_RUNOUT) {
		return 0;
	}
	static const legacy_s8 normal_x[] = {0, 1, 0, -1};
	static const legacy_s8 normal_z[] = {1, 0, -1, 0};
	legacy_u8 rotation = object->rotation & UNIT_GATE_QUARTER_TURN_MASK;
	legacy_f64 nx = normal_x[rotation], nz = normal_z[rotation];
	/* A safe offset passage may be the planner's way around the human. Do
	 * not replace it with a center target merely because this piece is a gate. */
	if (unit_checkpoint_plan_crosses(route, track, object, car, plan, center_x, center_z, nx, nz)) {
		return 0;
	}
	legacy_f64 lateral = (x - center_x) * nz - (z - center_z) * nx;
	legacy_f64 depth = (x - center_x) * nx + (z - center_z) * nz;
	legacy_f64 previous_depth =
		(car->car_previous_position.lx / UNIT_POSITION_SCALE - center_x) * nx +
		(car->car_previous_position.lz / UNIT_POSITION_SCALE - center_z) * nz;
	/* The observer accepts either direction. Stay on the current side while
	 * aligning, then target the other side so an exact plane landing advances. */
	legacy_f64 side = depth > 0.0 || (depth == 0.0 && previous_depth > 0.0) ? 1.0 : -1.0;
	legacy_f64 yaw = -car->car_rotate.x * UNIT_ANGLE_RADIANS;
	legacy_f64 forward_x = sin(yaw), forward_z = cos(yaw);
	legacy_f64 desired_x = -side * nx, desired_z = -side * nz;
	legacy_f64 heading = forward_x * desired_x + forward_z * desired_z;
	legacy_f64 clearance =
		fmax(UNIT_GATE_APERTURE_MARGIN, CHECKPOINT_GATE_INNER_HALF_WIDTH -
											route->limits.half_width - UNIT_GATE_APERTURE_MARGIN);
	legacy_f64 target_x = center_x - side * nx * UNIT_GATE_RUNOUT;
	legacy_f64 target_z = center_z - side * nz * UNIT_GATE_RUNOUT;
	legacy_f64 target_speed = plan->target_speed;
	if (heading < 0.0) {
		/* A point directly behind the car has zero pure-pursuit curvature.
		 * Turn toward the required heading first. The signed heading error
		 * keeps the chosen turn stable as the vehicle crosses the centerline. */
		legacy_f64 turn = desired_x * forward_z - desired_z * forward_x;
		if (fabs(turn) < UNIT_GATE_DIRECTION_EPSILON) {
			turn = lateral > 0.0 ? -(nz * forward_z + nx * forward_x)
								 : nz * forward_z + nx * forward_x;
		}
		legacy_f64 sign = turn >= 0.0 ? 1.0 : -1.0;
		legacy_f64 lookahead = fmax(route->wheelbase * UNIT_GATE_TURN_LOOKAHEAD_SCALE,
									route->limits.half_length * UNIT_GATE_TURN_LOOKAHEAD_SCALE);
		target_x = x + lookahead * (sign * forward_z + forward_x * UNIT_GATE_TURN_FORWARD_FRACTION);
		target_z =
			z + lookahead * (-sign * forward_x + forward_z * UNIT_GATE_TURN_FORWARD_FRACTION);
		target_speed = fmin(target_speed, UNIT_GATE_TURN_SPEED);
	} else if (fabs(lateral) > clearance * UNIT_GATE_ALIGN_FRACTION) {
		legacy_f64 approach = fmax(fabs(depth), UNIT_GATE_RUNOUT);
		target_x = center_x + side * nx * approach;
		target_z = center_z + side * nz * approach;
		target_speed = fmin(target_speed, UNIT_GATE_ALIGN_SPEED);
	}
	plan->target_x = target_x;
	plan->target_y = unit_checkpoint_height(track, object, target_x, target_z);
	plan->target_z = target_z;
	plan->target_speed = target_speed;
	plan->route_cursor = gate_cursor;
	return 1;
}
#endif
