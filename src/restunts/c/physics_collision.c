#include "legacy.h"
#include "math.h"
#include "physics_internal.h"
#include "residue.h"
#include "trackdata_layout.h"
#include "track_objects.h"
#include "track_collision.h"
#include "externs.h"
#ifdef RESTUNTS_SDL3
#include "extended_track.h"
#include "extended_geometry.h"
#endif

#define COLLISION_ANGLE_DAMPING_NUMERATOR 15
#define COLLISION_ANGLE_DAMPING_SHIFT 4U
#define COLLISION_ANGLE_DAMPING_DIVISOR 16L

static legacy_s16 legacy_collision_enabled = 1;

legacy_u16 legacy_collision_is_enabled(void)
{
	return legacy_collision_enabled != 0;
}

void configure_legacy_collision(legacy_s16 argc, legacy_s8 *argv[])
{
	static const legacy_s8 off_option[] = "--lc:off";
	static const legacy_s8 on_option[] = "--lc:on";
	legacy_collision_enabled = 1;
	for (legacy_s16 index = 1; index < argc; index++) {
		if (stricmp(argv[index], off_option) == 0) {
			legacy_collision_enabled = 0;
		} else if (stricmp(argv[index], on_option) == 0) {
			legacy_collision_enabled = 1;
		}
	}
}

legacy_s16 damp_collision_angle(legacy_s16 angle)
{
	if (legacy_collision_enabled != 0) {
		return LEGACY_S16_SAR(LEGACY_S16_WRAP_MUL(angle, COLLISION_ANGLE_DAMPING_NUMERATOR),
							  COLLISION_ANGLE_DAMPING_SHIFT);
	}

	/* Round toward zero so negative offsets cannot retain a permanent turn.
	 * Widen before multiplying to keep recovery monotonic at large angles. */
	return (legacy_s16)((legacy_s32)angle * COLLISION_ANGLE_DAMPING_NUMERATOR /
						COLLISION_ANGLE_DAMPING_DIVISOR);
}

static legacy_s16 interpolate_collision_axis(legacy_s16 first, legacy_s16 second, legacy_s32 factor,
											 legacy_s32 divisor)
{
	legacy_s32 difference = (legacy_s32)first - (legacy_s32)second;
	legacy_s32 product = LEGACY_S32_WRAP_MUL(difference, factor);
	legacy_s32 quotient = LEGACY_S32_DIV_OR_ZERO(product, divisor);
	return LEGACY_S16_WRAP_ADD(LEGACY_S16_FROM_BITS((legacy_u16)quotient), second);
}

void interpolate_collision_at_z(struct VECTOR *first, struct VECTOR *second, struct VECTOR *result,
								legacy_s16 depth)
{
	if (legacy_collision_enabled != 0) {
		vector_interpolate_at_z(first, second, result, depth);
		return;
	}

	/* Restore the earlier signed interpolation for physics. The shared math
	 * helper keeps the original 16-bit behavior used by renderer clipping. */
	legacy_s32 depth_offset = (legacy_s32)depth - (legacy_s32)second->z;
	legacy_s32 depth_span = (legacy_s32)first->z - (legacy_s32)second->z;
	if (depth_span < 0) {
		/* Retain the earlier arithmetic shifts, including rounding of odd depths. */
		depth_offset = LEGACY_S32_SAR(depth_offset, 1U);
		depth_span = LEGACY_S32_SAR(depth_span, 1U);
	}

	result->x = interpolate_collision_axis(first->x, second->x, depth_offset, depth_span);
	result->y = interpolate_collision_axis(first->y, second->y, depth_offset, depth_span);
	result->z = depth;
}

/* Plane normals use half the rotation matrix's fixed-point scale. */
#define COLLISION_NORMAL_SCALE 8192L

static legacy_s16 sweep_coordinate(legacy_s16 previous, legacy_s16 current, legacy_s16 fraction)
{
	legacy_s32 delta = (legacy_s32)current - (legacy_s32)previous;
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		delta = LEGACY_S16_WRAP_SUB(current, previous);
	}
#endif
	legacy_s32 displacement = delta * fraction / TRIG_FIXED_ONE;
	return LEGACY_S16_WRAP_ADD(previous, LEGACY_S16_FROM_BITS((legacy_u16)displacement));
}

static legacy_s16 selected_body_plane_is_coplanar(const struct TRACK_COLLISION_SNAPSHOT *other)
{
	const struct PLANE far *plane = current_planptr;
	const struct PLANE far *other_plane = other->plane;
#ifdef RESTUNTS_SDL3
	if (other->extended_plane_valid) {
		other_plane = &other->extended_plane;
	}
#endif
	if (plane->plane_normal.x != other_plane->plane_normal.x ||
		plane->plane_normal.y != other_plane->plane_normal.y ||
		plane->plane_normal.z != other_plane->plane_normal.z) {
		return 0;
	}

	legacy_s32 delta_x = (legacy_s32)elem_xCenter - other->element_x + plane->plane_origin.x -
						 other_plane->plane_origin.x;
	legacy_s32 delta_y = (legacy_s32)terrainHeight - other->terrain_height + plane->plane_origin.y -
						 other_plane->plane_origin.y;
	legacy_s32 delta_z = (legacy_s32)elem_zCenter - other->element_z + plane->plane_origin.z -
						 other_plane->plane_origin.z;
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		delta_x = (legacy_s32)LEGACY_S16_WRAP_SUB(elem_xCenter, other->element_x) +
				  plane->plane_origin.x - other_plane->plane_origin.x;
		delta_z = (legacy_s32)LEGACY_S16_WRAP_SUB(elem_zCenter, other->element_z) +
				  plane->plane_origin.z - other_plane->plane_origin.z;
	}
