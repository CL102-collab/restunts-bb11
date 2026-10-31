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

legacy_u8 frame_extended_native_grass(const struct CSTRK_CELL *cell)
{
	return cell->surface == CSTRK_TERRAIN_GRASS && cell->half_water_corner == CSTRK_WATER_NONE;
}

legacy_u8 frame_extended_object_sloped(const struct CSTRK_OBJECT *object)
{
	return object && (object->slope_x || object->slope_y);
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

#if defined(RESTUNTS_SDL3)

#endif
