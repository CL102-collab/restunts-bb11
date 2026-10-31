#include "../restunts.h"
#include "../legacy.h"
#include "../math.h"
#include "../track/trackdata_layout.h"
#include "../track/track_types.h"
#include "../track/track_objects.h"
#include "../track/track_collision.h"
#include "../track/trackobj_internal.h"
#include "../physics_internal.h"
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
	if (object == 0) {
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
	saved->extended_plane_valid =
		extended_track_active() && planindex >= EXTENDED_GEOMETRY_PLANE_INDEX;
	if (saved->extended_plane_valid) {
		saved->extended_plane = *current_planptr;
	}
	saved->extended_exact_normal_valid =
		saved->extended_plane_valid && extended_selected_exact_normal_valid;
	saved->extended_exact_normal = saved->extended_exact_normal_valid
									   ? extended_selected_exact_normal
									   : (struct VECTORLONG){0, 0, 0};
}

void extended_trackobj_restore(const struct TRACK_COLLISION_SNAPSHOT *saved)
{
	if (saved->extended_plane_valid) {
		extended_selected_plane = saved->extended_plane;
		current_planptr = &extended_selected_plane;
	}
	extended_selected_exact_normal_valid = saved->extended_exact_normal_valid;
	extended_selected_exact_normal = saved->extended_exact_normal;
}

legacy_s32 extended_trackobj_coordinate_delta(legacy_s16 first, legacy_s16 second,
											  legacy_s32 difference)
{
	return extended_track_active() ? LEGACY_S16_WRAP_SUB(second, first) : difference;
}

#endif

#ifdef RESTUNTS_SDL3
static legacy_s16 roll_ramp_clip_solid(legacy_s64 first, legacy_s64 second, legacy_s16 *entry,
									   legacy_s16 *exit)
{
	if (first >= 0 && second >= 0) {
		return 0;
	}
	if (first <= 0 && second <= 0) {
		return 1;
	}
	legacy_s16 crossing = (legacy_s16)(first * TRIG_FIXED_ONE / (first - second));
	if (first >= 0) {
		if (crossing > *entry) {
			*entry = crossing;
		}
	} else if (crossing < *exit) {
		*exit = crossing;
	}
	return *entry < *exit;
}

static legacy_s16 roll_ramp_clip_solid_cap(legacy_s64 first, legacy_s64 second, legacy_u8 shared,
										   legacy_s16 *entry, legacy_s16 *exit)
{
	/* Adjacent concrete segments share a face. A sweep along that face still
	 * enters their union, while the outside faces retain strict clipping. */
	if (shared && first == 0 && second == 0) {
		return 1;
	}
	return roll_ramp_clip_solid(first, second, entry, exit);
}

static legacy_s16 roll_ramp_wedge_contact(const struct VECTOR *first, const struct VECTOR *second,
										  legacy_s16 *fraction)
{
	const legacy_s16 span = ROLL_RAMP_LENGTH / ROLL_RAMP_SEGMENT_COUNT;
	legacy_s16 hit = 0, earliest = TRIG_FIXED_ONE;
	/* Each concrete segment is the same linear top used by the road mesh.
	 * Clip the motion against its finite convex volume instead of extending
	 * that top plane below the open grille or beyond the launch edge. */
	for (legacy_u16 segment = 0; segment < ROLL_RAMP_SEGMENT_COUNT / 2U; segment++) {
		legacy_s16 start = segment * span, end = start + span;
		legacy_s16 height = roll_ramp_center_height(start);
		legacy_s16 rise = roll_ramp_center_height(end) - height;
		legacy_s16 entry = 0, exit = TRIG_FIXED_ONE;
		if (!roll_ramp_clip_solid(-ROLL_RAMP_HALF_WIDTH - (legacy_s32)first->x,
								  -ROLL_RAMP_HALF_WIDTH - (legacy_s32)second->x, &entry, &exit) ||
			!roll_ramp_clip_solid((legacy_s32)first->x - ROLL_RAMP_HALF_WIDTH,
								  (legacy_s32)second->x - ROLL_RAMP_HALF_WIDTH, &entry, &exit) ||
			!roll_ramp_clip_solid(-(legacy_s32)first->y, -(legacy_s32)second->y, &entry, &exit) ||
			!roll_ramp_clip_solid_cap(ROLL_RAMP_HALF_LENGTH - end - (legacy_s32)first->z,
									  ROLL_RAMP_HALF_LENGTH - end - (legacy_s32)second->z,
									  end < CSTRK_TILE_WORLD_SIZE, &entry, &exit) ||
			!roll_ramp_clip_solid_cap((legacy_s32)first->z - ROLL_RAMP_HALF_LENGTH + start,
									  (legacy_s32)second->z - ROLL_RAMP_HALF_LENGTH + start,
									  start > 0, &entry, &exit) ||
			!roll_ramp_clip_solid(((legacy_s64)first->y - height) * span -
									  (legacy_s64)rise * (ROLL_RAMP_HALF_LENGTH - first->z - start),
								  ((legacy_s64)second->y - height) * span -
									  (legacy_s64)rise *
										  (ROLL_RAMP_HALF_LENGTH - second->z - start),
								  &entry, &exit)) {
			continue;
		}
		if (!hit || entry < earliest) {
			earliest = entry;
		}
		hit = 1;
	}
	if (hit) {
		*fraction = earliest;
	}
	return hit;
}

