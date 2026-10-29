#include "owoot_route.h"
#include "owoot.h"
#include "owoot_road.h"
#include "externs.h"
#include "track_objects.h"
#include "track_collision.h"
#include "trackdata_layout.h"
#include "extended_track.h"
#ifdef RESTUNTS_SDL3
#include "extended_geometry.h"
#endif

#define OWOOT_POSITION_SCALE 64L
#define OWOOT_ROUTE_SUBTYPE_MASK 15U
#define OWOOT_ROUTE_REVERSE_FLAG 16U
#define OWOOT_GATE_SCALE 256L
#define OWOOT_FRACTION_BITS 10U
#define OWOOT_FRACTION_SCALE (1L << OWOOT_FRACTION_BITS)
#define OWOOT_CORKSCREW_GATE_COUNT 5
#define OWOOT_SLALOM_GATE_COUNT 2
#define OWOOT_JUMP_CONNECTION 1
#define OWOOT_TILE_SIZE TRACK_TILE_SIZE
#define OWOOT_GAP_HALF_WIDTH (OWOOT_TILE_SIZE / 2L)
#define OWOOT_SINGLE_GAP_DISTANCE (2L * OWOOT_TILE_SIZE)
#define OWOOT_PIPE_HEIGHT 235
#define OWOOT_PIPE_HALF_WIDTH 115
#define OWOOT_PIPE_ENTRANCE_HEIGHT 171
#define OWOOT_PIPE_ENTRANCE_HALF_WIDTH 115

enum OWOOT_SCRATCH_INDEX {
	OWOOT_SCRATCH_ROUTE_DIRECTION,
	OWOOT_SCRATCH_JUMP_SOURCE,
	OWOOT_SCRATCH_JUMP_TARGET,
	OWOOT_SCRATCH_JUMP_LAUNCH
};

struct OWOOT_POINT {
	legacy_s32 x, y, z;
};

struct OWOOT_BOUNDS {
	legacy_s32 minimum_x, maximum_x, minimum_z, maximum_z;
};

struct OWOOT_TIRES {
	struct VECTOR vertices[CARSTATE_WHEEL_COUNT][OWOOT_WHEEL_VERTEX_COUNT];
	legacy_u16 counts[CARSTATE_WHEEL_COUNT];
};

enum OWOOT_GATE_KIND {
	OWOOT_GATE_FLAT_LANE,
	OWOOT_GATE_ROAD_SURFACE,
	OWOOT_GATE_TUNNEL,
	OWOOT_GATE_CORKSCREW_PHASE,
	OWOOT_GATE_PIPE
};

struct OWOOT_GATE {
	struct OWOOT_POINT center, tangent, edge, normal;
	legacy_s32 lateral_limit, normal_limit;
	legacy_u16 kind;
};

static legacy_s32 route_abs(legacy_s32 value)
{
	return value < 0 ? -value : value;
}

static struct OWOOT_POINT route_position(const struct VECTORLONG *position)
{
	struct OWOOT_POINT result;
	result.x = position->lx / OWOOT_POSITION_SCALE;
	result.y = position->ly / OWOOT_POSITION_SCALE;
	result.z = position->lz / OWOOT_POSITION_SCALE;
	return result;
}

static struct OWOOT_POINT route_vector(const struct VECTOR *vector)
{
	struct OWOOT_POINT result;
	result.x = vector->x;
	result.y = vector->y;
	result.z = vector->z;
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		extended_geometry_world_position(vector, &result.x, &result.z);
	}
#endif
	return result;
}

static struct OWOOT_POINT route_delta(const struct OWOOT_POINT *first,
									  const struct OWOOT_POINT *second)
{
	struct OWOOT_POINT result;
	result.x = first->x - second->x;
	result.y = first->y - second->y;
	result.z = first->z - second->z;
	return result;
}

static legacy_s32 route_dot(const struct OWOOT_POINT *first, const struct OWOOT_POINT *second)
{
	return first->x * second->x + first->y * second->y + first->z * second->z;
}

static legacy_s32 route_maximum_component(const struct OWOOT_POINT *vector)
{
	legacy_s32 maximum = route_abs(vector->x);
	if (route_abs(vector->y) > maximum) {
		maximum = route_abs(vector->y);
	}
	if (route_abs(vector->z) > maximum) {
		maximum = route_abs(vector->z);
	}
	return maximum;
}

static void route_normalize(struct OWOOT_POINT *vector)
{
	legacy_s32 maximum = route_maximum_component(vector);
	if (maximum != 0) {
		vector->x = vector->x * OWOOT_GATE_SCALE / maximum;
		vector->y = vector->y * OWOOT_GATE_SCALE / maximum;
		vector->z = vector->z * OWOOT_GATE_SCALE / maximum;
	}
}

static const struct TRACKOBJECT *route_object(legacy_s16 piece)
{
	return &trkObjectList[(legacy_u8)track_route_element_ids[piece]];
}

static const struct TRKOBJINFO *route_info(legacy_s16 piece)
{
	const struct TRACKOBJECT *object = route_object(piece);
	if (object->ss_trkObjInfoPtr == 0) {
		return 0;
	}
	return &object->ss_trkObjInfoPtr[(legacy_u8)track_route_traversal_flags[piece] &
									 OWOOT_ROUTE_SUBTYPE_MASK];
}

