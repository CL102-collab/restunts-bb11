#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "../c/externs.h"
#include "../c/math/projection.h"
#include "../c/fileio.h"
#include "../c/platform.h"
#include "../c/memmgr.h"
#include "../c/scene_resources.h"
#include "../c/car/car_model.h"
#include "../c/hires/hires.h"

#include "../c/hires/shape3d_mesh.h"
#include "../c/hires/shape3d_hires.h"
#include "../c/shape3d/shape3d_pipeline_internal.h"
#include "../c/shape2d/shape2d.h"
#include "../c/video/video_framebuffer.h"

#undef memcpy

#define TEST_MESH_HEADER_SIZE 8U
#define TEST_MESH_PRIMITIVE_COUNT_OFFSET 2U
#define TEST_MESH_PAINT_COUNT_OFFSET 4U
#define TEST_MESH_FRACTION_BITS_OFFSET 6U
#define TEST_MESH_STORAGE_PREFIX 1U
#define TEST_MESH_WIDE_VERTEX_COUNT 260U
#define TEST_MESH_PAINT_COUNT 2U
#define TEST_MESH_MATERIAL_LIMIT 129U
#define TEST_MESH_INVALID_FLAGS 4U
#define TEST_MESH_NEGATIVE_LIMIT LEGACY_S16_FROM_BITS(LEGACY_U16_SIGN_BIT)
#define TEST_MESH_VISIBILITY_MASK 0x12345678UL
#define TEST_MESH_FRONT_FACING_MASK 0x89abcdefUL
#define TEST_MESH_FRACTION_BITS 4U
#define TEST_MESH_FRACTION_DIVISOR (1U << TEST_MESH_FRACTION_BITS)
#define TEST_MESH_OBSOLETE_VERSION 1U
#define TEST_MESH_INVALID_VERSION 3U
#define TEST_MESH_ADDRESS_BOUNDARY_VERTEX (LEGACY_U16_MAX / SHAPE3D_VERTEX_SIZE + 1U)

struct TEST_MESH {
	legacy_u8 *storage, *data;
	size_t size, primitive_offset, index_offset;
};

static struct TEST_MESH make_mesh(legacy_u16 vertices, legacy_u16 paints, legacy_u8 type)
{
	static const legacy_u8 counts[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 2, 6};
	assert(type > SHAPE3D_PRIMITIVE_EMPTY && type <= SHAPE3D_PRIMITIVE_WHEEL);
	legacy_u16 count = counts[type];
	size_t width = vertices > LEGACY_U8_MAX ? sizeof(legacy_u16) : sizeof(legacy_u8);
	struct TEST_MESH mesh = {0};
	mesh.primitive_offset = TEST_MESH_HEADER_SIZE + (size_t)vertices * SHAPE3D_VERTEX_SIZE +
							2U * SHAPE3D_VISIBILITY_MASK_SIZE;
	mesh.index_offset = mesh.primitive_offset + SHAPE3D_PRIMITIVE_HEADER_SIZE + paints;
	mesh.size = mesh.index_offset + count * width;
	mesh.storage = calloc(1, TEST_MESH_STORAGE_PREFIX + mesh.size + 1U);
	assert(mesh.storage != NULL);
	mesh.data = mesh.storage + TEST_MESH_STORAGE_PREFIX;
	LEGACY_WRITE_U16_LE(mesh.data, vertices);
	LEGACY_WRITE_U16_LE(mesh.data + TEST_MESH_PRIMITIVE_COUNT_OFFSET, 1);
	LEGACY_WRITE_U16_LE(mesh.data + TEST_MESH_PAINT_COUNT_OFFSET, paints);
	legacy_u8 *visibility =
		mesh.data + TEST_MESH_HEADER_SIZE + (size_t)vertices * SHAPE3D_VERTEX_SIZE;
	LEGACY_WRITE_U32_LE(visibility, TEST_MESH_VISIBILITY_MASK);
	LEGACY_WRITE_U32_LE(visibility + SHAPE3D_VISIBILITY_MASK_SIZE, TEST_MESH_FRONT_FACING_MASK);
	mesh.data[mesh.primitive_offset] = type;
	mesh.data[mesh.primitive_offset + 1U] = 3;
	for (legacy_u16 paint = 0; paint < paints; paint++) {
		mesh.data[mesh.primitive_offset + SHAPE3D_PRIMITIVE_HEADER_SIZE + paint] =
			paint % TEST_MESH_MATERIAL_LIMIT;
	}
	for (legacy_u16 index = 0; index < count; index++) {
		legacy_u16 vertex = vertices - count + index;
		if (width == sizeof(legacy_u16)) {
			LEGACY_WRITE_U16_LE(mesh.data + mesh.index_offset + index * width, vertex);
		} else {
			mesh.data[mesh.index_offset + index] = (legacy_u8)vertex;
		}
	}
	return mesh;
}

static void expect_rejected(const legacy_u8 *data, size_t size)
{
	struct SHAPE3D shape, empty = {0};
	memset(&shape, 0xa5, sizeof(shape));
	assert(!hires_shape3d_mesh_decode(data, size, &shape));
	assert(memcmp(&shape, &empty, sizeof(shape)) == 0);
}

static void check_valid_meshes(void)
{
	const legacy_u16 counts[] = {3U, LEGACY_U8_MAX, LEGACY_U8_MAX + 1U, TEST_MESH_WIDE_VERTEX_COUNT,
								 LEGACY_U16_MAX};
	for (size_t sample = 0; sample < sizeof(counts) / sizeof(counts[0]); sample++) {
		struct TEST_MESH mesh =
			make_mesh(counts[sample], TEST_MESH_PAINT_COUNT, SHAPE3D_PRIMITIVE_POLYGON_FIRST);
		legacy_u8 *last =
			mesh.data + TEST_MESH_HEADER_SIZE + (size_t)(counts[sample] - 1U) * SHAPE3D_VERTEX_SIZE;
		LEGACY_WRITE_U16_LE(last + SHAPE3D_VERTEX_X_OFFSET, LEGACY_U16_SIGN_BIT);
		LEGACY_WRITE_U16_LE(last + SHAPE3D_VERTEX_Y_OFFSET, LEGACY_S16_MAX);
		LEGACY_WRITE_U16_LE(last + SHAPE3D_VERTEX_Z_OFFSET, LEGACY_U16_MAX);
		struct SHAPE3D shape;
		assert(hires_shape3d_mesh_decode(mesh.data, mesh.size, &shape));
		assert(shape.shape3d_numverts == counts[sample]);
		assert(shape.shape3d_numprimitives == 1);
		assert(shape.shape3d_numpaints == TEST_MESH_PAINT_COUNT);
		assert(shape.shape3d_vertex_bytes == mesh.data + TEST_MESH_HEADER_SIZE);
		assert(shape.shape3d_primitives == mesh.data + mesh.primitive_offset);
		assert(LEGACY_READ_U32_LE(shape.shape3d_visibility_masks) == TEST_MESH_VISIBILITY_MASK);
		assert(LEGACY_READ_U32_LE(shape.shape3d_front_facing_masks) == TEST_MESH_FRONT_FACING_MASK);
		struct VECTOR vertex;
		shape3d_vertex_read(&shape, counts[sample] - 1U, &vertex);
		assert(vertex.x == TEST_MESH_NEGATIVE_LIMIT && vertex.y == LEGACY_S16_MAX &&
			   vertex.z == -1);
		free(mesh.storage);
	}

	/* Every native primitive retains its existing arity with the wide indices. */
	for (legacy_u8 type = SHAPE3D_PRIMITIVE_POINT; type <= SHAPE3D_PRIMITIVE_WHEEL; type++) {
		struct TEST_MESH mesh = make_mesh(TEST_MESH_WIDE_VERTEX_COUNT, LEGACY_U8_MAX, type);
		struct SHAPE3D shape;
		assert(hires_shape3d_mesh_decode(mesh.data, mesh.size, &shape));
		assert(shape.shape3d_numpaints == LEGACY_U8_MAX);
		free(mesh.storage);
	}
}

