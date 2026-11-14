/* Real queued primitives use the original low-color material facts while
 * preserving VGA bytes and the renderer's legacy stack handoff. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../c/externs.h"
#include "../c/platform.h"
#include "../c/shape2d/shape2d.h"
#include "../c/shape3d/shape3d_pipeline_internal.h"
#include "../c/video/video_pages.h"
#include "../platform/sdl3/video_mode.h"

#undef memcpy
#undef memset

enum {
	TEST_WIDTH = 320,
	TEST_HEIGHT = 200,
	TEST_PIXELS = TEST_WIDTH * TEST_HEIGHT,
	TEST_VIDEO_SEGMENT = 0xA000,
	TEST_BACKGROUND = 15,
	TEST_STACK_WORDS = 4,
	TEST_DITHER_MATERIAL = 6,
	TEST_SOLID_MATERIAL = 16,
	TEST_MASKED_MATERIAL = 24,
	TEST_TWO_COLOR_MATERIAL = 26,
	TEST_POLYGON_VERTICES = 4,
	TEST_SPHERE_SIZE = 16
};

static legacy_u8 *screen;
static legacy_u8 rows[TEST_HEIGHT * LEGACY_WORD_BYTES];
static struct SPRITE target;
static legacy_u8 record[POLYINFO_MAX_RECORD_SIZE];
static polyinfo_link links[POLYINFO_STORAGE_PRIMITIVE_CAPACITY + 1U];
static polyinfo_offset offsets[POLYINFO_STORAGE_PRIMITIVE_CAPACITY];
static legacy_s16 headings[TEST_STACK_WORDS];
static legacy_u8 reference[TEST_PIXELS];

static void make_record(legacy_u8 type, legacy_u8 material, const struct POINT2D *points,
						legacy_u8 count, legacy_u16 sphere_size)
{
	memset(record, 0, sizeof(record));
	record[POLYINFO_TYPE_OFFSET] = type;
	record[POLYINFO_MATERIAL_OFFSET] = material;
	record[POLYINFO_VERTEX_COUNT_OFFSET] = count;
	for (legacy_u16 index = 0; index < count; index++) {
		polyinfo_write_point(record, index, &points[index]);
	}
	if ((type & ~RENDER_PRIMITIVE_VISUAL_FLAGS) == RENDER_PRIMITIVE_SPHERE) {
		polyinfo_write_word(record, POLYINFO_SPHERE_SIZE_WORD, sphere_size);
	}
}

static const legacy_u8 *render(legacy_u8 mode, legacy_u8 background)
{
	sdl3_video_mode_shutdown();
	sdl3_video_set_startup_mode(mode);
	hires_set_enabled(0);
	memset(screen, background, TEST_PIXELS);
	sprite_select_target(&target);
	if (mode != SDL3_VIDEO_MODE_VGA) {
		legacy_u16 colors[3] = {0};
		colors[0] = background == 0 ? 0 : mode == SDL3_VIDEO_MODE_CGA ? LEGACY_U16_MAX : background;
		sdl3_video_mode_primitive_begin();
		sdl3_video_mode_primitive_capture(colors);
		video_pages_fill_span(screen, 0, TEST_PIXELS, SDL3_VIDEO_NATIVE_PRIMARY);
		sdl3_video_mode_primitive_end();
	}
	for (legacy_u16 index = 0; index < TEST_STACK_WORDS; index++) {
		headings[index] = (legacy_s16)(index + 1U);
	}
	shape3d_set_legacy_render_stack(headings, 100U, 200U, NULL);
	polyinfo_reset();
	polyinfoptr = record;
	polyinfonumpolys = 1;
	polygon_next_index[polyinfo_primitive_capacity] = 0;
	polygon_next_index[0] = POLYINFO_LIST_SENTINEL;
	polygon_record_offsets[0] = 0;
	shape3d_render_queued_primitives();
	return sdl3_video_mode_framebuffer(screen);
}

static const legacy_u8 *render_checked(legacy_u8 mode, legacy_u8 background)
{
	legacy_s16 expected_headings[TEST_STACK_WORDS];
	(void)render(SDL3_VIDEO_MODE_VGA, background);
	memcpy(reference, screen, sizeof(reference));
	memcpy(expected_headings, headings, sizeof(expected_headings));
	const legacy_u8 *hardware = render(mode, background);
	assert(memcmp(screen, reference, sizeof(reference)) == 0);
	assert(memcmp(headings, expected_headings, sizeof(expected_headings)) == 0);
	return hardware;
}

static void assert_row(const legacy_u8 *pixels, legacy_u16 x, legacy_u16 y,
					   const legacy_u8 expected[8])
{
	assert(memcmp(pixels + y * TEST_WIDTH + x, expected, 8U) == 0);
}

static const struct POINT2D rectangle[TEST_POLYGON_VERTICES] = {{8, 8}, {24, 8}, {24, 24}, {8, 24}};

static void test_polygons(void)
{
	make_record(RENDER_PRIMITIVE_POLYGON, TEST_SOLID_MATERIAL, rectangle, TEST_POLYGON_VERTICES, 0);
	const legacy_u8 *pixels = render_checked(SDL3_VIDEO_MODE_EGA, TEST_BACKGROUND);
	static const legacy_u8 ega_solid[] = {2, 2, 2, 2, 2, 2, 2, 2};
	assert_row(pixels, 8, 10, ega_solid);
	pixels = render_checked(SDL3_VIDEO_MODE_CGA, TEST_BACKGROUND);
	static const legacy_u8 cga_solid[] = {5, 5, 5, 5, 5, 5, 5, 5};
	assert_row(pixels, 8, 10, cga_solid);

	/* Original material 6 is CGA word 0x1889: the two rows differ. */
	make_record(RENDER_PRIMITIVE_POLYGON, TEST_DITHER_MATERIAL, rectangle, TEST_POLYGON_VERTICES,
				0);
	pixels = render_checked(SDL3_VIDEO_MODE_CGA, TEST_BACKGROUND);
	static const legacy_u8 cga_even[] = {0, 3, 5, 0, 0, 3, 5, 0};
	static const legacy_u8 cga_odd[] = {5, 0, 5, 3, 5, 0, 5, 3};
	assert_row(pixels, 8, 10, cga_even);
	assert_row(pixels, 8, 11, cga_odd);

	/* Native 0x77DD masks are high-byte first on EGA and low-byte first
	 * on CGA. Holes retain the background even where VGA painted a pixel. */
	make_record(RENDER_PRIMITIVE_POLYGON, TEST_MASKED_MATERIAL, rectangle, TEST_POLYGON_VERTICES,
				0);
	pixels = render_checked(SDL3_VIDEO_MODE_EGA, TEST_BACKGROUND);
	static const legacy_u8 ega_mask_even[] = {15, 14, 14, 14, 15, 14, 14, 14};
	static const legacy_u8 ega_mask_odd[] = {14, 14, 15, 14, 14, 14, 15, 14};
	assert_row(pixels, 8, 10, ega_mask_even);
	assert_row(pixels, 8, 11, ega_mask_odd);
	pixels = render_checked(SDL3_VIDEO_MODE_CGA, TEST_BACKGROUND);
	static const legacy_u8 cga_mask_even[] = {5, 5, 7, 5, 5, 5, 7, 5};
	static const legacy_u8 cga_mask_odd[] = {7, 5, 5, 5, 7, 5, 5, 5};
	assert_row(pixels, 8, 10, cga_mask_even);
	assert_row(pixels, 8, 11, cga_mask_odd);

	/* EGA's two-color material 26 uses primary 6, secondary 4, AA55. */
	make_record(RENDER_PRIMITIVE_POLYGON, TEST_TWO_COLOR_MATERIAL, rectangle, TEST_POLYGON_VERTICES,
				0);
	pixels = render_checked(SDL3_VIDEO_MODE_EGA, TEST_BACKGROUND);
	static const legacy_u8 ega_two_even[] = {6, 4, 6, 4, 6, 4, 6, 4};
	static const legacy_u8 ega_two_odd[] = {4, 6, 4, 6, 4, 6, 4, 6};
	assert_row(pixels, 8, 10, ega_two_even);
	assert_row(pixels, 8, 11, ega_two_odd);
}