static legacy_s16 roll_ramp_solid_contact(const struct VECTOR *first, const struct VECTOR *second,
										  legacy_s16 *fraction)
{
	legacy_s16 earliest = TRIG_FIXED_ONE;
	legacy_s16 hit = roll_ramp_wedge_contact(first, second, &earliest);
	for (legacy_u16 index = 0; index < roll_ramp_support_box_count(); index++) {
		struct ROLL_RAMP_BOX support;
		roll_ramp_support_box(index, &support);
		struct TRACK_SOLID_BOX box = {support.minimum, support.maximum};
		legacy_s16 contact;
		if (solid_box_contact(first, second, &box, &contact)) {
			if (!hit || contact < earliest) {
				earliest = contact;
			}
			hit = 1;
		}
	}
	if (hit) {
		*fraction = earliest;
	}
	return hit;
}

static legacy_s16 extended_solid_obstacle_contact(struct VECTOR *first, struct VECTOR *second,
												  legacy_s16 *fraction,
												  const struct TRACK_SOLID_BOX *boxes,
												  legacy_u16 box_count)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	legacy_s16 hit = 0, earliest = TRIG_FIXED_ONE;
	for (legacy_u32 i = 0; i < track->object_count; i++) {
		const struct CSTRK_OBJECT *object = &track->objects[i];
		legacy_u32 id = extended_track_object_id(object);
		if (id >= sizeof(trkObjectList) / sizeof(trkObjectList[0]) ||
			(!object->roll_ramp && trkObjectList[id].ss_physicalModel != PHYSICAL_MODEL_SLALOM)) {
			continue;
		}
		struct VECTOR origin, local_first, local_second;
		extended_geometry_object_origin(track, object, &origin);
		local_first = (struct VECTOR){LEGACY_S16_WRAP_SUB(first->x, origin.x),
									  LEGACY_S16_WRAP_SUB(first->y, origin.y),
									  LEGACY_S16_WRAP_SUB(first->z, origin.z)};
		local_second = (struct VECTOR){LEGACY_S16_WRAP_SUB(second->x, origin.x),
									   LEGACY_S16_WRAP_SUB(second->y, origin.y),
									   LEGACY_S16_WRAP_SUB(second->z, origin.z)};
		if (extended_geometry_uses_grade(object)) {
			local_first.y = LEGACY_S16_WRAP_SUB(
				local_first.y,
				extended_geometry_object_grade(object, local_first.x, local_first.z));
			local_second.y = LEGACY_S16_WRAP_SUB(
				local_second.y,
				extended_geometry_object_grade(object, local_second.x, local_second.z));
		}
		/* Exclude another 65536-unit page with identical wrapped coordinates. */
		legacy_s32 absolute_x, absolute_z;
		extended_geometry_world_position(first, &absolute_x, &absolute_z);
		legacy_s32 center_x = object->x * 1024 + object->width * 512;
		legacy_s32 center_z = track->height * 1024 - object->y * 1024 - object->height * 512;
		if (absolute_x - center_x < -3072 || absolute_x - center_x > 3072 ||
			absolute_z - center_z < -3072 || absolute_z - center_z > 3072) {
			continue;
		}
		legacy_s16 orientation =
			object->roll_ramp ? roll_ramp_orientation(object) : trkObjectList[id].ss_rotY;
		track_rotate_local(&local_first, orientation);
		track_rotate_local(&local_second, orientation);
		if (object->roll_ramp) {
			legacy_s16 contact;
			if (roll_ramp_solid_contact(&local_first, &local_second, &contact)) {
				if (!hit || contact < earliest) {
					earliest = contact;
				}
				hit = 1;
			}
			continue;
		}
		for (legacy_u16 box = 0; box < box_count; box++) {
			legacy_s16 contact;
			if (solid_box_contact(&local_first, &local_second, &boxes[box], &contact)) {
				if (!hit || contact < earliest) {
					earliest = contact;
				}
				hit = 1;
			}
		}
	}
	if (hit) {
		*fraction = earliest;
	}
	return hit;
}
/* Solid sweeps must retain all six position fraction bits. Rounding a wheel
 * resting on an incline down to a word can invent entry into its concrete. */