static void check_fractional_mesh_headers(void)
{
	struct TEST_MESH mesh = make_mesh(TEST_MESH_WIDE_VERTEX_COUNT, TEST_MESH_PAINT_COUNT,
									  SHAPE3D_PRIMITIVE_POLYGON_FIRST);
	struct SHAPE3D shape;
	for (legacy_u16 bits = 0; bits <= HIRES_MESH_MAX_FRACTION_BITS; bits++) {
		LEGACY_WRITE_U16_LE(mesh.data + TEST_MESH_FRACTION_BITS_OFFSET, bits);
		assert(hires_shape3d_mesh_decode(mesh.data, mesh.size, &shape));
		assert(shape.shape3d_numverts == TEST_MESH_WIDE_VERTEX_COUNT);
		assert(hires_shape3d_car_vertices(&shape) == NULL);
	}
	LEGACY_WRITE_U16_LE(mesh.data + TEST_MESH_FRACTION_BITS_OFFSET,
						HIRES_MESH_MAX_FRACTION_BITS + 1U);
	expect_rejected(mesh.data, mesh.size);
	free(mesh.storage);
}

static void check_primitive_counts_past_byte_range(void)
{
	const legacy_u16 counts[] = {LEGACY_U8_MAX + 1U, LEGACY_U16_MAX};
	for (size_t sample = 0; sample < sizeof(counts) / sizeof(counts[0]); sample++) {
		size_t primitive_offset = TEST_MESH_HEADER_SIZE + SHAPE3D_VERTEX_SIZE +
								  (size_t)counts[sample] * 2U * SHAPE3D_VISIBILITY_MASK_SIZE;
		size_t stride = SHAPE3D_PRIMITIVE_HEADER_SIZE + 2U;
		size_t size = primitive_offset + (size_t)counts[sample] * stride;
		legacy_u8 *data = calloc(1, size);
		assert(data != NULL);
		LEGACY_WRITE_U16_LE(data, 1);
		LEGACY_WRITE_U16_LE(data + TEST_MESH_PRIMITIVE_COUNT_OFFSET, counts[sample]);
		LEGACY_WRITE_U16_LE(data + TEST_MESH_PAINT_COUNT_OFFSET, 1);
		for (legacy_u32 index = 0; index < counts[sample]; index++) {
			data[primitive_offset + index * stride] = SHAPE3D_PRIMITIVE_POINT;
		}
		struct SHAPE3D shape;
		assert(hires_shape3d_mesh_decode(data, size, &shape));
		assert(shape.shape3d_numprimitives == counts[sample]);
		expect_rejected(data, size - 1U);
		data[primitive_offset + (size_t)(counts[sample] - 1U) * stride +
			 SHAPE3D_PRIMITIVE_HEADER_SIZE] = TEST_MESH_MATERIAL_LIMIT;
		expect_rejected(data, size);
		free(data);
	}
}

static void check_coordinate_offsets_past_one_segment(void)
{
	struct TEST_MESH mesh =
		make_mesh(TEST_MESH_ADDRESS_BOUNDARY_VERTEX + 1U, 1, SHAPE3D_PRIMITIVE_POINT);
	struct SHAPE3D shape;
	assert(hires_shape3d_mesh_decode(mesh.data, mesh.size, &shape));
	const struct VECTOR value = {TEST_MESH_NEGATIVE_LIMIT, 1234, LEGACY_S16_MAX};
	shape3d_vertex_write(&shape, TEST_MESH_ADDRESS_BOUNDARY_VERTEX, &value);
	const legacy_u8 *encoded = shape.shape3d_vertex_bytes +
							   (size_t)TEST_MESH_ADDRESS_BOUNDARY_VERTEX * SHAPE3D_VERTEX_SIZE;
	assert(LEGACY_READ_S16_LE(encoded + SHAPE3D_VERTEX_X_OFFSET) == value.x);
	assert(LEGACY_READ_S16_LE(encoded + SHAPE3D_VERTEX_Y_OFFSET) == value.y);
	assert(LEGACY_READ_S16_LE(encoded + SHAPE3D_VERTEX_Z_OFFSET) == value.z);
	struct VECTOR result;
	shape3d_vertex_read(&shape, TEST_MESH_ADDRESS_BOUNDARY_VERTEX, &result);
	assert(result.x == value.x && result.y == value.y && result.z == value.z);
	for (legacy_u16 index = 0; index < SHAPE3D_VERTEX_SIZE; index++) {
		assert(shape.shape3d_vertex_bytes[index] == 0);
	}
	free(mesh.storage);
}

