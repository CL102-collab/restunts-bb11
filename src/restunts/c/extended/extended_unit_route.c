#include "../opponent/unit_opponent_internal.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../externs.h"
#include "extended_geometry.h"
#include "extended_opponent.h"
#include "../track/track_objects.h"
#include "../track/trackdata_layout.h"
#include "../crash_state.h"
#include "roll_ramp.h"
#include "extended_unit_checkpoint.h"
#include "../track/track_collision.h"
#include "../opponent/unit_route_graph.h"
#include "../opponent/unit_shortcut.h"
#include "../owoot/owoot.h"

#define UNIT_MINIMUM_DIMENSION 12.0
#define UNIT_MINIMUM_WHEELBASE 24.0
#define UNIT_MINIMUM_GRIP 1.0
#define UNIT_MINIMUM_ACCELERATION 1.0
#define UNIT_MINIMUM_BRAKING 2.0
#define UNIT_MAXIMUM_SPEED 255.0
#define UNIT_MINIMUM_SPEED_LIMIT 20.0
#define UNIT_REACHED_DISTANCE 64.0
#define UNIT_ROUTE_HALF_WIDTH_MINIMUM 16.0
#define UNIT_ROUTE_GATE_MARGIN 64.0
#define UNIT_STUNT_GATE_MARGIN 192.0
#define UNIT_FINISH_HEIGHT_TOLERANCE 128.0
#define UNIT_NATIVE_GRIP_SCALE 128.0
#define UNIT_NATIVE_TORQUE_SCALE 16.0
#define UNIT_NATIVE_MASS_FACTOR 12.5
#define UNIT_NATIVE_CURVATURE_GRIP (3.14159265358979323846 / 4.0)
#define UNIT_GEOMETRY_EPSILON 0.000001
#define UNIT_CAPACITY_GROWTH 2U
#define UNIT_FINISH_GUIDE_PIECES 4U
#define UNIT_OPENING_GUIDE_PIECES 4U
#define UNIT_STUNT_CORKSCREW_SPEED 105.0
#define UNIT_STUNT_OTHER_SPEED 105.0
#define UNIT_STUNT_PIPE_SPEED 105.0
#define UNIT_BANKED_ROAD_SPEED 80.0
#define UNIT_ELEVATED_CORNER_SPEED 80.0
#define UNIT_STUNT_CORKSCREW_FLOOR 70.0
#define UNIT_STUNT_OTHER_FLOOR 90.0
#define UNIT_BARREL_DEFAULT_SPEED 100.0
#define UNIT_RAMP_JUMP_SPEED 110.0
#define UNIT_BARREL_LAUNCH_GRADE_RESERVE 0.5
#define UNIT_SLALOM_APPROACH_PIECES 2U
#define UNIT_STUNT_ALIGNMENT_PIECES 2U
#define UNIT_NATIVE_GRAVITY (18.0 * 20.0 * 20.0 / UNIT_POSITION_SCALE)
#define UNIT_SLALOM_GUIDANCE_SPEED 90.0

