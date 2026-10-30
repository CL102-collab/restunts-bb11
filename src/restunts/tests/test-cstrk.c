#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../c/cstrk.h"

#define TEST_FUTURE_REVISION 101U

static char error[256];

static struct CSTRK_OBJECT object_at(uint8_t x, uint8_t y)
{
	struct CSTRK_OBJECT object = {0};
	object.legacy_id = 1U;
	object.x = x;
	object.y = y;
	object.width = 1U;
	object.height = 1U;
	object.occupied_mask = 1U;
	return object;
}

static void test_wire_layout(void)
{
	static const uint8_t expected[] = {
		'C', 'S', 'T', 'R', 'K', 0x1a, '\r', '\n', CSTRK_VERSION,
		0,	 32,  0,   0,	0,	 0,	   0,	 1,	   0,
		1,	 0,	  2,   0,	0,	 0,	   1,	 0,	   0,
		0,	 24,  0,   0,	0,	 0,	   0,	 0,	   0, /* Four shared terrain corners. */
		2,	 4,										  /* Dirt, southwest water triangle. */
		17,	 0,	  0,   0,	3,	 2,	   1,	 1,	   1,
		0,	 0,	  0,   0,	0,	 0,	   0,	 'A',  '\n'};
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 1, 1, error, sizeof(error)));
	track.version = CSTRK_VERSION;
	track.cells[0].surface = CSTRK_TERRAIN_DIRT;
	track.cells[0].half_water_corner = CSTRK_WATER_SOUTHWEST;
	struct CSTRK_OBJECT object = object_at(0, 0);
	object.legacy_id = 17;
	object.rotation = 3;
	object.surface = CSTRK_ROAD_ICE;
	assert(cstrk_add_object(&track, &object, error, sizeof(error)));
	assert(cstrk_set_notes(&track, "A\n", 2U, error, sizeof(error)));
	uint8_t *data = NULL;
	size_t size = 0;
	assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
	assert(size == sizeof(expected));
	assert(memcmp(data, expected, size) == 0);
	assert(cstrk_validate_bytes(expected, sizeof(expected), error, sizeof(error)));
	for (size_t end = 0; end < size; end++) {
		assert(!cstrk_validate_bytes(data, end, error, sizeof(error)));
	}
	uint8_t extra[sizeof(expected) + 1U];
	memcpy(extra, expected, sizeof(expected));
	extra[sizeof(expected)] = 0;
	assert(!cstrk_validate_bytes(extra, sizeof(extra), error, sizeof(error)));
	free(data);
	cstrk_free(&track);
	cstrk_free(&track);
}

static void test_horizons_and_versions(void)
{
	struct CSTRK_TRACK track, decoded;
	cstrk_init(&track);
	cstrk_init(&decoded);
	assert(track.version == CSTRK_VERSION && decoded.version == CSTRK_VERSION);
	assert(cstrk_create(&track, 1U, 1U, error, sizeof(error)));
	assert(track.version == CSTRK_VERSION && track.horizon == CSTRK_HORIZON_DEFAULT);
	static const legacy_char notes[] = "Keep these track notes.\n";
	static const legacy_u16 versions[] = {CSTRK_VERSION, 15U, TEST_FUTURE_REVISION, LEGACY_U16_MAX};
	assert(cstrk_set_notes(&track, notes, sizeof(notes) - 1U, error, sizeof(error)));
	for (size_t index = 0; index < sizeof(versions) / sizeof(versions[0]); index++) {
		track.version = versions[index];
		for (legacy_u8 horizon = CSTRK_HORIZON_DEFAULT; horizon < CSTRK_HORIZON_COUNT; horizon++) {
			track.horizon = horizon;
			legacy_u8 *data = NULL;
			size_t size = 0;
			assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
			assert(LEGACY_READ_U16_LE(data + CSTRK_VERSION_OFFSET) == track.version);
			assert(data[CSTRK_HORIZON_OFFSET] == horizon);
			assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
			assert(decoded.version == track.version && decoded.horizon == horizon);
			assert(decoded.notes_length == sizeof(notes) - 1U && strcmp(decoded.notes, notes) == 0);
			legacy_u8 *encoded = NULL;
			size_t encoded_size = 0;
			assert(cstrk_encode(&decoded, &encoded, &encoded_size, error, sizeof(error)));
			assert(encoded_size == size && memcmp(encoded, data, size) == 0);
			free(encoded);

			/* Future metadata still requires a structurally valid known payload. */
			legacy_u8 before[sizeof(decoded)];
			memcpy(before, &decoded, sizeof(decoded));
			data[CSTRK_HORIZON_OFFSET] = CSTRK_HORIZON_COUNT;
			assert(!cstrk_decode(data, size, &decoded, error, sizeof(error)));
			assert(memcmp(&decoded, before, sizeof(decoded)) == 0);
			data[CSTRK_HORIZON_OFFSET] = horizon;
			data[CSTRK_HORIZON_OFFSET + 1U] = 1U;
			assert(!cstrk_decode(data, size, &decoded, error, sizeof(error)));
			assert(memcmp(&decoded, before, sizeof(decoded)) == 0);
			data[CSTRK_HORIZON_OFFSET + 1U] = 0;
			assert(!cstrk_decode(data, size - 1U, &decoded, error, sizeof(error)));
			assert(memcmp(&decoded, before, sizeof(decoded)) == 0);
			for (legacy_u16 version = 0; version < CSTRK_MIN_VERSION; version++) {
				LEGACY_WRITE_U16_LE(data + CSTRK_VERSION_OFFSET, version);
				assert(!cstrk_decode(data, size, &decoded, error, sizeof(error)));
				assert(strstr(error, "Unsupported track version") != NULL);
				assert(memcmp(&decoded, before, sizeof(decoded)) == 0);
			}
			free(data);
		}
	}

	legacy_u8 *data = NULL;
	size_t size = 0;
	assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
	legacy_u8 *original_output = data;
	size_t original_size = size;
	for (legacy_u16 version = 0; version < CSTRK_MIN_VERSION; version++) {
		decoded.version = version;
		assert(!cstrk_validate(&decoded, error, sizeof(error)));
		assert(!cstrk_encode(&decoded, &data, &size, error, sizeof(error)));
		assert(strstr(error, "Unsupported track version") != NULL);
		assert(data == original_output && size == original_size);
	}
	decoded.version = CSTRK_VERSION;
	decoded.horizon = CSTRK_HORIZON_COUNT;
	assert(!cstrk_validate(&decoded, error, sizeof(error)));
	assert(!cstrk_encode(&decoded, &data, &size, error, sizeof(error)));
	assert(data == original_output && size == original_size);
	free(data);
	cstrk_free(&track);
	cstrk_free(&decoded);
	assert(track.version == CSTRK_VERSION && decoded.version == CSTRK_VERSION);
}