static void check_malformed_meshes(void)
{
	struct TEST_MESH mesh = make_mesh(TEST_MESH_WIDE_VERTEX_COUNT, TEST_MESH_PAINT_COUNT,
									  SHAPE3D_PRIMITIVE_POLYGON_FIRST);
	expect_rejected(NULL, mesh.size);
	assert(!hires_shape3d_mesh_decode(mesh.data, mesh.size, NULL));
	for (size_t size = 0; size < mesh.size; size++) {
		expect_rejected(mesh.data, size);
	}
	expect_rejected(mesh.data, mesh.size + 1U);

	const size_t zero_fields[] = {0, TEST_MESH_PRIMITIVE_COUNT_OFFSET,
								  TEST_MESH_PAINT_COUNT_OFFSET};
	for (size_t field = 0; field < sizeof(zero_fields) / sizeof(zero_fields[0]); field++) {
		legacy_u16 saved = LEGACY_READ_U16_LE(mesh.data + zero_fields[field]);
		LEGACY_WRITE_U16_LE(mesh.data + zero_fields[field], 0);
		expect_rejected(mesh.data, mesh.size);
		LEGACY_WRITE_U16_LE(mesh.data + zero_fields[field], saved);
	}
	LEGACY_WRITE_U16_LE(mesh.data + TEST_MESH_PAINT_COUNT_OFFSET, LEGACY_U8_MAX + 1U);
	expect_rejected(mesh.data, mesh.size);
	LEGACY_WRITE_U16_LE(mesh.data + TEST_MESH_PAINT_COUNT_OFFSET, TEST_MESH_PAINT_COUNT);
	LEGACY_WRITE_U16_LE(mesh.data + TEST_MESH_FRACTION_BITS_OFFSET,
						HIRES_MESH_MAX_FRACTION_BITS + 1U);
	expect_rejected(mesh.data, mesh.size);
	LEGACY_WRITE_U16_LE(mesh.data + TEST_MESH_FRACTION_BITS_OFFSET, 0);
	LEGACY_WRITE_U16_LE(mesh.data + TEST_MESH_PRIMITIVE_COUNT_OFFSET, LEGACY_U16_MAX);
	expect_rejected(mesh.data, mesh.size);
	LEGACY_WRITE_U16_LE(mesh.data + TEST_MESH_PRIMITIVE_COUNT_OFFSET, 1);

	const legacy_u8 invalid_types[] = {SHAPE3D_PRIMITIVE_EMPTY, SHAPE3D_PRIMITIVE_WHEEL + 1U,
									   LEGACY_U8_MAX};
	for (size_t index = 0; index < sizeof(invalid_types) / sizeof(invalid_types[0]); index++) {
		mesh.data[mesh.primitive_offset] = invalid_types[index];
		expect_rejected(mesh.data, mesh.size);
	}
	mesh.data[mesh.primitive_offset] = SHAPE3D_PRIMITIVE_POLYGON_FIRST;
	mesh.data[mesh.primitive_offset + 1U] = TEST_MESH_INVALID_FLAGS;
	expect_rejected(mesh.data, mesh.size);
	mesh.data[mesh.primitive_offset + 1U] = 3;
	mesh.data[mesh.primitive_offset + SHAPE3D_PRIMITIVE_HEADER_SIZE + 1U] =
		TEST_MESH_MATERIAL_LIMIT;
	expect_rejected(mesh.data, mesh.size);
	mesh.data[mesh.primitive_offset + SHAPE3D_PRIMITIVE_HEADER_SIZE + 1U] = 1;
	LEGACY_WRITE_U16_LE(mesh.data + mesh.index_offset, TEST_MESH_WIDE_VERTEX_COUNT);
	expect_rejected(mesh.data, mesh.size);
	LEGACY_WRITE_U16_LE(mesh.data + mesh.index_offset, LEGACY_U16_MAX);
	expect_rejected(mesh.data, mesh.size);
	free(mesh.storage);

	mesh = make_mesh(3, 1, SHAPE3D_PRIMITIVE_POLYGON_FIRST);
	mesh.data[mesh.index_offset] = 3;
	expect_rejected(mesh.data, mesh.size);
	free(mesh.storage);
}

#define TEST_CAR_MESH_COUNT 3U
#define TEST_CAR_COUNT 2U
#define TEST_CAR_VERTEX_COUNT 32U
#define TEST_CAR_FIRST_WHEEL_VERTEX 8U
#define TEST_CAR_WHEEL_VERTEX_COUNT 24U
#define TEST_CONTAINER_HEADER_SIZE 8U
#define TEST_CONTAINER_ENTRY_SIZE 12U
#define TEST_CONTAINER_VERSION_OFFSET 4U
#define TEST_CONTAINER_COUNT_OFFSET 6U
#define TEST_CONTAINER_OFFSET_FIELD 4U
#define TEST_CONTAINER_LENGTH_FIELD 8U
#define TEST_CONTAINER_INVALID_ENTRY_COUNT (TEST_CAR_MESH_COUNT + 1U)

struct TEST_CLASSIC_CARS {
	struct SHAPE3D shapes[TEST_CAR_COUNT][TEST_CAR_MESH_COUNT];
	const struct SHAPE3D *pointers[TEST_CAR_COUNT][TEST_CAR_MESH_COUNT];
	legacy_u8 vertices[TEST_CAR_COUNT][TEST_CAR_MESH_COUNT]
					  [TEST_CAR_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE];
};

static void prepare_classic_cars(struct TEST_CLASSIC_CARS *cars)
{
	memset(cars, 0, sizeof(*cars));
	for (legacy_u16 car = 0; car < TEST_CAR_COUNT; car++) {
		for (legacy_u16 model = 0; model < TEST_CAR_MESH_COUNT; model++) {
			struct SHAPE3D *shape = &cars->shapes[car][model];
			shape->shape3d_numverts = TEST_CAR_VERTEX_COUNT;
			shape->shape3d_numpaints = TEST_MESH_PAINT_COUNT;
			shape->shape3d_vertex_bytes = cars->vertices[car][model];
			cars->pointers[car][model] = shape;
			for (legacy_u16 index = TEST_CAR_FIRST_WHEEL_VERTEX; index < TEST_CAR_VERTEX_COUNT;
				 index++) {
				const struct VECTOR vertex = {(legacy_s16)index, (legacy_s16)(index + 1U),
											  (legacy_s16)(index + 2U)};
				shape3d_vertex_write(shape, index, &vertex);
			}
		}
	}
}

static legacy_u8 *make_container(const struct TEST_CLASSIC_CARS *cars, size_t *size)
{
	struct TEST_MESH mesh = make_mesh(TEST_MESH_ADDRESS_BOUNDARY_VERTEX + 3U, TEST_MESH_PAINT_COUNT,
									  SHAPE3D_PRIMITIVE_POLYGON_FIRST);
	size_t offset = TEST_CONTAINER_HEADER_SIZE + TEST_CAR_MESH_COUNT * TEST_CONTAINER_ENTRY_SIZE;
	*size = offset + TEST_CAR_MESH_COUNT * mesh.size;
	assert(*size > LEGACY_U16_MAX);
	legacy_u8 *data = calloc(1, *size);
	assert(data != NULL);
	memcpy(data, "H3D", sizeof("H3D"));
	LEGACY_WRITE_U16_LE(data + TEST_CONTAINER_VERSION_OFFSET, HIRES_MESH_VERSION);
	LEGACY_WRITE_U16_LE(data + TEST_CONTAINER_COUNT_OFFSET, TEST_CAR_MESH_COUNT);
	for (legacy_u16 index = 0; index < TEST_CAR_MESH_COUNT; index++) {
		legacy_u8 *entry = data + TEST_CONTAINER_HEADER_SIZE + index * TEST_CONTAINER_ENTRY_SIZE;
		memcpy(entry, "car0", sizeof("car0") - 1U);
		entry[3] += index;
		LEGACY_WRITE_U32_LE(entry + TEST_CONTAINER_OFFSET_FIELD, offset);
		LEGACY_WRITE_U32_LE(entry + TEST_CONTAINER_LENGTH_FIELD, mesh.size);
		memcpy(data + offset, mesh.data, mesh.size);
		memcpy(data + offset + TEST_MESH_HEADER_SIZE +
				   TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
			   cars->vertices[0][index] + TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
			   TEST_CAR_WHEEL_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE);
		offset += mesh.size;
	}
	free(mesh.storage);
	return data;
}