static void test_lines_and_round_primitives(void)
{
	static const legacy_u8 line_pixels[] = {5, 0, 5, 3, 5, 0, 5, 3};
	for (legacy_s16 row = 10; row <= 11; row++) {
		struct POINT2D line[] = {{8, row}, {15, row}};
		make_record(RENDER_PRIMITIVE_LINE, TEST_DITHER_MATERIAL, line, 2, 0);
		assert_row(render_checked(SDL3_VIDEO_MODE_CGA, TEST_BACKGROUND), 8, row, line_pixels);
		/* A degenerate polygon reaches the same original line raster. */
		make_record(RENDER_PRIMITIVE_POLYGON, TEST_DITHER_MATERIAL, line, 2, 0);
		assert_row(render_checked(SDL3_VIDEO_MODE_CGA, TEST_BACKGROUND), 8, row, line_pixels);
	}
	/* The original CGA point write shifts the low byte without masking it,
	 * so material 0x1889 can set earlier pixels in the same hardware byte. */
	static const legacy_u8 point_bytes[][4] = {
		{3, 0, 0, 0}, {5, 3, 0, 0}, {0, 5, 3, 0}, {5, 0, 5, 3}};
	const legacy_u8 point_types[] = {RENDER_PRIMITIVE_POINT, RENDER_PRIMITIVE_SPHERE};
	const legacy_u8 *pixels;
	for (legacy_u8 type = 0; type < sizeof(point_types); type++) {
		for (legacy_s16 phase = 0; phase < 4; phase++) {
			const struct POINT2D center[] = {{8 + phase, 10}};
			make_record(point_types[type], TEST_DITHER_MATERIAL, center, 1, 1);
			pixels = render_checked(SDL3_VIDEO_MODE_CGA, 0);
			assert(memcmp(pixels + 10 * TEST_WIDTH + 8, point_bytes[phase], 4U) == 0);
		}
	}

	const struct POINT2D sphere[] = {{32, 32}};
	make_record(RENDER_PRIMITIVE_SPHERE, TEST_DITHER_MATERIAL, sphere, 1, TEST_SPHERE_SIZE);
	pixels = render_checked(SDL3_VIDEO_MODE_CGA, TEST_BACKGROUND);
	static const legacy_u8 sphere_even[] = {0, 3, 5, 0, 0, 3, 5, 0};
	static const legacy_u8 sphere_odd[] = {5, 0, 5, 3, 5, 0, 5, 3};
	assert_row(pixels, 28, 30, sphere_even);
	assert_row(pixels, 28, 31, sphere_odd);
}