legacy_f64 unit_bounded(legacy_f64 value, legacy_f64 minimum, legacy_f64 maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

static void unit_route_car_limits(struct UNIT_ROUTE *route)
{
	struct UNIT_CAR_LIMITS *limits = &route->limits;
	const struct SIMD *car = &simd_opponent;
	memset(limits, 0, sizeof(*limits));
	/* The second collision pair stores length and the broad-phase radius;
	 * it is not another corner of the physical rectangle. */
	limits->half_width = fabs((legacy_f64)car->collide_points[0].px);
	limits->half_length = fabs((legacy_f64)car->collide_points[1].px);
	for (legacy_u16 index = 0; index < CARSTATE_WHEEL_COUNT; index++) {
		limits->half_width =
			fmax(limits->half_width, fabs(car->wheel_coords[index].x / UNIT_POSITION_SCALE));
		limits->half_length =
			fmax(limits->half_length, fabs(car->wheel_coords[index].z / UNIT_POSITION_SCALE));
	}
	limits->half_width = fmax(limits->half_width, UNIT_MINIMUM_DIMENSION);
	limits->half_length = fmax(limits->half_length, UNIT_MINIMUM_DIMENSION);
	route->wheelbase = fmax(fabs((legacy_f64)car->wheel_coords[0].z + car->wheel_coords[1].z -
								 car->wheel_coords[2].z - car->wheel_coords[3].z) /
								(2.0 * UNIT_POSITION_SCALE),
							UNIT_MINIMUM_WHEELBASE);
	limits->wheelbase = route->wheelbase;
	legacy_u16 top_gear = (legacy_u16)unit_bounded(car->num_gears, 1, SIMD_GEAR_RATIO_COUNT - 1);
	legacy_u16 ratio = car->gear_ratios[top_gear];
	limits->max_speed = ratio ? unit_bounded(car->max_rpm * UNIT_SPEED_SCALE / ratio,
											 UNIT_MINIMUM_SPEED_LIMIT, UNIT_MAXIMUM_SPEED)
							  : UNIT_MAXIMUM_SPEED;
	limits->braking =
		fmax(car->braking_eff * GAME_FRAME_RATE_NORMAL / UNIT_SPEED_SCALE, UNIT_MINIMUM_BRAKING);
	legacy_u16 torque = 0;
	for (legacy_u16 index = 0; index < SIMD_TORQUE_CURVE_SIZE; index++) {
		if ((legacy_u8)car->torque_curve[index] > torque) {
			torque = (legacy_u8)car->torque_curve[index];
		}
	}
	legacy_f64 mass = fmax(car->car_mass, 1.0);
	legacy_f64 second_ratio = car->gear_ratios[top_gear > 1U ? 2U : 1U] / UNIT_SPEED_SCALE;
	limits->acceleration =
		fmax(second_ratio * torque / UNIT_NATIVE_TORQUE_SCALE * UNIT_NATIVE_MASS_FACTOR / mass *
				 GAME_FRAME_RATE_NORMAL / UNIT_SPEED_SCALE,
			 UNIT_MINIMUM_ACCELERATION);
	legacy_f64 paved = fmax(car->surface_grip[CAR_SURFACE_PAVED - 1U], UNIT_MINIMUM_GRIP);
	legacy_f64 grip = fmax(car->grip * paved / UNIT_NATIVE_GRIP_SCALE, UNIT_MINIMUM_GRIP);
	limits->lateral_acceleration =
		grip * UNIT_NATIVE_CURVATURE_GRIP / route->wheelbase * UNIT_WORLD_PER_MPH;
	for (legacy_u16 surface = CAR_SURFACE_PAVED; surface <= CAR_SURFACE_GRASS; surface++) {
		limits->surface_grip[surface] =
			fmax(car->surface_grip[surface - 1U], UNIT_MINIMUM_GRIP) / paved;
	}
}

static const struct TRKOBJINFO *unit_route_info(legacy_s16 piece)
{
	legacy_u8 id = (legacy_u8)track_route_element_ids[piece];
	const struct TRKOBJINFO *blocks =
		extended_opponent_route_info(piece, id, trkObjectList[id].ss_trkObjInfoPtr);
	return blocks ? &blocks[(legacy_u8)track_route_traversal_flags[piece] & UNIT_ROUTE_SUBTYPE_MASK]
				  : NULL;
}

static legacy_int unit_route_reserve(struct UNIT_ROUTE *route)
{
	if (route->count < route->capacity) {
		return 1;
	}
	legacy_u32 capacity =
		route->capacity ? route->capacity * UNIT_CAPACITY_GROWTH : UNIT_ROUTE_INITIAL_CAPACITY;
	if (capacity > UNIT_ROUTE_MAXIMUM_POINTS) {
		capacity = UNIT_ROUTE_MAXIMUM_POINTS;
	}
	if (capacity <= route->count) {
		return 0;
	}
	struct UNIT_ROUTE_POINT *points = realloc(route->points, capacity * sizeof(*points));
	if (!points) {
		return 0;
	}
	route->points = points;
	route->capacity = capacity;
	return 1;
}

static legacy_int unit_route_append(struct UNIT_ROUTE *route, legacy_s16 piece,
									legacy_u16 point_index)
{
	if (!unit_route_reserve(route)) {
		return 0;
	}
	/* The native routine also writes both edges and a trailing flag. CARSTATE
	 * provides the documented contiguous destination rather than a single VECTOR. */
	struct CARSTATE sample;
	memset(&sample, 0, sizeof(sample));
	get_track_route_point(piece, &sample.car_route_target, (legacy_s16)point_index, NULL);
	legacy_s32 origin_x =
		(legacy_u8)track_route_columns[piece] * TRACK_TILE_SIZE + TRACK_TILE_HALF_SIZE;
	legacy_s32 origin_z =
		(extended_track_height() - (legacy_u8)track_route_rows[piece] - 1) * TRACK_TILE_SIZE +
		TRACK_TILE_HALF_SIZE;
	struct UNIT_ROUTE_POINT *point = &route->points[route->count++];
	memset(point, 0, sizeof(*point));
	point->x = extended_geometry_unwrap(sample.car_route_target.x, origin_x);
	point->z = extended_geometry_unwrap(sample.car_route_target.z, origin_z);
	point->y = sample.car_route_target.y;
	point->height_unspecified = sample.car_route_target.y == ROUTE_POINT_HEIGHT_UNSPECIFIED;
	if (point->height_unspecified) {
		if (extended_track_active()) {
			point->y = extended_track_elevation((legacy_u8)track_route_columns[piece],
												(legacy_u8)track_route_rows[piece],
												TRACK_TILE_HALF_SIZE, TRACK_TILE_HALF_SIZE);
		} else {
			legacy_u16 cell = terrainrows[(legacy_u8)track_route_rows[piece]] +
							  (legacy_u8)track_route_columns[piece];
			point->y = track_terrain_map[cell] == TERRAIN_RAISED_TILE
						   ? hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX]
						   : 0;
		}
	}
	legacy_f64 dx =
		LEGACY_S16_WRAP_SUB(sample.car_route_first_edge.x, sample.car_route_second_edge.x);
	legacy_f64 dz =
		LEGACY_S16_WRAP_SUB(sample.car_route_first_edge.z, sample.car_route_second_edge.z);
	point->half_width = fmax(hypot(dx, dz) / 2.0, UNIT_ROUTE_HALF_WIDTH_MINIMUM);
	point->speed_limit = route->limits.max_speed;
	point->route_index = piece;
	point->route_point = point_index;
	legacy_u8 id = (legacy_u8)track_route_element_ids[piece];
	legacy_s16 model = trkObjectList[id].ss_physicalModel;
	point->surface = (legacy_u8)(trkObjectList[id].ss_surfaceType + CAR_SURFACE_PAVED);
	if (extended_track_active()) {
		const struct CSTRK_OBJECT *object = extended_track_object_for_id(
			(legacy_u8)track_route_columns[piece], (legacy_u8)track_route_rows[piece], id);
		if (object && object->surface <= CSTRK_ROAD_ICE) {
			point->surface = object->surface + CAR_SURFACE_PAVED;
		}
		if (object && object->roll_ramp) {
			point->stunt = UNIT_STUNT_BARREL;
			point->speed_limit = UNIT_BARREL_DEFAULT_SPEED;
			point->speed_floor = UNIT_BARREL_DEFAULT_SPEED;
			if (point_index >= ROLL_RAMP_SUPPORTED_ROUTE_POINT_COUNT) {
				point->height_unspecified = 1;
			}
		} else if (object && point->height_unspecified &&
				   (model <= PHYSICAL_MODEL_CROSSROAD || model == PHYSICAL_MODEL_SLALOM ||
					(model >= PHYSICAL_MODEL_ELEVATED_ROAD &&
					 model <= PHYSICAL_MODEL_ELEVATED_CORNER))) {
			struct VECTOR origin;
			extended_geometry_object_origin(extended_track_model(), object, &origin);
			legacy_f64 center_x = (object->x + object->width * 0.5) * TRACK_TILE_SIZE;
			legacy_f64 center_z =
				(extended_track_height() - object->y - object->height * 0.5) * TRACK_TILE_SIZE;
			point->y = origin.y +
					   extended_geometry_support_grade(object, (legacy_s16)(point->x - center_x),
													   (legacy_s16)(point->z - center_z));
			if (model >= PHYSICAL_MODEL_ELEVATED_ROAD && model <= PHYSICAL_MODEL_ELEVATED_CORNER) {
				point->y += hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX];
			}
			point->height_unspecified = 0;
		}
	}
	if (!point->stunt && (model == PHYSICAL_MODEL_PIPE || model == PHYSICAL_MODEL_HALF_PIPE)) {
		/* The native guide can run along a banked pipe wall. Its horizontal
		 * edge projection is not a flat road corridor or the car's banked
		 * clearance. Preserve the ordered 3-D guide and native validation;
		 * unlike a loop, a pipe has no mandatory sustaining speed. */
		point->stunt = UNIT_STUNT_PROTECTED;
		point->speed_limit = UNIT_STUNT_PIPE_SPEED;
	} else if (!point->stunt &&
			   (model == PHYSICAL_MODEL_LOOP || model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A ||
				model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B ||
				model == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT)) {
		point->stunt = UNIT_STUNT_PROTECTED;
		point->speed_limit = model == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT
								 ? UNIT_STUNT_CORKSCREW_SPEED
								 : UNIT_STUNT_OTHER_SPEED;
		point->speed_floor = model == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT
								 ? UNIT_STUNT_CORKSCREW_FLOOR
								 : UNIT_STUNT_OTHER_FLOOR;
	}
	if (model >= PHYSICAL_MODEL_BANKED_ENTRANCE_B && model <= PHYSICAL_MODEL_BANKED_CORNER) {
		/* Fresh live guidance reserves a centered, slower banked approach.
		 * Complete native previews retain the original speed envelope and
		 * prove their actual receiving contact before caching controls. */
		point->fallback_speed_limit = fmin(point->speed_limit, UNIT_BANKED_ROAD_SPEED);
		point->align_center |= UNIT_ALIGN_FALLBACK;
	}
	if (model == PHYSICAL_MODEL_ELEVATED_CORNER) {
		/* Reserve live steering and braking time before an elevated bend,
		 * while native-validated trajectories keep their proven speeds. */
		point->fallback_speed_limit = fmin(point->speed_limit, UNIT_ELEVATED_CORNER_SPEED);
	}
	if (model == PHYSICAL_MODEL_SLALOM) {
		/* Keep the native alternating pole-avoidance line. The narrow center
		 * gap leaves too little tolerance for steering lag or a banked landing. */
		point->speed_limit = UNIT_SLALOM_GUIDANCE_SPEED;
	}

	if (point->surface < CAR_SURFACE_PAVED || point->surface > CAR_SURFACE_GRASS) {
		point->surface = CAR_SURFACE_PAVED;
	}
	/* Widening is enabled only after the complete route's neighboring samples
	 * pass the native footprint proof. This never authorizes skipping pieces. */
	point->shortcut_safe = 0;
	return 1;
}

