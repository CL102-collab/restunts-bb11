#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/platform.h"
#include "../c/memmgr.h"
#include "../c/resource.h"
#include "../c/shape2d.h"
#include "../c/shape2d_internal.h"
#include "../c/ui_text.h"

#define TEST_SOURCE_SEGMENT 0x2000U
#define TEST_BITMAP_SEGMENT 0x5000U
#define TEST_SPRITE_SEGMENT 0x7000U
#define TEST_LINE_OFFSET 512U

/* Fingerprints were captured from production at 43fba20d. The segmented
 * arena keeps source-offset wrapping and framebuffer writes observable while
 * valid resource fixtures exercise parser output normalization and unflip. */
static union {
	legacy_u32 alignment;
	legacy_u8 bytes[0x100000];
} memory;
legacy_u8 far *active_font_definition;

void *_memcpy(void *destination, const void *source, legacy_u16 length)
{
	return memmove(destination, source, length);
}

static struct SHAPE2D *aliased_shape;
static legacy_u16 shape_offset;
static legacy_u16 resized_offset, resized_segment, resized_paragraphs;
static legacy_u32 fingerprint;

void far *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	size_t address = (size_t)segment * 16U + offset;

	assert(address < sizeof(memory.bytes));
	return memory.bytes + address;
}

legacy_u16 dos_memory_pointer_segment(const void far *pointer)
{
	if (pointer == &drawing_sprite || pointer == drawing_sprite.sprite_lineofs) {
		return TEST_SPRITE_SEGMENT;
	}
	if (pointer == aliased_shape) {
		return TEST_SOURCE_SEGMENT;
	}
	ptrdiff_t address = (const legacy_u8 *)pointer - memory.bytes;
	assert(address >= 0 && (size_t)address < sizeof(memory.bytes));
	return (legacy_u16)((size_t)address >> 4);
}

legacy_u16 dos_memory_pointer_offset(const void far *pointer)
{
	if (pointer == &drawing_sprite) {
		return 0;
	}
	if (pointer == drawing_sprite.sprite_lineofs) {
		return TEST_LINE_OFFSET;
	}
	if (pointer == aliased_shape) {
		return shape_offset;
	}
	ptrdiff_t address = (const legacy_u8 *)pointer - memory.bytes;
	assert(address >= 0 && (size_t)address < sizeof(memory.bytes));
	return (legacy_u16)((size_t)address & 15U);
}

legacy_u16 mmgr_resize_memory(legacy_u16 offset, legacy_u16 segment, legacy_u16 paragraphs)
{
	resized_offset = offset;
	resized_segment = segment;
	resized_paragraphs = paragraphs;
	return paragraphs;
}

void fatal_error(const legacy_s8 *format, ...)
{
	fprintf(stderr, "Unexpected fatal error: %s\n", format);
	abort();
}

static void hash_word(legacy_u16 value)
{
	fingerprint = (fingerprint ^ value) * 16777619UL;
}

static void hash_bytes(const legacy_u8 *data, legacy_u32 count)
{
	for (legacy_u32 index = 0; index < count; index++) {
		hash_word(data[index]);
	}
}

static void hash_bitmap(void)
{
	hash_bytes(dos_memory_make_pointer(TEST_BITMAP_SEGMENT, 0), 65536);
}

static void reset_bitmap(void)
{
	aliased_shape = NULL;
	memset(&drawing_sprite, 0, sizeof(drawing_sprite));
	drawing_sprite.sprite_bitmapptr = dos_memory_make_pointer(TEST_BITMAP_SEGMENT, 0);
	drawing_sprite.sprite_lineofs = dos_memory_make_pointer(TEST_SPRITE_SEGMENT, TEST_LINE_OFFSET);
	drawing_sprite.sprite_pitch = 64;
	sprite_set_target_clip_bounds(0, 64, 0, 48);
	memset(drawing_sprite.sprite_bitmapptr, 0x5a, 65536);
	legacy_u8 *lines = dos_memory_make_pointer(TEST_SPRITE_SEGMENT, TEST_LINE_OFFSET);
	for (legacy_u32 index = 0; index < 32768; index++) {
		LEGACY_WRITE_U16_LE(lines + index * 2, (legacy_u16)(index * 64));
	}
	resized_offset = resized_segment = resized_paragraphs = 0;
}

