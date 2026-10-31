#ifdef RESTUNTS_SDL3
#include "../extended/extended_frame_internal.h"
#include <stdlib.h>
#include <string.h>

/* The corrected native-road classification belongs to HyperVision rendering.
 * Classic frames retain their historical treatment of the two road directions
 * which have no separate catalog spelling. Physics uses the common predicate. */
legacy_u8 frame_extended_uses_grade(const struct CSTRK_OBJECT *object)
{
	if (!supersight_enabled && object && (object->slope_x || object->slope_y) &&
		object->width == 1U && object->height == 1U &&
		trkObjectList[object->legacy_id].ss_physicalModel == PHYSICAL_MODEL_ROAD) {
		legacy_u16 angle = ((legacy_u16)trkObjectList[object->legacy_id].ss_rotY +
							object->rotation * ANGLE_QUARTER_TURN) &
						   ANGLE_MASK;
		if (angle & ANGLE_HALF_TURN) {
			return 1;
		}
	}
	return extended_geometry_uses_grade(object);
}

legacy_s16 frame_extended_object_grade(const struct CSTRK_OBJECT *object, legacy_s16 local_x,
									   legacy_s16 local_z)
{
	if (supersight_enabled) {
		return extended_geometry_object_grade(object, local_x, local_z);
	}
	return frame_extended_uses_grade(object)
			   ? extended_geometry_support_grade(object, local_x, local_z)
			   : 0;
}

void frame_extended_collect_terrain_objects(struct FRAME_EXTENDED_TERRAIN *terrain)
{
	terrain->object_count = extended_track_objects_in_cell(
		terrain->column, terrain->row, terrain->objects, EXTENDED_TRACK_CELL_OBJECT_MAX);
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

void frame_extended_join_low_half(struct VECTOR *points, legacy_u16 count,
								  const struct FRAME_EXTENDED_TERRAIN *terrain, legacy_s8 detail)
{
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

legacy_s16 frame_extended_draw_supporting_ground(const struct FRAME_EXTENDED_TERRAIN *terrain,
												 legacy_s8 detail)
{
	const struct CSTRK_TERRAIN_PROFILE *profile = &terrain->profile;
	if (profile->terrain_id != 0 && (profile->terrain_id < FRAME_EXTENDED_OUTER_CORNER_FIRST ||
									 profile->terrain_id > FRAME_EXTENDED_OUTER_CORNER_LAST)) {
		return 0;
	}
	if (profile->base_height == 0 && frame_extended_native_grass(terrain->cell)) {
		return 0;
	}
	/* Only the low half of an outer corner needs a supporting fill.
	 * A full quad beneath a slope can overpaint it in the classic painter. */
	const struct VECTOR flat[] = {
		{-FRAME_EXTENDED_TERRAIN_HALF_TILE, 0, FRAME_EXTENDED_TERRAIN_HALF_TILE},
		{FRAME_EXTENDED_TERRAIN_HALF_TILE, 0, FRAME_EXTENDED_TERRAIN_HALF_TILE},
		{FRAME_EXTENDED_TERRAIN_HALF_TILE, 0, -FRAME_EXTENDED_TERRAIN_HALF_TILE},
		{-FRAME_EXTENDED_TERRAIN_HALF_TILE, 0, -FRAME_EXTENDED_TERRAIN_HALF_TILE}};
	struct VECTOR low_half[SHAPE3D_PRIMITIVE_POLYGON_FIRST];
	const struct VECTOR *points = flat;
	legacy_u16 count = sizeof(flat) / sizeof(flat[0]);
	if (profile->terrain_id != 0) {
		legacy_u8 high_corner = 0;
		while (!(profile->high_mask & (1U << high_corner))) {
			high_corner++;
		}
		count = sizeof(low_half) / sizeof(low_half[0]);
		for (legacy_u16 vertex = 0; vertex < count; vertex++) {
			low_half[vertex] =
				flat[(high_corner + vertex + 1U) % FRAME_EXTENDED_TERRAIN_CORNER_COUNT];
		}
		if (supersight_enabled) {
			/* The authored hill's diagonal can overhang the grid by two units.
			 * Share its exact endpoints instead of leaving a crack above this fill. */
			frame_extended_join_low_half(low_half, count, terrain, detail);
		}
		points = low_half;
	}
	legacy_u8 material = profile->base_height == 0 ? FRAME_EXTENDED_GROUND_GRASS_MATERIAL
												   : FRAME_EXTENDED_HILL_GRASS_MATERIAL;
	return frame_extended_paint_terrain_polygon(points, count,
												FRAME_EXTENDED_TERRAIN_ALWAYS_VISIBLE, material,
												terrain->cell, &terrain->position, terrain->flags);
}

legacy_s16 frame_extended_draw_terrain_mesh(const struct FRAME_EXTENDED_TERRAIN *terrain,
											legacy_s8 detail)
{
	const struct TRACKOBJECT *definition = &terrain_scene_objects[terrain->profile.terrain_id];
	const struct SHAPE3D *source =
		supersight_enabled != 0 ? frame_track_shape(definition, detail) : definition->ss_shapePtr;
	if (!terrain->graded && frame_extended_native_grass(terrain->cell)) {
		struct TRANSFORMEDSHAPE3D instance = {0};
		instance.pos = terrain->position;
		instance.shapeptr = (struct SHAPE3D *)source;
		instance.rectptr = terrain->profile.base_height == 0 ? &frame_unsorted_shapes_rect
															 : &frame_sorted_shapes_rect;
		instance.rotvec.z = definition->ss_rotY;
		instance.culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
		instance.ts_flags = terrain->flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT;
		return (legacy_s16)frame_extended_queue_hill_shape(&instance, terrain->edges) > 0 &&
			   supersight_enabled != 0;
	}
	const legacy_u8 *primitive = source->shape3d_primitives;
	for (legacy_u16 index = 0; index < source->shape3d_numprimitives; index++) {
		legacy_u16 count = primitive[0];
		if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST || count > SHAPE3D_PRIMITIVE_POLYGON_LAST) {
			break;
		}
		struct VECTOR points[SHAPE3D_POLYGON_MAX_VERTICES];
		for (legacy_u16 vertex = 0; vertex < count; vertex++) {
			shape3d_vertex_read(source, primitive[2U + source->shape3d_numpaints + vertex],
								&points[vertex]);
			frame_extended_rotate_vertex(&points[vertex], definition->ss_rotY);
			frame_extended_join_hill_vertex(&points[vertex], terrain->edges);
		}
		if (frame_extended_paint_terrain_face(points, count, primitive, terrain)) {
			return 1;
		}
		primitive += 2U + source->shape3d_numpaints + count;
	}
	return 0;
}

/* Original hill meshes retain their full tile span, corner diagonals, and
 * authored shading. Higher levels translate those meshes without tilting them. */
legacy_s16 extended_frame_draw_terrain(const struct FRAME_TILE *tile,
									   const struct FRAME_CAMERA *camera, legacy_s8 flags)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	struct FRAME_EXTENDED_TERRAIN terrain;
	if (!cstrk_terrain_profile(track, (legacy_u16)tile->east, (legacy_u16)tile->south,
							   &terrain.profile)) {
		return 0;
	}
	terrain.cell = &track->cells[(size_t)tile->south * track->width + tile->east];
	terrain.edges = frame_extended_hill_edges(track, tile->east, tile->south);
	terrain.position = (struct VECTOR){
		LEGACY_S16_WRAP_SUB(track_column_centers[tile->east], camera->position.x),
		LEGACY_S16_WRAP_SUB((legacy_s16)(terrain.profile.base_height * CSTRK_LEVEL_WORLD_HEIGHT),
							camera->position.y),
		LEGACY_S16_WRAP_SUB(track_row_centers[tile->south], camera->position.z)};
	terrain.column = tile->east;
	terrain.row = tile->south;
	terrain.object = extended_track_object_at(tile->east, tile->south);
	terrain.graded = 0;
	frame_extended_collect_terrain_objects(&terrain);
	for (legacy_u16 candidate = 0; candidate < terrain.object_count; candidate++) {
		if (frame_extended_uses_grade(terrain.objects[candidate])) {
			terrain.graded = 1;
			terrain.edges |= frame_extended_grade_edges(terrain.objects[candidate]);
		}
	}
	terrain.flags = flags;
	if (frame_extended_object_sloped(terrain.object) &&
		(!terrain.graded || (supersight_enabled && !frame_extended_uses_grade(terrain.object))) &&
		frame_extended_native_grass(terrain.cell)) {
		/* Native hill roads and spans already include their supporting grass. */
		return 0;
	}
	if (frame_extended_draw_supporting_ground(&terrain, tile->detail)) {
		return 1;
	}
	if (terrain.profile.terrain_id == 0) {
		return 0;
	}
	return frame_extended_draw_terrain_mesh(&terrain, tile->detail);
}

