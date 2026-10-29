#include "extended_geometry.h"
#include "trackdata_layout.h"
#include "track_collision.h"
#include "gamestate.h"
#include <stdlib.h>
#include <string.h>

#define EXTENDED_GEOMETRY_TERMINATOR_BYTES 2U

static legacy_s32 context_x, context_z;

legacy_s32 extended_geometry_unwrap(legacy_s16 coordinate, legacy_s32 reference)
{
	/* The difference is intentionally a wrapped word: it describes a local
	 * displacement, while the reference identifies the correct 65536-unit page. */
	legacy_s16 reference_word = LEGACY_S16_FROM_BITS((legacy_u16)reference);
	return reference + LEGACY_S16_WRAP_SUB(coordinate, reference_word);
}

void extended_geometry_set_context(legacy_s32 fixed_x, legacy_s32 fixed_z)
{
	context_x = LEGACY_S32_SAR(fixed_x, 6U);
	context_z = LEGACY_S32_SAR(fixed_z, 6U);
}

void extended_geometry_get_context(legacy_s32 *world_x, legacy_s32 *world_z)
{
	*world_x = context_x;
	*world_z = context_z;
}

void extended_geometry_world_position(const struct VECTOR *wrapped, legacy_s32 *x, legacy_s32 *z)
{
	*x = extended_geometry_unwrap(wrapped->x, context_x);
	*z = extended_geometry_unwrap(wrapped->z, context_z);
}

void extended_geometry_object_origin(const struct CSTRK_TRACK *track,
									 const struct CSTRK_OBJECT *object, struct VECTOR *origin)
{
	origin->x = LEGACY_S16_FROM_BITS((legacy_u16)(object->x * 1024U + object->width * 512U));
	origin->z = LEGACY_S16_FROM_BITS(
		(legacy_u16)(track->height * 1024U - object->y * 1024U - object->height * 512U));
	legacy_s16 base_height = object->base_height;
	if (object->slope_x < 0) {
		base_height += object->slope_x * object->width;
	}
	if (object->slope_y < 0) {
		base_height += object->slope_y * object->height;
	}
	origin->y = base_height * CSTRK_LEVEL_WORLD_HEIGHT;
}

enum EXTENDED_TERRAIN_PLANE {
	EXTENDED_TERRAIN_FLAT_PLANE = 0,
	EXTENDED_TERRAIN_STRAIGHT_PLANE = 3,
	EXTENDED_TERRAIN_OUTER_PLANE = 4,
	EXTENDED_TERRAIN_INNER_PLANE = 5
};

#define EXTENDED_TERRAIN_ORIENTATION_COUNT 4U
#define EXTENDED_TERRAIN_HALF_TILE (CSTRK_TILE_WORLD_SIZE / 2)
#define EXTENDED_TERRAIN_CORNER_SPLIT_ANGLE (-ANGLE_EIGHTH_TURN)

static void geometry_native_terrain_plane(const struct CSTRK_TERRAIN_PROFILE *profile, legacy_s16 u,
										  legacy_s16 v, struct PLANE *plane)
{
	legacy_u8 terrain = profile->terrain_id;
	legacy_u16 index = EXTENDED_TERRAIN_FLAT_PLANE;
	legacy_s16 height = profile->base_height * CSTRK_LEVEL_WORLD_HEIGHT;
	if (terrain >= CSTRK_TERRAIN_PROFILE_NORTH) {
		legacy_u16 orientation =
			(terrain - CSTRK_TERRAIN_PROFILE_NORTH) % EXTENDED_TERRAIN_ORIENTATION_COUNT;
		legacy_s16 x = u - EXTENDED_TERRAIN_HALF_TILE;
		legacy_s16 z = EXTENDED_TERRAIN_HALF_TILE - v;
		legacy_s16 old_x = x;
		/* Native hill orientations are 0, three-quarter, half, quarter turns;
		 * their plane table offsets occur in exactly this terrain ID order. */
		switch (orientation) {
			case 1:
				x = z;
				z = LEGACY_S16_WRAP_NEGATE(old_x);
				break;
			case 2:
				x = LEGACY_S16_WRAP_NEGATE(x);
				z = LEGACY_S16_WRAP_NEGATE(z);
				break;
			case 3:
				x = LEGACY_S16_WRAP_NEGATE(z);
				z = old_x;
				break;
		}
		legacy_s16 side = LEGACY_S16_WRAP_ADD(
			multiply_and_scale(sin_fast((legacy_u16)EXTENDED_TERRAIN_CORNER_SPLIT_ANGLE), z),
			multiply_and_scale(cos_fast((legacy_u16)EXTENDED_TERRAIN_CORNER_SPLIT_ANGLE), x));
		legacy_u16 native_plane = EXTENDED_TERRAIN_FLAT_PLANE;
		if (terrain <= CSTRK_TERRAIN_PROFILE_EAST) {
			native_plane = EXTENDED_TERRAIN_STRAIGHT_PLANE;
		} else if (terrain <= CSTRK_TERRAIN_PROFILE_OUTER_NORTHEAST) {
			if (side < 0) {
				native_plane = EXTENDED_TERRAIN_OUTER_PLANE;
			}
		} else if (side > 0) {
			native_plane = EXTENDED_TERRAIN_INNER_PLANE;
		} else {
			height += CSTRK_LEVEL_WORLD_HEIGHT;
		}
		if (native_plane != EXTENDED_TERRAIN_FLAT_PLANE) {
			index = native_plane * EXTENDED_TERRAIN_ORIENTATION_COUNT + orientation;
		}
	}
	*plane = planptr[index];
	plane->plane_origin.y = LEGACY_S16_WRAP_ADD(plane->plane_origin.y, height);
}