static struct SHAPE2D *make_shape(legacy_u16 width, legacy_u16 height, legacy_u16 offset)
{
	legacy_u8 *source = dos_memory_make_pointer(TEST_SOURCE_SEGMENT, 0);

	for (legacy_u32 index = 0; index < 65536; index++) {
		source[index] = (legacy_u8)(index % 7 == 0 ? 255 : index * 37 + 11);
	}
	struct SHAPE2D *shape = dos_memory_make_pointer(TEST_SOURCE_SEGMENT, offset);
	memset(shape, 0, sizeof(*shape));
	shape->width = width;
	shape->height = height;
	shape->position_x = 4;
	shape->position_y = 3;
	aliased_shape = shape;
	shape_offset = offset;
	return shape;
}

static void test_lines(void)
{
	legacy_u16 line[10];
	static const legacy_u16 fractions[] = {0, 0x7fff, 0x8000, 0xffff};
	for (legacy_u32 mode = 0; mode <= 10; mode++) {
		for (legacy_u32 phase = 0; phase < 8; phase++) {
			reset_bitmap();
			memset(line, 0, sizeof(line));
			line[0] = fractions[phase % 4];
			line[1] = phase & 1 ? 0xffff : 32;
			line[2] = fractions[(phase + 1) % 4];
			line[3] = phase & 2 ? 0xffff : 4;
			line[6] = fractions[(phase + 2) % 4];
			line[7] = phase == 7 ? 0 : 17;
			line[8] = 0x13d;
			line[9] = mode;
			sprite_draw_line_from_setup(line);
			hash_bitmap();
		}
	}
}

static void test_font(void)
{
	static const legacy_u8 byte_counts[] = {0, 1, 2, 0x80, 0xff};
	static const legacy_u16 counts[] = {0, 1, 3, 0x8000, 0xffff};
	for (legacy_u32 scenario = 0; scenario < 100; scenario++) {
		reset_bitmap();
		legacy_u8 *font = dos_memory_make_pointer(0x1000, 0);
		memset(font, 0, 2048);
		active_font_definition = font;
		font[0] = 0xe3;
		font[2] = 0x17;
		shape2d_put_word(font + 4, 11);
		font[12] = byte_counts[scenario % 5];
		shape2d_put_word(font + 14, counts[(scenario / 5) % 5]);
		shape2d_put_word(font + 16, 9);
		shape2d_put_word(font + 18, 3);
		font[20] = (scenario / 25) & 1;
		shape2d_put_word(font + 22 + 'A' * 2, 600);
		shape2d_put_word(font + 22 + 'B' * 2, 700);
		for (legacy_u32 index = 600; index < 900; index++) {
			font[index] = (legacy_u8)(index * 17 + scenario);
		}
		if (font[20]) {
			font[600] = 9;
			font[700] = 3;
		}
		if (scenario & 1) {
			font_draw_text_opaque((const legacy_s8 *)"A?B\r\nB", -3, 2);
		} else {
			font_draw_text((const legacy_s8 *)"A?B\r\nB", -3, 2);
		}
		hash_bytes(font, 1024);
		hash_bitmap();
	}
}

static void test_dissolve(void)
{
	struct SHAPE2D *shape;
	for (legacy_u32 scenario = 0; scenario < 64; scenario++) {
		reset_bitmap();
		shape = make_shape(scenario % 8, 1 + scenario % 25, scenario & 1 ? 0xffe0 : 16);
		shape->position_x = scenario & 2 ? 0xfffe : 7;
		shape->position_y = scenario & 4 ? 0xfffd : 1;
		sprite_draw_dissolve_phase(shape, (legacy_u16)(scenario * 17));
		hash_bitmap();
	}
}

static void test_scaled(void)
{
	struct SHAPE2D *shape;
	static const legacy_u16 scales[] = {0, 1, 2, 127, 128, 255, 256, 257, 511, 512, 32768, 65535};
	static const legacy_s16 positions[] = {-32768, -8, -1, 0, 60, 64, 32767};
	for (legacy_u32 scale = 0; scale < 12; scale++) {
		for (legacy_u32 position = 0; position < 7; position++) {
			for (legacy_u32 clipped = 0; clipped < 2; clipped++) {
				reset_bitmap();
				shape = make_shape(4, scale == 10 ? 256 : 5, position & 1 ? 0xffe0 : 16);
				shape->centre_x = (legacy_u16)(position - 3);
				shape->centre_y = (legacy_u16)(3 - position);
				if (clipped) {
					shape2d_draw_scaled_transparent_clipped(
						scales[scale], shape, positions[position], positions[(position + 2) % 7]);
				} else {
					shape2d_draw_scaled_transparent(scales[scale], shape, positions[position],
													positions[(position + 2) % 7]);
				}
				hash_bitmap();
			}
		}
	}
}

