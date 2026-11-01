/* Query finite walls and solids without replacing the selected surface. */
#include "../restunts.h"
#include "../legacy.h"
#include "../math/math.h"
#include "trackdata_layout.h"
#include "track_types.h"
#include "track_objects.h"
#include "track_collision.h"
#include "trackobj_internal.h"
#include "../physics/physics_internal.h"
#include "../owoot/owoot_road.h"
#include "../externs.h"
#ifdef RESTUNTS_SDL3
#include "../extended/extended_trackobj.h"
#endif

#include "trackobj_model_internal.h"

static legacy_s16 wall_intersection_coordinate(legacy_s16 first, legacy_s16 second,
											   legacy_s16 first_depth, legacy_s16 second_depth)
{
	legacy_s32 difference = (legacy_s32)second - first;
#ifdef RESTUNTS_SDL3
	difference = extended_trackobj_coordinate_delta(first, second, difference);
#endif
	legacy_s32 numerator = difference * -(legacy_s32)first_depth;
	legacy_s32 denominator = (legacy_s32)second_depth - first_depth;
	return LEGACY_S16_FROM_BITS((legacy_u16)((legacy_s32)first + numerator / denominator));
}

static legacy_s16 selected_wall_matches(const struct TRACK_COLLISION_SNAPSHOT *candidate)
{
	return wallindex == candidate->wall_index && wallStartX == candidate->wall_x &&
		   wallStartZ == candidate->wall_z && wallOrientation == candidate->wall_orientation &&
		   planindex == candidate->plane_index && terrainHeight == candidate->terrain_height &&
		   elem_xCenter == candidate->element_x && elem_zCenter == candidate->element_z &&
		   wallHeight == candidate->wall_height && elRdWallRelated == candidate->wall_lower_bound;
}

#define WALL_CONTACT_PROBE_DISTANCE 2

static legacy_s16 selected_wall_contains_point(struct VECTOR *point, struct MATRIX *rotation)
{
	struct TRACK_COLLISION_SNAPSHOT candidate;
	track_collision_capture(&candidate);

	/* Requery across the wall at the intersection, not at a segment endpoint.
	 * This retains the finite extent and distinguishes a rail from the solid
	 * side below it. Two world units keep the probes on opposite grid sides. */
	legacy_s16 offset_x =
		(legacy_s16)((legacy_s32)rotation->m._31 * WALL_CONTACT_PROBE_DISTANCE / TRIG_FIXED_ONE);
	legacy_s16 offset_z =
		(legacy_s16)((legacy_s32)rotation->m._33 * WALL_CONTACT_PROBE_DISTANCE / TRIG_FIXED_ONE);
	struct VECTOR probe;
	probe.x = offset_x;
	probe.y = 0;
	probe.z = offset_z;
	struct VECTOR first = *point;
	struct VECTOR second = *point;
	first.x = LEGACY_S16_WRAP_ADD(first.x, probe.x);
	first.y = LEGACY_S16_WRAP_ADD(first.y, probe.y);
	first.z = LEGACY_S16_WRAP_ADD(first.z, probe.z);
	second.x = LEGACY_S16_WRAP_SUB(second.x, probe.x);
	second.y = LEGACY_S16_WRAP_SUB(second.y, probe.y);
	second.z = LEGACY_S16_WRAP_SUB(second.z, probe.z);
	build_track_object(&first, &second);
	legacy_s16 contains = selected_wall_matches(&candidate);
	if (!contains) {
		build_track_object(&second, &first);
		contains = selected_wall_matches(&candidate);
	}
	track_collision_restore(&candidate);
	return contains;
}

