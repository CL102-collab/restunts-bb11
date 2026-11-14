/* Real queued primitives use the original low-color material facts while
 * preserving VGA bytes and the renderer's legacy stack handoff. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../c/externs.h"
#include "../c/platform.h"
#include "../c/resource.h"
#include "../c/skybox.h"
#include "../c/menu/menu_common.h"
#include "../c/menu/menu_internal.h"
#include "../c/ui/ui_text.h"
#include "../c/video/video_color.h"
#include "../platform/sdl3/video_mode_bitmaps.h"
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
	const struct POINT2D ghost_line[] = {{8, 10}, {15, 10}};
	make_record(RENDER_PRIMITIVE_LINE | RENDER_PRIMITIVE_GHOST_FLAG, TEST_SOLID_MATERIAL,
				ghost_line, 2, 0);
	pixels = render_checked(SDL3_VIDEO_MODE_CGA, TEST_BACKGROUND);
	static const legacy_u8 ghost_line_pixels[] = {0, 0, 7, 7, 0, 0, 7, 7};
	assert_row(pixels, 8, 10, ghost_line_pixels);
}

/* The source maps deliberately disagree with procedural material/UI colors. */
enum {
	TEST_RESOURCE_SEGMENT = 0x8800,
	TEST_FONT_SEGMENT = 0x8900,
	TEST_RESOURCE_BYTES = 1024,
	TEST_RESOURCE_COUNT = 4,
	TEST_TABLE_SIDE = 16,
	TEST_BITMAP_WIDTH = 8,
	TEST_BITMAP_HEIGHT = 2,
	TEST_GRASS_VGA_INDEX = 108,
	TEST_EGA_GRAY_PAIR = 0x88,
	TEST_FONT_BYTES = 1024,
	TEST_FONT_BYTES_PER_ROW_OFFSET = 12,
	TEST_FONT_HEIGHT_OFFSET = 14,
	TEST_FONT_WIDTH_OFFSET = 16,
	TEST_FONT_LINE_HEIGHT_OFFSET = 18,
	TEST_FONT_GLYPH_TABLE_OFFSET = 22,
	TEST_FONT_GLYPH_OFFSET = 600,
	TEST_FONT_GLYPH_BITS = 0x80,
	TEST_FONT_GLYPH_WIDTH = 8,
	TEST_SKYBOX_NO_IMAGES = 8,
	TEST_SKYBOX_SKY_MATERIAL = 17,
	TEST_SKYBOX_GROUND_MATERIAL = 16,
	TEST_WORLD_LEFT = 8,
	TEST_WORLD_RIGHT = 24,
	TEST_WORLD_TOP = 16,
	TEST_WORLD_HORIZON = 24,
	TEST_WORLD_BOTTOM = 32,
	TEST_BITMAP_X = 64,
	TEST_BITMAP_Y = 24,
	TEST_BITMAP_COPY_X = 80,
	TEST_WORLD_COPY_X = 96,
	TEST_BUTTON_X = 8,
	TEST_BUTTON_Y = 64,
	TEST_BUTTON_WIDTH = 24,
	TEST_BUTTON_HEIGHT = 16,
	TEST_BUTTON_SAMPLE_X = 16,
	TEST_BUTTON_FILL_Y = 72,
	TEST_GRAPH_X = 40,
	TEST_GRAPH_Y = 88,
	TEST_GRAPH_PHASE_COUNT = 4,
	TEST_GRAPH_ROW_STEP = 2,
	TEST_FONT_X = 80,
	TEST_FONT_Y = 64,
	TEST_SHADOW_FONT_Y = 66,
	TEST_XOR_BLACK_X = 112,
	TEST_XOR_BLACK_Y = 88,
	TEST_XOR_WIDTH = 4,
	TEST_XOR_HEIGHT = 2
};