static void write_container(const legacy_char *path, const legacy_u8 *data, size_t size)
{
	FILE *file = fopen(path, "wb");
	assert(file != NULL);
	assert(fwrite(data, 1, size, file) == size);
	assert(fclose(file) == 0);
}

static void assert_classic_fallback(const struct TEST_CLASSIC_CARS *cars, legacy_u16 car)
{
	for (legacy_u16 index = 0; index < TEST_CAR_MESH_COUNT; index++) {
		assert(hires_shape3d_car_mesh(cars->pointers[car][index]) == cars->pointers[car][index]);
	}
}

static void check_car_mesh_lifetime(void)
{
	struct TEST_CLASSIC_CARS cars;
	prepare_classic_cars(&cars);
	size_t size;
	legacy_u8 *data = make_container(&cars, &size);
	legacy_char directory[FILE_CAR_PATH_SIZE], car_directory[FILE_CAR_PATH_SIZE];
	legacy_char path[FILE_CAR_PATH_SIZE], loose_path[FILE_CAR_PATH_SIZE];
	legacy_char classic_path[FILE_CAR_PATH_SIZE];
	assert(SDL_snprintf(directory, sizeof(directory), "hires-mesh-%" LEGACY_PRIu64,
						(legacy_u64)SDL_GetTicksNS()) > 0);
	assert(SDL_CreateDirectory(directory));
	assert(SDL_snprintf(car_directory, sizeof(car_directory), "%s/cars", directory) > 0);
	assert(SDL_CreateDirectory(car_directory));
	assert(SDL_snprintf(path, sizeof(path), "%s/StWiDe.H3d", car_directory) > 0);
	assert(SDL_snprintf(loose_path, sizeof(loose_path), "%s/STWIDE.H3D", directory) > 0);
	assert(SDL_snprintf(classic_path, sizeof(classic_path), "%s/STWIDE.3SH", directory) > 0);
	file_car_set_base((const legacy_s8 *)directory);
	write_container(path, data, size);
	assert(file_open_resource((const legacy_s8 *)"STWIDE.3SH") == 0);
	assert_classic_fallback(&cars, 0);
	assert(hires_shape3d_car_mesh(NULL) == NULL);

	for (legacy_u16 cycle = 0; cycle < 3U; cycle++) {
		for (legacy_u16 car = 0; car < TEST_CAR_COUNT; car++) {
			hires_shape3d_load_car_meshes(car, (const legacy_s8 *)"WIDE", cars.pointers[car]);
		}
		for (legacy_u16 index = 0; index < TEST_CAR_MESH_COUNT; index++) {
			struct SHAPE3D *player = hires_shape3d_car_mesh(cars.pointers[0][index]);
			struct SHAPE3D *opponent = hires_shape3d_car_mesh(cars.pointers[1][index]);
			assert(player != cars.pointers[0][index] && opponent != cars.pointers[1][index]);
			assert(player != opponent &&
				   player->shape3d_vertex_bytes != opponent->shape3d_vertex_bytes);
			assert(player->shape3d_numverts == TEST_MESH_ADDRESS_BOUNDARY_VERTEX + 3U);
			assert(opponent->shape3d_numverts == player->shape3d_numverts);
			assert(LEGACY_READ_U32_LE(player->shape3d_visibility_masks) ==
				   TEST_MESH_VISIBILITY_MASK);
			assert(LEGACY_READ_U32_LE(player->shape3d_front_facing_masks) ==
				   TEST_MESH_FRONT_FACING_MASK);
			assert(hires_shape3d_vertex_index(player,
											  player->shape3d_primitives +
												  SHAPE3D_PRIMITIVE_HEADER_SIZE +
												  player->shape3d_numpaints,
											  2) == player->shape3d_numverts - 1U);
			assert(memcmp(player->shape3d_vertex_bytes, opponent->shape3d_vertex_bytes,
						  (size_t)player->shape3d_numverts * SHAPE3D_VERTEX_SIZE) == 0);
		}
		hires_set_enabled(0);
		assert(cars.shapes[0][1].shape3d_numverts == TEST_CAR_VERTEX_COUNT);
		assert(hires_shape3d_car_mesh(cars.pointers[0][1]) != cars.pointers[0][1]);
		hires_set_enabled(1);
		hires_shape3d_free_car_meshes();
		hires_shape3d_free_car_meshes();
		assert_classic_fallback(&cars, 0);
		assert_classic_fallback(&cars, 1);
	}

	for (legacy_u16 car = 0; car < TEST_CAR_COUNT; car++) {
		hires_shape3d_load_car_meshes(car, (const legacy_s8 *)"WIDE", cars.pointers[car]);
	}
	/* Lookup itself stays read-only; wheel updates synchronize just the matching model. */
	const struct VECTOR wheel = {-123, 45, 67};
	shape3d_vertex_write(&cars.shapes[0][1], TEST_CAR_FIRST_WHEEL_VERTEX, &wheel);
	struct VECTOR vertex;
	shape3d_vertex_read(hires_shape3d_car_mesh(cars.pointers[0][1]), TEST_CAR_FIRST_WHEEL_VERTEX,
						&vertex);
	assert(vertex.x != wheel.x);
	hires_shape3d_update_car_mesh(cars.pointers[0][1]);
	shape3d_vertex_read(hires_shape3d_car_mesh(cars.pointers[0][1]), TEST_CAR_FIRST_WHEEL_VERTEX,
						&vertex);
	assert(vertex.x == wheel.x && vertex.y == wheel.y && vertex.z == wheel.z);
	shape3d_vertex_read(hires_shape3d_car_mesh(cars.pointers[1][1]), TEST_CAR_FIRST_WHEEL_VERTEX,
						&vertex);
	assert(vertex.x == TEST_CAR_FIRST_WHEEL_VERTEX);
	shape3d_vertex_read(hires_shape3d_car_mesh(cars.pointers[0][0]), TEST_CAR_FIRST_WHEEL_VERTEX,
						&vertex);
	assert(vertex.x == TEST_CAR_FIRST_WHEEL_VERTEX);
	hires_shape3d_load_car_meshes(0, (const legacy_s8 *)"NONE", cars.pointers[0]);
	assert_classic_fallback(&cars, 0);
	assert(hires_shape3d_car_mesh(cars.pointers[1][1]) != cars.pointers[1][1]);
	hires_shape3d_free_car_meshes();
	prepare_classic_cars(&cars);

	/* A loose classic override blocks lower-priority enhanced assets. An explicit
	 * enhanced file alongside it is still eligible. */
	write_container(classic_path, data, 1);
	hires_shape3d_load_car_meshes(0, (const legacy_s8 *)"WIDE", cars.pointers[0]);
	assert_classic_fallback(&cars, 0);
	write_container(loose_path, data, size);
	hires_shape3d_load_car_meshes(0, (const legacy_s8 *)"WIDE", cars.pointers[0]);
	assert(hires_shape3d_car_mesh(cars.pointers[0][1]) != cars.pointers[0][1]);
	hires_shape3d_free_car_meshes();
	assert(SDL_RemovePath(loose_path));
	assert(SDL_RemovePath(classic_path));

	/* A loose override takes precedence over a valid file in cars/. */
	write_container(loose_path, data, TEST_CONTAINER_HEADER_SIZE - 1U);
	hires_shape3d_load_car_meshes(0, (const legacy_s8 *)"WIDE", cars.pointers[0]);
	assert_classic_fallback(&cars, 0);
	assert(SDL_RemovePath(loose_path));
	hires_shape3d_load_car_meshes(0, (const legacy_s8 *)"WIDE", cars.pointers[0]);
	assert(hires_shape3d_car_mesh(cars.pointers[0][1]) != cars.pointers[0][1]);
	hires_shape3d_free_car_meshes();

	/* Reject invalid directory entries atomically, even after a valid first entry. */
	legacy_u8 *second = data + TEST_CONTAINER_HEADER_SIZE + TEST_CONTAINER_ENTRY_SIZE;
	const size_t bad_fields[] = {
		0,
		TEST_CONTAINER_VERSION_OFFSET,
		TEST_CONTAINER_VERSION_OFFSET,
		TEST_CONTAINER_COUNT_OFFSET,
		TEST_CONTAINER_HEADER_SIZE + TEST_CONTAINER_ENTRY_SIZE,
		TEST_CONTAINER_HEADER_SIZE + TEST_CONTAINER_ENTRY_SIZE + TEST_CONTAINER_OFFSET_FIELD,
		TEST_CONTAINER_HEADER_SIZE + TEST_CONTAINER_ENTRY_SIZE + TEST_CONTAINER_LENGTH_FIELD};
	const legacy_u32 bad_values[] = {
		0,
		TEST_MESH_OBSOLETE_VERSION,
		TEST_MESH_INVALID_VERSION,
		TEST_CONTAINER_INVALID_ENTRY_COUNT,
		LEGACY_READ_U32_LE(data + TEST_CONTAINER_HEADER_SIZE),
		LEGACY_READ_U32_LE(data + TEST_CONTAINER_HEADER_SIZE + TEST_CONTAINER_OFFSET_FIELD),
		LEGACY_U32_MAX};
	const size_t field_widths[] = {sizeof(legacy_u32), sizeof(legacy_u16), sizeof(legacy_u16),
								   sizeof(legacy_u16), sizeof(legacy_u32), sizeof(legacy_u32),
								   sizeof(legacy_u32)};
	for (size_t index = 0; index < sizeof(bad_fields) / sizeof(bad_fields[0]); index++) {
		legacy_u32 previous = LEGACY_READ_U32_LE(data + bad_fields[index]);
		if (field_widths[index] == sizeof(legacy_u16)) {
			LEGACY_WRITE_U16_LE(data + bad_fields[index], bad_values[index]);
		} else {
			LEGACY_WRITE_U32_LE(data + bad_fields[index], bad_values[index]);
		}
		write_container(path, data, size);
		hires_shape3d_load_car_meshes(0, (const legacy_s8 *)"WIDE", cars.pointers[0]);
		assert_classic_fallback(&cars, 0);
		if (field_widths[index] == sizeof(legacy_u16)) {
			LEGACY_WRITE_U16_LE(data + bad_fields[index], previous);
		} else {
			LEGACY_WRITE_U32_LE(data + bad_fields[index], previous);
		}
	}
	legacy_u32 second_offset = LEGACY_READ_U32_LE(second + TEST_CONTAINER_OFFSET_FIELD);
	legacy_u8 *wheel_byte = data + second_offset + TEST_MESH_HEADER_SIZE +
							TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE;
	(*wheel_byte)++;
	write_container(path, data, size);
	hires_shape3d_load_car_meshes(0, (const legacy_s8 *)"WIDE", cars.pointers[0]);
	assert_classic_fallback(&cars, 0);
	(*wheel_byte)--;
	write_container(path, data, size);
	hires_shape3d_load_car_meshes(0, (const legacy_s8 *)"WIDE", cars.pointers[0]);
	assert(hires_shape3d_car_mesh(cars.pointers[0][1]) != cars.pointers[0][1]);
	hires_shape3d_free_car_meshes();
	/* Four-character car IDs retain the same underscore support as classic assets. */
	legacy_char underscore_path[FILE_CAR_PATH_SIZE];
	assert(SDL_snprintf(underscore_path, sizeof(underscore_path), "%s/STW_01.H3D", car_directory) >
		   0);
	write_container(underscore_path, data, size);
	hires_shape3d_load_car_meshes(0, (const legacy_s8 *)"W_01", cars.pointers[0]);
	assert(hires_shape3d_car_mesh(cars.pointers[0][1]) != cars.pointers[0][1]);
	hires_shape3d_load_car_meshes(0, NULL, cars.pointers[0]);
	assert_classic_fallback(&cars, 0);
	hires_shape3d_free_car_meshes();
	assert(SDL_RemovePath(underscore_path));
	file_car_set_base(NULL);
	assert(SDL_RemovePath(path));
	assert(SDL_RemovePath(car_directory));
	assert(SDL_RemovePath(directory));
	free(data);
}