static legacy_s16 route_gate_count(legacy_s16 piece)
{
	/* The entry/exit fans join the spiral over a range of angular phases.
	 * Require both sides and its crown, rather than the AI's intermediate
	 * subdivisions of those continuously drivable surfaces. */
	if (route_object(piece)->ss_physicalModel == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT) {
		return OWOOT_CORKSCREW_GATE_COUNT;
	}
	if (route_object(piece)->ss_physicalModel == PHYSICAL_MODEL_SLALOM) {
		return OWOOT_SLALOM_GATE_COUNT;
	}
	return (legacy_u8)route_info(piece)->route_point_count;
}

static void route_bounds(legacy_s16 piece, struct OWOOT_BOUNDS *bounds)
{
	const struct TRACKOBJECT *object = route_object(piece);
	legacy_u8 column = (legacy_u8)track_route_columns[piece];
	legacy_u8 row = (legacy_u8)track_route_rows[piece];
	legacy_s32 half_width = ((legacy_u8)object->ss_multiTileFlag & MULTI_TILE_COLUMN_EDGE_FLAG) != 0
								? OWOOT_TILE_SIZE
								: TRACK_TILE_HALF_SIZE;
	legacy_s32 half_length = ((legacy_u8)object->ss_multiTileFlag & MULTI_TILE_ROW_EDGE_FLAG) != 0
								 ? OWOOT_TILE_SIZE
								 : TRACK_TILE_HALF_SIZE;
	legacy_s32 center_x = track_object_base_x(object, column);
	legacy_s32 center_z = track_object_base_z(object, row);
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		const struct CSTRK_OBJECT *placed =
			extended_track_object_for_id(column, row, (legacy_u8)track_route_element_ids[piece]);
		if (placed) {
			center_x = placed->x * 1024L + placed->width * 512L;
			center_z = extended_track_height() * 1024L - placed->y * 1024L - placed->height * 512L;
		}
	}
#endif
	bounds->minimum_x = center_x - half_width;
	bounds->maximum_x = center_x + half_width;
	bounds->minimum_z = center_z - half_length;
	bounds->maximum_z = center_z + half_length;
}

static legacy_s16 route_contains(const struct OWOOT_BOUNDS *bounds,
								 const struct OWOOT_POINT *position)
{
	return position->x >= bounds->minimum_x && position->x < bounds->maximum_x &&
		   position->z >= bounds->minimum_z && position->z < bounds->maximum_z;
}

static legacy_s16 route_requires_gates(legacy_s16 piece)
{
	legacy_s16 model = route_object(piece)->ss_physicalModel;
	return model >= PHYSICAL_MODEL_BANKED_ENTRANCE_B &&
		   model <= PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT;
}

static legacy_s16 route_has_finite_portal(legacy_s16 piece)
{
	legacy_s16 model = route_object(piece)->ss_physicalModel;
	return model == PHYSICAL_MODEL_TUNNEL || model == PHYSICAL_MODEL_PIPE ||
		   model == PHYSICAL_MODEL_HALF_PIPE;
}

static legacy_s16 route_owns_position(legacy_s16 piece, const struct OWOOT_POINT *position)
{
	if (position->x < 0 || position->z < 0 ||
		position->x >= extended_track_width() * OWOOT_TILE_SIZE ||
		position->z >= extended_track_height() * OWOOT_TILE_SIZE) {
		return 0;
	}
	legacy_s16 column = (legacy_s16)(position->x / OWOOT_TILE_SIZE);
	legacy_s16 row =
		(legacy_s16)(extended_track_height() - 1) - (legacy_s16)(position->z / OWOOT_TILE_SIZE);
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		const struct CSTRK_OBJECT *placed = extended_track_object_at(column, row);
		return placed && placed->x == (legacy_u8)track_route_columns[piece] &&
			   placed->y == (legacy_u8)track_route_rows[piece] &&
			   extended_track_object_id(placed) == (legacy_u8)track_route_element_ids[piece];
	}
#endif
	legacy_u8 tile = track_element_map[trackrows[row] + column];
	if (tile == TRACK_TILE_CONTINUATION_SOUTHEAST) {
		row--;
		column--;
	} else if (tile == TRACK_TILE_CONTINUATION_SOUTH) {
		row--;
	} else if (tile == TRACK_TILE_CONTINUATION_EAST) {
		column--;
	}
	/* Edited tracks can replace a multi-tile stunt's continuation with a
	 * separate bridge. Its bounding box alone does not own that bridge. */
	return row >= 0 && column >= 0 && row == (legacy_u8)track_route_rows[piece] &&
		   column == (legacy_u8)track_route_columns[piece] &&
		   track_element_map[trackrows[row] + column] == (legacy_u8)track_route_element_ids[piece];
}

static legacy_s16 route_tires_occupy_piece(legacy_s16 piece, const struct OWOOT_TIRES *tires)
{
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		for (legacy_u16 index = 0; index < tires->counts[wheel]; index++) {
			struct OWOOT_POINT point = route_vector(&tires->vertices[wheel][index]);
			if (route_owns_position(piece, &point)) {
				return 1;
			}
		}
	}
	return 0;
}

#ifdef RESTUNTS_SDL3
static const struct CSTRK_OBJECT *route_extended_object(legacy_s16 piece)
{
	return extended_track_object_for_id((legacy_u8)track_route_columns[piece],
										(legacy_u8)track_route_rows[piece],
										(legacy_u8)track_route_element_ids[piece]);
}