static struct SHAPE2D *register_conflicting_bitmap(void)
{
	static const legacy_char names[TEST_RESOURCE_COUNT][RESOURCE_FILE_IDENTIFIER_SIZE + 1U] = {
		"!eg0", "!cg0", "test", "!pal"};
	legacy_u8 *resource = dos_memory_make_pointer(TEST_RESOURCE_SEGMENT, 0);
	memset(resource, 0, TEST_RESOURCE_BYTES);
	LEGACY_WRITE_U16_LE(resource + RESOURCE_FILE_COUNT_OFFSET, TEST_RESOURCE_COUNT);
	legacy_u32 base =
		RESOURCE_FILE_DIRECTORY_OFFSET +
		TEST_RESOURCE_COUNT * (RESOURCE_FILE_IDENTIFIER_SIZE + RESOURCE_FILE_OFFSET_SIZE);
	legacy_u32 cursor = base;
	struct SHAPE2D *bitmap = NULL;
	for (legacy_u16 index = 0; index < TEST_RESOURCE_COUNT; index++) {
		memcpy(resource + RESOURCE_FILE_DIRECTORY_OFFSET + index * RESOURCE_FILE_IDENTIFIER_SIZE,
			   names[index], RESOURCE_FILE_IDENTIFIER_SIZE);
		LEGACY_WRITE_U32_LE(resource + RESOURCE_FILE_DIRECTORY_OFFSET +
								TEST_RESOURCE_COUNT * RESOURCE_FILE_IDENTIFIER_SIZE +
								index * RESOURCE_FILE_OFFSET_SIZE,
							cursor - base);
		legacy_u16 width = index < 2U ? TEST_TABLE_SIDE : index == 2U ? TEST_BITMAP_WIDTH : 1U;
		legacy_u16 height = index < 2U ? TEST_TABLE_SIDE : index == 2U ? TEST_BITMAP_HEIGHT : 1U;
		LEGACY_WRITE_U16_LE(resource + cursor, width);
		LEGACY_WRITE_U16_LE(resource + cursor + LEGACY_WORD_BYTES, height);
		if (index == 2U) {
			bitmap = (struct SHAPE2D *)(resource + cursor);
		}
		cursor += SHAPE2D_HEADER_SIZE;
		if (index == 0) {
			memset(resource + cursor, TEST_EGA_GRAY_PAIR, width * height);
			resource[cursor] = 0;
		} else if (index == 2U) {
			memset(resource + cursor, TEST_GRASS_VGA_INDEX, width * height);
		}
		cursor += width * height;
	}
	assert(cursor <= TEST_RESOURCE_BYTES);
	LEGACY_WRITE_U32_LE(resource + RESOURCE_FILE_SIZE_OFFSET, cursor);
	sdl3_video_bitmap_register(resource, cursor, 0);
	return bitmap;
}

static void initialize_test_font(void)
{
	legacy_u8 *font = dos_memory_make_pointer(TEST_FONT_SEGMENT, 0);
	memset(font, 0, TEST_FONT_BYTES);
	font[TEST_FONT_BYTES_PER_ROW_OFFSET] = 1;
	LEGACY_WRITE_U16_LE(font + TEST_FONT_HEIGHT_OFFSET, 1U);
	LEGACY_WRITE_U16_LE(font + TEST_FONT_WIDTH_OFFSET, TEST_FONT_GLYPH_WIDTH);
	LEGACY_WRITE_U16_LE(font + TEST_FONT_LINE_HEIGHT_OFFSET, 1U);
	LEGACY_WRITE_U16_LE(font + TEST_FONT_GLYPH_TABLE_OFFSET + 'A' * LEGACY_WORD_BYTES,
						TEST_FONT_GLYPH_OFFSET);
	font[TEST_FONT_GLYPH_OFFSET] = TEST_FONT_GLYPH_BITS;
	active_font_definition = font;
	font_glyph_height = 1;
}

static void draw_procedural_scene(legacy_u8 mode, legacy_u8 tagged, legacy_u8 enhanced)
{
	hires_shutdown();
	sdl3_video_mode_shutdown();
	sdl3_video_set_startup_mode(mode);
	if (enhanced) {
		hires_set_enabled(1);
		hires_set_render_scale(HIRES_MINIMUM_SCALE);
	}
	memset(screen, 0, TEST_PIXELS);
	sprite_select_target(&target);
	struct SHAPE2D *bitmap = register_conflicting_bitmap();
	initialize_test_font();

	/* Exercise the real horizon path without external game assets. */
	load_skybox(TEST_SKYBOX_NO_IMAGES);
	detail_level = SKYBOX_LOWEST_DETAIL_LEVEL;
	assert(skybox.ground_color == material_color_list[TEST_SKYBOX_GROUND_MATERIAL]);
	assert(skybox.sky_color == material_color_list[TEST_SKYBOX_SKY_MATERIAL]);
	struct RECTANGLE world = {TEST_WORLD_LEFT, TEST_WORLD_RIGHT, TEST_WORLD_TOP, TEST_WORLD_BOTTOM};
	skybox_render_level_rect(&world, 0, TEST_WORLD_HORIZON);
	sprite_select_target(&target);

	/* Equal VGA bytes retain distinct hardware colors through surface copies. */
	sprite_copy_image_at(bitmap, TEST_BITMAP_X, TEST_BITMAP_Y);
	video_pages_copy_span(screen, TEST_BITMAP_Y * TEST_WIDTH + TEST_BITMAP_COPY_X, screen,
						  TEST_BITMAP_Y * TEST_WIDTH + TEST_BITMAP_X, TEST_BITMAP_WIDTH);
	video_pages_copy_span(screen, TEST_BITMAP_Y * TEST_WIDTH + TEST_WORLD_COPY_X, screen,
						  TEST_WORLD_HORIZON * TEST_WIDTH + TEST_WORLD_LEFT, TEST_BITMAP_WIDTH);

	draw_button(NULL, TEST_BUTTON_X, TEST_BUTTON_Y, TEST_BUTTON_WIDTH, TEST_BUTTON_HEIGHT,
				tagged ? VIDEO_UI_COLOR(button_top_color) : button_top_color,
				tagged ? VIDEO_UI_COLOR(button_bottom_color) : button_bottom_color,
				tagged ? VIDEO_UI_COLOR(button_fill_color) : button_fill_color, 0);
	for (legacy_s16 phase = 0; phase < TEST_GRAPH_PHASE_COUNT; phase++) {
		sprite_putpixel_clipped(TEST_GRAPH_X + phase, TEST_GRAPH_Y + phase * TEST_GRAPH_ROW_STEP,
								tagged ? VIDEO_UI_COLOR(performGraphColor) : performGraphColor);
	}
	font_set_colors(tagged ? VIDEO_UI_COLOR(dialog_fnt_colour) : dialog_fnt_colour,
					tagged ? VIDEO_UI_COLOR(button_top_color) : button_top_color);
	font_draw_text_opaque((const legacy_s8 *)"A", TEST_FONT_X, TEST_FONT_Y);
	font_set_colors(tagged ? VIDEO_UI_COLOR(credits_production_heading_shadow_color)
						   : credits_production_heading_shadow_color,
					0);
	font_draw_text((const legacy_s8 *)"A", TEST_FONT_X, TEST_SHADOW_FONT_Y);
}