#define TEST_MCMURTRY_CLASSIC_PRIMITIVES 247U
#define TEST_MCMURTRY_STEERING_ANGLE 64
#define TEST_MCMURTRY_OPPONENT_STEERING_ANGLE -96
#define TEST_MCMURTRY_WHEEL_BYTES (TEST_CAR_WHEEL_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE)

extern void full_data_initialize(void);

static struct SHAPE3D *check_real_mcmurtry_mesh(struct SHAPE3D *classic, legacy_u8 fractional)
{
	struct SHAPE3D *mesh = hires_shape3d_car_mesh(classic);
	assert(mesh != classic);
	assert(mesh->shape3d_numverts > LEGACY_U8_MAX);
	assert(mesh->shape3d_numprimitives > LEGACY_U8_MAX);
	assert(mesh->shape3d_numpaints == classic->shape3d_numpaints);
	assert(memcmp(classic->shape3d_vertex_bytes + TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
				  mesh->shape3d_vertex_bytes + TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
				  TEST_MCMURTRY_WHEEL_BYTES) == 0);
	const struct SHAPE3D_HIRES_VECTOR *vertices = hires_shape3d_car_vertices(mesh);
	assert((vertices != NULL) == (fractional != 0));
	assert(vertices == hires_shape3d_car_vertices(classic));
	for (legacy_u16 index = 0; vertices != NULL && index < TEST_CAR_VERTEX_COUNT; index++) {
		struct VECTOR vertex;
		shape3d_vertex_read(mesh, index, &vertex);
		assert(vertices[index].x == vertex.x && vertices[index].y == vertex.y &&
			   vertices[index].z == vertex.z);
	}
	return mesh;
}

