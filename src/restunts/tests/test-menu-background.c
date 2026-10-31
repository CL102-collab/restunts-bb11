#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "../c/hires/hires.h"
#include "../c/platform.h"
#include "../c/shape2d/shape2d.h"
#include "../c/menu/menu_background.h"

#define TEST_WIDTH 320
#define TEST_HEIGHT 200
#define TEST_SHOWROOM_HEIGHT 103
#define TEST_OPPONENT_HEIGHT 182
#define TEST_ASSET_SCALE 2
#define TEST_BACKGROUND_COLOR 3
#define TEST_OVERLAY_COLOR 7
#define TEST_MENU_WHITE_INDEX 15
#define TEST_FADE_CHANNEL 128
#define TEST_COLOR_COUNT 256
#define TEST_ALPHA_MASK 0xFF000000U
#define TEST_PALETTE_STEP 0x010101U
#define TEST_VIDEO_SEGMENT 0xA000
#define TEST_CLIP_LEFT 29
#define TEST_CLIP_RIGHT 153
#define TEST_CLIP_TOP 13
#define TEST_CLIP_BOTTOM 91
#define TEST_INVALID_DIMENSION 8192
#define TEST_MASK_LEFT 17
#define TEST_MASK_TOP 9
#define TEST_MASK_WIDTH 11
#define TEST_MASK_HEIGHT 7
#define TEST_MASK_VISIBLE_COLOR 5
#define TEST_MASK_HIDDEN_COLOR 9
#define TEST_MASK_PATTERN_PERIOD 3
#define TEST_MASK_CLIP_INSET 2
#define TEST_PORTRAIT_ARGB 0xFF2456A8U

static legacy_u8 rows[TEST_HEIGHT * LEGACY_WORD_BYTES];
static legacy_u8 *screen;
static legacy_u32 palette[TEST_COLOR_COUNT];
static struct SPRITE target;
static struct SHAPE2D original;
static legacy_u8 overlay_bytes[SHAPE2D_HEADER_SIZE + TEST_MASK_WIDTH * TEST_MASK_HEIGHT];
legacy_u8 sprite_palette_map[TEST_COLOR_COUNT];

void dos_process_exit(legacy_s16 status)
{
	exit(status);
}

legacy_u16 shape2d_get_width(const struct SHAPE2D *shape)
{
	return shape->width;
}

legacy_u16 shape2d_get_height(const struct SHAPE2D *shape)
{
	return shape->height;
}

legacy_u16 shape2d_get_pos_x(const struct SHAPE2D *shape)
{
	return shape->position_x;
}

legacy_u16 shape2d_get_pos_y(const struct SHAPE2D *shape)
{
	return shape->position_y;
}

static legacy_u32 fixture_color(legacy_s32 x, legacy_s32 y)
{
	static const legacy_u32 colors[] = {0xFF336600U, 0xFFCC9900U, 0xFF6633CCU, 0xFF009999U};
	return colors[(x + y) % SDL_arraysize(colors)];
}

static void write_fixture(const legacy_char *path, legacy_s32 width, legacy_s32 height)
{
	SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
	assert(surface != NULL);
	for (legacy_s32 y = 0; y < height; y++) {
		legacy_u32 *pixels = (legacy_u32 *)((legacy_u8 *)surface->pixels + y * surface->pitch);
		for (legacy_s32 x = 0; x < width; x++) {
			pixels[x] = fixture_color(x, y);
		}
	}
	assert(SDL_SavePNG(surface, path));
	SDL_DestroySurface(surface);
}

static void reset(legacy_s32 height)
{
	menu_background_unload();
	hires_shutdown();
	memset(screen, TEST_BACKGROUND_COLOR, TEST_WIDTH * TEST_HEIGHT);
	memset(&original, 0, sizeof(original));
	original.width = TEST_WIDTH;
	original.height = height;
	target.sprite_raster_left = 0;
	target.sprite_raster_right = TEST_WIDTH;
	target.sprite_top = 0;
	target.sprite_bottom = TEST_HEIGHT;
}