static struct OWOOT_POINT route_extended_origin(const struct CSTRK_OBJECT *placed)
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
	int model = route_object(piece)->ss_physicalModel;
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

static legacy_s16 route_extended_corkscrew_gate(legacy_s16 piece, legacy_s16 index,
												struct OWOOT_GATE *gate)
{
	const struct CSTRK_OBJECT *placed = route_extended_object(piece);
	if (!placed) {
		return 0;
	}
	struct OWOOT_POINT origin = route_extended_origin(placed);
	struct OWOOT_POINT first = {0, 0, 0}, last = {0, 0, 0};
	legacy_s32 highest = 0;
	int count = (legacy_u8)route_info(piece)->route_point_count;
	if (count < 2) {
		return 0;
	}
	struct VECTOR section[ROUTE_GEOMETRY_POINT_COUNT];
	for (int point = 0; point < count; point++) {
		get_track_route_point(piece, section, (legacy_s16)point, 0);
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
	(void)index;
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
		index == 0 || index == count - 1) {
		return 0;
	}
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		return route_extended_corkscrew_gate(piece, index, gate);
	}
#endif
	struct VECTOR first[ROUTE_GEOMETRY_POINT_COUNT], last[ROUTE_GEOMETRY_POINT_COUNT],
		section[ROUTE_GEOMETRY_POINT_COUNT];
	get_track_route_point(piece, first, 0, 0);
	get_track_route_point(piece, last, count - 1, 0);
	legacy_s32 highest = first[0].y;
	for (legacy_s16 point = 1; point < count; point++) {
		get_track_route_point(piece, section, point, 0);
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
static void route_gate(legacy_s16 piece, legacy_s16 index, struct OWOOT_GATE *gate)
{
	struct VECTOR section[ROUTE_GEOMETRY_POINT_COUNT], before[ROUTE_GEOMETRY_POINT_COUNT],
		after[ROUTE_GEOMETRY_POINT_COUNT];
	legacy_s16 count = (legacy_u8)route_info(piece)->route_point_count;
	if (route_object(piece)->ss_physicalModel == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT) {
		index = index * (count - 1) / (OWOOT_CORKSCREW_GATE_COUNT - 1);
	} else if (route_object(piece)->ss_physicalModel == PHYSICAL_MODEL_SLALOM) {
		/* Actual barrier clearance below enforces the S-shaped traversal.
		 * The AI's narrow middle lane is not a separate physical obstacle. */
		index *= count - 1;
	}
	get_track_route_point(piece, section, index, 0);
	get_track_route_point(piece, before, index == 0 ? index : index - 1, 0);
	get_track_route_point(piece, after, index + 1 == count ? index : index + 1, 0);
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
	if (route_object(piece)->ss_physicalModel == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT) {
		/* Entry and exit are flat fans perpendicular to the cylinder axis;
		 * the adjacent AI point already starts to spiral. Its rising tangent
		 * would move the entry plane backward for an airborne approach. */
		get_track_route_point(piece, before, 0, 0);
		get_track_route_point(piece, after, count - 1, 0);
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
		(index == 0 || index == count - 1)) {
		/* The real approach/exit fans out to a full road. The AI's endpoint
		 * vectors describe only its preferred narrow line through that fan. */
		gate->lateral_limit = ROAD_HALF_WIDTH * (route_abs(gate->edge.x) + route_abs(gate->edge.z));
	}
}

static legacy_s32 route_gate_distance(const struct OWOOT_GATE *gate,
									  const struct OWOOT_POINT *position)
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
static legacy_s16 route_swept_gate(const struct OWOOT_GATE *gate, const struct OWOOT_TIRES *tires,
								   const struct OWOOT_POINT *previous,
								   const struct OWOOT_POINT *current, legacy_s16 direction,
								   legacy_s16 minimum_fraction)
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

static legacy_s16 route_previous_tire_before(const struct OWOOT_GATE *gate,
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

static legacy_s16 route_enter(struct CARSTATE *carstate, legacy_s16 piece,
							  const struct OWOOT_POINT *previous, const struct OWOOT_POINT *current,
							  const struct OWOOT_TIRES *tires)
{
	struct OWOOT_GATE gate;
	struct OWOOT_POINT motion = route_delta(current, previous);
	legacy_s16 finite = route_has_finite_portal(piece);
	route_gate(piece, 0, &gate);
	legacy_s16 direction = 0;
	if (finite ? route_previous_tire_before(&gate, tires, &motion, 1)
			   : route_gate_distance(&gate, previous) <= 0) {
		direction = 1;
	} else {
		legacy_s16 count = route_gate_count(piece);
		route_gate(piece, count - 1, &gate);
		if (finite ? route_previous_tire_before(&gate, tires, &motion, -1)
				   : route_gate_distance(&gate, previous) >= 0) {
			direction = -1;
		}
	}
	if (direction == 0) {
		return 0;
	}
	carstate->car_reserved_route_word1 = piece + 1;
	carstate->car_reserved_route_word2 = 0;
	carstate->car_reserved_wheel_state[OWOOT_SCRATCH_ROUTE_DIRECTION] = direction;
	return 1;
}

#ifdef RESTUNTS_SDL3
static legacy_s16 route_extended_slalom_barrier(legacy_s16 piece, const struct CARSTATE *carstate,
												const struct OWOOT_TIRES *tires,
												const struct OWOOT_POINT *motion)
{
	const struct CSTRK_OBJECT *placed = route_extended_object(piece);
	if (!placed) {
		return 0;
	}
	struct VECTOR origin, zero = {0, 0, 0}, local_motion;
	extended_geometry_object_origin(extended_track_model(), placed, &origin);
	struct OWOOT_TIRES local;
	struct VECTOR body[CARSTATE_WHEEL_COUNT];
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		local.counts[wheel] = tires->counts[wheel];
		for (legacy_u16 point = 0; point < local.counts[wheel]; point++) {
			local.vertices[wheel][point] =
				(struct VECTOR){LEGACY_S16_WRAP_SUB(tires->vertices[wheel][point].x, origin.x),
								LEGACY_S16_WRAP_SUB(tires->vertices[wheel][point].y, origin.y),
								LEGACY_S16_WRAP_SUB(tires->vertices[wheel][point].z, origin.z)};
		}
		body[wheel] = (struct VECTOR){
			LEGACY_S16_WRAP_SUB(carstate->car_body_corner_positions[wheel].x, origin.x),
			LEGACY_S16_WRAP_SUB(carstate->car_body_corner_positions[wheel].y, origin.y),
			LEGACY_S16_WRAP_SUB(carstate->car_body_corner_positions[wheel].z, origin.z)};
	}
	local_motion =
		(struct VECTOR){(legacy_s16)motion->x, (legacy_s16)motion->y, (legacy_s16)motion->z};
	return track_slalom_wheel_envelope_crosses_barrier(local.vertices, local.counts, body, &zero,
													   route_object(piece)->ss_rotY, &local_motion);
}
#endif

static legacy_s16 route_advance(struct CARSTATE *carstate, const struct OWOOT_POINT *previous,
								const struct OWOOT_POINT *current, const struct OWOOT_TIRES *tires)
{
	legacy_s16 piece = carstate->car_reserved_route_word1 - 1;
	const struct TRACKOBJECT *object = route_object(piece);
	if (object->ss_physicalModel == PHYSICAL_MODEL_SLALOM) {
#ifdef RESTUNTS_SDL3
		if (extended_track_active()) {
			struct OWOOT_POINT motion = route_delta(current, previous);
			if (route_extended_slalom_barrier(piece, carstate, tires, &motion)) {
				return 0;
			}
		} else
#endif
		{
			struct VECTOR center, motion;
			center.x = track_object_base_x(object, (legacy_u8)track_route_columns[piece]);
			center.y = 0;
			center.z = track_object_base_z(object, (legacy_u8)track_route_rows[piece]);
			motion.x = (legacy_s16)(current->x - previous->x);
			motion.y = 0;
			motion.z = (legacy_s16)(current->z - previous->z);
			if (track_slalom_wheel_envelope_crosses_barrier(tires->vertices, tires->counts,
															carstate->car_body_corner_positions,
															&center, object->ss_rotY, &motion)) {
				return 0;
			}
		}
	}
	legacy_s16 count = route_gate_count(piece);
	legacy_s16 direction = carstate->car_reserved_wheel_state[OWOOT_SCRATCH_ROUTE_DIRECTION];
	legacy_s16 progress = carstate->car_reserved_route_word2;
	legacy_s16 last_fraction = 0;
	struct OWOOT_GATE gate;
	while (progress < count) {
		legacy_s16 index = direction > 0 ? progress : count - progress - 1;
		route_gate(piece, index, &gate);
		legacy_s16 fraction =
			route_swept_gate(&gate, tires, previous, current, direction, last_fraction);
		if (fraction < 0) {
			break;
		}
		last_fraction = fraction;
		progress++;
	}
	/* Driving back across a completed gate undoes that progress. A player can
	 * back out and retry, but cannot collect gates in a different order. */
	last_fraction = 0;
	while (progress > 0) {
		legacy_s16 index = direction > 0 ? progress - 1 : count - progress;
		route_gate(piece, index, &gate);
		legacy_s16 fraction =
			route_swept_gate(&gate, tires, previous, current, -direction, last_fraction);
		if (fraction < 0) {
			break;
		}
		last_fraction = fraction;
		progress--;
	}
	carstate->car_reserved_route_word2 = progress;
	return 1;
}

legacy_s16 owoot_route_is_valid(struct CARSTATE *carstate, legacy_s16 allowed_jump)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		extended_geometry_set_context(carstate->car_position.lx, carstate->car_position.lz);
	}
