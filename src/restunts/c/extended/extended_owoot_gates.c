/* Build and sweep finite driving portals from authored route geometry. */
#include "extended_owoot_route.h"
#include "../owoot/owoot_route.h"
#include "../owoot/owoot.h"
#include "../owoot/owoot_road.h"
#include "../externs.h"
#include "../track/track_objects.h"
#include "../track/track_collision.h"
#include "../track/trackdata_layout.h"
#include "extended_track.h"
#ifdef RESTUNTS_SDL3
#include "extended_geometry.h"
#include "roll_ramp.h"
#include "../physics/physics_internal.h"
#endif

#include "extended_owoot_internal.h"

/* Gate construction uses the authored geometry. Its plane constraints are
 * transformed together after the intrinsic tube, loop or corkscrew shape. */
static void route_geometry_points(legacy_s16 piece, struct VECTOR *output, legacy_s16 index,
								  legacy_s8 *optional_speed)
{
	get_track_route_point(piece, output, index, optional_speed);
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		const struct CSTRK_OBJECT *placed = extended_track_object_for_id(
			(legacy_u8)track_route_columns[piece], (legacy_u8)track_route_rows[piece],
			(legacy_u8)track_route_element_ids[piece]);
		if (extended_geometry_uses_grade(placed)) {
			struct VECTOR origin;
			extended_geometry_object_origin(extended_track_model(), placed, &origin);
			/* The fourth storage slot contains a flag, not a route vector. */
			for (legacy_u16 point = 0; point < OWOOT_ROUTE_VECTOR_COUNT; point++) {
				output[point].y = LEGACY_S16_WRAP_SUB(
					output[point].y, extended_geometry_object_grade(
										 placed, LEGACY_S16_WRAP_SUB(output[point].x, origin.x),
										 LEGACY_S16_WRAP_SUB(output[point].z, origin.z)));
			}
		}
	}
#endif
}

#ifdef RESTUNTS_SDL3
const struct CSTRK_OBJECT *route_extended_object(legacy_s16 piece)
{
	return extended_track_object_for_id((legacy_u8)track_route_columns[piece],
										(legacy_u8)track_route_rows[piece],
										(legacy_u8)track_route_element_ids[piece]);
}

struct OWOOT_POINT route_extended_origin(const struct CSTRK_OBJECT *placed)
{
	struct VECTOR wrapped;
	extended_geometry_object_origin(extended_track_model(), placed, &wrapped);
	struct OWOOT_POINT origin = {placed->x * 1024L + placed->width * 512L, wrapped.y,
								 extended_track_height() * 1024L - placed->y * 1024L -
									 placed->height * 512L};
	return origin;
}

static struct OWOOT_POINT route_extended_unplace(const struct OWOOT_POINT *point,
												 const struct OWOOT_POINT *origin)
{
	struct OWOOT_POINT result = {(legacy_s16)(point->x - origin->x),
								 (legacy_s16)(point->y - origin->y),
								 (legacy_s16)(point->z - origin->z)};
	return result;
}

static void route_extended_place_gate(struct OWOOT_GATE *gate, const struct OWOOT_POINT *origin)
{
	gate->center.x += origin->x;
	gate->center.y += origin->y;
	gate->center.z += origin->z;
}