#define TRACK_SOLID_POSITION_SCALE 64L
#define TRACK_SOLID_QUERY_RADIUS (3L * CSTRK_TILE_WORLD_SIZE)

struct TRACK_FIXED_SOLID_POINT {
	legacy_s64 x, y, z;
};

struct TRACK_FIXED_SOLID_RATIO {
	legacy_u64 numerator, denominator;
};

struct TRACK_FIXED_SOLID_INTERVAL {
	struct TRACK_FIXED_SOLID_RATIO entry, exit;
	legacy_u8 blocking_entry, started_inside;
};

enum TRACK_FIXED_SOLID_QUERY { TRACK_FIXED_SOLID_VOLUME, TRACK_FIXED_SOLID_WALLS };

static legacy_s16 fixed_solid_ratio_less(struct TRACK_FIXED_SOLID_RATIO first,
										 struct TRACK_FIXED_SOLID_RATIO second)
{
	/* The usual small ratios need one cross multiplication. Euclidean fraction
	 * comparison also admits long sweeps without overflowing a 64-bit product. */
	legacy_u64 maximum = (legacy_u64)-1;
	if ((first.numerator == 0 || second.denominator <= maximum / first.numerator) &&
		(second.numerator == 0 || first.denominator <= maximum / second.numerator)) {
		return first.numerator * second.denominator < second.numerator * first.denominator;
	}
	legacy_u8 inverted = 0;
	for (;;) {
		legacy_u64 first_whole = first.numerator / first.denominator;
		legacy_u64 second_whole = second.numerator / second.denominator;
		if (first_whole != second_whole) {
			return inverted ? first_whole > second_whole : first_whole < second_whole;
		}
		legacy_u64 first_remainder = first.numerator % first.denominator;
		legacy_u64 second_remainder = second.numerator % second.denominator;
		if (!first_remainder || !second_remainder) {
			return inverted ? first_remainder != 0 : second_remainder != 0;
		}
		first.numerator = first.denominator;
		first.denominator = first_remainder;
		second.numerator = second.denominator;
		second.denominator = second_remainder;
		inverted ^= 1U;
	}
}

