#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../c/extended/checkpoint_shape.h"

#define TEST_CHECKPOINT_PAINT_COUNT 3U
#define TEST_CHECKPOINT_PRIMITIVE_COUNT 3U
#define TEST_CHECKPOINT_QUAD_VERTEX_COUNT 4U
#define TEST_CHECKPOINT_PRIMITIVE_SIZE                                                             \
	(SHAPE3D_PRIMITIVE_HEADER_SIZE + TEST_CHECKPOINT_PAINT_COUNT +                                 \
	 TEST_CHECKPOINT_QUAD_VERTEX_COUNT)
#define TEST_CHECKPOINT_STRUCTURE_MATERIAL 31U
#define TEST_CHECKPOINT_PAVED_MATERIAL 19U
#define TEST_CHECKPOINT_DIRT_MATERIAL 25U
#define TEST_CHECKPOINT_ICE_MATERIAL 28U

static void test_checkpoint_paint(legacy_u16 id, legacy_u8 expected)
{
	legacy_u8 primitives[] = {TEST_CHECKPOINT_QUAD_VERTEX_COUNT,
							  1,
							  CHECKPOINT_SHAPE_WHITE_MATERIAL,
							  CHECKPOINT_SHAPE_WHITE_MATERIAL,
							  CHECKPOINT_SHAPE_WHITE_MATERIAL,
							  0,
							  1,
							  2,
							  3,
							  TEST_CHECKPOINT_QUAD_VERTEX_COUNT,
							  0,
							  TEST_CHECKPOINT_STRUCTURE_MATERIAL,
							  TEST_CHECKPOINT_STRUCTURE_MATERIAL,
							  TEST_CHECKPOINT_STRUCTURE_MATERIAL,
							  0,
							  1,
							  2,
							  3,
							  TEST_CHECKPOINT_QUAD_VERTEX_COUNT,
							  0,
							  TEST_CHECKPOINT_PAVED_MATERIAL,
							  TEST_CHECKPOINT_DIRT_MATERIAL,
							  TEST_CHECKPOINT_ICE_MATERIAL,
							  0,
							  1,
							  2,
							  3,
							  0,
							  0};
	legacy_u8 original[sizeof(primitives)];
	memcpy(original, primitives, sizeof(primitives));
	legacy_u8 masks[TEST_CHECKPOINT_PRIMITIVE_COUNT * SHAPE3D_VISIBILITY_MASK_SIZE] = {0};
	struct SHAPE3D mesh = {TEST_CHECKPOINT_QUAD_VERTEX_COUNT,
						   NULL,
						   TEST_CHECKPOINT_PRIMITIVE_COUNT,
						   TEST_CHECKPOINT_PAINT_COUNT,
						   primitives,
						   masks,
						   masks};
	assert(checkpoint_shape_modify(&mesh, id));
	for (legacy_u16 paint = 0; paint < TEST_CHECKPOINT_PAINT_COUNT; paint++) {
		assert(primitives[SHAPE3D_PRIMITIVE_HEADER_SIZE + paint] == expected);
		original[SHAPE3D_PRIMITIVE_HEADER_SIZE + paint] = expected;
	}
	assert(memcmp(primitives, original, sizeof(primitives)) == 0);
	assert(mesh.shape3d_numprimitives == TEST_CHECKPOINT_PRIMITIVE_COUNT);
	assert(mesh.shape3d_numpaints == TEST_CHECKPOINT_PAINT_COUNT);
	assert(mesh.shape3d_visibility_masks == masks && mesh.shape3d_front_facing_masks == masks);
	assert(checkpoint_shape_modify(&mesh, 0));
	assert(memcmp(primitives, original, sizeof(primitives)) == 0);
}

#define TEST_COMBINED_VERTEX_COUNT 4U
#define TEST_COMBINED_PRIMITIVE_COUNT 2U
#define TEST_COMBINED_PRIMITIVE_BYTES                                                              \
	(TEST_COMBINED_PRIMITIVE_COUNT * TEST_CHECKPOINT_PRIMITIVE_SIZE + 2U)
#define TEST_COMBINED_VERTEX_BYTES (TEST_COMBINED_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE)
#define TEST_COMBINED_MASK_BYTES (TEST_COMBINED_PRIMITIVE_COUNT * SHAPE3D_VISIBILITY_MASK_SIZE)
#define TEST_COMBINED_ALLOCATION_BYTES                                                             \
	(TEST_COMBINED_VERTEX_BYTES + TEST_COMBINED_PRIMITIVE_BYTES + TEST_COMBINED_MASK_BYTES * 2U)
#define TEST_COMBINED_DIRECTION_COUNT 4U
#define TEST_COMBINED_SEAM_VERTICES 2U
#define TEST_COMBINED_VISIBILITY 0x35U
#define TEST_COMBINED_FACING 0xA7U