static void test_surface_corners(void)
{
	struct CSTRK_TRACK track, decoded;
	cstrk_init(&track);
	cstrk_init(&decoded);
	assert(cstrk_create(&track, 1U, 1U, error, sizeof(error)));
	assert(track.cells[0].corner_surface == CSTRK_TERRAIN_WATER);
	size_t cell_offset = CSTRK_HEADER_SIZE + (track.width + 1U) * (track.height + 1U);
	for (legacy_u8 surface = CSTRK_TERRAIN_GRASS; surface <= CSTRK_TERRAIN_WATER; surface++) {
		for (legacy_u8 other = CSTRK_TERRAIN_GRASS; other <= CSTRK_TERRAIN_WATER; other++) {
			if (surface == other) {
				continue;
			}
			for (legacy_u8 corner = CSTRK_WATER_NORTHWEST; corner <= CSTRK_WATER_SOUTHWEST;
				 corner++) {
				track.version = CSTRK_VERSION;
				track.cells[0].surface = surface;
				track.cells[0].corner_surface = other;
				track.cells[0].half_water_corner = corner;
				legacy_u8 *data = NULL;
				size_t size = 0;
				assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
				assert(data[cell_offset] == surface);
				assert(data[cell_offset + 1U] ==
					   (corner | ((other ^ CSTRK_TERRAIN_WATER) << CSTRK_CORNER_SURFACE_SHIFT)));
				assert(LEGACY_READ_U16_LE(data + CSTRK_VERSION_OFFSET) == CSTRK_VERSION);
				assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
				assert(decoded.cells[0].surface == surface);
				assert(decoded.cells[0].corner_surface == other);
				assert(decoded.cells[0].half_water_corner == corner);
				legacy_u8 *again = NULL;
				size_t again_size = 0;
				assert(cstrk_encode(&decoded, &again, &again_size, error, sizeof(error)));
				assert(again_size == size && memcmp(again, data, size) == 0);
				free(again);
				free(data);
			}
		}
	}

	track.version = TEST_FUTURE_REVISION;
	track.cells[0].surface = CSTRK_TERRAIN_WATER;
	track.cells[0].corner_surface = CSTRK_TERRAIN_ICE;
	track.cells[0].half_water_corner = CSTRK_WATER_NORTHWEST;
	legacy_u8 *future = NULL;
	size_t future_size = 0;
	assert(cstrk_encode(&track, &future, &future_size, error, sizeof(error)));
	assert(LEGACY_READ_U16_LE(future + CSTRK_VERSION_OFFSET) == TEST_FUTURE_REVISION);
	free(future);
	track.version = CSTRK_VERSION;
	for (legacy_u8 surface = CSTRK_TERRAIN_GRASS; surface <= CSTRK_TERRAIN_WATER; surface++) {
		track.cells[0].surface = surface;
		track.cells[0].corner_surface = surface;
		assert(!cstrk_validate(&track, error, sizeof(error)));
	}
	track.cells[0].surface = CSTRK_TERRAIN_GRASS;
	track.cells[0].corner_surface = LEGACY_U8_MAX;
	assert(!cstrk_validate(&track, error, sizeof(error)));

	/* Inactive secondary material data cannot change the serialized course. */
	track.cells[0].half_water_corner = CSTRK_WATER_NONE;
	legacy_u8 *data = NULL;
	size_t size = 0;
	assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
	assert(data[cell_offset + 1U] == CSTRK_WATER_NONE);
	assert(LEGACY_READ_U16_LE(data + CSTRK_VERSION_OFFSET) == CSTRK_VERSION);
	assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
	assert(decoded.cells[0].corner_surface == CSTRK_TERRAIN_WATER);
	legacy_u8 before[sizeof(decoded)];
	memcpy(before, &decoded, sizeof(decoded));
	static const legacy_u8 invalid_corners[] = {
		CSTRK_WATER_SOUTHWEST + 1U,
		CSTRK_WATER_SOUTHWEST + 2U,
		CSTRK_CORNER_DIRECTION_MASK,
		1U << CSTRK_CORNER_SURFACE_SHIFT,
		(CSTRK_CORNER_SURFACE_MASK + 1U) << CSTRK_CORNER_SURFACE_SHIFT,
		LEGACY_U8_MAX,
		CSTRK_WATER_NORTHWEST |
			((CSTRK_TERRAIN_GRASS ^ CSTRK_TERRAIN_WATER) << CSTRK_CORNER_SURFACE_SHIFT)};
	for (size_t index = 0; index < sizeof(invalid_corners) / sizeof(invalid_corners[0]); index++) {
		data[cell_offset + 1U] = invalid_corners[index];
		assert(!cstrk_decode(data, size, &decoded, error, sizeof(error)));
		assert(memcmp(before, &decoded, sizeof(decoded)) == 0);
	}
	free(data);
	cstrk_free(&track);
	cstrk_free(&decoded);
}

#define TEST_TERRAIN_CORNER_COUNT 4U
#define TEST_TERRAIN_PROFILE_MASK_COUNT (1U << TEST_TERRAIN_CORNER_COUNT)

static void set_terrain_corners(struct CSTRK_TRACK *track,
								const legacy_u8 heights[TEST_TERRAIN_CORNER_COUNT])
{
	track->corner_heights[0] = heights[0];
	track->corner_heights[1] = heights[1];
	track->corner_heights[3] = heights[2];
	track->corner_heights[2] = heights[3];
}