static legacy_s16 fixed_solid_clip(legacy_s64 first, legacy_s64 second, legacy_u8 shared,
								   legacy_u8 blocking, struct TRACK_FIXED_SOLID_INTERVAL *interval)
{
	if (first >= 0 && !(shared && first == 0)) {
		interval->started_inside = 0;
	}
	if (shared && first == 0 && second == 0) {
		return 1;
	}
	if (first >= 0 && second >= 0) {
		return 0;
	}
	if (first < 0 && second <= 0) {
		return 1;
	}
	struct TRACK_FIXED_SOLID_RATIO crossing;
	crossing.numerator = (legacy_u64)(first < 0 ? -first : first);
	legacy_s64 difference = first - second;
	crossing.denominator = (legacy_u64)(difference < 0 ? -difference : difference);
	if (first >= 0) {
		if (fixed_solid_ratio_less(interval->entry, crossing)) {
			interval->entry = crossing;
			interval->blocking_entry = blocking;
		} else if (!fixed_solid_ratio_less(crossing, interval->entry)) {
			/* A real side or bottom wins a simultaneous edge contact. */
			interval->blocking_entry |= blocking;
		}
	} else if (fixed_solid_ratio_less(crossing, interval->exit)) {
		interval->exit = crossing;
	}
	return fixed_solid_ratio_less(interval->entry, interval->exit);
}

static legacy_s64 fixed_solid_plane_distance(const struct TRACK_FIXED_SOLID_POINT *point,
											 const struct VECTOR corners[3])
{
	legacy_s64 ux = corners[1].x - corners[0].x;
	legacy_s64 uy = corners[1].y - corners[0].y;
	legacy_s64 uz = corners[1].z - corners[0].z;
	legacy_s64 vx = corners[2].x - corners[0].x;
	legacy_s64 vy = corners[2].y - corners[0].y;
	legacy_s64 vz = corners[2].z - corners[0].z;
	legacy_s64 nx = uy * vz - uz * vy;
	legacy_s64 ny = uz * vx - ux * vz;
	legacy_s64 nz = ux * vy - uy * vx;
	return nx * (point->x - corners[0].x * TRACK_SOLID_POSITION_SCALE) +
		   ny * (point->y - corners[0].y * TRACK_SOLID_POSITION_SCALE) +
		   nz * (point->z - corners[0].z * TRACK_SOLID_POSITION_SCALE);
}

static void fixed_solid_grade_corners(const struct CSTRK_OBJECT *object, legacy_s16 orientation,
									  struct VECTOR corners[3])
{
	legacy_s16 inverse = LEGACY_S16_WRAP_NEGATE(orientation) & ANGLE_MASK;
	for (legacy_u8 index = 0; index < 3U; index++) {
		struct VECTOR world = corners[index];
		track_rotate_local(&world, inverse);
		corners[index].y = LEGACY_S16_WRAP_ADD(
			corners[index].y, extended_geometry_object_grade(object, world.x, world.z));
	}
}

/* The concrete-to-grille join is a driving seam only for a span whose actual
 * endpoints are supported by the finite authored road. Its chord may cross a
 * slightly concave section boundary while both endpoints remain above the mesh. */
static legacy_s16 fixed_ramp_point_above_road(const struct TRACK_FIXED_SOLID_POINT *point,
											  const struct CSTRK_OBJECT *object,
											  legacy_s16 orientation)
{
	for (legacy_u16 index = 0; index < roll_ramp_road_triangle_count(); index++) {
		struct VECTOR triangle[3];
		roll_ramp_road_triangle(index, triangle);
		fixed_solid_grade_corners(object, orientation, triangle);
		legacy_u8 positive = 0, negative = 0;
		legacy_s64 projected_area = 0;
		for (legacy_u8 edge = 0; edge < 3U; edge++) {
			const struct VECTOR *first = &triangle[edge];
			const struct VECTOR *second = &triangle[(edge + 1U) % 3U];
			legacy_s64 dx = second->x - first->x;
			legacy_s64 dz = second->z - first->z;
			legacy_s64 side = dx * (point->z - first->z * TRACK_SOLID_POSITION_SCALE) -
							  dz * (point->x - first->x * TRACK_SOLID_POSITION_SCALE);
			positive |= side > 0;
			negative |= side < 0;
			projected_area += (legacy_s64)first->x * second->z - (legacy_s64)second->x * first->z;
		}
		/* Authored road winding has an upward normal. Degenerate or inverted
		 * projected triangles cannot prove that an endpoint is road-supported. */
		if (projected_area < 0 && !(positive && negative) &&
			fixed_solid_plane_distance(point, triangle) >= 0) {
			return 1;
		}
	}
	return 0;
}

