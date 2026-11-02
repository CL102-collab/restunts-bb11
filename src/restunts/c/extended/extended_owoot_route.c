/* Shared OWOOT traversal engine. Format-aware geometry, portal and stunt
 * policy live together here so legacy entry points only delegate. */
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

legacy_s32 route_abs(legacy_s32 value)
{
	return value < 0 ? -value : value;
}

struct OWOOT_POINT route_position(const struct VECTORLONG *position)
{
	struct OWOOT_POINT result;
	result.x = position->lx / OWOOT_POSITION_SCALE;
	result.y = position->ly / OWOOT_POSITION_SCALE;
	result.z = position->lz / OWOOT_POSITION_SCALE;
	return result;
}

struct OWOOT_POINT route_vector(const struct VECTOR *vector)
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

struct OWOOT_POINT route_delta(const struct OWOOT_POINT *first, const struct OWOOT_POINT *second)
{
	struct OWOOT_POINT result;
	result.x = first->x - second->x;
	result.y = first->y - second->y;
	result.z = first->z - second->z;
	return result;
}

legacy_s32 route_dot(const struct OWOOT_POINT *first, const struct OWOOT_POINT *second)
{
	return first->x * second->x + first->y * second->y + first->z * second->z;
}

legacy_s32 route_maximum_component(const struct OWOOT_POINT *vector)
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

void route_normalize(struct OWOOT_POINT *vector)
{
	legacy_s32 maximum = route_maximum_component(vector);
	if (maximum != 0) {
		vector->x = vector->x * OWOOT_GATE_SCALE / maximum;
		vector->y = vector->y * OWOOT_GATE_SCALE / maximum;
		vector->z = vector->z * OWOOT_GATE_SCALE / maximum;
	}
}

const struct TRACKOBJECT *route_object(legacy_s16 piece)
{
	return &trkObjectList[(legacy_u8)track_route_element_ids[piece]];
}

legacy_s16 route_is_roll_ramp(legacy_s16 piece)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		const struct CSTRK_OBJECT *placed = extended_track_object_for_id(
			(legacy_u8)track_route_columns[piece], (legacy_u8)track_route_rows[piece],
			(legacy_u8)track_route_element_ids[piece]);
		return placed && placed->roll_ramp;
	}
#else
	(void)piece;
#endif
	return 0;
}

static legacy_s16 route_is_tunnel(legacy_s16 piece)
{
	if (route_object(piece)->ss_physicalModel == PHYSICAL_MODEL_TUNNEL) {
		return 1;
	}
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		const struct CSTRK_OBJECT *placed = extended_track_object_for_id(
			(legacy_u8)track_route_columns[piece], (legacy_u8)track_route_rows[piece],
			(legacy_u8)track_route_element_ids[piece]);
		return placed && placed->tunnel_slalom;
	}
#endif
	return 0;
}

const struct TRKOBJINFO *route_info(legacy_s16 piece)
{
	const struct TRACKOBJECT *object = route_object(piece);
	const struct TRKOBJINFO *blocks = object->ss_trkObjInfoPtr;
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		blocks = extended_track_route_info((legacy_u8)track_route_columns[piece],
										   (legacy_u8)track_route_rows[piece],
										   (legacy_u8)track_route_element_ids[piece]);
	}
#endif
	if (blocks == 0) {
		return 0;
	}
	return &blocks[(legacy_u8)track_route_traversal_flags[piece] & OWOOT_ROUTE_SUBTYPE_MASK];
}

static legacy_s16 route_gate_count(legacy_s16 piece)
{
	/* The entry/exit fans join the spiral over a range of angular phases.
	 * Require both sides and its crown, rather than the AI's intermediate
	 * subdivisions of those continuously drivable surfaces. */
	if (route_object(piece)->ss_physicalModel == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT &&
		!route_is_roll_ramp(piece)) {
		return OWOOT_CORKSCREW_GATE_COUNT;
	}
	if (route_object(piece)->ss_physicalModel == PHYSICAL_MODEL_SLALOM) {
		return OWOOT_SLALOM_GATE_COUNT;
	}
	return (legacy_u8)route_info(piece)->route_point_count;
}