static legacy_s16 route_extended_tube_gate(legacy_s16 piece, legacy_s16 index, legacy_s16 count,
										   struct OWOOT_GATE *gate)
{
	const struct CSTRK_OBJECT *placed = route_extended_object(piece);
	if (!placed) {
		return 0;
	}
	legacy_s16 model =
		placed->tunnel_slalom ? PHYSICAL_MODEL_TUNNEL : route_object(piece)->ss_physicalModel;
	legacy_s32 height, half_width;
	if (model == PHYSICAL_MODEL_TUNNEL) {
		height = TUNNEL_HEIGHT;
		half_width = ROAD_HALF_WIDTH;
	} else if (model == PHYSICAL_MODEL_PIPE || model == PHYSICAL_MODEL_HALF_PIPE) {
		height = OWOOT_PIPE_HEIGHT;
		half_width = OWOOT_PIPE_HALF_WIDTH;
	} else if (model == PHYSICAL_MODEL_PIPE_ENTRANCE) {
		height = OWOOT_PIPE_ENTRANCE_HEIGHT;
		half_width = OWOOT_PIPE_ENTRANCE_HALF_WIDTH;
	} else {
		return 0;
	}
	struct OWOOT_POINT origin = route_extended_origin(placed);
	gate->center = route_extended_unplace(&gate->center, &origin);
	struct OWOOT_POINT zero = {0, 0, 0};
	gate->tangent = route_extended_unplace(&gate->tangent, &zero);
	gate->tangent.y = 0;
	if (route_abs(gate->tangent.x) > route_abs(gate->tangent.z)) {
		gate->tangent.z = 0;
		gate->center.z = 0;
	} else {
		gate->tangent.x = 0;
		gate->center.x = 0;
	}
	route_normalize(&gate->tangent);
	gate->center.y = model == PHYSICAL_MODEL_TUNNEL ? height / 2 : 0;
	gate->edge = (struct OWOOT_POINT){gate->tangent.z, 0, -gate->tangent.x};
	gate->normal = (struct OWOOT_POINT){0, OWOOT_GATE_SCALE, 0};
	gate->lateral_limit = half_width * OWOOT_GATE_SCALE;
	gate->normal_limit = height / 2 * OWOOT_GATE_SCALE;
	gate->kind = model == PHYSICAL_MODEL_TUNNEL ? OWOOT_GATE_TUNNEL : OWOOT_GATE_PIPE;
	if (model == PHYSICAL_MODEL_PIPE_ENTRANCE) {
		gate->kind = OWOOT_GATE_ROAD_SURFACE;
		gate->normal_limit = 0;
	} else if (index == 0 || index == count - 1) {
		legacy_s32 sign = index == count - 1 ? 1 : -1;
		if (gate->tangent.x) {
			gate->center.x = gate->tangent.x * sign > 0 ? 512 : -512;
		} else {
			gate->center.z = gate->tangent.z * sign > 0 ? 512 : -512;
		}
	}
	route_extended_place_gate(gate, &origin);
	return 1;
}

static legacy_s16 route_extended_corkscrew_gate(legacy_s16 piece, struct OWOOT_GATE *gate)
{
	const struct CSTRK_OBJECT *placed = route_extended_object(piece);
	if (!placed) {
		return 0;
	}
	struct OWOOT_POINT origin = route_extended_origin(placed);
	struct OWOOT_POINT first = {0, 0, 0}, last = {0, 0, 0};
	legacy_s32 highest = 0;
	legacy_s16 count = (legacy_u8)route_info(piece)->route_point_count;
	if (count < 2) {
		return 0;
	}
	struct VECTOR section[ROUTE_GEOMETRY_POINT_COUNT];
	for (legacy_s16 point = 0; point < count; point++) {
		route_geometry_points(piece, section, (legacy_s16)point, 0);
		struct OWOOT_POINT world = route_vector(&section[0]);
		struct OWOOT_POINT local = route_extended_unplace(&world, &origin);
		if (!point) {
			first = local;
			highest = local.y;
		}
		if (point == count - 1) {
			last = local;
		}
		if (local.y > highest) {
			highest = local.y;
		}
	}
	struct OWOOT_POINT axis = {(first.x + last.x) / 2, (first.y + highest) / 2,
							   (first.z + last.z) / 2};
	struct OWOOT_POINT local_center = route_extended_unplace(&gate->center, &origin);
	struct OWOOT_POINT radial = route_delta(&local_center, &axis);
	struct OWOOT_POINT zero = {0, 0, 0}, tangent = {0, 0, 0};
	struct OWOOT_POINT preferred = route_extended_unplace(&gate->tangent, &zero);
	if (route_abs(last.x - first.x) > route_abs(last.z - first.z)) {
		radial.x = 0;
		tangent.y = -radial.z;
		tangent.z = radial.y;
		gate->edge = (struct OWOOT_POINT){OWOOT_GATE_SCALE, 0, 0};
	} else {
		radial.z = 0;
		tangent.x = -radial.y;
		tangent.y = radial.x;
		gate->edge = (struct OWOOT_POINT){0, 0, OWOOT_GATE_SCALE};
	}
	if (route_dot(&tangent, &preferred) < 0) {
		tangent.x = -tangent.x;
		tangent.y = -tangent.y;
		tangent.z = -tangent.z;
	}
	gate->center = axis;
	gate->tangent = tangent;
	gate->normal = radial;
	route_normalize(&gate->tangent);
	route_normalize(&gate->normal);
	gate->lateral_limit = OWOOT_TILE_SIZE * OWOOT_GATE_SCALE;
	gate->normal_limit = 0;
	gate->kind = OWOOT_GATE_CORKSCREW_PHASE;
	route_extended_place_gate(gate, &origin);
	return 1;
}
#endif

