#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/extended/wheat_field.h"

#define TEST_WHEAT_HALF_TILE (CSTRK_TILE_WORLD_SIZE / 2)
#define TEST_WHEAT_PAINT_COUNT 1U
#define TEST_WHEAT_PRIMITIVE_PREFIX (SHAPE3D_PRIMITIVE_HEADER_SIZE + TEST_WHEAT_PAINT_COUNT)

static legacy_s16 read_coordinate(const struct SHAPE3D *mesh, legacy_u16 vertex, legacy_u16 offset)
{
	return LEGACY_READ_S16_LE(mesh->shape3d_vertex_bytes + (size_t)vertex * SHAPE3D_VERTEX_SIZE +
							  offset);
}

static size_t check_mesh(const struct SHAPE3D *mesh, legacy_u8 low_detail)
{
	assert(mesh->shape3d_vertex_bytes && mesh->shape3d_primitives);
	assert(mesh->shape3d_visibility_masks && mesh->shape3d_front_facing_masks);
	assert(mesh->shape3d_numverts ==
		   (low_detail ? WHEAT_FIELD_LOW_VERTEX_COUNT : WHEAT_FIELD_VERTEX_COUNT));
	assert(mesh->shape3d_numprimitives ==
		   (low_detail ? WHEAT_FIELD_LOW_PRIMITIVE_COUNT : WHEAT_FIELD_PRIMITIVE_COUNT));
	assert(mesh->shape3d_numverts <= LEGACY_U8_MAX);
	assert(mesh->shape3d_numprimitives <= LEGACY_U8_MAX);
	assert(mesh->shape3d_numpaints == TEST_WHEAT_PAINT_COUNT);
	assert(read_coordinate(mesh, 0, SHAPE3D_VERTEX_X_OFFSET) == -TEST_WHEAT_HALF_TILE);
	assert(read_coordinate(mesh, 0, SHAPE3D_VERTEX_Y_OFFSET) == 0);
	assert(read_coordinate(mesh, 0, SHAPE3D_VERTEX_Z_OFFSET) == -TEST_WHEAT_HALF_TILE);
	assert(read_coordinate(mesh, 4, SHAPE3D_VERTEX_X_OFFSET) == TEST_WHEAT_HALF_TILE);
	assert(read_coordinate(mesh, 4, SHAPE3D_VERTEX_Y_OFFSET) == WHEAT_FIELD_HEIGHT);
	assert(read_coordinate(mesh, 4, SHAPE3D_VERTEX_Z_OFFSET) == TEST_WHEAT_HALF_TILE);
	for (legacy_u16 vertex = 0; vertex < mesh->shape3d_numverts; vertex++) {
		legacy_s16 x = read_coordinate(mesh, vertex, SHAPE3D_VERTEX_X_OFFSET);
		legacy_s16 y = read_coordinate(mesh, vertex, SHAPE3D_VERTEX_Y_OFFSET);
		legacy_s16 z = read_coordinate(mesh, vertex, SHAPE3D_VERTEX_Z_OFFSET);
		assert(x >= -TEST_WHEAT_HALF_TILE && x <= TEST_WHEAT_HALF_TILE);
		assert(z >= -TEST_WHEAT_HALF_TILE && z <= TEST_WHEAT_HALF_TILE);
		assert(y >= 0 && y <= WHEAT_FIELD_HEIGHT);
	}
	const legacy_u8 *primitive = mesh->shape3d_primitives;
	legacy_u16 stalks = 0, heads = 0;
	for (legacy_u16 index = 0; index < mesh->shape3d_numprimitives; index++) {
		legacy_u8 count = primitive[0];
		assert(count == SHAPE3D_PRIMITIVE_LINE || count == WHEAT_FIELD_QUAD_VERTICES);
		assert(primitive[1] == 1U);
		legacy_u8 material = primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE];
		assert(material == WHEAT_FIELD_BASE_MATERIAL || material == WHEAT_FIELD_CANOPY_MATERIAL ||
			   material == WHEAT_FIELD_STEM_MATERIAL || material == WHEAT_FIELD_GRAIN_MATERIAL ||
			   material == WHEAT_FIELD_GRAIN_LIGHT_MATERIAL);
		if (count == SHAPE3D_PRIMITIVE_LINE) {
			assert(material == WHEAT_FIELD_STEM_MATERIAL);
			stalks++;
		}
		if (material == WHEAT_FIELD_GRAIN_MATERIAL ||
			material == WHEAT_FIELD_GRAIN_LIGHT_MATERIAL) {
			heads++;
		}
		for (legacy_u16 vertex = 0; vertex < count; vertex++) {
			legacy_u8 actual = primitive[TEST_WHEAT_PRIMITIVE_PREFIX + vertex];
			assert(actual >= WHEAT_FIELD_BOUNDING_VERTICES);
			assert(actual < mesh->shape3d_numverts);
			for (legacy_u16 previous = 0; previous < vertex; previous++) {
				assert(actual != primitive[TEST_WHEAT_PRIMITIVE_PREFIX + previous]);
			}
		}
		for (legacy_u16 mask = 0; mask < SHAPE3D_VISIBILITY_MASK_SIZE; mask++) {
			assert(mesh->shape3d_visibility_masks[index * SHAPE3D_VISIBILITY_MASK_SIZE + mask] ==
				   LEGACY_U8_MAX);
			assert(mesh->shape3d_front_facing_masks[index * SHAPE3D_VISIBILITY_MASK_SIZE + mask] ==
				   0);
		}
		primitive += TEST_WHEAT_PRIMITIVE_PREFIX + count;
	}
	assert(primitive[0] == 0 && primitive[1] == 0);
	assert(stalks == (low_detail ? 0 : WHEAT_FIELD_ROW_COUNT * WHEAT_FIELD_PLANTS_PER_ROW));
	assert(heads == stalks * 2U);
	return (size_t)(primitive - mesh->shape3d_primitives) + SHAPE3D_PRIMITIVE_HEADER_SIZE;
}

legacy_int main(void)
{
	assert(!wheat_field_create_mesh(NULL, 0));
	struct SHAPE3D full, low, repeated;
	assert(wheat_field_create_mesh(&full, 0));
	assert(wheat_field_create_mesh(&low, 1));
	assert(wheat_field_create_mesh(&repeated, 0));
	size_t full_bytes = check_mesh(&full, 0);
	(void)check_mesh(&low, 1);
	assert(check_mesh(&repeated, 0) == full_bytes);
	assert(!memcmp(full.shape3d_vertex_bytes, repeated.shape3d_vertex_bytes,
				   (size_t)full.shape3d_numverts * SHAPE3D_VERTEX_SIZE));
	assert(!memcmp(full.shape3d_primitives, repeated.shape3d_primitives, full_bytes));
	/* Changing detail preserves field edges, soil and row locations. */
	assert(!memcmp(full.shape3d_vertex_bytes, low.shape3d_vertex_bytes,
				   (size_t)low.shape3d_numverts * SHAPE3D_VERTEX_SIZE));
	free(full.shape3d_vertex_bytes);
	free(low.shape3d_vertex_bytes);
	free(repeated.shape3d_vertex_bytes);
	puts("wheat field mesh tests passed");
	return 0;
}