static void write_rle(legacy_u16 offset, const legacy_u8 *bytes, legacy_u32 length)
{
	legacy_u8 *source = dos_memory_make_pointer(TEST_SOURCE_SEGMENT, 0);

	for (legacy_u32 index = 0; index < length; index++) {
		source[(legacy_u16)(offset + SHAPE2D_HEADER_SIZE + index)] = bytes[index];
	}
}

static void test_rle(void)
{
	struct SHAPE2D *shape;
	static const legacy_u8 stream[] = {252, 0, 1, 255, 63, 5, 72, 253, 9, 0, 11, 0};
	static const legacy_s16 positions[] = {-32768, -6, -1, 0, 1, 60, 64, 32767};
	static const legacy_u16 widths[] = {0, 1, 6, 0x8000};
	for (legacy_u32 scenario = 0; scenario < 128; scenario++) {
		reset_bitmap();
		shape = make_shape(widths[scenario % 4], 6, scenario & 1 ? 0xfff0 : 16);
		write_rle(shape_offset, stream, sizeof(stream));
		if (scenario & 4) {
			write_rle(shape_offset, stream + sizeof(stream) - 1, 1);
		}
		shape2d_rle_copy_clipped(shape, positions[(scenario / 4) % 8],
								 positions[(scenario / 8) % 8]);
		hash_bitmap();
		reset_bitmap();
		shape = make_shape(widths[scenario % 4], 6, scenario & 1 ? 0xfff0 : 16);
		write_rle(shape_offset, stream, sizeof(stream));
		shape->position_x = 0xfffe;
		shape->position_y = 2;
		if (scenario % 3 == 0) {
			shape2d_rle_copy_at_position(shape);
		} else if (scenario % 3 == 1) {
			shape2d_render_bmp_as_mask(shape);
		} else {
			shape2d_rle_or_far_pointer(shape_offset, TEST_SOURCE_SEGMENT);
		}
		hash_bitmap();
	}
}

static const legacy_s16 clipped_mask_positions[] = {-4, -1, 1, 3, 5, 7, 9, 12};

static void draw_rle_clipped_mask(legacy_u32 scenario)
{
	/* Both kinds of run cross rows, and the aliased source can wrap its segment. */
	static const legacy_u8 stream[] = {252, 255, 15, 0,	  240, 5, 51, 253, 128, 0,
									   255, 7,	 85, 251, 1,   2, 4,  8,   16,	0};
	reset_bitmap();
	struct SHAPE2D *shape = make_shape(6, 4, (scenario & 128U) != 0 ? 0xffe0 : 16);
	write_rle(shape_offset, stream, sizeof(stream));
	shape->position_x = (legacy_u16)clipped_mask_positions[scenario % 8];
	shape->position_y = (legacy_u16)clipped_mask_positions[(scenario / 8) % 8];
	sprite_set_target_clip_bounds(2, 9, 2, 7);
	if ((scenario & 64U) != 0) {
		shape2d_rle_or_position_clipped(shape);
	} else {
		shape2d_rle_mask_position_clipped(shape);
	}
}

static void test_rle_clipped_masks(void)
{
	static const legacy_u8 pixels[] = {255, 15, 0,	240, 51, 51, 51, 51, 51, 128, 0, 255,
									   85,	85, 85, 85,	 85, 85, 85, 1,	 2,	 4,	  8, 16};
	for (legacy_u32 scenario = 0; scenario < 256; scenario++) {
		draw_rle_clipped_mask(scenario);
		legacy_u8 *bitmap = dos_memory_make_pointer(TEST_BITMAP_SEGMENT, 0);
		legacy_s16 x = clipped_mask_positions[scenario % 8];
		legacy_s16 y = clipped_mask_positions[(scenario / 8) % 8];
		for (legacy_u32 offset = 0; offset < 65536; offset++) {
			legacy_s32 column = offset % 64;
			legacy_s32 row = offset / 64;
			legacy_s32 source_x = column - x;
			legacy_s32 source_y = row - y;
			legacy_u8 expected = 0x5a;
			if (column >= 2 && column < 9 && row >= 2 && row < 7 && source_x >= 0 && source_x < 6 &&
				source_y >= 0 && source_y < 4) {
				legacy_u8 value = pixels[source_y * 6 + source_x];
				expected = (scenario & 64U) != 0 ? expected | value : expected & value;
			}
			assert(bitmap[offset] == expected);
		}
	}
}

