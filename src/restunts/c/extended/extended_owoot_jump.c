/* Validate road gaps and contact-derived barrel-roll airborne progress. */
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
		legacy_s32 actual_column = (legacy_s32)(position->x / 1024L);
		legacy_s32 world_row = (legacy_s32)(position->z / 1024L);
		elevation = extended_track_elevation(actual_column, extended_track_height() - 1 - world_row,
											 (legacy_s32)(position->x - actual_column * 1024L),
											 (legacy_s32)(1024L - position->z + world_row * 1024L));
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

#ifdef RESTUNTS_SDL3
#define OWOOT_ROLL_NOT_APPLICABLE (-2)
#define OWOOT_ROLL_ANGLE_BITS 10U
#define OWOOT_ROLL_UPRIGHT_TOLERANCE (ANGLE_FULL_TURN / 64)
#define OWOOT_ROLL_NORMAL_SCALE (TRIG_FIXED_ONE / 2)

enum OWOOT_ROLL_PHASE {
	OWOOT_ROLL_ARMED,
	OWOOT_ROLL_BEFORE_SIDE,
	OWOOT_ROLL_PASSED_SIDE,
	OWOOT_ROLL_PASSED_INVERTED,
	OWOOT_ROLL_PASSED_OTHER_SIDE,
	OWOOT_ROLL_COMPLETE
};

static struct VECTOR roll_jump_grade_normal(const struct CARSTATE *carstate,
											const struct CSTRK_OBJECT *placed)
{
	struct VECTOR normal = {0, OWOOT_ROLL_NORMAL_SCALE, 0};
	struct VECTOR heading, level;
	struct MATRIX rotation;
	extended_geometry_grade_normal(placed, &normal);
	mat_rot_y(&rotation, carstate->car_rotate.x);
	mat_mul_vector(&normal, &rotation, &heading);
	mat_rot_x(&rotation, carstate->car_rotate.y);
	mat_mul_vector(&heading, &rotation, &level);
	return level;
}

static legacy_u16 roll_jump_angle(const struct CARSTATE *carstate,
								  const struct CSTRK_OBJECT *placed)
{
	struct VECTOR normal = roll_jump_grade_normal(carstate, placed);
	return (legacy_u16)(carstate->car_rotate.z - polarAngle(normal.x, normal.y)) & ANGLE_MASK;
}

static legacy_s16 roll_jump_lands_wheels_down(const struct CARSTATE *carstate,
											  const struct CSTRK_OBJECT *placed)
{
	struct VECTOR normal = roll_jump_grade_normal(carstate, placed);
	legacy_u16 roll = (legacy_u16)carstate->car_rotate.z;
	legacy_s32 up = (legacy_s32)normal.x * sin_fast(roll) + (legacy_s32)normal.y * cos_fast(roll);
	return up > 0;
}

static void roll_jump_store(struct CARSTATE *carstate, legacy_u16 angle, legacy_u16 phase)
{
	/* This word normally names a launch piece. For this custom jump the
	 * source already identifies that piece, leaving one serialized word for
	 * its previous angle and ordered airborne milestones. */
	carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_LAUNCH] =
		(legacy_s16)(angle | (phase << OWOOT_ROLL_ANGLE_BITS));
}

static legacy_s16 roll_jump_fail(struct CARSTATE *carstate)
{
	jump_clear(carstate);
	return OWOOT_JUMP_FAILED;
}

