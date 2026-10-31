#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "../c/hires/hires.h"
#include "../c/platform.h"
#include "../c/shape2d/shape2d.h"
#include "../c/opponent/opponent_portrait.h"

static legacy_u8 rows[400];
static legacy_u8 original_bytes[SHAPE2D_HEADER_SIZE + 80 * 83];
static struct SPRITE target;
static legacy_u8 *screen;
static legacy_u32 palette[256];

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

static void reset(void)
{
	hires_shutdown();
	opponent_portrait_unload();
	memset(screen, 3, 64000);
	target.sprite_raster_right = 320;
	target.sprite_bottom = 200;
}

static void write_solid_fixture(const legacy_char *path, legacy_s32 width, legacy_s32 height,
								legacy_u32 color)
{
	SDL_Surface *source = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
	assert(source != NULL);
	assert(SDL_FillSurfaceRect(source, NULL, color));
	assert(SDL_SavePNG(source, path));
	SDL_DestroySurface(source);
}

static const SDL_Color prepared_colors[2] = {{204, 51, 0, 255}, {0, 102, 204, 255}};

static void write_prepared_fixture(const legacy_char *path)
{
	SDL_Surface *source = SDL_CreateSurface(296, 316, SDL_PIXELFORMAT_INDEX8);
	assert(source != NULL);
	SDL_Palette *colors = SDL_CreateSurfacePalette(source);
	assert(colors != NULL && SDL_SetPaletteColors(colors, prepared_colors, 0, 2));
	for (legacy_s32 y = 0; y < source->h; y++) {
		legacy_u8 *pixels = (legacy_u8 *)source->pixels + y * source->pitch;
		for (legacy_s32 x = 0; x < source->w; x++) {
			pixels[x] = (x / 2 + y / 2) % 2;
		}
	}
	assert(SDL_SavePNG(source, path));
	SDL_DestroySurface(source);
}

static void test_prepared(struct SHAPE2D *shape)
{
	write_prepared_fixture("opponents/game/opp1.png");
	reset();
	hires_set_enabled(1);
	opponent_portrait_draw(&target, shape, 1);
	const legacy_u32 *pixels = hires_framebuffer_argb(screen, palette);
	assert(pixels != NULL);
	/* A master and a stale game2 tile are also present. The legacy 296x316
	 * interior must win without filtering, except the original digit contour. */
	for (legacy_s32 y = 0; y < 316; y++) {
		for (legacy_s32 x = 0; x < 296; x++) {
			const SDL_Color *color = &prepared_colors[(x / 2 + y / 2) % 2];
			legacy_u32 expected =
				0xFF000000U | ((legacy_u32)color->r << 16) | ((legacy_u32)color->g << 8) | color->b;
			if (x / HIRES_SCALE + 2 == 66 && y / HIRES_SCALE + 2 == 4) {
				expected = palette[3];
			}
			assert(pixels[(y + 32) * HIRES_WIDTH + x + 456] == expected);
		}
	}
	hires_set_enabled(0);
	opponent_portrait_draw(&target, shape, 1);
	assert(hires_framebuffer_argb(screen, palette) == NULL);
	/* Bad prepared images and absent prepared images both retain the master. */
	FILE *invalid = fopen("opponents/game/opp1.png", "wb");
	assert(invalid != NULL && fputs("not a PNG", invalid) >= 0);
	assert(fclose(invalid) == 0);
	reset();
	hires_set_enabled(1);
	opponent_portrait_draw(&target, shape, 1);
	assert(hires_framebuffer_argb(screen, palette)[32 * HIRES_WIDTH + 456] == 0xFF123456U);
	assert(remove("opponents/game/opp1.png") == 0);
	reset();
	hires_set_enabled(1);
	opponent_portrait_draw(&target, shape, 1);
	assert(hires_framebuffer_argb(screen, palette)[32 * HIRES_WIDTH + 456] == 0xFF123456U);
	/* A prepared portrait does not require a master; deleting both uses the original. */
	write_prepared_fixture("opponents/game/opp2.png");
	reset();
	hires_set_enabled(1);
	opponent_portrait_draw(&target, shape, 2);
	assert(hires_framebuffer_argb(screen, palette)[32 * HIRES_WIDTH + 456] == 0xFFCC3300U);
	assert(remove("opponents/game/opp2.png") == 0);
	reset();
	hires_set_enabled(1);
	opponent_portrait_draw(&target, shape, 2);
	assert(hires_framebuffer_argb(screen, palette) == NULL);
}