#endif
	return (legacy_s64)delta_x * plane->plane_normal.x +
			   (legacy_s64)delta_y * plane->plane_normal.y +
			   (legacy_s64)delta_z * plane->plane_normal.z ==
		   0;
}

static legacy_s64 body_plane_distance(const struct VECTOR *point)
{
	const struct PLANE far *plane = current_planptr;
	legacy_s32 x = (legacy_s32)point->x - elem_xCenter - plane->plane_origin.x;
	legacy_s32 y = (legacy_s32)point->y - terrainHeight - plane->plane_origin.y;
	legacy_s32 z = (legacy_s32)point->z - elem_zCenter - plane->plane_origin.z;
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		x = (legacy_s32)LEGACY_S16_WRAP_SUB(point->x, elem_xCenter) - plane->plane_origin.x;
		z = (legacy_s32)LEGACY_S16_WRAP_SUB(point->z, elem_zCenter) - plane->plane_origin.z;
	}
#endif
	return (legacy_s64)x * plane->plane_normal.x + (legacy_s64)y * plane->plane_normal.y +
		   (legacy_s64)z * plane->plane_normal.z;
}

#define BODY_PLANE_CONTACT_BOUND_COUNT 2U

static void body_plane_intersection_axis(legacy_s16 previous, legacy_s16 current, legacy_s64 offset,
										 legacy_s64 span, legacy_s16 *first, legacy_s16 *second)
{
	legacy_s64 delta = (legacy_s64)current - previous;
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		delta = LEGACY_S16_WRAP_SUB(current, previous);
	}
#endif
	legacy_s64 numerator = (legacy_s64)previous * span + delta * offset;
	*first = (legacy_s16)(numerator / span);
	*second = *first;
	/* A fractional contact can lie inside a narrow surface while its rounded
	 * coordinate lands just outside. Retain both neighboring integer bounds. */
	if (numerator % span != 0) {
		*second += (numerator < 0) == (span < 0) ? 1 : -1;
	}
}

static legacy_s16 body_crosses_selected_plane(struct VECTOR *previous, struct VECTOR *current)
{
	legacy_s64 start = body_plane_distance(previous);
	legacy_s64 end = body_plane_distance(current);
	if (!((start < 0 && end > 0) || (start > 0 && end < 0))) {
		return 0;
	}

	/* Intersect one world plane, not distances measured from two placements of
	 * a reusable template. Keep sub-unit plane distances and the full ratio until
	 * each contact coordinate is rounded. */
	legacy_s64 offset = -start;
	legacy_s64 span = end - start;
	struct VECTOR bounds[BODY_PLANE_CONTACT_BOUND_COUNT];
	body_plane_intersection_axis(previous->x, current->x, offset, span, &bounds[0].x, &bounds[1].x);
	body_plane_intersection_axis(previous->y, current->y, offset, span, &bounds[0].y, &bounds[1].y);
	body_plane_intersection_axis(previous->z, current->z, offset, span, &bounds[0].z, &bounds[1].z);

	struct TRACK_COLLISION_SNAPSHOT selected;
	track_collision_capture(&selected);
	legacy_s16 hit = 0;
	/* Check the finite surface at the integer bounds of the exact contact.
	 * Coplanar neighboring tiles are valid too, including a contact on a seam. */
	for (legacy_u16 x = 0; x < BODY_PLANE_CONTACT_BOUND_COUNT && !hit; x++) {
		for (legacy_u16 y = 0; y < BODY_PLANE_CONTACT_BOUND_COUNT && !hit; y++) {
			for (legacy_u16 z = 0; z < BODY_PLANE_CONTACT_BOUND_COUNT && !hit; z++) {
				struct VECTOR contact;
				contact.x = bounds[x].x;
				contact.y = bounds[y].y;
				contact.z = bounds[z].z;
				build_track_object(&contact, &contact);
				hit = selected_body_plane_is_coplanar(&selected);
			}
		}
	}
	track_collision_restore(&selected);
	return hit;
}

legacy_s16 body_plane_crossing_is_collision(struct VECTOR *previous, struct VECTOR *current)
{
	if (legacy_collision_enabled != 0) {
		return 1;
	}

	/* Validate an existing body crash without changing the legacy trigger or
	 * adding collisions for unrelated plane IDs. Preserve the caller's lookup. */
	struct TRACK_COLLISION_SNAPSHOT saved;
	struct TRACK_COLLISION_SNAPSHOT selected;
	track_collision_capture(&saved);
	build_track_object(current, previous);
	track_collision_capture(&selected);
	build_track_object(previous, current);
	legacy_s16 hit = selected_body_plane_is_coplanar(&selected);
	if (!hit) {
		hit = body_crosses_selected_plane(previous, current);
		if (!hit) {
			track_collision_restore(&selected);
			hit = body_crosses_selected_plane(previous, current);
		}
	}
	track_collision_restore(&saved);
	return hit;
}

static legacy_s16 project_contact_coordinate(legacy_s16 position, legacy_s16 distance,
											 legacy_s16 normal)
{
	legacy_s32 offset = (legacy_s32)distance * normal / COLLISION_NORMAL_SCALE;
	return LEGACY_S16_WRAP_SUB(position, LEGACY_S16_FROM_BITS((legacy_u16)offset));
}

