#include "../restunts.h"
#include "../legacy.h"
#include "../math/math.h"
#include "../track/trackdata_layout.h"
#include "../track/track_types.h"
#include "../track/track_objects.h"
#include "../track/track_collision.h"
#include "../track/trackobj_internal.h"
#include "../physics/physics_internal.h"
#include "../owoot/owoot_road.h"
#include "../externs.h"
#include "extended_trackobj.h"
#ifdef RESTUNTS_SDL3
#include "extended_track.h"
#include "extended_geometry.h"
#include "roll_ramp.h"
#endif

#ifdef RESTUNTS_SDL3
static struct PLANE extended_selected_plane;
static struct VECTORLONG extended_selected_exact_normal;
static legacy_u8 extended_selected_exact_normal_valid;

#define TRACK_CONTACT_POSITION_SCALE 64L
#define TRACK_CONTACT_NORMAL_SCALE (TRIG_FIXED_ONE / 2)

legacy_s16 track_selected_plane_contact_fixed(const struct VECTORLONG *position, legacy_s32 *height,
											  legacy_s16 *distance)
{
	if (!extended_track_active() || !extended_selected_exact_normal_valid ||
		extended_selected_exact_normal.ly <= 0) {
		return 0;
	}
	const struct VECTORLONG *normal = &extended_selected_exact_normal;
	const struct VECTOR *origin = &current_planptr->plane_origin;
	legacy_s32 center_x = extended_geometry_unwrap(
		elem_xCenter, (legacy_s32)(position->lx / TRACK_CONTACT_POSITION_SCALE));
	legacy_s32 center_z = extended_geometry_unwrap(
		elem_zCenter, (legacy_s32)(position->lz / TRACK_CONTACT_POSITION_SCALE));
	legacy_s64 x = (legacy_s64)position->lx -
				   ((legacy_s64)center_x + origin->x) * TRACK_CONTACT_POSITION_SCALE;
	legacy_s64 z = (legacy_s64)position->lz -
				   ((legacy_s64)center_z + origin->z) * TRACK_CONTACT_POSITION_SCALE;
	legacy_s32 origin_y = ((legacy_s32)terrainHeight + origin->y) * TRACK_CONTACT_POSITION_SCALE;
	legacy_s64 horizontal = x * normal->lx + z * normal->lz;
	legacy_s64 rise = -horizontal;
	legacy_s64 elevation = rise / normal->ly;
	if (rise > 0 && rise % normal->ly) {
		elevation++;
	}
	*height = origin_y + (legacy_s32)elevation;
	legacy_s64 signed_distance = (horizontal + ((legacy_s64)position->ly - origin_y) * normal->ly) *
								 current_planptr->plane_normal.y;
	legacy_s64 divisor =
		(legacy_s64)normal->ly * TRACK_CONTACT_POSITION_SCALE * TRACK_CONTACT_NORMAL_SCALE;
	legacy_s64 whole_distance = signed_distance / divisor;
	/* Sub-unit penetration still needs the native landing response. */
	if (signed_distance < 0 && signed_distance % divisor) {
		whole_distance--;
	}
	*distance = (legacy_s16)whole_distance;
	return 1;
}
static void extended_select_terrain(const struct EXTENDED_TERRAIN_SAMPLE *terrain,
									const struct VECTOR *position)
{
	elem_xCenter = terrain->origin.x;
	elem_zCenter = terrain->origin.z;
	terrainHeight = terrain->surface == CAR_SURFACE_GRASS
						? (legacy_s16)(((legacy_u16)(position->x ^ position->z) >> 8U) & 1U)
						: 2;
	current_surf_type = (legacy_s8)terrain->surface;
	extended_selected_exact_normal_valid = 0;
	extended_selected_plane = terrain->plane;
	current_planptr = &extended_selected_plane;
	planindex = EXTENDED_GEOMETRY_PLANE_INDEX;
}

void build_extended_track_terrain(struct VECTOR *position)
{
	legacy_s32 world_x, world_z;
	struct EXTENDED_TERRAIN_SAMPLE sample;
	extended_geometry_world_position(position, &world_x, &world_z);
	if (extended_geometry_terrain(extended_track_model(), world_x, world_z, &sample)) {
		extended_select_terrain(&sample, position);
		wallindex = TRACK_WALL_NONE;
		track_wall_collision_enabled = 1;
	}
}

