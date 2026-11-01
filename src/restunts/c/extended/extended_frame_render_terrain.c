#ifdef RESTUNTS_SDL3
#include "extended_frame_internal.h"
#include <stdlib.h>
#include <string.h>

void frame_extended_join_low_half(struct VECTOR *points, legacy_u16 count,
								  const struct FRAME_EXTENDED_TERRAIN *terrain, legacy_s8 detail)
{
	if (!supersight_enabled) {
		return;
	}
	const struct TRACKOBJECT *definition = &terrain_scene_objects[terrain->profile.terrain_id];
	const struct SHAPE3D *source = frame_track_shape(definition, detail);
	const legacy_u8 *primitive = source->shape3d_primitives;
	for (legacy_u16 vertex = 0; vertex < primitive[0]; vertex++) {
		struct VECTOR point;
		shape3d_vertex_read(source, primitive[2U + source->shape3d_numpaints + vertex], &point);
		frame_extended_rotate_vertex(&point, definition->ss_rotY);
		frame_extended_join_hill_vertex(&point, terrain->edges);
		if (point.y != 0) {
			continue;
		}
		for (legacy_u16 corner = 0; corner < count; corner++) {
			if ((points[corner].x < 0) == (point.x < 0) &&
				(points[corner].z < 0) == (point.z < 0)) {
				points[corner] = point;
			}
		}
	}
}

void hires_track_collect_terrain_objects(struct FRAME_EXTENDED_TERRAIN *terrain)
{
	if (!supersight_enabled) {
		return;
	}
	/* Authored roads overhang their grid rectangle by a few world units.
	 * Cut those faces from neighboring terrain too, using the same bounded
	 * origin lookup rather than scanning every object in the track. */
	legacy_s32 left = terrain->column * CSTRK_TILE_WORLD_SIZE;
	legacy_s32 north = terrain->row * CSTRK_TILE_WORLD_SIZE;
	for (legacy_s16 row = terrain->row - 1; row <= terrain->row + 1; row++) {
		for (legacy_s16 column = terrain->column - 1; column <= terrain->column + 1; column++) {
			if (column == terrain->column && row == terrain->row) {
				continue;
			}
			const struct CSTRK_OBJECT *candidates[EXTENDED_TRACK_CELL_OBJECT_MAX];
			legacy_u16 count = extended_track_objects_in_cell(column, row, candidates,
															  EXTENDED_TRACK_CELL_OBJECT_MAX);
			for (legacy_u16 candidate = 0; candidate < count; candidate++) {
				const struct CSTRK_OBJECT *object = candidates[candidate];
				legacy_s32 object_left = object->x * CSTRK_TILE_WORLD_SIZE;
				legacy_s32 object_north = object->y * CSTRK_TILE_WORLD_SIZE;
				if (left >= object_left + object->width * CSTRK_TILE_WORLD_SIZE +
								FRAME_EXTENDED_ROAD_OVERSCAN ||
					left + CSTRK_TILE_WORLD_SIZE <= object_left - FRAME_EXTENDED_ROAD_OVERSCAN ||
					north >= object_north + object->height * CSTRK_TILE_WORLD_SIZE +
								 FRAME_EXTENDED_ROAD_OVERSCAN ||
					north + CSTRK_TILE_WORLD_SIZE <= object_north - FRAME_EXTENDED_ROAD_OVERSCAN) {
					continue;
				}
				legacy_u16 existing = 0;
				while (existing < terrain->object_count && terrain->objects[existing] != object) {
					existing++;
				}
				if (existing == terrain->object_count &&
					terrain->object_count < FRAME_EXTENDED_TERRAIN_OBJECT_MAX) {
					terrain->objects[terrain->object_count++] = object;
				}
			}
		}
	}
}

legacy_u8 hires_track_uses_grade(const struct CSTRK_OBJECT *object, legacy_u8 *result)
{
	if (!supersight_enabled) {
		return 0;
	}
	*result = extended_geometry_uses_grade(object);
	return 1;
}
legacy_u8 hires_track_object_grade(const struct CSTRK_OBJECT *object, legacy_s16 x, legacy_s16 z,
								   legacy_s16 *result)
{
	if (!supersight_enabled) {
		return 0;
	}
	*result = extended_geometry_object_grade(object, x, z);
	return 1;
}
void hires_track_select_terrain_shape(const struct TRACKOBJECT *definition, legacy_s8 detail,
									  const struct SHAPE3D **source)
{
	if (supersight_enabled) {
		*source = frame_track_shape(definition, detail);
	}
}
legacy_u8 hires_track_native_support(const struct FRAME_EXTENDED_TERRAIN *terrain)
{
	return supersight_enabled && !frame_extended_uses_grade(terrain->object);
}
legacy_s16 hires_track_queue_exhausted(legacy_s16 result)
{
	return result > 0 && supersight_enabled != 0;
}

#endif