static legacy_s16 sweep_selected_track_underside(struct VECTOR *previous, struct VECTOR *current,
												 legacy_s16 *fraction)
{
	if (track_wall_collision_enabled != 0 || planindex < PHYSICS_HEIGHT_ONLY_PLANE_COUNT) {
		return 0;
	}

	legacy_s16 start = plane_signed_distance(planindex, previous->x, previous->y, previous->z);
	legacy_s16 end = plane_signed_distance(planindex, current->x, current->y, current->z);
	if (start >= -PHYSICS_PLANE_CONTACT_TOLERANCE || end <= -PHYSICS_UNDERSIDE_CLEARANCE ||
		end <= start) {
		return 0;
	}

	/* Sweep the existing body clearance: a fast wheel can jump across the whole
	 * underside contact band in one tick, including onto the front of a plane. */
	legacy_s16 contact_fraction = 0;
	if (start < -PHYSICS_UNDERSIDE_CLEARANCE) {
		legacy_s32 distance = -(legacy_s32)PHYSICS_UNDERSIDE_CLEARANCE - start;
		legacy_s32 travel = (legacy_s32)end - start;
		contact_fraction = (legacy_s16)(distance * TRIG_FIXED_ONE / travel);
	}
	struct VECTOR contact;
	contact.x = sweep_coordinate(previous->x, current->x, contact_fraction);
	contact.y = sweep_coordinate(previous->y, current->y, contact_fraction);
	contact.z = sweep_coordinate(previous->z, current->z, contact_fraction);

	/* A selected plane extends beyond its actual track surface. Validate the
	 * projected impact point so passing beside or beneath that surface is safe. */
	legacy_s16 distance = plane_signed_distance(planindex, contact.x, contact.y, contact.z);
	struct VECTOR normal = current_planptr->plane_normal;
	contact.x = project_contact_coordinate(contact.x, distance, normal.x);
	contact.y = project_contact_coordinate(contact.y, distance, normal.y);
	contact.z = project_contact_coordinate(contact.z, distance, normal.z);
	if (!track_surface_contains_point(&contact)) {
		return 0;
	}

	*fraction = contact_fraction;
	return 1;
}

legacy_s16 sweep_track_underside(struct VECTOR *previous, struct VECTOR *current,
								 legacy_s16 *fraction)
{
	if (legacy_collision_enabled != 0) {
		return 0;
	}
	/* The wheel can leave a surface's footprint before this tick's lookup.
	 * Check both selected surfaces before accepting the new ground contact. */
	return sweep_track_surface_candidates(previous, current, sweep_selected_track_underside,
										  fraction);
}

static legacy_s16 sweep_fixed_coordinate(legacy_s32 previous, legacy_s32 current,
										 legacy_s16 fraction)
{
	/* Match the fixed-position clipping applied to all wheels on impact. */
	legacy_s16 offset = scale_position_delta(current, previous, fraction, TRIG_FIXED_ONE);
	return position_to_word(LEGACY_S32_WRAP_ADD_S16(previous, offset));
}

static void sweep_position(struct VECTOR *result, struct VECTORLONG *previous,
						   struct VECTORLONG *current, legacy_s16 fraction)
{
	result->x = sweep_fixed_coordinate(previous->lx, current->lx, fraction);
	result->y = sweep_fixed_coordinate(previous->ly, current->ly, fraction);
	result->z = sweep_fixed_coordinate(previous->lz, current->lz, fraction);
}

legacy_s16 sweep_track_solid_obstacle(struct VECTORLONG *previous, struct VECTORLONG *current,
									  legacy_s16 *fraction)
{
	if (legacy_collision_enabled != 0) {
		return 0;
	}
	struct VECTOR start;
	struct VECTOR end;
	sweep_position(&start, previous, current, 0);
	sweep_position(&end, previous, current, TRIG_FIXED_ONE);
	legacy_s16 contact;
	if (!track_solid_obstacle_contact(&start, &end, &contact) ||
		track_solid_obstacle_contact(&start, &start, &contact)) {
		return 0;
	}

	/* Test the whole traveled prefix, not only its endpoint: a wheel can enter
	 * and leave a thin obstacle in one tick. Use the same fixed-position
	 * arithmetic as the eventual stop to retain the last clear position. */
	legacy_s16 clear_fraction = 0;
	legacy_s16 contact_fraction = TRIG_FIXED_ONE;
	while (contact_fraction - clear_fraction > 1) {
		legacy_s16 midpoint = clear_fraction + (contact_fraction - clear_fraction) / 2;
		sweep_position(&end, previous, current, midpoint);
		if (track_solid_obstacle_contact(&start, &end, &contact)) {
			contact_fraction = midpoint;
		} else {
			clear_fraction = midpoint;
		}
	}
	*fraction = clear_fraction;
	return 1;
}

legacy_s16 sweep_track_wall_span(struct VECTORLONG *previous_first,
								 struct VECTORLONG *previous_second,
								 struct VECTORLONG *current_first,
								 struct VECTORLONG *current_second, legacy_s16 *fraction)
{
	if (legacy_collision_enabled != 0) {
		return 0;
	}
	struct VECTOR first;
	struct VECTOR second;
	sweep_position(&first, previous_first, current_first, TRIG_FIXED_ONE);
	sweep_position(&second, previous_second, current_second, TRIG_FIXED_ONE);
	if (!track_wall_intersects_segment(&first, &second)) {
		return 0;
	}
	sweep_position(&first, previous_first, current_first, 0);
	sweep_position(&second, previous_second, current_second, 0);
	if (track_wall_intersects_segment(&first, &second)) {
		return 0;
	}