static void check_real_car_cache_reload(void)
{
	full_data_initialize();
	mmgr_init_conventional_arena();
	init_polyinfo();
	file_car_set_base((const legacy_s8 *)TEST_HIRES_MESH_ASSET_DIRECTORY);
	legacy_s8 car_id[] = "MCMU";
	legacy_s8 no_opponent[] = {-1, 0, 0, 0, 0};
	legacy_s16 player_suspension[] = {64, -128, 192, -64};
	legacy_s16 opponent_suspension[] = {-192, 128, 64, 256};
	struct SHAPE3D *player = &game3dshapes[PLAYER_CAR_WHEEL_SHAPE];
	struct SHAPE3D *opponent = &game3dshapes[OPPONENT_CAR_WHEEL_SHAPE];
	legacy_u8 original[TEST_MCMURTRY_WHEEL_BYTES];
	legacy_u8 player_before[TEST_MCMURTRY_WHEEL_BYTES], opponent_before[TEST_MCMURTRY_WHEEL_BYTES];

	shape3d_load_car_shapes(car_id, no_opponent);
	assert(player->shape3d_numverts == LEGACY_U8_MAX);
	assert(player->shape3d_numprimitives == TEST_MCMURTRY_CLASSIC_PRIMITIVES);
	struct SHAPE3D *enhanced = check_real_mcmurtry_mesh(player, 1);
	memcpy(original,
		   player->shape3d_vertex_bytes + TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
		   sizeof(original));
	shape3d_update_car_wheel_vertices(
		player, TEST_CAR_FIRST_WHEEL_VERTEX, TEST_MCMURTRY_STEERING_ANGLE, player_suspension,
		player_wheel_vertex_state, player_base_wheel_vertices, player_front_wheel_centers);
	assert(memcmp(original,
				  player->shape3d_vertex_bytes + TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
				  sizeof(original)) != 0);
	check_real_mcmurtry_mesh(player, 1);
	shape3d_free_car_shapes();
	assert(hires_shape3d_car_mesh(player) == player);

	/* Free restores steering to neutral inside the cached classic resource.
	 * Its front controls can now be reflected relative to the authored file. */
	for (legacy_u16 cycle = 0; cycle < TEST_CAR_MESH_COUNT; cycle++) {
		shape3d_load_car_shapes(car_id, car_id);
		if (cycle == 0) {
			assert(memcmp(original,
						  player->shape3d_vertex_bytes +
							  TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
						  sizeof(original)) != 0);
		}
		enhanced = check_real_mcmurtry_mesh(player, 1);
		struct SHAPE3D *opponent_enhanced = check_real_mcmurtry_mesh(opponent, 1);
		struct SHAPE3D *showroom = check_real_mcmurtry_mesh(&game3dshapes[PLAYER_CAR_LOW_SHAPE], 0);
		struct SHAPE3D *far_mesh =
			check_real_mcmurtry_mesh(&game3dshapes[PLAYER_CAR_HIGH_SHAPE], 1);
		assert(showroom->shape3d_numprimitives > enhanced->shape3d_numprimitives);
		assert(enhanced->shape3d_numprimitives > far_mesh->shape3d_numprimitives);
		assert(player->shape3d_vertex_bytes != opponent->shape3d_vertex_bytes);
		assert(enhanced->shape3d_vertex_bytes != opponent_enhanced->shape3d_vertex_bytes);
		const struct SHAPE3D_HIRES_VECTOR *player_cache = hires_shape3d_car_vertices(enhanced);
		const struct SHAPE3D_HIRES_VECTOR *opponent_cache =
			hires_shape3d_car_vertices(opponent_enhanced);
		assert(player_cache != opponent_cache);
		struct SHAPE3D_HIRES_VECTOR original_body = player_cache[TEST_CAR_VERTEX_COUNT];
		struct SHAPE3D_HIRES_VECTOR opponent_wheel = opponent_cache[TEST_CAR_FIRST_WHEEL_VERTEX];
		memcpy(opponent_before,
			   opponent->shape3d_vertex_bytes + TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
			   sizeof(opponent_before));
		shape3d_update_car_wheel_vertices(
			player, TEST_CAR_FIRST_WHEEL_VERTEX, TEST_MCMURTRY_STEERING_ANGLE, player_suspension,
			player_wheel_vertex_state, player_base_wheel_vertices, player_front_wheel_centers);
		check_real_mcmurtry_mesh(player, 1);
		assert(player_cache == hires_shape3d_car_vertices(enhanced));
		assert(memcmp(&original_body, &player_cache[TEST_CAR_VERTEX_COUNT],
					  sizeof(original_body)) == 0);
		assert(memcmp(&opponent_wheel, &opponent_cache[TEST_CAR_FIRST_WHEEL_VERTEX],
					  sizeof(opponent_wheel)) == 0);
		assert(memcmp(opponent_before,
					  opponent->shape3d_vertex_bytes +
						  TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
					  sizeof(opponent_before)) == 0);
		assert(memcmp(opponent_before,
					  opponent_enhanced->shape3d_vertex_bytes +
						  TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
					  sizeof(opponent_before)) == 0);
		memcpy(player_before,
			   player->shape3d_vertex_bytes + TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
			   sizeof(player_before));
		shape3d_update_car_wheel_vertices(
			opponent, TEST_CAR_FIRST_WHEEL_VERTEX, TEST_MCMURTRY_OPPONENT_STEERING_ANGLE,
			opponent_suspension, opponent_wheel_vertex_state, opponent_base_wheel_vertices,
			opponent_front_wheel_centers);
		assert(memcmp(opponent_before,
					  opponent->shape3d_vertex_bytes +
						  TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
					  sizeof(opponent_before)) != 0);
		check_real_mcmurtry_mesh(opponent, 1);
		assert(
			memcmp(player_before,
				   player->shape3d_vertex_bytes + TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
				   sizeof(player_before)) == 0);
		assert(memcmp(player_before,
					  enhanced->shape3d_vertex_bytes +
						  TEST_CAR_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
					  sizeof(player_before)) == 0);
		shape3d_free_car_shapes();
		assert(hires_shape3d_car_mesh(player) == player);
		assert(hires_shape3d_car_mesh(opponent) == opponent);
	}
	shape3d_free_car_shapes();
	file_car_set_base(NULL);
}