legacy_s16 selected_wall_intersects_segment(struct VECTOR *first, struct VECTOR *second)
{
	if (wallindex == TRACK_WALL_NONE) {
		return 0;
	}
	struct MATRIX rotation;
	mat_rot_y(&rotation,
			  LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_NEGATE(wallOrientation), ANGLE_QUARTER_TURN));
	struct VECTOR relative;
	relative.x = LEGACY_S16_WRAP_SUB(first->x, wallStartX);
	relative.y = 0;
	relative.z = LEGACY_S16_WRAP_SUB(first->z, wallStartZ);
	struct VECTOR start;
	mat_mul_vector(&relative, &rotation, &start);
	relative.x = LEGACY_S16_WRAP_SUB(second->x, wallStartX);
	relative.z = LEGACY_S16_WRAP_SUB(second->z, wallStartZ);
	struct VECTOR end;
	mat_mul_vector(&relative, &rotation, &end);
	/* Endpoint touches belong to the existing wheel collision response. This
	 * test only adds walls that pierce the car between two wheel paths. */
	if ((start.z >= 0 && end.z >= 0) || (start.z <= 0 && end.z <= 0)) {
		return 0;
	}

	struct VECTOR intersection;
	intersection.x = wall_intersection_coordinate(first->x, second->x, start.z, end.z);
	intersection.y = wall_intersection_coordinate(first->y, second->y, start.z, end.z);
	intersection.z = wall_intersection_coordinate(first->z, second->z, start.z, end.z);
	/* Interpolate signed distances before rounding the world intersection. On
	 * an inclined plane, rounding its coordinates can hide a shallow impact.
	 * Compare the ratio directly to retain the strict wall height limits. */
	legacy_s16 first_height = plane_signed_distance(planindex, first->x, first->y, first->z);
	legacy_s16 second_height = plane_signed_distance(planindex, second->x, second->y, second->z);
	legacy_s32 numerator = (legacy_s32)first_height * end.z - (legacy_s32)second_height * start.z;
	legacy_s32 denominator = (legacy_s32)end.z - start.z;
	if (denominator < 0) {
		numerator = -numerator;
		denominator = -denominator;
	}
	return numerator > (legacy_s32)elRdWallRelated * denominator &&
		   numerator < (legacy_s32)wallHeight * denominator &&
		   selected_wall_contains_point(&intersection, &rotation);
}

struct TRACK_CONTACT_RATIO {
	legacy_u32 numerator;
	legacy_u32 denominator;
};

struct TRACK_SOLID_INTERVAL {
	struct TRACK_CONTACT_RATIO entry;
	struct TRACK_CONTACT_RATIO exit;
};

static legacy_s16 solid_ratio_less(const struct TRACK_CONTACT_RATIO *first,
								   const struct TRACK_CONTACT_RATIO *second)
{
	/* Word-coordinate differences are at most 65535. Unsigned products retain
	 * the full comparison without overflow or premature fraction rounding. */
	return first->numerator * second->denominator < second->numerator * first->denominator;
}

static legacy_s16 clip_solid_axis(legacy_s16 first, legacy_s16 second, legacy_s16 minimum,
								  legacy_s16 maximum, struct TRACK_SOLID_INTERVAL *interval)
{
	legacy_s32 denominator = (legacy_s32)second - first;
	if (denominator == 0) {
		return first > minimum && first < maximum;
	}
	legacy_s32 entry = (legacy_s32)minimum - first;
	legacy_s32 exit = (legacy_s32)maximum - first;
	if (denominator < 0) {
		legacy_s32 temporary = entry;
		entry = -exit;
		exit = -temporary;
		denominator = -denominator;
	}
	if (entry >= denominator || exit <= 0) {
		return 0;
	}
	struct TRACK_CONTACT_RATIO ratio;
	ratio.denominator = (legacy_u32)denominator;
	if (entry > 0) {
		ratio.numerator = (legacy_u32)entry;
		if (solid_ratio_less(&interval->entry, &ratio)) {
			interval->entry = ratio;
		}
	}
	if (exit < denominator) {
		ratio.numerator = (legacy_u32)exit;
		if (solid_ratio_less(&ratio, &interval->exit)) {
			interval->exit = ratio;
		}
	}
	return solid_ratio_less(&interval->entry, &interval->exit);
}