	/* A finite wall can enter between two wheel paths at its leading edge.
	 * Find the last clear car span, so stopping cannot finish beyond that edge. */
	legacy_s16 clear_fraction = 0;
	legacy_s16 contact_fraction = TRIG_FIXED_ONE;
	while (contact_fraction - clear_fraction > 1) {
		legacy_s16 midpoint = clear_fraction + (contact_fraction - clear_fraction) / 2;
		sweep_position(&first, previous_first, current_first, midpoint);
		sweep_position(&second, previous_second, current_second, midpoint);
		if (track_wall_intersects_segment(&first, &second)) {
			contact_fraction = midpoint;
		} else {
			clear_fraction = midpoint;
		}
	}
	*fraction = clear_fraction;
	return 1;
}

#define SPEED_TO_TRAVEL_NUMERATOR 1408UL
#define COLLISION_MODEL_COUNT 5U
#define PHYSICAL_MODEL_SCENERY_FIRST 71
#define PHYSICAL_MODEL_SCENERY_LAST 74
#define ELEVATED_ROAD_COLLISION_POINT_COUNT 8U
#define CORKSCREW_COLLISION_POINT_COUNT 2U
#define SLALOM_COLLISION_POINT_COUNT 4U
#define SCENERY_COLLISION_POINT_COUNT 1U
#define MULTI_TILE_ROW_FLAG 1U
#define MULTI_TILE_COLUMN_FLAG 2U
#define SUSPENSION_TARGET_DECAY 4
#define SUSPENSION_RETURN_STEP 128
#define CONTACT_DELTA_LIMIT 192
#define SUSPENSION_TRAVEL_LIMIT 384
#define SUSPENSION_SOFT_CONTACT_THRESHOLD (-288)
#define HARD_CONTACT_SCALE_NUMERATOR 3
#define COLLISION_SPEED_FIXED_SHIFT 8U
#define COLLISION_SLOWDOWN_SCALE 768
#define COLLISION_MIN_RELATIVE_SPEED 10
#define COLLISION_ACTIVE_SPEED_THRESHOLD 30
#define COLLISION_CORNER_COUNT 4

enum COLLISION_WORLD_COORDINATE_INDEX {
	COLLISION_POSITION_INDEX = 0,
	COLLISION_ROTATION_INDEX = 1
};

enum COLLISION_BOUNDING_POINT_INDEX {
	COLLISION_EXTENT_POINT_INDEX = 0,
	COLLISION_RADIUS_POINT_INDEX = 1
};

#define COLLISION_LEFT_FIRST_CORNER 0
#define COLLISION_LEFT_LAST_CORNER 3
#define COLLISION_NEGATIVE_Z_FIRST_CORNER 2

legacy_s16 scale_position_delta(legacy_s32 current, legacy_s32 previous, legacy_s16 factor,
								legacy_s16 divisor)
{
	legacy_s32 delta = LEGACY_S32_WRAP_SUB(current, previous);
	legacy_s32 product = LEGACY_S32_WRAP_MUL(delta, (legacy_s32)factor);
	legacy_s32 quotient = LEGACY_S32_DIV_OR_ZERO(product, (legacy_s32)divisor);
	return LEGACY_S16_FROM_BITS((legacy_u16)quotient);
}

legacy_s16 scale_speed_to_travel(legacy_u16 speed, legacy_u16 divisor)
{
	legacy_u32 product = LEGACY_U32_WRAP_MUL((legacy_u32)speed, SPEED_TO_TRAVEL_NUMERATOR);
	legacy_u32 quotient = LEGACY_U32_DIV_OR_ZERO(product, divisor);
	return LEGACY_S16_FROM_BITS((legacy_u16)quotient);
}

legacy_s16 physics_difference_word(legacy_s32 left, legacy_s32 right)
{
	return LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_WRAP_SUB(left, right));
}

legacy_s16 wheel_pair_delta(legacy_s16 first, legacy_s16 second, legacy_s16 third,
							legacy_s16 fourth)
{
	return LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(first, second), third),
							   fourth);
}

/* Most physical models bring a fixed set of collision points with them. */
struct COLLISION_MODEL {
	legacy_s8 physical_model;
	const struct VECTOR *points;
	legacy_u16 count;
};

static const struct COLLISION_MODEL collision_models[COLLISION_MODEL_COUNT] = {
	{PHYSICAL_MODEL_ELEVATED_ROAD, elevated_road_collision_points,
	 ELEVATED_ROAD_COLLISION_POINT_COUNT},
	{PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A, corkscrew_up_collision_points,
	 CORKSCREW_COLLISION_POINT_COUNT},
	{PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B, corkscrew_down_collision_points,
	 CORKSCREW_COLLISION_POINT_COUNT},
	{PHYSICAL_MODEL_SLALOM, slalom_collision_points, SLALOM_COLLISION_POINT_COUNT},
	{PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT, corkscrew_lr_collision_points,
	 CORKSCREW_COLLISION_POINT_COUNT}};

/* A multi-tile element anchors its collision box on the shared tile edge
 * rather than on the centre of the tile the car happens to be standing on. */
static void collision_tile_center(legacy_u8 tile_element, legacy_u16 row_index,
								  legacy_u16 column_index, legacy_u16 *center_z,
								  legacy_u16 *center_x)
{
	legacy_u8 multi_tile_flags = trkObjectList[tile_element].ss_multiTileFlag;
	if ((multi_tile_flags & MULTI_TILE_ROW_FLAG) != 0) {
		*center_z = (legacy_u16)track_row_position(row_index);
	}
	if ((multi_tile_flags & MULTI_TILE_COLUMN_FLAG) != 0) {
		*center_x = (legacy_u16)track_column_position(column_index);
	}
}