static legacy_u8 *make_resource(legacy_u16 width, legacy_u16 height, legacy_u8 flag,
								legacy_u16 count)
{
	legacy_u8 *resource = dos_memory_make_pointer(0x1000, 2);
	legacy_u32 stride = SHAPE2D_HEADER_SIZE + (legacy_u32)width * height + 2;

	aliased_shape = NULL;
	memset(resource, 0, 4096);
	resource_file_set_size(resource, 4096);
	LEGACY_WRITE_U16_LE(resource + 4, count);
	struct SHAPE2D *shape;
	for (legacy_u32 shape_index = 0; shape_index < count; shape_index++) {
		memcpy(resource + 6 + shape_index * 4, "TEST", 4);
		resource_file_set_offset(resource, count, shape_index, shape_index * stride);
		shape = file_get_shape2d(resource, shape_index);
		shape->width = width;
		shape->height = height;
		shape->plane_flags[2] = flag;
		for (legacy_u32 index = 0; index < (legacy_u32)width * height; index++) {
			((legacy_u8 *)shape)[SHAPE2D_HEADER_SIZE + index] = (legacy_u8)(index * 23);
		}
	}
	return resource;
}

static void test_unflip(void)
{
	legacy_u8 *workspace = dos_memory_make_pointer(0x4000, 0);

	struct SHAPE2D *shape;
	for (legacy_u32 width = 0; width < 5; width++) {
		for (legacy_u32 height = 0; height < 8; height++) {
			for (legacy_u32 flag = 0; flag < 6; flag++) {
				legacy_u8 *resource =
					make_resource(width, height, (legacy_u8)((flag % 5) << 4), flag == 4 ? 2 : 1);
				shape = file_get_shape2d(resource, 0);
				shape->plane_flags[3] = flag == 5 ? 0x10 : 0;
				if (flag == 4) {
					file_get_shape2d(resource, 1)->plane_flags[2] = 0x10;
				}
				memset(workspace, 0xa5, 4096);
				file_unflip_shape2d(resource, (legacy_s8 *)workspace);
				hash_bytes(resource, 128);
				hash_bytes(workspace, 128);
			}
		}
	}
}

static void test_parse(void)
{
	legacy_u8 *output = dos_memory_make_pointer(0x4000, 6);

	static const legacy_u16 lengths[] = {0, 1, 3, 4, 126, 127, 128, 129, 254, 260};
	for (legacy_u32 length = 0; length < 10; length++) {
		for (legacy_u32 pattern = 0; pattern < 4; pattern++) {
			legacy_u8 *resource = make_resource(lengths[length], 1, 0, pattern % 3 + 1);
			for (legacy_u32 shape_index = 0; shape_index < pattern % 3 + 1; shape_index++) {
				legacy_u8 *pixels =
					(legacy_u8 *)file_get_shape2d(resource, shape_index) + SHAPE2D_HEADER_SIZE;
				for (legacy_u32 index = 0; index < lengths[length]; index++) {
					pixels[index] =
						(legacy_u8)(pattern == 0
										? index
										: (pattern == 1 ? 7 : index / (pattern == 2 ? 3 : 130)));
				}
				pixels[lengths[length]] = 0xa1;
			}

			memset(output, 0x5a, 4096);
			parse_shape2d(resource, output);
			hash_bytes(output, 4096);
			hash_word(resized_offset);
			hash_word(resized_segment);
			hash_word(resized_paragraphs);
		}
	}
}

#define TEST_TRANSFORM_SOURCE_PITCH 7U
#define TEST_TRANSFORM_SOURCE_ROWS 5U
#define TEST_TRANSFORM_WIDTH 5U
#define TEST_TRANSFORM_HEIGHT 3U
#define TEST_TRANSFORM_SOURCE_CENTER_X 2
#define TEST_TRANSFORM_SOURCE_CENTER_Y 1
#define TEST_TRANSFORM_CLIP_LEFT 1
#define TEST_TRANSFORM_CLIP_RIGHT 31
#define TEST_TRANSFORM_CLIP_TOP 2
#define TEST_TRANSFORM_CLIP_BOTTOM 23
#define TEST_TRANSFORM_CASE_COUNT 6U
#define TEST_TRANSFORM_BACKGROUND 0x5aU