static void test_original_terrain_profiles(void)
{
	static const legacy_u8 expected_ids[TEST_TERRAIN_PROFILE_MASK_COUNT] = {
		0, 11, 14, 7, 13, LEGACY_U8_MAX, 10, 18, 12, 8, LEGACY_U8_MAX, 15, 9, 16, 17, 6};
	struct CSTRK_TRACK track, decoded;
	cstrk_init(&track);
	cstrk_init(&decoded);
	assert(cstrk_create(&track, 1U, 1U, error, sizeof(error)));
	for (legacy_u8 base = 0; base < CSTRK_MAX_HEIGHT; base++) {
		for (legacy_u8 mask = 1; mask < TEST_TERRAIN_PROFILE_MASK_COUNT - 1U; mask++) {
			legacy_u8 heights[TEST_TERRAIN_CORNER_COUNT];
			for (legacy_u8 corner = 0; corner < TEST_TERRAIN_CORNER_COUNT; corner++) {
				heights[corner] = base + ((mask >> corner) & 1U);
			}
			set_terrain_corners(&track, heights);
			struct CSTRK_TERRAIN_PROFILE profile = {LEGACY_U8_MAX, LEGACY_U8_MAX, LEGACY_U8_MAX};
			struct CSTRK_TERRAIN_PROFILE before = profile;
			if (expected_ids[mask] == LEGACY_U8_MAX) {
				assert(!cstrk_terrain_profile(&track, 0, 0, &profile));
				assert(memcmp(&profile, &before, sizeof(profile)) == 0);
				assert(!cstrk_validate(&track, error, sizeof(error)));
				continue;
			}
			assert(cstrk_terrain_profile(&track, 0, 0, &profile));
			assert(profile.terrain_id == expected_ids[mask]);
			assert(profile.base_height == base && profile.high_mask == mask);
			assert(cstrk_validate(&track, error, sizeof(error)));
			legacy_u8 *data = NULL;
			size_t size = 0;
			assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
			assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
			assert(cstrk_terrain_profile(&decoded, 0, 0, &before));
			assert(memcmp(&profile, &before, sizeof(profile)) == 0);
			for (legacy_u8 corner = CSTRK_WATER_NONE; corner <= CSTRK_WATER_SOUTHWEST; corner++) {
				track.cells[0].half_water_corner = corner;
				assert(cstrk_terrain_profile(&track, 0, 0, &before));
				assert(memcmp(&profile, &before, sizeof(profile)) == 0);
			}
			free(data);
		}
	}
	for (legacy_u8 level = 0; level <= CSTRK_MAX_HEIGHT; level++) {
		memset(track.corner_heights, level, TEST_TERRAIN_CORNER_COUNT);
		struct CSTRK_TERRAIN_PROFILE profile;
		assert(cstrk_terrain_profile(&track, 0, 0, &profile));
		assert(profile.terrain_id ==
			   (level == 0 ? CSTRK_TERRAIN_PROFILE_FLAT : CSTRK_TERRAIN_PROFILE_RAISED));
		assert(profile.base_height == level && profile.high_mask == 0);
		assert(cstrk_validate(&track, error, sizeof(error)));
	}
	static const legacy_u8 invalid[][TEST_TERRAIN_CORNER_COUNT] = {
		{0, 1, 2, 1}, {2, 1, 0, 1}, {0, 2, 2, 0}, {0, 0, 0, CSTRK_MAX_HEIGHT + 1U}};
	for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); index++) {
		set_terrain_corners(&track, invalid[index]);
		assert(!cstrk_terrain_profile(&track, 0, 0, NULL));
		assert(!cstrk_validate(&track, error, sizeof(error)));
	}
	memset(track.corner_heights, 0, TEST_TERRAIN_CORNER_COUNT);
	assert(cstrk_terrain_profile(&track, 0, 0, NULL));
	assert(!cstrk_terrain_profile(&track, track.width, 0, NULL));
	assert(!cstrk_terrain_profile(&track, 0, track.height, NULL));
	assert(!cstrk_terrain_profile(NULL, 0, 0, NULL));
	legacy_u8 *data = NULL;
	size_t size = 0;
	assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
	assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
	legacy_u8 *saved_corners = decoded.corner_heights;
	/* Reject a known-layout track whose four corners encode a diagonal ramp,
	 * preserving the last valid document on a failed load. */
	data[CSTRK_HEADER_SIZE + 1U] = 1;
	data[CSTRK_HEADER_SIZE + 2U] = 1;
	data[CSTRK_HEADER_SIZE + 3U] = 2;
	assert(!cstrk_validate_bytes(data, size, error, sizeof(error)));
	assert(!cstrk_decode(data, size, &decoded, error, sizeof(error)));
	assert(decoded.corner_heights == saved_corners && decoded.corner_heights[3] == 0);
	free(data);
	cstrk_free(&track);
	cstrk_free(&decoded);
}