/* Sparse occupancy frees editor cells, while the authored road and guardrails
 * can still cross a small part of their corners. Resolve that geometry before
 * falling back to the independently placed piece that owns the cell. */
static legacy_s16 extended_object_road_at(const struct CSTRK_TRACK *track,
										  const struct CSTRK_OBJECT *object,
										  const struct VECTOR *position)
{
	legacy_u32 id = extended_track_object_id(object);
	const struct TRACKOBJECT *definition = &trkObjectList[id];
	struct VECTOR origin, local;
	extended_geometry_object_origin(track, object, &origin);
	local.x = LEGACY_S16_WRAP_SUB(position->x, origin.x);
	local.y = LEGACY_S16_WRAP_SUB(position->y, origin.y);
	local.z = LEGACY_S16_WRAP_SUB(position->z, origin.z);
	if (extended_geometry_uses_grade(object)) {
		local.y =
			LEGACY_S16_WRAP_SUB(local.y, extended_geometry_object_grade(object, local.x, local.z));
	}
	track_rotate_local(&local, definition->ss_rotY);
	legacy_s16 model = definition->ss_physicalModel;
	if (model == PHYSICAL_MODEL_LARGE_CORNER) {
		return track_radius_in_band(LEGACY_S16_WRAP_ADD(local.x, TRACK_ARC_CENTER_OFFSET),
									LEGACY_S16_WRAP_ADD(local.z, TRACK_ARC_CENTER_OFFSET),
									LARGE_CORNER_INNER_RADIUS, LARGE_CORNER_OUTER_RADIUS);
	}
	if (model == PHYSICAL_MODEL_BANKED_CORNER) {
		legacy_s16 radius = track_arc_radius(&local);
		return radius > -BANKED_CORNER_INNER_OFFSET && radius < BANKED_CORNER_OUTER_OFFSET;
	}
	if (model == PHYSICAL_MODEL_ELEVATED_CORNER) {
		legacy_s16 radius = track_arc_radius(&local);
		return local.y > ELEVATED_DECK_CLEARANCE && radius > -ELEVATED_CORNER_OUTER_OFFSET &&
			   radius < ELEVATED_CORNER_OUTER_OFFSET;
	}
	return track_road_contains_local_point(model, &local);
}

static const struct CSTRK_OBJECT *
extended_collision_object(const struct CSTRK_TRACK *track,
						  const struct EXTENDED_TERRAIN_SAMPLE *terrain,
						  const struct VECTOR *position)
{
	const struct CSTRK_OBJECT *owner =
		extended_track_object_at(terrain->column, terrain->north_row);
	const struct CSTRK_OBJECT *candidates[EXTENDED_TRACK_CELL_OBJECT_MAX];
	legacy_u16 count = extended_track_objects_in_cell(terrain->column, terrain->north_row,
													  candidates, EXTENDED_TRACK_CELL_OBJECT_MAX);
	if (owner && (count == 1 || extended_object_road_at(track, owner, position))) {
		return owner;
	}
	for (legacy_u16 index = 0; index < count; index++) {
		const struct CSTRK_OBJECT *candidate = candidates[index];
		legacy_u16 full_mask = (1U << (candidate->width * candidate->height)) - 1U;
		if (candidate != owner && candidate->occupied_mask != full_mask &&
			extended_object_road_at(track, candidate, position)) {
			return candidate;
		}
	}
	return owner;
}

#define ROLL_RAMP_COLLISION_NORMAL_SCALE (TRIG_FIXED_ONE / 2)
#define ROLL_RAMP_GRILLE_CONTACT_DEPTH 16

static legacy_s64 roll_ramp_triangle_side(const struct VECTOR *a, const struct VECTOR *b,
										  const struct VECTOR *point)
{
	return (legacy_s64)(b->x - a->x) * (point->z - a->z) -
		   (legacy_s64)(b->z - a->z) * (point->x - a->x);
}

static legacy_s16 roll_ramp_triangle_contains(const struct VECTOR triangle[3],
											  const struct VECTOR *point)
{
	legacy_u8 negative = 0, positive = 0;
	for (legacy_u8 edge = 0; edge < 3U; edge++) {
		legacy_s64 side =
			roll_ramp_triangle_side(&triangle[edge], &triangle[(edge + 1U) % 3U], point);
		negative |= side < 0;
		positive |= side > 0;
	}
	return !(negative && positive);
}

