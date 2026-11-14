/* Authored DOS bitmap maps remain independent of the VGA pixel resources. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../platform/sdl3/video_mode_bitmaps.h"
#include "../platform/sdl3/video_mode.h"
#include "../c/shape2d/shape2d.h"
#include "../c/resource.h"

#define TEST_RESOURCE_CAPACITY 4096U
#define TEST_TABLE_SIZE 256U
#define TEST_TABLE_COUNT 4U
#define TEST_SHAPE_INDEX 4U
#define TEST_DIRECTORY_COUNT 5U
#define TEST_PALETTE_DIRECTORY_COUNT 6U
#define TEST_TABLE_WIDTH 16U
#define TEST_TABLE_HEIGHT 16U
#define TEST_REPEAT_COUNT 128U
#define TEST_LITERAL_LIMIT 127U
#define TEST_SOURCE_ROW 3U
#define TEST_TABLE_SEED_STEP 37U
#define TEST_TABLE_SEED_BASE 19U
#define TEST_BITMAP_COLOR 42U

struct BITMAP_FIXTURE {
	legacy_u8 bytes[TEST_RESOURCE_CAPACITY];
	legacy_u32 size;
	legacy_u32 shape_offset;
	legacy_u32 table_offset[TEST_TABLE_COUNT];
};

static legacy_u8 expected_color(legacy_u8 table, legacy_u16 index)
{
	legacy_u8 seed = (legacy_u8)(table * TEST_TABLE_SEED_STEP + TEST_TABLE_SEED_BASE);
	return index < TEST_REPEAT_COUNT ? seed : (legacy_u8)(index ^ seed);
}

static void build_fixture(struct BITMAP_FIXTURE *fixture, legacy_u8 parsed, legacy_u8 palette)
{
	static const legacy_char names[][RESOURCE_FILE_IDENTIFIER_SIZE + 1U] = {"!eg0", "!eg3", "!cg0",
																			"!cg3", "test", "!pal"};
	memset(fixture, 0, sizeof(*fixture));
	legacy_u16 count = palette ? TEST_PALETTE_DIRECTORY_COUNT : TEST_DIRECTORY_COUNT;
	legacy_u32 base = RESOURCE_FILE_DIRECTORY_OFFSET +
					  count * (RESOURCE_FILE_IDENTIFIER_SIZE + RESOURCE_FILE_OFFSET_SIZE);
	legacy_u32 offset = base;
	LEGACY_WRITE_U16_LE(fixture->bytes + RESOURCE_FILE_COUNT_OFFSET, count);
	for (legacy_u16 shape = 0; shape < count; shape++) {
		memcpy(fixture->bytes + RESOURCE_FILE_DIRECTORY_OFFSET +
				   shape * RESOURCE_FILE_IDENTIFIER_SIZE,
			   names[shape], RESOURCE_FILE_IDENTIFIER_SIZE);
		LEGACY_WRITE_U32_LE(fixture->bytes + RESOURCE_FILE_DIRECTORY_OFFSET +
								count * RESOURCE_FILE_IDENTIFIER_SIZE +
								shape * RESOURCE_FILE_OFFSET_SIZE,
							offset - base);
		if (shape < TEST_TABLE_COUNT) {
			fixture->table_offset[shape] = offset;
			LEGACY_WRITE_U16_LE(fixture->bytes + offset, TEST_TABLE_WIDTH);
			LEGACY_WRITE_U16_LE(fixture->bytes + offset + sizeof(legacy_u16), TEST_TABLE_HEIGHT);
			offset += SHAPE2D_HEADER_SIZE;
			if (parsed) {
				/* Both positive repeat runs and negative literal runs occur in
				 * original parsed resource tables. Keep each run at most 127. */
				fixture->bytes[offset++] = TEST_LITERAL_LIMIT;
				fixture->bytes[offset++] = expected_color((legacy_u8)shape, 0);
				fixture->bytes[offset++] = 1U;
				fixture->bytes[offset++] = expected_color((legacy_u8)shape, 0);
				fixture->bytes[offset++] = (legacy_u8)(0U - TEST_LITERAL_LIMIT);
				for (legacy_u16 index = TEST_REPEAT_COUNT; index < TEST_TABLE_SIZE - 1U; index++) {
					fixture->bytes[offset++] = expected_color((legacy_u8)shape, index);
				}
				fixture->bytes[offset++] = LEGACY_U8_MAX;
				fixture->bytes[offset++] = expected_color((legacy_u8)shape, TEST_TABLE_SIZE - 1U);
				fixture->bytes[offset++] = 0;
			} else {
				for (legacy_u16 index = 0; index < TEST_TABLE_SIZE; index++) {
					fixture->bytes[offset++] = expected_color((legacy_u8)shape, index);
				}
			}
		} else {
			if (shape == TEST_SHAPE_INDEX) {
				fixture->shape_offset = offset;
			}
			LEGACY_WRITE_U16_LE(fixture->bytes + offset, 1U);
			LEGACY_WRITE_U16_LE(fixture->bytes + offset + sizeof(legacy_u16), 1U);
			LEGACY_WRITE_U16_LE(fixture->bytes + offset + offsetof(struct SHAPE2D, position_y),
								TEST_SOURCE_ROW);
			offset += SHAPE2D_HEADER_SIZE;
			fixture->bytes[offset++] = parsed ? 1U : TEST_BITMAP_COLOR;
			if (parsed) {
				fixture->bytes[offset++] = TEST_BITMAP_COLOR;
				fixture->bytes[offset++] = 0;
			}
		}
	}
	assert(offset <= sizeof(fixture->bytes));
	fixture->size = offset;
	LEGACY_WRITE_U32_LE(fixture->bytes + RESOURCE_FILE_SIZE_OFFSET, offset);
}