static legacy_s16 route_tube_gate(legacy_s16 piece, struct OWOOT_GATE *gate)
{
	legacy_s16 model = route_object(piece)->ss_physicalModel;
	legacy_s32 height, half_width;
	if (model == PHYSICAL_MODEL_TUNNEL) {
		height = TUNNEL_HEIGHT;
		half_width = ROAD_HALF_WIDTH;
	} else if (model == PHYSICAL_MODEL_PIPE || model == PHYSICAL_MODEL_HALF_PIPE) {
		height = OWOOT_PIPE_HEIGHT;
		half_width = OWOOT_PIPE_HALF_WIDTH;
	} else if (model == PHYSICAL_MODEL_PIPE_ENTRANCE) {
		height = OWOOT_PIPE_ENTRANCE_HEIGHT;
		half_width = OWOOT_PIPE_ENTRANCE_HALF_WIDTH;
	} else {
		return 0;
	}
	legacy_u8 column = (legacy_u8)track_route_columns[piece];
	legacy_u8 row = (legacy_u8)track_route_rows[piece];
	legacy_s32 elevation = track_terrain_map[terrainrows[row] + column] == TERRAIN_RAISED_TILE
							   ? hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX]
							   : 0;
	const struct TRACKOBJECT *object = route_object(piece);
	/* The physical tube is cardinal even when its preferred steering line
	 * shifts sideways between guidance points. */
	gate->tangent.y = 0;
	if (route_abs(gate->tangent.x) > route_abs(gate->tangent.z)) {
		gate->tangent.z = 0;
	} else {
		gate->tangent.x = 0;
	}
	/* Pipe guidance vectors run along one wall. Any lane through the actual
	 * tube is valid; its physical cross-section, including the ceiling, is the
	 * traversal gate rather than that preferred opponent line. */
	if (gate->tangent.x == 0) {
		gate->center.x = track_object_base_x(object, column);
	} else {
		gate->center.z = track_object_base_z(object, row);
	}
	gate->center.y = elevation + height / 2;
	gate->tangent.y = 0;
	route_normalize(&gate->tangent);
	gate->edge.x = gate->tangent.z;
	gate->edge.y = 0;
	gate->edge.z = -gate->tangent.x;
	gate->normal.x = 0;
	gate->normal.y = OWOOT_GATE_SCALE;
	gate->normal.z = 0;
	gate->lateral_limit = half_width * OWOOT_GATE_SCALE;
	gate->normal_limit = height / 2 * OWOOT_GATE_SCALE;
	gate->kind = OWOOT_GATE_TUNNEL;
	if (model == PHYSICAL_MODEL_PIPE || model == PHYSICAL_MODEL_HALF_PIPE) {
		gate->center.y = elevation;
		gate->kind = OWOOT_GATE_PIPE;
	} else if (model == PHYSICAL_MODEL_PIPE_ENTRANCE) {
		/* The entrance bowl is open above. Its sloped road still supplies
		 * the actual lower boundary through the road-coverage query. */
		gate->center.y = elevation;
		gate->normal_limit = 0;
		gate->kind = OWOOT_GATE_ROAD_SURFACE;
	}
	return 1;
}