static legacy_s16 fixed_solid_prism(const struct TRACK_FIXED_SOLID_POINT *first,
									const struct TRACK_FIXED_SOLID_POINT *second,
									const struct CSTRK_OBJECT *object, legacy_s16 orientation,
									const struct TRACK_SOLID_BOX *bounds, legacy_s16 end_height,
									legacy_u8 shared_minimum, legacy_u8 shared_maximum,
									legacy_u8 road_top, legacy_u8 road_minimum,
									enum TRACK_FIXED_SOLID_QUERY query, legacy_s16 *fraction)
{
	struct TRACK_FIXED_SOLID_INTERVAL interval = {{0, 1}, {1, 1}, 0, 1};
	if (!fixed_solid_clip(bounds->minimum.x * TRACK_SOLID_POSITION_SCALE - first->x,
						  bounds->minimum.x * TRACK_SOLID_POSITION_SCALE - second->x, 0, 1,
						  &interval) ||
		!fixed_solid_clip(first->x - bounds->maximum.x * TRACK_SOLID_POSITION_SCALE,
						  second->x - bounds->maximum.x * TRACK_SOLID_POSITION_SCALE, 0, 1,
						  &interval) ||
		!fixed_solid_clip(bounds->minimum.z * TRACK_SOLID_POSITION_SCALE - first->z,
						  bounds->minimum.z * TRACK_SOLID_POSITION_SCALE - second->z,
						  shared_minimum, !(shared_minimum || road_minimum), &interval) ||
		!fixed_solid_clip(first->z - bounds->maximum.z * TRACK_SOLID_POSITION_SCALE,
						  second->z - bounds->maximum.z * TRACK_SOLID_POSITION_SCALE,
						  shared_maximum, !shared_maximum, &interval)) {
		return 0;
	}
	/* Use the very same integer corner heights as the rendered mesh. CSTRK
	 * support tilts along one axis, so these top and bottom quads are planar. */
	struct VECTOR bottom[3] = {{bounds->minimum.x, bounds->minimum.y, bounds->maximum.z},
							   {bounds->maximum.x, bounds->minimum.y, bounds->maximum.z},
							   {bounds->minimum.x, bounds->minimum.y, bounds->minimum.z}};
	struct VECTOR top[3] = {{bounds->minimum.x, bounds->maximum.y, bounds->maximum.z},
							{bounds->maximum.x, bounds->maximum.y, bounds->maximum.z},
							{bounds->minimum.x, end_height, bounds->minimum.z}};
	fixed_solid_grade_corners(object, orientation, bottom);
	fixed_solid_grade_corners(object, orientation, top);
	if (!fixed_solid_clip(-fixed_solid_plane_distance(first, bottom),
						  -fixed_solid_plane_distance(second, bottom), 0, 1, &interval) ||
		!fixed_solid_clip(fixed_solid_plane_distance(first, top),
						  fixed_solid_plane_distance(second, top), 0, !road_top, &interval)) {
		return 0;
	}
	/* Plane contact resolves road-top entry. The additional wheel-wall sweep
	 * must only stop on a genuine side, bottom, terminal face or support. A
	 * concrete segment's shared caps are internal to the same finite solid. */
	if (query == TRACK_FIXED_SOLID_WALLS && !interval.started_inside && !interval.blocking_entry) {
		return 0;
	}
	*fraction =
		(legacy_s16)(interval.entry.numerator * TRIG_FIXED_ONE / interval.entry.denominator);
	return 1;
}

static struct TRACK_FIXED_SOLID_POINT fixed_solid_local(const struct VECTORLONG *point,
														legacy_s32 center_x, legacy_s16 elevation,
														legacy_s32 center_z, legacy_s16 orientation)
{
	struct TRACK_FIXED_SOLID_POINT local = {
		(legacy_s64)point->lx - center_x * TRACK_SOLID_POSITION_SCALE,
		(legacy_s64)point->ly - elevation * TRACK_SOLID_POSITION_SCALE,
		(legacy_s64)point->lz - center_z * TRACK_SOLID_POSITION_SCALE};
	legacy_s64 x = local.x;
	switch (orientation) {
		case ANGLE_QUARTER_TURN:
			local.x = -local.z;
			local.z = x;
			break;
		case ANGLE_HALF_TURN:
			local.x = -x;
			local.z = -local.z;
			break;
		case ANGLE_THREE_QUARTER_TURN:
			local.x = local.z;
			local.z = -x;
			break;
	}
	return local;
}

