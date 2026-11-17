#include "unit_overtake.h"

#include <math.h>
#include <string.h>
#include "unit_collision.h"
#include "unit_planner_internal.h"
#include "unit_shortcut.h"

#define UNIT_OVERTAKE_MAXIMUM_REACH 4096.0
#define UNIT_OVERTAKE_MINIMUM_REACH 768.0
#define UNIT_OVERTAKE_REACTION_SECONDS 2.4
#define UNIT_OVERTAKE_HEIGHT_CLEARANCE 90.0
#define UNIT_OVERTAKE_BODY_CLEARANCE 10.0
#define UNIT_OVERTAKE_FINISH_CLEARANCE 24.0
#define UNIT_OVERTAKE_SPEED_ADVANTAGE 3.0
#define UNIT_OVERTAKE_CROSSING_SPEED 32.0
#define UNIT_OVERTAKE_MERGE_SLOPE 0.15
#define UNIT_OVERTAKE_MERGE_CENTER_CLEARANCE 2.0
#define UNIT_OVERTAKE_MINIMUM_DIRECTION 0.01
#define UNIT_OVERTAKE_LEFT 1
#define UNIT_OVERTAKE_RIGHT (-1)

static legacy_int unit_overtake_relevant(const struct UNIT_VEHICLE *unit,
										 const struct UNIT_VEHICLE *player)
{
	return unit && player && fabs(unit->y - player->y) < UNIT_OVERTAKE_HEIGHT_CLEARANCE &&
		   hypot(unit->x - player->x, unit->z - player->z) < UNIT_OVERTAKE_MAXIMUM_REACH;
}

static legacy_f64 overtake_width(const struct UNIT_ROUTE_POINT *point,
								 const struct UNIT_CAR_LIMITS *limits)
{
	legacy_f64 width = point->half_width - limits->half_width - UNIT_SHORTCUT_ROAD_CLEARANCE;
	/* A passing lane keeps its wheels on the road; grass drag would make
	 * matching a faster leader possible without ever completing the pass. */
	return fmax(0.0, width);
}

static legacy_int overtake_direction(const struct UNIT_ROUTE_POINT *points, legacy_u32 count,
									 legacy_u32 cursor, legacy_f64 *dx, legacy_f64 *dz)
{
	legacy_u32 first = cursor ? cursor - 1U : cursor;
	legacy_u32 last = cursor ? cursor : cursor + 1U;
	if (last >= count) {
		return 0;
	}
	*dx = points[last].x - points[first].x;
	*dz = points[last].z - points[first].z;
	legacy_f64 length = hypot(*dx, *dz);
	if (length <= UNIT_OVERTAKE_MINIMUM_DIRECTION) {
		return 0;
	}
	*dx /= length;
	*dz /= length;
	return 1;
}

static legacy_f64 overtake_offset(legacy_s8 side, legacy_f64 player_lateral, legacy_f64 clearance,
								  legacy_f64 available)
{
	return side == UNIT_OVERTAKE_LEFT ? fmin(available, fmax(0.0, player_lateral + clearance))
									  : fmax(-available, fmin(0.0, player_lateral - clearance));
}