static legacy_u32 original_upscale_color(legacy_s32 x, legacy_s32 y)
{
	static const legacy_u32 colors[4] = {0xFF336600U, 0xFFCC9900U, 0xFF6633CCU, 0xFF009999U};
	return colors[(x + y * 3) % 4];
}

static void write_original_upscale_fixture(const legacy_char *path, legacy_s32 width,
										   legacy_s32 height)
{
	SDL_Surface *source = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
	assert(source != NULL);
	for (legacy_s32 y = 0; y < height; y++) {
		legacy_u32 *pixels = (legacy_u32 *)((legacy_u8 *)source->pixels + y * source->pitch);
		for (legacy_s32 x = 0; x < width; x++) {
			/* A contrasting full-tile border catches missing or shifted crops. */
			pixels[x] =
				x >= 4 && x < 152 && y >= 4 && y < 162 ? original_upscale_color(x, y) : 0xFFFF00FFU;
		}
	}
	assert(SDL_SavePNG(source, path));
	SDL_DestroySurface(source);
}

static void assert_first_portrait_pixel(struct SHAPE2D *shape, legacy_u8 opponent,
										legacy_u32 expected)
{
	reset();
	hires_set_enabled(1);
	opponent_portrait_draw(&target, shape, opponent);
	const legacy_u32 *pixels = hires_framebuffer_argb(screen, palette);
	assert(pixels != NULL && pixels[32 * HIRES_WIDTH + 456] == expected);
}

static void test_original_upscale(struct SHAPE2D *shape)
{
	write_original_upscale_fixture("opponents/game/opp1.png", 160, 166);
	reset();
	hires_set_enabled(1);
	opponent_portrait_draw(&target, shape, 1);
	const legacy_u32 *pixels = hires_framebuffer_argb(screen, palette);
	assert(pixels != NULL);
	/* A master and a stale game2 tile are also present. Every cropped game
	 * pixel must win and expand to exactly 2x2 samples with no new colors. */
	for (legacy_s32 y = 0; y < 316; y++) {
		for (legacy_s32 x = 0; x < 296; x++) {
			legacy_u32 expected = original_upscale_color(x / 2 + 4, y / 2 + 4);
			if (x / HIRES_SCALE + 2 == 66 && y / HIRES_SCALE + 2 == 4) {
				expected = palette[3];
			}
			assert(pixels[(y + 32) * HIRES_WIDTH + x + 456] == expected);
		}
	}
	assert(pixels[31 * HIRES_WIDTH + 456] == palette[3]);
	assert(pixels[32 * HIRES_WIDTH + 455] == palette[3]);
	assert(pixels[32 * HIRES_WIDTH + 752] == palette[3]);
	assert(pixels[348 * HIRES_WIDTH + 456] == palette[3]);
	hires_set_enabled(0);
	opponent_portrait_draw(&target, shape, 1);
	assert(hires_framebuffer_argb(screen, palette) == NULL);
	/* Wrong dimensions and corrupt PNGs use the master, never the stale folder. */
	write_original_upscale_fixture("opponents/game/opp1.png", 159, 166);
	assert_first_portrait_pixel(shape, 1, 0xFF123456U);
	write_original_upscale_fixture("opponents/game/opp1.png", 160, 165);
	assert_first_portrait_pixel(shape, 1, 0xFF123456U);
	write_original_upscale_fixture("opponents/game/opp1.png", 295, 316);
	assert_first_portrait_pixel(shape, 1, 0xFF123456U);
	write_original_upscale_fixture("opponents/game/opp1.png", 296, 315);
	assert_first_portrait_pixel(shape, 1, 0xFF123456U);
	FILE *invalid = fopen("opponents/game/opp1.png", "wb");
	assert(invalid != NULL && fputs("not a PNG", invalid) >= 0);
	assert(fclose(invalid) == 0);
	assert_first_portrait_pixel(shape, 1, 0xFF123456U);
	assert(remove("opponents/game/opp1.png") == 0);
	assert_first_portrait_pixel(shape, 1, 0xFF123456U);
	/* A full tile works alone; removing it restores the original even when a
	 * valid tile remains in the former game2 installation directory. */
	write_solid_fixture("opponents/game2/opp2.png", 160, 166, 0xFFFF00FFU);
	write_original_upscale_fixture("opponents/game/opp2.png", 160, 166);
	assert_first_portrait_pixel(shape, 2, original_upscale_color(4, 4));
	assert(remove("opponents/game/opp2.png") == 0);
	reset();
	hires_set_enabled(1);
	opponent_portrait_draw(&target, shape, 2);
	assert(hires_framebuffer_argb(screen, palette) == NULL);
	assert(remove("opponents/game2/opp2.png") == 0);
}

