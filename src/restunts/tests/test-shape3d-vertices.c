#include <assert.h>

#include "../c/externs.h"
#include "../c/shape3d/shape3d.h"

#define TEST_VERTEX_INDEX 1U
#define TEST_VERTEX_STORAGE_PREFIX_SIZE 1U
#define TEST_VERTEX_STORAGE_SIZE                                                                   \
	(TEST_VERTEX_STORAGE_PREFIX_SIZE + (TEST_VERTEX_INDEX + 1U) * SHAPE3D_VERTEX_SIZE)
#define TEST_VERTEX_BYTE_OFFSET                                                                    \
	(TEST_VERTEX_STORAGE_PREFIX_SIZE + TEST_VERTEX_INDEX * SHAPE3D_VERTEX_SIZE)

int main(void)
{
	struct SHAPE3D shape;
	legacy_u8 bytes[TEST_VERTEX_STORAGE_SIZE];
	shape.shape3d_vertex_bytes = bytes + TEST_VERTEX_STORAGE_PREFIX_SIZE;
	struct VECTOR source;
	source.x = LEGACY_S16_FROM_BITS(LEGACY_U16_SIGN_BIT);
	source.y = LEGACY_S16_MAX;
	source.z = -1;
	shape3d_vertex_write(&shape, TEST_VERTEX_INDEX, &source);

	assert(bytes[TEST_VERTEX_BYTE_OFFSET + SHAPE3D_VERTEX_X_OFFSET] == 0U);
	assert(bytes[TEST_VERTEX_BYTE_OFFSET + SHAPE3D_VERTEX_X_OFFSET + 1U] == LEGACY_U8_SIGN_BIT);
	assert(bytes[TEST_VERTEX_BYTE_OFFSET + SHAPE3D_VERTEX_Y_OFFSET] == LEGACY_U8_MAX);
	assert(bytes[TEST_VERTEX_BYTE_OFFSET + SHAPE3D_VERTEX_Y_OFFSET + 1U] == LEGACY_S8_MAX);
	assert(bytes[TEST_VERTEX_BYTE_OFFSET + SHAPE3D_VERTEX_Z_OFFSET] == LEGACY_U8_MAX);
	assert(bytes[TEST_VERTEX_BYTE_OFFSET + SHAPE3D_VERTEX_Z_OFFSET + 1U] == LEGACY_U8_MAX);

	struct VECTOR destination;
	destination.x = 0;
	destination.y = 0;
	destination.z = 0;
	shape3d_vertex_read(&shape, TEST_VERTEX_INDEX, &destination);
	assert(destination.x == source.x);
	assert(destination.y == source.y);
	assert(destination.z == source.z);

	return 0;
}