legacy_s16 solid_box_contact(const struct VECTOR *first, const struct VECTOR *second,
							 const struct TRACK_SOLID_BOX *box, legacy_s16 *fraction)
{
	struct TRACK_SOLID_INTERVAL interval = {{0, 1}, {1, 1}};
	if (!clip_solid_axis(first->x, second->x, box->minimum.x, box->maximum.x, &interval) ||
		!clip_solid_axis(first->y, second->y, box->minimum.y, box->maximum.y, &interval) ||
		!clip_solid_axis(first->z, second->z, box->minimum.z, box->maximum.z, &interval)) {
		return 0;
	}
	*fraction =
		(legacy_s16)(interval.entry.numerator * TRIG_FIXED_ONE / interval.entry.denominator);
	return 1;
}

static void solid_world_bounds(struct TRACK_SOLID_BOX *world, const struct TRACK_SOLID_BOX *local,
							   legacy_s16 orientation, legacy_s16 column, legacy_s16 row,
							   legacy_s16 elevation)
{
	*world = *local;
	legacy_s16 inverse_orientation =
		LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S16_WRAP_NEGATE(orientation) & ANGLE_MASK);
	track_rotate_local(&world->minimum, inverse_orientation);
	track_rotate_local(&world->maximum, inverse_orientation);
	if (world->minimum.x > world->maximum.x) {
		legacy_s16 temporary = world->minimum.x;
		world->minimum.x = world->maximum.x;
		world->maximum.x = temporary;
	}
	if (world->minimum.z > world->maximum.z) {
		legacy_s16 temporary = world->minimum.z;
		world->minimum.z = world->maximum.z;
		world->maximum.z = temporary;
	}
	world->minimum.x = LEGACY_S16_WRAP_ADD(world->minimum.x, track_column_centers[column]);
	world->maximum.x = LEGACY_S16_WRAP_ADD(world->maximum.x, track_column_centers[column]);
	world->minimum.y = LEGACY_S16_WRAP_ADD(world->minimum.y, elevation);
	world->maximum.y = LEGACY_S16_WRAP_ADD(world->maximum.y, elevation);
	world->minimum.z = LEGACY_S16_WRAP_ADD(world->minimum.z, terraincenterpos[row]);
	world->maximum.z = LEGACY_S16_WRAP_ADD(world->maximum.z, terraincenterpos[row]);
}

/* These finite solids use the existing wall clearance above the road, but
 * stop at their physical base rather than extending below a raised tile. */
const struct TRACK_SOLID_BOX slalom_boxes[TRACK_SLALOM_BOX_COUNT] = {
	{{SLALOM_POLE_INNER_X, 0, -SLALOM_POLE_FAR_Z},
	 {SLALOM_POLE_OUTER_X, NON_GRASS_HEIGHT_OFFSET + SLALOM_POLE_WALL_HEIGHT, -SLALOM_POLE_NEAR_Z}},
	{{-SLALOM_POLE_OUTER_X, 0, SLALOM_POLE_NEAR_Z},
	 {-SLALOM_POLE_INNER_X, NON_GRASS_HEIGHT_OFFSET + SLALOM_POLE_WALL_HEIGHT, SLALOM_POLE_FAR_Z}}};