#endif
	struct OWOOT_POINT previous = route_position(&carstate->car_previous_position);
	struct OWOOT_POINT current = route_position(&carstate->car_position);
	struct OWOOT_TIRES tires;
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		tires.counts[wheel] = owoot_wheel_footprint(carstate, wheel, tires.vertices[wheel]);
	}
	legacy_s16 active = carstate->car_reserved_route_word1 - 1;
	if (active >= 0 && active < track_pieces_counter) {
		if (!route_advance(carstate, &previous, &current, &tires)) {
			return 0;
		}
		struct OWOOT_BOUNDS bounds;
		route_bounds(active, &bounds);
		legacy_s16 finite = route_has_finite_portal(active);
		legacy_s16 occupied =
			finite ? route_tires_occupy_piece(active, &tires) : route_contains(&bounds, &current);
		legacy_s16 progress = carstate->car_reserved_route_word2;
		legacy_s16 count = route_gate_count(active);
		if (occupied && progress != count) {
			return 1;
		}
		if (!occupied) {
			if (progress != 0 && progress != count) {
				return 0;
			}
			struct OWOOT_GATE gate;
			legacy_s16 direction =
				carstate->car_reserved_wheel_state[OWOOT_SCRATCH_ROUTE_DIRECTION];
			legacy_s16 index = progress == count ? count - 1 : 0;
			if (direction < 0) {
				index = count - index - 1;
			}
			route_gate(active, index, &gate);
			legacy_s32 distance = route_gate_distance(&gate, &current) * direction;
			if ((progress == 0 && distance > 0) || (progress == count && distance < 0)) {
				return 0;
			}
			carstate->car_reserved_route_word1 = 0;
			carstate->car_reserved_route_word2 = 0;
			carstate->car_reserved_wheel_state[OWOOT_SCRATCH_ROUTE_DIRECTION] = 0;
		}
		/* Once any predecessor is complete, a front tire may already reach
		 * the next tube. Validate its entry in this same sweep, before that
		 * qualifying tire leaves the portal behind. Keep the completed piece
		 * while straddling ordinary road, so it cannot be re-entered. */
	}
	if (allowed_jump) {
		return 1;
	}
	for (legacy_s16 piece = 0; piece < track_pieces_counter; piece++) {
		if (piece == active || !route_requires_gates(piece) || route_info(piece) == 0 ||
			(legacy_u8)route_info(piece)->route_point_count < 2U) {
			continue;
		}
		struct OWOOT_BOUNDS bounds;
		route_bounds(piece, &bounds);
		legacy_s16 occupied =
			route_has_finite_portal(piece)
				? route_tires_occupy_piece(piece, &tires)
				: route_contains(&bounds, &current) && route_owns_position(piece, &current);
		if (!occupied) {
			continue;
		}
		if (!route_enter(carstate, piece, &previous, &current, &tires)) {
			return 0;
		}
		return route_advance(carstate, &previous, &current, &tires);
	}
	return 1;
}