static void test_fallback(struct SHAPE2D *shape)
{
	for (legacy_u8 opponent = 0; opponent <= 7; opponent++) {
		reset();
		if (opponent != 1) {
			hires_set_enabled(1);
		}
		opponent_portrait_draw(&target, shape, opponent);
		assert(hires_framebuffer_argb(screen, palette) == NULL);
		for (legacy_u32 pixel = 0; pixel < 64000; pixel++) {
			assert(screen[pixel] == 3);
		}
	}
}

static void test_photo(struct SHAPE2D *shape)
{
	reset();
	hires_set_enabled(1);
	opponent_portrait_draw(&target, shape, 1);
	const legacy_u32 *pixels = hires_framebuffer_argb(screen, palette);
	assert(pixels != NULL);
	assert(pixels[32 * HIRES_WIDTH + 456] == 0xFF123456U);
	assert(pixels[31 * HIRES_WIDTH + 456] == palette[3]);
	assert(pixels[32 * HIRES_WIDTH + 455] == palette[3]);
	assert(pixels[348 * HIRES_WIDTH + 456] == palette[3]);
	/* Original digit pixel at source (66,4), without a rectangular seam. */
	assert(pixels[40 * HIRES_WIDTH + 712] == palette[3]);
	assert(pixels[40 * HIRES_WIDTH + 711] == 0xFF123456U);
	assert(pixels[40 * HIRES_WIDTH + 716] == 0xFF123456U);
	hires_set_enabled(0);
	assert(hires_framebuffer_argb(screen, palette) == NULL);
	hires_set_enabled(1);
	opponent_portrait_draw(&target, shape, 1);
	assert(hires_framebuffer_argb(screen, palette)[32 * HIRES_WIDTH + 456] == 0xFF123456U);
	reset();
	hires_set_enabled(1);
	target.sprite_raster_right = 115;
	target.sprite_bottom = 9;
	opponent_portrait_draw(&target, shape, 1);
	pixels = hires_framebuffer_argb(screen, palette);
	assert(pixels[32 * HIRES_WIDTH + 456] == 0xFF123456U);
	assert(pixels[32 * HIRES_WIDTH + 460] == palette[3]);
	assert(pixels[36 * HIRES_WIDTH + 456] == palette[3]);
}