static const struct {
	legacy_s16 quarter_turn;
	legacy_s16 target_x, target_y;
} transform_cases[TEST_TRANSFORM_CASE_COUNT] = {{0, 6, 7},	{1, 6, 7},	 {-1, 6, 7},
												{0, -1, 2}, {1, 31, 22}, {-1, 1, 2}};

static legacy_u8 transform_source_pixel(legacy_u16 x, legacy_u16 y)
{
	return x == 1U && y == 1U ? SHAPE2D_TRANSPARENT_COLOR
							  : (legacy_u8)(1U + x + y * TEST_TRANSFORM_SOURCE_PITCH);
}

static void draw_transform_fixture(legacy_u32 scenario)
{
	struct SHAPE2D *shape = make_shape(TEST_TRANSFORM_SOURCE_PITCH, TEST_TRANSFORM_SOURCE_ROWS, 0);
	legacy_u8 *pixels = (legacy_u8 *)shape + SHAPE2D_HEADER_SIZE;
	memset(pixels, SHAPE2D_TRANSPARENT_COLOR,
		   TEST_TRANSFORM_SOURCE_PITCH * TEST_TRANSFORM_SOURCE_ROWS);
	for (legacy_u16 y = 0; y < TEST_TRANSFORM_HEIGHT; y++) {
		for (legacy_u16 x = 0; x < TEST_TRANSFORM_WIDTH; x++) {
			pixels[y * TEST_TRANSFORM_SOURCE_PITCH + x] = transform_source_pixel(x, y);
		}
	}
	struct SPRITE source = {0};
	source.sprite_bitmapptr = shape;
	source.sprite_pitch = TEST_TRANSFORM_SOURCE_PITCH;
	struct SHAPE2D_TRANSFORM transform = {0};
	legacy_s16 direction = transform_cases[scenario].quarter_turn;
	if (direction == 0) {
		transform.inverse_xx = (legacy_s16)SHAPE2D_TRANSFORM_ONE;
		transform.inverse_yy = (legacy_s16)SHAPE2D_TRANSFORM_ONE;
	} else {
		transform.inverse_xy = direction * (legacy_s16)SHAPE2D_TRANSFORM_ONE;
		transform.inverse_yx = -direction * (legacy_s16)SHAPE2D_TRANSFORM_ONE;
	}
	transform.source_pivot_x = TEST_TRANSFORM_SOURCE_CENTER_X;
	transform.source_pivot_y = TEST_TRANSFORM_SOURCE_CENTER_Y;
	transform.target_pivot_x = transform_cases[scenario].target_x;
	transform.target_pivot_y = transform_cases[scenario].target_y;
	transform.source_width = TEST_TRANSFORM_WIDTH;
	transform.source_height = TEST_TRANSFORM_HEIGHT;
	transform.left = -(legacy_s16)TEST_TRANSFORM_WIDTH;
	transform.top = -(legacy_s16)TEST_TRANSFORM_HEIGHT;
	transform.right = TEST_TRANSFORM_CLIP_RIGHT + TEST_TRANSFORM_WIDTH;
	transform.bottom = TEST_TRANSFORM_CLIP_BOTTOM + TEST_TRANSFORM_HEIGHT;
	sprite_set_target_clip_bounds(TEST_TRANSFORM_CLIP_LEFT, TEST_TRANSFORM_CLIP_RIGHT,
								  TEST_TRANSFORM_CLIP_TOP, TEST_TRANSFORM_CLIP_BOTTOM);
	sprite_copy_image_transformed(&source, &transform);
}