static void test_plateaus_and_hill_slopes(void)
{
	struct CSTRK_TRACK track;
	struct CSTRK_TRACK decoded;
	cstrk_init(&track);
	cstrk_init(&decoded);
	assert(cstrk_create(&track, 2, 2, error, sizeof(error)));
	memset(track.corner_heights, CSTRK_MAX_HEIGHT, 9U);
	struct CSTRK_OBJECT corner = object_at(0, 0);
	corner.legacy_id = 10;
	corner.width = 2;
	corner.height = 2;
	corner.occupied_mask = 7;
	corner.base_height = CSTRK_MAX_HEIGHT;
	assert(cstrk_add_object(&track, &corner, error, sizeof(error)));
	struct CSTRK_OBJECT decoration = object_at(1, 1);
	decoration.legacy_id = 126;
	decoration.base_height = CSTRK_MAX_HEIGHT;
	assert(cstrk_add_object(&track, &decoration, error, sizeof(error)));
	assert(!cstrk_add_object(&track, &decoration, error, sizeof(error)));
	assert(track.object_count == 2U);
	uint8_t *data = NULL;
	size_t size = 0;
	assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
	assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
	assert(decoded.object_count == 2U);
	assert(decoded.objects[0].occupied_mask == 7U);
	assert(decoded.objects[1].base_height == CSTRK_MAX_HEIGHT);
	assert(decoded.corner_heights[8] == 4U);
	uint8_t *second = NULL;
	size_t second_size = 0;
	assert(cstrk_encode(&decoded, &second, &second_size, error, sizeof(error)));
	assert(size == second_size && memcmp(data, second, size) == 0);
	assert(cstrk_remove_object(&decoded, 0));
	assert(decoded.object_count == 1U && decoded.objects[0].legacy_id == 126U);
	assert(!cstrk_remove_object(&decoded, 1));
	assert(cstrk_validate(&decoded, error, sizeof(error)));
	decoded.objects[0].slope_x = -1;
	assert(!cstrk_validate(&decoded, error, sizeof(error)));
	decoded.objects[0].slope_x = 0;
	decoded.corner_heights[8] = 3;
	assert(!cstrk_validate(&decoded, error, sizeof(error)));
	free(data);
	free(second);
	cstrk_free(&track);
	cstrk_free(&decoded);

	/* Negative plane slopes encode as signed two's-complement bytes. */
	cstrk_init(&track);
	assert(cstrk_create(&track, 2, 1, error, sizeof(error)));
	for (unsigned row = 0; row < 2U; row++) {
		for (unsigned column = 0; column < 3U; column++) {
			track.corner_heights[row * 3U + column] = (uint8_t)(2U - column);
		}
	}
	corner = object_at(0, 0);
	corner.legacy_id = 5;
	corner.base_height = 2;
	corner.slope_x = -1;
	assert(cstrk_add_object(&track, &corner, error, sizeof(error)));
	struct CSTRK_OBJECT wide = corner;
	wide.width = 2;
	wide.occupied_mask = 3;
	assert(!cstrk_add_object(&track, &wide, error, sizeof(error)));
	assert(track.object_count == 1U);
	assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
	cstrk_init(&decoded);
	assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
	assert(decoded.objects[0].slope_x == -1);
	free(data);
	cstrk_free(&track);
	cstrk_free(&decoded);
}

#define TEST_GENERAL_SLOPE_SIZE 2U
#define TEST_GENERAL_SLOPE_STRIDE (TEST_GENERAL_SLOPE_SIZE + 1U)
#define TEST_GENERAL_SLOPE_BASE 2U
#define TEST_GENERAL_SLOPE_DIRECTION_COUNT 4U