legacy_s16 track_solid_obstacle_contact(struct VECTOR *first, struct VECTOR *second,
										legacy_s16 *fraction)
{
#ifdef RESTUNTS_SDL3
	legacy_s16 extended_hit;
	if (extended_trackobj_solid_contact(first, second, fraction, &extended_hit)) {
		return extended_hit;
	}
#endif
	legacy_s16 minimum_x = first->x < second->x ? first->x : second->x;
	legacy_s16 maximum_x = first->x > second->x ? first->x : second->x;
	legacy_s16 minimum_z = first->z < second->z ? first->z : second->z;
	legacy_s16 maximum_z = first->z > second->z ? first->z : second->z;
	legacy_s16 first_column = LEGACY_S16_SAR(minimum_x, TRACK_TILE_POSITION_SHIFT);
	legacy_s16 last_column = LEGACY_S16_SAR(maximum_x, TRACK_TILE_POSITION_SHIFT);
	legacy_s16 first_row = LEGACY_S16_SAR(minimum_z, TRACK_TILE_POSITION_SHIFT);
	legacy_s16 last_row = LEGACY_S16_SAR(maximum_z, TRACK_TILE_POSITION_SHIFT);
	if (first_column < 0) {
		first_column = 0;
	}
	if (last_column > TRACK_GRID_LAST_INDEX) {
		last_column = TRACK_GRID_LAST_INDEX;
	}
	if (first_row < 0) {
		first_row = 0;
	}
	if (last_row > TRACK_GRID_LAST_INDEX) {
		last_row = TRACK_GRID_LAST_INDEX;
	}

	/* Inspect every candidate tile, including solids between two outside
	 * endpoints. This lookup does not alter the selected collision globals. */
	legacy_s16 first_contact = TRIG_FIXED_ONE;
	legacy_s16 hit = 0;
	for (legacy_s16 row = first_row; row <= last_row; row++) {
		for (legacy_s16 column = first_column; column <= last_column; column++) {
			legacy_u8 tile = track_element_map[terrainrows[row] + column];
			if (tile >= sizeof(trkObjectList) / sizeof(trkObjectList[0]) ||
				trkObjectList[tile].ss_physicalModel != PHYSICAL_MODEL_SLALOM) {
				continue;
			}
			legacy_s16 elevation = track_terrain_map[trackrows[row] + column] == TERRAIN_RAISED_TILE
									   ? hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX]
									   : 0;
			for (legacy_u16 index = 0; index < sizeof(slalom_boxes) / sizeof(slalom_boxes[0]);
				 index++) {
				struct TRACK_SOLID_BOX box;
				solid_world_bounds(&box, &slalom_boxes[index], trkObjectList[tile].ss_rotY, column,
								   row, elevation);
				legacy_s16 contact;
				if (solid_box_contact(first, second, &box, &contact)) {
					if (!hit || contact < first_contact) {
						first_contact = contact;
					}
					hit = 1;
				}
			}
		}
	}
	if (hit) {
		*fraction = first_contact;
	}
	return hit;
}

legacy_s16 track_wall_intersects_segment(struct VECTOR *first, struct VECTOR *second)
{
	struct TRACK_COLLISION_SNAPSHOT saved;
	track_collision_capture(&saved);
	build_track_object(first, second);
	legacy_s16 hit = selected_wall_intersects_segment(first, second);
	if (!hit) {
		build_track_object(second, first);
		hit = selected_wall_intersects_segment(first, second);
	}
	track_collision_restore(&saved);
	if (!hit) {
		legacy_s16 fraction;
		hit = track_solid_obstacle_contact(first, second, &fraction);
	}
	return hit;
}

legacy_s16 track_surface_contains_point(struct VECTOR *point)
{
	/* A footprint query must not replace the wheel's selected collision state. */
	struct TRACK_COLLISION_SNAPSHOT saved;
	track_collision_capture(&saved);
	build_track_object(point, point);
	legacy_s16 contains = planindex == saved.plane_index && elem_xCenter == saved.element_x &&
						  elem_zCenter == saved.element_z &&
						  terrainHeight == saved.terrain_height &&
						  track_wall_collision_enabled == 0;
	track_collision_restore(&saved);
	return contains;
}

legacy_s16 sweep_track_surface_candidates(struct VECTOR *previous, struct VECTOR *current,
										  TRACK_SURFACE_SWEEP_TEST test, legacy_s16 *fraction)
{
	struct TRACK_COLLISION_SNAPSHOT saved;
	track_collision_capture(&saved);
	legacy_s16 first_fraction = 0;
	legacy_s16 hit = test(previous, current, &first_fraction);

	/* A fast wheel can leave the surface footprint before the next lookup. */
	build_track_object(previous, current);
	legacy_s16 previous_fraction = 0;
	if (test(previous, current, &previous_fraction)) {
		if (!hit || previous_fraction < first_fraction) {
			first_fraction = previous_fraction;
		}
		hit = 1;
	}
	track_collision_restore(&saved);
	if (hit) {
		*fraction = first_fraction;
	}
	return hit;
}