static void unit_route_slalom_approaches(struct UNIT_ROUTE *route)
{
	for (legacy_u32 index = 0; index < route->finish_begin; index++) {
		legacy_u8 id = (legacy_u8)track_route_element_ids[route->points[index].route_index];
		if (trkObjectList[id].ss_physicalModel != PHYSICAL_MODEL_SLALOM ||
			(index && route->points[index - 1U].route_index == route->points[index].route_index)) {
			continue;
		}
		/* Reach weaving speed before an uphill approach launches the car;
		 * air braking cannot shorten its ballistic trajectory. */
		legacy_u32 previous = index, pieces = 0;
		while (previous) {
			previous--;
			if (route->points[previous].route_index != route->points[previous + 1U].route_index &&
				++pieces > UNIT_SLALOM_APPROACH_PIECES) {
				break;
			}
			if (route->points[previous].stunt == UNIT_STUNT_NONE) {
				route->points[previous].speed_limit =
					fmin(route->points[previous].speed_limit, UNIT_SLALOM_GUIDANCE_SPEED);
			}
		}
	}
}

static void unit_route_align_approaches(struct UNIT_ROUTE *route)
{
	for (legacy_u32 index = 0; index < route->count; index++) {
		struct UNIT_ROUTE_POINT *point = &route->points[index];
		legacy_u8 id = (legacy_u8)track_route_element_ids[point->route_index];
		if (!point->stunt && trkObjectList[id].ss_physicalModel != PHYSICAL_MODEL_SLALOM) {
			continue;
		}
		point->align_center = UNIT_ALIGN_MANEUVER;
		if (index && route->points[index - 1U].route_index == point->route_index) {
			continue;
		}
		legacy_u32 previous = index, pieces = 0;
		while (previous) {
			previous--;
			if (route->points[previous].route_index != route->points[previous + 1U].route_index &&
				++pieces > UNIT_STUNT_ALIGNMENT_PIECES) {
				break;
			}
			route->points[previous].align_center = UNIT_ALIGN_MANEUVER;
		}
	}
}