static void test_positioned_photo(struct SHAPE2D *shape)
{
	reset();
	hires_set_enabled(1);
	opponent_portrait_draw_at(&target, shape, 1, 240, 0);
	const legacy_u32 *pixels = hires_framebuffer_argb(screen, palette);
	assert(pixels != NULL);
	/* The car panel uses an explicit origin, independent of the source tile's
	 * opponent-menu position. Its border and number still come from the original. */
	assert(pixels[8 * HIRES_WIDTH + 968] == 0xFF123456U);
	assert(pixels[7 * HIRES_WIDTH + 968] == palette[3]);
	assert(pixels[8 * HIRES_WIDTH + 967] == palette[3]);
	assert(pixels[8 * HIRES_WIDTH + 1264] == palette[3]);
	assert(pixels[324 * HIRES_WIDTH + 968] == palette[3]);
	assert(pixels[16 * HIRES_WIDTH + 1224] == palette[3]);
	assert(pixels[16 * HIRES_WIDTH + 1223] == 0xFF123456U);
	assert(pixels[16 * HIRES_WIDTH + 1228] == 0xFF123456U);
	assert(pixels[32 * HIRES_WIDTH + 456] == palette[3]);
	assert(shape->position_x == 112 && shape->position_y == 6);

	hires_set_enabled(0);
	opponent_portrait_draw_at(&target, shape, 1, 240, 0);
	assert(hires_framebuffer_argb(screen, palette) == NULL);
	hires_set_enabled(1);
	opponent_portrait_draw_at(&target, shape, 1, 240, 0);
	assert(hires_framebuffer_argb(screen, palette)[8 * HIRES_WIDTH + 968] == 0xFF123456U);

	reset();
	hires_set_enabled(1);
	target.sprite_raster_right = 243;
	target.sprite_bottom = 3;
	opponent_portrait_draw_at(&target, shape, 1, 240, 0);
	pixels = hires_framebuffer_argb(screen, palette);
	assert(pixels != NULL && pixels[8 * HIRES_WIDTH + 968] == 0xFF123456U);
	assert(pixels[8 * HIRES_WIDTH + 972] == palette[3]);
	assert(pixels[12 * HIRES_WIDTH + 968] == palette[3]);

	reset();
	hires_set_enabled(1);
	opponent_portrait_draw_at(&target, shape, 2, 240, 0);
	assert(hires_framebuffer_argb(screen, palette) == NULL);
	assert(shape->position_x == 112 && shape->position_y == 6);
	for (legacy_u32 pixel = 0; pixel < 64000; pixel++) {
		assert(screen[pixel] == 3);
	}
}

enum {
	SCALE_PORTRAIT_LEFT = 2,
	SCALE_PORTRAIT_TOP = 2,
	SCALE_PORTRAIT_WIDTH = 74,
	SCALE_PORTRAIT_HEIGHT = 79,
	SCALE_PORTRAIT_DIGIT_X = 66,
	SCALE_PORTRAIT_DIGIT_Y = 4,
	SCALE_PORTRAIT_BACKGROUND = 3
};

static void test_render_scale_roundtrip(struct SHAPE2D *shape)
{
	static legacy_u32 full_resolution[HIRES_WIDTH * HIRES_HEIGHT];
	const legacy_s32 scales[] = {HIRES_SCALE, HIRES_SCALE / 2, 1, HIRES_SCALE};
	write_prepared_fixture("opponents/game/opp1.png");
	reset();
	hires_set_enabled(1);
	for (legacy_u32 step = 0; step < SDL_arraysize(scales); step++) {
		legacy_s32 scale = scales[step];
		legacy_s32 source_step = HIRES_SCALE / scale;
		hires_set_render_scale(scale);
		opponent_portrait_draw(&target, shape, 1);
		legacy_s32 width = hires_render_width();
		legacy_s32 height = hires_render_height();
		const legacy_u32 *image = hires_framebuffer_argb(screen, palette);
		assert(image != NULL && hires_enabled());
		legacy_s32 left = (shape->position_x + SCALE_PORTRAIT_LEFT) * scale;
		legacy_s32 top = (shape->position_y + SCALE_PORTRAIT_TOP) * scale;
		legacy_u32 drawn = 0;
		for (legacy_s32 y = 0; y < height; y++) {
			for (legacy_s32 x = 0; x < width; x++) {
				legacy_u32 expected = palette[SCALE_PORTRAIT_BACKGROUND];
				legacy_s32 column = x - left, row = y - top;
				if (column >= 0 && column < SCALE_PORTRAIT_WIDTH * scale && row >= 0 &&
					row < SCALE_PORTRAIT_HEIGHT * scale &&
					!(column / scale + SCALE_PORTRAIT_LEFT == SCALE_PORTRAIT_DIGIT_X &&
					  row / scale + SCALE_PORTRAIT_TOP == SCALE_PORTRAIT_DIGIT_Y)) {
					legacy_s32 source_x = column * source_step + source_step / 2;
					legacy_s32 source_y = row * source_step + source_step / 2;
					const SDL_Color *color = &prepared_colors[(source_x / 2 + source_y / 2) % 2];
					expected = 0xFF000000U | ((legacy_u32)color->r << LEGACY_WORD_BITS) |
							   ((legacy_u32)color->g << LEGACY_BYTE_BITS) | color->b;
					drawn++;
				}
				assert(image[y * width + x] == expected);
			}
		}
		assert(drawn == (SCALE_PORTRAIT_WIDTH * SCALE_PORTRAIT_HEIGHT - 1U) * scale * scale);
		if (step == 0) {
			memcpy(full_resolution, image, sizeof(full_resolution));
		} else if (step == SDL_arraysize(scales) - 1) {
			assert(memcmp(full_resolution, image, sizeof(full_resolution)) == 0);
		}
		for (legacy_u32 pixel = 0;
			 pixel < sizeof(rows) / LEGACY_WORD_BYTES * target.sprite_raster_right; pixel++) {
			assert(screen[pixel] == SCALE_PORTRAIT_BACKGROUND);
		}
	}
	assert(remove("opponents/game/opp1.png") == 0);
}