static void assert_table(const legacy_u8 *table, legacy_u8 expected)
{
	assert(table != NULL);
	for (legacy_u16 index = 0; index < TEST_TABLE_SIZE; index++) {
		assert(table[index] == expected_color(expected, index));
	}
}

static void test_authored_tables(legacy_u8 parsed)
{
	struct BITMAP_FIXTURE fixture;
	struct BITMAP_FIXTURE other;
	build_fixture(&fixture, parsed, 1);
	build_fixture(&other, parsed, 0);
	legacy_u8 original[TEST_RESOURCE_CAPACITY];
	memcpy(original, fixture.bytes, sizeof(original));
	sdl3_video_bitmap_register(fixture.bytes, fixture.size, parsed);
	sdl3_video_bitmap_register(other.bytes, other.size, parsed);
	const legacy_u8 *shape = fixture.bytes + fixture.shape_offset;
	assert(sdl3_video_bitmap_source_phase(shape) == 1);
	assert(sdl3_video_bitmap_source_phase(other.bytes) == 1);
	assert(sdl3_video_bitmap_source_phase(NULL) == 0);
	assert_table(sdl3_video_bitmap_table(shape, SDL3_VIDEO_MODE_EGA), 0);
	assert_table(sdl3_video_bitmap_table(shape, SDL3_VIDEO_MODE_CGA), 2);
	assert_table(sdl3_video_bitmap_default_table(SDL3_VIDEO_MODE_EGA), 0);
	assert_table(sdl3_video_bitmap_default_table(SDL3_VIDEO_MODE_CGA), 2);
	assert(sdl3_video_bitmap_table(shape, SDL3_VIDEO_MODE_VGA) == NULL);

	sdl3_video_bitmap_set_raw(fixture.bytes);
	assert(sdl3_video_bitmap_source_phase(shape) == 0);
	sdl3_video_bitmap_select(fixture.bytes, 3);
	assert_table(sdl3_video_bitmap_table(shape, SDL3_VIDEO_MODE_EGA), 1);
	assert_table(sdl3_video_bitmap_table(shape, SDL3_VIDEO_MODE_CGA), 3);
	assert_table(sdl3_video_bitmap_default_table(SDL3_VIDEO_MODE_EGA), 1);
	assert_table(sdl3_video_bitmap_table(other.bytes + other.shape_offset, SDL3_VIDEO_MODE_EGA), 0);
	/* Missing numbered tables fall back within their own source resource. */
	sdl3_video_bitmap_select((void *)shape, 7);
	assert_table(sdl3_video_bitmap_table(shape, SDL3_VIDEO_MODE_EGA), 0);
	assert_table(sdl3_video_bitmap_table(shape, SDL3_VIDEO_MODE_CGA), 2);
	sdl3_video_bitmap_select(fixture.bytes, 3);
	sdl3_video_bitmap_select(fixture.bytes, LEGACY_U8_MAX);
	assert_table(sdl3_video_bitmap_table(shape, SDL3_VIDEO_MODE_EGA), 1);
	assert(memcmp(original, fixture.bytes, sizeof(original)) == 0);

	assert(sdl3_video_bitmap_row_phase((const struct SHAPE2D *)shape, TEST_SOURCE_ROW) == 0);
	assert(sdl3_video_bitmap_row_phase((const struct SHAPE2D *)shape, TEST_SOURCE_ROW + 1U) == 1);
	assert(sdl3_video_bitmap_row_phase(NULL, TEST_SOURCE_ROW) == 0);

	/* Partial overlaps must discard the resource and its copied UI default. */
	sdl3_video_bitmap_forget(shape + SHAPE2D_HEADER_SIZE, 1U);
	assert(sdl3_video_bitmap_table(shape, SDL3_VIDEO_MODE_EGA) == NULL);
	assert(sdl3_video_bitmap_source_phase(shape) == 0);
	assert(sdl3_video_bitmap_default_table(SDL3_VIDEO_MODE_EGA) == NULL);
	assert_table(sdl3_video_bitmap_table(other.bytes, SDL3_VIDEO_MODE_CGA), 2);
	sdl3_video_bitmap_reset();
	assert(sdl3_video_bitmap_table(other.bytes, SDL3_VIDEO_MODE_CGA) == NULL);
}