/* The corkscrew's spiral panels extend along most of a tile. Its AI line
 * picks one longitudinal phase, but every phase on those panels is legal.
 * Intermediate gates therefore follow angular progress around the cylinder,
 * with their radial half-plane spanning the complete element length. */
static legacy_s16 route_corkscrew_gate(legacy_s16 piece, legacy_s16 index, struct OWOOT_GATE *gate)
{
	legacy_s16 count = (legacy_u8)route_info(piece)->route_point_count;
	if (route_object(piece)->ss_physicalModel != PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT ||
		route_is_roll_ramp(piece) || index == 0 || index == count - 1) {
		return 0;
	}
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		return route_extended_corkscrew_gate(piece, gate);
	}
#endif
	struct VECTOR first[ROUTE_GEOMETRY_POINT_COUNT], last[ROUTE_GEOMETRY_POINT_COUNT],
		section[ROUTE_GEOMETRY_POINT_COUNT];
	route_geometry_points(piece, first, 0, 0);
	route_geometry_points(piece, last, count - 1, 0);
	legacy_s32 highest = first[0].y;
	for (legacy_s16 point = 1; point < count; point++) {
		route_geometry_points(piece, section, point, 0);
		if (section[0].y > highest) {
			highest = section[0].y;
		}
	}
	struct OWOOT_POINT axis;
	axis.x = ((legacy_s32)first[0].x + last[0].x) / 2;
	axis.y = ((legacy_s32)first[0].y + highest) / 2;
	axis.z = ((legacy_s32)first[0].z + last[0].z) / 2;
	struct OWOOT_POINT radial = route_delta(&gate->center, &axis);
	struct OWOOT_POINT tangent = {0, 0, 0};
	if (route_abs((legacy_s32)last[0].x - first[0].x) >
		route_abs((legacy_s32)last[0].z - first[0].z)) {
		radial.x = 0;
		tangent.y = -radial.z;
		tangent.z = radial.y;
		gate->edge.x = OWOOT_GATE_SCALE;
		gate->edge.y = gate->edge.z = 0;
	} else {
		radial.z = 0;
		tangent.x = -radial.y;
		tangent.y = radial.x;
		gate->edge.z = OWOOT_GATE_SCALE;
		gate->edge.x = gate->edge.y = 0;
	}
	if (route_dot(&tangent, &gate->tangent) < 0) {
		tangent.x = -tangent.x;
		tangent.y = -tangent.y;
		tangent.z = -tangent.z;
	}
	gate->center = axis;
	gate->tangent = tangent;
	route_normalize(&gate->tangent);
	gate->normal = radial;
	route_normalize(&gate->normal);
	gate->lateral_limit = OWOOT_TILE_SIZE * OWOOT_GATE_SCALE;
	gate->normal_limit = 0;
	gate->kind = OWOOT_GATE_CORKSCREW_PHASE;
	return 1;
}

/* Use the existing edge vectors, including their reverse paths and world
 * rotations. The guidance cursor is intentionally not used: it can recover
 * directly to a later point after a shortcut. */
