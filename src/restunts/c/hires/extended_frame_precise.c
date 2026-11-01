#ifdef RESTUNTS_SDL3
#include "../extended/extended_frame_internal.h"
#include <stdlib.h>
#include <string.h>

#if defined(RESTUNTS_SDL3)
void frame_extended_append_precise_vertex(struct SHAPE3D_HIRES_VECTOR *output, legacy_u16 *count,
										  const struct SHAPE3D_HIRES_VECTOR *point)
{
	if (!*count || output[*count - 1U].x != point->x || output[*count - 1U].y != point->y ||
		output[*count - 1U].z != point->z) {
		output[(*count)++] = *point;
	}
}
#endif

#if defined(RESTUNTS_SDL3)
legacy_f64 frame_extended_precise_shore_distance(const struct SHAPE3D_HIRES_VECTOR *point,
												 legacy_u8 corner)
{
	switch (corner) {
		case CSTRK_WATER_NORTHWEST:
			return point->z - point->x;
		case CSTRK_WATER_NORTHEAST:
			return point->x + point->z;
		case CSTRK_WATER_SOUTHEAST:
			return point->x - point->z;
		default:
			return -point->x - point->z;
	}
}
#endif

#if defined(RESTUNTS_SDL3)
legacy_u16 frame_extended_clip_precise_terrain(const struct SHAPE3D_HIRES_VECTOR *source,
											   legacy_u16 count,
											   struct SHAPE3D_HIRES_VECTOR *output,
											   legacy_u8 corner, legacy_u8 secondary)
{
	legacy_u16 written = 0;
	struct SHAPE3D_HIRES_VECTOR previous = source[count - 1U];
	legacy_f64 previous_distance = frame_extended_precise_shore_distance(&previous, corner);
	if (!secondary) {
		previous_distance = -previous_distance;
	}
	for (legacy_u16 index = 0; index < count; index++) {
		struct SHAPE3D_HIRES_VECTOR current = source[index];
		legacy_f64 distance = frame_extended_precise_shore_distance(&current, corner);
		if (!secondary) {
			distance = -distance;
		}
		if ((distance >= 0) != (previous_distance >= 0)) {
			legacy_f64 divisor = previous_distance - distance;
			/* Use the same intersection for the two painted halves. */
			struct SHAPE3D_HIRES_VECTOR crossing = {
				((current.x * previous_distance - previous.x * distance) / divisor),
				((current.y * previous_distance - previous.y * distance) / divisor),
				((current.z * previous_distance - previous.z * distance) / divisor)};
			frame_extended_append_precise_vertex(output, &written, &crossing);
		}
		if (distance >= 0) {
			frame_extended_append_precise_vertex(output, &written, &current);
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
#endif

#if defined(RESTUNTS_SDL3)
legacy_s16 frame_extended_queue_precise_terrain(const struct SHAPE3D_HIRES_VECTOR *points,
												legacy_u16 count, legacy_u8 primitive_flags,
												legacy_u8 material, const struct VECTOR *position,
												legacy_s8 flags)
{
	if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST) {
		return 0;
	}
	if (count > SHAPE3D_POLYGON_MAX_VERTICES) {
		for (legacy_u16 index = 1; index + 1U < count; index++) {
			struct SHAPE3D_HIRES_VECTOR triangle[] = {points[0], points[index], points[index + 1U]};
			if (frame_extended_queue_precise_terrain(triangle, SHAPE3D_PRIMITIVE_POLYGON_FIRST,
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
		struct VECTOR fallback = {(legacy_s16)points[index].x, (legacy_s16)points[index].y,
								  (legacy_s16)points[index].z};
		shape3d_vertex_write(&shape, index, &fallback);
		primitive[index + 3U] = (legacy_u8)index;
	}
	primitive[count + 3U] = 0;
	struct TRANSFORMEDSHAPE3D instance = {0};
	instance.pos = *position;
	instance.shapeptr = &shape;
	instance.rectptr = &frame_sorted_shapes_rect;
	instance.culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
	instance.ts_flags = flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
	return (legacy_s16)shape3d_hires_transform_and_queue(&instance, points) > 0 &&
		   supersight_enabled != 0;
}
#endif

#if defined(RESTUNTS_SDL3)
legacy_s16 frame_extended_paint_precise_terrain(const struct SHAPE3D_HIRES_VECTOR *points,
												legacy_u16 count, legacy_u8 primitive_flags,
												legacy_u8 grass_material,
												const struct CSTRK_CELL *cell,
												const struct VECTOR *position, legacy_s8 flags)
{
	static const legacy_u8 materials[] = {FRAME_EXTENDED_GROUND_GRASS_MATERIAL,
										  FRAME_EXTENDED_ICE_MATERIAL, FRAME_EXTENDED_DIRT_MATERIAL,
										  FRAME_EXTENDED_WATER_MATERIAL};
	for (legacy_u8 half = 0; half <= (cell->half_water_corner != CSTRK_WATER_NONE); half++) {
		struct SHAPE3D_HIRES_VECTOR clipped[SHAPE3D_POLYGON_MAX_VERTICES + 1U];
		const struct SHAPE3D_HIRES_VECTOR *polygon = points;
		legacy_u16 polygon_count = count;
		legacy_u8 surface = half ? cell->corner_surface : cell->surface;
		legacy_u8 material = surface == CSTRK_TERRAIN_GRASS ? grass_material : materials[surface];
		if (cell->half_water_corner != CSTRK_WATER_NONE) {
			polygon_count = frame_extended_clip_precise_terrain(points, count, clipped,
																cell->half_water_corner, half);
			polygon = clipped;
		}
		if (frame_extended_queue_precise_terrain(polygon, polygon_count, primitive_flags, material,
												 position, flags)) {
			return 1;
		}
	}
	return 0;
}
#endif

#if defined(RESTUNTS_SDL3)
legacy_f64 frame_extended_precise_ground_side(const struct SHAPE3D_HIRES_VECTOR *a,
											  const struct SHAPE3D_HIRES_VECTOR *b,
											  const struct SHAPE3D_HIRES_VECTOR *point)
{
	return (legacy_f64)(b->x - a->x) * (point->z - a->z) -
		   (legacy_f64)(b->z - a->z) * (point->x - a->x);
}
#endif

#if defined(RESTUNTS_SDL3)
legacy_u16 frame_extended_clip_precise_ground(const struct SHAPE3D_HIRES_VECTOR *points,
											  legacy_u16 count, struct SHAPE3D_HIRES_VECTOR *output,
											  const struct SHAPE3D_HIRES_VECTOR *a,
											  const struct SHAPE3D_HIRES_VECTOR *b,
											  legacy_s16 orientation)
{
	if (!count) {
		return 0;
	}
	legacy_u16 written = 0;
	struct SHAPE3D_HIRES_VECTOR previous = points[count - 1U];
	legacy_f64 previous_side = frame_extended_precise_ground_side(a, b, &previous) * orientation;
	for (legacy_u16 vertex = 0; vertex < count; vertex++) {
		struct SHAPE3D_HIRES_VECTOR current = points[vertex];
		legacy_f64 side = frame_extended_precise_ground_side(a, b, &current) * orientation;
		if ((side >= 0) != (previous_side >= 0)) {
			legacy_f64 divisor = previous_side - side;
			struct SHAPE3D_HIRES_VECTOR crossing = {
				(((legacy_f64)current.x * previous_side - (legacy_f64)previous.x * side) / divisor),
				(((legacy_f64)current.y * previous_side - (legacy_f64)previous.y * side) / divisor),
				(((legacy_f64)current.z * previous_side - (legacy_f64)previous.z * side) /
				 divisor)};
			frame_extended_append_precise_vertex(output, &written, &crossing);
		}
		if (side >= 0) {
			frame_extended_append_precise_vertex(output, &written, &current);
		}
		previous = current;
		previous_side = side;
	}
	if (written > 1U && memcmp(&output[0], &output[written - 1U], sizeof(output[0])) == 0) {
		written--;
	}
	return written;
}
#endif

#if defined(RESTUNTS_SDL3)
legacy_s16
frame_extended_append_precise_fragments(struct FRAME_EXTENDED_PRECISE_GROUND_FRAGMENT **fragments,
										legacy_u16 *written, legacy_u16 *capacity,
										const struct SHAPE3D_HIRES_VECTOR *points, legacy_u16 count)
{
	if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST) {
		return 1;
	}
	if (count > SHAPE3D_POLYGON_MAX_VERTICES) {
		for (legacy_u16 vertex = 1; vertex + 1U < count; vertex++) {
			struct SHAPE3D_HIRES_VECTOR triangle[] = {points[0], points[vertex],
													  points[vertex + 1U]};
			if (!frame_extended_append_precise_fragments(fragments, written, capacity, triangle,
														 OWOOT_ROAD_TRIANGLE_VERTEX_COUNT)) {
				return 0;
			}
		}
		return 1;
	}
	legacy_f64 area = 0;
	for (legacy_u16 vertex = 1; vertex + 1U < count; vertex++) {
		area +=
			frame_extended_precise_ground_side(&points[0], &points[vertex], &points[vertex + 1U]);
	}
	if (area > -FRAME_EXTENDED_GROUND_AREA_EPSILON && area < FRAME_EXTENDED_GROUND_AREA_EPSILON) {
		return 1;
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
		struct FRAME_EXTENDED_PRECISE_GROUND_FRAGMENT *grown =
			realloc(*fragments, next_capacity * sizeof(**fragments));
		if (!grown) {
			return 0;
		}
		*fragments = grown;
		*capacity = (legacy_u16)next_capacity;
	}
	(*fragments)[*written].count = count;
	memcpy((*fragments)[*written].points, points, count * sizeof(points[0]));
	(*written)++;
	return 1;
}
#endif

#if defined(RESTUNTS_SDL3)
legacy_s16 frame_extended_paint_precise_terrain_face(const struct VECTOR *source, legacy_u16 count,
													 const legacy_u8 *primitive,
													 const struct FRAME_EXTENDED_TERRAIN *terrain)
{
	struct SHAPE3D_HIRES_VECTOR points[SHAPE3D_POLYGON_MAX_VERTICES];
	for (legacy_u16 vertex = 0; vertex < count; vertex++) {
		points[vertex] =
			(struct SHAPE3D_HIRES_VECTOR){source[vertex].x, source[vertex].y, source[vertex].z};
	}
	struct FRAME_EXTENDED_PRECISE_GROUND_FRAGMENT *current = NULL, *next = NULL;
	legacy_u16 remaining = 0, current_capacity = 0, next_capacity = 0;
	if (!frame_extended_append_precise_fragments(&current, &remaining, &current_capacity, points,
												 count)) {
		free(current);
		return frame_extended_paint_precise_terrain(points, count, primitive[1], primitive[2],
													terrain->cell, &terrain->position,
													terrain->flags);
	}
	legacy_s16 complete = 1;
	for (legacy_u16 object_index = 0; object_index < terrain->object_count && complete;
		 object_index++) {
		const struct CSTRK_OBJECT *object = terrain->objects[object_index];
		if (object->roll_ramp || !frame_extended_uses_grade(object) ||
			!frame_extended_terrain_matches_grade(source, count, terrain, object)) {
			continue;
		}
		const struct TRACKOBJECT *definition = &trkObjectList[extended_track_object_id(object)];
		legacy_u16 triangle_count = track_road_triangle_count(definition->ss_physicalModel);
		for (legacy_u16 triangle_index = 0; triangle_index < triangle_count && remaining;
			 triangle_index++) {
			struct SHAPE3D_HIRES_VECTOR triangle[OWOOT_ROAD_TRIANGLE_VERTEX_COUNT];
			struct VECTOR authored[OWOOT_ROAD_TRIANGLE_VERTEX_COUNT];
			track_road_triangle(definition->ss_physicalModel, triangle_index, authored);
			for (legacy_u16 vertex = 0; vertex < OWOOT_ROAD_TRIANGLE_VERTEX_COUNT; vertex++) {
				frame_extended_rotate_vertex(&authored[vertex], definition->ss_rotY);
				frame_extended_join_object_grade_vertex(&authored[vertex], object);
				triangle[vertex] = (struct SHAPE3D_HIRES_VECTOR){
					authored[vertex].x, authored[vertex].y, authored[vertex].z};
			}
			if (triangle[0].y || triangle[1].y || triangle[2].y) {
				continue;
			}
			for (legacy_u16 vertex = 0; vertex < OWOOT_ROAD_TRIANGLE_VERTEX_COUNT; vertex++) {
				triangle[vertex].x +=
					((legacy_s16)object->x - terrain->column) * CSTRK_TILE_WORLD_SIZE +
					((legacy_s16)object->width - 1) * FRAME_EXTENDED_TERRAIN_HALF_TILE;
				triangle[vertex].z +=
					(terrain->row - (legacy_s16)object->y) * CSTRK_TILE_WORLD_SIZE -
					((legacy_s16)object->height - 1) * FRAME_EXTENDED_TERRAIN_HALF_TILE;
			}
			legacy_f64 winding =
				frame_extended_precise_ground_side(&triangle[0], &triangle[1], &triangle[2]);
			if (!winding) {
				continue;
			}
			legacy_s16 orientation = winding > 0 ? 1 : -1;
			legacy_u16 written = 0;
			for (legacy_u16 fragment = 0; fragment < remaining && complete; fragment++) {
				struct SHAPE3D_HIRES_VECTOR inside[FRAME_EXTENDED_GROUND_CLIPPED_VERTEX_MAX];
				legacy_u16 inside_count = current[fragment].count;
				memcpy(inside, current[fragment].points, inside_count * sizeof(inside[0]));
				for (legacy_u16 edge = 0; edge < OWOOT_ROAD_TRIANGLE_VERTEX_COUNT && inside_count;
					 edge++) {
					struct SHAPE3D_HIRES_VECTOR outside[FRAME_EXTENDED_GROUND_CLIPPED_VERTEX_MAX];
					struct SHAPE3D_HIRES_VECTOR clipped[FRAME_EXTENDED_GROUND_CLIPPED_VERTEX_MAX];
					const struct SHAPE3D_HIRES_VECTOR *a = &triangle[edge];
					const struct SHAPE3D_HIRES_VECTOR *b =
						&triangle[(edge + 1U) % OWOOT_ROAD_TRIANGLE_VERTEX_COUNT];
					legacy_u16 outside_count = frame_extended_clip_precise_ground(
						inside, inside_count, outside, a, b, -orientation);
					if (!frame_extended_append_precise_fragments(&next, &written, &next_capacity,
																 outside, outside_count)) {
						complete = 0;
						break;
					}
					inside_count = frame_extended_clip_precise_ground(inside, inside_count, clipped,
																	  a, b, orientation);
					memcpy(inside, clipped, inside_count * sizeof(inside[0]));
				}
			}
			if (!complete) {
				break;
			}
			struct FRAME_EXTENDED_PRECISE_GROUND_FRAGMENT *swap = current;
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
			if (frame_extended_paint_precise_terrain(
					current[fragment].points, current[fragment].count, primitive[1], primitive[2],
					terrain->cell, &terrain->position, terrain->flags)) {
				result = 1;
				break;
			}
		}
	} else {
		result =
			frame_extended_paint_precise_terrain(points, count, primitive[1], primitive[2],
												 terrain->cell, &terrain->position, terrain->flags);
	}
	free(current);
	free(next);
	return result;
}
#endif

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

#endif
