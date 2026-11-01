#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/extended/wheat_field.h"

#define TEST_WHEAT_HALF_TILE (CSTRK_TILE_WORLD_SIZE / 2)
#define TEST_WHEAT_PAINT_COUNT 1U
#define TEST_WHEAT_EXPECTED_HEIGHT 10
#define TEST_WHEAT_EXPECTED_VERTEX_COUNT 36U
#define TEST_WHEAT_EXPECTED_PRIMITIVE_COUNT 7U
#define TEST_WHEAT_PRIMITIVE_PREFIX (SHAPE3D_PRIMITIVE_HEADER_SIZE + TEST_WHEAT_PAINT_COUNT)

static legacy_s16 read_coordinate(const struct SHAPE3D *mesh, legacy_u16 vertex, legacy_u16 offset)
{
	return LEGACY_READ_S16_LE(mesh->shape3d_vertex_bytes + (size_t)vertex * SHAPE3D_VERTEX_SIZE +
							  offset);
}

static size_t check_mesh(const struct SHAPE3D *mesh)
{
	assert(mesh->shape3d_vertex_bytes && mesh->shape3d_primitives);
	assert(mesh->shape3d_visibility_masks && mesh->shape3d_front_facing_masks);
	assert(mesh->shape3d_numverts == TEST_WHEAT_EXPECTED_VERTEX_COUNT);
	assert(mesh->shape3d_numprimitives == TEST_WHEAT_EXPECTED_PRIMITIVE_COUNT);
	assert(mesh->shape3d_numverts <= LEGACY_U8_MAX);
	assert(mesh->shape3d_numprimitives <= LEGACY_U8_MAX);
	assert(mesh->shape3d_numpaints == TEST_WHEAT_PAINT_COUNT);
	assert(read_coordinate(mesh, 0, SHAPE3D_VERTEX_X_OFFSET) == -TEST_WHEAT_HALF_TILE);
	assert(read_coordinate(mesh, 0, SHAPE3D_VERTEX_Y_OFFSET) == 0);
	assert(read_coordinate(mesh, 0, SHAPE3D_VERTEX_Z_OFFSET) == -TEST_WHEAT_HALF_TILE);
	assert(read_coordinate(mesh, 4, SHAPE3D_VERTEX_X_OFFSET) == TEST_WHEAT_HALF_TILE);
	assert(read_coordinate(mesh, 4, SHAPE3D_VERTEX_Y_OFFSET) == TEST_WHEAT_EXPECTED_HEIGHT);
	assert(read_coordinate(mesh, 4, SHAPE3D_VERTEX_Z_OFFSET) == TEST_WHEAT_HALF_TILE);
	for (legacy_u16 vertex = 0; vertex < mesh->shape3d_numverts; vertex++) {
		legacy_s16 x = read_coordinate(mesh, vertex, SHAPE3D_VERTEX_X_OFFSET);
		legacy_s16 y = read_coordinate(mesh, vertex, SHAPE3D_VERTEX_Y_OFFSET);
		legacy_s16 z = read_coordinate(mesh, vertex, SHAPE3D_VERTEX_Z_OFFSET);
		assert(x >= -TEST_WHEAT_HALF_TILE && x <= TEST_WHEAT_HALF_TILE);
		assert(z >= -TEST_WHEAT_HALF_TILE && z <= TEST_WHEAT_HALF_TILE);
		assert(y >= 0 && y <= TEST_WHEAT_EXPECTED_HEIGHT);
	}
	const legacy_u8 *primitive = mesh->shape3d_primitives;
	legacy_u16 soil = 0, rows = 0;
	for (legacy_u16 index = 0; index < mesh->shape3d_numprimitives; index++) {
		legacy_u8 count = primitive[0];
		/* Only horizontal soil and crop rows remain: no upright decorations. */
		assert(count == WHEAT_FIELD_QUAD_VERTICES);
		assert(primitive[1] == 1U);
		legacy_u8 material = primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE];
		assert(material == WHEAT_FIELD_BASE_MATERIAL || material == WHEAT_FIELD_CANOPY_MATERIAL);
		legacy_s16 height = material == WHEAT_FIELD_BASE_MATERIAL ? WHEAT_FIELD_BASE_HEIGHT
																  : TEST_WHEAT_EXPECTED_HEIGHT;
		soil += material == WHEAT_FIELD_BASE_MATERIAL;
		rows += material == WHEAT_FIELD_CANOPY_MATERIAL;
		for (legacy_u16 vertex = 0; vertex < count; vertex++) {
			legacy_u8 actual = primitive[TEST_WHEAT_PRIMITIVE_PREFIX + vertex];
			assert(actual >= WHEAT_FIELD_BOUNDING_VERTICES);
			assert(actual < mesh->shape3d_numverts);
			assert(read_coordinate(mesh, actual, SHAPE3D_VERTEX_Y_OFFSET) == height);
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
	assert(soil == 1U);
	assert(rows == WHEAT_FIELD_ROW_COUNT);
	return (size_t)(primitive - mesh->shape3d_primitives) + SHAPE3D_PRIMITIVE_HEADER_SIZE;
}

legacy_int main(void)
{
	assert(!wheat_field_create_mesh(NULL));
	struct SHAPE3D mesh, repeated;
	assert(wheat_field_create_mesh(&mesh));
	assert(wheat_field_create_mesh(&repeated));
	size_t mesh_bytes = check_mesh(&mesh);
	assert(check_mesh(&repeated) == mesh_bytes);
	assert(!memcmp(mesh.shape3d_vertex_bytes, repeated.shape3d_vertex_bytes,
				   (size_t)mesh.shape3d_numverts * SHAPE3D_VERTEX_SIZE));
	assert(!memcmp(mesh.shape3d_primitives, repeated.shape3d_primitives, mesh_bytes));
	free(mesh.shape3d_vertex_bytes);
	free(repeated.shape3d_vertex_bytes);
	puts("wheat field mesh tests passed");
	return 0;
}