static legacy_s16 roll_jump_airborne_progress(struct CARSTATE *carstate,
											  const struct CSTRK_OBJECT *placed)
{
	legacy_u16 saved = (legacy_u16)carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_LAUNCH];
	legacy_u16 previous = saved & ANGLE_MASK;
	legacy_u16 phase = saved >> OWOOT_ROLL_ANGLE_BITS;
	legacy_u16 angle = roll_jump_angle(carstate, placed);
	legacy_s16 delta =
		(legacy_s16)(((angle - previous + ANGLE_HALF_TURN) & ANGLE_MASK) - ANGLE_HALF_TURN);
	/* A half-turn jump is ambiguous. Smaller forward sweeps can cross
	 * several gates at 10 Hz; reversing invalidates this attempt instead of
	 * retaining milestones collected by rocking back and forth. */
	if (delta < 0 || phase > OWOOT_ROLL_COMPLETE) {
		return 0;
	}
	if (phase == OWOOT_ROLL_ARMED) {
		if (previous >= ANGLE_QUARTER_TURN) {
			return 0;
		}
		phase = OWOOT_ROLL_BEFORE_SIDE;
	}
	legacy_s32 end = (legacy_s32)previous + delta;
	while (phase < OWOOT_ROLL_COMPLETE) {
		legacy_s32 threshold = phase == OWOOT_ROLL_PASSED_OTHER_SIDE
								   ? ANGLE_FULL_TURN - OWOOT_ROLL_UPRIGHT_TOLERANCE
								   : (legacy_s32)phase * ANGLE_QUARTER_TURN;
		if (previous >= threshold || end < threshold) {
			break;
		}
		phase++;
	}
	roll_jump_store(carstate, angle, phase);
	return 1;
}

static legacy_s16 roll_jump_touches_ramp(const struct CSTRK_OBJECT *placed,
										 const struct OWOOT_POINT *contact)
{
	struct OWOOT_POINT origin = route_extended_origin(placed);
	struct VECTOR local = {(legacy_s16)(contact->x - origin.x), (legacy_s16)(contact->y - origin.y),
						   (legacy_s16)(contact->z - origin.z)};
	local.y =
		LEGACY_S16_WRAP_SUB(local.y, extended_geometry_object_grade(placed, local.x, local.z));
	legacy_s16 x = local.x, z = local.z;
	switch (roll_ramp_orientation(placed)) {
		case ANGLE_QUARTER_TURN:
			local.x = LEGACY_S16_WRAP_NEGATE(z);
			local.z = x;
			break;
		case ANGLE_HALF_TURN:
			local.x = LEGACY_S16_WRAP_NEGATE(x);
			local.z = LEGACY_S16_WRAP_NEGATE(z);
			break;
		case ANGLE_THREE_QUARTER_TURN:
			local.x = z;
			local.z = LEGACY_S16_WRAP_NEGATE(x);
			break;
	}
	legacy_s16 along = ROLL_RAMP_HALF_LENGTH - local.z;
	if (along < CSTRK_TILE_WORLD_SIZE || along > ROLL_RAMP_LENGTH) {
		return 0;
	}
	legacy_s16 cosine = cos_fast(roll_ramp_bank_angle(along));
	if (cosine <= 0) {
		return 0;
	}
	legacy_s32 lateral = (legacy_s32)local.x * TRIG_FIXED_ONE / cosine;
	if (lateral < -ROLL_RAMP_HALF_WIDTH || lateral > ROLL_RAMP_HALF_WIDTH) {
		return 0;
	}
	struct VECTOR surface, normal;
	roll_ramp_sample(along, (legacy_s16)lateral, &surface, &normal);
	legacy_s32 distance =
		((legacy_s32)local.x - surface.x) * normal.x + ((legacy_s32)local.y - surface.y) * normal.y;
	return route_abs(distance) <=
		   (legacy_s32)PHYSICS_PLANE_CONTACT_TOLERANCE * polarRadius3D(&normal);
}

static legacy_s16 roll_jump_has_ramp_contact(const struct CARSTATE *carstate,
											 const struct CSTRK_OBJECT *placed, legacy_s16 piece)
{
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		if (carstate->car_surfaceWhl[wheel] < CAR_SURFACE_PAVED ||
			carstate->car_surfaceWhl[wheel] > CAR_SURFACE_ICE) {
			continue;
		}
		struct OWOOT_POINT contact = route_vector(&carstate->car_wheel_contact_positions[wheel]);
		if (route_owns_position(piece, &contact) && roll_jump_touches_ramp(placed, &contact)) {
			return 1;
		}
	}
	return 0;
}

