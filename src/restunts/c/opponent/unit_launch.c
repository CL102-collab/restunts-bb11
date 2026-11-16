#include "unit_launch.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include <math.h>
#include "unit_opponent_internal.h"
#include "../externs.h"
#include "../extended/extended_owoot_internal.h"
#include "../owoot/owoot.h"
#include "../track/trackdata_layout.h"

#define UNIT_LAUNCH_LOOKBEHIND_PIECES 2U
#define UNIT_LAUNCH_APPROACH_TILES 2.0
#define UNIT_LAUNCH_LOOKAHEAD_BASE 160.0
#define UNIT_LAUNCH_LOOKAHEAD_SPEED_SCALE 1.3
#define UNIT_LAUNCH_LOOKAHEAD_MAXIMUM 260.0
#define UNIT_BARREL_LINE_CLEARANCE 4.0
#define UNIT_RAMP_AXIS_BANK_MINIMUM (ANGLE_EIGHTH_TURN / 8)

static legacy_int unit_launch_road_contact(const struct CARSTATE *car)
{
	for (legacy_u32 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		if (car->car_surfaceWhl[wheel] < CAR_SURFACE_PAVED ||
			car->car_surfaceWhl[wheel] > CAR_SURFACE_ICE) {
			return 0;
		}
	}
	return 1;
}

static legacy_int unit_launch_receiver_contact(const struct CARSTATE *car)
{
	for (legacy_u32 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		if (car->car_surfaceWhl[wheel] >= CAR_SURFACE_PAVED &&
			car->car_surfaceWhl[wheel] <= CAR_SURFACE_ICE) {
			return 1;
		}
	}
	return 0;
}

static legacy_int unit_launch_landed(const struct CARSTATE *car)
{
	return unit_launch_road_contact(car) &&
		   (!owoot_enabled || car->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] == 0);
}

static legacy_f64 unit_launch_width(const struct UNIT_ROUTE *route, legacy_u32 first,
									legacy_u32 lip, legacy_f64 dx, legacy_f64 dz, legacy_f64 along)
{
	const struct UNIT_ROUTE_POINT *entry = &route->points[first];
	legacy_f64 width = entry->half_width, previous = 0.0;
	if (along <= 0.0) {
		return width;
	}
	for (legacy_u32 point = first + 1U; point <= lip; point++) {
		const struct UNIT_ROUTE_POINT *next = &route->points[point];
		legacy_f64 distance = (next->x - entry->x) * dx + (next->z - entry->z) * dz;
		if (distance > previous && along <= distance) {
			return width + (next->half_width - width) * (along - previous) / (distance - previous);
		}
		width = next->half_width;
		previous = distance;
	}
	return width;
}

static legacy_f64 unit_launch_progress(const struct UNIT_ROUTE *route, legacy_u32 first,
									   legacy_u32 cursor, const struct CARSTATE *car)
{
	if (cursor <= first) {
		return 0.0;
	}
	const struct UNIT_ROUTE_POINT *a = &route->points[cursor - 1U];
	const struct UNIT_ROUTE_POINT *b = &route->points[cursor];
	legacy_f64 dx = b->x - a->x, dy = b->y - a->y, dz = b->z - a->z;
	legacy_f64 length = dx * dx + dy * dy + dz * dz;
	legacy_f64 along = length > 0.0 ? ((car->car_position.lx / UNIT_POSITION_SCALE - a->x) * dx +
									   (car->car_position.ly / UNIT_POSITION_SCALE - a->y) * dy +
									   (car->car_position.lz / UNIT_POSITION_SCALE - a->z) * dz) /
										  length
									: 0.0;
	/* The ordered target can advance within 64 units of a sample. Preserve a
	 * negative projection until the car physically crosses that sample. */
	return (legacy_f64)(cursor - first - 1U) + fmax(-1.0, fmin(1.0, along));
}

/* A barrel ramp supplies roll. Stabilize its world axis during full road
 * contact, then retain neutral controls through unsupported roll and landing.
 * Derive this phase from recorded route provenance and native contact state;
 * there is no unsnapshotted replay state. */