static legacy_u16 collision_model_points(legacy_u8 tile_element,
										 const struct VECTOR **dependency_points)
{
	*dependency_points = 0;
	legacy_s8 physical_model = (legacy_s8)trkObjectList[tile_element].ss_physicalModel;
	legacy_u16 count = 0;
	if (physical_model == PHYSICAL_MODEL_HIGHWAY ||
		(physical_model >= PHYSICAL_MODEL_SCENERY_FIRST &&
		 physical_model <= PHYSICAL_MODEL_SCENERY_LAST)) {
		*dependency_points = scenery_collision_points;
		count = SCENERY_COLLISION_POINT_COUNT;
	} else {
		for (legacy_u16 index = 0U; index < COLLISION_MODEL_COUNT; index++) {
			if (collision_models[index].physical_model == physical_model) {
				*dependency_points = collision_models[index].points;
				count = collision_models[index].count;
				break;
			}
		}
	}
	return count;
}

static void transform_collision_points(const struct VECTOR *dependency_points,
									   struct VECTOR *output, legacy_u16 count,
									   legacy_u16 orientation, legacy_u16 center_x,
									   legacy_u16 center_z, legacy_u16 terrain_height)
{
	for (legacy_u16 index = 0; index < count; index++) {
		legacy_u16 source_x = (legacy_u16)dependency_points[index].x;
		legacy_u16 source_y = (legacy_u16)dependency_points[index].y;
		legacy_u16 source_z = (legacy_u16)dependency_points[index].z;
		legacy_u16 rotated_z;
		legacy_u16 rotated_x;
		if (orientation == 0) {
			rotated_x = source_x;
			rotated_z = source_z;
		} else if (orientation == ANGLE_QUARTER_TURN) {
			rotated_x = source_z;
			rotated_z = LEGACY_U16_WRAP_SUB(0, source_x);
		} else if (orientation == ANGLE_HALF_TURN) {
			rotated_x = LEGACY_U16_WRAP_SUB(0, source_x);
			rotated_z = LEGACY_U16_WRAP_SUB(0, source_z);
		} else if (orientation == ANGLE_THREE_QUARTER_TURN) {
			rotated_x = LEGACY_U16_WRAP_SUB(0, source_z);
			rotated_z = source_x;
		} else {
			continue;
		}
		output[index].x = LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(rotated_x, center_x));
		output[index].y = LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(source_y, terrain_height));
		output[index].z = LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(rotated_z, center_z));
	}
}

legacy_s16 get_track_collision_points(legacy_s16 column_arg, legacy_s16 row_arg,
									  struct VECTOR *output)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		const struct CSTRK_OBJECT *object = extended_track_object_at(column_arg, row_arg);
		if (!object) {
			return 0;
		}
		unsigned id = extended_track_object_id(object);
		if (id >= 215) {
			return 0;
		}
		const struct VECTOR *points;
		legacy_u16 count = collision_model_points((legacy_u8)id, &points);
		struct VECTOR origin;
		extended_geometry_object_origin(extended_track_model(), object, &origin);
		transform_collision_points(points, output, count, trkObjectList[id].ss_rotY, 0, 0, 0);
		for (unsigned i = 0; i < count; i++) {
			if (extended_geometry_uses_grade(object)) {
				output[i].y = LEGACY_S16_WRAP_ADD(
					output[i].y, extended_geometry_object_grade(object, output[i].x, output[i].z));
			}
			output[i].x = LEGACY_S16_WRAP_ADD(output[i].x, origin.x);
			output[i].y = LEGACY_S16_WRAP_ADD(output[i].y, origin.y);
			output[i].z = LEGACY_S16_WRAP_ADD(output[i].z, origin.z);
		}
		return count;
	}
#endif
	legacy_u16 column = (legacy_u16)column_arg;
	legacy_u16 row = (legacy_u16)row_arg;
	legacy_u8 tile_element = track_element_map[trackrows[row] + column];
	if (tile_element == 0) {
		return 0;
	}

	legacy_u16 center_x = (legacy_u16)track_column_centers[column];
	legacy_u16 center_z = (legacy_u16)track_row_centers[row];
	legacy_u16 previous_row_base =
		row == 0 ? (legacy_u16)replay_overflow_acknowledged_word : (legacy_u16)trackrows[row - 1U];
	if (tile_element == TRACK_TILE_CONTINUATION_SOUTHEAST) {
		tile_element = track_element_map[LEGACY_U16_WRAP_SUB(previous_row_base + column, 1U)];
		collision_tile_center(tile_element, row + 1U, column, &center_z, &center_x);
	} else if (tile_element == TRACK_TILE_CONTINUATION_SOUTH) {
		tile_element = track_element_map[previous_row_base + column];
		collision_tile_center(tile_element, row + 1U, column + 1U, &center_z, &center_x);
	} else if (tile_element == TRACK_TILE_CONTINUATION_EAST) {
		tile_element = track_element_map[LEGACY_U16_WRAP_SUB(trackrows[row] + column, 1U)];
		collision_tile_center(tile_element, row, column, &center_z, &center_x);
	} else {
		collision_tile_center(tile_element, row, column + 1U, &center_z, &center_x);
	}

	const struct VECTOR *dependency_points;
	legacy_u16 count = collision_model_points(tile_element, &dependency_points);
	if (count == 0) {
		return 0;
	}

	legacy_u16 terrain_height = track_terrain_map[terrainrows[row] + column] == TERRAIN_RAISED_TILE
									? (legacy_u16)hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX]
									: 0;
	legacy_u16 orientation = (legacy_u16)trkObjectList[tile_element].ss_rotY;
	transform_collision_points(dependency_points, output, count, orientation, center_x, center_z,
							   terrain_height);
	return count;
}