static void roll_ramp_triangle_plane(const struct VECTOR triangle[3], struct PLANE *plane,
									 struct VECTORLONG *exact_normal)
{
	const struct VECTOR *a = &triangle[0], *b = &triangle[1], *c = &triangle[2];
	legacy_s32 normal[3] = {
		(legacy_s32)(b->y - a->y) * (c->z - a->z) - (legacy_s32)(b->z - a->z) * (c->y - a->y),
		(legacy_s32)(b->z - a->z) * (c->x - a->x) - (legacy_s32)(b->x - a->x) * (c->z - a->z),
		(legacy_s32)(b->x - a->x) * (c->y - a->y) - (legacy_s32)(b->y - a->y) * (c->x - a->x)};
	if (normal[1] < 0) {
		for (legacy_u8 axis = 0; axis < 3U; axis++) {
			normal[axis] = -normal[axis];
		}
	}
	*exact_normal = (struct VECTORLONG){normal[0], normal[1], normal[2]};
	legacy_s32 maximum = 0;
	for (legacy_u8 axis = 0; axis < 3U; axis++) {
		legacy_s32 magnitude = normal[axis] < 0 ? -normal[axis] : normal[axis];
		if (magnitude > maximum) {
			maximum = magnitude;
		}
	}
	while (maximum > ROLL_RAMP_COLLISION_NORMAL_SCALE) {
		for (legacy_u8 axis = 0; axis < 3U; axis++) {
			normal[axis] /= 2;
		}
		maximum /= 2;
	}
	plane->plane_origin = *a;
	plane->plane_normal =
		(struct VECTOR){(legacy_s16)normal[0], (legacy_s16)normal[1], (legacy_s16)normal[2]};
	legacy_s16 length = polarRadius3D(&plane->plane_normal);
	if (length > 0) {
		plane->plane_normal.x = (legacy_s16)(normal[0] * ROLL_RAMP_COLLISION_NORMAL_SCALE / length);
		plane->plane_normal.y = (legacy_s16)(normal[1] * ROLL_RAMP_COLLISION_NORMAL_SCALE / length);
		plane->plane_normal.z = (legacy_s16)(normal[2] * ROLL_RAMP_COLLISION_NORMAL_SCALE / length);
	}
	legacy_s16 across = polarRadius2D(plane->plane_normal.y, plane->plane_normal.z);
	plane->plane_yz = -polarAngle(plane->plane_normal.x, across);
	plane->plane_xy = polarAngle(plane->plane_normal.z, plane->plane_normal.y);
	struct MATRIX roll, pitch;
	mat_rot_z(&roll, plane->plane_yz);
	mat_rot_x(&pitch, plane->plane_xy);
	mat_multiply(&roll, &pitch, &plane->plane_rotation);
}

static legacy_s16 extended_select_roll_ramp(const struct CSTRK_OBJECT *object,
											const struct VECTOR *origin,
											const struct VECTOR *position)
{
	struct VECTOR relative = {LEGACY_S16_WRAP_SUB(position->x, origin->x),
							  LEGACY_S16_WRAP_SUB(position->y, origin->y),
							  LEGACY_S16_WRAP_SUB(position->z, origin->z)};
	legacy_s16 orientation = roll_ramp_orientation(object);
	legacy_s16 inverse = LEGACY_S16_WRAP_NEGATE(orientation) & ANGLE_MASK;
	for (legacy_u16 index = 0; index < roll_ramp_road_triangle_count(); index++) {
		struct VECTOR triangle[3];
		roll_ramp_road_triangle(index, triangle);
		for (legacy_u8 vertex = 0; vertex < 3U; vertex++) {
			track_rotate_local(&triangle[vertex], inverse);
			triangle[vertex].y = LEGACY_S16_WRAP_ADD(
				triangle[vertex].y,
				extended_geometry_object_grade(object, triangle[vertex].x, triangle[vertex].z));
		}
		if (!roll_ramp_triangle_contains(triangle, &relative)) {
			continue;
		}
		struct PLANE plane;
		struct VECTORLONG exact_normal;
		roll_ramp_triangle_plane(triangle, &plane, &exact_normal);
		legacy_s64 distance =
			(legacy_s64)(relative.x - plane.plane_origin.x) * plane.plane_normal.x +
			(legacy_s64)(relative.y - plane.plane_origin.y) * plane.plane_normal.y +
			(legacy_s64)(relative.z - plane.plane_origin.z) * plane.plane_normal.z;
		/* The second tile is an elevated strip, not a solid wedge. A car
		 * underneath it retains contact with the independent terrain. */
		if (index >= roll_ramp_road_triangle_count() / 2U &&
			distance <
				-(legacy_s64)ROLL_RAMP_GRILLE_CONTACT_DEPTH * ROLL_RAMP_COLLISION_NORMAL_SCALE) {
			return 0;
		}
		extended_selected_plane = plane;
		extended_selected_exact_normal = exact_normal;
		extended_selected_exact_normal_valid = 1;
		current_planptr = &extended_selected_plane;
		planindex = EXTENDED_GEOMETRY_PLANE_INDEX + 1U + index;
		elem_xCenter = origin->x;
		elem_zCenter = origin->z;
		terrainHeight = origin->y;
		current_surf_type = (legacy_s8)(object->surface + CAR_SURFACE_PAVED);
		track_wall_collision_enabled = 0;
		return 1;
	}
	return 0;
}