int extended_geometry_terrain(const struct CSTRK_TRACK *track, legacy_s32 world_x,
							  legacy_s32 world_z, struct EXTENDED_TERRAIN_SAMPLE *sample)
{
	legacy_s32 map_y = (legacy_s32)track->height * 1024 - world_z;
	if (world_x < 0 || map_y < 0 || world_x >= (legacy_s32)track->width * 1024 ||
		map_y >= (legacy_s32)track->height * 1024) {
		return 0;
	}
	legacy_s16 column = (legacy_s16)(world_x / 1024);
	legacy_s16 row = (legacy_s16)(map_y / 1024);
	legacy_s16 u = (legacy_s16)(world_x % 1024), v = (legacy_s16)(map_y % 1024);
	struct CSTRK_TERRAIN_PROFILE profile;
	if (!cstrk_terrain_profile(track, (legacy_u16)column, (legacy_u16)row, &profile) || !planptr) {
		return 0;
	}
	const struct CSTRK_CELL *cell = &track->cells[(size_t)row * track->width + column];
	sample->column = column;
	sample->north_row = row;
	sample->origin.x = LEGACY_S16_FROM_BITS((legacy_u16)(column * 1024 + 512));
	sample->origin.z = LEGACY_S16_FROM_BITS((legacy_u16)(track->height * 1024 - row * 1024 - 512));
	sample->origin.y = 0;
	sample->terrain_id = profile.terrain_id;
	sample->base_height = profile.base_height;
	geometry_native_terrain_plane(&profile, u, v, &sample->plane);
	static const legacy_s16 surface_types[] = {CAR_SURFACE_GRASS, CAR_SURFACE_ICE, CAR_SURFACE_DIRT,
											   CAR_SURFACE_WATER};
	sample->surface = surface_types[cell->surface];
	/* The material boundary never selects or changes the hill's geometry. */
	if ((cell->half_water_corner == CSTRK_WATER_NORTHWEST && u + v < 1024) ||
		(cell->half_water_corner == CSTRK_WATER_NORTHEAST && v < u) ||
		(cell->half_water_corner == CSTRK_WATER_SOUTHEAST && u + v >= 1024) ||
		(cell->half_water_corner == CSTRK_WATER_SOUTHWEST && v >= u)) {
		sample->surface = surface_types[cell->corner_surface];
	}
	return 1;
}