#define TEST_FRACTION_RENDER_WIDTH 320U
#define TEST_FRACTION_RENDER_HEIGHT 200U
#define TEST_FRACTION_RENDER_SCALE 2
#define TEST_FRACTION_RENDER_FOCAL 160U
#define TEST_FRACTION_RENDER_DEPTH 100
#define TEST_FRACTION_RENDER_BACKGROUND 3U
#define TEST_FRACTION_RENDER_WIDTH_LIMIT 40
#define TEST_FRACTION_RENDER_MIN_PIXELS 256U

static legacy_s16 fractional_render_width(struct SHAPE3D *classic)
{
	legacy_u8 *screen = dos_memory_make_pointer(0xA000U, 0);
	legacy_u8 rows[TEST_FRACTION_RENDER_HEIGHT * LEGACY_WORD_BYTES];
	struct SPRITE target = {0};
	target.sprite_bitmapptr = (struct SHAPE2D *)screen;
	target.sprite_lineofs = rows;
	target.sprite_right = target.sprite_pitch = target.sprite_buffer_width =
		target.sprite_raster_right = TEST_FRACTION_RENDER_WIDTH;
	target.sprite_bottom = TEST_FRACTION_RENDER_HEIGHT;
	for (legacy_u16 row = 0; row < TEST_FRACTION_RENDER_HEIGHT; row++) {
		LEGACY_WRITE_U16_LE(rows + row * LEGACY_WORD_BYTES, row * TEST_FRACTION_RENDER_WIDTH);
	}
	hires_shutdown();
	memset(screen, TEST_FRACTION_RENDER_BACKGROUND,
		   TEST_FRACTION_RENDER_WIDTH * TEST_FRACTION_RENDER_HEIGHT);
	hires_set_enabled(1);
	hires_set_render_scale(TEST_FRACTION_RENDER_SCALE);
	sprite_select_target(&target);
	struct RECTANGLE clip = {0, TEST_FRACTION_RENDER_WIDTH, 0, TEST_FRACTION_RENDER_HEIGHT};
	select_cliprect_rotate(0, 0, 0, &clip, 0);
	projection_center_x = TEST_FRACTION_RENDER_WIDTH / 2U;
	projection_center_y = TEST_FRACTION_RENDER_HEIGHT / 2U;
	projection_focal_length_x = projection_focal_length_y = TEST_FRACTION_RENDER_FOCAL;
	material_clrlist_ptr_cpy = material_clrlist_ptr;
	material_clrlist2_ptr_cpy = material_clrlist2_ptr;
	material_patlist_ptr_cpy = material_patlist_ptr;
	material_patlist2_ptr_cpy = material_patlist2_ptr;
	polyinfo_reset();
	struct RECTANGLE bounds = {TEST_FRACTION_RENDER_WIDTH, 0, TEST_FRACTION_RENDER_HEIGHT, 0};
	struct TRANSFORMEDSHAPE3D instance = {0};
	instance.shapeptr = classic;
	instance.rectptr = &bounds;
	instance.ts_flags = SHAPE3D_USE_BOUNDING_RECT_FLAG | SHAPE3D_PRETRANSFORMED_FLAG;
	instance.pos.z = TEST_FRACTION_RENDER_DEPTH;
	instance.culling_distance = TEST_FRACTION_RENDER_DEPTH;
	assert(shape3d_transform_and_queue(&instance) == 0);
	assert(shape3d_queued_primitive_count() == 1);
	shape3d_render_queued_primitives();
	legacy_s32 width, height;
	const legacy_u8 *image = video_framebuffer(screen, &width, &height);
	assert(width == TEST_FRACTION_RENDER_WIDTH * TEST_FRACTION_RENDER_SCALE &&
		   height == TEST_FRACTION_RENDER_HEIGHT * TEST_FRACTION_RENDER_SCALE);
	legacy_u32 covered = 0;
	for (legacy_s32 pixel = 0; pixel < width * height; pixel++) {
		covered += image[pixel] != TEST_FRACTION_RENDER_BACKGROUND;
	}
	assert(covered > TEST_FRACTION_RENDER_MIN_PIXELS);
	hires_shutdown();
	return bounds.right - bounds.left;
}

