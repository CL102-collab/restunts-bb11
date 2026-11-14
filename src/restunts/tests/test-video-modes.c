/* Authored DOS samples remain independent of the unchanged VGA framebuffer. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../c/resource.h"
#include "../c/video/video_pages.h"
#include "../platform/sdl3/video_mode.h"
#include "../platform/sdl3/video_mode_bitmaps.h"
#include "../platform/sdl3/video_mode_materials.h"

enum {
	TEST_WIDTH = 320,
	TEST_HEIGHT = 200,
	TEST_BYTES = 65536,
	TEST_TABLE_SIZE = 256,
	TEST_RESOURCE_SIZE = 1024,
	TEST_RESOURCE_COUNT = 4,
	TEST_TABLE_WIDTH = 16,
	TEST_BITMAP_WIDTH = 8,
	TEST_BITMAP_HEIGHT = 2,
	TEST_BITMAP_COLOR = 42,
	TEST_BITMAP_OTHER_COLOR = 43,
	TEST_STORED_ROW = 1,
	TEST_DRAW_ROW = 6,
	TEST_NATIVE_EGA_COLOR = 12,
	TEST_NATIVE_CGA_WORD = 0x1BE4,
	TEST_EGA_PAIR = 0x24,
	TEST_CGA_TILE = 0x1B,
	TEST_RED_COMPONENT_SHIFT = 16,
	TEST_GREEN_COMPONENT_SHIFT = 8
};
#define TEST_FNV_OFFSET 2166136261U
#define TEST_FNV_PRIME 16777619U

static legacy_u8 screen[TEST_BYTES];
static legacy_u8 alternate[TEST_BYTES];
static legacy_u8 rows[TEST_HEIGHT * LEGACY_WORD_BYTES];
static legacy_u8 resource[TEST_RESOURCE_SIZE];
static legacy_u32 resource_size;
static struct SHAPE2D *source_shape;
static legacy_u32 palette[TEST_TABLE_SIZE];

/* The production page helpers perform the actual VGA writes. HyperVision is
 * outside this unit fixture; its companion hooks do not affect these bytes. */
void hires_write(const legacy_u8 *base, legacy_u16 offset, legacy_u8 color)
{
	(void)base;
	(void)offset;
	(void)color;
}

void hires_raster(const legacy_u8 *destination, legacy_u16 destination_offset,
				  const legacy_u8 *source, legacy_u16 source_offset, legacy_u16 count,
				  legacy_s16 operation, const legacy_u8 *map)
{
	(void)destination;
	(void)destination_offset;
	(void)source;
	(void)source_offset;
	(void)count;
	(void)operation;
	(void)map;
}

static legacy_u32 table_hash(const legacy_u16 *table)
{
	legacy_u32 hash = TEST_FNV_OFFSET;
	for (legacy_u16 index = 0; index < VIDEO_MODE_MATERIAL_COUNT; index++) {
		hash = (hash ^ (table[index] & LEGACY_U8_MAX)) * TEST_FNV_PRIME;
		hash = (hash ^ (table[index] >> LEGACY_BYTE_BITS)) * TEST_FNV_PRIME;
	}
	return hash;
}

static void test_original_material_facts(void)
{
	/* Independent little-endian byte hashes of decoded original driver tables.
	 * See fixtures/video-mode-material-reference.json for offsets and SHA256s. */
	assert(VIDEO_MODE_MATERIAL_COUNT == 129U);
	assert(table_hash(video_mode_ega_primary) == 0x3C28D83AU);
	assert(table_hash(video_mode_ega_secondary) == 0xFD250415U);
	assert(table_hash(video_mode_ega_pattern_types) == 0x761895E7U);
	assert(table_hash(video_mode_ega_pattern_bits) == 0x56C5ADC7U);
	assert(table_hash(video_mode_cga_primary) == 0x7260FF33U);
	assert(table_hash(video_mode_cga_secondary) == 0x7260FF33U);
	assert(table_hash(video_mode_cga_pattern_types) == 0x0C00E3B5U);
	assert(table_hash(video_mode_cga_pattern_bits) == 0xC71B9085U);
}