static void unit_route_ramp_jumps(struct UNIT_ROUTE *route)
{
	for (legacy_u32 first = 0; first < route->finish_begin; first++) {
		legacy_u8 id = (legacy_u8)track_route_element_ids[route->points[first].route_index];
		legacy_s16 model = trkObjectList[id].ss_physicalModel;
		if (model != PHYSICAL_MODEL_RAMP && model != PHYSICAL_MODEL_SOLID_RAMP) {
			continue;
		}
		legacy_u32 last = first;
		while (last + 1U < route->count &&
			   route->points[last + 1U].route_index == route->points[first].route_index) {
			last++;
		}
		if (last + 1U < route->count &&
			hypot(route->points[last + 1U].x - route->points[last].x,
				  route->points[last + 1U].z - route->points[last].z) > TRACK_TILE_SIZE) {
			for (legacy_u32 point = first; point <= last; point++) {
				route->points[point].stunt = UNIT_STUNT_RAMP;
				route->points[point].speed_floor = UNIT_RAMP_JUMP_SPEED;
				route->points[point].speed_limit = UNIT_RAMP_JUMP_SPEED;
			}
		}
		first = last;
	}
}

static void unit_route_barrel_speeds(struct UNIT_ROUTE *route)
{
	for (legacy_u32 first = 0; first < route->count; first++) {
		if (route->points[first].stunt != UNIT_STUNT_BARREL) {
			continue;
		}
		legacy_u32 last = first;
		while (last + 1U < route->count &&
			   route->points[last + 1U].route_index == route->points[first].route_index) {
			last++;
		}
		if (last > first + 1U && last + 1U < route->count) {
			const struct UNIT_ROUTE_POINT *lip = &route->points[last - 1U];
			const struct UNIT_ROUTE_POINT *before = &route->points[last - 2U];
			const struct UNIT_ROUTE_POINT *landing = &route->points[last + 1U];
			legacy_f64 dx = lip->x - before->x, dz = lip->z - before->z;
			legacy_f64 length = hypot(dx, dz);
			if (length > UNIT_GEOMETRY_EPSILON) {
				legacy_f64 distance =
					((landing->x - lip->x) * dx + (landing->z - lip->z) * dz) / length;
				legacy_f64 slope = (lip->y - before->y) / length * UNIT_BARREL_LAUNCH_GRADE_RESERVE;
				legacy_f64 drop = distance * slope - (landing->y - lip->y);
				if (distance > 0.0 && drop > 0.0) {
					/* Ballistic range to the receiving road, using native 18/64
					 * units of gravity per 20-Hz frame squared. */
					legacy_f64 speed = sqrt(UNIT_NATIVE_GRAVITY * distance * distance *
											(1.0 + slope * slope) / (2.0 * drop)) /
									   UNIT_WORLD_PER_MPH;
					/* Native suspension changes launch pitch with speed and roll.
					 * Prefer the ghost driver's 100-mph starting recipe when it
					 * lies inside the geometric launch-speed neighborhood. */
					speed = unit_bounded(UNIT_BARREL_DEFAULT_SPEED, speed * 0.90, speed * 1.10);
					speed = unit_bounded(speed, UNIT_MINIMUM_SPEED_LIMIT, route->limits.max_speed);
					for (legacy_u32 point = first; point <= last; point++) {
						route->points[point].speed_limit = speed;
						route->points[point].speed_floor = speed;
						route->points[point].height_unspecified = 1;
					}
				}
			}
		}
		first = last;
	}
}