static void assert_indexed_unchanged(void)
{
	for (legacy_s32 pixel = 0; pixel < TEST_WIDTH * TEST_HEIGHT; pixel++) {
		assert(screen[pixel] == TEST_BACKGROUND_COLOR);
	}
}

static void assert_drawing(legacy_s32 height, legacy_s32 scale)
{
	const legacy_u32 *pixels = hires_framebuffer_argb(screen, palette);
	assert(pixels != NULL);
	legacy_s32 width = hires_render_width();
	for (legacy_s32 y = 0; y < hires_render_height(); y++) {
		for (legacy_s32 x = 0; x < width; x++) {
			legacy_u32 expected = palette[TEST_BACKGROUND_COLOR];
			if (x >= target.sprite_raster_left * scale && x < target.sprite_raster_right * scale &&
				y >= target.sprite_top * scale && y < target.sprite_bottom * scale &&
				y < height * scale) {
				expected =
					fixture_color(x * TEST_ASSET_SCALE / scale + TEST_ASSET_SCALE / scale / 2,
								  y * TEST_ASSET_SCALE / scale + TEST_ASSET_SCALE / scale / 2);
			}
			assert(pixels[y * width + x] == expected);
		}
	}
	assert_indexed_unchanged();
}

static void test_scales_and_toggles(enum MENU_BACKGROUND kind, legacy_s32 height)
{
	const legacy_s32 scales[] = {HIRES_SCALE, HIRES_MEDIUM_SCALE, HIRES_MINIMUM_SCALE, HIRES_SCALE};
	reset(height);
	menu_background_draw(&target, &original, kind);
	assert(hires_framebuffer_argb(screen, palette) == NULL);
	for (legacy_u32 pass = 0; pass < SDL_arraysize(scales); pass++) {
		hires_set_enabled(1);
		hires_set_render_scale(scales[pass]);
		menu_background_draw(&target, &original, kind);
		assert_drawing(height, scales[pass]);
	}
	hires_set_enabled(0);
	menu_background_draw(&target, &original, kind);
	assert(hires_framebuffer_argb(screen, palette) == NULL);
	hires_set_enabled(1);
	menu_background_draw(&target, &original, kind);
	assert_drawing(height, HIRES_SCALE);

	/* Optional artwork follows the menu palette's fade through white entry 15. */
	palette[TEST_MENU_WHITE_INDEX] = TEST_ALPHA_MASK | TEST_FADE_CHANNEL * TEST_PALETTE_STEP;
	legacy_u32 source_color = fixture_color(0, 0);
	legacy_u32 faded_color = TEST_ALPHA_MASK;
	for (legacy_u32 shift = 0; shift < LEGACY_THREE_BYTE_BITS; shift += LEGACY_BYTE_BITS) {
		legacy_u32 channel =
			((source_color >> shift) & LEGACY_U8_MAX) * TEST_FADE_CHANNEL / LEGACY_U8_MAX;
		faded_color |= channel << shift;
	}
	assert(hires_framebuffer_argb(screen, palette)[0] == faded_color);
	palette[TEST_MENU_WHITE_INDEX] = LEGACY_U32_MAX;
	assert_drawing(height, HIRES_SCALE);

	/* A later indexed UI/car write retires the optional image at that cell.
	 * Restoring just its dirty rectangle recovers the original enhanced pixels. */
	legacy_u16 offset = TEST_CLIP_TOP * TEST_WIDTH + TEST_CLIP_LEFT;
	hires_write(screen, offset, TEST_OVERLAY_COLOR);
	screen[offset] = TEST_OVERLAY_COLOR;
	const legacy_u32 *pixels = hires_framebuffer_argb(screen, palette);
	assert(pixels[TEST_CLIP_TOP * HIRES_SCALE * HIRES_WIDTH + TEST_CLIP_LEFT * HIRES_SCALE] ==
		   palette[TEST_OVERLAY_COLOR]);
	screen[offset] = TEST_BACKGROUND_COLOR;
	target.sprite_raster_left = TEST_CLIP_LEFT;
	target.sprite_raster_right = TEST_CLIP_LEFT + 1;
	target.sprite_top = TEST_CLIP_TOP;
	target.sprite_bottom = TEST_CLIP_TOP + 1;
	menu_background_draw(&target, &original, kind);
	target.sprite_raster_left = target.sprite_top = 0;
	target.sprite_raster_right = TEST_WIDTH;
	target.sprite_bottom = TEST_HEIGHT;
	assert_drawing(height, HIRES_SCALE);

	for (legacy_u32 pass = 0; pass < SDL_arraysize(scales); pass++) {
		reset(height);
		hires_set_enabled(1);
		hires_set_render_scale(scales[pass]);
		target.sprite_raster_left = TEST_CLIP_LEFT;
		target.sprite_raster_right = TEST_CLIP_RIGHT;
		target.sprite_top = TEST_CLIP_TOP;
		target.sprite_bottom = TEST_CLIP_BOTTOM;
		menu_background_draw(&target, &original, kind);
		assert_drawing(height, scales[pass]);
	}
}