legacy_int main(void)
{
	assert(SDL_Init(0));
	assert(SDL_CreateDirectory("portrait-test"));
	assert(chdir("portrait-test") == 0);
	assert(SDL_CreateDirectory("opponents"));
	assert(SDL_CreateDirectory("opponents/game"));
	assert(SDL_CreateDirectory("opponents/game2"));
	write_solid_fixture("opponents/opp1.png", 19, 23, 0xFF123456U);
	write_solid_fixture("opponents/game2/opp1.png", 160, 166, 0xFFFF00FFU);
	FILE *invalid = fopen("opponents/opp3.png", "wb");
	assert(invalid != NULL && fputs("not a PNG", invalid) >= 0);
	assert(fclose(invalid) == 0);
	/* The dimension guard must reject this before PNG decoding/allocation. */
	legacy_u8 oversized[24] = {137, 'P', 'N', 'G', 13, 10, 26,	 10, 0, 0, 0, 13,
							   'I', 'H', 'D', 'R', 0,  0,  0x20, 0,	 0, 0, 0, 1};
	invalid = fopen("opponents/opp4.png", "wb");
	assert(invalid != NULL &&
		   fwrite(oversized, 1, sizeof(oversized), invalid) == sizeof(oversized));
	assert(fclose(invalid) == 0);
	screen = dos_memory_make_pointer(0xA000, 0);
	target.sprite_bitmapptr = (struct SHAPE2D *)screen;
	target.sprite_lineofs = rows;
	target.sprite_raster_right = 320;
	target.sprite_bottom = 200;
	for (legacy_u32 y = 0; y < 200; y++) {
		LEGACY_WRITE_U16_LE(rows + y * 2, y * 320);
	}
	for (legacy_u32 index = 0; index < 256; index++) {
		palette[index] = 0xFF000000U | index * 0x010101U;
	}
	palette[15] = 0xFFFFFFFFU;
	struct SHAPE2D *shape = (struct SHAPE2D *)original_bytes;
	shape->width = 80;
	shape->height = 83;
	shape->position_x = 112;
	shape->position_y = 6;
	original_bytes[SHAPE2D_HEADER_SIZE + 4 * 80 + 66] = 39;
	test_fallback(shape);
	test_photo(shape);
	test_positioned_photo(shape);
	test_prepared(shape);
	test_original_upscale(shape);
	test_render_scale_roundtrip(shape);
	reset();
	hires_set_enabled(1);
	shape->width = 79;
	opponent_portrait_draw(&target, shape, 1);
	assert(hires_framebuffer_argb(screen, palette) == NULL);
	reset();
	assert(remove("opponents/opp1.png") == 0);
	assert(remove("opponents/opp3.png") == 0);
	assert(remove("opponents/opp4.png") == 0);
	assert(remove("opponents/game2/opp1.png") == 0);
	assert(rmdir("opponents/game2") == 0);
	assert(rmdir("opponents/game") == 0);
	assert(rmdir("opponents") == 0);
	assert(chdir("..") == 0);
	assert(rmdir("portrait-test") == 0);
	SDL_Quit();
	puts("Opponent portrait preparation, loading, fallback, number and clipping tests passed.");
	return 0;
}