static void check_fractional_car_cache(void)
{
	struct TEST_CLASSIC_CARS cars;
	prepare_classic_cars(&cars);
	size_t size;
	legacy_u8 *data = make_container(&cars, &size);
	const struct VECTOR body = {123, -140, 7};
	const struct VECTOR triangle[] = {{-320, -320, 1600}, {320, -320, 1600}, {0, 320, 1600}};
	for (legacy_u16 index = 0; index < TEST_CAR_MESH_COUNT; index++) {
		legacy_u8 *entry = data + TEST_CONTAINER_HEADER_SIZE + index * TEST_CONTAINER_ENTRY_SIZE;
		legacy_u8 *payload = data + LEGACY_READ_U32_LE(entry + TEST_CONTAINER_OFFSET_FIELD);
		LEGACY_WRITE_U16_LE(payload + TEST_MESH_FRACTION_BITS_OFFSET, TEST_MESH_FRACTION_BITS);
		struct SHAPE3D shape;
		assert(hires_shape3d_mesh_decode(
			payload, LEGACY_READ_U32_LE(entry + TEST_CONTAINER_LENGTH_FIELD), &shape));
		shape3d_vertex_write(&shape, TEST_CAR_VERTEX_COUNT, &body);
		for (legacy_u16 corner = 0; corner < SDL_arraysize(triangle); corner++) {
			shape3d_vertex_write(&shape, shape.shape3d_numverts - SDL_arraysize(triangle) + corner,
								 &triangle[corner]);
		}
		LEGACY_WRITE_U32_LE(shape.shape3d_visibility_masks, LEGACY_U32_MAX);
		LEGACY_WRITE_U32_LE(shape.shape3d_front_facing_masks, 0);
		shape.shape3d_primitives[1] = SHAPE3D_PRIMITIVE_ALWAYS_VISIBLE_FLAG;
	}
	legacy_char directory[FILE_CAR_PATH_SIZE], path[FILE_CAR_PATH_SIZE];
	assert(SDL_snprintf(directory, sizeof(directory), "hires-fractional-%" LEGACY_PRIu64,
						(legacy_u64)SDL_GetTicksNS()) > 0);
	assert(SDL_CreateDirectory(directory));
	assert(SDL_snprintf(path, sizeof(path), "%s/STFRAC.H3D", directory) > 0);
	file_car_set_base((const legacy_s8 *)directory);
	write_container(path, data, size);
	for (legacy_u16 car = 0; car < TEST_CAR_COUNT; car++) {
		hires_shape3d_load_car_meshes(car, (const legacy_s8 *)"FRAC", cars.pointers[car]);
	}
	struct SHAPE3D *player = hires_shape3d_car_mesh(cars.pointers[0][1]);
	struct SHAPE3D *opponent = hires_shape3d_car_mesh(cars.pointers[1][1]);
	const struct SHAPE3D_HIRES_VECTOR *vertices = hires_shape3d_car_vertices(player);
	const struct SHAPE3D_HIRES_VECTOR *other = hires_shape3d_car_vertices(opponent);
	assert(player != cars.pointers[0][1] && opponent != cars.pointers[1][1]);
	assert(vertices != NULL && other != NULL && vertices != other);
	assert(vertices == hires_shape3d_car_vertices(cars.pointers[0][1]));
	assert(vertices[TEST_CAR_VERTEX_COUNT].x == (legacy_f64)body.x / TEST_MESH_FRACTION_DIVISOR &&
		   vertices[TEST_CAR_VERTEX_COUNT].y == (legacy_f64)body.y / TEST_MESH_FRACTION_DIVISOR &&
		   vertices[TEST_CAR_VERTEX_COUNT].z == (legacy_f64)body.z / TEST_MESH_FRACTION_DIVISOR);
	for (legacy_u16 index = 0; index < TEST_CAR_VERTEX_COUNT; index++) {
		struct VECTOR vertex;
		shape3d_vertex_read(player, index, &vertex);
		assert(vertices[index].x == vertex.x && vertices[index].y == vertex.y &&
			   vertices[index].z == vertex.z);
	}
	legacy_s16 fractional_width = fractional_render_width(&cars.shapes[0][1]);
	assert(fractional_width < TEST_FRACTION_RENDER_WIDTH_LIMIT);

	const struct VECTOR wheel = {-123, 45, 67};
	shape3d_vertex_write(&cars.shapes[0][1], TEST_CAR_FIRST_WHEEL_VERTEX, &wheel);
	hires_shape3d_update_car_mesh(cars.pointers[0][1]);
	assert(vertices[TEST_CAR_FIRST_WHEEL_VERTEX].x == wheel.x &&
		   vertices[TEST_CAR_FIRST_WHEEL_VERTEX].y == wheel.y &&
		   vertices[TEST_CAR_FIRST_WHEEL_VERTEX].z == wheel.z);
	assert(other[TEST_CAR_FIRST_WHEEL_VERTEX].x == TEST_CAR_FIRST_WHEEL_VERTEX);
	assert(vertices[TEST_CAR_VERTEX_COUNT].x == (legacy_f64)body.x / TEST_MESH_FRACTION_DIVISOR);
	prepare_classic_cars(&cars);

	/* One malformed last entry rejects every replacement for that car while
	 * leaving the independently loaded opponent's fractional cache intact. */
	legacy_u8 *last =
		data + TEST_CONTAINER_HEADER_SIZE + (TEST_CAR_MESH_COUNT - 1U) * TEST_CONTAINER_ENTRY_SIZE;
	legacy_u8 *last_header = data + LEGACY_READ_U32_LE(last + TEST_CONTAINER_OFFSET_FIELD);
	LEGACY_WRITE_U16_LE(last_header + TEST_MESH_FRACTION_BITS_OFFSET,
						HIRES_MESH_MAX_FRACTION_BITS + 1U);
	write_container(path, data, size);
	hires_shape3d_load_car_meshes(0, (const legacy_s8 *)"FRAC", cars.pointers[0]);
	assert_classic_fallback(&cars, 0);
	assert(hires_shape3d_car_vertices(cars.pointers[0][1]) == NULL);
	assert(hires_shape3d_car_vertices(opponent) == other);
	assert(other[TEST_CAR_VERTEX_COUNT].x == (legacy_f64)body.x / TEST_MESH_FRACTION_DIVISOR);
	LEGACY_WRITE_U16_LE(last_header + TEST_MESH_FRACTION_BITS_OFFSET, TEST_MESH_FRACTION_BITS);

	/* The same current-format triangle has a different projected size when its
	 * fraction bits are zero. Exercise the production loader and raster pipeline. */
	for (legacy_u16 index = 0; index < TEST_CAR_MESH_COUNT; index++) {
		legacy_u8 *entry = data + TEST_CONTAINER_HEADER_SIZE + index * TEST_CONTAINER_ENTRY_SIZE;
		legacy_u8 *payload = data + LEGACY_READ_U32_LE(entry + TEST_CONTAINER_OFFSET_FIELD);
		LEGACY_WRITE_U16_LE(payload + TEST_MESH_FRACTION_BITS_OFFSET, 0);
	}
	write_container(path, data, size);
	hires_shape3d_load_car_meshes(0, (const legacy_s8 *)"FRAC", cars.pointers[0]);
	assert(hires_shape3d_car_mesh(cars.pointers[0][1]) != cars.pointers[0][1]);
	assert(hires_shape3d_car_vertices(cars.pointers[0][1]) == NULL);
	legacy_s16 integer_width = fractional_render_width(&cars.shapes[0][1]);
	assert(integer_width > TEST_FRACTION_RENDER_WIDTH_LIMIT && integer_width > fractional_width);
	hires_shape3d_free_car_meshes();
	assert_classic_fallback(&cars, 0);
	assert_classic_fallback(&cars, 1);
	file_car_set_base(NULL);
	assert(SDL_RemovePath(path));
	assert(SDL_RemovePath(directory));
	free(data);
}

legacy_int main(void)
{
	check_valid_meshes();
	check_coordinate_offsets_past_one_segment();
	check_fractional_mesh_headers();
	check_primitive_counts_past_byte_range();
	check_malformed_meshes();
	check_car_mesh_lifetime();
	check_real_car_cache_reload();
	check_fractional_car_cache();
	puts("HyperVision wide mesh decoding, resource lifetime and fallback checks passed.");
	return 0;
}
