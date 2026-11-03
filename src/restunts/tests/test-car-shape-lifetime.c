#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../c/externs.h"
#include "../c/car/car_model.h"
#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/scene_resources.h"

#define TEST_SHAPE_COUNT 7U
#define TEST_VERTEX_COUNT 32U
#define TEST_SHAPE_BYTES (SHAPE3D_HEADER_SIZE + TEST_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE + 14U)
#define TEST_RESOURCE_BYTES (TEST_SHAPE_COUNT * TEST_SHAPE_BYTES)
#define TEST_FIRST_CAR_SHAPE 116U

legacy_s8 car_shape_resource_name[] = "sttest";
/* The empty-shape render path does not use the math lookup table. */
legacy_u8 atantable[257];

static void *allocated[2];
static legacy_u32 allocation_count;
static legacy_u32 file_load_count;
static legacy_u32 cached_release_count;
static legacy_u32 discarded_release_count;

void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	abort();
}

void far *mmgr_alloc_resbytes(const legacy_s8 *name, legacy_s32 size)
{
	(void)name;
	assert(size == TEST_RESOURCE_BYTES);
	assert(allocation_count < 2U);
	void *resource = calloc(1U, TEST_RESOURCE_BYTES);
	assert(resource != 0);
	allocated[allocation_count++] = resource;
	return resource;
}

legacy_u32 mmgr_get_chunk_size_bytes(legacy_s8 far *resource)
{
	assert(resource == allocated[0]);
	return TEST_RESOURCE_BYTES;
}

void far *file_load_3dres(const legacy_s8 *name)
{
	file_load_count++;
	legacy_u8 *resource = mmgr_alloc_resbytes(name, TEST_RESOURCE_BYTES);
	for (legacy_u32 shape_index = 0; shape_index < TEST_SHAPE_COUNT; shape_index++) {
		legacy_u8 *shape = resource + shape_index * TEST_SHAPE_BYTES;
		shape[SHAPE3D_VERTEX_COUNT_OFFSET] = TEST_VERTEX_COUNT;
		shape[SHAPE3D_PRIMITIVE_COUNT_OFFSET] = 1U;
		shape[SHAPE3D_PAINT_COUNT_OFFSET] = 1U;
		for (legacy_u32 vertex_index = 0; vertex_index < TEST_VERTEX_COUNT; vertex_index++) {
			LEGACY_WRITE_U16_LE(shape + SHAPE3D_HEADER_SIZE + vertex_index * SHAPE3D_VERTEX_SIZE,
								vertex_index + 100U * shape_index);
		}
	}
	return resource;
}

legacy_s8 far *locate_shape_fatal(legacy_s8 far *resource, const legacy_s8 *name)
{
	legacy_u32 shape_index;

	if (memcmp(name, "car", 3U) == 0) {
		shape_index = (legacy_u32)(name[3] - '0');
	} else {
		assert(memcmp(name, "exp", 3U) == 0);
		shape_index = (legacy_u32)(name[3] - '0') + 3U;
	}
	assert(shape_index < TEST_SHAPE_COUNT);
	return resource + shape_index * TEST_SHAPE_BYTES;
}

static void release_resource(void *resource)
{
	assert(allocation_count != 0U);
	assert(resource == allocated[allocation_count - 1U]);
	memset(resource, 0xa5, TEST_RESOURCE_BYTES);
	free(resource);
	allocated[--allocation_count] = 0;
}

void mmgr_release(void far *resource)
{
	discarded_release_count++;
	release_resource(resource);
}

void far *mmgr_free(legacy_s8 far *resource)
{
	cached_release_count++;
	release_resource(resource);
	return 0;
}

static void check_released_shapes(void)
{
	struct TRANSFORMEDSHAPE3D instance = {0};
	instance.culling_distance = 1024U;
	polyinfo_reset();
	for (legacy_u32 index = TEST_FIRST_CAR_SHAPE; index <= OPPONENT_CAR_HIGH_SHAPE; index++) {
		instance.shapeptr = &game3dshapes[index];
		assert(instance.shapeptr->shape3d_numverts == 0U);
		assert(instance.shapeptr->shape3d_numprimitives == 0U);
		assert(instance.shapeptr->shape3d_numpaints == 0U);
		assert(instance.shapeptr->shape3d_vertex_bytes == 0);
		assert(instance.shapeptr->shape3d_primitives == 0);
		assert(instance.shapeptr->shape3d_visibility_masks == 0);
		assert(instance.shapeptr->shape3d_front_facing_masks == 0);
		/* A scene may retain the descriptor after its resource is released. */
		assert(shape3d_transform_and_queue(&instance) == LEGACY_U16_MAX);
	}
	assert(carresptr == 0);
	assert(car2resptr == 0);
	assert(allocation_count == 0U);
}