void route_bounds(legacy_s16 piece, struct OWOOT_BOUNDS *bounds)
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

legacy_s16 route_contains(const struct OWOOT_BOUNDS *bounds, const struct OWOOT_POINT *position)
{
	return position->x >= bounds->minimum_x && position->x < bounds->maximum_x &&
		   position->z >= bounds->minimum_z && position->z < bounds->maximum_z;
}

/* Branches can give the same physical element forward and reverse route nodes.
 * Completing one must not re-enter its alias while a rear tire still overlaps it. */
static legacy_s16 route_same_object(legacy_s16 first, legacy_s16 second)
{
	return second >= 0 && second < track_pieces_counter &&
		   track_route_columns[first] == track_route_columns[second] &&
		   track_route_rows[first] == track_route_rows[second] &&
		   track_route_element_ids[first] == track_route_element_ids[second];
}

static legacy_s16 route_requires_gates(legacy_s16 piece)
{
	legacy_s16 model = route_object(piece)->ss_physicalModel;
	return model >= PHYSICAL_MODEL_BANKED_ENTRANCE_B &&
		   model <= PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT;
}

legacy_s16 route_has_finite_portal(legacy_s16 piece)
{
	legacy_s16 model = route_object(piece)->ss_physicalModel;
	return route_is_tunnel(piece) || model == PHYSICAL_MODEL_PIPE ||
		   model == PHYSICAL_MODEL_HALF_PIPE;
}

legacy_s16 route_owns_position(legacy_s16 piece, const struct OWOOT_POINT *position)
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
		const struct CSTRK_OBJECT *placed = extended_track_object_for_id(
			(legacy_u8)track_route_columns[piece], (legacy_u8)track_route_rows[piece],
			(legacy_u8)track_route_element_ids[piece]);
		if (!placed || column < placed->x || row < placed->y ||
			column >= placed->x + placed->width || row >= placed->y + placed->height) {
			return 0;
		}
		if (extended_track_object_at(column, row) == placed) {
			return 1;
		}
		legacy_u16 full_mask = (1U << (placed->width * placed->height)) - 1U;
		if (placed->occupied_mask == full_mask) {
			return 0;
		}
		/* The small road arc in a free corner still belongs to this route.
		 * Another piece placed in the rest of that cell keeps its own gates. */
		struct VECTOR local = {
			(legacy_s16)(position->x - placed->x * CSTRK_TILE_WORLD_SIZE -
						 placed->width * (CSTRK_TILE_WORLD_SIZE / 2)),
			0,
			(legacy_s16)(position->z -
						 (extended_track_height() - placed->y) * CSTRK_TILE_WORLD_SIZE +
						 placed->height * (CSTRK_TILE_WORLD_SIZE / 2))};
		legacy_s16 x = local.x, z = local.z;
		switch ((legacy_u16)route_object(piece)->ss_rotY) {
			case ANGLE_QUARTER_TURN:
				local.x = -z;
				local.z = x;
				break;
			case ANGLE_HALF_TURN:
				local.x = -x;
				local.z = -z;
				break;
			case ANGLE_THREE_QUARTER_TURN:
				local.x = z;
				local.z = -x;
				break;
		}
		return track_road_contains_local_point(route_object(piece)->ss_physicalModel, &local);
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
	if (direction == 0 || (route_is_roll_ramp(piece) && direction < 0)) {
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

legacy_s16 extended_owoot_route_is_valid(struct CARSTATE *carstate, legacy_s16 allowed_jump)
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
		if (route_same_object(piece, active) || !route_requires_gates(piece) ||
			route_info(piece) == 0 || (legacy_u8)route_info(piece)->route_point_count < 2U) {
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