static legacy_s16 extended_solid_obstacle_contact_fixed(const struct VECTORLONG *first,
														const struct VECTORLONG *second,
														enum TRACK_FIXED_SOLID_QUERY query,
														legacy_s16 *fraction)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	legacy_s16 hit = 0, earliest = TRIG_FIXED_ONE;
	for (legacy_u32 index = 0; index < track->object_count; index++) {
		const struct CSTRK_OBJECT *object = &track->objects[index];
		legacy_u16 id = extended_track_object_id(object);
		if (id >= sizeof(trkObjectList) / sizeof(trkObjectList[0]) ||
			(!object->roll_ramp && trkObjectList[id].ss_physicalModel != PHYSICAL_MODEL_SLALOM)) {
			continue;
		}
		legacy_s32 center_x =
			object->x * CSTRK_TILE_WORLD_SIZE + object->width * (CSTRK_TILE_WORLD_SIZE / 2);
		legacy_s32 center_z = track->height * CSTRK_TILE_WORLD_SIZE -
							  object->y * CSTRK_TILE_WORLD_SIZE -
							  object->height * (CSTRK_TILE_WORLD_SIZE / 2);
		legacy_s64 minimum_x = (center_x - TRACK_SOLID_QUERY_RADIUS) * TRACK_SOLID_POSITION_SCALE;
		legacy_s64 maximum_x = (center_x + TRACK_SOLID_QUERY_RADIUS) * TRACK_SOLID_POSITION_SCALE;
		legacy_s64 minimum_z = (center_z - TRACK_SOLID_QUERY_RADIUS) * TRACK_SOLID_POSITION_SCALE;
		legacy_s64 maximum_z = (center_z + TRACK_SOLID_QUERY_RADIUS) * TRACK_SOLID_POSITION_SCALE;
		if ((first->lx < minimum_x && second->lx < minimum_x) ||
			(first->lx > maximum_x && second->lx > maximum_x) ||
			(first->lz < minimum_z && second->lz < minimum_z) ||
			(first->lz > maximum_z && second->lz > maximum_z)) {
			continue;
		}
		struct VECTOR origin;
		extended_geometry_object_origin(track, object, &origin);
		legacy_s16 orientation =
			object->roll_ramp ? roll_ramp_orientation(object) : trkObjectList[id].ss_rotY;
		struct TRACK_FIXED_SOLID_POINT local_first =
			fixed_solid_local(first, center_x, origin.y, center_z, orientation);
		struct TRACK_FIXED_SOLID_POINT local_second =
			fixed_solid_local(second, center_x, origin.y, center_z, orientation);
		legacy_u8 road_seam = query == TRACK_FIXED_SOLID_WALLS && object->roll_ramp &&
							  local_first.z <= 0 && local_second.z > 0 &&
							  fixed_ramp_point_above_road(&local_first, object, orientation) &&
							  fixed_ramp_point_above_road(&local_second, object, orientation);
		legacy_u16 count = object->roll_ramp
							   ? ROLL_RAMP_SEGMENT_COUNT / 2U + ROLL_RAMP_SUPPORT_BOX_COUNT
							   : sizeof(slalom_boxes) / sizeof(slalom_boxes[0]);
		for (legacy_u16 part = 0; part < count; part++) {
			struct TRACK_SOLID_BOX box;
			legacy_s16 end_height;
			legacy_u8 shared_minimum = 0, shared_maximum = 0;
			legacy_u8 road_top = object->roll_ramp && part < ROLL_RAMP_SEGMENT_COUNT / 2U;
			if (road_top) {
				legacy_s16 span = ROLL_RAMP_LENGTH / ROLL_RAMP_SEGMENT_COUNT;
				legacy_s16 start = part * span, end = start + span;
				box.minimum =
					(struct VECTOR){-ROLL_RAMP_HALF_WIDTH, 0, ROLL_RAMP_HALF_LENGTH - end};
				box.maximum = (struct VECTOR){ROLL_RAMP_HALF_WIDTH, roll_ramp_center_height(start),
											  ROLL_RAMP_HALF_LENGTH - start};
				end_height = roll_ramp_center_height(end);
				shared_minimum = end < CSTRK_TILE_WORLD_SIZE;
				shared_maximum = start > 0;
			} else if (object->roll_ramp) {
				struct ROLL_RAMP_BOX support;
				roll_ramp_support_box(part - ROLL_RAMP_SEGMENT_COUNT / 2U, &support);
				box.minimum = support.minimum;
				box.maximum = support.maximum;
				end_height = box.maximum.y;
			} else {
				box = slalom_boxes[part];
				end_height = box.maximum.y;
			}
			legacy_s16 contact;
			if (fixed_solid_prism(&local_first, &local_second, object, orientation, &box,
								  end_height, shared_minimum, shared_maximum, road_top,
								  road_top && !shared_minimum && road_seam, query, &contact)) {
				if (!hit || contact < earliest) {
					earliest = contact;
				}
				hit = 1;
			}
		}
	}
	if (hit) {
		*fraction = earliest;
	}
	return hit;
}

