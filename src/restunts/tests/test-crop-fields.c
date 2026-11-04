#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/extended/crop_fields.h"

#define TEST_CROP_PAINT_COUNT 1U
#define TEST_CROP_PRIMITIVE_PREFIX (SHAPE3D_PRIMITIVE_HEADER_SIZE + TEST_CROP_PAINT_COUNT)

static legacy_s16 coordinate(const struct SHAPE3D *mesh, legacy_u16 index, legacy_u16 offset)
{
	return LEGACY_READ_S16_LE(mesh->shape3d_vertex_bytes + (size_t)index * SHAPE3D_VERTEX_SIZE +
							  offset);
}

#define TEST_TULIP_COLOR_COUNT 8U
#define TEST_TULIP_ROW_COUNT 8U
#define TEST_TULIP_BAND_HALF_WIDTH 50
#define TEST_TULIP_LEAF_HALF_WIDTH 60
#define TEST_TULIP_LEAF_HEIGHT 4
#define TEST_TULIP_HEIGHT 12
#define TEST_TULIP_VERTEX_COUNT 76U
#define TEST_TULIP_PRIMITIVE_COUNT 17U
#define TEST_VINE_WIDTH 22
#define TEST_VINE_LEFT_OFFSET (-11)
#define TEST_VINE_RIGHT_OFFSET 11
#define TEST_VINE_HEIGHT 48
#define TEST_VINE_EAVE_HEIGHT 36
#define TEST_VINE_RIDGE_OFFSET 0
#define TEST_VINE_PROFILE_POINT_COUNT 5U
#define TEST_VINE_ROW_COUNT 24U
#define TEST_VINE_FOLIAGE_VERTEX_COUNT 252U
#define TEST_VINE_FOLIAGE_PRIMITIVE_COUNT 145U
#define TEST_VINE_MIN_ROW_SPACING 42
#define TEST_VINE_MAX_ROW_SPACING 43
#define TEST_VINE_FIRST_CENTER (-491)
#define TEST_VINE_LAST_CENTER 490

static const legacy_u8 test_tulip_colors[TEST_TULIP_COLOR_COUNT] = {
	TULIP_FIELD_WHITE_MATERIAL,	 TULIP_FIELD_GREEN_MATERIAL, TULIP_FIELD_YELLOW_MATERIAL,
	TULIP_FIELD_ORANGE_MATERIAL, TULIP_FIELD_RED_MATERIAL,	 TULIP_FIELD_PURPLE_MATERIAL,
	TULIP_FIELD_PINK_MATERIAL,	 TULIP_FIELD_BLUE_MATERIAL};
static const legacy_u8 test_tulip_row_colors[TEST_TULIP_ROW_COUNT] = {
	TULIP_FIELD_WHITE_MATERIAL,	 TULIP_FIELD_GREEN_MATERIAL, TULIP_FIELD_YELLOW_MATERIAL,
	TULIP_FIELD_ORANGE_MATERIAL, TULIP_FIELD_RED_MATERIAL,	 TULIP_FIELD_PURPLE_MATERIAL,
	TULIP_FIELD_PINK_MATERIAL,	 TULIP_FIELD_BLUE_MATERIAL};

static legacy_int flower_material(legacy_u8 material)
{
	for (legacy_u8 color = 0; color < TEST_TULIP_COLOR_COUNT; color++) {
		if (material == test_tulip_colors[color]) {
			return 1;
		}
	}
	return 0;
}