static void check_cycle(legacy_s8 *opponent, legacy_u32 expected_loads,
						legacy_u32 expected_discards)
{
	legacy_u32 previous_loads = file_load_count;
	legacy_u32 previous_discards = discarded_release_count;
	legacy_u32 previous_cached = cached_release_count;

	legacy_s8 player[] = "TEST";
	shape3d_load_car_shapes(player, opponent);
	assert(file_load_count - previous_loads == expected_loads);
	assert(game3dshapes[PLAYER_CAR_HIGH_SHAPE].shape3d_numverts == TEST_VERTEX_COUNT);
	struct VECTOR vertex;
	shape3d_vertex_read(&game3dshapes[PLAYER_CAR_HIGH_SHAPE], 9U, &vertex);
	assert(vertex.x == 209);
	if (opponent[0] != -1) {
		assert(carresptr != car2resptr);
		shape3d_vertex_read(&game3dshapes[OPPONENT_CAR_HIGH_SHAPE], 9U, &vertex);
		assert(vertex.x == 209);
	}
	shape3d_free_car_shapes();
	assert(cached_release_count - previous_cached == 1U);
	assert(discarded_release_count - previous_discards == expected_discards);
	check_released_shapes();
	assert(game3dshapes[TEST_FIRST_CAR_SHAPE - 1U].shape3d_numverts == 17U);
	shape3d_free_car_shapes();
	assert(cached_release_count - previous_cached == 1U);
	assert(discarded_release_count - previous_discards == expected_discards);
}

static legacy_u32 wheel_vertex_fingerprint(void)
{
	legacy_s8 no_opponent[] = {-1, 0, 0, 0};
	legacy_s8 player[] = "TEST";
	shape3d_load_car_shapes(player, no_opponent);
	static const legacy_s16 angles[] = {-32768, -2048, -1024, -481, -240, -1,	0,
										1,		240,   481,	  1024, 2048, 32767};
	static const legacy_s16 offsets[] = {-32768, -65, -64, -63, -1, 0, 1, 63, 64, 65, 32767};
	legacy_s16 suspension[4];
	struct VECTOR vertex;
	struct VECTOR previous[TEST_VERTEX_COUNT];
	legacy_u32 hash = 2166136261UL;
	for (legacy_u32 sample = 0; sample < 4096; sample++) {
		for (legacy_u32 wheel = 0; wheel < 4; wheel++) {
			suspension[wheel] = offsets[(sample + wheel * 3) % 11];
		}
		for (legacy_u32 repeated = 0; repeated < 2; repeated++) {
			shape3d_update_car_wheel_vertices(
				&game3dshapes[PLAYER_CAR_WHEEL_SHAPE], 8, angles[sample % 13], suspension,
				player_wheel_vertex_state, player_base_wheel_vertices, player_front_wheel_centers);
			for (legacy_u32 index = 0; index < TEST_VERTEX_COUNT; index++) {
				shape3d_vertex_read(&game3dshapes[PLAYER_CAR_WHEEL_SHAPE], index, &vertex);
				if (repeated != 0) {
					assert(vertex.x == previous[index].x);
					assert(vertex.y == previous[index].y);
					assert(vertex.z == previous[index].z);
				} else {
					previous[index] = vertex;
					hash = (hash ^ (legacy_u16)vertex.x) * 16777619UL;
					hash = (hash ^ (legacy_u16)vertex.y) * 16777619UL;
					hash = (hash ^ (legacy_u16)vertex.z) * 16777619UL;
				}
			}
		}
		for (legacy_u32 index = 0; index < 5; index++) {
			hash = (hash ^ (legacy_u16)player_wheel_vertex_state[index]) * 16777619UL;
		}
	}
	shape3d_free_car_shapes();
	check_released_shapes();
	return hash;
}

int main(void)
{
	game3dshapes[TEST_FIRST_CAR_SHAPE - 1U].shape3d_numverts = 17U;
	legacy_s8 different_opponent[] = "DIFF";
	legacy_s8 no_opponent[] = {-1, 0, 0, 0};
	legacy_s8 same_opponent[] = "TEST";
	for (legacy_u32 cycle = 0; cycle < 20U; cycle++) {
		check_cycle(same_opponent, 1U, 1U);
		check_cycle(no_opponent, 1U, 0U);
		check_cycle(different_opponent, 2U, 1U);
	}
#ifdef PRERENDER_RECORD_BASELINE
	fprintf(stdout, "%08" LEGACY_PRIx32 "\n", wheel_vertex_fingerprint());
#else
	/* Pre-refactor geometry and cache state across steering and suspension boundaries. */
	assert(wheel_vertex_fingerprint() == 0x12e2833dUL);
#endif

	return 0;
}