legacy_int unit_route_build_opening(struct UNIT_ROUTE *route)
{
	unit_route_free(route);
	unit_route_car_limits(route);
	if (track_pieces_counter <= 0 || !opponent_route_track_indices) {
		return 0;
	}
	for (legacy_u32 index = 0; index < UNIT_OPENING_GUIDE_PIECES; index++) {
		legacy_s16 piece = LEGACY_READ_S16_LE((const legacy_u8 *)opponent_route_track_indices +
											  index * LEGACY_WORD_BYTES);
		if (piece < 0 || piece >= track_pieces_counter || (index && piece == 0)) {
			break;
		}
		const struct TRKOBJINFO *info = unit_route_info(piece);
		if (!info || !info->route_vectors || info->route_point_count <= 0) {
			unit_route_free(route);
			return 0;
		}
		for (legacy_u16 point = 0; point < (legacy_u8)info->route_point_count; point++) {
			if (!unit_route_append(route, piece, point)) {
				unit_route_free(route);
				return 0;
			}
		}
	}
	route->finish_begin = route->count;
	unit_route_slalom_approaches(route);
	unit_route_align_approaches(route);
	unit_checkpoint_align_route(route);
	unit_shortcut_mark_route(route->points, route->count, &route->limits, owoot_enabled != 0);
	return route->count >= 2U;
}