static void select_target(legacy_u8 *pixels)
{
	struct SPRITE sprite = {0};
	sprite.sprite_bitmapptr = (struct SHAPE2D *)pixels;
	sprite.sprite_lineofs = rows;
	sprite.sprite_pitch = TEST_WIDTH;
	sprite.sprite_bottom = TEST_HEIGHT;
	sprite.sprite_buffer_width = TEST_WIDTH;
	sdl3_video_mode_target(&sprite);
}

static void initialize_mode(legacy_u8 mode)
{
	sdl3_video_mode_shutdown();
	sdl3_video_set_startup_mode(mode);
	memset(screen, 0, sizeof(screen));
	memset(alternate, 0, sizeof(alternate));
	for (legacy_u16 y = 0; y < TEST_HEIGHT; y++) {
		LEGACY_WRITE_U16_LE(rows + y * LEGACY_WORD_BYTES, y * TEST_WIDTH);
	}
	for (legacy_u16 index = 0; index < TEST_TABLE_SIZE; index++) {
		palette[index] = 0xFF000000U | ((legacy_u32)index << TEST_RED_COMPONENT_SHIFT) |
						 ((legacy_u32)index << TEST_GREEN_COMPONENT_SHIFT) | index;
	}
	palette[TEST_BITMAP_COLOR] = palette[3];
	sdl3_video_mode_palette(palette);
	select_target(screen);
}

static void register_authored_bitmap(void)
{
	static const legacy_char names[TEST_RESOURCE_COUNT][RESOURCE_FILE_IDENTIFIER_SIZE + 1U] = {
		"!eg0", "!cg0", "test", "!pal"};
	memset(resource, 0, sizeof(resource));
	LEGACY_WRITE_U16_LE(resource + RESOURCE_FILE_COUNT_OFFSET, TEST_RESOURCE_COUNT);
	legacy_u32 base =
		RESOURCE_FILE_DIRECTORY_OFFSET +
		TEST_RESOURCE_COUNT * (RESOURCE_FILE_IDENTIFIER_SIZE + RESOURCE_FILE_OFFSET_SIZE);
	legacy_u32 cursor = base;
	for (legacy_u16 index = 0; index < TEST_RESOURCE_COUNT; index++) {
		memcpy(resource + RESOURCE_FILE_DIRECTORY_OFFSET + index * RESOURCE_FILE_IDENTIFIER_SIZE,
			   names[index], RESOURCE_FILE_IDENTIFIER_SIZE);
		LEGACY_WRITE_U32_LE(resource + RESOURCE_FILE_DIRECTORY_OFFSET +
								TEST_RESOURCE_COUNT * RESOURCE_FILE_IDENTIFIER_SIZE +
								index * RESOURCE_FILE_OFFSET_SIZE,
							cursor - base);
		legacy_u16 width = index < 2U ? TEST_TABLE_WIDTH : TEST_BITMAP_WIDTH;
		legacy_u16 height = index < 2U ? TEST_TABLE_WIDTH : TEST_BITMAP_HEIGHT;
		LEGACY_WRITE_U16_LE(resource + cursor, width);
		LEGACY_WRITE_U16_LE(resource + cursor + sizeof(legacy_u16), height);
		if (index == 2U) {
			source_shape = (struct SHAPE2D *)(resource + cursor);
			LEGACY_WRITE_U16_LE(resource + cursor + offsetof(struct SHAPE2D, position_y),
								TEST_STORED_ROW);
		}
		cursor += SHAPE2D_HEADER_SIZE;
		if (index < 2U) {
			resource[cursor + TEST_BITMAP_COLOR] = index == 0 ? TEST_EGA_PAIR : TEST_CGA_TILE;
		} else {
			memset(resource + cursor, TEST_BITMAP_COLOR, width * height);
		}
		cursor += width * height;
	}
	assert(cursor <= sizeof(resource));
	resource_size = cursor;
	LEGACY_WRITE_U32_LE(resource + RESOURCE_FILE_SIZE_OFFSET, cursor);
	sdl3_video_bitmap_register(resource, resource_size, 0);
}