/* A roll launch has a longer, explicitly compiled landing corridor. Arming
 * requires actual tire contact with that ramp; merely flying above it cannot
 * enable this exception. Every other jump retains the native one-cell rule. */
static legacy_s16 roll_jump_is_valid(struct CARSTATE *carstate)
{
	legacy_s16 source = carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] - 1;
	const struct CSTRK_OBJECT *placed =
		source >= 0 && source < track_pieces_counter ? route_extended_object(source) : NULL;
	if (!placed || !placed->roll_ramp) {
		source = -1;
		if (carstate->car_sumSurfAllWheels != CAR_WHEEL_CONTACT_NONE &&
			jump_has_road_contact(carstate)) {
			for (legacy_s16 piece = 0; piece < track_pieces_counter; piece++) {
				if (!route_is_roll_ramp(piece)) {
					continue;
				}
				const struct CSTRK_OBJECT *candidate = route_extended_object(piece);
				if (roll_jump_has_ramp_contact(carstate, candidate, piece)) {
					source = piece;
					placed = candidate;
				}
				if (source >= 0) {
					break;
				}
			}
		}
		if (source < 0) {
			return OWOOT_ROLL_NOT_APPLICABLE;
		}
		legacy_s16 target = track_primary_route_links[source];
		if (target < 0 || target >= track_pieces_counter) {
			return roll_jump_fail(carstate);
		}
		carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] = source + 1;
		carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_TARGET] = target + 1;
		roll_jump_store(carstate, roll_jump_angle(carstate, placed), OWOOT_ROLL_ARMED);
	}
	legacy_s16 target = carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_TARGET] - 1;
	if (target < 0 || target >= track_pieces_counter) {
		return roll_jump_fail(carstate);
	}
	static const legacy_s8 forward_x[] = {0, -1, 0, 1};
	static const legacy_s8 forward_z[] = {-1, 0, 1, 0};
	legacy_u16 direction = (legacy_u16)roll_ramp_orientation(placed) / ANGLE_QUARTER_TURN;
	legacy_s32 dx = forward_x[direction], dz = forward_z[direction];
	struct OWOOT_POINT origin = route_extended_origin(placed);
	struct OWOOT_POINT lip = {origin.x + dx * ROLL_RAMP_HALF_LENGTH, 0,
							  origin.z + dz * ROLL_RAMP_HALF_LENGTH};
	struct OWOOT_BOUNDS landing;
	route_bounds(target, &landing);
	legacy_s32 center_x = (landing.minimum_x + landing.maximum_x) / 2;
	legacy_s32 center_z = (landing.minimum_z + landing.maximum_z) / 2;
	legacy_s32 distance = (center_x - lip.x) * dx + (center_z - lip.z) * dz;
	legacy_s32 lateral = (center_x - lip.x) * dz - (center_z - lip.z) * dx;
	if (lateral != 0 || distance < TRACK_TILE_HALF_SIZE ||
		distance > (ROLL_RAMP_JUMP_MAX_EMPTY_TILES + 1L) * OWOOT_TILE_SIZE) {
		return roll_jump_fail(carstate);
	}
	struct OWOOT_POINT current = route_position(&carstate->car_position);
	struct OWOOT_POINT previous = route_position(&carstate->car_previous_position);
	legacy_s16 on_landing = route_contains(&landing, &current);
	legacy_s16 landing_piece = on_landing ? target : -1;
	legacy_s16 receiver = target;
	/* The first receiving road can begin before touchdown. Follow only its
	 * compiled straight runout, bounded by the roll launch's flight budget. */
	for (legacy_u16 step = 0; step < ROLL_RAMP_JUMP_MAX_EMPTY_TILES; step++) {
		legacy_s16 next = track_primary_route_links[receiver];
		if (next < 0 || next >= track_pieces_counter || next == source ||
			route_object(next)->ss_physicalModel != PHYSICAL_MODEL_ROAD) {
			break;
		}
		struct OWOOT_BOUNDS runout;
		route_bounds(next, &runout);
		legacy_s32 next_x = (runout.minimum_x + runout.maximum_x) / 2;
		legacy_s32 next_z = (runout.minimum_z + runout.maximum_z) / 2;
		legacy_s32 next_distance = (next_x - lip.x) * dx + (next_z - lip.z) * dz;
		if ((next_x - lip.x) * dz - (next_z - lip.z) * dx != 0 || next_distance <= distance ||
			next_distance - distance > OWOOT_TILE_SIZE ||
			next_distance > (ROLL_RAMP_JUMP_MAX_EMPTY_TILES + 1L) * OWOOT_TILE_SIZE) {
			break;
		}
		distance = next_distance;
		if (route_contains(&runout, &current)) {
			on_landing = 1;
			landing_piece = next;
		}
		receiver = next;
	}
	legacy_s32 along = (current.x - lip.x) * dx + (current.z - lip.z) * dz;
	lateral = (current.x - lip.x) * dz - (current.z - lip.z) * dx;
	legacy_u16 roll_phase =
		(legacy_u16)carstate->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_LAUNCH] >>
		OWOOT_ROLL_ANGLE_BITS;
	if ((current.x - previous.x) * dx + (current.z - previous.z) * dz < 0) {
		if (roll_phase == OWOOT_ROLL_ARMED &&
			carstate->car_sumSurfAllWheels != CAR_WHEEL_CONTACT_NONE) {
			/* Backing out before takeoff abandons the attempt. The ordinary
			 * route gates still decide whether that movement is permitted. */
			jump_clear(carstate);
			return OWOOT_ROLL_NOT_APPLICABLE;
		}
		return roll_jump_fail(carstate);
	}
	if (route_abs(lateral) > OWOOT_GAP_HALF_WIDTH || along > distance + TRACK_TILE_HALF_SIZE) {
		return roll_jump_fail(carstate);
	}
	if (carstate->car_sumSurfAllWheels != CAR_WHEEL_CONTACT_NONE) {
		if (!jump_has_road_contact(carstate)) {
			return roll_jump_fail(carstate);
		}
		if (on_landing) {
			/* A landing correction must not supply the missing last part of
			 * the roll. Completion must already have happened unsupported. */
			const struct CSTRK_OBJECT *receiver_object = route_extended_object(landing_piece);
			/* First tire contact can precede the suspension settling upright.
			 * The car's up vector must face the receiving road normal: a
			 * side or roof landing cannot complete this stunt. */
			if (roll_phase != OWOOT_ROLL_COMPLETE ||
				!roll_jump_lands_wheels_down(carstate, receiver_object)) {
				return roll_jump_fail(carstate);
			}
			jump_clear(carstate);
			return OWOOT_ROLL_NOT_APPLICABLE;
		}
		if (roll_phase != OWOOT_ROLL_ARMED) {
			return roll_jump_fail(carstate);
		}
		if (!roll_jump_has_ramp_contact(carstate, placed, source)) {
			jump_clear(carstate);
			return OWOOT_ROLL_NOT_APPLICABLE;
		}
		/* Supported ramp motion never earns airborne milestones. */
		roll_jump_store(carstate, roll_jump_angle(carstate, placed), OWOOT_ROLL_ARMED);
	} else if (along >= 0) {
		if (!roll_jump_airborne_progress(carstate, placed)) {
			return roll_jump_fail(carstate);
		}
	} else {
		/* Rotation above the ramp cannot complete a gate beyond its lip. */
		roll_jump_store(carstate, roll_jump_angle(carstate, placed), OWOOT_ROLL_ARMED);
	}
	return along >= -ROLL_RAMP_HALF_WIDTH;
}
#endif

legacy_s16 extended_owoot_jump_is_valid(struct CARSTATE *carstate)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		extended_geometry_set_context(carstate->car_position.lx, carstate->car_position.lz);
	}
#endif
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		legacy_s16 roll_jump = roll_jump_is_valid(carstate);
		if (roll_jump != OWOOT_ROLL_NOT_APPLICABLE) {
			return roll_jump;
		}
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