static void test_procedural_ui_xor(legacy_u8 mode)
{
	static const struct POINT2D positions[] = {{TEST_XOR_BLACK_X, TEST_XOR_BLACK_Y},
											   {TEST_BUTTON_SAMPLE_X, TEST_BUTTON_FILL_Y}};
	static const legacy_u8 before[] = {0, 7};
	static const legacy_u8 ega_after[] = {12, 11};
	static const legacy_u8 cga_after[] = {5, 3};
	for (legacy_u16 index = 0; index < sizeof(positions) / sizeof(positions[0]); index++) {
		sprite_xor_rect_clipped(positions[index].px, positions[index].py, TEST_XOR_WIDTH,
								TEST_XOR_HEIGHT, VIDEO_UI_COLOR(track_editor_highlight_color));
		const legacy_u8 *pixels = sdl3_video_mode_framebuffer(screen);
		legacy_u8 expected = mode == SDL3_VIDEO_MODE_CGA ? cga_after[index] : ega_after[index];
		for (legacy_u16 row = 0; row < TEST_XOR_HEIGHT; row++) {
			for (legacy_u16 column = 0; column < TEST_XOR_WIDTH; column++) {
				legacy_u16 offset =
					(positions[index].py + row) * TEST_WIDTH + positions[index].px + column;
				assert(screen[offset] == (before[index] ^ (legacy_u8)track_editor_highlight_color));
				assert(pixels[offset] == expected);
			}
		}
		sprite_xor_rect_clipped(positions[index].px, positions[index].py, TEST_XOR_WIDTH,
								TEST_XOR_HEIGHT, VIDEO_UI_COLOR(track_editor_highlight_color));
		assert(memcmp(screen, reference, sizeof(reference)) == 0);
		pixels = sdl3_video_mode_framebuffer(screen);
		for (legacy_u16 row = 0; row < TEST_XOR_HEIGHT; row++) {
			for (legacy_u16 column = 0; column < TEST_XOR_WIDTH; column++) {
				legacy_u16 offset =
					(positions[index].py + row) * TEST_WIDTH + positions[index].px + column;
				assert(pixels[offset] == before[index]);
			}
		}
	}
}