int extended_geometry_transform_shape(const struct SHAPE3D *source, struct SHAPE3D *destination,
									  legacy_s16 orientation, legacy_u16 surface,
									  legacy_s16 physical_model)
{
	static const legacy_u8 counts[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 2, 6, 3, 0, 0};
	memset(destination, 0, sizeof(*destination));
	if (source == 0 || source->shape3d_numverts == 0 || source->shape3d_numpaints == 0) {
		return 0;
	}
	/* Scenery keeps its own paint and animation palette regardless of the
	 * selected road surface. */
	if (physical_model >= 64) {
		surface = CSTRK_ROAD_PAVED;
	}
	size_t primitive_bytes = EXTENDED_GEOMETRY_TERMINATOR_BYTES;
	const legacy_u8 *primitive = source->shape3d_primitives;
	for (unsigned i = 0; i < source->shape3d_numprimitives; i++) {
		if (primitive[0] >= 16 || counts[primitive[0]] == 0) {
			return 0;
		}
		primitive_bytes += 3U + counts[primitive[0]];
		primitive += 2U + source->shape3d_numpaints + counts[primitive[0]];
	}
	size_t vertex_bytes = (size_t)source->shape3d_numverts * SHAPE3D_VERTEX_SIZE;
	size_t mask_bytes = (size_t)source->shape3d_numprimitives * 4U;
	legacy_u8 *memory = malloc(vertex_bytes + primitive_bytes + mask_bytes * 2U);
	if (memory == 0) {
		return 0;
	}
	*destination = *source;
	destination->shape3d_vertex_bytes = memory;
	destination->shape3d_primitives = memory + vertex_bytes;
	destination->shape3d_visibility_masks = memory + vertex_bytes + primitive_bytes;
	destination->shape3d_front_facing_masks = memory + vertex_bytes + primitive_bytes + mask_bytes;
	destination->shape3d_numpaints = 1;
	memset(destination->shape3d_visibility_masks, 255, mask_bytes);
	memset(destination->shape3d_front_facing_masks, 0, mask_bytes);
	for (unsigned i = 0; i < source->shape3d_numverts; i++) {
		struct VECTOR local, rotated;
		const legacy_u8 *vertex = source->shape3d_vertex_bytes + (size_t)i * SHAPE3D_VERTEX_SIZE;
		local.x = LEGACY_READ_S16_LE(vertex);
		local.y = LEGACY_READ_S16_LE(vertex + 2);
		local.z = LEGACY_READ_S16_LE(vertex + 4);
		rotated = local;
		switch (orientation & ANGLE_MASK) {
			case ANGLE_QUARTER_TURN:
				rotated.x = local.z;
				rotated.z = LEGACY_S16_WRAP_NEGATE(local.x);
				break;
			case ANGLE_HALF_TURN:
				rotated.x = LEGACY_S16_WRAP_NEGATE(local.x);
				rotated.z = LEGACY_S16_WRAP_NEGATE(local.z);
				break;
			case ANGLE_THREE_QUARTER_TURN:
				rotated.x = LEGACY_S16_WRAP_NEGATE(local.z);
				rotated.z = local.x;
				break;
		}
		legacy_u8 *out_vertex = destination->shape3d_vertex_bytes + (size_t)i * SHAPE3D_VERTEX_SIZE;
		LEGACY_WRITE_U16_LE(out_vertex, (legacy_u16)rotated.x);
		LEGACY_WRITE_U16_LE(out_vertex + 2, (legacy_u16)rotated.y);
		LEGACY_WRITE_U16_LE(out_vertex + 4, (legacy_u16)rotated.z);
	}
	primitive = source->shape3d_primitives;
	legacy_u8 *out = destination->shape3d_primitives;
	for (unsigned i = 0; i < source->shape3d_numprimitives; i++) {
		unsigned count = counts[primitive[0]];
		unsigned paint = surface < source->shape3d_numpaints ? surface : 0;
		legacy_u8 material = primitive[2U + paint];
		if (surface != 0 && source->shape3d_numpaints < 3) {
			if (material == 19) {
				material = surface == CSTRK_ROAD_DIRT ? 25 : 28;
			} else if (material == 21) {
				material = surface == CSTRK_ROAD_DIRT ? 27 : 30;
			} else if (physical_model == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT && material >= 22 &&
					   material <= 24) {
				/* The horizontal corkscrew's curved road uses two shaded
				 * pavement materials and separate lane markings. */
				material = (surface == CSTRK_ROAD_DIRT ? 25 : 28) + material - 22;
			} else if ((physical_model == 18 || physical_model == 19 || physical_model == 22) &&
					   (material == 22 || material == 23)) {
				material = surface == CSTRK_ROAD_DIRT ? 25 : 28;
			}
		}
		out[0] = primitive[0];
		out[1] = primitive[1];
		out[2] = material;
		memcpy(out + 3, primitive + 2U + source->shape3d_numpaints, count);
		out += 3U + count;
		primitive += 2U + source->shape3d_numpaints + count;
	}
	out[0] = 0;
	out[1] = 0;
	return 1;
}

void extended_geometry_free_shape(struct SHAPE3D *shape)
{
	free(shape->shape3d_vertex_bytes);
	memset(shape, 0, sizeof(*shape));
}