struct LEGACY_EXECUTION_RESIDUE legacy_execution_residue;
legacy_s16 legacy_render_player_headings_active;

static legacy_s16 decay_suspension_target(struct CARSTATE *carstate, legacy_s16 wheel_index,
										  legacy_s16 decay)
{
	/* Decay the per-wheel target toward zero at the requested tick rate. */
	legacy_s16 target = (legacy_s16)carstate->car_suspension_target[wheel_index];
	if (target < 0) {
		target = LEGACY_S16_WRAP_ADD(target, decay);
		if (target > 0) {
			target = 0;
		}
	} else if (target > 0) {
		target = LEGACY_S16_WRAP_SUB(target, decay);
		if (target < 0) {
			target = 0;
		}
	}
	carstate->car_suspension_target[wheel_index] = target;
	return target;
}

static legacy_s16 return_wheel_suspension(struct CARSTATE *carstate, legacy_s16 wheel_index,
										  legacy_s16 target, legacy_s16 previous_deflection,
										  legacy_s16 return_step)
{
	legacy_s16 adjustment = 0;

	if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] > target) {
		carstate->car_suspension_deflection[wheel_index] =
			LEGACY_S16_WRAP_SUB(carstate->car_suspension_deflection[wheel_index], return_step);
		if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] < target) {
			carstate->car_suspension_deflection[wheel_index] = target;
		}
		adjustment = LEGACY_S16_WRAP_SUB(previous_deflection,
										 carstate->car_suspension_deflection[wheel_index]);
	} else if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] < target) {
		carstate->car_suspension_deflection[wheel_index] =
			LEGACY_S16_WRAP_ADD(carstate->car_suspension_deflection[wheel_index], return_step);
		if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] > target) {
			carstate->car_suspension_deflection[wheel_index] = target;
		}
	}
	return adjustment;
}

static legacy_s16 update_wheel_suspension_step(struct CARSTATE *carstate,
											   legacy_s16 contact_delta_arg, legacy_s16 wheel_index,
											   legacy_s16 target_decay, legacy_s16 return_step)
{
	legacy_s16 previous_deflection = (legacy_s16)carstate->car_suspension_deflection[wheel_index];
	legacy_s16 contact_delta = (legacy_s16)contact_delta_arg;

	legacy_s16 target = decay_suspension_target(carstate, wheel_index, target_decay);

	if (contact_delta < 0 && (legacy_s16)carstate->car_suspension_deflection[wheel_index] >
								 LEGACY_S16_WRAP_NEGATE(contact_delta)) {
		contact_delta = 0;
	}

	legacy_s16 adjustment = 0;
	if (contact_delta == 0) {
		adjustment = return_wheel_suspension(carstate, wheel_index, target, previous_deflection,
											 return_step);
	} else if (contact_delta > 0) {
		if (contact_delta > CONTACT_DELTA_LIMIT) {
			contact_delta = CONTACT_DELTA_LIMIT;
		}
		carstate->car_suspension_deflection[wheel_index] =
			LEGACY_S16_WRAP_ADD(carstate->car_suspension_deflection[wheel_index], contact_delta);
		if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] >
			SUSPENSION_TRAVEL_LIMIT) {
			carstate->car_suspension_deflection[wheel_index] = SUSPENSION_TRAVEL_LIMIT;
		}
		carstate->car_reserved_contact_state[wheel_index] = 0;
	} else {
		if (LEGACY_S16_WRAP_ADD(contact_delta, carstate->car_suspension_deflection[wheel_index]) >
			SUSPENSION_SOFT_CONTACT_THRESHOLD) {
			carstate->car_suspension_deflection[wheel_index] = LEGACY_S16_WRAP_ADD(
				carstate->car_suspension_deflection[wheel_index], contact_delta);
		} else {
			legacy_s16 scaled_delta =
				LEGACY_S16_SAR2(LEGACY_S16_WRAP_MUL(contact_delta, HARD_CONTACT_SCALE_NUMERATOR));
			carstate->car_suspension_deflection[wheel_index] =
				LEGACY_S16_WRAP_ADD(carstate->car_suspension_deflection[wheel_index], scaled_delta);
			if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] <
				-SUSPENSION_TRAVEL_LIMIT) {
				carstate->car_suspension_deflection[wheel_index] = -SUSPENSION_TRAVEL_LIMIT;
			}
		}
		adjustment = LEGACY_S16_WRAP_ADD(
			LEGACY_S16_WRAP_SUB(previous_deflection,
								carstate->car_suspension_deflection[wheel_index]),
			contact_delta);
	}

	return LEGACY_S16_WRAP_ADD(previous_deflection, adjustment);
}

legacy_s16 update_wheel_suspension(struct CARSTATE *carstate, legacy_s16 contact_delta,
								   legacy_s16 wheel_index)
{
	return update_wheel_suspension_step(carstate, contact_delta, wheel_index,
										SUSPENSION_TARGET_DECAY, SUSPENSION_RETURN_STEP);
}