static void draw_authored_bitmap(void)
{
	sdl3_video_mode_source_begin(source_shape, TEST_DRAW_ROW);
	for (legacy_u16 y = 0; y < TEST_BITMAP_HEIGHT; y++) {
		for (legacy_u16 x = 0; x < TEST_BITMAP_WIDTH; x++) {
			video_pages_write_pixel(screen, (TEST_DRAW_ROW + y) * TEST_WIDTH + x,
									TEST_BITMAP_COLOR);
		}
	}
	sdl3_video_mode_source_end();
}

/* Stored Y=1, drawn Y=6 retains the original bitmap's odd first-row phase. */
static const legacy_u8 bitmap_ega[TEST_BITMAP_HEIGHT][TEST_BITMAP_WIDTH] = {
	{4, 2, 4, 2, 4, 2, 4, 2}, {2, 4, 2, 4, 2, 4, 2, 4}};
static const legacy_u8 bitmap_cga[TEST_BITMAP_HEIGHT][TEST_BITMAP_WIDTH] = {
	{5, 7, 5, 7, 5, 7, 5, 7}, {0, 3, 0, 3, 0, 3, 0, 3}};

static void test_bitmap_patterns_and_copies(void)
{
	for (legacy_u8 mode = SDL3_VIDEO_MODE_EGA; mode <= SDL3_VIDEO_MODE_CGA; mode++) {
		initialize_mode(mode);
		register_authored_bitmap();
		legacy_u8 original[TEST_RESOURCE_SIZE];
		memcpy(original, resource, sizeof(original));
		draw_authored_bitmap();
		const legacy_u8(*expected)[TEST_BITMAP_WIDTH] =
			mode == SDL3_VIDEO_MODE_EGA ? bitmap_ega : bitmap_cga;
		const legacy_u8 *actual = sdl3_video_mode_framebuffer(screen);
		for (legacy_u16 y = 0; y < TEST_BITMAP_HEIGHT; y++) {
			legacy_u16 offset = (TEST_DRAW_ROW + y) * TEST_WIDTH;
			assert(memcmp(actual + offset, expected[y], TEST_BITMAP_WIDTH) == 0);
			for (legacy_u16 x = 0; x < TEST_BITMAP_WIDTH; x++) {
				assert(screen[offset + x] == TEST_BITMAP_COLOR);
			}
		}
		assert(memcmp(resource, original, sizeof(original)) == 0);

		/* Save/restore at a different row and X phase must copy actual hardware
		 * pixels, rather than redithering a material at its new coordinates. */
		select_target(alternate);
		legacy_u16 from = TEST_DRAW_ROW * TEST_WIDTH;
		legacy_u16 to = (TEST_DRAW_ROW + 1U) * TEST_WIDTH + 1U;
		for (legacy_u16 x = 0; x < TEST_BITMAP_WIDTH; x++) {
			video_pages_write_pixel(alternate, to + x, screen[from + x]);
			sdl3_video_mode_copy_sample(alternate, to + x, screen, from + x);
		}
		actual = sdl3_video_mode_framebuffer(alternate);
		assert(memcmp(actual + to, expected[0], TEST_BITMAP_WIDTH) == 0);

		/* The real shifted-copy caller walks backward for a rightward overlap. */
		select_target(screen);
		for (legacy_u16 x = TEST_BITMAP_WIDTH; x != 0; x--) {
			video_pages_write_pixel(screen, from + x, screen[from + x - 1U]);
			sdl3_video_mode_copy_sample(screen, from + x, screen, from + x - 1U);
		}
		actual = sdl3_video_mode_framebuffer(screen);
		assert(memcmp(actual + from + 1U, expected[0], TEST_BITMAP_WIDTH) == 0);
	}
}