static void unit_overtake_update(struct UNIT_OVERTAKE *intent,
								 const struct UNIT_ROUTE_POINT *points, legacy_u32 count,
								 const struct UNIT_CAR_LIMITS *limits,
								 const struct UNIT_VEHICLE *unit, const struct UNIT_VEHICLE *player,
								 legacy_u32 cursor, legacy_u32 generation, legacy_f64 desired_speed,
								 legacy_u8 enabled)
{
	if (!intent) {
		return;
	}
	if (!enabled || !points || !limits || !unit || cursor >= count || unit->airborne ||
		unit->recovering ||
		(points[cursor].stunt != UNIT_STUNT_NONE && !points[cursor].clearance_only) ||
		(points[cursor].align_center & UNIT_ALIGN_MANEUVER)) {
		memset(intent, 0, sizeof(*intent));
		return;
	}
	if (intent->have_generation && generation < intent->generation) {
		/* Rewinding never inherits a traffic maneuver from the abandoned race. */
		memset(intent, 0, sizeof(*intent));
		intent->have_generation = 1;
		intent->generation = generation;
		intent->previous_x = unit->x;
		intent->previous_z = unit->z;
		return;
	}
	if (intent->have_generation && generation == intent->generation) {
		return;
	}
	legacy_f64 traveled = intent->have_generation
							  ? hypot(unit->x - intent->previous_x, unit->z - intent->previous_z)
							  : 0.0;
	intent->generation = generation;
	intent->have_generation = 1;
	intent->previous_x = unit->x;
	intent->previous_z = unit->z;
	if (traveled > UNIT_OVERTAKE_MAXIMUM_REACH) {
		intent->phase = UNIT_OVERTAKE_IDLE;
		intent->offset = 0.0;
		return;
	}
	legacy_f64 dx, dz;
	if (!overtake_direction(points, count, cursor, &dx, &dz)) {
		intent->phase = UNIT_OVERTAKE_IDLE;
		intent->offset = 0.0;
		return;
	}
	legacy_f64 available = overtake_width(&points[cursor], limits);
	if (cursor && points[cursor - 1U].route_index == points[cursor].route_index) {
		available = fmin(available, overtake_width(&points[cursor - 1U], limits));
	}
	if (available <= 0.0) {
		intent->phase = UNIT_OVERTAKE_IDLE;
		intent->offset = 0.0;
		return;
	}
	legacy_f64 unit_lateral =
		-(unit->x - points[cursor].x) * dz + (unit->z - points[cursor].z) * dx;
	legacy_u8 relevant = unit_overtake_relevant(unit, player);
	legacy_f64 ahead = player ? (player->x - unit->x) * dx + (player->z - unit->z) * dz : 0.0;
	legacy_f64 angle = player ? player->yaw - atan2(dx, dz) : 0.0;
	legacy_f64 player_length =
		player ? unit_collision_projected_extent(player->half_length, player->half_width, angle)
			   : 0.0;
	legacy_f64 player_width =
		player ? unit_collision_projected_extent(player->half_width, player->half_length, angle)
			   : 0.0;
	legacy_f64 clearance = unit->half_width + player_width + UNIT_OVERTAKE_BODY_CLEARANCE;
	legacy_f64 player_speed =
		player ? fmax(0.0, (player->vx * dx + player->vz * dz) / UNIT_PLANNER_WORLD_PER_MPH) : 0.0;
	legacy_f64 turn_reach = fmax(UNIT_OVERTAKE_MINIMUM_REACH,
								 (unit->speed - player_speed) * UNIT_PLANNER_WORLD_PER_MPH *
										 UNIT_OVERTAKE_REACTION_SECONDS +
									 unit->half_length + player_length);
	legacy_f64 player_lateral =
		player ? -(player->x - points[cursor].x) * dz + (player->z - points[cursor].z) * dx : 0.0;
	legacy_u8 rear_clear =
		!relevant || ahead < -unit->half_length - player_length - UNIT_OVERTAKE_FINISH_CLEARANCE;
	legacy_u8 longitudinally_clear = !relevant || fabs(ahead) > unit->half_length + player_length +
																	UNIT_OVERTAKE_FINISH_CLEARANCE;
	if (intent->phase == UNIT_OVERTAKE_MERGING && !longitudinally_clear) {
		/* A human accelerating back alongside cancels the merge, not the side. */
		intent->phase = UNIT_OVERTAKE_COMMITTED;
		intent->started = 1;
	}
	legacy_u8 leader_escaped = relevant && ahead > 0.0 && longitudinally_clear &&
							   desired_speed > 0.0 &&
							   player_speed + UNIT_OVERTAKE_SPEED_ADVANTAGE >= desired_speed;
	if (intent->phase == UNIT_OVERTAKE_COMMITTED && (rear_clear || leader_escaped)) {
		intent->phase = UNIT_OVERTAKE_MERGING;
	}
	if (intent->phase == UNIT_OVERTAKE_MERGING) {
		legacy_u8 centered = fabs(unit_lateral) <= UNIT_OVERTAKE_MERGE_CENTER_CLEARANCE;
		legacy_f64 remaining =
			centered ? 0.0 : fmax(0.0, fabs(intent->offset) - traveled * UNIT_OVERTAKE_MERGE_SLOPE);
		intent->offset = copysign(fmin(available, remaining), intent->offset);
		/* Keep the maneuver live until the body reaches the centered line,
		 * including the final steering interval after the target reaches zero. */
		if (centered) {
			intent->phase = UNIT_OVERTAKE_IDLE;
			intent->offset = 0.0;
		}
		return;
	}
	if (intent->phase == UNIT_OVERTAKE_COMMITTED) {
		intent->started |= ahead <= turn_reach;
		if (intent->started) {
			intent->offset = overtake_offset(intent->side, player_lateral,
											 clearance + unit->half_length, available);
		}
		return;
	}
	if (!relevant || !isfinite(desired_speed) || desired_speed <= 0.0) {
		return;
	}
	legacy_f64 forward_speed = player->vx * dx + player->vz * dz;
	legacy_f64 crossing_speed = -player->vx * dz + player->vz * dx;
	if (forward_speed < -UNIT_OVERTAKE_CROSSING_SPEED ||
		fabs(crossing_speed) > fmax(UNIT_OVERTAKE_CROSSING_SPEED, fabs(forward_speed)) ||
		player_speed + UNIT_OVERTAKE_SPEED_ADVANTAGE >= desired_speed ||
		ahead < -unit->half_length || fabs(unit_lateral - player_lateral) >= clearance) {
		return;
	}
	legacy_f64 reach = fmin(UNIT_OVERTAKE_MAXIMUM_REACH,
							fmax(UNIT_OVERTAKE_MINIMUM_REACH,
								 (desired_speed - player_speed) * UNIT_PLANNER_WORLD_PER_MPH *
										 UNIT_OVERTAKE_REACTION_SECONDS +
									 unit->half_length + player_length));
	if (ahead > reach) {
		return;
	}
	legacy_f64 left_room = available - player_lateral - player_width;
	legacy_f64 right_room = available + player_lateral - player_width;
	intent->side = left_room > right_room ||
						   (fabs(left_room - right_room) <= UNIT_OVERTAKE_MINIMUM_DIRECTION &&
							unit_lateral >= 0.0)
					   ? UNIT_OVERTAKE_LEFT
					   : UNIT_OVERTAKE_RIGHT;
	intent->started = ahead <= turn_reach;
	intent->offset = intent->started ? overtake_offset(intent->side, player_lateral,
													   clearance + unit->half_length, available)
									 : unit_lateral;
	intent->phase = UNIT_OVERTAKE_COMMITTED;
}