static void test_multi_tile_straight_slopes(void)
{
	static const legacy_s8 slopes[TEST_GENERAL_SLOPE_DIRECTION_COUNT][2] = {
		{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
	for (legacy_u8 direction = 0; direction < TEST_GENERAL_SLOPE_DIRECTION_COUNT; direction++) {
		struct CSTRK_TRACK track, decoded;
		cstrk_init(&track);
		cstrk_init(&decoded);
		assert(cstrk_create(&track, TEST_GENERAL_SLOPE_SIZE, TEST_GENERAL_SLOPE_SIZE, error,
							sizeof(error)));
		for (legacy_u8 y = 0; y <= track.height; y++) {
			for (legacy_u8 x = 0; x <= track.width; x++) {
				track.corner_heights[y * TEST_GENERAL_SLOPE_STRIDE + x] =
					TEST_GENERAL_SLOPE_BASE + slopes[direction][0] * x + slopes[direction][1] * y;
			}
		}
		struct CSTRK_OBJECT object = object_at(0, 0);
		object.legacy_id = 11U;
		object.width = object.height = TEST_GENERAL_SLOPE_SIZE;
		object.occupied_mask = 9U;
		object.base_height = TEST_GENERAL_SLOPE_BASE;
		object.slope_x = slopes[direction][0];
		object.slope_y = slopes[direction][1];
		assert(cstrk_add_object(&track, &object, error, sizeof(error)));
		legacy_u8 *data = NULL;
		size_t size = 0;
		assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
		assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
		assert(decoded.version == CSTRK_VERSION);
		assert(decoded.objects[0].slope_x == object.slope_x);
		assert(decoded.objects[0].slope_y == object.slope_y);
		assert(memcmp(decoded.corner_heights, track.corner_heights,
					  TEST_GENERAL_SLOPE_STRIDE * TEST_GENERAL_SLOPE_STRIDE) == 0);
		for (legacy_u16 version = CSTRK_MIN_VERSION; version < CSTRK_GENERIC_SLOPE_VERSION;
			 version++) {
			track.version = version;
			assert(!cstrk_validate(&track, error, sizeof(error)));
			LEGACY_WRITE_U16_LE(data + CSTRK_VERSION_OFFSET, version);
			assert(!cstrk_decode(data, size, &decoded, error, sizeof(error)));
			assert(decoded.version == CSTRK_VERSION && decoded.object_count == 1U);
		}
		track.version = CSTRK_VERSION;
		/* Shared road vertices cannot acquire a hill corner or a different grade. */
		track.corner_heights[TEST_GENERAL_SLOPE_STRIDE + 1U] +=
			direction < TEST_GENERAL_SLOPE_SIZE ? -1 : 1;
		assert(!cstrk_validate(&track, error, sizeof(error)));
		free(data);
		cstrk_free(&track);
		cstrk_free(&decoded);
	}
}

#define TEST_SPARSE_FOOTPRINT_SIZE 2U
#define TEST_SPARSE_TERRAIN_STRIDE (TEST_SPARSE_FOOTPRINT_SIZE + 1U)
#define TEST_SPARSE_TERRAIN_VERTICES (TEST_SPARSE_TERRAIN_STRIDE * TEST_SPARSE_TERRAIN_STRIDE)
#define TEST_SPARSE_BASE_HEIGHT 2U

static void test_sparse_terrain_support(void)
{
	static const legacy_u8 masks[] = {6U, 9U, 7U, 11U, 13U, 14U};
	for (legacy_u8 variant = 0; variant < sizeof(masks) / sizeof(masks[0]); variant++) {
		for (legacy_s8 change = -1; change <= 1; change += 2) {
			struct CSTRK_TRACK track, decoded;
			cstrk_init(&track);
			cstrk_init(&decoded);
			assert(cstrk_create(&track, TEST_SPARSE_FOOTPRINT_SIZE, TEST_SPARSE_FOOTPRINT_SIZE,
								error, sizeof(error)));
			memset(track.corner_heights, TEST_SPARSE_BASE_HEIGHT, TEST_SPARSE_TERRAIN_VERTICES);
			struct CSTRK_OBJECT object = object_at(0, 0);
			object.width = object.height = TEST_SPARSE_FOOTPRINT_SIZE;
			object.occupied_mask = masks[variant];
			object.base_height = TEST_SPARSE_BASE_HEIGHT;
			for (legacy_u8 cell = 0; cell < TEST_SPARSE_FOOTPRINT_SIZE * TEST_SPARSE_FOOTPRINT_SIZE;
				 cell++) {
				if (!(object.occupied_mask & (1U << cell))) {
					legacy_u8 x = (cell % TEST_SPARSE_FOOTPRINT_SIZE) * TEST_SPARSE_FOOTPRINT_SIZE;
					legacy_u8 y = (cell / TEST_SPARSE_FOOTPRINT_SIZE) * TEST_SPARSE_FOOTPRINT_SIZE;
					track.corner_heights[y * TEST_SPARSE_TERRAIN_STRIDE + x] += change;
				}
			}
			assert(cstrk_add_object(&track, &object, error, sizeof(error)));
			legacy_u8 *data = NULL;
			size_t size = 0;
			assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
			assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
			assert(decoded.objects[0].occupied_mask == object.occupied_mask);
			assert(memcmp(decoded.corner_heights, track.corner_heights,
						  TEST_SPARSE_TERRAIN_VERTICES) == 0);
			/* The central vertex supports every diagonal road arrangement. */
			decoded.corner_heights[TEST_SPARSE_TERRAIN_STRIDE + 1U] += change;
			assert(!cstrk_validate(&decoded, error, sizeof(error)));
			free(data);
			cstrk_free(&track);
			cstrk_free(&decoded);
		}
	}
}

#define TEST_SLOPED_STUNT_DIRECTIONS 4U
#define TEST_SLOPED_STUNT_REVERSES 2U
#define TEST_SLOPED_STUNT_HALF_TURN 2U

static void test_sloped_stunt_versions(void)
{
	static const legacy_u16 ids[] = {CSTRK_TUNNEL_FIRST_ID, CSTRK_TUNNEL_LAST_ID,
									 CSTRK_SLALOM_FIRST_ID, CSTRK_SLALOM_LAST_ID};
	static const legacy_u8 masks[TEST_SLOPED_STUNT_DIRECTIONS] = {3U, 9U, 12U, 6U};
	struct CSTRK_TRACK track, decoded;
	cstrk_init(&track);
	cstrk_init(&decoded);
	assert(cstrk_create(&track, 1U, 1U, error, sizeof(error)));
	for (size_t id_index = 0; id_index < sizeof(ids) / sizeof(ids[0]); id_index++) {
		for (legacy_u8 base = 0; base < CSTRK_MAX_HEIGHT; base++) {
			for (legacy_u8 direction = 0; direction < TEST_SLOPED_STUNT_DIRECTIONS; direction++) {
				legacy_u8 heights[TEST_TERRAIN_CORNER_COUNT];
				for (legacy_u8 corner = 0; corner < TEST_TERRAIN_CORNER_COUNT; corner++) {
					heights[corner] = base + ((masks[direction] >> corner) & 1U);
				}
				set_terrain_corners(&track, heights);
				for (legacy_u8 reverse = 0; reverse < TEST_SLOPED_STUNT_REVERSES; reverse++) {
					for (legacy_u8 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE;
						 surface++) {
						struct CSTRK_OBJECT object = object_at(0, 0);
						object.legacy_id = ids[id_index];
						object.base_height = heights[0];
						object.slope_x = heights[1] - heights[0];
						object.slope_y = heights[3] - heights[0];
						object.rotation =
							((object.slope_x != 0) ^ (object.legacy_id == CSTRK_TUNNEL_LAST_ID ||
													  object.legacy_id == CSTRK_SLALOM_LAST_ID)) +
							reverse * TEST_SLOPED_STUNT_HALF_TURN;
						object.surface = surface;
						if (track.object_count == 0) {
							assert(cstrk_add_object(&track, &object, error, sizeof(error)));
						} else {
							track.objects[0] = object;
						}
						legacy_u8 *data = NULL;
						size_t size = 0;
						assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
						assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
						assert(decoded.version == CSTRK_VERSION && decoded.object_count == 1);
						assert(decoded.objects[0].legacy_id == object.legacy_id);
						assert(decoded.objects[0].rotation == object.rotation);
						assert(decoded.objects[0].surface == object.surface);
						assert(decoded.objects[0].base_height == object.base_height);
						assert(decoded.objects[0].slope_x == object.slope_x);
						assert(decoded.objects[0].slope_y == object.slope_y);
						legacy_u8 *again = NULL;
						size_t again_size = 0;
						assert(cstrk_encode(&decoded, &again, &again_size, error, sizeof(error)));
						assert(again_size == size && memcmp(again, data, size) == 0);
						free(again);

						/* Old revisions cannot acquire the new placement merely by having
						 * their metadata upgraded. A failed import/export is transactional. */
						legacy_u8 before[sizeof(decoded)];
						memcpy(before, &decoded, sizeof(decoded));
						legacy_u8 *original_data = data;
						size_t original_size = size;
						for (legacy_u16 version = CSTRK_MIN_VERSION;
							 version < CSTRK_SLOPED_STUNT_VERSION; version++) {
							LEGACY_WRITE_U16_LE(data + CSTRK_VERSION_OFFSET, version);
							assert(!cstrk_decode(data, size, &decoded, error, sizeof(error)));
							assert(strstr(error, "version") != NULL);
							assert(memcmp(before, &decoded, sizeof(decoded)) == 0);
							track.version = version;
							assert(!cstrk_encode(&track, &data, &size, error, sizeof(error)));
							assert(data == original_data && size == original_size);
						}
						track.version = CSTRK_VERSION;
						free(data);
					}
				}
			}
		}
	}
	track.version = TEST_FUTURE_REVISION;
	legacy_u8 *data = NULL;
	size_t size = 0;
	assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
	assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
	assert(decoded.version == TEST_FUTURE_REVISION);
	free(data);

	/* Flat stunt pieces and existing hill roads remain valid in both old
	 * revisions, and are upgraded only after their original rules pass. */
	for (legacy_u16 version = CSTRK_MIN_VERSION; version < CSTRK_SLOPED_STUNT_VERSION; version++) {
		track.version = version;
		for (size_t index = 0; index < sizeof(ids) / sizeof(ids[0]); index++) {
			memset(track.corner_heights, CSTRK_MAX_HEIGHT, TEST_TERRAIN_CORNER_COUNT);
			track.objects[0].legacy_id = ids[index];
			track.objects[0].base_height = CSTRK_MAX_HEIGHT;
			track.objects[0].slope_x = track.objects[0].slope_y = 0;
			assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
			assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
			assert(decoded.version == CSTRK_VERSION && decoded.objects[0].legacy_id == ids[index]);
			free(data);
		}
		static const legacy_u8 road_heights[TEST_TERRAIN_CORNER_COUNT] = {0, 1, 1, 0};
		set_terrain_corners(&track, road_heights);
		track.objects[0].legacy_id = 5U;
		track.objects[0].base_height = 0;
		track.objects[0].slope_x = 1;
		assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
		assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
		assert(decoded.version == CSTRK_VERSION && decoded.objects[0].slope_x == 1);
		free(data);
	}
	cstrk_free(&track);
	cstrk_free(&decoded);
}

static void test_older_revisions_reject_generalized_single_tile_slopes(void)
{
	static const legacy_u16 ids[] = {1U, 4U, 7U, 49U, 66U, 115U, 159U};
	static const legacy_u8 heights[TEST_TERRAIN_CORNER_COUNT] = {0U, 1U, 1U, 0U};
	for (legacy_u8 index = 0; index < sizeof(ids) / sizeof(ids[0]); index++) {
		struct CSTRK_TRACK track, decoded;
		cstrk_init(&track);
		cstrk_init(&decoded);
		assert(cstrk_create(&track, 1U, 1U, error, sizeof(error)));
		set_terrain_corners(&track, heights);
		struct CSTRK_OBJECT object = object_at(0, 0);
		object.legacy_id = ids[index];
		object.slope_x = 1;
		assert(cstrk_add_object(&track, &object, error, sizeof(error)));
		legacy_u8 *data = NULL;
		size_t size = 0;
		assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
		assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
		legacy_u8 *original_corners = decoded.corner_heights;
		for (legacy_u16 version = CSTRK_MIN_VERSION; version < CSTRK_GENERIC_SLOPE_VERSION;
			 version++) {
			track.version = version;
			assert(!cstrk_validate(&track, error, sizeof(error)));
			LEGACY_WRITE_U16_LE(data + CSTRK_VERSION_OFFSET, version);
			assert(!cstrk_decode(data, size, &decoded, error, sizeof(error)));
			assert(decoded.corner_heights == original_corners && decoded.object_count == 1U);
		}
		free(data);
		cstrk_free(&track);
		cstrk_free(&decoded);
	}
}

static void test_notes(void)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 1, 1, error, sizeof(error)));
	char notes[CSTRK_MAX_NOTES + 1U];
	memset(notes, 'X', sizeof(notes));
	assert(cstrk_set_notes(&track, notes, CSTRK_MAX_NOTES, error, sizeof(error)));
	assert(track.notes_length == CSTRK_MAX_NOTES && track.notes[CSTRK_MAX_NOTES] == '\0');
	assert(!cstrk_set_notes(&track, notes, sizeof(notes), error, sizeof(error)));
	assert(track.notes_length == CSTRK_MAX_NOTES);
	const char invalid[] = {'\0', '\x01', '\x1f', '\x7f', (char)0x80, (char)0xff};
	for (size_t index = 0; index < sizeof(invalid); index++) {
		assert(!cstrk_set_notes(&track, invalid + index, 1U, error, sizeof(error)));
		assert(track.notes[0] == 'X');
	}
	assert(cstrk_set_notes(&track, "A\tB\r\nC ~", 8U, error, sizeof(error)));
	assert(cstrk_set_notes(&track, track.notes + 2U, 6U, error, sizeof(error)));
	assert(strcmp(track.notes, "B\r\nC ~") == 0);
	assert(cstrk_set_notes(&track, NULL, 0, error, sizeof(error)));
	assert(track.notes[0] == '\0');
	cstrk_free(&track);
}