legacy_int unit_route_build(struct UNIT_ROUTE *route)
{
	unit_route_free(route);
	unit_route_car_limits(route);
	legacy_s16 *path = NULL;
	legacy_u32 count = 0;
	if (!unit_route_graph_build(&path, &count)) {
		return 0;
	}
	for (legacy_u32 index = 0; index < count; index++) {
		legacy_s16 piece = path[index];
		if (piece < 0 || piece >= track_pieces_counter) {
			break;
		}
		const struct TRKOBJINFO *info = unit_route_info(piece);
		if (!info || !info->route_vectors || info->route_point_count <= 0) {
			break;
		}
		if (index && piece == 0) {
			route->finish_begin = route->count;
		}
		for (legacy_u16 point = 0; point < (legacy_u8)info->route_point_count; point++) {
			if (!unit_route_append(route, piece, point)) {
				free(path);
				unit_route_free(route);
				return 0;
			}
		}
		if (index && piece == 0 && route->count > 1U) {
			/* A driving horizon beyond the timing line avoids braking before it. */
			for (legacy_u32 continuation = 1U;
				 continuation < count - 1U && continuation <= UNIT_FINISH_GUIDE_PIECES;
				 continuation++) {
				legacy_s16 next_piece = path[continuation];
				const struct TRKOBJINFO *next_info = unit_route_info(next_piece);
				if (!next_info || next_info->route_point_count <= 0) {
					break;
				}
				for (legacy_u16 point = 0; point < (legacy_u8)next_info->route_point_count;
					 point++) {
					if (!unit_route_append(route, next_piece, point)) {
						free(path);
						unit_route_free(route);
						return 0;
					}
				}
			}
			free(path);
			route->verified_count = (legacy_u32)track_pieces_counter;
			route->verified_speeds = calloc(route->verified_count, sizeof(*route->verified_speeds));
			route->verified_exits = calloc(route->verified_count, sizeof(*route->verified_exits));
			route->verified_barrel_offsets =
				calloc(route->verified_count, sizeof(*route->verified_barrel_offsets));
			route->verified_ramp_approaches =
				calloc(route->verified_count, sizeof(*route->verified_ramp_approaches));
			if (!route->verified_speeds || !route->verified_exits ||
				!route->verified_barrel_offsets || !route->verified_ramp_approaches) {
				unit_route_free(route);
				return 0;
			}
			unit_route_barrel_speeds(route);
			unit_route_ramp_jumps(route);
			unit_route_slalom_approaches(route);
			unit_route_align_approaches(route);
			unit_checkpoint_align_route(route);
			unit_shortcut_mark_route(route->points, route->count, &route->limits,
									 owoot_enabled != 0);
			return 1;
		}
	}
	free(path);
	unit_route_free(route);
	return 0;
}

void unit_route_free(struct UNIT_ROUTE *route)
{
	free(route->points);
	free(route->verified_speeds);
	free(route->verified_exits);
	free(route->verified_barrel_offsets);
	free(route->verified_ramp_approaches);
	memset(route, 0, sizeof(*route));
}

legacy_u32 unit_route_cursor(const struct UNIT_ROUTE *route, const struct CARSTATE *car)
{
	legacy_u32 first = car->car_lap_count ? route->finish_begin : 0;
	for (legacy_u32 cursor = first; cursor < route->count; cursor++) {
		if (route->points[cursor].route_index == car->car_route_index &&
			route->points[cursor].route_point == (legacy_u8)car->car_route_point_index) {
			return cursor;
		}
	}
	return first;
}