/* Connection points use the track builder's owner-tile offsets. The final
 * two components point out through that edge in world X/Z coordinates. */
struct OWOOT_CONNECTION_EDGE {
	legacy_s8 column, row, x, z;
};

static const struct OWOOT_CONNECTION_EDGE jump_edges[] = {
	{0, 0, 0, 0},  {0, -1, 0, 1},  {0, 1, 0, -1}, {1, 0, 1, 0}, {-1, 0, -1, 0},
	{1, -1, 0, 1}, {-1, 1, -1, 0}, {1, 1, 1, 0},  {2, 0, 1, 0}, {2, 1, 1, 0},
	{1, 1, 0, -1}, {0, 2, 0, -1},  {1, 2, 0, -1}};

static legacy_s16 jump_model(legacy_s16 piece)
{
	const struct TRKOBJINFO *info = route_info(piece);
	return info != 0 && (info->si_entryType == OWOOT_JUMP_CONNECTION ||
						 info->si_exitType == OWOOT_JUMP_CONNECTION);
}

static legacy_s16 jump_on_deck(legacy_s16 piece, const struct OWOOT_POINT *position)
{
	legacy_s16 model = route_object(piece)->ss_physicalModel;
	if (model == PHYSICAL_MODEL_RAMP || model == PHYSICAL_MODEL_SOLID_RAMP) {
		return 1;
	}
	legacy_u8 column = (legacy_u8)track_route_columns[piece];
	legacy_u8 row = (legacy_u8)track_route_rows[piece];
	legacy_s32 elevation = track_terrain_map[terrainrows[row] + column] == TERRAIN_RAISED_TILE
							   ? hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX]
							   : 0;
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		int actual_column = (int)(position->x / 1024L);
		int world_row = (int)(position->z / 1024L);
		elevation = extended_track_elevation(actual_column, extended_track_height() - 1 - world_row,
											 (int)(position->x - actual_column * 1024L),
											 (int)(1024L - position->z + world_row * 1024L));
	}
#endif
	/* Only contact on the upper deck can launch an elevated connection. A
	 * lower overpass crossing, or the low end of a corkscrew, cannot arm it. */
	return position->y > elevation + ELEVATED_DECK_CLEARANCE;
}

static legacy_s16 jump_endpoint(legacy_s16 piece, legacy_s16 outgoing, struct OWOOT_POINT *position,
								struct OWOOT_POINT *direction)
{
	const struct TRKOBJINFO *info = route_info(piece);
	if (info == 0) {
		return 0;
	}
	if (((legacy_u8)track_route_traversal_flags[piece] & OWOOT_ROUTE_REVERSE_FLAG) != 0) {
		outgoing = !outgoing;
	}
	if ((outgoing ? info->si_exitType : info->si_entryType) != OWOOT_JUMP_CONNECTION) {
		return 0;
	}
	legacy_u8 point = (legacy_u8)(outgoing ? info->si_exitPoint : info->si_entryPoint);
	if (point == 0 || point >= sizeof(jump_edges) / sizeof(jump_edges[0])) {
		return 0;
	}
	const struct OWOOT_CONNECTION_EDGE *edge = &jump_edges[point];
	const struct TRACKOBJECT *object = route_object(piece);
	legacy_u8 column = (legacy_u8)track_route_columns[piece];
	legacy_u8 row = (legacy_u8)track_route_rows[piece];
	legacy_s32 owner_x = track_object_base_x(object, column);
	legacy_s32 owner_z = track_object_base_z(object, row);
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		owner_x =
			column * 1024L +
			(((legacy_u8)object->ss_multiTileFlag & MULTI_TILE_COLUMN_EDGE_FLAG) ? 1024L : 512L);
		owner_z = (extended_track_height() - 1 - row) * 1024L +
				  (((legacy_u8)object->ss_multiTileFlag & MULTI_TILE_ROW_EDGE_FLAG) ? 0L : 512L);
	}