static void test_invalid_records(void)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 2, 2, error, sizeof(error)));
	struct CSTRK_OBJECT object = object_at(0, 0);
	assert(cstrk_add_object(&track, &object, error, sizeof(error)));
	struct CSTRK_OBJECT invalid[] = {
		{0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0}, {215, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0},
		{1, 0, 0, 4, 0, 1, 1, 1, 0, 0, 0, 0}, {1, 0, 0, 0, 3, 1, 1, 1, 0, 0, 0, 0},
		{1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0}, {1, 0, 0, 0, 0, 3, 1, 1, 0, 0, 0, 0},
		{1, 2, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0}, {1, 0, 2, 0, 0, 1, 1, 1, 0, 0, 0, 0},
		{1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0}, {1, 0, 0, 0, 0, 1, 1, 2, 0, 0, 0, 0},
		{1, 0, 0, 0, 0, 1, 1, 1, 5, 0, 0, 0}, {1, 0, 0, 0, 0, 1, 1, 1, 0, 2, 0, 0},
		{1, 0, 0, 0, 0, 1, 1, 1, 0, 0, -2, 0}};
	for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); index++) {
		track.objects[0] = invalid[index];
		assert(!cstrk_validate(&track, error, sizeof(error)));
	}
	track.objects[0] = object;
	track.cells[0].surface = 4;
	assert(!cstrk_validate(&track, error, sizeof(error)));
	track.cells[0].surface = 0;
	track.cells[0].half_water_corner = 5;
	assert(!cstrk_validate(&track, error, sizeof(error)));
	track.cells[0].half_water_corner = 0;
	track.corner_heights[8] = 5;
	assert(!cstrk_validate(&track, error, sizeof(error)));
	track.corner_heights[8] = 2;
	assert(!cstrk_validate(&track, error, sizeof(error)));
	track.corner_heights[8] = 0;
	uint8_t *data = NULL;
	size_t size = 0;
	assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
	uint8_t *old_corners = track.corner_heights;
	static const size_t offsets[] = {0,	 10, 12, 16, 18, 20, 24, 28, 32, 41, 42,
									 49, 53, 54, 55, 56, 57, 58, 59, 60, 61};
	for (size_t index = 0; index < sizeof(offsets) / sizeof(offsets[0]); index++) {
		size_t offset = offsets[index];
		assert(offset < size);
		uint8_t saved = data[offset];
		data[offset] = 0xff;
		assert(!cstrk_decode(data, size, &track, error, sizeof(error)));
		assert(track.corner_heights == old_corners && track.object_count == 1U);
		data[offset] = saved;
	}
	/* Mutation coverage checks failure paths with arbitrary signed slopes, sizes
	 * and length fields under ASan/UBSan, including otherwise-valid mutations. */
	for (size_t index = 0; index < size; index++) {
		uint8_t saved = data[index];
		for (unsigned value = 0; value < 256U; value += 17U) {
			data[index] = (uint8_t)value;
			(void)cstrk_validate_bytes(data, size, error, sizeof(error));
		}
		data[index] = saved;
	}
	assert(!cstrk_create(&track, 91, 90, error, sizeof(error)));
	assert(track.corner_heights == old_corners);
	assert(!cstrk_create(&track, 90, 0, error, sizeof(error)));
	assert(!cstrk_encode(&track, NULL, &size, error, sizeof(error)));
	assert(!cstrk_validate_bytes(NULL, 0, NULL, 0));
	free(data);
	cstrk_free(&track);
}