static void seed_portrait(const struct SHAPE2D *overlay)
{
	assert(hires_begin_argb(&target));
	legacy_s32 scale = hires_render_scale();
	for (legacy_s32 y = overlay->position_y * scale;
		 y < (overlay->position_y + overlay->height) * scale; y++) {
		for (legacy_s32 x = overlay->position_x * scale;
			 x < (overlay->position_x + overlay->width) * scale; x++) {
			hires_argb_pixel(x, y, TEST_PORTRAIT_ARGB);
		}
	}
	hires_end();
}

static void assert_overlay(const struct SHAPE2D *overlay, legacy_u8 applied)
{
	const legacy_u32 *pixels = hires_framebuffer_argb(screen, palette);
	assert(pixels != NULL);
	const legacy_u8 *mask = (const legacy_u8 *)overlay + SHAPE2D_HEADER_SIZE;
	legacy_s32 scale = hires_render_scale();
	legacy_s32 width = hires_render_width();
	for (legacy_s32 y = 0; y < hires_render_height(); y++) {
		for (legacy_s32 x = 0; x < width; x++) {
			legacy_s32 column = x / scale, row = y / scale;
			legacy_u32 expected = palette[TEST_BACKGROUND_COLOR];
			if (column >= overlay->position_x && column < overlay->position_x + overlay->width &&
				row >= overlay->position_y && row < overlay->position_y + overlay->height) {
				expected = TEST_PORTRAIT_ARGB;
				legacy_u8 index = mask[(row - overlay->position_y) * overlay->width + column -
									   overlay->position_x];
				if (applied != 0 && column >= target.sprite_raster_left &&
					column < target.sprite_raster_right && row >= target.sprite_top &&
					row < target.sprite_bottom && row < TEST_OPPONENT_HEIGHT &&
					sprite_palette_map[index] != SHAPE2D_TRANSPARENT_COLOR) {
					expected =
						fixture_color(x * TEST_ASSET_SCALE / scale + TEST_ASSET_SCALE / scale / 2,
									  y * TEST_ASSET_SCALE / scale + TEST_ASSET_SCALE / scale / 2);
				}
			}
			assert(pixels[y * width + x] == expected);
		}
	}
	assert_indexed_unchanged();
}