void unit_overtake_cancel(struct UNIT_OVERTAKE *intent)
{
	if (intent) {
		memset(intent, 0, sizeof(*intent));
	}
}

void unit_overtake_request(struct UNIT_OVERTAKE *intent, const struct UNIT_ROUTE_POINT *points,
						   legacy_u32 count, const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot,
						   const struct UNIT_PROFILE *profile, struct UNIT_REQUEST *request)
{
	legacy_f64 desired_speed = 0.0;
	if (!request->native_preview && unit_overtake_relevant(&request->unit, &request->player)) {
		/* A stopped follower compares the human with its free-road pace. */
		struct UNIT_REQUEST unobstructed = *request;
		unobstructed.player = (struct UNIT_VEHICLE){.x = UNIT_NO_PLAYER_POSITION};
		struct UNIT_ROLLOUT free_road;
		if (unit_planner_rollout(points, count, limits, owoot, profile, &unobstructed, 0U,
								 &free_road) &&
			free_road.plan.valid) {
			desired_speed = free_road.plan.target_speed;
		}
	}
	unit_overtake_update(intent, points, count, limits, &request->unit, &request->player,
						 request->cursor, request->generation, desired_speed,
						 !request->native_preview);
	request->overtaking = intent->phase != UNIT_OVERTAKE_IDLE;
	request->overtake_offset = intent->offset;
}