static void test_maximum_track(void)
{
	struct CSTRK_TRACK track;
	struct CSTRK_TRACK decoded;
	cstrk_init(&track);
	cstrk_init(&decoded);
	assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
	memset(track.corner_heights, 4, 91U * 91U);
	track.object_count = 90U * 90U;
	track.objects = calloc(track.object_count, sizeof(*track.objects));
	assert(track.objects != NULL);
	for (unsigned row = 0; row < 90U; row++) {
		for (unsigned column = 0; column < 90U; column++) {
			unsigned index = row * 90U + column;
			track.objects[index] = object_at((uint8_t)column, (uint8_t)row);
			track.objects[index].base_height = 4;
			track.objects[index].surface = (uint8_t)(index % 3U);
			track.objects[index].rotation = (uint8_t)(index % 4U);
			track.cells[index].surface = (uint8_t)(index % 4U);
			track.cells[index].half_water_corner = (uint8_t)(index % 5U);
			track.cells[index].corner_surface = track.cells[index].surface == CSTRK_TERRAIN_WATER
													? CSTRK_TERRAIN_GRASS
													: CSTRK_TERRAIN_WATER;
		}
	}
	memset(track.notes, '~', CSTRK_MAX_NOTES);
	track.notes_length = CSTRK_MAX_NOTES;
	uint8_t *data = NULL;
	size_t size = 0;
	assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
	assert(size == 162113U);
	assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
	assert(decoded.width == 90U && decoded.height == 90U);
	assert(decoded.object_count == 8100U && decoded.notes_length == 8000U);
	assert(decoded.objects[8099].x == 89U && decoded.objects[8099].y == 89U);
	assert(decoded.corner_heights[8280] == 4U);
	assert(decoded.notes[8000] == '\0');
	track.object_count++;
	assert(!cstrk_validate(&track, error, sizeof(error)));
	free(data);
	cstrk_free(&track);
	cstrk_free(&decoded);
}

#define TEST_OVERPASS_ROTATIONS 4U