legacy_s16 frame_extended_queue_terrain_polygon(const struct VECTOR *points, legacy_u16 count,
												legacy_u8 primitive_flags, legacy_u8 material,
												const struct VECTOR *position, legacy_s8 flags)
{
	if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST) {
		return 0;
	}
	if (count > SHAPE3D_POLYGON_MAX_VERTICES) {
		for (legacy_u16 index = 1; index + 1U < count; index++) {
			struct VECTOR triangle[] = {points[0], points[index], points[index + 1U]};
			if (frame_extended_queue_terrain_polygon(triangle, SHAPE3D_PRIMITIVE_POLYGON_FIRST,
													 primitive_flags, material, position, flags)) {
				return 1;
			}
		}
		return 0;
	}
	legacy_u8 bytes[SHAPE3D_POLYGON_MAX_VERTICES * SHAPE3D_VERTEX_SIZE];
	legacy_u8 primitive[SHAPE3D_POLYGON_MAX_VERTICES + 5U] = {0};
	legacy_u8 masks[SHAPE3D_VISIBILITY_MASK_SIZE] = {255, 255, 255, 255};
	legacy_u8 front_masks[SHAPE3D_VISIBILITY_MASK_SIZE] = {0};
	struct SHAPE3D shape = {count, bytes, 1, 1, primitive, masks, front_masks};
	primitive[0] = (legacy_u8)count;
	primitive[1] = primitive_flags;
	primitive[2] = material;
	for (legacy_u16 index = 0; index < count; index++) {
		shape3d_vertex_write(&shape, index, &points[index]);
		primitive[index + 3U] = (legacy_u8)index;
	}
	primitive[count + 3U] = 0;
	struct TRANSFORMEDSHAPE3D instance = {0};
	instance.pos = *position;
	instance.shapeptr = &shape;
	instance.rectptr = &frame_sorted_shapes_rect;
	instance.culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
	instance.ts_flags = flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
	return (legacy_s16)shape3d_transform_and_queue(&instance) > 0 && supersight_enabled != 0;
}

#endif