static void check_tulip_rows(const struct SHAPE3D *mesh)
{
	assert(TULIP_FIELD_ROW_COUNT == TEST_TULIP_ROW_COUNT);
	assert(mesh->shape3d_numverts == TEST_TULIP_VERTEX_COUNT);
	assert(TEST_TULIP_BAND_HALF_WIDTH < TEST_TULIP_LEAF_HALF_WIDTH);
	assert(mesh->shape3d_numprimitives == TEST_TULIP_PRIMITIVE_COUNT);
	assert(coordinate(mesh, 4, SHAPE3D_VERTEX_Y_OFFSET) == TEST_TULIP_HEIGHT);
	legacy_s16 centers[TULIP_FIELD_ROW_COUNT];
	legacy_u8 green_rows = 0, bands = 0, soil = 0, colors = 0;
	const legacy_u8 *primitive = mesh->shape3d_primitives;
	for (legacy_u16 index = 0; index < mesh->shape3d_numprimitives; index++) {
		legacy_u8 count = primitive[0], material = primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE];
		/* Every visible surface is one flat row or the soil; no individual plants. */
		assert(count == CROP_FIELDS_QUAD_VERTICES);
		legacy_u8 first = primitive[TEST_CROP_PRIMITIVE_PREFIX];
		legacy_u8 right = primitive[TEST_CROP_PRIMITIVE_PREFIX + 1U];
		legacy_s16 left_x = coordinate(mesh, first, SHAPE3D_VERTEX_X_OFFSET);
		legacy_s16 right_x = coordinate(mesh, right, SHAPE3D_VERTEX_X_OFFSET);
		legacy_s16 height;
		if (material == TULIP_FIELD_ROW_MATERIAL) {
			assert(green_rows < TULIP_FIELD_ROW_COUNT);
			assert(right_x - left_x == 2 * TEST_TULIP_LEAF_HALF_WIDTH);
			legacy_s16 center = (left_x + right_x) / 2;
			if (green_rows) {
				assert(center - centers[green_rows - 1U] > 2 * TEST_TULIP_LEAF_HALF_WIDTH);
			}
			centers[green_rows++] = center;
			height = TEST_TULIP_LEAF_HEIGHT;
		} else if (flower_material(material)) {
			assert(green_rows == TULIP_FIELD_ROW_COUNT && bands < TULIP_FIELD_ROW_COUNT);
			assert(material == test_tulip_row_colors[bands]);
			assert(right_x - left_x == 2 * TEST_TULIP_BAND_HALF_WIDTH);
			assert(left_x + TEST_TULIP_BAND_HALF_WIDTH == centers[bands]);
			height = TEST_TULIP_HEIGHT;
			for (legacy_u8 color = 0; color < TEST_TULIP_COLOR_COUNT; color++) {
				colors |= material == test_tulip_colors[color] ? (legacy_u8)(1U << color) : 0;
			}
			bands++;
		} else {
			assert(material == CROP_FIELDS_SOIL_MATERIAL);
			assert(left_x == -CROP_FIELDS_HALF_TILE && right_x == CROP_FIELDS_HALF_TILE);
			height = CROP_FIELDS_SOIL_HEIGHT;
			soil++;
		}
		for (legacy_u8 corner = 0; corner < count; corner++) {
			legacy_u8 actual = primitive[TEST_CROP_PRIMITIVE_PREFIX + corner];
			assert(coordinate(mesh, actual, SHAPE3D_VERTEX_Y_OFFSET) == height);
			legacy_s16 z = coordinate(mesh, actual, SHAPE3D_VERTEX_Z_OFFSET);
			assert(z == -CROP_FIELDS_HALF_TILE || z == CROP_FIELDS_HALF_TILE);
		}
		primitive += TEST_CROP_PRIMITIVE_PREFIX + count;
	}
	assert(soil == 1U && green_rows == TULIP_FIELD_ROW_COUNT && bands == TULIP_FIELD_ROW_COUNT);
	assert(colors == (1U << TEST_TULIP_COLOR_COUNT) - 1U);
}