legacy_s16 update_wheel_suspension_fraction(struct CARSTATE *carstate, legacy_s16 contact_delta,
											legacy_s16 wheel_index, legacy_u32 fraction20)
{
	if (fraction20 == 0) {
		return carstate->car_suspension_deflection[wheel_index];
	}
	if (fraction20 > PHYSICS_FRACTION_ONE) {
		fraction20 = PHYSICS_FRACTION_ONE;
	}
	/* Contact distances are geometric constraints; only spring recovery and
	 * target decay are rates. Scaling penetration would leave wheels in ground. */
	legacy_s16 target_decay =
		(legacy_s16)((SUSPENSION_TARGET_DECAY * fraction20 + PHYSICS_FRACTION_ONE / 2) /
					 PHYSICS_FRACTION_ONE);
	legacy_s16 return_step =
		(legacy_s16)((SUSPENSION_RETURN_STEP * fraction20 + PHYSICS_FRACTION_ONE / 2) /
					 PHYSICS_FRACTION_ONE);
	return update_wheel_suspension_step(carstate, contact_delta, wheel_index, target_decay,
										return_step);
}

legacy_s16 resolve_car_collision_speeds(struct CARSTATE *first_state, struct CARSTATE *second_state)
{
	first_state->car_collision_latch = CAR_COLLISION_LATCH_SET;
	second_state->car_collision_latch = CAR_COLLISION_LATCH_SET;
	legacy_s16 first_angle = (legacy_s16)first_state->car_rotate.x;
	legacy_s16 second_angle = (legacy_s16)second_state->car_rotate.x;

	legacy_s16 first_sin_speed = multiply_and_scale(
		(legacy_s16)(first_state->car_actual_speed >> COLLISION_SPEED_FIXED_SHIFT),
		sin_fast((legacy_u16)first_angle));
	legacy_s16 second_sin_speed = multiply_and_scale(
		(legacy_s16)(second_state->car_actual_speed >> COLLISION_SPEED_FIXED_SHIFT),
		sin_fast((legacy_u16)second_angle));
	legacy_s16 first_cos_speed = multiply_and_scale(
		(legacy_s16)(first_state->car_actual_speed >> COLLISION_SPEED_FIXED_SHIFT),
		cos_fast((legacy_u16)first_angle));
	legacy_s16 second_cos_speed = multiply_and_scale(
		(legacy_s16)(second_state->car_actual_speed >> COLLISION_SPEED_FIXED_SHIFT),
		cos_fast((legacy_u16)second_angle));

	legacy_s16 relative_speed =
		(legacy_s16)polarRadius2D(LEGACY_S16_WRAP_SUB(second_sin_speed, first_sin_speed),
								  LEGACY_S16_WRAP_SUB(second_cos_speed, first_cos_speed));
	if (relative_speed < COLLISION_MIN_RELATIVE_SPEED) {
		relative_speed = COLLISION_MIN_RELATIVE_SPEED;
	}

	/* The original keeps only the low product word before shifting it. */
	legacy_s16 slowdown =
		LEGACY_S16_SAR2(LEGACY_S16_WRAP_MUL(COLLISION_SLOWDOWN_SCALE, relative_speed));
	if ((legacy_u16)first_state->car_actual_speed < (legacy_u16)slowdown) {
		first_state->car_actual_speed = CAR_SPEED_STOPPED;
	} else {
		first_state->car_actual_speed =
			LEGACY_U16_WRAP_SUB(first_state->car_actual_speed, slowdown);
	}

	legacy_s16 angle_delta = LEGACY_S16_WRAP_SUB(second_angle, first_angle);
	if (angle_delta >= ANGLE_HALF_TURN) {
		angle_delta = LEGACY_S16_WRAP_SUB(angle_delta, ANGLE_FULL_TURN);
	}
	if (angle_delta <= -ANGLE_HALF_TURN) {
		angle_delta = LEGACY_S16_WRAP_ADD(angle_delta, ANGLE_FULL_TURN);
	}
	first_state->car_velocity_heading_offset = angle_delta;

	angle_delta = LEGACY_S16_WRAP_SUB(first_angle, second_angle);
	if (angle_delta >= ANGLE_HALF_TURN) {
		angle_delta = LEGACY_S16_WRAP_SUB(angle_delta, ANGLE_FULL_TURN);
	}
	if (angle_delta <= -ANGLE_HALF_TURN) {
		angle_delta = LEGACY_S16_WRAP_ADD(angle_delta, ANGLE_FULL_TURN);
	}
	second_state->car_velocity_heading_offset = angle_delta;

	first_state->car_rev_speed = first_state->car_actual_speed;
	second_state->car_rev_speed = second_state->car_actual_speed;
	return relative_speed > COLLISION_ACTIVE_SPEED_THRESHOLD;
}

static legacy_s16 collision_axis_distance(legacy_s16 first, legacy_s16 second)
{
	if (first < second) {
		return LEGACY_S16_WRAP_SUB(second, first);
	}
	return LEGACY_S16_WRAP_SUB(first, second);
}

