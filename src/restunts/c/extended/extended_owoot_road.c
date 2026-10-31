#include "extended_owoot_road.h"
#ifdef RESTUNTS_SDL3
#include "extended_track.h"
#include "extended_geometry.h"
#include "roll_ramp.h"
#include "../owoot/owoot_road_internal.h"
#include "../track/track_objects.h"
#include "../track/trackdata_layout.h"
#include "../externs.h"

static legacy_s16 road_extended_object_overlaps_wheel(const struct CSTRK_OBJECT *placed,
													  const struct VECTOR *vertices,
													  legacy_u16 ring_count,
													  legacy_s16 contact_tolerance)
{
	legacy_u32 id = extended_track_object_id(placed);
	const struct TRACKOBJECT *object = &trkObjectList[id];
	legacy_s16 model = placed->tunnel_slalom ? PHYSICAL_MODEL_TUNNEL : object->ss_physicalModel;
	if (model < 0 || model > PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT) {
		return 0;
	}
	if (model == PHYSICAL_MODEL_ROAD && (placed->slope_x || placed->slope_y) &&
		!extended_geometry_uses_grade(placed)) {
		model = OWOOT_HILL_ROAD_MODEL;
	}
	struct VECTOR origin, local[OWOOT_WHEEL_VERTEX_MAX];
	extended_geometry_object_origin(extended_track_model(), placed, &origin);
	for (legacy_u16 index = 0; index < ring_count * OWOOT_WHEEL_RIM_COUNT; index++) {
		local[index] = (struct VECTOR){LEGACY_S16_WRAP_SUB(vertices[index].x, origin.x),
									   LEGACY_S16_WRAP_SUB(vertices[index].y, origin.y),
									   LEGACY_S16_WRAP_SUB(vertices[index].z, origin.z)};
		if (extended_geometry_uses_grade(placed)) {
			local[index].y = LEGACY_S16_WRAP_SUB(
				local[index].y,
				extended_geometry_object_grade(placed, local[index].x, local[index].z));
		}
		legacy_s16 x = local[index].x, z = local[index].z;
		legacy_s16 orientation =
			placed->roll_ramp ? roll_ramp_orientation(placed) : object->ss_rotY;
		switch ((legacy_u16)orientation) {
			case ANGLE_QUARTER_TURN:
				local[index].x = -z;
				local[index].z = x;
				break;
			case ANGLE_HALF_TURN:
				local[index].x = -x;
				local[index].z = -z;
				break;
			case ANGLE_THREE_QUARTER_TURN:
				local[index].x = z;
				local[index].z = -x;
				break;
		}
	}
	if (placed->roll_ramp) {
		for (legacy_u16 index = 0; index < roll_ramp_road_triangle_count(); index++) {
			struct OWOOT_ROAD_TRIANGLE triangle;
			roll_ramp_road_triangle(index, triangle.vertex);
			if (owoot_road_triangle_overlaps_wheel(&triangle, local, ring_count,
												   contact_tolerance)) {
				return 1;
			}
		}
		return 0;
	}
	legacy_u16 count = track_road_triangle_count(model);
	for (legacy_u16 index = 0; index < count; index++) {
		struct OWOOT_ROAD_TRIANGLE triangle;
		track_road_triangle(model, index, triangle.vertex);
		if (owoot_road_triangle_overlaps_wheel(&triangle, local, ring_count, contact_tolerance)) {
			return 1;
		}
	}
	return 0;
}

static legacy_s16 road_extended_tile_overlaps_wheel(const struct VECTOR *vertices,
													legacy_u16 ring_count, legacy_s16 column,
													legacy_s16 world_row,
													legacy_s16 contact_tolerance)
{
	const struct CSTRK_OBJECT *candidates[EXTENDED_TRACK_CELL_OBJECT_MAX];
	legacy_u16 count =
		extended_track_objects_in_cell(column, extended_track_height() - 1 - world_row, candidates,
									   EXTENDED_TRACK_CELL_OBJECT_MAX);
	for (legacy_u16 index = 0; index < count; index++) {
		if (road_extended_object_overlaps_wheel(candidates[index], vertices, ring_count,
												contact_tolerance)) {
			return 1;
		}
	}
	return 0;
}

static legacy_s16 road_extended_overlaps_wheel(const struct VECTOR *vertices, legacy_u16 ring_count,
											   legacy_s16 contact_tolerance)
{
	legacy_s32 min_x, min_z;
	extended_geometry_world_position(&vertices[0], &min_x, &min_z);
	legacy_s32 max_x = min_x, max_z = min_z;
	for (legacy_u16 index = 1; index < ring_count * OWOOT_WHEEL_RIM_COUNT; index++) {
		legacy_s32 x, z;
		extended_geometry_world_position(&vertices[index], &x, &z);
		if (x < min_x) {
			min_x = x;
		}
		if (x > max_x) {
			max_x = x;
		}
		if (z < min_z) {
			min_z = z;
		}
		if (z > max_z) {
			max_z = z;
		}
	}
	legacy_s32 first_column =
		(legacy_s32)((min_x - OWOOT_ROAD_TILE_OVERHANG) >> TRACK_TILE_POSITION_SHIFT);
	legacy_s32 last_column =
		(legacy_s32)((max_x + OWOOT_ROAD_TILE_OVERHANG) >> TRACK_TILE_POSITION_SHIFT);
	legacy_s32 first_row =
		(legacy_s32)((min_z - OWOOT_ROAD_TILE_OVERHANG) >> TRACK_TILE_POSITION_SHIFT);
	legacy_s32 last_row =
		(legacy_s32)((max_z + OWOOT_ROAD_TILE_OVERHANG) >> TRACK_TILE_POSITION_SHIFT);
	if (first_column < 0) {
		first_column = 0;
	}
	if (first_row < 0) {
		first_row = 0;
	}
	if (last_column >= extended_track_width()) {
		last_column = extended_track_width() - 1;
	}
	if (last_row >= extended_track_height()) {
		last_row = extended_track_height() - 1;
	}
	for (legacy_s32 row = first_row; row <= last_row; row++) {
		for (legacy_s32 column = first_column; column <= last_column; column++) {
			if (road_extended_tile_overlaps_wheel(vertices, ring_count, column, row,
												  contact_tolerance)) {
				return 1;
			}
		}
	}
	return 0;
}

legacy_s16 extended_owoot_road_overlap(const struct VECTOR *vertices, legacy_u16 ring_count,
									   legacy_s16 contact_tolerance, legacy_s16 *overlap)
{
	if (!extended_track_active()) {
		return 0;
	}
	*overlap = road_extended_overlaps_wheel(vertices, ring_count, contact_tolerance);
	return 1;
}
#endif
