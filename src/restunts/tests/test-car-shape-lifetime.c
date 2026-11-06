#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../c/externs.h"
#include "../c/car/car_model.h"
#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/scene_resources.h"
#include "../c/owoot/owoot.h"

#undef memcpy

#define TEST_SHAPE_COUNT 7U
#define TEST_VERTEX_COUNT 32U
#define TEST_SHOWROOM_VERTEX_COUNT 255U
#define TEST_SHOWROOM_PRIMITIVE_COUNT 2U
#define TEST_PRIMITIVE_MASK_BYTES (2U * SHAPE3D_VISIBILITY_MASK_SIZE)
#define TEST_SHOWROOM_SCALE 20
#define TEST_FIRST_WHEEL_VERTEX 8U
#define TEST_WHEEL_VERTEX_COUNT 24U
#define TEST_WHEEL_CONTROL_COUNT 6U
#define TEST_WHEEL_COUNT 4U
#define TEST_BODY_VERTEX_COUNT (TEST_SHOWROOM_VERTEX_COUNT - TEST_VERTEX_COUNT)
#define TEST_STEERING_ANGLE 128
#define TEST_SHAPE_BYTES                                                                           \
	(SHAPE3D_HEADER_SIZE + TEST_SHOWROOM_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE + 28U)
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
static legacy_u8 showroom_fixture_enabled;
static legacy_u8 original_resources[2][TEST_RESOURCE_BYTES];

static void write_test_vertex(legacy_u8 *shape, legacy_u16 index, const struct VECTOR *vertex)
{
	legacy_u8 *bytes = shape + SHAPE3D_HEADER_SIZE + index * SHAPE3D_VERTEX_SIZE;
	LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_X_OFFSET, (legacy_u16)vertex->x);
	LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_Y_OFFSET, (legacy_u16)vertex->y);
	LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_Z_OFFSET, (legacy_u16)vertex->z);
}