static void route_authored_gate(legacy_s16 piece, legacy_s16 index, struct OWOOT_GATE *gate)
{
	struct VECTOR section[ROUTE_GEOMETRY_POINT_COUNT], before[ROUTE_GEOMETRY_POINT_COUNT],
		after[ROUTE_GEOMETRY_POINT_COUNT];
	legacy_s16 count = (legacy_u8)route_info(piece)->route_point_count;
	if (route_object(piece)->ss_physicalModel == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT &&
		!route_is_roll_ramp(piece)) {
		index = index * (count - 1) / (OWOOT_CORKSCREW_GATE_COUNT - 1);
	} else if (route_object(piece)->ss_physicalModel == PHYSICAL_MODEL_SLALOM) {
		/* Actual barrier clearance below enforces the S-shaped traversal.
		 * The AI's narrow middle lane is not a separate physical obstacle. */
		index *= count - 1;
	}
	route_geometry_points(piece, section, index, 0);
	route_geometry_points(piece, before, index == 0 ? index : index - 1, 0);
	route_geometry_points(piece, after, index + 1 == count ? index : index + 1, 0);
	gate->kind = section[0].y != ROUTE_POINT_HEIGHT_UNSPECIFIED ? OWOOT_GATE_ROAD_SURFACE
																: OWOOT_GATE_FLAT_LANE;
	gate->center = route_vector(&section[0]);
	struct OWOOT_POINT first = route_vector(&before[0]);
	struct OWOOT_POINT second = route_vector(&after[0]);
	gate->tangent = route_delta(&second, &first);
	first = route_vector(&section[1]);
	second = route_vector(&section[2]);
	gate->edge = route_delta(&second, &first);
#ifdef RESTUNTS_SDL3
	if (extended_track_active() && route_extended_tube_gate(piece, index, count, gate)) {
		return;
	}
#endif
	if (!extended_track_active() && route_tube_gate(piece, gate)) {
		if (route_has_finite_portal(piece) && (index == 0 || index == count - 1)) {
			struct OWOOT_BOUNDS bounds;
			route_bounds(piece, &bounds);
			/* Shared tile-boundary portal planes avoid the AI's inset
			 * and the rendered shell's two-unit seam overlap. */
			legacy_s16 forward = index == count - 1 ? 1 : -1;
			if (gate->tangent.x != 0) {
				gate->center.x =
					gate->tangent.x * forward > 0 ? bounds.maximum_x : bounds.minimum_x;
			} else {
				gate->center.z =
					gate->tangent.z * forward > 0 ? bounds.maximum_z : bounds.minimum_z;
			}
		}
		return;
	}
	if (route_corkscrew_gate(piece, index, gate)) {
		return;
	}
	if (route_object(piece)->ss_physicalModel == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT &&
		!route_is_roll_ramp(piece)) {
		/* Entry and exit are flat fans perpendicular to the cylinder axis;
		 * the adjacent AI point already starts to spiral. Its rising tangent
		 * would move the entry plane backward for an airborne approach. */
		route_geometry_points(piece, before, 0, 0);
		route_geometry_points(piece, after, count - 1, 0);
		first = route_vector(&before[0]);
		second = route_vector(&after[0]);
		gate->tangent = route_delta(&second, &first);
		if (!extended_track_active()) {
			gate->tangent.y = 0;
		}
		if (!extended_track_active() && route_abs(gate->tangent.x) > route_abs(gate->tangent.z)) {
			gate->tangent.z = 0;
		} else if (!extended_track_active()) {
			gate->tangent.x = 0;
		}
	}
	if (gate->kind == OWOOT_GATE_FLAT_LANE) {
		gate->tangent.y = 0;
		gate->edge.y = 0;
	}
	legacy_s32 half_width = route_maximum_component(&gate->edge) / 2;
	if (half_width < ROAD_HALF_WIDTH) {
		half_width = ROAD_HALF_WIDTH;
	}
	struct OWOOT_POINT edge = gate->edge;
	route_normalize(&gate->tangent);
	route_normalize(&gate->edge);
	gate->normal.x = gate->tangent.y * gate->edge.z - gate->tangent.z * gate->edge.y;
	gate->normal.y = gate->tangent.z * gate->edge.x - gate->tangent.x * gate->edge.z;
	gate->normal.z = gate->tangent.x * gate->edge.y - gate->tangent.y * gate->edge.x;
	route_normalize(&gate->normal);
	gate->normal_limit = half_width * (route_abs(gate->normal.x) + route_abs(gate->normal.y) +
									   route_abs(gate->normal.z));
	/* Vertical clearance must not displace a tire past an inclined lane edge.
	 * The separate 3D gate distinguishes consecutive parts of the stunt. */
	gate->edge = edge;
	gate->edge.y = 0;
	route_normalize(&gate->edge);
	gate->lateral_limit = route_abs(route_dot(&edge, &gate->edge)) / 2;
	if (route_object(piece)->ss_physicalModel == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT &&
		!route_is_roll_ramp(piece) && (index == 0 || index == count - 1)) {
		/* The real approach/exit fans out to a full road. The AI's endpoint
		 * vectors describe only its preferred narrow line through that fan. */
		gate->lateral_limit = ROAD_HALF_WIDTH * (route_abs(gate->edge.x) + route_abs(gate->edge.z));
	}
}