static void test_overpass_lower_surfaces(void)
{
	struct CSTRK_TRACK track, decoded;
	cstrk_init(&track);
	cstrk_init(&decoded);
	assert(cstrk_create(&track, 1U, 1U, error, sizeof(error)));
	struct CSTRK_OBJECT object = object_at(0, 0);
	object.legacy_id = CSTRK_OVERPASS_FIRST_ID;
	assert(cstrk_add_object(&track, &object, error, sizeof(error)));
	size_t object_offset = CSTRK_HEADER_SIZE + TEST_TERRAIN_CORNER_COUNT + 2U;
	size_t lower_offset = object_offset + CSTRK_OBJECT_LOWER_SURFACE_OFFSET;
	/* Independent wire fixture: byte 5 is the deck and byte 12 is the road
	 * underneath, without changing the sixteen-byte object record. */
	static const legacy_u8 expected[] = {101, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 2, 0, 0, 0};
	track.objects[0].surface = CSTRK_ROAD_DIRT;
	track.objects[0].lower_surface = CSTRK_ROAD_ICE;
	legacy_u8 *fixture = NULL;
	size_t fixture_size = 0;
	assert(cstrk_encode(&track, &fixture, &fixture_size, error, sizeof(error)));
	assert(fixture_size == object_offset + sizeof(expected));
	assert(memcmp(fixture + object_offset, expected, sizeof(expected)) == 0);
	/* Revision 4 already stores independent surfaces; upgrading its metadata
	 * must preserve both materials rather than applying the v3 shared rule. */
	LEGACY_WRITE_U16_LE(fixture + CSTRK_VERSION_OFFSET, CSTRK_LOWER_SURFACE_VERSION);
	assert(cstrk_decode(fixture, fixture_size, &decoded, error, sizeof(error)));
	assert(decoded.version == CSTRK_VERSION);
	assert(decoded.objects[0].surface == CSTRK_ROAD_DIRT);
	assert(decoded.objects[0].lower_surface == CSTRK_ROAD_ICE);
	free(fixture);
	for (legacy_u16 id = CSTRK_OVERPASS_FIRST_ID; id <= CSTRK_OVERPASS_LAST_ID; id++) {
		for (legacy_u8 rotation = 0; rotation < TEST_OVERPASS_ROTATIONS; rotation++) {
			for (legacy_u8 upper = CSTRK_ROAD_PAVED; upper <= CSTRK_ROAD_ICE; upper++) {
				for (legacy_u8 lower = CSTRK_ROAD_PAVED; lower <= CSTRK_ROAD_ICE; lower++) {
					track.objects[0].legacy_id = id;
					track.objects[0].rotation = rotation;
					track.objects[0].surface = upper;
					track.objects[0].lower_surface = lower;
					legacy_u8 *data = NULL;
					size_t size = 0;
					assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
					assert(data[object_offset + 5U] == upper && data[lower_offset] == lower);
					for (size_t offset = CSTRK_OBJECT_LOWER_SURFACE_OFFSET + 1U;
						 offset < CSTRK_OBJECT_SIZE; offset++) {
						assert(data[object_offset + offset] == 0);
					}
					assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
					assert(decoded.version == CSTRK_VERSION && decoded.object_count == 1);
					assert(decoded.objects[0].legacy_id == id);
					assert(decoded.objects[0].rotation == rotation);
					assert(decoded.objects[0].surface == upper);
					assert(decoded.objects[0].lower_surface == lower);
					legacy_u8 *again = NULL;
					size_t again_size = 0;
					assert(cstrk_encode(&decoded, &again, &again_size, error, sizeof(error)));
					assert(again_size == size && memcmp(data, again, size) == 0);
					free(again);
					free(data);
				}
			}
		}
	}

	track.version = TEST_FUTURE_REVISION;
	legacy_u8 *data = NULL;
	size_t size = 0;
	assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
	assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
	assert(decoded.version == TEST_FUTURE_REVISION);
	assert(decoded.objects[0].lower_surface == CSTRK_ROAD_ICE);
	free(data);

	track.version = CSTRK_VERSION;
	track.objects[0].lower_surface = CSTRK_ROAD_PAVED;
	assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
	/* Revision 3 shared the selected surface across both roads. Loading
	 * upgrades metadata and records that same lower material explicitly. */
	LEGACY_WRITE_U16_LE(data + CSTRK_VERSION_OFFSET, CSTRK_MIN_VERSION);
	for (legacy_u8 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE; surface++) {
		data[object_offset + 5U] = surface;
		assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
		assert(decoded.version == CSTRK_VERSION);
		assert(decoded.objects[0].surface == surface);
		assert(decoded.objects[0].lower_surface == surface);
		legacy_u8 *upgraded = NULL;
		size_t upgraded_size = 0;
		assert(cstrk_encode(&decoded, &upgraded, &upgraded_size, error, sizeof(error)));
		assert(upgraded_size == size);
		assert(LEGACY_READ_U16_LE(upgraded + CSTRK_VERSION_OFFSET) == CSTRK_VERSION);
		assert(upgraded[lower_offset] == surface);
		free(upgraded);
	}
	/* Other v3 objects still have no independent lower road. */
	LEGACY_WRITE_U16_LE(data + object_offset, 1U);
	assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
	assert(decoded.objects[0].surface == CSTRK_ROAD_ICE);
	assert(decoded.objects[0].lower_surface == CSTRK_ROAD_PAVED);
	LEGACY_WRITE_U16_LE(data + object_offset, CSTRK_OVERPASS_LAST_ID);
	assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
	legacy_u8 before[sizeof(decoded)];
	memcpy(before, &decoded, sizeof(decoded));
	for (size_t offset = CSTRK_OBJECT_LOWER_SURFACE_OFFSET; offset < CSTRK_OBJECT_SIZE; offset++) {
		data[object_offset + offset] = 1;
		assert(!cstrk_decode(data, size, &decoded, error, sizeof(error)));
		assert(memcmp(before, &decoded, sizeof(decoded)) == 0);
		data[object_offset + offset] = 0;
	}
	LEGACY_WRITE_U16_LE(data + CSTRK_VERSION_OFFSET, CSTRK_VERSION);
	for (size_t offset = CSTRK_OBJECT_LOWER_SURFACE_OFFSET + 1U; offset < CSTRK_OBJECT_SIZE;
		 offset++) {
		data[object_offset + offset] = 1;
		assert(!cstrk_decode(data, size, &decoded, error, sizeof(error)));
		assert(memcmp(before, &decoded, sizeof(decoded)) == 0);
		data[object_offset + offset] = 0;
	}
	data[lower_offset] = CSTRK_ROAD_ICE + 1U;
	assert(!cstrk_decode(data, size, &decoded, error, sizeof(error)));
	data[lower_offset] = CSTRK_ROAD_DIRT;
	LEGACY_WRITE_U16_LE(data + object_offset, 1U);
	assert(!cstrk_decode(data, size, &decoded, error, sizeof(error)));
	free(data);

	track.objects[0].lower_surface = CSTRK_ROAD_ICE + 1U;
	assert(!cstrk_validate(&track, error, sizeof(error)));
	track.objects[0].lower_surface = CSTRK_ROAD_DIRT;
	track.version = CSTRK_MIN_VERSION;
	assert(!cstrk_validate(&track, error, sizeof(error)));
	/* Explicit legacy exports can represent only equal overpass surfaces.
	 * They retain revision 3 and leave the old reserved bytes zero. */
	for (legacy_u8 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE; surface++) {
		track.objects[0].surface = track.objects[0].lower_surface = surface;
		assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
		assert(LEGACY_READ_U16_LE(data + CSTRK_VERSION_OFFSET) == CSTRK_MIN_VERSION);
		assert(data[lower_offset] == 0);
		assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
		assert(decoded.version == CSTRK_VERSION);
		assert(decoded.objects[0].surface == surface);
		assert(decoded.objects[0].lower_surface == surface);
		free(data);
	}
	track.objects[0].lower_surface = CSTRK_ROAD_DIRT;
	track.version = CSTRK_VERSION;
	track.objects[0].legacy_id = 1;
	assert(!cstrk_validate(&track, error, sizeof(error)));
	track.objects[0].lower_surface = CSTRK_ROAD_PAVED;
	assert(cstrk_validate(&track, error, sizeof(error)));
	cstrk_free(&track);
	cstrk_free(&decoded);
}

int main(void)
{
	test_wire_layout();
	test_overpass_lower_surfaces();
	test_horizons_and_versions();
	test_surface_corners();
	test_original_terrain_profiles();
	test_plateaus_and_hill_slopes();
	test_multi_tile_straight_slopes();
	test_sparse_terrain_support();
	test_sloped_stunt_versions();
	test_older_revisions_reject_generalized_single_tile_slopes();
	test_notes();
	test_invalid_records();
	test_maximum_track();
	return 0;
}