legacy_u32 unit_route_advance(const struct UNIT_ROUTE *route, const struct CARSTATE *car,
							  legacy_u32 cursor)
{
	legacy_f64 x = car->car_position.lx / UNIT_POSITION_SCALE;
	legacy_f64 y = car->car_position.ly / UNIT_POSITION_SCALE;
	legacy_f64 z = car->car_position.lz / UNIT_POSITION_SCALE;
	while (cursor + 1U < route->count) {
		const struct UNIT_ROUTE_POINT *point = &route->points[cursor];
		legacy_f64 dx = x - point->x, dz = z - point->z;
		/* Ordinary roads may be crossed during a native legal jump. Their
		 * support height must not trap progress behind an airborne car. Only
		 * ordered loop/corkscrew gates need the third dimension; native penalty,
		 * OWOOT and checkpoint observers retain authority over actual motion. */
		legacy_int ordered_height =
			point->stunt == UNIT_STUNT_PROTECTED && !point->height_unspecified;
		legacy_f64 dy = ordered_height ? y - point->y : 0.0;
		if (hypot(dx, dz) < UNIT_REACHED_DISTANCE && fabs(dy) < UNIT_FINISH_HEIGHT_TOLERANCE) {
			if (!unit_checkpoint_can_advance(route, cursor)) {
				break;
			}
			cursor++;
			continue;
		}
		const struct UNIT_ROUTE_POINT *previous = cursor ? &route->points[cursor - 1U] : point;
		const struct UNIT_ROUTE_POINT *next = &route->points[cursor + 1U];
		legacy_f64 tx = cursor ? point->x - previous->x : next->x - point->x;
		legacy_f64 tz = cursor ? point->z - previous->z : next->z - point->z;
		legacy_f64 ty = !ordered_height ? 0.0
						: cursor		? point->y - previous->y
										: next->y - point->y;
		legacy_f64 length = sqrt(tx * tx + ty * ty + tz * tz);
		legacy_f64 along =
			length > UNIT_GEOMETRY_EPSILON ? (dx * tx + dy * ty + dz * tz) / length : 0;
		legacy_f64 cross = sqrt(fmax(0.0, dx * dx + dy * dy + dz * dz - along * along));
		/* A rolling car's body center is offset from its narrow road spline.
		 * Native tire contacts/OWOOT validate support; retain ordered 3-D
		 * crossing planes without trapping guidance behind a banked body. */
		legacy_f64 margin =
			point->stunt == UNIT_STUNT_PROTECTED ? UNIT_STUNT_GATE_MARGIN : UNIT_ROUTE_GATE_MARGIN;
		if (length > UNIT_GEOMETRY_EPSILON && along > 0 && cross < point->half_width + margin) {
			if (!unit_checkpoint_can_advance(route, cursor)) {
				break;
			}
			cursor++;
			continue;
		}
		break;
	}
	return cursor;
}

void unit_route_store(const struct UNIT_ROUTE *route, legacy_u32 cursor, struct CARSTATE *car)
{
	if (cursor >= route->count) {
		return;
	}
	const struct UNIT_ROUTE_POINT *point = &route->points[cursor];
	car->car_route_index = point->route_index;
	car->car_route_point_index = LEGACY_S8_FROM_BITS((legacy_u8)point->route_point);
	get_track_route_point(point->route_index, &car->car_route_target,
						  (legacy_s16)point->route_point, NULL);
	car->car_lap_count = cursor >= route->finish_begin;
}

legacy_int unit_route_finished(const struct UNIT_ROUTE *route, const struct CARSTATE *car)
{
	if (!route->count || !car->car_lap_count || car->car_crashBmpFlag != CRASH_EVENT_NONE) {
		return 0;
	}
	legacy_f64 center_x = (legacy_u8)start_finish_column * TRACK_TILE_SIZE + TRACK_TILE_HALF_SIZE;
	legacy_f64 center_z =
		(extended_track_height() - (legacy_u8)start_finish_row - 1) * TRACK_TILE_SIZE +
		TRACK_TILE_HALF_SIZE;
	legacy_f64 sine = sin(track_angle * UNIT_ANGLE_RADIANS);
	legacy_f64 cosine = cos(track_angle * UNIT_ANGLE_RADIANS);
	legacy_f64 x = car->car_position.lx / UNIT_POSITION_SCALE - center_x;
	legacy_f64 z = car->car_position.lz / UNIT_POSITION_SCALE - center_z;
	legacy_f64 px = car->car_previous_position.lx / UNIT_POSITION_SCALE - center_x;
	legacy_f64 pz = car->car_previous_position.lz / UNIT_POSITION_SCALE - center_z;
	legacy_f64 width = route->points[route->finish_begin].half_width;
	return px * sine + pz * cosine <= 0 && x * sine + z * cosine > 0 &&
		   fabs(x * cosine - z * sine) <= width &&
		   fabs(car->car_position.ly / UNIT_POSITION_SCALE - route->points[route->finish_begin].y) <
			   UNIT_FINISH_HEIGHT_TOLERANCE;
}
#endif