#endif
	if (((legacy_u8)object->ss_multiTileFlag & MULTI_TILE_COLUMN_EDGE_FLAG) != 0) {
		owner_x -= TRACK_TILE_HALF_SIZE;
	}
	if (((legacy_u8)object->ss_multiTileFlag & MULTI_TILE_ROW_EDGE_FLAG) != 0) {
		owner_z += TRACK_TILE_HALF_SIZE;
	}
	direction->x = edge->x;
	direction->y = 0;
	direction->z = edge->z;
	position->x = owner_x + edge->column * OWOOT_TILE_SIZE - edge->x * TRACK_TILE_HALF_SIZE;
	position->y = 0;
	position->z = owner_z - edge->row * OWOOT_TILE_SIZE - edge->z * TRACK_TILE_HALF_SIZE;
	return 1;
}

static legacy_s16 jump_forward_aligned(legacy_s16 first, legacy_s16 second,
									   struct OWOOT_POINT *direction, legacy_s32 *distance)
{
	if (first < 0 || first >= track_pieces_counter || second < 0 ||
		second >= track_pieces_counter) {
		return 0;
	}
	struct OWOOT_POINT start, end, incoming;
	if (!jump_endpoint(first, 1, &start, direction) || !jump_endpoint(second, 0, &end, &incoming) ||
		direction->x != -incoming.x || direction->z != -incoming.z) {
		return 0;
	}
	legacy_s32 dx = end.x - start.x;
	legacy_s32 dz = end.z - start.z;
	legacy_s32 separation = dx * direction->x + dz * direction->z;
	if (dx * direction->z - dz * direction->x != 0 || separation < 0) {
		return 0;
	}
	/* Keep distances between virtual one-tile runway centres. Unlike the
	 * entire element centre, these work for a two-by-two corkscrew endpoint. */
	*distance = separation + OWOOT_TILE_SIZE;
	return 1;
}

static legacy_s16 jump_source_center(legacy_s16 first, legacy_s16 second,
									 struct OWOOT_POINT *center)
{
	struct OWOOT_POINT direction;
	legacy_s32 distance;
	legacy_s16 outgoing = 1;
	if (!jump_forward_aligned(first, second, &direction, &distance)) {
		if (!jump_forward_aligned(second, first, &direction, &distance)) {
			return 0;
		}
		outgoing = 0;
	}
	if (!jump_endpoint(first, outgoing, center, &direction)) {
		return 0;
	}
	center->x -= direction.x * TRACK_TILE_HALF_SIZE;
	center->z -= direction.z * TRACK_TILE_HALF_SIZE;
	return 1;
}

/* The same physical gap is usable in either direction. Reverse traversal
 * uses the existing predecessor link and reverses both endpoint tangents. */
static legacy_s16 jump_aligned(legacy_s16 first, legacy_s16 second, struct OWOOT_POINT *direction,
							   legacy_s32 *distance)
{
	if (jump_forward_aligned(first, second, direction, distance)) {
		return 1;
	}
	if (first >= 0 && first < track_pieces_counter && second >= 0 &&
		second < track_pieces_counter && jump_forward_aligned(second, first, direction, distance)) {
		direction->x = -direction->x;
		direction->z = -direction->z;
		return 1;
	}
	return 0;
}

static legacy_s16 jump_next(legacy_s16 piece, legacy_s16 reverse,
							const struct OWOOT_POINT *approach, legacy_s16 first_step,
							struct OWOOT_POINT *direction, legacy_s32 *distance)
{
	legacy_s16 candidate_count = reverse ? track_pieces_counter : 2;
	for (legacy_s16 index = 0; index < candidate_count; index++) {
		legacy_s16 next;
		if (reverse) {
			next = index;
			if (track_primary_route_links[next] != piece &&
				track_alternate_route_links[next] != piece) {
				continue;
			}
		} else {
			next =
				index == 0 ? track_primary_route_links[piece] : track_alternate_route_links[piece];
		}
		if (!jump_aligned(piece, next, direction, distance) ||
			(*distance != OWOOT_TILE_SIZE && *distance != OWOOT_SINGLE_GAP_DISTANCE)) {
			continue;
		}
		if (!first_step && (direction->x != approach->x || direction->z != approach->z)) {
			continue;
		}
		return next;
	}
	return -1;
}

/* Search through an aligned bridge approach as well as the takeoff tile.
 * Replays can become airborne several road tiles before the single empty
 * tile; the exception is still limited to that paired gap. */