static void build_extended_track_object(struct VECTOR *position, struct VECTOR *next)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	legacy_s32 world_x, world_z;
	extended_geometry_world_position(position, &world_x, &world_z);
	struct EXTENDED_TERRAIN_SAMPLE terrain;
	if (!extended_geometry_terrain(track, world_x, world_z, &terrain)) {
		current_planptr = &planptr[0];
		return;
	}
	const struct CSTRK_OBJECT *object = extended_collision_object(track, &terrain, position);
	/* Crop geometry is decorative: preserve the underlying terrain contact,
	 * surface grip and slope without introducing a driving floor or wall. */
	if (object == 0 || cstrk_is_crop_field(object->legacy_id)) {
		extended_select_terrain(&terrain, position);
		return;
	}
	legacy_u32 id = extended_track_object_id(object);
	if (id >= sizeof(trkObjectList) / sizeof(trkObjectList[0])) {
		extended_select_terrain(&terrain, position);
		return;
	}
	const struct TRACKOBJECT *definition = &trkObjectList[id];
	struct VECTOR origin, local, next_local;
	extended_geometry_object_origin(track, object, &origin);
	if (object->roll_ramp) {
		if (!extended_select_roll_ramp(object, &origin, position)) {
			extended_select_terrain(&terrain, position);
		}
		return;
	}
	local.x = LEGACY_S16_WRAP_SUB(position->x, origin.x);
	local.y = LEGACY_S16_WRAP_SUB(position->y, origin.y);
	local.z = LEGACY_S16_WRAP_SUB(position->z, origin.z);
	next_local.x = LEGACY_S16_WRAP_SUB(next->x, origin.x);
	next_local.y = LEGACY_S16_WRAP_SUB(next->y, origin.y);
	next_local.z = LEGACY_S16_WRAP_SUB(next->z, origin.z);
	legacy_s16 graded = extended_geometry_uses_grade(object);
	if (graded) {
		local.y =
			LEGACY_S16_WRAP_SUB(local.y, extended_geometry_object_grade(object, local.x, local.z));
		next_local.y = LEGACY_S16_WRAP_SUB(
			next_local.y, extended_geometry_object_grade(object, next_local.x, next_local.z));
	}
	legacy_s16 orientation = definition->ss_rotY;
	track_rotate_local(&local, orientation);
	track_rotate_local(&next_local, orientation);
	struct TRACK_OBJECT_SAMPLE sample;
	sample.position = local;
	sample.next_position = next_local;
	sample.height = local.y;
	sample.next_height = next_local.y;
	sample.physical_model = definition->ss_physicalModel;
	legacy_u8 surface = object->surface;
	if (sample.physical_model == PHYSICAL_MODEL_OVERPASS && local.y <= ELEVATED_DECK_CLEARANCE) {
		surface = object->lower_surface;
	}
	sample.surface_type = (legacy_s16)(surface + CAR_SURFACE_PAVED);
	sample.absolute_x = absolute_word(local.x);
	sample.absolute_z = absolute_word(local.z);
	sample.wall_orientation_modifier = 0;
	/* Outside the chicane road, select the background contact plane even when
	 * its material matches the road. A graded road can round differently. */
	if (!chicane_fast_grass_is_enabled() &&
		(sample.physical_model == PHYSICAL_MODEL_CHICANE_LEFT_RIGHT ||
		 sample.physical_model == PHYSICAL_MODEL_CHICANE_RIGHT_LEFT) &&
		!track_road_contains_local_point(sample.physical_model, &sample.position)) {
		extended_select_terrain(&terrain, position);
		return;
	}
	elem_xCenter = origin.x;
	elem_zCenter = origin.z;
	terrainHeight = origin.y;
	current_surf_type = (legacy_s8)terrain.surface;
	if (object->tunnel_slalom) {
		/* The shared slalom route keeps its finite barriers and steering line.
		 * Select the tunnel shell first so a pole cannot replace its roof or
		 * outside wall with a road-level collision plane. */
		track_object_tunnel(&sample);
		if (sample.height < TUNNEL_HEIGHT && sample.next_height < TUNNEL_HEIGHT &&
			wallindex == TRACK_WALL_NONE) {
			track_object_slalom(&sample);
		}
	} else {
		track_object_model(&sample);
	}
	/* Graded driving floors use the same authored flat plane as elevated
	 * decks and roofs. Applying one transform keeps their relative heights
	 * and normals exact; terrain outside the piece retains its native plane. */
	if (planindex == NO_PLANE_INDEX && wallindex == TRACK_WALL_NONE &&
		(!graded || current_surf_type != sample.surface_type)) {
		legacy_s8 surface = current_surf_type;
		extended_select_terrain(&terrain, position);
		current_surf_type = surface;
		terrainHeight = surface == CAR_SURFACE_GRASS
							? (legacy_s16)(((legacy_u16)(position->x ^ position->z) >>
											GRASS_HEIGHT_HASH_SHIFT) &
										   GRASS_HEIGHT_VARIATION_MASK)
							: NON_GRASS_HEIGHT_OFFSET;
		return;
	}
	track_object_world_collision(position, orientation, sample.wall_orientation_modifier);
	legacy_s16 source_index = planindex;
	extended_selected_plane = *current_planptr;
	if (graded) {
		legacy_s16 original_vertical_normal = extended_selected_plane.plane_normal.y;
		extended_geometry_grade_plane(object, &extended_selected_plane);
		if (wallindex != TRACK_WALL_NONE && original_vertical_normal != 0) {
			/* Wall bounds are signed distances to the selected driving plane.
			 * The inverse-transpose normal changes their distance scale while
			 * the sheared wall keeps its original vertical clearance. */
			wallHeight =
				(legacy_s16)((legacy_s32)wallHeight * extended_selected_plane.plane_normal.y /
							 original_vertical_normal);
			elRdWallRelated =
				(legacy_s16)((legacy_s32)elRdWallRelated * extended_selected_plane.plane_normal.y /
							 original_vertical_normal);
		}
	}
	current_planptr = &extended_selected_plane;
	planindex = EXTENDED_GEOMETRY_PLANE_INDEX + source_index;
}