void route_gate(legacy_s16 piece, legacy_s16 index, struct OWOOT_GATE *gate)
{
	route_authored_gate(piece, index, gate);
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		const struct CSTRK_OBJECT *placed = route_extended_object(piece);
		if (extended_geometry_uses_grade(placed)) {
			struct OWOOT_POINT origin = route_extended_origin(placed);
			gate->center.y +=
				extended_geometry_object_grade(placed, (legacy_s16)(gate->center.x - origin.x),
											   (legacy_s16)(gate->center.z - origin.z));
			struct OWOOT_POINT *constraints[] = {&gate->tangent, &gate->edge, &gate->normal};
			for (legacy_u16 constraint = 0;
				 constraint < sizeof(constraints) / sizeof(constraints[0]); constraint++) {
				struct VECTOR normal = {(legacy_s16)constraints[constraint]->x,
										(legacy_s16)constraints[constraint]->y,
										(legacy_s16)constraints[constraint]->z};
				extended_geometry_grade_normal(placed, &normal);
				constraints[constraint]->x = normal.x;
				constraints[constraint]->y = normal.y;
				constraints[constraint]->z = normal.z;
			}
		}
	}
#endif
}

legacy_s32 route_gate_distance(const struct OWOOT_GATE *gate, const struct OWOOT_POINT *position)
{
	struct OWOOT_POINT relative = route_delta(position, &gate->center);
	return route_dot(&relative, &gate->tangent);
}

/* Binary division keeps the intermediate within 32 bits on the DOS build. */
static legacy_s16 route_fraction(legacy_s32 numerator, legacy_s32 denominator)
{
	legacy_s16 fraction = 0;
	if (numerator >= denominator) {
		return OWOOT_FRACTION_SCALE;
	}
	for (legacy_u16 bit = 0; bit < OWOOT_FRACTION_BITS; bit++) {
		numerator *= 2;
		fraction *= 2;
		if (numerator >= denominator) {
			numerator -= denominator;
			fraction++;
		}
	}
	return fraction;
}

static legacy_s16 route_gate_crossing(const struct OWOOT_GATE *gate,
									  const struct OWOOT_POINT *previous,
									  const struct OWOOT_POINT *current, legacy_s16 direction)
{
	legacy_s32 first = route_gate_distance(gate, previous) * direction;
	legacy_s32 second = route_gate_distance(gate, current) * direction;
	if (first > 0 || second < 0 || first == second) {
		return -1;
	}
	return route_fraction(-first, second - first);
}

/* The physical tire is a prism with two matched sixteen-point rims. Slice
 * its actual edges at the portal plane; projecting the complete tire could
 * accept a low point that remains behind a portal while its high end crosses. */
static void route_slice_add(struct VECTOR *slice, legacy_u16 *count, const struct VECTOR *point)
{
	for (legacy_u16 index = 0; index < *count; index++) {
		if (slice[index].x == point->x && slice[index].y == point->y &&
			slice[index].z == point->z) {
			return;
		}
	}
	if (*count < OWOOT_WHEEL_VERTEX_COUNT) {
		slice[(*count)++] = *point;
	}
}

