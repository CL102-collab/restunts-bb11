#include "wheat_field.h"

#include <stdlib.h>
#include <string.h>

#define WHEAT_FIELD_HALF_TILE (CSTRK_TILE_WORLD_SIZE / 2)
#define WHEAT_FIELD_ALWAYS_VISIBLE 1U
#define WHEAT_FIELD_PAINT_COUNT 1U
#define WHEAT_FIELD_PRIMITIVE_HEADER_BYTES (SHAPE3D_PRIMITIVE_HEADER_SIZE + WHEAT_FIELD_PAINT_COUNT)
#define WHEAT_FIELD_QUAD_BYTES (WHEAT_FIELD_PRIMITIVE_HEADER_BYTES + WHEAT_FIELD_QUAD_VERTICES)
#define WHEAT_FIELD_PRIMITIVE_TERMINATOR_BYTES 2U
#define WHEAT_FIELD_BASE_VERTEX_FIRST WHEAT_FIELD_BOUNDING_VERTICES
#define WHEAT_FIELD_ROW_VERTEX_FIRST (WHEAT_FIELD_BASE_VERTEX_FIRST + WHEAT_FIELD_QUAD_VERTICES)

static void wheat_field_write_vertex(struct SHAPE3D *mesh, legacy_u16 index, legacy_s16 x,
									 legacy_s16 y, legacy_s16 z)
{
	legacy_u8 *bytes = mesh->shape3d_vertex_bytes + (size_t)index * SHAPE3D_VERTEX_SIZE;
	LEGACY_WRITE_U16_LE(bytes, (legacy_u16)x);
	LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_Y_OFFSET, (legacy_u16)y);
	LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_Z_OFFSET, (legacy_u16)z);
}

static legacy_u8 *wheat_field_write_primitive(legacy_u8 *bytes, legacy_u8 count, legacy_u8 material,
											  const legacy_u8 *indices)
{
	bytes[0] = count;
	bytes[1] = WHEAT_FIELD_ALWAYS_VISIBLE;
	bytes[SHAPE3D_PRIMITIVE_HEADER_SIZE] = material;
	memcpy(bytes + WHEAT_FIELD_PRIMITIVE_HEADER_BYTES, indices, count);
	return bytes + WHEAT_FIELD_PRIMITIVE_HEADER_BYTES + count;
}

static legacy_u8 *wheat_field_write_quad(struct SHAPE3D *mesh, legacy_u8 *primitive,
										 legacy_u16 first, legacy_s16 left, legacy_s16 right,
										 legacy_s16 height, legacy_u8 material)
{
	for (legacy_u16 corner = 0; corner < WHEAT_FIELD_QUAD_VERTICES; corner++) {
		wheat_field_write_vertex(mesh, first + corner, corner & 1U ? right : left, height,
								 corner & 2U ? WHEAT_FIELD_HALF_TILE : -WHEAT_FIELD_HALF_TILE);
	}
	legacy_u8 indices[WHEAT_FIELD_QUAD_VERTICES];
	indices[0] = (legacy_u8)first;
	indices[1] = (legacy_u8)(first + 1U);
	indices[2] = (legacy_u8)(first + 3U);
	indices[3] = (legacy_u8)(first + 2U);
	return wheat_field_write_primitive(primitive, WHEAT_FIELD_QUAD_VERTICES, material, indices);
}

static legacy_s16 wheat_field_center(legacy_u16 index, legacy_u16 count)
{
	return (legacy_s16)(((legacy_s32)index * 2 + 1) * CSTRK_TILE_WORLD_SIZE / (2 * count) -
						WHEAT_FIELD_HALF_TILE);
}

legacy_s16 wheat_field_create_mesh(struct SHAPE3D *mesh)
{
	if (!mesh) {
		return 0;
	}
	memset(mesh, 0, sizeof(*mesh));
	size_t vertex_bytes = WHEAT_FIELD_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE;
	size_t primitive_bytes = WHEAT_FIELD_PRIMITIVE_COUNT * WHEAT_FIELD_QUAD_BYTES +
							 WHEAT_FIELD_PRIMITIVE_TERMINATOR_BYTES;
	size_t mask_bytes = WHEAT_FIELD_PRIMITIVE_COUNT * SHAPE3D_VISIBILITY_MASK_SIZE;
	legacy_u8 *memory = malloc(vertex_bytes + primitive_bytes + mask_bytes * 2U);
	if (!memory) {
		return 0;
	}
	mesh->shape3d_numverts = WHEAT_FIELD_VERTEX_COUNT;
	mesh->shape3d_vertex_bytes = memory;
	mesh->shape3d_numprimitives = WHEAT_FIELD_PRIMITIVE_COUNT;
	mesh->shape3d_numpaints = WHEAT_FIELD_PAINT_COUNT;
	mesh->shape3d_primitives = memory + vertex_bytes;
	mesh->shape3d_visibility_masks = mesh->shape3d_primitives + primitive_bytes;
	mesh->shape3d_front_facing_masks = mesh->shape3d_visibility_masks + mask_bytes;
	memset(mesh->shape3d_visibility_masks, LEGACY_U8_MAX, mask_bytes);
	memset(mesh->shape3d_front_facing_masks, 0, mask_bytes);
	/* Native culling uses vertices zero and four as opposite enclosing corners. */
	static const legacy_u8 bounds_order[WHEAT_FIELD_BOUNDING_VERTICES] = {0, 1, 3, 2, 7, 6, 4, 5};
	for (legacy_u16 index = 0; index < WHEAT_FIELD_BOUNDING_VERTICES; index++) {
		legacy_u8 corner = bounds_order[index];
		wheat_field_write_vertex(mesh, index,
								 corner & 1U ? WHEAT_FIELD_HALF_TILE : -WHEAT_FIELD_HALF_TILE,
								 corner & 2U ? WHEAT_FIELD_HEIGHT : 0,
								 corner & 4U ? WHEAT_FIELD_HALF_TILE : -WHEAT_FIELD_HALF_TILE);
	}
	legacy_u8 *primitive = wheat_field_write_quad(
		mesh, mesh->shape3d_primitives, WHEAT_FIELD_BASE_VERTEX_FIRST, -WHEAT_FIELD_HALF_TILE,
		WHEAT_FIELD_HALF_TILE, WHEAT_FIELD_BASE_HEIGHT, WHEAT_FIELD_BASE_MATERIAL);
	for (legacy_u16 row = 0; row < WHEAT_FIELD_ROW_COUNT; row++) {
		legacy_s16 center = wheat_field_center(row, WHEAT_FIELD_ROW_COUNT);
		primitive = wheat_field_write_quad(
			mesh, primitive, WHEAT_FIELD_ROW_VERTEX_FIRST + row * WHEAT_FIELD_QUAD_VERTICES,
			center - WHEAT_FIELD_ROW_HALF_WIDTH, center + WHEAT_FIELD_ROW_HALF_WIDTH,
			WHEAT_FIELD_HEIGHT, WHEAT_FIELD_CANOPY_MATERIAL);
	}
	primitive[0] = primitive[1] = 0;
	return 1;
}
