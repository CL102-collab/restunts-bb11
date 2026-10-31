#include "extended_frame_internal.h"

legacy_s32 frame_extended_shore_distance(const struct VECTOR *point, legacy_u8 corner)
{
	switch (corner) {
		case CSTRK_WATER_NORTHWEST:
			return (legacy_s32)point->z - point->x;
		case CSTRK_WATER_NORTHEAST:
			return (legacy_s32)point->x + point->z;
		case CSTRK_WATER_SOUTHEAST:
			return (legacy_s32)point->x - point->z;
		default:
			return -(legacy_s32)point->x - point->z;
	}
}

void frame_extended_append_terrain_vertex(struct VECTOR *output, legacy_u16 *count,
										  const struct VECTOR *point)
{
	if (*count == 0 || output[*count - 1U].x != point->x || output[*count - 1U].y != point->y ||
		output[*count - 1U].z != point->z) {
		output[(*count)++] = *point;
	}
}

/* Clip paint boundaries after choosing the native hill mesh. The same vertices
 * and collision surface are retained regardless of the selected Shore corner. */
legacy_u16 frame_extended_clip_terrain(const struct VECTOR *source, legacy_u16 count,
									   struct VECTOR *output, legacy_u8 corner, legacy_u8 secondary)
{
	legacy_u16 written = 0;
	struct VECTOR previous = source[count - 1U];
	legacy_s32 previous_distance = frame_extended_shore_distance(&previous, corner);
	if (!secondary) {
		previous_distance = -previous_distance;
	}
	for (legacy_u16 index = 0; index < count; index++) {
		struct VECTOR current = source[index];
		legacy_s32 distance = frame_extended_shore_distance(&current, corner);
		if (!secondary) {
			distance = -distance;
		}
		if ((distance >= 0) != (previous_distance >= 0)) {
			legacy_s32 divisor = previous_distance - distance;
			/* A symmetric rational expression rounds both painted halves to
			 * exactly the same word-sized point, even on a half-unit crossing. */
			struct VECTOR crossing = {(legacy_s16)(((legacy_s32)current.x * previous_distance -
													(legacy_s32)previous.x * distance) /
												   divisor),
									  (legacy_s16)(((legacy_s32)current.y * previous_distance -
													(legacy_s32)previous.y * distance) /
												   divisor),
									  (legacy_s16)(((legacy_s32)current.z * previous_distance -
													(legacy_s32)previous.z * distance) /
												   divisor)};
			frame_extended_append_terrain_vertex(output, &written, &crossing);
		}
		if (distance >= 0) {
			frame_extended_append_terrain_vertex(output, &written, &current);
		}
		previous = current;
		previous_distance = distance;
	}
	if (written > 1U && output[0].x == output[written - 1U].x &&
		output[0].y == output[written - 1U].y && output[0].z == output[written - 1U].z) {
		written--;
	}
	return written;
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

legacy_s16 frame_extended_paint_terrain_polygon(const struct VECTOR *points, legacy_u16 count,
												legacy_u8 primitive_flags, legacy_u8 grass_material,
												const struct CSTRK_CELL *cell,
												const struct VECTOR *position, legacy_s8 flags)
{
	static const legacy_u8 materials[] = {FRAME_EXTENDED_GROUND_GRASS_MATERIAL,
										  FRAME_EXTENDED_ICE_MATERIAL, FRAME_EXTENDED_DIRT_MATERIAL,
										  FRAME_EXTENDED_WATER_MATERIAL};
	for (legacy_u8 half = 0; half <= (cell->half_water_corner != CSTRK_WATER_NONE); half++) {
		struct VECTOR clipped[SHAPE3D_POLYGON_MAX_VERTICES + 1U];
		const struct VECTOR *polygon = points;
		legacy_u16 polygon_count = count;
		legacy_u8 surface = half ? cell->corner_surface : cell->surface;
		legacy_u8 material = surface == CSTRK_TERRAIN_GRASS ? grass_material : materials[surface];
		if (cell->half_water_corner != CSTRK_WATER_NONE) {
			polygon_count =
				frame_extended_clip_terrain(points, count, clipped, cell->half_water_corner, half);
			polygon = clipped;
		}
		if (frame_extended_queue_terrain_polygon(polygon, polygon_count, primitive_flags, material,
												 position, flags)) {
			return 1;
		}
	}
	return 0;
}

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

legacy_u8 frame_extended_native_grass(const struct CSTRK_CELL *cell)
{
	return cell->surface == CSTRK_TERRAIN_GRASS && cell->half_water_corner == CSTRK_WATER_NONE;
}

legacy_u8 frame_extended_object_sloped(const struct CSTRK_OBJECT *object)
{
	return object && (object->slope_x || object->slope_y);
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

legacy_s64 frame_extended_ground_side(const struct VECTOR *a, const struct VECTOR *b,
									  const struct VECTOR *point)
{
	return (legacy_s64)(b->x - a->x) * (point->z - a->z) -
		   (legacy_s64)(b->z - a->z) * (point->x - a->x);
}

legacy_u16 frame_extended_clip_ground(const struct VECTOR *points, legacy_u16 count,
									  struct VECTOR *output, const struct VECTOR *a,
									  const struct VECTOR *b, legacy_s16 orientation)
{
	if (!count) {
		return 0;
	}
	legacy_u16 written = 0;
	struct VECTOR previous = points[count - 1U];
	legacy_s64 previous_side = frame_extended_ground_side(a, b, &previous) * orientation;
	for (legacy_u16 vertex = 0; vertex < count; vertex++) {
		struct VECTOR current = points[vertex];
		legacy_s64 side = frame_extended_ground_side(a, b, &current) * orientation;
		if ((side >= 0) != (previous_side >= 0)) {
			legacy_s64 divisor = previous_side - side;
			struct VECTOR crossing = {(legacy_s16)(((legacy_s64)current.x * previous_side -
													(legacy_s64)previous.x * side) /
												   divisor),
									  (legacy_s16)(((legacy_s64)current.y * previous_side -
													(legacy_s64)previous.y * side) /
												   divisor),
									  (legacy_s16)(((legacy_s64)current.z * previous_side -
													(legacy_s64)previous.z * side) /
												   divisor)};
			frame_extended_append_terrain_vertex(output, &written, &crossing);
		}
		if (side >= 0) {
			frame_extended_append_terrain_vertex(output, &written, &current);
		}
		previous = current;
		previous_side = side;
	}
	if (written > 1U && memcmp(&output[0], &output[written - 1U], sizeof(output[0])) == 0) {
		written--;
	}
	return written;
}

legacy_s16 frame_extended_append_ground_fragments(struct FRAME_EXTENDED_GROUND_FRAGMENT **fragments,
												  legacy_u16 *written, legacy_u16 *capacity,
												  const struct VECTOR *points, legacy_u16 count)
{
	if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST) {
		return 1;
	}
	if (count > SHAPE3D_POLYGON_MAX_VERTICES ||
		(!supersight_enabled && count > OWOOT_ROAD_TRIANGLE_VERTEX_COUNT)) {
		for (legacy_u16 vertex = 1; vertex + 1U < count; vertex++) {
			struct VECTOR triangle[] = {points[0], points[vertex], points[vertex + 1U]};
			if (!frame_extended_append_ground_fragments(fragments, written, capacity, triangle,
														OWOOT_ROAD_TRIANGLE_VERTEX_COUNT)) {
				return 0;
			}
		}
		return 1;
	}
	legacy_s64 area = 0;
	for (legacy_u16 vertex = 1; vertex + 1U < count; vertex++) {
		area += frame_extended_ground_side(&points[0], &points[vertex], &points[vertex + 1U]);
	}
	if (!area) {
		return 1;
	}
	if (!supersight_enabled && *written == FRAME_EXTENDED_CLASSIC_GROUND_FRAGMENT_MAX) {
		return 0;
	}
	if (*written == *capacity) {
		if (*capacity == LEGACY_U16_MAX) {
			return 0;
		}
		legacy_u32 next_capacity = *capacity ? (legacy_u32)*capacity * 2U
											 : FRAME_EXTENDED_GROUND_FRAGMENT_INITIAL_CAPACITY;
		if (next_capacity > LEGACY_U16_MAX) {
			next_capacity = LEGACY_U16_MAX;
		}
		struct FRAME_EXTENDED_GROUND_FRAGMENT *grown =
			realloc(*fragments, next_capacity * sizeof(**fragments));
		if (!grown) {
			return 0;
		}
		*fragments = grown;
		*capacity = (legacy_u16)next_capacity;
	}
	/* HyperVision keeps convex remainders intact until final submission.
	 * Premature fan triangulation cuts new internal edges repeatedly on curves.
	 * The classic path retains its original triangulation and fragment budget. */
	(*fragments)[*written].count = count;
	memcpy((*fragments)[*written].points, points, count * sizeof(points[0]));
	(*written)++;
	return 1;
}

#if defined(RESTUNTS_SDL3)
/* Free footprint corners and neighboring cells can lie below a road's support
 * plane. Its projected footprint must not remove that lower terrain. Match each
 * face separately so the compatible part of an inner hill corner can still be
 * cut, allowing the native mesh's small authored edge overlap. */
legacy_u8 frame_extended_terrain_matches_grade(const struct VECTOR *points, legacy_u16 count,
											   const struct FRAME_EXTENDED_TERRAIN *terrain,
											   const struct CSTRK_OBJECT *object)
{
	for (legacy_u16 vertex = 0; vertex < count; vertex++) {
		legacy_s32 x = ((legacy_s16)terrain->column - object->x) * CSTRK_TILE_WORLD_SIZE +
					   FRAME_EXTENDED_TERRAIN_HALF_TILE + points[vertex].x;
		legacy_s32 y = ((legacy_s16)terrain->row - object->y) * CSTRK_TILE_WORLD_SIZE +
					   FRAME_EXTENDED_TERRAIN_HALF_TILE - points[vertex].z;
		legacy_f64 road_height = object->base_height * CSTRK_LEVEL_WORLD_HEIGHT +
								 (legacy_f64)(object->slope_x * x + object->slope_y * y) *
									 CSTRK_LEVEL_WORLD_HEIGHT / CSTRK_TILE_WORLD_SIZE;
		legacy_f64 difference = terrain->profile.base_height * CSTRK_LEVEL_WORLD_HEIGHT +
								points[vertex].y - road_height;
		if (difference < -FRAME_EXTENDED_HILL_EDGE_OVERLAP ||
			difference > FRAME_EXTENDED_HILL_EDGE_OVERLAP) {
			return 0;
		}
	}
	return 1;
}
#endif

/* Remove only ground-height driving faces. The same authored road triangles
 * define flat turns, branches, straight roads and stunt approaches; elevated
 * faces and scenery leave the supporting terrain intact. */
legacy_s16 frame_extended_paint_terrain_face(const struct VECTOR *points, legacy_u16 count,
											 const legacy_u8 *primitive,
											 const struct FRAME_EXTENDED_TERRAIN *terrain)
{
#if defined(RESTUNTS_SDL3)
	if (supersight_enabled && terrain->graded) {
		return frame_extended_paint_precise_terrain_face(points, count, primitive, terrain);
	}
#endif
	if (!terrain->graded) {
		return frame_extended_paint_terrain_polygon(points, count, primitive[1], primitive[2],
													terrain->cell, &terrain->position,
													terrain->flags);
	}
	struct FRAME_EXTENDED_GROUND_FRAGMENT *current = NULL, *next = NULL;
	legacy_u16 remaining = 0, current_capacity = 0, next_capacity = 0;
	if (!frame_extended_append_ground_fragments(&current, &remaining, &current_capacity, points,
												count)) {
		free(current);
		return frame_extended_paint_terrain_polygon(points, count, primitive[1], primitive[2],
													terrain->cell, &terrain->position,
													terrain->flags);
	}
	legacy_s16 complete = 1;
	for (legacy_u16 object_index = 0; object_index < terrain->object_count && complete;
		 object_index++) {
		const struct CSTRK_OBJECT *object = terrain->objects[object_index];
		if (object->roll_ramp || !frame_extended_uses_grade(object)) {
			continue;
		}
		const struct TRACKOBJECT *definition = &trkObjectList[extended_track_object_id(object)];
		legacy_u16 triangle_count = track_road_triangle_count(definition->ss_physicalModel);
		for (legacy_u16 triangle_index = 0; triangle_index < triangle_count && remaining;
			 triangle_index++) {
			struct VECTOR triangle[OWOOT_ROAD_TRIANGLE_VERTEX_COUNT];
			track_road_triangle(definition->ss_physicalModel, triangle_index, triangle);
			if (triangle[0].y || triangle[1].y || triangle[2].y) {
				continue;
			}
			for (legacy_u16 vertex = 0; vertex < OWOOT_ROAD_TRIANGLE_VERTEX_COUNT; vertex++) {
				frame_extended_rotate_vertex(&triangle[vertex], definition->ss_rotY);
				triangle[vertex].x +=
					((legacy_s16)object->x - terrain->column) * CSTRK_TILE_WORLD_SIZE +
					(object->width - 1U) * FRAME_EXTENDED_TERRAIN_HALF_TILE;
				triangle[vertex].z +=
					(terrain->row - (legacy_s16)object->y) * CSTRK_TILE_WORLD_SIZE -
					(object->height - 1U) * FRAME_EXTENDED_TERRAIN_HALF_TILE;
			}
			legacy_s64 winding =
				frame_extended_ground_side(&triangle[0], &triangle[1], &triangle[2]);
			if (!winding) {
				continue;
			}
			legacy_s16 orientation = winding > 0 ? 1 : -1;
			legacy_u16 written = 0;
			for (legacy_u16 fragment = 0; fragment < remaining && complete; fragment++) {
				struct VECTOR inside[FRAME_EXTENDED_GROUND_CLIPPED_VERTEX_MAX];
				legacy_u16 inside_count = current[fragment].count;
				memcpy(inside, current[fragment].points, inside_count * sizeof(inside[0]));
				for (legacy_u16 edge = 0; edge < OWOOT_ROAD_TRIANGLE_VERTEX_COUNT && inside_count;
					 edge++) {
					struct VECTOR outside[FRAME_EXTENDED_GROUND_CLIPPED_VERTEX_MAX];
					struct VECTOR clipped[FRAME_EXTENDED_GROUND_CLIPPED_VERTEX_MAX];
					const struct VECTOR *a = &triangle[edge];
					const struct VECTOR *b =
						&triangle[(edge + 1U) % OWOOT_ROAD_TRIANGLE_VERTEX_COUNT];
					legacy_u16 outside_count = frame_extended_clip_ground(
						inside, inside_count, outside, a, b, -orientation);
					if (!frame_extended_append_ground_fragments(&next, &written, &next_capacity,
																outside, outside_count)) {
						complete = 0;
						break;
					}
					inside_count = frame_extended_clip_ground(inside, inside_count, clipped, a, b,
															  orientation);
					memcpy(inside, clipped, inside_count * sizeof(inside[0]));
				}
			}
			if (!complete) {
				break;
			}
			struct FRAME_EXTENDED_GROUND_FRAGMENT *swap = current;
			current = next;
			next = swap;
			legacy_u16 swap_capacity = current_capacity;
			current_capacity = next_capacity;
			next_capacity = swap_capacity;
			remaining = written;
		}
	}
	legacy_s16 result = 0;
	if (complete) {
		for (legacy_u16 fragment = 0; fragment < remaining; fragment++) {
			if (frame_extended_paint_terrain_polygon(
					current[fragment].points, current[fragment].count, primitive[1], primitive[2],
					terrain->cell, &terrain->position, terrain->flags)) {
				result = 1;
				break;
			}
		}
	} else {
		result =
			frame_extended_paint_terrain_polygon(points, count, primitive[1], primitive[2],
												 terrain->cell, &terrain->position, terrain->flags);
	}
	free(current);
	free(next);
	return result;
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