static legacy_s16 jump_find_gap(legacy_s16 launch, legacy_s16 reverse, legacy_s16 *source,
								legacy_s16 *target)
{
	legacy_s16 piece = launch;
	struct OWOOT_POINT approach = {0, 0, 0};
	for (legacy_s16 step = 0;
		 step < (extended_track_width() > extended_track_height() ? extended_track_width()
																  : extended_track_height());
		 step++) {
		struct OWOOT_POINT direction;
		legacy_s32 distance;
		legacy_s16 next = jump_next(piece, reverse, &approach, step == 0, &direction, &distance);
		if (next < 0) {
			return 0;
		}
		if (step != 0 && (direction.x != approach.x || direction.z != approach.z)) {
			return 0;
		}
		approach = direction;
		if (distance == OWOOT_SINGLE_GAP_DISTANCE) {
			*source = piece;
			*target = next;
			return 1;
		}
		if (distance != OWOOT_TILE_SIZE) {
			return 0;
		}
		piece = next;
	}
	return 0;
}

static void jump_clear(struct CARSTATE *carstate)
{
	carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] = 0;
	carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_TARGET] = 0;
	carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_LAUNCH] = 0;
}

static legacy_s16 jump_has_road_contact(const struct CARSTATE *carstate)
{
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		if (carstate->car_surfaceWhl[wheel] >= CAR_SURFACE_PAVED &&
			carstate->car_surfaceWhl[wheel] <= CAR_SURFACE_ICE) {
			return 1;
		}
	}
	return 0;
}

static legacy_s32 jump_cross(const struct VECTOR *a, const struct VECTOR *b, const struct VECTOR *c)
{
	return ((legacy_s32)b->x - a->x) * ((legacy_s32)c->z - a->z) -
		   ((legacy_s32)b->z - a->z) * ((legacy_s32)c->x - a->x);
}

/* The projection of a tire is convex. Its bounds provide the two rectangle
 * separating axes; hull edges provide the remaining axes. A diagonal tire
 * near a corner must actually reach the permitted gap, not merely its bounds.
 * The exception covers the complete grass tile between the paired connectors,
 * rather than a road-width strip drawn through that tile. */
static legacy_s16 jump_footprint_in_gap(struct VECTOR *points, legacy_u16 count)
{
	legacy_u16 hull[OWOOT_WHEEL_VERTEX_COUNT * 2];
	if (count == 0 || count > OWOOT_WHEEL_VERTEX_COUNT) {
		return 0;
	}
	for (legacy_u16 i = 1; i < count; i++) {
		struct VECTOR point = points[i];
		legacy_u16 j = i;
		while (j > 0 && (points[j - 1].x > point.x ||
						 (points[j - 1].x == point.x && points[j - 1].z > point.z))) {
			points[j] = points[j - 1];
			j--;
		}
		points[j] = point;
	}
	legacy_s16 minimum_along = points[0].z;
	legacy_s16 maximum_along = points[0].z;
	for (legacy_u16 i = 1; i < count; i++) {
		if (points[i].z < minimum_along) {
			minimum_along = points[i].z;
		}
		if (points[i].z > maximum_along) {
			maximum_along = points[i].z;
		}
	}
	if (points[0].x > OWOOT_GAP_HALF_WIDTH || points[count - 1].x < -OWOOT_GAP_HALF_WIDTH ||
		minimum_along > OWOOT_SINGLE_GAP_DISTANCE - TRACK_TILE_HALF_SIZE ||
		maximum_along < TRACK_TILE_HALF_SIZE) {
		return 0;
	}
	legacy_u16 length = 0;
	for (legacy_u16 i = 0; i < count; i++) {
		while (length >= 2 &&
			   jump_cross(&points[hull[length - 2]], &points[hull[length - 1]], &points[i]) <= 0) {
			length--;
		}
		hull[length++] = i;
	}
	legacy_u16 lower = length + 1;
	for (legacy_s16 i = (legacy_s16)count - 2; i >= 0; i--) {
		while (length >= lower &&
			   jump_cross(&points[hull[length - 2]], &points[hull[length - 1]], &points[i]) <= 0) {
			length--;
		}
		hull[length++] = (legacy_u16)i;
	}
	if (length > 1) {
		length--;
	}
	for (legacy_u16 i = 0; i < length; i++) {
		const struct VECTOR *a = &points[hull[i]];
		const struct VECTOR *b = &points[hull[(i + 1) % length]];
		struct VECTOR corner;
		corner.x = b->z <= a->z ? OWOOT_GAP_HALF_WIDTH : -OWOOT_GAP_HALF_WIDTH;
		corner.z =
			b->x >= a->x ? OWOOT_SINGLE_GAP_DISTANCE - TRACK_TILE_HALF_SIZE : TRACK_TILE_HALF_SIZE;
		if (jump_cross(a, b, &corner) < 0) {
			return 0;
		}
	}
	return 1;
}

static legacy_s16 jump_wheel_in_gap(const struct CARSTATE *carstate,
									const struct OWOOT_POINT *source,
									const struct OWOOT_POINT *direction)
{
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		struct VECTOR points[OWOOT_WHEEL_VERTEX_COUNT];
		legacy_u16 count = owoot_wheel_footprint(carstate, wheel, points);
		for (legacy_u16 point = 0; point < count; point++) {
			struct OWOOT_POINT absolute = route_vector(&points[point]);
			legacy_s32 dx = absolute.x - source->x;
			legacy_s32 dz = absolute.z - source->z;
			points[point].z = (legacy_s16)(dx * direction->x + dz * direction->z);
			points[point].x = (legacy_s16)(dx * direction->z - dz * direction->x);
		}
		if (jump_footprint_in_gap(points, count)) {
			return 1;
		}
	}
	return 0;
}