static legacy_u16 route_portal_slice(const struct OWOOT_GATE *gate, const struct VECTOR *points,
									 legacy_u16 count, const struct OWOOT_POINT *translation,
									 legacy_u16 crossing_point, struct VECTOR *slice)
{
	if (count < 4U || count > OWOOT_WHEEL_VERTEX_COUNT || (count & 1U) != 0) {
		return 0;
	}
	struct VECTOR relative[OWOOT_WHEEL_VERTEX_COUNT];
	legacy_s32 distances[OWOOT_WHEEL_VERTEX_COUNT];
	struct OWOOT_POINT crossing = route_vector(&points[crossing_point]);
	crossing.x += translation->x - gate->center.x;
	crossing.y += translation->y - gate->center.y;
	crossing.z += translation->z - gate->center.z;
	legacy_s32 correction = route_dot(&crossing, &gate->tangent);
	legacy_u16 length = 0;
	for (legacy_u16 index = 0; index < count; index++) {
		struct OWOOT_POINT point = route_vector(&points[index]);
		point.x += translation->x - gate->center.x;
		point.y += translation->y - gate->center.y;
		point.z += translation->z - gate->center.z;
		legacy_s32 plane_distance = route_dot(&point, &gate->tangent) - correction;
		/* Portal planes are vertical and cardinal. Correct their normal
		 * coordinate to the exact vertex crossing, avoiding Q1024 time
		 * rounding that otherwise leaves the first vertex before the plane. */
		if (extended_track_active()) {
			legacy_s64 length2 = (legacy_s64)gate->tangent.x * gate->tangent.x +
								 (legacy_s64)gate->tangent.y * gate->tangent.y +
								 (legacy_s64)gate->tangent.z * gate->tangent.z;
			if (length2) {
				point.x -= (legacy_s32)((legacy_s64)correction * gate->tangent.x / length2);
				point.y -= (legacy_s32)((legacy_s64)correction * gate->tangent.y / length2);
				point.z -= (legacy_s32)((legacy_s64)correction * gate->tangent.z / length2);
			}
		} else if (gate->tangent.x != 0) {
			point.x -= correction / gate->tangent.x;
		} else {
			point.z -= correction / gate->tangent.z;
		}
		relative[index].x = (legacy_s16)(route_dot(&point, &gate->edge) / OWOOT_GATE_SCALE);
		relative[index].y = extended_track_active()
								? (legacy_s16)(route_dot(&point, &gate->normal) / OWOOT_GATE_SCALE)
								: (legacy_s16)point.y;
		relative[index].z = 0;
		distances[index] =
			extended_track_active() ? plane_distance : route_dot(&point, &gate->tangent);
		if (distances[index] == 0) {
			route_slice_add(slice, &length, &relative[index]);
		}
	}
	legacy_u16 rim_count = count / OWOOT_WHEEL_RIM_COUNT;
	for (legacy_u16 index = 0; index < count; index++) {
		legacy_u16 neighbor = index / rim_count * rim_count + (index + 1) % rim_count;
		for (legacy_u16 edge = 0; edge < (index < rim_count ? 2U : 1U); edge++) {
			legacy_u16 other = edge == 0 ? neighbor : index + rim_count;
			legacy_s32 first = distances[index], second = distances[other];
			if (!((first < 0 && second > 0) || (first > 0 && second < 0))) {
				continue;
			}
			struct VECTOR point = relative[index];
			point.x +=
				(legacy_s16)(((legacy_s32)relative[other].x - point.x) * first / (first - second));
			point.y +=
				(legacy_s16)(((legacy_s32)relative[other].y - point.y) * first / (first - second));
			route_slice_add(slice, &length, &point);
		}
	}
	return length;
}