static void test_procedural_materials_and_ui(void)
{
	/* Original CGA points can spill cyan left within one packed hardware byte. */
	static const legacy_u8 ega_graph[TEST_GRAPH_PHASE_COUNT][TEST_GRAPH_PHASE_COUNT] = {
		{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
	static const legacy_u8 cga_graph[TEST_GRAPH_PHASE_COUNT][TEST_GRAPH_PHASE_COUNT] = {
		{3, 0, 0, 0}, {3, 3, 0, 0}, {3, 3, 3, 0}, {3, 3, 3, 3}};
	draw_procedural_scene(SDL3_VIDEO_MODE_VGA, 0, 0);
	memcpy(reference, screen, sizeof(reference));
	assert(reference[TEST_WORLD_HORIZON * TEST_WIDTH + TEST_WORLD_LEFT] == TEST_GRASS_VGA_INDEX);
	assert(reference[TEST_BUTTON_Y * TEST_WIDTH + TEST_BUTTON_SAMPLE_X] == button_top_color);
	assert(reference[(TEST_BUTTON_Y + TEST_BUTTON_HEIGHT) * TEST_WIDTH + TEST_BUTTON_SAMPLE_X] ==
		   button_bottom_color);
	assert(reference[TEST_FONT_Y * TEST_WIDTH + TEST_FONT_X] == dialog_fnt_colour);
	for (legacy_u8 mode = SDL3_VIDEO_MODE_VGA; mode <= SDL3_VIDEO_MODE_CGA; mode++) {
		draw_procedural_scene(mode, 1, 0);
		assert(VIDEO_COLOR_VALUE(PRERENDER_GHOST_COLOR) == PRERENDER_GHOST_COLOR);
		for (legacy_u16 token = SDL3_VIDEO_NATIVE_PRIMARY; token <= SDL3_VIDEO_NATIVE_TERTIARY;
			 token++) {
			assert(VIDEO_COLOR_VALUE(token) == token);
		}
		assert(memcmp(screen, reference, sizeof(reference)) == 0);
		if (mode != SDL3_VIDEO_MODE_VGA) {
			const legacy_u8 *pixels = sdl3_video_mode_framebuffer(screen);
			legacy_u8 cga = mode == SDL3_VIDEO_MODE_CGA;
			for (legacy_u16 x = TEST_WORLD_LEFT; x < TEST_WORLD_RIGHT; x++) {
				assert(pixels[TEST_WORLD_TOP * TEST_WIDTH + x] == (cga ? 3U : 11U));
				assert(pixels[TEST_WORLD_HORIZON * TEST_WIDTH + x] == (cga ? 5U : 2U));
			}
			for (legacy_u16 x = 0; x < TEST_BITMAP_WIDTH; x++) {
				assert(pixels[TEST_BITMAP_Y * TEST_WIDTH + TEST_BITMAP_X + x] == (cga ? 0U : 8U));
				assert(pixels[TEST_BITMAP_Y * TEST_WIDTH + TEST_BITMAP_COPY_X + x] ==
					   (cga ? 0U : 8U));
				assert(pixels[TEST_BITMAP_Y * TEST_WIDTH + TEST_WORLD_COPY_X + x] ==
					   (cga ? 5U : 2U));
			}
			assert(pixels[TEST_BUTTON_Y * TEST_WIDTH + TEST_BUTTON_SAMPLE_X] == (cga ? 3U : 15U));
			assert(
				pixels[(TEST_BUTTON_Y + TEST_BUTTON_HEIGHT) * TEST_WIDTH + TEST_BUTTON_SAMPLE_X] ==
				(cga ? 5U : 8U));
			assert(pixels[TEST_BUTTON_FILL_Y * TEST_WIDTH + TEST_BUTTON_SAMPLE_X] == 7U);
			assert(pixels[TEST_FONT_Y * TEST_WIDTH + TEST_FONT_X] == (cga ? 7U : 15U));
			/* Foreground/background share VGA15 but have white/cyan CGA roles. */
			for (legacy_u16 x = 1; x < TEST_FONT_GLYPH_WIDTH; x++) {
				assert(pixels[TEST_FONT_Y * TEST_WIDTH + TEST_FONT_X + x] == (cga ? 3U : 15U));
			}
			assert(pixels[TEST_SHADOW_FONT_Y * TEST_WIDTH + TEST_FONT_X] == (cga ? 5U : 1U));
			for (legacy_u16 phase = 0; phase < TEST_GRAPH_PHASE_COUNT; phase++) {
				assert(memcmp(pixels + (TEST_GRAPH_Y + phase * TEST_GRAPH_ROW_STEP) * TEST_WIDTH +
								  TEST_GRAPH_X,
							  cga ? cga_graph[phase] : ega_graph[phase],
							  TEST_GRAPH_PHASE_COUNT) == 0);
			}
		}
		test_procedural_ui_xor(mode);
		/* Real HyperVision composition still uses VGA for all tagged colors. */
		draw_procedural_scene(mode, 1, 1);
		assert(memcmp(screen, reference, sizeof(reference)) == 0);
		legacy_s32 width = 0, height = 0;
		const legacy_u8 *enhanced = hires_framebuffer(screen, &width, &height);
		assert(enhanced != NULL && width == TEST_WIDTH && height == TEST_HEIGHT);
		assert(memcmp(enhanced, reference, sizeof(reference)) == 0);
	}
	hires_shutdown();
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
	test_procedural_materials_and_ui();
	shape3d_set_legacy_render_stack(NULL, 0, 0, NULL);
	sdl3_video_mode_shutdown();
	puts("Native primitives, procedural world/UI colors and HyperVision preserve VGA/residue.");
	return 0;
}