static void check_vine_foliage(const struct SHAPE3D *mesh)
{
	static const legacy_s16 profile_x[TEST_VINE_PROFILE_POINT_COUNT] = {
		TEST_VINE_LEFT_OFFSET, TEST_VINE_LEFT_OFFSET, TEST_VINE_RIDGE_OFFSET,
		TEST_VINE_RIGHT_OFFSET, TEST_VINE_RIGHT_OFFSET};
	static const legacy_s16 profile_y[TEST_VINE_PROFILE_POINT_COUNT] = {
		CROP_FIELDS_SOIL_HEIGHT, TEST_VINE_EAVE_HEIGHT, TEST_VINE_HEIGHT, TEST_VINE_EAVE_HEIGHT,
		CROP_FIELDS_SOIL_HEIGHT};
	assert(VINE_FIELD_SECTION_VERTICES == TEST_VINE_PROFILE_POINT_COUNT);
	assert(mesh->shape3d_numverts == TEST_VINE_FOLIAGE_VERTEX_COUNT);
	assert(mesh->shape3d_numprimitives == TEST_VINE_FOLIAGE_PRIMITIVE_COUNT);
	assert(VINE_FIELD_ROW_COUNT == TEST_VINE_ROW_COUNT);
	assert(coordinate(mesh, 4, SHAPE3D_VERTEX_Y_OFFSET) == TEST_VINE_HEIGHT);
	legacy_s16 centers[TEST_VINE_ROW_COUNT];
	for (legacy_u8 row = 0; row < VINE_FIELD_ROW_COUNT; row++) {
		legacy_u16 first = CROP_FIELDS_BOUNDING_VERTICES + CROP_FIELDS_QUAD_VERTICES +
						   row * VINE_FIELD_ROW_VERTICES;
		legacy_s16 left = coordinate(mesh, first, SHAPE3D_VERTEX_X_OFFSET);
		legacy_s16 right =
			coordinate(mesh, first + VINE_FIELD_SECTION_VERTICES - 1U, SHAPE3D_VERTEX_X_OFFSET);
		assert(right - left == TEST_VINE_WIDTH);
		centers[row] = left - TEST_VINE_LEFT_OFFSET;
		if (row) {
			legacy_s16 spacing = centers[row] - centers[row - 1U];
			assert(spacing >= TEST_VINE_MIN_ROW_SPACING && spacing <= TEST_VINE_MAX_ROW_SPACING);
			assert(spacing > TEST_VINE_WIDTH);
		}
		for (legacy_u8 end = 0; end < VINE_FIELD_CAP_COUNT; end++) {
			for (legacy_u8 corner = 0; corner < VINE_FIELD_SECTION_VERTICES; corner++) {
				legacy_u16 vertex = first + end * VINE_FIELD_SECTION_VERTICES + corner;
				assert(coordinate(mesh, vertex, SHAPE3D_VERTEX_X_OFFSET) - centers[row] ==
					   profile_x[corner]);
				assert(coordinate(mesh, vertex, SHAPE3D_VERTEX_Y_OFFSET) == profile_y[corner]);
				assert(coordinate(mesh, vertex, SHAPE3D_VERTEX_Z_OFFSET) ==
					   (end ? CROP_FIELDS_HALF_TILE : -CROP_FIELDS_HALF_TILE));
			}
		}
	}
	assert(centers[0] == TEST_VINE_FIRST_CENTER);
	assert(centers[TEST_VINE_ROW_COUNT - 1U] == TEST_VINE_LAST_CENTER);
}