static struct VECTOR test_combined_vertex(const struct SHAPE3D *mesh, legacy_u8 index)
{
	const legacy_u8 *bytes = mesh->shape3d_vertex_bytes + index * SHAPE3D_VERTEX_SIZE;
	struct VECTOR point = {LEGACY_READ_S16_LE(bytes),
						   LEGACY_READ_S16_LE(bytes + SHAPE3D_VERTEX_Y_OFFSET),
						   LEGACY_READ_S16_LE(bytes + SHAPE3D_VERTEX_Z_OFFSET)};
	return point;
}

static struct SHAPE3D test_combined_source(legacy_u8 direction)
{
	legacy_u8 *allocation = malloc(TEST_COMBINED_ALLOCATION_BYTES);
	assert(allocation != NULL);
	struct SHAPE3D mesh = {TEST_COMBINED_VERTEX_COUNT,
						   allocation,
						   TEST_COMBINED_PRIMITIVE_COUNT,
						   TEST_CHECKPOINT_PAINT_COUNT,
						   allocation + TEST_COMBINED_VERTEX_BYTES,
						   allocation + TEST_COMBINED_VERTEX_BYTES + TEST_COMBINED_PRIMITIVE_BYTES,
						   allocation + TEST_COMBINED_VERTEX_BYTES + TEST_COMBINED_PRIMITIVE_BYTES +
							   TEST_COMBINED_MASK_BYTES};
	for (legacy_u8 vertex = 0; vertex < TEST_COMBINED_VERTEX_COUNT; vertex++) {
		legacy_s16 x =
			vertex & 2U ? CHECKPOINT_GATE_OUTER_HALF_WIDTH : -CHECKPOINT_GATE_OUTER_HALF_WIDTH;
		legacy_s16 y =
			vertex == 1U || vertex == 2U ? CHECKPOINT_GATE_SIGN_TOP : CHECKPOINT_GATE_SIGN_BOTTOM;
		legacy_s16 z = CHECKPOINT_GATE_PLANE;
		for (legacy_u8 turn = 0; turn < direction; turn++) {
			legacy_s16 previous_x = x;
			x = z;
			z = -previous_x;
		}
		legacy_u8 *bytes = allocation + vertex * SHAPE3D_VERTEX_SIZE;
		LEGACY_WRITE_U16_LE(bytes, (legacy_u16)x);
		LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_Y_OFFSET, (legacy_u16)y);
		LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_Z_OFFSET, (legacy_u16)z);
	}
	for (legacy_u8 index = 0; index < TEST_COMBINED_PRIMITIVE_COUNT; index++) {
		legacy_u8 *primitive = mesh.shape3d_primitives + index * TEST_CHECKPOINT_PRIMITIVE_SIZE;
		primitive[0] = TEST_CHECKPOINT_QUAD_VERTEX_COUNT;
		primitive[1] = index;
		for (legacy_u8 paint = 0; paint < TEST_CHECKPOINT_PAINT_COUNT; paint++) {
			primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE + paint] =
				index ? TEST_CHECKPOINT_STRUCTURE_MATERIAL : CHECKPOINT_SHAPE_WHITE_MATERIAL;
		}
		for (legacy_u8 vertex = 0; vertex < TEST_CHECKPOINT_QUAD_VERTEX_COUNT; vertex++) {
			primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE + TEST_CHECKPOINT_PAINT_COUNT + vertex] =
				vertex;
		}
		memset(mesh.shape3d_visibility_masks + index * SHAPE3D_VISIBILITY_MASK_SIZE,
			   TEST_COMBINED_VISIBILITY + index, SHAPE3D_VISIBILITY_MASK_SIZE);
		memset(mesh.shape3d_front_facing_masks + index * SHAPE3D_VISIBILITY_MASK_SIZE,
			   TEST_COMBINED_FACING + index, SHAPE3D_VISIBILITY_MASK_SIZE);
	}
	mesh.shape3d_primitives[TEST_COMBINED_PRIMITIVE_BYTES - 2U] = 0;
	mesh.shape3d_primitives[TEST_COMBINED_PRIMITIVE_BYTES - 1U] = 0;
	return mesh;
}