static void test_transformed_images(void)
{
	/* The expected image scatters source pixels through exact quarter turns.
	 * It does not duplicate the inverse fixed-point raster used by production. */
	static legacy_u8 expected[65536];
	for (legacy_u32 scenario = 0; scenario < TEST_TRANSFORM_CASE_COUNT; scenario++) {
		reset_bitmap();
		draw_transform_fixture(scenario);
		memset(expected, TEST_TRANSFORM_BACKGROUND, sizeof(expected));
		for (legacy_u16 y = 0; y < TEST_TRANSFORM_HEIGHT; y++) {
			for (legacy_u16 x = 0; x < TEST_TRANSFORM_WIDTH; x++) {
				legacy_s16 dx = (legacy_s16)x - TEST_TRANSFORM_SOURCE_CENTER_X;
				legacy_s16 dy = (legacy_s16)y - TEST_TRANSFORM_SOURCE_CENTER_Y;
				legacy_s16 direction = transform_cases[scenario].quarter_turn;
				legacy_s16 target_x = transform_cases[scenario].target_x;
				legacy_s16 target_y = transform_cases[scenario].target_y;
				if (direction == 0) {
					target_x += dx;
					target_y += dy;
				} else {
					target_x -= direction * dy;
					target_y += direction * dx;
				}
				legacy_u8 color = transform_source_pixel(x, y);
				if (target_x >= TEST_TRANSFORM_CLIP_LEFT && target_x < TEST_TRANSFORM_CLIP_RIGHT &&
					target_y >= TEST_TRANSFORM_CLIP_TOP && target_y < TEST_TRANSFORM_CLIP_BOTTOM &&
					color != SHAPE2D_TRANSPARENT_COLOR) {
					expected[target_y * drawing_sprite.sprite_pitch + target_x] = color;
				}
			}
		}
		assert(memcmp(expected, dos_memory_make_pointer(TEST_BITMAP_SEGMENT, 0),
					  sizeof(expected)) == 0);
	}
}

#define TEST_TEN_DEGREE_WIDTH 13U
#define TEST_TEN_DEGREE_HEIGHT 7U
#define TEST_TEN_DEGREE_PITCH 17U
#define TEST_TEN_DEGREE_CENTER_X 6
#define TEST_TEN_DEGREE_CENTER_Y 3
#define TEST_TEN_DEGREE_TARGET_CENTER 10
#define TEST_TEN_DEGREE_COSINE 16135
#define TEST_TEN_DEGREE_INVERSE_SINE 3951
#define TEST_TEN_DEGREE_ASPECT_SINE 2048
#define TEST_TEN_DEGREE_RESULT_LEFT 2U
#define TEST_TEN_DEGREE_RESULT_TOP 5U
#define TEST_TEN_DEGREE_RESULT_WIDTH 17U
#define TEST_TEN_DEGREE_RESULT_HEIGHT 11U

/* An independently calculated floating-point reference for the Q14 ten-degree
 * matrix with vertical aspect 0.72, rounded to the nearest source pixel.
 * Zero cells denote untouched background; source pixels are numbered 1..91. */