static size_t check_mesh(const struct SHAPE3D *mesh, legacy_u16 id)
{
	legacy_u8 tulips = id == CSTRK_TULIP_FIELD_ID;
	legacy_s16 height = tulips ? TULIP_FIELD_HEIGHT : VINE_FIELD_HEIGHT;
	assert(mesh->shape3d_numverts == (tulips ? TULIP_FIELD_VERTEX_COUNT : VINE_FIELD_VERTEX_COUNT));
	assert(mesh->shape3d_numprimitives ==
		   (tulips ? TULIP_FIELD_PRIMITIVE_COUNT : VINE_FIELD_PRIMITIVE_COUNT));
	assert(mesh->shape3d_numverts <= LEGACY_U8_MAX);
	assert(mesh->shape3d_numprimitives <= LEGACY_U8_MAX);
	assert(mesh->shape3d_numpaints == TEST_CROP_PAINT_COUNT);
	assert(coordinate(mesh, 0, SHAPE3D_VERTEX_X_OFFSET) == -CROP_FIELDS_HALF_TILE);
	assert(coordinate(mesh, 0, SHAPE3D_VERTEX_Y_OFFSET) == 0);
	assert(coordinate(mesh, 0, SHAPE3D_VERTEX_Z_OFFSET) == -CROP_FIELDS_HALF_TILE);
	assert(coordinate(mesh, 4, SHAPE3D_VERTEX_X_OFFSET) == CROP_FIELDS_HALF_TILE);
	assert(coordinate(mesh, 4, SHAPE3D_VERTEX_Y_OFFSET) == height);
	assert(coordinate(mesh, 4, SHAPE3D_VERTEX_Z_OFFSET) == CROP_FIELDS_HALF_TILE);
	for (legacy_u16 index = 0; index < mesh->shape3d_numverts; index++) {
		legacy_s16 x = coordinate(mesh, index, SHAPE3D_VERTEX_X_OFFSET);
		legacy_s16 y = coordinate(mesh, index, SHAPE3D_VERTEX_Y_OFFSET);
		legacy_s16 z = coordinate(mesh, index, SHAPE3D_VERTEX_Z_OFFSET);
		assert(x >= -CROP_FIELDS_HALF_TILE && x <= CROP_FIELDS_HALF_TILE);
		assert(z >= -CROP_FIELDS_HALF_TILE && z <= CROP_FIELDS_HALF_TILE);
		assert(y >= 0 && y <= height);
	}
	const legacy_u8 *primitive = mesh->shape3d_primitives;
	legacy_u8 green_top = 0, green_side = 0;
	legacy_u16 end_caps = 0;
	for (legacy_u16 index = 0; index < mesh->shape3d_numprimitives; index++) {
		legacy_u8 count = primitive[0];
		legacy_u8 material = primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE];
		assert(count >= CROP_FIELDS_QUAD_VERTICES && count <= VINE_FIELD_SECTION_VERTICES);
		assert(primitive[1] == 1U);
		if (!tulips) {
			assert(material == CROP_FIELDS_SOIL_MATERIAL || material == VINE_FIELD_SIDE_MATERIAL ||
				   material == VINE_FIELD_TOP_MATERIAL);
		}
		if (!tulips && material == VINE_FIELD_SIDE_MATERIAL) {
			legacy_u8 first = primitive[TEST_CROP_PRIMITIVE_PREFIX];
			legacy_s16 depth = coordinate(mesh, first, SHAPE3D_VERTEX_Z_OFFSET);
			legacy_u8 end_cap = 1;
			for (legacy_u8 corner = 1; corner < count; corner++) {
				legacy_u8 vertex = primitive[TEST_CROP_PRIMITIVE_PREFIX + corner];
				end_cap &= coordinate(mesh, vertex, SHAPE3D_VERTEX_Z_OFFSET) == depth;
			}
			if (end_cap) {
				legacy_s32 area = 0;
				for (legacy_u8 corner = 0; corner < count; corner++) {
					legacy_u8 current = primitive[TEST_CROP_PRIMITIVE_PREFIX + corner];
					legacy_u8 next = primitive[TEST_CROP_PRIMITIVE_PREFIX + (corner + 1U) % count];
					area += (legacy_s32)coordinate(mesh, current, SHAPE3D_VERTEX_X_OFFSET) *
								coordinate(mesh, next, SHAPE3D_VERTEX_Y_OFFSET) -
							(legacy_s32)coordinate(mesh, next, SHAPE3D_VERTEX_X_OFFSET) *
								coordinate(mesh, current, SHAPE3D_VERTEX_Y_OFFSET);
				}
				assert(area != 0 && (area > 0) == (depth > 0));
			}
			end_caps += end_cap;
		}
		green_top |= material == VINE_FIELD_TOP_MATERIAL;
		green_side |= material == VINE_FIELD_SIDE_MATERIAL;
		for (legacy_u16 vertex = 0; vertex < count; vertex++) {
			legacy_u8 actual = primitive[TEST_CROP_PRIMITIVE_PREFIX + vertex];
			assert(actual >= CROP_FIELDS_BOUNDING_VERTICES);
			assert(actual < mesh->shape3d_numverts);
			for (legacy_u16 previous = 0; previous < vertex; previous++) {
				assert(actual != primitive[TEST_CROP_PRIMITIVE_PREFIX + previous]);
			}
		}
		for (legacy_u16 byte = 0; byte < SHAPE3D_VISIBILITY_MASK_SIZE; byte++) {
			assert(mesh->shape3d_visibility_masks[index * SHAPE3D_VISIBILITY_MASK_SIZE + byte] ==
				   LEGACY_U8_MAX);
			assert(mesh->shape3d_front_facing_masks[index * SHAPE3D_VISIBILITY_MASK_SIZE + byte] ==
				   0);
		}
		primitive += TEST_CROP_PRIMITIVE_PREFIX + count;
	}
	assert(primitive[0] == 0 && primitive[1] == 0);
	if (tulips) {
		assert(green_top && end_caps == 0);
		check_tulip_rows(mesh);
	} else {
		assert(green_top && green_side);
		assert(end_caps == TEST_VINE_ROW_COUNT * VINE_FIELD_CAP_COUNT);
		check_vine_foliage(mesh);
	}
	return (size_t)(primitive - mesh->shape3d_primitives) + SHAPE3D_PRIMITIVE_HEADER_SIZE;
}