legacy_s16 extended_trackobj_solid_contact(struct VECTOR *first, struct VECTOR *second,
										   legacy_s16 *fraction, legacy_s16 *hit)
{
	if (!extended_track_active()) {
		return 0;
	}
	*hit = extended_solid_obstacle_contact(first, second, fraction, slalom_boxes,
										   TRACK_SLALOM_BOX_COUNT);
	return 1;
}

#endif

legacy_s16 track_solid_obstacle_contact_fixed(struct VECTORLONG *first, struct VECTORLONG *second,
											  legacy_s16 *fraction)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		return extended_solid_obstacle_contact_fixed(first, second, TRACK_FIXED_SOLID_VOLUME,
													 fraction);
	}
#endif
	struct VECTOR start = {position_to_word(first->lx), position_to_word(first->ly),
						   position_to_word(first->lz)};
	struct VECTOR end = {position_to_word(second->lx), position_to_word(second->ly),
						 position_to_word(second->lz)};
	return track_solid_obstacle_contact(&start, &end, fraction);
}

legacy_s16 track_solid_wall_contact_fixed(struct VECTORLONG *first, struct VECTORLONG *second,
										  legacy_s16 *fraction)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		return extended_solid_obstacle_contact_fixed(first, second, TRACK_FIXED_SOLID_WALLS,
													 fraction);
	}
#endif
	return track_solid_obstacle_contact_fixed(first, second, fraction);
}

legacy_s16 track_wall_intersects_segment_fixed(struct VECTORLONG *first, struct VECTORLONG *second)
{
	struct VECTOR start = {position_to_word(first->lx), position_to_word(first->ly),
						   position_to_word(first->lz)};
	struct VECTOR end = {position_to_word(second->lx), position_to_word(second->ly),
						 position_to_word(second->lz)};
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		struct TRACK_COLLISION_SNAPSHOT saved;
		track_collision_capture(&saved);
		legacy_s32 context_x, context_z;
		extended_geometry_get_context(&context_x, &context_z);
		extended_geometry_set_context(first->lx, first->lz);
		build_track_object(&start, &end);
		legacy_s16 hit = selected_wall_intersects_segment(&start, &end);
		if (!hit) {
			build_track_object(&end, &start);
			hit = selected_wall_intersects_segment(&start, &end);
		}
		track_collision_restore(&saved);
		extended_geometry_set_context(context_x * TRACK_SOLID_POSITION_SCALE,
									  context_z * TRACK_SOLID_POSITION_SCALE);
		if (!hit) {
			legacy_s16 fraction;
			hit = extended_solid_obstacle_contact_fixed(first, second, TRACK_FIXED_SOLID_WALLS,
														&fraction);
		}
		return hit;
	}
#endif
	return track_wall_intersects_segment(&start, &end);
}
