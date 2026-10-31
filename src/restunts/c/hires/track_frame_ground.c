#ifdef RESTUNTS_SDL3
#include "../extended/extended_frame_internal.h"
#include <stdlib.h>
#include <string.h>

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

#endif