static void test_masked_overlay(void)
{
	struct SHAPE2D *overlay = (struct SHAPE2D *)overlay_bytes;
	overlay->width = TEST_MASK_WIDTH;
	overlay->height = TEST_MASK_HEIGHT;
	for (legacy_s32 pixel = 0; pixel < TEST_MASK_WIDTH * TEST_MASK_HEIGHT; pixel++) {
		static const legacy_u8 colors[TEST_MASK_PATTERN_PERIOD] = {
			SHAPE2D_TRANSPARENT_COLOR, TEST_MASK_HIDDEN_COLOR, TEST_MASK_VISIBLE_COLOR};
		overlay_bytes[SHAPE2D_HEADER_SIZE + pixel] = colors[pixel % TEST_MASK_PATTERN_PERIOD];
	}
	sprite_palette_map[TEST_MASK_HIDDEN_COLOR] = SHAPE2D_TRANSPARENT_COLOR;
	const legacy_s32 scales[] = {HIRES_SCALE, HIRES_MEDIUM_SCALE, HIRES_MINIMUM_SCALE};
	for (legacy_u32 pass = 0; pass < SDL_arraysize(scales); pass++) {
		for (legacy_u8 edge = 0; edge <= 1; edge++) {
			/* Also cross the background's right/bottom edges, where foreground
			 * must survive even though the mask itself still has opaque pixels. */
			overlay->position_x = edge != 0 ? TEST_WIDTH - TEST_MASK_WIDTH / 2 : TEST_MASK_LEFT;
			overlay->position_y =
				edge != 0 ? TEST_OPPONENT_HEIGHT - TEST_MASK_HEIGHT / 2 : TEST_MASK_TOP;
			for (legacy_u8 clipped = 0; clipped <= 1; clipped++) {
				reset(TEST_OPPONENT_HEIGHT);
				hires_set_enabled(1);
				hires_set_render_scale(scales[pass]);
				seed_portrait(overlay);
				sprite_palette_map[SHAPE2D_TRANSPARENT_COLOR] =
					clipped != 0 ? TEST_MASK_VISIBLE_COLOR : SHAPE2D_TRANSPARENT_COLOR;
				if (clipped != 0) {
					target.sprite_raster_left = overlay->position_x + TEST_MASK_CLIP_INSET;
					target.sprite_raster_right =
						overlay->position_x + overlay->width - TEST_MASK_CLIP_INSET;
					target.sprite_top = overlay->position_y + TEST_MASK_CLIP_INSET;
					target.sprite_bottom =
						overlay->position_y + overlay->height - TEST_MASK_CLIP_INSET;
				}
				menu_background_draw_overlay(&target, overlay, MENU_BACKGROUND_OPPONENT);
				assert_overlay(overlay, 1);
			}
		}
	}
	overlay->position_x = TEST_MASK_LEFT;
	overlay->position_y = TEST_MASK_TOP;
	sprite_palette_map[SHAPE2D_TRANSPARENT_COLOR] = SHAPE2D_TRANSPARENT_COLOR;
	reset(TEST_OPPONENT_HEIGHT);
	menu_background_draw_overlay(&target, overlay, MENU_BACKGROUND_OPPONENT);
	assert(hires_framebuffer_argb(screen, palette) == NULL);
	assert_indexed_unchanged();

	hires_set_enabled(1);
	seed_portrait(overlay);
	assert(remove("menus/opponent.png") == 0);
	menu_background_draw_overlay(&target, overlay, MENU_BACKGROUND_OPPONENT);
	assert_overlay(overlay, 0);
	write_fixture("menus/opponent.png", TEST_WIDTH * TEST_ASSET_SCALE,
				  TEST_OPPONENT_HEIGHT * TEST_ASSET_SCALE);
	/* Missing optional assets are retried after the normal menu unload. */
	menu_background_unload();
	menu_background_draw_overlay(&target, overlay, MENU_BACKGROUND_OPPONENT);
	assert_overlay(overlay, 1);
}

static void assert_fallback(enum MENU_BACKGROUND kind, legacy_s32 height)
{
	reset(height);
	hires_set_enabled(1);
	menu_background_draw(&target, &original, kind);
	assert(hires_framebuffer_argb(screen, palette) == NULL);
	assert_indexed_unchanged();
}