static void test_bitmap_translation_and_scaling(void)
{
	static const legacy_u8 scaled_ega[] = {4, 4, 2, 2, 4, 4, 2, 2};
	static const legacy_u8 scaled_cga[] = {5, 5, 7, 7, 5, 5, 7, 7};
	const legacy_u16 draw_x = 3;
	const legacy_u16 scaled_x = 7;
	const legacy_u16 scaled_y = TEST_DRAW_ROW + 4U;
	for (legacy_u8 mode = SDL3_VIDEO_MODE_EGA; mode <= SDL3_VIDEO_MODE_CGA; mode++) {
		initialize_mode(mode);
		register_authored_bitmap();
		sdl3_video_mode_source_begin_at(source_shape, draw_x, TEST_DRAW_ROW);
		for (legacy_u16 y = 0; y < TEST_BITMAP_HEIGHT; y++) {
			for (legacy_u16 x = 0; x < TEST_BITMAP_WIDTH; x++) {
				video_pages_write_pixel(screen, (TEST_DRAW_ROW + y) * TEST_WIDTH + draw_x + x,
										TEST_BITMAP_COLOR);
			}
		}
		sdl3_video_mode_source_end();
		const legacy_u8(*expected)[TEST_BITMAP_WIDTH] =
			mode == SDL3_VIDEO_MODE_EGA ? bitmap_ega : bitmap_cga;
		const legacy_u8 *actual = sdl3_video_mode_framebuffer(screen);
		for (legacy_u16 y = 0; y < TEST_BITMAP_HEIGHT; y++) {
			assert(memcmp(actual + (TEST_DRAW_ROW + y) * TEST_WIDTH + draw_x, expected[y],
						  TEST_BITMAP_WIDTH) == 0);
		}

		/* Sampling one source pixel twice must duplicate its already-authored
		 * hardware color instead of dithering it again at each output pixel. */
		sdl3_video_mode_source_begin_at(source_shape, scaled_x, scaled_y);
		for (legacy_u16 y = 0; y < TEST_BITMAP_HEIGHT; y++) {
			for (legacy_u16 x = 0; x < TEST_BITMAP_WIDTH; x++) {
				sdl3_video_mode_source_sample(x / 2U, y / 2U);
				video_pages_write_pixel(screen, (scaled_y + y) * TEST_WIDTH + scaled_x + x,
										TEST_BITMAP_COLOR);
			}
		}
		sdl3_video_mode_source_end();
		actual = sdl3_video_mode_framebuffer(screen);
		for (legacy_u16 y = 0; y < TEST_BITMAP_HEIGHT; y++) {
			assert(memcmp(actual + (scaled_y + y) * TEST_WIDTH + scaled_x,
						  mode == SDL3_VIDEO_MODE_EGA ? scaled_ega : scaled_cga,
						  TEST_BITMAP_WIDTH) == 0);
		}
	}
}

static void draw_native(legacy_u8 mode, legacy_u16 word)
{
	initialize_mode(mode);
	sdl3_video_mode_primitive_begin();
	for (legacy_u16 y = 0; y < TEST_BITMAP_HEIGHT; y++) {
		for (legacy_u16 x = 0; x < TEST_BITMAP_WIDTH; x++) {
			video_pages_write_pixel(screen, y * TEST_WIDTH + x, TEST_BITMAP_COLOR);
		}
	}
	legacy_u16 words[] = {word, 0, 0};
	sdl3_video_mode_primitive_capture(words);
	for (legacy_u16 y = 0; y < TEST_BITMAP_HEIGHT; y++) {
		for (legacy_u16 x = 0; x < TEST_BITMAP_WIDTH; x++) {
			video_pages_write_pixel(screen, y * TEST_WIDTH + x, SDL3_VIDEO_NATIVE_PRIMARY);
		}
	}
	sdl3_video_mode_primitive_end();
}

static void test_native_material_phases(void)
{
	static const legacy_u8 cga[TEST_BITMAP_HEIGHT][TEST_BITMAP_WIDTH] = {{0, 3, 5, 7, 0, 3, 5, 7},
																		 {7, 5, 3, 0, 7, 5, 3, 0}};
	for (legacy_u8 mode = SDL3_VIDEO_MODE_EGA; mode <= SDL3_VIDEO_MODE_CGA; mode++) {
		draw_native(mode,
					mode == SDL3_VIDEO_MODE_EGA ? TEST_NATIVE_EGA_COLOR : TEST_NATIVE_CGA_WORD);
		const legacy_u8 *actual = sdl3_video_mode_framebuffer(screen);
		for (legacy_u16 y = 0; y < TEST_BITMAP_HEIGHT; y++) {
			for (legacy_u16 x = 0; x < TEST_BITMAP_WIDTH; x++) {
				legacy_u16 offset = y * TEST_WIDTH + x;
				assert(actual[offset] ==
					   (mode == SDL3_VIDEO_MODE_EGA ? TEST_NATIVE_EGA_COLOR : cga[y][x]));
				assert(screen[offset] == TEST_BITMAP_COLOR);
			}
		}
	}
}

