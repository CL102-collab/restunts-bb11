#include <assert.h>
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
	checkpoint_shape_modify(&mesh, id);
	for (legacy_u16 paint = 0; paint < TEST_CHECKPOINT_PAINT_COUNT; paint++) {
		assert(primitives[SHAPE3D_PRIMITIVE_HEADER_SIZE + paint] == expected);
		original[SHAPE3D_PRIMITIVE_HEADER_SIZE + paint] = expected;
	}
	assert(memcmp(primitives, original, sizeof(primitives)) == 0);
	assert(mesh.shape3d_numprimitives == TEST_CHECKPOINT_PRIMITIVE_COUNT);
	assert(mesh.shape3d_numpaints == TEST_CHECKPOINT_PAINT_COUNT);
	assert(mesh.shape3d_visibility_masks == masks && mesh.shape3d_front_facing_masks == masks);
	checkpoint_shape_modify(&mesh, 0);
	assert(memcmp(primitives, original, sizeof(primitives)) == 0);
}

legacy_int main(void)
{
	test_checkpoint_paint(CSTRK_BLUE_CHECKPOINT_ID, CHECKPOINT_SHAPE_BLUE_MATERIAL);
	test_checkpoint_paint(CSTRK_RED_CHECKPOINT_ID, CHECKPOINT_SHAPE_RED_MATERIAL);
	return 0;
}