static void initialize_showroom_fixture(legacy_u8 *resource, legacy_u8 different_car)
{
	/* Matching wheel centres identify the driving origin. The showroom is
	 * deliberately raised and shifted on all axes, with half-unit body detail. */
	const struct VECTOR origin = {100, 120, -80};
	const struct VECTOR body[TEST_BODY_VERTEX_COUNT] = {
		{different_car != 0 ? -410 : -210, 170, -330},
		{210, 170, -330},
		{-10, 30, 50},
		{10, 30, 50}};
	for (legacy_u16 model = 0; model < 2U; model++) {
		legacy_u8 *shape = resource + model * TEST_SHAPE_BYTES;
		legacy_u16 count = model == 0 ? TEST_SHOWROOM_VERTEX_COUNT : TEST_VERTEX_COUNT;
		shape[SHAPE3D_VERTEX_COUNT_OFFSET] = (legacy_u8)count;
		shape[SHAPE3D_PRIMITIVE_COUNT_OFFSET] = model == 0 ? TEST_SHOWROOM_PRIMITIVE_COUNT : 1U;
		for (legacy_u16 index = 0; index < count; index++) {
			struct VECTOR vertex;
			if (index < TEST_FIRST_WHEEL_VERTEX) {
				vertex.x = (index & 1U) != 0 ? 20 : -20;
				vertex.y = (index & 2U) != 0 ? 25 : -6;
				vertex.z = (index & 4U) != 0 ? 50 : -50;
			} else if (index < TEST_VERTEX_COUNT) {
				legacy_u16 wheel = (index - TEST_FIRST_WHEEL_VERTEX) / TEST_WHEEL_CONTROL_COUNT;
				legacy_u16 control = (index - TEST_FIRST_WHEEL_VERTEX) % TEST_WHEEL_CONTROL_COUNT;
				legacy_s16 width = different_car != 0 ? 14 : 12;
				vertex.x = ((wheel & 1U) != 0 ? 1 : -1) * (width + (control >= 3U ? 4 : 0));
				vertex.y = 3 + (control % 3U == 1U ? 6 : 0);
				vertex.z = ((wheel & 2U) != 0 ? -30 : 30) + (control % 3U == 2U ? 6 : 0);
			} else {
				vertex = body[index - TEST_VERTEX_COUNT];
			}
			if (model == 0) {
				if (index < TEST_VERTEX_COUNT) {
					vertex.x *= TEST_SHOWROOM_SCALE;
					vertex.y *= TEST_SHOWROOM_SCALE;
					vertex.z *= TEST_SHOWROOM_SCALE;
				}
				vertex.x += origin.x;
				vertex.y += origin.y;
				vertex.z += origin.z;
			}
			write_test_vertex(shape, index, &vertex);
		}
		legacy_u16 primitives = shape[SHAPE3D_PRIMITIVE_COUNT_OFFSET];
		legacy_u8 *masks = shape + SHAPE3D_HEADER_SIZE + count * SHAPE3D_VERTEX_SIZE;
		memset(masks, 0xff, primitives * TEST_PRIMITIVE_MASK_BYTES);
		legacy_u8 *primitive = masks + primitives * TEST_PRIMITIVE_MASK_BYTES;
		for (legacy_u16 index = 0; index < primitives; index++) {
			primitive[0] = SHAPE3D_PRIMITIVE_POLYGON_FIRST;
			primitive[1] = 0;
			primitive[2] = 4;
			primitive[3] =
				(legacy_u8)((model == 0 ? TEST_VERTEX_COUNT : TEST_FIRST_WHEEL_VERTEX) + index);
			primitive[4] = primitive[3] + 1U;
			primitive[5] = primitive[3] + 2U;
			primitive += 6U;
		}
	}
}

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
	if (showroom_fixture_enabled != 0) {
		initialize_showroom_fixture(resource, name[2] == 'D');
		memcpy(original_resources[allocation_count - 1U], resource, TEST_RESOURCE_BYTES);
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

static void assert_scaled_showroom(legacy_u16 showroom_index, legacy_u16 driving_index,
								   legacy_u16 reduced_index, legacy_u8 different_car)
{
	const struct SHAPE3D *showroom = &game3dshapes[showroom_index];
	const struct SHAPE3D *driving = &game3dshapes[driving_index];
	const struct SHAPE3D *reduced = &game3dshapes[reduced_index];
	assert(driving->shape3d_numverts == TEST_SHOWROOM_VERTEX_COUNT);
	assert(driving->shape3d_numprimitives == TEST_SHOWROOM_PRIMITIVE_COUNT);
	assert(driving->shape3d_vertex_bytes != showroom->shape3d_vertex_bytes);
	assert(driving->shape3d_primitives == showroom->shape3d_primitives);
	assert(driving->shape3d_visibility_masks == showroom->shape3d_visibility_masks);
	assert(driving->shape3d_front_facing_masks == showroom->shape3d_front_facing_masks);
	assert(memcmp(driving, reduced, sizeof(*driving)) == 0);
	const legacy_u8 *native_vertices = showroom->shape3d_vertex_bytes + TEST_SHAPE_BYTES;
	assert(memcmp(driving->shape3d_vertex_bytes, native_vertices,
				  TEST_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE) == 0);
	struct VECTOR vertex;
	shape3d_vertex_read(driving, TEST_FIRST_WHEEL_VERTEX, &vertex);
	assert(vertex.x == (different_car != 0 ? -14 : -12));
	assert(vertex.y == 3 && vertex.z == 30);
	shape3d_vertex_read(driving, TEST_VERTEX_COUNT, &vertex);
	assert(vertex.x == (different_car != 0 ? -21 : -11));
	assert(vertex.y == 9 && vertex.z == -17);
	shape3d_vertex_read(driving, TEST_VERTEX_COUNT + 1U, &vertex);
	assert(vertex.x == 11 && vertex.y == 9 && vertex.z == -17);
	shape3d_vertex_read(driving, TEST_VERTEX_COUNT + 2U, &vertex);
	assert(vertex.x == -1 && vertex.y == 2 && vertex.z == 3);
	shape3d_vertex_read(driving, TEST_VERTEX_COUNT + 3U, &vertex);
	assert(vertex.x == 1 && vertex.y == 2 && vertex.z == 3);
}

static void test_showroom_driving_models(legacy_s8 *opponent)
{
	legacy_s8 player[] = "TEST";
	legacy_u8 has_opponent = opponent[0] != -1;
	legacy_u8 different_car = has_opponent && memcmp(player, opponent, 4U) != 0;
	legacy_u8 player_vertices[TEST_SHOWROOM_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE];
	legacy_u8 opponent_vertices[TEST_SHOWROOM_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE];
	struct VECTOR physics_wheels_before[TEST_WHEEL_COUNT][OWOOT_WHEEL_VERTEX_COUNT];
	struct VECTOR physics_wheels_after[OWOOT_WHEEL_VERTEX_COUNT];
	struct CARSTATE physics_car = {0};
	showroom_fixture_enabled = 1;
	owoot_enabled = 1;
	shape3d_set_showroom_car_enabled(0);
	shape3d_load_car_shapes(player, opponent);
	for (legacy_u16 wheel = 0; wheel < TEST_WHEEL_COUNT; wheel++) {
		assert(owoot_wheel_footprint(&physics_car, wheel, physics_wheels_before[wheel]) ==
			   OWOOT_WHEEL_VERTEX_COUNT);
	}
	shape3d_free_car_shapes();
	check_released_shapes();

	shape3d_set_showroom_car_enabled(1);
	shape3d_load_car_shapes(player, opponent);
	assert_scaled_showroom(PLAYER_CAR_LOW_SHAPE, PLAYER_CAR_WHEEL_SHAPE, PLAYER_CAR_HIGH_SHAPE, 0);
	struct SHAPE3D *driving = &game3dshapes[PLAYER_CAR_WHEEL_SHAPE];
	memcpy(player_vertices, driving->shape3d_vertex_bytes, sizeof(player_vertices));
	if (has_opponent) {
		assert_scaled_showroom(OPPONENT_CAR_LOW_SHAPE, OPPONENT_CAR_WHEEL_SHAPE,
							   OPPONENT_CAR_HIGH_SHAPE, different_car);
		assert(game3dshapes[OPPONENT_CAR_WHEEL_SHAPE].shape3d_vertex_bytes !=
			   driving->shape3d_vertex_bytes);
		memcpy(opponent_vertices, game3dshapes[OPPONENT_CAR_WHEEL_SHAPE].shape3d_vertex_bytes,
			   sizeof(opponent_vertices));
	}
	legacy_s16 suspension[TEST_WHEEL_COUNT] = {64, 128, -64, 192};
	shape3d_update_car_wheel_vertices(driving, TEST_FIRST_WHEEL_VERTEX, TEST_STEERING_ANGLE,
									  suspension, player_wheel_vertex_state,
									  player_base_wheel_vertices, player_front_wheel_centers);
	assert(memcmp(driving->shape3d_vertex_bytes, player_vertices,
				  TEST_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE) == 0);
	assert(memcmp(driving->shape3d_vertex_bytes + TEST_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
				  player_vertices + TEST_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
				  TEST_WHEEL_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE) != 0);
	assert(memcmp(driving->shape3d_vertex_bytes + TEST_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE,
				  player_vertices + TEST_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE,
				  TEST_BODY_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE) == 0);
	if (has_opponent) {
		assert(memcmp(game3dshapes[OPPONENT_CAR_WHEEL_SHAPE].shape3d_vertex_bytes,
					  opponent_vertices, sizeof(opponent_vertices)) == 0);
		memcpy(player_vertices, driving->shape3d_vertex_bytes, sizeof(player_vertices));
		shape3d_update_car_wheel_vertices(&game3dshapes[OPPONENT_CAR_WHEEL_SHAPE],
										  TEST_FIRST_WHEEL_VERTEX, -TEST_STEERING_ANGLE, suspension,
										  opponent_wheel_vertex_state, opponent_base_wheel_vertices,
										  opponent_front_wheel_centers);
		assert(memcmp(driving->shape3d_vertex_bytes, player_vertices, sizeof(player_vertices)) ==
			   0);
		assert(memcmp(car2resptr, original_resources[different_car != 0 ? 1U : 0U],
					  TEST_RESOURCE_BYTES) == 0);
	}
	assert(memcmp(carresptr, original_resources[0], TEST_RESOURCE_BYTES) == 0);
	for (legacy_u16 wheel = 0; wheel < TEST_WHEEL_COUNT; wheel++) {
		assert(owoot_wheel_footprint(&physics_car, wheel, physics_wheels_after) ==
			   OWOOT_WHEEL_VERTEX_COUNT);
		assert(memcmp(physics_wheels_before[wheel], physics_wheels_after,
					  sizeof(physics_wheels_after)) == 0);
	}
	shape3d_free_car_shapes();
	check_released_shapes();

	/* A later disabled launch restores native racing geometry and resource ownership. */
	shape3d_set_showroom_car_enabled(0);
	shape3d_load_car_shapes(player, opponent);
	assert(game3dshapes[PLAYER_CAR_WHEEL_SHAPE].shape3d_numverts == TEST_VERTEX_COUNT);
	assert(game3dshapes[PLAYER_CAR_WHEEL_SHAPE].shape3d_numprimitives == 1U);
	assert(game3dshapes[PLAYER_CAR_WHEEL_SHAPE].shape3d_vertex_bytes ==
		   (legacy_u8 *)locate_shape_fatal(carresptr, "car1") + SHAPE3D_HEADER_SIZE);
	assert(game3dshapes[PLAYER_CAR_HIGH_SHAPE].shape3d_vertex_bytes !=
		   game3dshapes[PLAYER_CAR_WHEEL_SHAPE].shape3d_vertex_bytes);
	shape3d_free_car_shapes();
	check_released_shapes();
	showroom_fixture_enabled = 0;
	owoot_enabled = 0;
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
	test_showroom_driving_models(same_opponent);
	test_showroom_driving_models(different_opponent);
	test_showroom_driving_models(no_opponent);

	return 0;
}