static void test_invalid_maps(void)
{
	struct BITMAP_FIXTURE fixture;
	build_fixture(&fixture, 1, 1);
	/* A short selected table cannot consume the following table's pixels. */
	fixture.bytes[fixture.table_offset[1] + SHAPE2D_HEADER_SIZE] = 0;
	sdl3_video_bitmap_register(fixture.bytes, fixture.size, 1);
	sdl3_video_bitmap_select(fixture.bytes, 3);
	assert_table(sdl3_video_bitmap_table(fixture.bytes, SDL3_VIDEO_MODE_EGA), 0);
	assert_table(sdl3_video_bitmap_table(fixture.bytes, SDL3_VIDEO_MODE_CGA), 3);

	/* A known tableless resource at a reused address must not inherit maps. */
	for (legacy_u16 index = 0; index < TEST_TABLE_COUNT; index++) {
		fixture.bytes[RESOURCE_FILE_DIRECTORY_OFFSET + index * RESOURCE_FILE_IDENTIFIER_SIZE] = '?';
	}
	sdl3_video_bitmap_register(fixture.bytes, fixture.size, 1);
	assert(sdl3_video_bitmap_table(fixture.bytes, SDL3_VIDEO_MODE_EGA) == NULL);
	assert(sdl3_video_bitmap_table(fixture.bytes, SDL3_VIDEO_MODE_CGA) == NULL);
	assert(sdl3_video_bitmap_default_table(SDL3_VIDEO_MODE_EGA) == NULL);

	build_fixture(&fixture, 0, 1);
	sdl3_video_bitmap_register(fixture.bytes, fixture.size, 0);
	assert(sdl3_video_bitmap_source_phase(fixture.bytes) == 1);
	sdl3_video_bitmap_set_raw(fixture.bytes);
	assert(sdl3_video_bitmap_source_phase(fixture.bytes) == 0);
	sdl3_video_bitmap_register(fixture.bytes, fixture.size, 0);
	assert(sdl3_video_bitmap_source_phase(fixture.bytes) == 1);
	sdl3_video_bitmap_register(fixture.bytes, 1U, 0);
	assert(sdl3_video_bitmap_default_table(SDL3_VIDEO_MODE_EGA) == NULL);
	assert(sdl3_video_bitmap_table(fixture.bytes, SDL3_VIDEO_MODE_EGA) == NULL);
	assert(sdl3_video_bitmap_source_phase(fixture.bytes) == 0);
	sdl3_video_bitmap_register(NULL, fixture.size, 0);
	sdl3_video_bitmap_reset();
}

legacy_int main(void)
{
	test_authored_tables(0);
	test_authored_tables(1);
	test_invalid_maps();
	puts("Authored VGA bitmap maps, numbered selection, parsed runs and resource lifetime passed.");
	return 0;
}