void extended_trackobj_reset(void)
{
	extended_selected_exact_normal_valid = 0;
}

legacy_s16 extended_trackobj_build(struct VECTOR *position, struct VECTOR *next)
{
	if (!extended_track_active()) {
		return 0;
	}
	build_extended_track_object(position, next);
	return 1;
}

void extended_trackobj_capture(struct TRACK_COLLISION_SNAPSHOT *saved)
{
	saved->extended.plane_valid =
		extended_track_active() && planindex >= EXTENDED_GEOMETRY_PLANE_INDEX;
	if (saved->extended.plane_valid) {
		saved->extended.plane = *current_planptr;
	}
	saved->extended.exact_normal_valid =
		saved->extended.plane_valid && extended_selected_exact_normal_valid;
	saved->extended.exact_normal = saved->extended.exact_normal_valid
									   ? extended_selected_exact_normal
									   : (struct VECTORLONG){0, 0, 0};
}

void extended_trackobj_restore(const struct TRACK_COLLISION_SNAPSHOT *saved)
{
	if (saved->extended.plane_valid) {
		extended_selected_plane = saved->extended.plane;
		current_planptr = &extended_selected_plane;
	}
	extended_selected_exact_normal_valid = saved->extended.exact_normal_valid;
	extended_selected_exact_normal = saved->extended.exact_normal;
}

legacy_s32 extended_trackobj_coordinate_delta(legacy_s16 first, legacy_s16 second,
											  legacy_s32 difference)
{
	return extended_track_active() ? LEGACY_S16_WRAP_SUB(second, first) : difference;
}

#endif