static void test_invalid_assets(enum MENU_BACKGROUND kind, legacy_s32 height,
								const legacy_char *path)
{
	assert(remove(path) == 0);
	assert_fallback(kind, height);
	write_fixture(path, TEST_WIDTH * TEST_ASSET_SCALE - 1, height * TEST_ASSET_SCALE);
	assert_fallback(kind, height);
	write_fixture(path, TEST_WIDTH * TEST_ASSET_SCALE, height * TEST_ASSET_SCALE - 1);
	assert_fallback(kind, height);
	write_fixture(path, TEST_INVALID_DIMENSION, 1);
	assert_fallback(kind, height);
	FILE *file = fopen(path, "wb");
	assert(file != NULL && fputs("not a PNG", file) >= 0);
	assert(fclose(file) == 0);
	assert_fallback(kind, height);
	write_fixture(path, TEST_WIDTH * TEST_ASSET_SCALE, height * TEST_ASSET_SCALE);
	assert_fallback(kind, height - 1);
	assert_fallback(MENU_BACKGROUND_COUNT, height);
	assert_fallback((enum MENU_BACKGROUND) - 1, height);
}

legacy_int main(void)
{
	assert(SDL_Init(0));
	assert(SDL_CreateDirectory("menus"));
	screen = dos_memory_make_pointer(TEST_VIDEO_SEGMENT, 0);
	target.sprite_bitmapptr = (struct SHAPE2D *)screen;
	target.sprite_lineofs = rows;
	for (legacy_s32 y = 0; y < TEST_HEIGHT; y++) {
		LEGACY_WRITE_U16_LE(rows + y * LEGACY_WORD_BYTES, y * TEST_WIDTH);
	}
	for (legacy_u32 index = 0; index < TEST_COLOR_COUNT; index++) {
		palette[index] = TEST_ALPHA_MASK | index * TEST_PALETTE_STEP;
		sprite_palette_map[index] = (legacy_u8)index;
	}
	palette[TEST_MENU_WHITE_INDEX] = LEGACY_U32_MAX;
	write_fixture("menus/main.png", TEST_WIDTH * TEST_ASSET_SCALE, TEST_HEIGHT * TEST_ASSET_SCALE);
	write_fixture("menus/showroom.png", TEST_WIDTH * TEST_ASSET_SCALE,
				  TEST_SHOWROOM_HEIGHT * TEST_ASSET_SCALE);
	write_fixture("menus/opponent.png", TEST_WIDTH * TEST_ASSET_SCALE,
				  TEST_OPPONENT_HEIGHT * TEST_ASSET_SCALE);
	test_scales_and_toggles(MENU_BACKGROUND_MAIN, TEST_HEIGHT);
	test_scales_and_toggles(MENU_BACKGROUND_SHOWROOM, TEST_SHOWROOM_HEIGHT);
	test_scales_and_toggles(MENU_BACKGROUND_OPPONENT, TEST_OPPONENT_HEIGHT);
	test_masked_overlay();
	test_invalid_assets(MENU_BACKGROUND_MAIN, TEST_HEIGHT, "menus/main.png");
	test_invalid_assets(MENU_BACKGROUND_SHOWROOM, TEST_SHOWROOM_HEIGHT, "menus/showroom.png");
	test_invalid_assets(MENU_BACKGROUND_OPPONENT, TEST_OPPONENT_HEIGHT, "menus/opponent.png");
	reset(TEST_HEIGHT);
	assert(remove("menus/main.png") == 0);
	assert(remove("menus/showroom.png") == 0);
	assert(remove("menus/opponent.png") == 0);
	assert(rmdir("menus") == 0);
	SDL_Quit();
	puts("Menu backgrounds: nearest pixels, mode/scale changes, clipping, overlays and fallback "
		 "passed.");
	return 0;
}