static void build_collision_corners(struct POINT2D *collision_points,
									struct VECTOR *world_coordinates,
									struct VECTOR corners[COLLISION_CORNER_COUNT])
{
	struct MATRIX *rotation =
		mat_rot_zxy(LEGACY_S16_WRAP_NEGATE(world_coordinates[COLLISION_ROTATION_INDEX].x),
					LEGACY_S16_WRAP_NEGATE(world_coordinates[COLLISION_ROTATION_INDEX].y),
					LEGACY_S16_WRAP_NEGATE(world_coordinates[COLLISION_ROTATION_INDEX].z),
					MATRIX_ROTATION_ORDER_ZXY);
	struct VECTOR local_corner;
	for (legacy_s16 corner = 0; corner < COLLISION_CORNER_COUNT; corner++) {
		if (corner == COLLISION_LEFT_FIRST_CORNER || corner == COLLISION_LEFT_LAST_CORNER) {
			local_corner.x =
				LEGACY_S16_WRAP_NEGATE(collision_points[COLLISION_EXTENT_POINT_INDEX].px);
		} else {
			local_corner.x = (legacy_s16)collision_points[COLLISION_EXTENT_POINT_INDEX].px;
		}
		local_corner.y = 0;
		if (corner >= COLLISION_NEGATIVE_Z_FIRST_CORNER) {
			local_corner.z =
				LEGACY_S16_WRAP_NEGATE(collision_points[COLLISION_RADIUS_POINT_INDEX].px);
		} else {
			local_corner.z = (legacy_s16)collision_points[COLLISION_RADIUS_POINT_INDEX].px;
		}

		mat_mul_vector(&local_corner, rotation, &corners[corner]);
		corners[corner].x =
			LEGACY_S16_WRAP_ADD(corners[corner].x, world_coordinates[COLLISION_POSITION_INDEX].x);
		corners[corner].y =
			LEGACY_S16_WRAP_ADD(corners[corner].y, world_coordinates[COLLISION_POSITION_INDEX].y);
		corners[corner].z =
			LEGACY_S16_WRAP_ADD(corners[corner].z, world_coordinates[COLLISION_POSITION_INDEX].z);
	}
}

static legacy_s16 collision_corners_inside(struct VECTOR corners[COLLISION_CORNER_COUNT],
										   struct POINT2D *collision_points,
										   struct VECTOR *world_coordinates)
{
	struct MATRIX *rotation =
		mat_rot_zxy(world_coordinates[COLLISION_ROTATION_INDEX].x,
					world_coordinates[COLLISION_ROTATION_INDEX].y,
					world_coordinates[COLLISION_ROTATION_INDEX].z, MATRIX_ROTATION_ORDER_YXZ);
	struct VECTOR local_corner;
	struct VECTOR relative_corner;
	for (legacy_s16 corner = 0; corner < COLLISION_CORNER_COUNT; corner++) {
		relative_corner.x =
			LEGACY_S16_WRAP_SUB(world_coordinates[COLLISION_POSITION_INDEX].x, corners[corner].x);
		relative_corner.y =
			LEGACY_S16_WRAP_SUB(world_coordinates[COLLISION_POSITION_INDEX].y, corners[corner].y);
		relative_corner.z =
			LEGACY_S16_WRAP_SUB(world_coordinates[COLLISION_POSITION_INDEX].z, corners[corner].z);
		mat_mul_vector(&relative_corner, rotation, &local_corner);

		if (local_corner.y < 0 ||
			local_corner.y > (legacy_s16)collision_points[COLLISION_EXTENT_POINT_INDEX].py) {
			continue;
		}
		legacy_s16 negative_extent =
			LEGACY_S16_WRAP_NEGATE(collision_points[COLLISION_EXTENT_POINT_INDEX].px);
		if (local_corner.x < negative_extent ||
			local_corner.x > (legacy_s16)collision_points[COLLISION_EXTENT_POINT_INDEX].px) {
			continue;
		}
		negative_extent = LEGACY_S16_WRAP_NEGATE(collision_points[COLLISION_RADIUS_POINT_INDEX].px);
		if (local_corner.z < negative_extent ||
			local_corner.z > (legacy_s16)collision_points[COLLISION_RADIUS_POINT_INDEX].px) {
			continue;
		}
		return 1;
	}

	return 0;
}

legacy_s16 car_collision_boxes_overlap(struct POINT2D *first_collision_points,
									   struct VECTOR *first_world_coordinates,
									   struct POINT2D *second_collision_points,
									   struct VECTOR *second_world_coordinates)
{
	legacy_s16 combined_radius =
		LEGACY_S16_WRAP_ADD(first_collision_points[COLLISION_RADIUS_POINT_INDEX].py,
							second_collision_points[COLLISION_RADIUS_POINT_INDEX].py);
	if (collision_axis_distance(first_world_coordinates[COLLISION_POSITION_INDEX].x,
								second_world_coordinates[COLLISION_POSITION_INDEX].x) >
		combined_radius) {
		return 0;
	}
	if (collision_axis_distance(first_world_coordinates[COLLISION_POSITION_INDEX].z,
								second_world_coordinates[COLLISION_POSITION_INDEX].z) >
		combined_radius) {
		return 0;
	}
	if (collision_axis_distance(first_world_coordinates[COLLISION_POSITION_INDEX].y,
								second_world_coordinates[COLLISION_POSITION_INDEX].y) >
		combined_radius) {
		return 0;
	}

	struct VECTOR position_delta;
	position_delta.x = LEGACY_S16_WRAP_SUB(first_world_coordinates[COLLISION_POSITION_INDEX].x,
										   second_world_coordinates[COLLISION_POSITION_INDEX].x);
	position_delta.y = LEGACY_S16_WRAP_SUB(first_world_coordinates[COLLISION_POSITION_INDEX].y,
										   second_world_coordinates[COLLISION_POSITION_INDEX].y);
	position_delta.z = LEGACY_S16_WRAP_SUB(first_world_coordinates[COLLISION_POSITION_INDEX].z,
										   second_world_coordinates[COLLISION_POSITION_INDEX].z);
	if ((legacy_u16)polarRadius3D(&position_delta) > (legacy_u16)combined_radius) {
		return 0;
	}

	struct VECTOR corners[COLLISION_CORNER_COUNT];
	build_collision_corners(first_collision_points, first_world_coordinates, corners);
	if (collision_corners_inside(corners, second_collision_points, second_world_coordinates)) {
		return 1;
	}

	build_collision_corners(second_collision_points, second_world_coordinates, corners);
	return collision_corners_inside(corners, first_collision_points, first_world_coordinates);
}