legacy_s16 owoot_jump_is_valid(struct CARSTATE *carstate)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		extended_geometry_set_context(carstate->car_position.lx, carstate->car_position.lz);
	}
#endif
	struct OWOOT_POINT current = route_position(&carstate->car_position);
	struct OWOOT_POINT previous = route_position(&carstate->car_previous_position);
	if (carstate->car_sumSurfAllWheels != 0) {
		legacy_s16 saved_source = carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE];
		legacy_s16 saved_target = carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_TARGET];
		legacy_s16 saved_launch = carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_LAUNCH];
		jump_clear(carstate);
		if (!jump_has_road_contact(carstate)) {
			return 0;
		}
		for (legacy_s16 piece = 0; piece < track_pieces_counter; piece++) {
			if (!jump_model(piece)) {
				continue;
			}
			struct OWOOT_BOUNDS bounds;
			route_bounds(piece, &bounds);
			if (!route_contains(&bounds, &current) || !jump_on_deck(piece, &current)) {
				continue;
			}
			for (legacy_s16 reverse = 0; reverse < 2; reverse++) {
				legacy_s16 source, target;
				if (!jump_find_gap(piece, reverse, &source, &target)) {
					continue;
				}
				struct OWOOT_POINT direction;
				legacy_s32 distance;
				if (!jump_aligned(source, target, &direction, &distance) ||
					(current.x - previous.x) * direction.x +
							(current.z - previous.z) * direction.z <=
						0) {
					continue;
				}
				carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] = source + 1;
				carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_TARGET] = target + 1;
				carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_LAUNCH] = piece + 1;
				struct OWOOT_POINT source_center;
				if (jump_source_center(source, target, &source_center)) {
					return jump_wheel_in_gap(carstate, &source_center, &direction);
				}
				return 0;
			}
		}
		/* The center can still be over the gap while rear tires touch the
		 * takeoff or front tires first touch the receiving deck. Keep that
		 * proven corridor until the tire footprint has left the gap. */
		for (legacy_u16 endpoint = 0; endpoint < 2; endpoint++) {
			legacy_s16 contact_piece = (endpoint == 0 ? saved_source : saved_target) - 1;
			if (contact_piece < 0 || contact_piece >= track_pieces_counter) {
				continue;
			}
			struct OWOOT_BOUNDS bounds;
			route_bounds(contact_piece, &bounds);
			for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
				if (carstate->car_surfaceWhl[wheel] < CAR_SURFACE_PAVED ||
					carstate->car_surfaceWhl[wheel] > CAR_SURFACE_ICE) {
					continue;
				}
				struct OWOOT_POINT contact =
					route_vector(&carstate->car_wheel_contact_positions[wheel]);
				if (!route_contains(&bounds, &contact) || !jump_on_deck(contact_piece, &contact)) {
					continue;
				}
				struct OWOOT_POINT direction, source_center;
				legacy_s32 distance;
				if (jump_aligned(saved_source - 1, saved_target - 1, &direction, &distance) &&
					jump_source_center(saved_source - 1, saved_target - 1, &source_center)) {
					carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] = saved_source;
					carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_TARGET] = saved_target;
					carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_LAUNCH] = saved_launch;
					return jump_wheel_in_gap(carstate, &source_center, &direction);
				}
			}
		}
		return 0;
	}
	legacy_s16 source = carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] - 1;
	legacy_s16 target = carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_TARGET] - 1;
	legacy_s16 launch = carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_LAUNCH] - 1;
	if (source < 0 || source >= track_pieces_counter || target < 0 ||
		target >= track_pieces_counter || launch < 0 || launch >= track_pieces_counter) {
		jump_clear(carstate);
		return 0;
	}
	struct OWOOT_POINT direction;
	legacy_s32 distance;
	if (!jump_aligned(source, target, &direction, &distance) ||
		distance != OWOOT_SINGLE_GAP_DISTANCE) {
		jump_clear(carstate);
		return 0;
	}
	struct OWOOT_POINT source_center;
	jump_source_center(source, target, &source_center);
	struct OWOOT_BOUNDS launch_bounds;
	route_bounds(launch, &launch_bounds);
	legacy_s32 along =
		(current.x - source_center.x) * direction.x + (current.z - source_center.z) * direction.z;
	legacy_s32 start_x = direction.x > 0 ? launch_bounds.minimum_x : launch_bounds.maximum_x;
	legacy_s32 start_z = direction.z > 0 ? launch_bounds.minimum_z : launch_bounds.maximum_z;
	legacy_s32 start =
		(start_x - source_center.x) * direction.x + (start_z - source_center.z) * direction.z;
	if (along < start || along > distance + TRACK_TILE_HALF_SIZE ||
		(current.x - previous.x) * direction.x + (current.z - previous.z) * direction.z < 0) {
		jump_clear(carstate);
		return 0;
	}
	/* Before and after the empty tile ordinary wheel/road overlap applies. */
	return jump_wheel_in_gap(carstate, &source_center, &direction);
}