static void test_masks_transparency_and_xor(void)
{
	draw_native(SDL3_VIDEO_MODE_EGA, TEST_NATIVE_EGA_COLOR);
	register_authored_bitmap();
	legacy_u8 mask[] = {LEGACY_U8_MAX, 0, LEGACY_U8_MAX, 0};
	video_pages_raster_span(screen, 0, mask, 0, sizeof(mask), SHAPE2D_RASTER_AND, NULL);
	static const legacy_u8 masked[] = {TEST_NATIVE_EGA_COLOR, 0, TEST_NATIVE_EGA_COLOR, 0};
	assert(memcmp(sdl3_video_mode_framebuffer(screen), masked, sizeof(masked)) == 0);

	legacy_u8 overlay[] = {0, TEST_BITMAP_COLOR, 0, TEST_BITMAP_COLOR};
	sdl3_video_mode_source_begin(source_shape, 0);
	video_pages_raster_span(screen, 0, overlay, 0, sizeof(overlay), SHAPE2D_RASTER_OR, NULL);
	sdl3_video_mode_source_end();
	static const legacy_u8 combined[] = {TEST_NATIVE_EGA_COLOR, 2, TEST_NATIVE_EGA_COLOR, 2};
	assert(memcmp(sdl3_video_mode_framebuffer(screen), combined, sizeof(combined)) == 0);

	legacy_u8 map[TEST_TABLE_SIZE];
	for (legacy_u16 index = 0; index < TEST_TABLE_SIZE; index++) {
		map[index] = (legacy_u8)index;
	}
	map[TEST_BITMAP_COLOR] = SHAPE2D_TRANSPARENT_COLOR;
	map[TEST_BITMAP_OTHER_COLOR] = TEST_BITMAP_COLOR;
	legacy_u8 source[] = {TEST_BITMAP_COLOR, TEST_BITMAP_OTHER_COLOR};
	sdl3_video_mode_source_begin(source_shape, 0);
	video_pages_raster_span(screen, 2, source, 0, sizeof(source), SHAPE2D_RASTER_MAP, map);
	sdl3_video_mode_source_end();
	const legacy_u8 *actual = sdl3_video_mode_framebuffer(screen);
	assert(actual[2] == TEST_NATIVE_EGA_COLOR);
	assert(actual[3] == 2);
	assert(screen[2] == TEST_BITMAP_COLOR && screen[3] == TEST_BITMAP_COLOR);

	legacy_u8 original = actual[2];
	for (legacy_u16 repeat = 0; repeat < 2U; repeat++) {
		sdl3_video_mode_xor_begin(screen, 2, TEST_BITMAP_COLOR);
		video_pages_write_pixel(screen, 2, screen[2] ^ TEST_BITMAP_COLOR);
	}
	assert(sdl3_video_mode_framebuffer(screen)[2] == original);
	assert(screen[2] == TEST_BITMAP_COLOR);

	legacy_u32 generation = sdl3_video_mode_generation();
	sdl3_video_mode_forget(screen, sizeof(screen));
	assert(sdl3_video_mode_generation() != generation);
	assert(sdl3_video_mode_framebuffer(screen)[2] == 3);
}

legacy_int main(void)
{
	assert(sdl3_video_mode_selected == SDL3_VIDEO_MODE_VGA);
	assert(sdl3_video_mode_framebuffer(screen) == screen);
	test_original_material_facts();
	test_bitmap_patterns_and_copies();
	test_bitmap_translation_and_scaling();
	test_native_material_phases();
	test_masks_transparency_and_xor();
	sdl3_video_mode_shutdown();
	sdl3_video_set_startup_mode(SDL3_VIDEO_MODE_VGA);
	assert(sdl3_video_mode_framebuffer(screen) == screen);
	puts("Original VGA/EGA/CGA material facts, bitmap phases, copies, masks and native samples "
		 "passed.");
	return 0;
}