static legacy_s16 route_wheel_crosses_gate(const struct OWOOT_GATE *gate,
										   const struct OWOOT_TIRES *tires,
										   const struct OWOOT_POINT *previous,
										   const struct OWOOT_POINT *current, legacy_u16 wheel,
										   legacy_u16 crossing_point, legacy_s16 fraction)
{
	struct OWOOT_POINT translation;
	translation.x =
		(previous->x - current->x) * (OWOOT_FRACTION_SCALE - fraction) / OWOOT_FRACTION_SCALE;
	translation.y =
		(previous->y - current->y) * (OWOOT_FRACTION_SCALE - fraction) / OWOOT_FRACTION_SCALE;
	translation.z =
		(previous->z - current->z) * (OWOOT_FRACTION_SCALE - fraction) / OWOOT_FRACTION_SCALE;
	const struct VECTOR *points = tires->vertices[wheel];
	legacy_u16 count = tires->counts[wheel];
	if (gate->kind == OWOOT_GATE_TUNNEL || gate->kind == OWOOT_GATE_PIPE) {
		struct VECTOR slice[OWOOT_WHEEL_VERTEX_COUNT];
		count = route_portal_slice(gate, points, count, &translation, crossing_point, slice);
		if (gate->kind == OWOOT_GATE_PIPE) {
			return track_pipe_aperture_overlaps_wheel(slice, count);
		}
		/* The rectangular aperture helper measures height from the floor. */
		for (legacy_u16 index = 0; index < count; index++) {
			slice[index].y += TUNNEL_HEIGHT / 2;
		}
		return track_tunnel_aperture_overlaps_wheel(slice, count);
	}
	legacy_s32 minimum_edge = (legacy_s32)LEGACY_S32_MAX,
			   maximum_edge = -(legacy_s32)LEGACY_S32_MAX;
	legacy_s32 minimum_normal = (legacy_s32)LEGACY_S32_MAX,
			   maximum_normal = -(legacy_s32)LEGACY_S32_MAX;
	for (legacy_u16 point = 0; point < count; point++) {
		struct OWOOT_POINT relative = route_vector(&points[point]);
		relative.x += translation.x - gate->center.x;
		relative.y += translation.y - gate->center.y;
		relative.z += translation.z - gate->center.z;
		legacy_s32 edge = route_dot(&relative, &gate->edge);
		legacy_s32 normal = route_dot(&relative, &gate->normal);
		if (edge < minimum_edge) {
			minimum_edge = edge;
		}
		if (edge > maximum_edge) {
			maximum_edge = edge;
		}
		if (normal < minimum_normal) {
			minimum_normal = normal;
		}
		if (normal > maximum_normal) {
			maximum_normal = normal;
		}
	}
	legacy_s16 height_valid;
	if (gate->kind == OWOOT_GATE_FLAT_LANE) {
		height_valid = 1;
	} else if (gate->kind == OWOOT_GATE_CORKSCREW_PHASE) {
		height_valid = maximum_normal >= 0;
	} else if (gate->normal.y == 0) {
		height_valid =
			minimum_normal <= gate->normal_limit && maximum_normal >= -gate->normal_limit;
	} else if (gate->normal.y > 0) {
		height_valid = maximum_normal >= -gate->normal_limit;
	} else {
		height_valid = minimum_normal <= gate->normal_limit;
	}
	legacy_s16 lateral_valid =
		minimum_edge <= gate->lateral_limit && maximum_edge >= -gate->lateral_limit;

	if (lateral_valid && height_valid) {
		return 1;
	}
	return 0;
}

/* A front or rear tire reaches a gate before or after the car's center.
 * Sweep the actual tire vertices, so an edge overlap can complete a gate
 * without requiring the center of the car to pass through the AI lane. */
legacy_s16 route_swept_gate(const struct OWOOT_GATE *gate, const struct OWOOT_TIRES *tires,
							const struct OWOOT_POINT *previous, const struct OWOOT_POINT *current,
							legacy_s16 direction, legacy_s16 minimum_fraction)
{
	struct OWOOT_POINT motion = route_delta(current, previous);
	if (route_dot(&motion, &gate->tangent) * direction <= 0) {
		return -1;
	}
	legacy_s16 best = -1;
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		for (legacy_u16 point = 0; point < tires->counts[wheel]; point++) {
			struct OWOOT_POINT end = route_vector(&tires->vertices[wheel][point]);
			struct OWOOT_POINT start = route_delta(&end, &motion);
			legacy_s16 fraction = route_gate_crossing(gate, &start, &end, direction);
			if (fraction < minimum_fraction || (best >= 0 && fraction >= best)) {
				continue;
			}
			if (route_wheel_crosses_gate(gate, tires, previous, current, wheel, point, fraction)) {
				best = fraction;
			}
		}
	}
	return best;
}

legacy_s16 route_previous_tire_before(const struct OWOOT_GATE *gate,
									  const struct OWOOT_TIRES *tires,
									  const struct OWOOT_POINT *motion, legacy_s16 direction)
{
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		for (legacy_u16 index = 0; index < tires->counts[wheel]; index++) {
			struct OWOOT_POINT point = route_vector(&tires->vertices[wheel][index]);
			point = route_delta(&point, motion);
			if (route_gate_distance(gate, &point) * direction <= 0) {
				return 1;
			}
		}
	}
	return 0;
}
