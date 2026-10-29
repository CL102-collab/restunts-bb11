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
	static const uint8_t
		expected
			[] =
				{
					'C', 'S', 'T', 'R', 'K', 0x1a, '\r', '\n', 1, 0, 32, 0, 0,
					0,	 0,	  0,   1,	0,	 1,	   0,	 2,	   0, 0, 0,	 1, 0,
					0,	 0,	  24,  0,	0,	 0,	   0,	 0,	   0, 0, /* Four shared terrain corners. */
					2,	 4, /* Dirt, southwest water triangle. */
					17,	 0,	  0,   0,	3,	 2,	   1,	 1,	   1, 0, 0,	 0, 0,
					0,	 0,	  0,   'A', '\n'};
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 1, 1, error, sizeof(error)));
	track.version = CSTRK_INITIAL_VERSION;
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
	static const legacy_u16 versions[] = {CSTRK_INITIAL_VERSION, CSTRK_VERSION, 15U, 101U,
										  LEGACY_U16_MAX};
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
			LEGACY_WRITE_U16_LE(data + CSTRK_VERSION_OFFSET, 0U);
			assert(!cstrk_decode(data, size, &decoded, error, sizeof(error)));
			assert(memcmp(&decoded, before, sizeof(decoded)) == 0);
			free(data);
		}
	}

	legacy_u8 *data = NULL;
	size_t size = 0;
	assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
	legacy_u8 *original_output = data;
	size_t original_size = size;
	decoded.version = 0;
	assert(!cstrk_validate(&decoded, error, sizeof(error)));
	assert(!cstrk_encode(&decoded, &data, &size, error, sizeof(error)));
	assert(data == original_output && size == original_size);
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
				track.version = CSTRK_INITIAL_VERSION;
				track.cells[0].surface = surface;
				track.cells[0].corner_surface = other;
				track.cells[0].half_water_corner = corner;
				legacy_u8 *data = NULL;
				size_t size = 0;
				assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
				assert(data[cell_offset] == surface);
				assert(data[cell_offset + 1U] ==
					   (corner | ((other ^ CSTRK_TERRAIN_WATER) << CSTRK_CORNER_SURFACE_SHIFT)));
				legacy_u16 expected_version = other == CSTRK_TERRAIN_WATER
												  ? CSTRK_INITIAL_VERSION
												  : CSTRK_SURFACE_CORNERS_VERSION;
				assert(LEGACY_READ_U16_LE(data + CSTRK_VERSION_OFFSET) == expected_version);
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

	/* The old codec admitted Water/Water corners, so keep those files readable. */
	track.version = CSTRK_INITIAL_VERSION;
	track.cells[0].surface = CSTRK_TERRAIN_WATER;
	track.cells[0].corner_surface = CSTRK_TERRAIN_WATER;
	track.cells[0].half_water_corner = CSTRK_WATER_NORTHWEST;
	assert(cstrk_validate(&track, error, sizeof(error)));
	legacy_u8 *legacy = NULL;
	size_t legacy_size = 0;
	assert(cstrk_encode(&track, &legacy, &legacy_size, error, sizeof(error)));
	assert(legacy[cell_offset + 1U] == CSTRK_WATER_NORTHWEST);
	assert(LEGACY_READ_U16_LE(legacy + CSTRK_VERSION_OFFSET) == CSTRK_INITIAL_VERSION);
	assert(cstrk_decode(legacy, legacy_size, &decoded, error, sizeof(error)));
	assert(decoded.cells[0].corner_surface == CSTRK_TERRAIN_WATER);
	free(legacy);

	track.version = TEST_FUTURE_REVISION;
	track.cells[0].corner_surface = CSTRK_TERRAIN_ICE;
	assert(cstrk_encode(&track, &legacy, &legacy_size, error, sizeof(error)));
	assert(LEGACY_READ_U16_LE(legacy + CSTRK_VERSION_OFFSET) == TEST_FUTURE_REVISION);
	free(legacy);
	track.version = CSTRK_INITIAL_VERSION;
	for (legacy_u8 surface = CSTRK_TERRAIN_GRASS; surface < CSTRK_TERRAIN_WATER; surface++) {
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
	assert(LEGACY_READ_U16_LE(data + CSTRK_VERSION_OFFSET) == CSTRK_INITIAL_VERSION);
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

static void test_slopes_and_unused_cells(void)
{
	struct CSTRK_TRACK track;
	struct CSTRK_TRACK decoded;
	cstrk_init(&track);
	cstrk_init(&decoded);
	assert(cstrk_create(&track, 2, 2, error, sizeof(error)));
	for (unsigned row = 0; row < 3U; row++) {
		for (unsigned column = 0; column < 3U; column++) {
			track.corner_heights[row * 3U + column] = (uint8_t)(row + column);
		}
	}
	struct CSTRK_OBJECT corner = object_at(0, 0);
	corner.legacy_id = 10;
	corner.width = 2;
	corner.height = 2;
	corner.occupied_mask = 7;
	corner.slope_x = 1;
	corner.slope_y = 1;
	assert(cstrk_add_object(&track, &corner, error, sizeof(error)));
	struct CSTRK_OBJECT decoration = object_at(1, 1);
	decoration.legacy_id = 126;
	decoration.base_height = 2;
	decoration.slope_x = 1;
	decoration.slope_y = 1;
	assert(cstrk_add_object(&track, &decoration, error, sizeof(error)));
	assert(!cstrk_add_object(&track, &decoration, error, sizeof(error)));
	assert(track.object_count == 2U);
	uint8_t *data = NULL;
	size_t size = 0;
	assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
	assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
	assert(decoded.object_count == 2U);
	assert(decoded.objects[0].occupied_mask == 7U);
	assert(decoded.objects[1].base_height == 2U);
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
	decoded.objects[0].slope_x = 1;
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
	corner.width = 2;
	corner.occupied_mask = 3;
	corner.base_height = 2;
	corner.slope_x = -1;
	assert(cstrk_add_object(&track, &corner, error, sizeof(error)));
	assert(cstrk_encode(&track, &data, &size, error, sizeof(error)));
	cstrk_init(&decoded);
	assert(cstrk_decode(data, size, &decoded, error, sizeof(error)));
	assert(decoded.objects[0].slope_x == -1);
	free(data);
	cstrk_free(&track);
	cstrk_free(&decoded);
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
		{0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0}, {215, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0},
		{1, 0, 0, 4, 0, 1, 1, 1, 0, 0, 0}, {1, 0, 0, 0, 3, 1, 1, 1, 0, 0, 0},
		{1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0}, {1, 0, 0, 0, 0, 3, 1, 1, 0, 0, 0},
		{1, 2, 0, 0, 0, 1, 1, 1, 0, 0, 0}, {1, 0, 2, 0, 0, 1, 1, 1, 0, 0, 0},
		{1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0}, {1, 0, 0, 0, 0, 1, 1, 2, 0, 0, 0},
		{1, 0, 0, 0, 0, 1, 1, 1, 5, 0, 0}, {1, 0, 0, 0, 0, 1, 1, 1, 0, 2, 0},
		{1, 0, 0, 0, 0, 1, 1, 1, 0, 0, -2}};
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

int main(void)
{
	test_wire_layout();
	test_horizons_and_versions();
	test_surface_corners();
	test_slopes_and_unused_cells();
	test_notes();
	test_invalid_records();
	test_maximum_track();
	return 0;
}