static void check_field(legacy_u16 id)
{
	legacy_u8 count = crop_fields_mesh_count(id);
	assert(count == 1U);
	for (legacy_u8 part = 0; part < count; part++) {
		struct SHAPE3D mesh, repeated;
		assert(crop_fields_create_mesh(&mesh, id, part));
		assert(crop_fields_create_mesh(&repeated, id, part));
		size_t primitive_bytes = check_mesh(&mesh, id);
		assert(check_mesh(&repeated, id) == primitive_bytes);
		assert(!memcmp(mesh.shape3d_vertex_bytes, repeated.shape3d_vertex_bytes,
					   (size_t)mesh.shape3d_numverts * SHAPE3D_VERTEX_SIZE));
		assert(!memcmp(mesh.shape3d_primitives, repeated.shape3d_primitives, primitive_bytes));
		free(mesh.shape3d_vertex_bytes);
		free(repeated.shape3d_vertex_bytes);
	}
	struct SHAPE3D invalid;
	assert(!crop_fields_create_mesh(&invalid, id, count));
	assert(!invalid.shape3d_vertex_bytes && !invalid.shape3d_primitives);
}

legacy_int main(void)
{
	assert(!crop_fields_create_mesh(NULL, CSTRK_TULIP_FIELD_ID, 0));
	struct SHAPE3D invalid;
	assert(crop_fields_mesh_count(0) == 0);
	assert(crop_fields_mesh_count(CSTRK_WHEAT_FIELD_ID) == 0);
	assert(!crop_fields_create_mesh(&invalid, 0, 0));
	assert(!invalid.shape3d_vertex_bytes && !invalid.shape3d_primitives);
	assert(invalid.shape3d_numverts == 0 && invalid.shape3d_numprimitives == 0);
	assert(!crop_fields_create_mesh(&invalid, CSTRK_WHEAT_FIELD_ID, 0));
	check_field(CSTRK_TULIP_FIELD_ID);
	check_field(CSTRK_VINE_FIELD_ID);
	puts("crop field mesh tests passed");
	return 0;
}