static const legacy_u8
	ten_degree_reference[TEST_TEN_DEGREE_RESULT_HEIGHT][TEST_TEN_DEGREE_RESULT_WIDTH] = {
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 14, 15, 3, 4, 5, 6, 7, 8, 9, 10, 0, 0, 0, 0},
		{0, 0, 27, 28, 29, 17, 18, 19, 20, 21, 21, 22, 23, 11, 12, 13, 0},
		{0, 0, 40, 41, 42, 30, 31, 32, 33, 34, 35, 36, 37, 25, 26, 0, 0},
		{0, 0, 53, 54, 55, 43, 44, 45, 46, 47, 48, 49, 50, 38, 39, 0, 0},
		{0, 0, 66, 67, 55, 56, 57, 58, 59, 60, 61, 62, 50, 51, 52, 0, 0},
		{0, 79, 80, 81, 69, 70, 71, 71, 72, 73, 74, 75, 63, 64, 65, 0, 0},
		{0, 0, 0, 0, 82, 83, 84, 85, 86, 87, 88, 89, 77, 78, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 90, 91, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

static void draw_ten_degree_transform_fixture(legacy_u32 scenario)
{
	struct SHAPE2D *shape = make_shape(TEST_TEN_DEGREE_PITCH, TEST_TEN_DEGREE_HEIGHT, 0);
	legacy_u8 *pixels = (legacy_u8 *)shape + SHAPE2D_HEADER_SIZE;
	memset(pixels, SHAPE2D_TRANSPARENT_COLOR, TEST_TEN_DEGREE_PITCH * TEST_TEN_DEGREE_HEIGHT);
	for (legacy_u16 y = 0; y < TEST_TEN_DEGREE_HEIGHT; y++) {
		for (legacy_u16 x = 0; x < TEST_TEN_DEGREE_WIDTH; x++) {
			pixels[y * TEST_TEN_DEGREE_PITCH + x] = (legacy_u8)(1U + x + y * TEST_TEN_DEGREE_WIDTH);
		}
	}
	struct SPRITE source = {0};
	source.sprite_bitmapptr = shape;
	source.sprite_pitch = TEST_TEN_DEGREE_PITCH;
	struct SHAPE2D_TRANSFORM transform = {0};
	legacy_s16 direction = scenario == 0 ? 1 : -1;
	transform.inverse_xx = TEST_TEN_DEGREE_COSINE;
	transform.inverse_xy = direction * TEST_TEN_DEGREE_INVERSE_SINE;
	transform.inverse_yx = -direction * TEST_TEN_DEGREE_ASPECT_SINE;
	transform.inverse_yy = TEST_TEN_DEGREE_COSINE;
	transform.source_pivot_x = TEST_TEN_DEGREE_CENTER_X;
	transform.source_pivot_y = TEST_TEN_DEGREE_CENTER_Y;
	transform.target_pivot_x = TEST_TEN_DEGREE_TARGET_CENTER;
	transform.target_pivot_y = TEST_TEN_DEGREE_TARGET_CENTER;
	transform.source_width = TEST_TEN_DEGREE_WIDTH;
	transform.source_height = TEST_TEN_DEGREE_HEIGHT;
	transform.left = TEST_TEN_DEGREE_RESULT_LEFT;
	transform.top = TEST_TEN_DEGREE_RESULT_TOP;
	transform.right = TEST_TEN_DEGREE_RESULT_LEFT + TEST_TEN_DEGREE_RESULT_WIDTH;
	transform.bottom = TEST_TEN_DEGREE_RESULT_TOP + TEST_TEN_DEGREE_RESULT_HEIGHT;
	sprite_copy_image_transformed(&source, &transform);
}

static void test_ten_degree_transformed_images(void)
{
	static legacy_u8 expected[65536];
	for (legacy_u32 scenario = 0; scenario < 2U; scenario++) {
		reset_bitmap();
		draw_ten_degree_transform_fixture(scenario);
		memset(expected, TEST_TRANSFORM_BACKGROUND, sizeof(expected));
		for (legacy_u16 y = 0; y < TEST_TEN_DEGREE_RESULT_HEIGHT; y++) {
			for (legacy_u16 x = 0; x < TEST_TEN_DEGREE_RESULT_WIDTH; x++) {
				legacy_u16 reference_x = scenario == 0 ? x : TEST_TEN_DEGREE_RESULT_WIDTH - 1U - x;
				legacy_u8 color = ten_degree_reference[y][reference_x];
				if (color == 0) {
					continue;
				}
				if (scenario != 0) {
					/* Mirror the source's columns along with the target's columns. */
					color = (legacy_u8)(color + TEST_TEN_DEGREE_WIDTH - 1U -
										2U * ((color - 1U) % TEST_TEN_DEGREE_WIDTH));
				}
				expected[(y + TEST_TEN_DEGREE_RESULT_TOP) * drawing_sprite.sprite_pitch + x +
						 TEST_TEN_DEGREE_RESULT_LEFT] = color;
			}
		}
		assert(memcmp(expected, dos_memory_make_pointer(TEST_BITMAP_SEGMENT, 0),
					  sizeof(expected)) == 0);
	}
}

static void check_fingerprint(const char *name, legacy_u32 expected, void (*test)(void))
{
	fingerprint = 2166136261UL;
	test();
#ifdef SHAPE2D_RECORD_BASELINE
	(void)expected;
	fprintf(stdout, "%s=0x%08" LEGACY_PRIx32 "UL\n", name, fingerprint);
#else
	if (fingerprint != expected) {
		fprintf(stderr, "%s: got %08" LEGACY_PRIx32 ", expected %08" LEGACY_PRIx32 "\n", name,
				fingerprint, expected);
		assert(0);
	}
#endif
}

int main(void)
{
	check_fingerprint("lines", 0x90f8d68cUL, test_lines);
	check_fingerprint("font", 0xd9c948eeUL, test_font);
	check_fingerprint("dissolve", 0x6fa2152dUL, test_dissolve);
	check_fingerprint("scaled", 0x8c1dc1abUL, test_scaled);
	test_transformed_images();
	test_ten_degree_transformed_images();
	check_fingerprint("rle", 0x45b9c13cUL, test_rle);
	test_rle_clipped_masks();
	check_fingerprint("unflip", 0x76403759UL, test_unflip);
	check_fingerprint("parse", 0x5b517e11UL, test_parse);
	return 0;
}