static void test_wheel_and_ghost(void)
{
	const struct POINT2D wheel[] = {{32, 32}, {44, 32}, {32, 44}, {40, 32}};
	make_record(RENDER_PRIMITIVE_WHEEL, TEST_DITHER_MATERIAL, wheel, WHEEL_SOURCE_POINT_COUNT, 0);
	const legacy_u8 *pixels = render_checked(SDL3_VIDEO_MODE_EGA, TEST_BACKGROUND);
	legacy_u32 counts[3] = {0};
	for (legacy_u32 index = 0; index < TEST_PIXELS; index++) {
		if (pixels[index] >= 6U && pixels[index] <= 8U) {
			counts[pixels[index] - 6U]++;
		}
	}
	assert(counts[0] && counts[1] && counts[2]);
	(void)render_checked(SDL3_VIDEO_MODE_CGA, TEST_BACKGROUND);

	make_record(RENDER_PRIMITIVE_POLYGON | RENDER_PRIMITIVE_BLACK_GRILLE_FLAG,
				SHAPE3D_STOCK_GRILLE_MATERIAL, rectangle, TEST_POLYGON_VERTICES, 0);
	pixels = render_checked(SDL3_VIDEO_MODE_EGA, TEST_BACKGROUND);
	static const legacy_u8 ega_black_grille[] = {15, 0, 0, 0, 15, 0, 0, 0};
	assert_row(pixels, 8, 10, ega_black_grille);
	pixels = render_checked(SDL3_VIDEO_MODE_CGA, TEST_BACKGROUND);
	static const legacy_u8 cga_black_grille[] = {0, 0, 7, 0, 0, 0, 7, 0};
	assert_row(pixels, 8, 10, cga_black_grille);

	make_record(RENDER_PRIMITIVE_POLYGON | RENDER_PRIMITIVE_GHOST_FLAG, TEST_SOLID_MATERIAL,
				rectangle, TEST_POLYGON_VERTICES, 0);
	pixels = render_checked(SDL3_VIDEO_MODE_CGA, TEST_BACKGROUND);
	legacy_u32 holes = 0, black = 0;
	for (legacy_u16 y = 10; y < 14; y++) {
		for (legacy_u16 x = 10; x < 14; x++) {
			holes += pixels[y * TEST_WIDTH + x] == 7;
			black += pixels[y * TEST_WIDTH + x] == 0;
		}
	}
	assert(holes && black && holes + black == 16U);
}

legacy_int main(void)
{
	screen = dos_memory_make_pointer(TEST_VIDEO_SEGMENT, 0);
	target.sprite_bitmapptr = (struct SHAPE2D *)screen;
	target.sprite_lineofs = rows;
	target.sprite_right = TEST_WIDTH;
	target.sprite_bottom = TEST_HEIGHT;
	target.sprite_pitch = TEST_WIDTH;
	target.sprite_buffer_width = TEST_WIDTH;
	target.sprite_raster_right = TEST_WIDTH;
	for (legacy_u16 row = 0; row < TEST_HEIGHT; row++) {
		LEGACY_WRITE_U16_LE(rows + row * LEGACY_WORD_BYTES, row * TEST_WIDTH);
	}
	polygon_next_index = links;
	polygon_record_offsets = offsets;
	material_clrlist_ptr_cpy = material_color_list;
	material_clrlist2_ptr_cpy = material_color_list;
	material_patlist_ptr_cpy = material_pattern_list;
	material_patlist2_ptr_cpy = material_pattern2_list;
	test_polygons();
	test_lines_and_round_primitives();
	test_wheel_and_ghost();
	shape3d_set_legacy_render_stack(NULL, 0, 0, NULL);
	sdl3_video_mode_shutdown();
	puts("Native video primitives preserve VGA/residue and original EGA/CGA material pixels.");
	return 0;
}