legacy_int unit_launch_guidance_policy(const struct UNIT_ROUTE *route, legacy_u32 cursor,
									   struct UNIT_PLAN *plan,
									   const struct UNIT_LAUNCH_POLICY *policy)
{
	legacy_s16 override_piece = policy->primary_piece;
	legacy_f64 override_speed = policy->primary_speed;
	const struct UNIT_EXIT_PROFILE *exit_override = policy->exit;
	const struct CARSTATE *car = &state.opponentstate;
	legacy_f64 x = car->car_position.lx / UNIT_POSITION_SCALE;
	legacy_f64 z = car->car_position.lz / UNIT_POSITION_SCALE;
	/* Keep the current piece and two predecessors so a barrel remains guided
	 * through flight and its first landing samples. Earlier protected pieces
	 * are excluded by their ordered cursor or physical landing phase. */
	legacy_u32 first = cursor, preceding = 0;
	while (first > 0) {
		if (route->points[first - 1U].route_index != route->points[first].route_index) {
			if (preceding == UNIT_LAUNCH_LOOKBEHIND_PIECES) {
				break;
			}
			preceding++;
		}
		first--;
	}
	legacy_u32 search_end = cursor;
	legacy_f64 search_distance = 0.0;
	while (search_end + 1U < route->count &&
		   search_distance <= UNIT_RAMP_APPROACH_MAXIMUM * TRACK_TILE_SIZE) {
		const struct UNIT_ROUTE_POINT *a = &route->points[search_end];
		const struct UNIT_ROUTE_POINT *b = &route->points[search_end + 1U];
		search_distance += hypot(b->x - a->x, b->z - a->z);
		search_end++;
	}
	for (; first <= search_end; first++) {
		const struct UNIT_ROUTE_POINT *entry = &route->points[first];
		legacy_f64 verified =
			entry->route_index >= 0 && (legacy_u32)entry->route_index < route->verified_count
				? route->verified_speeds[entry->route_index]
				: 0.0;
		if (entry->route_index == policy->secondary_piece && policy->secondary_speed > 0.0) {
			verified = fmin(policy->secondary_speed, route->limits.max_speed);
		}
		if (entry->route_index == override_piece && override_speed > 0.0) {
			verified = override_speed;
		}
		if (entry->stunt == UNIT_STUNT_NONE ||
			(entry->stunt != UNIT_STUNT_BARREL && entry->stunt != UNIT_STUNT_RAMP &&
			 verified <= 0.0)) {
			continue;
		}
		legacy_u32 last = first;
		while (last + 1U < route->count &&
			   route->points[last + 1U].route_index == route->points[first].route_index) {
			last++;
		}
		if (last + 1U >= route->count ||
			(entry->stunt == UNIT_STUNT_PROTECTED && cursor > last + 2U)) {
			first = last;
			continue;
		}
		legacy_u8 ramp_tiles = UNIT_RAMP_APPROACH_MINIMUM;
		if (entry->stunt == UNIT_STUNT_RAMP) {
			if (route->verified_ramp_approaches && entry->route_index >= 0 &&
				(legacy_u32)entry->route_index < route->verified_count &&
				route->verified_ramp_approaches[entry->route_index]) {
				ramp_tiles = route->verified_ramp_approaches[entry->route_index];
			}
			for (legacy_u32 slot = 0; slot < UNIT_RAMP_APPROACH_OVERRIDES; slot++) {
				if (policy->ramps[slot].tiles && policy->ramps[slot].piece == entry->route_index) {
					ramp_tiles = policy->ramps[slot].tiles;
					break;
				}
			}
			ramp_tiles = ramp_tiles < UNIT_RAMP_APPROACH_MINIMUM   ? UNIT_RAMP_APPROACH_MINIMUM
						 : ramp_tiles > UNIT_RAMP_APPROACH_MAXIMUM ? UNIT_RAMP_APPROACH_MAXIMUM
																   : ramp_tiles;
		}
		legacy_f64 approach_limit =
			(entry->stunt == UNIT_STUNT_RAMP ? ramp_tiles : UNIT_LAUNCH_APPROACH_TILES) *
			TRACK_TILE_SIZE;
		legacy_f64 approach_distance = 0.0;
		for (legacy_u32 point = cursor; point < first && approach_distance <= approach_limit;
			 point++) {
			approach_distance += hypot(route->points[point + 1U].x - route->points[point].x,
									   route->points[point + 1U].z - route->points[point].z);
		}
		if (approach_distance > approach_limit) {
			first = last;
			continue;
		}
		const struct UNIT_EXIT_PROFILE *exit =
			route->verified_exits && entry->route_index >= 0 &&
					(legacy_u32)entry->route_index < route->verified_count
				? &route->verified_exits[entry->route_index]
				: NULL;
		if (entry->route_index == override_piece && exit_override) {
			exit = exit_override;
		}
		if (entry->stunt != UNIT_STUNT_BARREL && entry->stunt != UNIT_STUNT_RAMP) {
			if (cursor <= last) {
				if (plan->brake == UNIT_PLAN_BRAKE_STOP) {
					plan->brake = UNIT_PLAN_BRAKE_NONE;
				}
				plan->target_speed = verified;
				if (exit && exit->fraction > 0.0 && exit->fraction < 1.0 && exit->speed > 0.0 &&
					unit_launch_progress(route, first, cursor, car) >=
						ceil((last - first) * exit->fraction)) {
					plan->target_speed = fmin(exit->speed, route->limits.max_speed);
				}
				return 0;
			}
			first = last;
			continue;
		}
		const struct UNIT_ROUTE_POINT *lip =
			&route->points[entry->stunt == UNIT_STUNT_RAMP ? last : last - 1U];
		const struct UNIT_ROUTE_POINT *landing = &route->points[last + 1U];
		legacy_f64 dx = lip->x - entry->x, dz = lip->z - entry->z;
		legacy_f64 length = hypot(dx, dz);
		if (length < 1.0) {
			first = last;
			continue;
		}
		dx /= length;
		dz /= length;
		legacy_f64 along = (x - entry->x) * dx + (z - entry->z) * dz;
		legacy_f64 arrival = (landing->x - entry->x) * dx + (landing->z - entry->z) * dz;
		if (along < -approach_limit || along > arrival + TRACK_TILE_SIZE ||
			(along > arrival && (entry->stunt == UNIT_STUNT_RAMP ? unit_launch_receiver_contact(car)
																 : unit_launch_landed(car)))) {
			first = last;
			continue;
		}
		if (along < 0.0 && policy->preserve_approach) {
			/* Fresh human avoidance retains its line and speed before commitment. */
			return UNIT_LAUNCH_NONE;
		}
		if ((verified > 0.0 || entry->stunt == UNIT_STUNT_RAMP) &&
			plan->brake == UNIT_PLAN_BRAKE_STOP) {
			plan->brake = UNIT_PLAN_BRAKE_NONE;
		}
		plan->target_speed = verified > 0.0 ? verified : entry->speed_limit;
		/* Preserve ordered ramp/receiver targets. On a supported bank, project
		 * steering horizontally so a vertical guide error cannot create yaw. */
		if (entry->stunt == UNIT_STUNT_RAMP) {
			legacy_s16 bank =
				(legacy_s16)(((legacy_u16)(car->car_rotate.z + ANGLE_HALF_TURN) & ANGLE_MASK) -
							 ANGLE_HALF_TURN);
			/* Both native guides are inset from physical ramp boundaries.
			 * Ordered source provenance permits bank correction on the lead-in;
			 * support and the receiver-plane release above end this phase. */
			if ((along >= 0.0 || car->car_route_index == entry->route_index) &&
				unit_launch_road_contact(car) &&
				(bank >= UNIT_RAMP_AXIS_BANK_MINIMUM || bank <= -UNIT_RAMP_AXIS_BANK_MINIMUM)) {
				return UNIT_LAUNCH_AXIS;
			}
			return along >= 0.0 ? UNIT_LAUNCH_GUIDED : UNIT_LAUNCH_NONE;
		}
		if (exit && exit->fraction > 0.0 && exit->fraction < 1.0 && exit->speed > 0.0 &&
			along >= length * exit->fraction) {
			/* Late linear-speed changes retain native filtered roll impulse.
			 * Use actual position along the supported ramp, not guide lookahead. */
			plan->target_speed = fmin(exit->speed, route->limits.max_speed);
		}
		legacy_f64 lookahead =
			fmin(UNIT_LAUNCH_LOOKAHEAD_MAXIMUM,
				 UNIT_LAUNCH_LOOKAHEAD_BASE +
					 car->car_actual_speed / UNIT_SPEED_SCALE * UNIT_LAUNCH_LOOKAHEAD_SPEED_SCALE);
		legacy_f64 trim = route->verified_barrel_offsets && entry->route_index >= 0 &&
								  (legacy_u32)entry->route_index < route->verified_count
							  ? route->verified_barrel_offsets[entry->route_index]
							  : 0.0;
		if (entry->route_index == override_piece && policy->barrel_offset) {
			trim = *policy->barrel_offset;
		}
		/* Follow the local projected strip width, retaining fixed body clearance.
		 * Sampling stops at the supported lip, never an unsupported guide. */
		legacy_f64 half_width =
			unit_launch_width(route, first, last - 1U, dx, dz, fmin(length, along + lookahead));
		legacy_f64 offset =
			fmax(-1.0, fmin(1.0, trim)) *
			fmax(0.0, half_width - route->limits.half_width - UNIT_BARREL_LINE_CLEARANCE);
		legacy_f64 axis_x = entry->x + dz * offset, axis_z = entry->z - dx * offset;
		if (offset != 0.0 && along > -TRACK_TILE_SIZE && along <= -lookahead) {
			legacy_f64 blend = (along + TRACK_TILE_SIZE) / (TRACK_TILE_SIZE - lookahead);
			legacy_f64 current =
				(plan->target_x - entry->x) * dz - (plan->target_z - entry->z) * dx;
			plan->target_x += dz * (offset - current) * blend;
			plan->target_z -= dx * (offset - current) * blend;
		}
		if (along >= 0.0) {
			if (along < length && unit_launch_road_contact(car)) {
				/* Support-bank and pitch coupling can yaw a grounded barrel.
				 * Pursue its horizontal axis with projected steering authority. */
				plan->target_x = axis_x + dx * (along + lookahead);
				plan->target_y = car->car_position.ly / UNIT_POSITION_SCALE;
				plan->target_z = axis_z + dz * (along + lookahead);
				if (trim != 0.0 && along > length - lookahead) {
					/* Let the final supported heading point at the receiver instead
					 * of carrying a contracting strip's lateral tangent into flight. */
					legacy_f64 receiver_x = landing->x - x, receiver_z = landing->z - z;
					legacy_f64 distance = hypot(receiver_x, receiver_z);
					if (distance > lookahead) {
						legacy_f64 blend = (along - length + lookahead) / lookahead;
						plan->target_x +=
							(x + receiver_x * lookahead / distance - plan->target_x) * blend;
						plan->target_z +=
							(z + receiver_z * lookahead / distance - plan->target_z) * blend;
					}
				}
				return UNIT_LAUNCH_AXIS;
			}
			return UNIT_LAUNCH_NEUTRAL;
		}
		/* Preserve preceding bends, then advance the target past the entry. */
		if (along > -lookahead) {
			plan->target_x = axis_x + dx * (along + lookahead);
			plan->target_y = entry->y;
			plan->target_z = axis_z + dz * (along + lookahead);
			plan->route_cursor = first;
		}
		return UNIT_LAUNCH_NONE;
	}
	return 0;
}

legacy_int unit_launch_guidance_profile(const struct UNIT_ROUTE *route, legacy_u32 cursor,
										struct UNIT_PLAN *plan, legacy_s16 override_piece,
										legacy_f64 override_speed,
										const struct UNIT_EXIT_PROFILE *exit_override)
{
	const struct UNIT_LAUNCH_POLICY policy = {override_piece, -1,  override_speed, 0.0,
											  exit_override,  NULL};
	return unit_launch_guidance_policy(route, cursor, plan, &policy);
}

legacy_int unit_launch_guidance(const struct UNIT_ROUTE *route, legacy_u32 cursor,
								struct UNIT_PLAN *plan, legacy_s16 override_piece,
								legacy_f64 override_speed)
{
	return unit_launch_guidance_profile(route, cursor, plan, override_piece, override_speed, NULL);
}

#endif