static void test_combined_banner(legacy_u8 direction)
{
	struct SHAPE3D mesh = test_combined_source(direction);
	legacy_u8 original[TEST_COMBINED_ALLOCATION_BYTES];
	memcpy(original, mesh.shape3d_vertex_bytes, sizeof(original));
	assert(checkpoint_shape_modify(&mesh, CSTRK_COMBINED_CHECKPOINT_ID));
	assert(mesh.shape3d_numverts == TEST_COMBINED_VERTEX_COUNT + TEST_COMBINED_SEAM_VERTICES);
	assert(mesh.shape3d_numprimitives == TEST_COMBINED_PRIMITIVE_COUNT + 1U);
	assert(mesh.shape3d_numpaints == TEST_CHECKPOINT_PAINT_COUNT);
	assert(memcmp(mesh.shape3d_vertex_bytes, original, TEST_COMBINED_VERTEX_BYTES) == 0);
	assert(memcmp(mesh.shape3d_primitives + TEST_CHECKPOINT_PRIMITIVE_SIZE * 2U,
				  original + TEST_COMBINED_VERTEX_BYTES + TEST_CHECKPOINT_PRIMITIVE_SIZE,
				  TEST_CHECKPOINT_PRIMITIVE_SIZE + 2U) == 0);
	for (legacy_u8 part = 0; part < 2U; part++) {
		const legacy_u8 *primitive =
			mesh.shape3d_primitives + part * TEST_CHECKPOINT_PRIMITIVE_SIZE;
		legacy_u8 seam_vertices = 0;
		legacy_s16 minimum = CHECKPOINT_GATE_SIGN_TOP, maximum = CHECKPOINT_GATE_SIGN_BOTTOM;
		for (legacy_u8 paint = 0; paint < TEST_CHECKPOINT_PAINT_COUNT; paint++) {
			assert(primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE + paint] ==
				   (part ? CHECKPOINT_SHAPE_RED_MATERIAL : CHECKPOINT_SHAPE_BLUE_MATERIAL));
		}
		for (legacy_u8 vertex = 0; vertex < TEST_CHECKPOINT_QUAD_VERTEX_COUNT; vertex++) {
			legacy_u8 index =
				primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE + TEST_CHECKPOINT_PAINT_COUNT + vertex];
			struct VECTOR point = test_combined_vertex(&mesh, index);
			assert(part ? point.y <= CHECKPOINT_GATE_SIGN_SPLIT
						: point.y >= CHECKPOINT_GATE_SIGN_SPLIT);
			assert(point.y >= CHECKPOINT_GATE_SIGN_BOTTOM && point.y <= CHECKPOINT_GATE_SIGN_TOP);
			if (point.y < minimum) {
				minimum = point.y;
			}
			if (point.y > maximum) {
				maximum = point.y;
			}
			if (index >= TEST_COMBINED_VERTEX_COUNT) {
				assert(point.y == CHECKPOINT_GATE_SIGN_SPLIT);
				assert(direction & 1U ? point.x == CHECKPOINT_GATE_PLANE
									  : point.z == CHECKPOINT_GATE_PLANE);
				assert(direction & 1U ? abs(point.z) == CHECKPOINT_GATE_OUTER_HALF_WIDTH
									  : abs(point.x) == CHECKPOINT_GATE_OUTER_HALF_WIDTH);
				seam_vertices++;
			}
		}
		assert(seam_vertices == TEST_COMBINED_SEAM_VERTICES);
		assert(maximum - minimum == (part ? CHECKPOINT_GATE_SIGN_SPLIT - CHECKPOINT_GATE_SIGN_BOTTOM
										  : CHECKPOINT_GATE_SIGN_TOP - CHECKPOINT_GATE_SIGN_SPLIT));
	}
	for (legacy_u8 index = 0; index < mesh.shape3d_numprimitives; index++) {
		legacy_u8 source = index ? index - 1U : 0U;
		for (legacy_u8 byte = 0; byte < SHAPE3D_VISIBILITY_MASK_SIZE; byte++) {
			assert(mesh.shape3d_visibility_masks[index * SHAPE3D_VISIBILITY_MASK_SIZE + byte] ==
				   TEST_COMBINED_VISIBILITY + source);
			assert(mesh.shape3d_front_facing_masks[index * SHAPE3D_VISIBILITY_MASK_SIZE + byte] ==
				   TEST_COMBINED_FACING + source);
		}
	}
	free(mesh.shape3d_vertex_bytes);
}

static void test_combined_failure(void)
{
	for (legacy_u8 failure = 0; failure < 3U; failure++) {
		struct SHAPE3D mesh = test_combined_source(0);
		if (failure == 0U) {
			mesh.shape3d_numverts = LEGACY_U8_MAX;
		} else if (failure == 1U) {
			mesh.shape3d_primitives[0] = SHAPE3D_PRIMITIVE_TYPE_COUNT;
		} else {
			mesh.shape3d_primitives[SHAPE3D_PRIMITIVE_HEADER_SIZE + TEST_CHECKPOINT_PAINT_COUNT] =
				LEGACY_U8_MAX;
		}
		legacy_u8 original[TEST_COMBINED_ALLOCATION_BYTES];
		struct SHAPE3D retained = mesh;
		memcpy(original, mesh.shape3d_vertex_bytes, sizeof(original));
		assert(!checkpoint_shape_modify(&mesh, CSTRK_COMBINED_CHECKPOINT_ID));
		assert(memcmp(&mesh, &retained, sizeof(mesh)) == 0);
		assert(memcmp(mesh.shape3d_vertex_bytes, original, sizeof(original)) == 0);
		free(mesh.shape3d_vertex_bytes);
	}
}

legacy_int main(void)
{
	test_checkpoint_paint(CSTRK_BLUE_CHECKPOINT_ID, CHECKPOINT_SHAPE_BLUE_MATERIAL);
	test_checkpoint_paint(CSTRK_RED_CHECKPOINT_ID, CHECKPOINT_SHAPE_RED_MATERIAL);
	for (legacy_u8 direction = 0; direction < TEST_COMBINED_DIRECTION_COUNT; direction++) {
		test_combined_banner(direction);
	}
	test_combined_failure();
	return 0;
}
