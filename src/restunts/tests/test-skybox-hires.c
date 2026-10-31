#include "../c/video_framebuffer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "../c/hires/hires.h"
#include "../c/platform.h"
#include "../c/shape2d/shape2d.h"
#include "../c/shape2d/shape2d_internal.h"
#include "../c/hires/skybox_hires.h"
#include "../c/skybox.h"

static struct SPRITE target;
static legacy_u8 rows[200 * 2];
static legacy_u8 *screen;
static legacy_u32 timer_pumps;

void sdl3_timer_pump(void)
{
	timer_pumps++;
}

#ifdef __EMSCRIPTEN__
void sdl3_browser_yield_if_due(void)
{
}
#endif

void dos_process_exit(legacy_s16 status)
{
	exit(status);
}

static void reset_target(void)
{
	hires_shutdown();
	skybox_hires_unload();
	memset(screen, 3, 320 * 200);
	target.sprite_raster_left = 0;
	target.sprite_raster_right = 320;
	target.sprite_top = 0;
	target.sprite_bottom = 200;
	hires_set_enabled(1);
}

static const legacy_u8 *pixels(void)
{
	legacy_s32 width;
	legacy_s32 height;
	const legacy_u8 *result = video_framebuffer(screen, &width, &height);
	assert(width == hires_render_width() && height == hires_render_height());
	return result;
}

static legacy_u8 fixture_color(legacy_s32 x, legacy_s32 y)
{
	return 17 + (x + y * 8) % 63;
}

static void write_fixture(const legacy_char *path)
{
	SDL_Surface *source = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGB24);
	assert(source != NULL);
	for (legacy_s32 y = 0; y < 8; y++) {
		for (legacy_s32 x = 0; x < 8; x++) {
			legacy_u8 shade = (fixture_color(x, y) - 16U) * 255U / 63U;
			assert(SDL_WriteSurfacePixel(source, x, y, shade, shade, shade, 255));
		}
	}
	assert(SDL_SavePNG(source, path));
	SDL_DestroySurface(source);
}

#define CONVERSION_WIDTH 65
#define CONVERSION_COLOR_RUN_WIDTH 3
#define CONVERSION_HEIGHT 3
#define CONVERSION_PALETTE_COLORS (LEGACY_U8_MAX + 1)
#define CONVERSION_PALETTE_CHANNELS 3
#define CONVERSION_PALETTE_CHANNEL_MAX 63U
#define CONVERSION_FIXTURE_PATH "skyboxes/tropical-scen.png"
#define CONVERSION_FIXTURE_THEME 1
#define CONVERSION_PARTIAL_ALPHA_FIXTURE 3

static void test_palette_conversion_and_audio(const legacy_u8 *game_palette)
{
	static const SDL_Color fixture_colors[] = {{7, 19, 31, SDL_ALPHA_TRANSPARENT},
											   {56, 81, 107, 85},
											   {141, 177, 203, 170},
											   {217, 233, 251, SDL_ALPHA_OPAQUE}};
	static const SDL_PixelFormat formats[] = {SDL_PIXELFORMAT_RGB24, SDL_PIXELFORMAT_RGBA32,
											  SDL_PIXELFORMAT_INDEX8, SDL_PIXELFORMAT_INDEX8};
	SDL_Color colors[CONVERSION_PALETTE_COLORS];
	for (legacy_s32 index = 0; index < (legacy_s32)CONVERSION_PALETTE_COLORS; index++) {
		const legacy_u8 *color = game_palette + index * CONVERSION_PALETTE_CHANNELS;
		colors[index].r = color[0] * SDL_ALPHA_OPAQUE / CONVERSION_PALETTE_CHANNEL_MAX;
		colors[index].g = color[1] * SDL_ALPHA_OPAQUE / CONVERSION_PALETTE_CHANNEL_MAX;
		colors[index].b = color[2] * SDL_ALPHA_OPAQUE / CONVERSION_PALETTE_CHANNEL_MAX;
		colors[index].a = SDL_ALPHA_OPAQUE;
	}
	SDL_Palette *palette = SDL_CreatePalette(CONVERSION_PALETTE_COLORS);
	assert(palette != NULL);
	assert(SDL_SetPaletteColors(palette, colors, 0, CONVERSION_PALETTE_COLORS));
	for (legacy_u32 fixture = 0; fixture < SDL_arraysize(formats); fixture++) {
		reset_target();
		SDL_Surface *source = SDL_CreateSurface(CONVERSION_WIDTH * HIRES_SCALE,
												CONVERSION_HEIGHT * HIRES_SCALE, formats[fixture]);
		assert(source != NULL);
		if (SDL_ISPIXELFORMAT_INDEXED(source->format)) {
			SDL_Palette *indexed_palette = SDL_CreatePalette(SDL_arraysize(fixture_colors));
			assert(indexed_palette != NULL);
			assert(SDL_SetPaletteColors(indexed_palette, fixture_colors, 0,
										SDL_arraysize(fixture_colors)));
			if (fixture != CONVERSION_PARTIAL_ALPHA_FIXTURE) {
				/* One transparent index loads as a color key; partial alpha loads
				 * with blending. Neither must skip or blend conversion pixels. */
				for (legacy_s32 index = 1; index < indexed_palette->ncolors; index++) {
					indexed_palette->colors[index].a = SDL_ALPHA_OPAQUE;
				}
			}
			assert(SDL_SetSurfacePalette(source, indexed_palette));
			SDL_DestroyPalette(indexed_palette);
		}
		for (legacy_s32 y = 0; y < source->h; y++) {
			for (legacy_s32 x = 0; x < source->w; x++) {
				legacy_u8 index =
					(x / CONVERSION_COLOR_RUN_WIDTH + y) % SDL_arraysize(fixture_colors);
				if (SDL_ISPIXELFORMAT_INDEXED(source->format)) {
					((legacy_u8 *)source->pixels)[y * source->pitch + x] = index;
				} else {
					SDL_Color color = fixture_colors[index];
					assert(SDL_WriteSurfacePixel(source, x, y, color.r, color.g, color.b, color.a));
				}
			}
		}
		assert(SDL_SavePNG(source, CONVERSION_FIXTURE_PATH));
		SDL_DestroySurface(source);
		source = SDL_LoadPNG(CONVERSION_FIXTURE_PATH);
		assert(source != NULL);
		SDL_Surface *reference = SDL_ConvertSurfaceAndColorspace(source, SDL_PIXELFORMAT_INDEX8,
																 palette, SDL_COLORSPACE_SRGB, 0);
		assert(reference != NULL);
		timer_pumps = 0;
		skybox_hires_draw(&target, CONVERSION_FIXTURE_THEME, 0, CONVERSION_WIDTH, CONVERSION_HEIGHT,
						  0, 0);
		/* Require checkpoints within a conversion, independently of machine speed. */
		assert(timer_pumps >= (legacy_u32)source->h);
		const legacy_u8 *output = pixels();
		for (legacy_s32 y = 0; y < source->h; y++) {
			assert(memcmp(output + y * HIRES_WIDTH,
						  (const legacy_u8 *)reference->pixels + y * reference->pitch,
						  source->w) == 0);
		}
		legacy_u32 completed_pumps = timer_pumps;
		skybox_hires_draw(&target, CONVERSION_FIXTURE_THEME, 0, CONVERSION_WIDTH, CONVERSION_HEIGHT,
						  0, 0);
		assert(timer_pumps == completed_pumps);
		SDL_DestroySurface(reference);
		SDL_DestroySurface(source);
	}
	SDL_DestroyPalette(palette);
	skybox_hires_unload();
	assert(remove(CONVERSION_FIXTURE_PATH) == 0);
}

static void test_detail_clipping_and_legacy(void)
{
	reset_target();
	skybox_hires_draw(&target, 0, 0, 2, 2, -1, -1);
	const legacy_u8 *output = pixels();
	for (legacy_s32 y = 0; y < HIRES_HEIGHT; y++) {
		for (legacy_s32 x = 0; x < HIRES_WIDTH; x++) {
			legacy_u8 expected = x < 4 && y < 4 ? fixture_color(x + 4, y + 4) : 3;
			assert(output[y * HIRES_WIDTH + x] == expected);
		}
	}
	for (legacy_s32 index = 0; index < 320 * 200; index++) {
		assert(screen[index] == 3);
	}
	reset_target();
	target.sprite_raster_left = 1;
	target.sprite_raster_right = 2;
	target.sprite_top = 1;
	target.sprite_bottom = 2;
	skybox_hires_draw(&target, 0, 0, 2, 2, 0, 0);
	output = pixels();
	assert(output[4 * HIRES_WIDTH + 4] == fixture_color(4, 4));
	assert(output[4 * HIRES_WIDTH + 3] == 3);
	assert(output[3 * HIRES_WIDTH + 4] == 3);
	assert(output[4 * HIRES_WIDTH + 8] == 3);
	assert(output[8 * HIRES_WIDTH + 4] == 3);
}

static void assert_plain(void)
{
	const legacy_u8 *output = pixels();
	for (legacy_s32 index = 0; index < HIRES_WIDTH * HIRES_HEIGHT; index++) {
		assert(output[index] == 3);
	}
}

static void test_fallback_and_toggle(void)
{
	reset_target();
	/* Missing, malformed, and incorrectly sized images retain the legacy sky. */
	skybox_hires_draw(&target, 0, 1, 2, 2, 0, 0);
	skybox_hires_draw(&target, 0, 2, 2, 2, 0, 0);
	skybox_hires_draw(&target, 0, 0, 3, 2, 0, 0);
	assert_plain();
	reset_target();
	skybox_hires_draw(&target, -1, 0, 2, 2, 0, 0);
	skybox_hires_draw(&target, 5, 0, 2, 2, 0, 0);
	skybox_hires_draw(&target, 0, 4, 2, 2, 0, 0);
	skybox_hires_draw(&target, 0, 0, 2, 2, -20, -20);
	skybox_hires_draw(&target, 0, 0, 2, 2, 320, 200);
	assert_plain();
	hires_set_enabled(0);
	skybox_hires_draw(&target, 0, 0, 2, 2, 0, 0);
	hires_set_enabled(1);
	assert_plain();
	skybox_hires_draw(&target, 0, 0, 2, 2, 0, 0);
	assert(pixels()[0] == fixture_color(0, 0));
	hires_set_enabled(0);
	legacy_s32 width;
	legacy_s32 height;
	assert(video_framebuffer(screen, &width, &height) == screen);
	assert(width == 320 && height == 200);
}

static void test_copies_overlays_and_theme_changes(void)
{
	reset_target();
	skybox_hires_draw(&target, 0, 0, 2, 2, 0, 0);
	hires_raster(screen, 10, screen, 0, 1, SHAPE2D_RASTER_COPY, NULL);
	assert(pixels()[40] == fixture_color(0, 0));
	assert(pixels()[43] == fixture_color(3, 0));
	hires_write(screen, 10, 3);
	assert(pixels()[40] == 3);
	skybox_hires_draw(&target, 1, 0, 2, 2, 0, 0);
	/* Changing to a missing theme cannot reuse the previous theme's image. */
	hires_write(screen, 0, 3);
	skybox_hires_draw(&target, 1, 0, 2, 2, 0, 0);
	assert(pixels()[0] == 3);
	skybox_hires_draw(&target, 0, 0, 2, 2, 0, 0);
	assert(pixels()[0] == fixture_color(0, 0));
	hires_write(screen, 0, 3);
	skybox_hires_draw(&target, 2, 0, 2, 2, 0, 0);
	assert(pixels()[0] == fixture_color(0, 0));
	/* Palette changes remap the source without consulting the display palette. */
	legacy_u8 black_palette[768] = {0};
	skybox_hires_set_palette(black_palette);
	skybox_hires_draw(&target, 0, 0, 2, 2, 0, 0);
	assert(pixels()[0] == 0);
}

legacy_u16 projection_center_x = 160;
legacy_u16 projection_center_y = 100;
legacy_u16 projection_focal_length_x = 160;
legacy_u16 projection_focal_length_y = 160;

static const legacy_s32 panorama_widths[SKYBOX_IMAGE_COUNT] = {320, 192, 320, 192};
static const legacy_s32 panorama_heights[SKYBOX_IMAGE_COUNT] = {24, 32, 40, 48};
static const legacy_char *panorama_paths[SKYBOX_IMAGE_COUNT] = {
	"skyboxes/city-scen.png", "skyboxes/city-sce2.png", "skyboxes/city-sce3.png",
	"skyboxes/city-sce4.png"};
static struct SHAPE2D *panorama_shapes[SKYBOX_IMAGE_COUNT];
static struct SKYBOX scenery = {{24, 32, 40, 48}, 24, 48, 1, 2, 4};

static legacy_u8 panorama_color(legacy_s32 image, legacy_s32 x, legacy_s32 y)
{
	return 17 + (image * 13 + x * 3 + y * 11) % 63;
}

static legacy_u8 original_color(legacy_s32 image, legacy_s32 x, legacy_s32 y)
{
	return 96 + image * 12 + x % 3 + y % 4 * 3;
}

static void create_panorama_fixtures(void)
{
	for (legacy_s32 image = 0; image < SKYBOX_IMAGE_COUNT; image++) {
		legacy_s32 width = panorama_widths[image];
		legacy_s32 height = panorama_heights[image];
		struct SHAPE2D *shape = calloc(1, sizeof(*shape) + width * height);
		assert(shape != NULL);
		shape->width = width;
		shape->height = height;
		panorama_shapes[image] = shape;
		legacy_u8 *original = (legacy_u8 *)(shape + 1);
		for (legacy_s32 y = 0; y < height; y++) {
			for (legacy_s32 x = 0; x < width; x++) {
				original[y * width + x] = original_color(image, x, y);
			}
		}
		SDL_Surface *source = SDL_CreateSurface(width * 4, height * 4, SDL_PIXELFORMAT_RGB24);
		assert(source != NULL);
		for (legacy_s32 y = 0; y < height * 4; y++) {
			for (legacy_s32 x = 0; x < width * 4; x++) {
				legacy_u8 shade = (panorama_color(image, x, y) - 16U) * 255U / 63U;
				assert(SDL_WriteSurfacePixel(source, x, y, shade, shade, shade, 255));
			}
		}
		assert(SDL_SavePNG(source, panorama_paths[image]));
		SDL_DestroySurface(source);
	}
}

static void destroy_panorama_fixtures(void)
{
	for (legacy_s32 image = 0; image < SKYBOX_IMAGE_COUNT; image++) {
		assert(remove(panorama_paths[image]) == 0);
		free(panorama_shapes[image]);
	}
}

static struct MATRIX roll_matrix(legacy_s32 angle)
{
	legacy_f64 radians = angle * (2.0 * SDL_PI_D / 1024.0);
	legacy_s16 cosine = SDL_lround(SDL_cos(radians) * TRIG_FIXED_ONE);
	legacy_s16 sine = SDL_lround(SDL_sin(radians) * TRIG_FIXED_ONE);
	struct MATRIX rotation = {0};
	rotation.m._11 = cosine;
	rotation.m._21 = sine;
	rotation.m._12 = -sine;
	rotation.m._22 = cosine;
	rotation.m._33 = TRIG_FIXED_ONE;
	return rotation;
}

/* Cardinal rotations give exact source texels, independently of the renderer's
 * general rotation/projection calculation. band_y is the source row relative
 * to the panorama's bottom edge, in enhanced pixels. */
static legacy_u8 expected_panorama(legacy_s32 u, legacy_s32 band_y, legacy_s32 original_mask,
								   legacy_s32 detail)
{
	if (band_y >= 0) {
		return scenery.ground_color;
	}
	if (detail == 4) {
		return scenery.sky_color;
	}
	u = (u % 4096 + 4096) % 4096;
	legacy_s32 image = 0;
	while (u >= panorama_widths[image] * 4) {
		u -= panorama_widths[image] * 4;
		image++;
	}
	legacy_s32 row = panorama_heights[image] * 4 + band_y;
	if (row < 0) {
		return scenery.sky_color;
	}
	if (original_mask & (1 << image)) {
		return original_color(image, u / 4, row / 4);
	}
	return panorama_color(image, u, row);
}

static void assert_legacy_unchanged(void)
{
	for (legacy_s32 index = 0; index < 320 * 200; index++) {
		assert(screen[index] == 3);
	}
}

static void assert_cardinal_image(legacy_s32 quarter_turns, legacy_s32 heading, legacy_s32 altitude,
								  legacy_s32 original_mask, legacy_s32 detail)
{
	const legacy_u8 *output = pixels();
	for (legacy_s32 y = 0; y < HIRES_HEIGHT; y++) {
		for (legacy_s32 x = 0; x < HIRES_WIDTH; x++) {
			legacy_s32 u;
			legacy_s32 band_y;
			switch (quarter_turns) {
				case 0:
					u = x;
					band_y = y - 400;
					break;
				case 1:
					u = 1039 - y;
					band_y = x - 640;
					break;
				case 2:
					u = 1279 - x;
					band_y = 399 - y;
					break;
				default:
					u = 240 + y;
					band_y = 639 - x;
					break;
			}
			legacy_u8 expected = 3;
			if (x >= target.sprite_raster_left * 4 && x < target.sprite_raster_right * 4 &&
				y >= target.sprite_top * 4 && y < target.sprite_bottom * 4) {
				expected = expected_panorama(u + heading, band_y + altitude, original_mask, detail);
			}
			assert(output[y * HIRES_WIDTH + x] == expected);
		}
	}
	assert_legacy_unchanged();
}

static void test_cardinal_rotations_and_wrap(void)
{
	for (legacy_s32 quarter = 0; quarter <= 4; quarter++) {
		struct MATRIX rotation = roll_matrix(quarter * 256);
		reset_target();
		assert(skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, 1, 512, 0, 0));
		assert_cardinal_image(quarter % 4, 0, 0, 0, 0);
	}
	/* These headings put each of the differently sized strips at the left
	 * edge and cross their joins; full-turn headings wrap without a seam. */
	static const legacy_s16 headings[] = {192, 0, 704, 1536, -512};
	struct MATRIX rotation = roll_matrix(0);
	for (legacy_s32 index = 0; index < (legacy_s32)SDL_arraysize(headings); index++) {
		reset_target();
		assert(skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, 1,
								   headings[index], 0, 0));
		assert_cardinal_image(0, -((headings[index] + 512) & 1023) * 4, 0, 0, 0);
	}
}

static void test_banked_roads_and_corkscrew(void)
{
	/* The old horizon-endpoint cutoff lies between the 44/1024 and 52/1024
	 * rolls with this viewport. Check both banks and a complete corkscrew. */
	static const legacy_s16 rolls[] = {40,	44,	 48,  52,  64,	-40, -44, -48, -52, -64, 128, 192,
									   256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960};
	for (legacy_s32 index = 0; index < (legacy_s32)SDL_arraysize(rolls); index++) {
		reset_target();
		struct MATRIX rotation = roll_matrix(rolls[index]);
		assert(skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, 1, 512, 0, 0));
		const legacy_u8 *output = pixels();
		legacy_s32 scenery_pixels = 0;
		for (legacy_s32 pixel = 0; pixel < HIRES_WIDTH * HIRES_HEIGHT; pixel++) {
			scenery_pixels += output[pixel] >= 17 && output[pixel] <= 79;
		}
		assert(scenery_pixels > 16000);
		assert_legacy_unchanged();
	}
}

enum {
	OBLIQUE_NORMAL_X = 3,
	OBLIQUE_NORMAL_Y = 4,
	OBLIQUE_NORMAL_LENGTH = 5,
	OBLIQUE_MATRIX_SCALE = 1000,
	OBLIQUE_TEXEL_DENOMINATOR = 2 * OBLIQUE_NORMAL_LENGTH,
	OBLIQUE_CLIP_LEFT = 9,
	OBLIQUE_CLIP_RIGHT_MARGIN = 13,
	OBLIQUE_CLIP_TOP = 7,
	OBLIQUE_CLIP_BOTTOM_MARGIN = 19,
	OBLIQUE_BACKGROUND_COLOR = 3,
	OBLIQUE_ORIGINAL_THEME = 0,
	OBLIQUE_ENHANCED_THEME = 3,
	OBLIQUE_FORWARD_DIRECTION = 1,
	OBLIQUE_CAMERA_ALTITUDE = 0,
	OBLIQUE_FULL_DETAIL = 0,
	OBLIQUE_ALL_ORIGINAL_MASK = (1U << SKYBOX_IMAGE_COUNT) - 1
};

/* The 3:4 normal is a 3/5, 4/5 rotation after normalization. Computing
 * its texels in tenths gives an independent integer oracle; odd numerators
 * keep every sample away from floating-point texel boundaries. */
static legacy_s32 floor_oblique_coordinate(legacy_s32 value)
{
	return value >= 0 ? value / OBLIQUE_TEXEL_DENOMINATOR
					  : (value - (OBLIQUE_TEXEL_DENOMINATOR - 1)) / OBLIQUE_TEXEL_DENOMINATOR;
}

static void test_oblique_texels_and_original_fallback(void)
{
	const legacy_s32 scales[] = {HIRES_SCALE, HIRES_MEDIUM_SCALE, HIRES_MINIMUM_SCALE};
	for (legacy_s32 sign = -1; sign <= 1; sign += 2) {
		struct MATRIX rotation = {0};
		rotation.m._12 = sign * OBLIQUE_NORMAL_X * OBLIQUE_MATRIX_SCALE;
		rotation.m._22 = sign * OBLIQUE_NORMAL_Y * OBLIQUE_MATRIX_SCALE;
		rotation.m._33 = OBLIQUE_NORMAL_LENGTH * OBLIQUE_MATRIX_SCALE;
		for (legacy_s32 original = 0; original <= 1; original++) {
			reset_target();
			target.sprite_raster_left = OBLIQUE_CLIP_LEFT;
			target.sprite_raster_right = SKYBOX_SCREEN_WIDTH - OBLIQUE_CLIP_RIGHT_MARGIN;
			target.sprite_top = OBLIQUE_CLIP_TOP;
			target.sprite_bottom = SKYBOX_SCREEN_BOTTOM - OBLIQUE_CLIP_BOTTOM_MARGIN;
			for (legacy_u32 step = 0; step < SDL_arraysize(scales); step++) {
				legacy_s32 scale = scales[step];
				legacy_s32 source_step = HIRES_SCALE / scale;
				hires_set_render_scale(scale);
				assert(
					skybox_hires_render(&target, &scenery, panorama_shapes,
										original ? OBLIQUE_ORIGINAL_THEME : OBLIQUE_ENHANCED_THEME,
										&rotation, OBLIQUE_FORWARD_DIRECTION, ANGLE_HALF_TURN,
										OBLIQUE_CAMERA_ALTITUDE, OBLIQUE_FULL_DETAIL));
				const legacy_u8 *output = pixels();
				for (legacy_s32 y = 0; y < hires_render_height(); y++) {
					for (legacy_s32 x = 0; x < hires_render_width(); x++) {
						legacy_u8 expected = OBLIQUE_BACKGROUND_COLOR;
						if (x >= target.sprite_raster_left * scale &&
							x < target.sprite_raster_right * scale &&
							y >= target.sprite_top * scale && y < target.sprite_bottom * scale) {
							legacy_s32 doubled_x = (2 * x + 1) * source_step - HIRES_WIDTH;
							legacy_s32 doubled_y = (2 * y + 1) * source_step - HIRES_HEIGHT;
							legacy_s32 along_numerator =
								projection_center_x * HIRES_SCALE * OBLIQUE_TEXEL_DENOMINATOR +
								sign *
									(OBLIQUE_NORMAL_Y * doubled_x + OBLIQUE_NORMAL_X * doubled_y);
							legacy_s32 above_numerator = sign * (OBLIQUE_NORMAL_Y * doubled_y -
																 OBLIQUE_NORMAL_X * doubled_x);
							legacy_s32 u = floor_oblique_coordinate(along_numerator);
							legacy_s32 band_y = floor_oblique_coordinate(above_numerator);
							expected = expected_panorama(u, band_y,
														 original || scale == HIRES_MINIMUM_SCALE
															 ? OBLIQUE_ALL_ORIGINAL_MASK
															 : 0,
														 OBLIQUE_FULL_DETAIL);
							/* Reduced-resolution centers can land exactly on a texel
							 * edge. Either neighboring texel is valid at that edge. */
							legacy_s32 edge_x = along_numerator % OBLIQUE_TEXEL_DENOMINATOR == 0;
							legacy_s32 edge_y = above_numerator % OBLIQUE_TEXEL_DENOMINATOR == 0;
							for (legacy_s32 dy = 0; dy <= edge_y; dy++) {
								for (legacy_s32 dx = 0; dx <= edge_x; dx++) {
									legacy_u8 adjacent =
										expected_panorama(u - dx, band_y - dy,
														  original || scale == HIRES_MINIMUM_SCALE
															  ? OBLIQUE_ALL_ORIGINAL_MASK
															  : 0,
														  OBLIQUE_FULL_DETAIL);
									if (output[y * hires_render_width() + x] == adjacent) {
										expected = adjacent;
									}
								}
							}
						}
						assert(output[y * hires_render_width() + x] == expected);
					}
				}
				assert_legacy_unchanged();
			}
		}
	}
}

static void test_pitch_poles_and_inverted_altitude(void)
{
	for (legacy_s16 direction = -1; direction <= 1; direction += 2) {
		reset_target();
		struct MATRIX rotation = {0};
		rotation.m._11 = TRIG_FIXED_ONE;
		rotation.m._32 = direction * TRIG_FIXED_ONE;
		rotation.m._23 = -direction * TRIG_FIXED_ONE;
		assert(skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, 1, 512, 0, 0));
		const legacy_u8 *output = pixels();
		legacy_u8 expected = direction > 0 ? scenery.sky_color : scenery.ground_color;
		for (legacy_s32 pixel = 0; pixel < HIRES_WIDTH * HIRES_HEIGHT; pixel++) {
			assert(output[pixel] == expected);
		}
		assert_legacy_unchanged();
	}
	/* A half-turn in pitch reverses both world up and the altitude correction.
	 * At height 1500, the horizon moves down 16 logical pixels. Rear views
	 * reverse the altitude correction and advance the panorama by half a turn. */
	struct MATRIX rotation = {0};
	rotation.m._11 = TRIG_FIXED_ONE;
	rotation.m._22 = -TRIG_FIXED_ONE;
	rotation.m._33 = -TRIG_FIXED_ONE;
	for (legacy_s16 direction = -1; direction <= 1; direction += 2) {
		reset_target();
		assert(skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, direction, 512,
								   1500, 0));
		assert_cardinal_image(2, direction < 0 ? 2048 : 0, direction * 64, 0, 0);
	}
}

static void test_anisotropic_projection(void)
{
	reset_target();
	projection_focal_length_x = 200;
	projection_focal_length_y = 100;
	struct MATRIX rotation = roll_matrix(256);
	assert(skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, 1, 512, 0, 0));
	const legacy_u8 *output = pixels();
	/* With focal lengths in a 2:1 ratio, a quarter-turn makes the panorama
	 * twice as wide vertically and halves its height horizontally. */
	for (legacy_s32 y = 0; y < HIRES_HEIGHT; y++) {
		for (legacy_s32 x = 0; x < HIRES_WIDTH; x++) {
			legacy_u8 expected = expected_panorama(1439 - 2 * y, x / 2 - 320, 0, 0);
			assert(output[y * HIRES_WIDTH + x] == expected);
		}
	}
	assert_legacy_unchanged();
	projection_focal_length_x = 160;
	projection_focal_length_y = 160;
}

static void test_oriented_clipping_fallback_and_toggle(void)
{
	struct MATRIX rotation = roll_matrix(768);
	reset_target();
	target.sprite_raster_left = 17;
	target.sprite_raster_right = 299;
	target.sprite_top = 11;
	target.sprite_bottom = 173;
	assert(skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, 1, 512, 0, 0));
	assert_cardinal_image(3, 0, 0, 0, 0);
	/* The desert fixtures are absent, malformed, or the wrong dimensions.
	 * Every strip must still rotate correctly using its original pixels. */
	for (legacy_s32 quarter = 0; quarter < 4; quarter++) {
		reset_target();
		rotation = roll_matrix(quarter * 256);
		assert(skybox_hires_render(&target, &scenery, panorama_shapes, 0, &rotation, 1, 0, 0, 0));
		assert_cardinal_image(quarter, -2048, 0, 15, 0);
	}
	reset_target();
	assert(skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, 1, 512, 0, 4));
	assert_cardinal_image(3, 0, 0, 0, 4);
	reset_target();
	hires_set_enabled(0);
	assert(!skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, 1, 512, 0, 0));
	hires_set_enabled(1);
	assert_plain();
	projection_focal_length_x = 0;
	assert(!skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, 1, 512, 0, 0));
	projection_focal_length_x = 160;
	projection_focal_length_y = 0;
	assert(!skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, 1, 512, 0, 0));
	projection_focal_length_y = 160;
	assert_plain();
	assert_legacy_unchanged();
	assert(skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, 1, 512, 0, 0));
	assert_cardinal_image(3, 0, 0, 0, 0);
}

static void test_mixed_artwork_and_view_transitions(void)
{
	reset_target();
	const legacy_char *hidden_path = "skyboxes/city-sce2.hidden.png";
	assert(rename(panorama_paths[1], hidden_path) == 0);
	struct MATRIX rotation = roll_matrix(0);
	assert(skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, 1, 192, 0, 0));
	/* The second strip uses original pixels next to the enhanced third strip. */
	assert_cardinal_image(0, -704 * 4, 0, 1 << 1, 0);
	assert(rename(hidden_path, panorama_paths[1]) == 0);
	skybox_hires_unload();
	/* Consecutive views must overwrite every prior sky/ground/scenery pixel,
	 * including when the horizon changes from vertical to inverted. */
	rotation = roll_matrix(256);
	assert(skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, 1, 512, 0, 0));
	assert_cardinal_image(1, 0, 0, 0, 0);
	rotation = roll_matrix(512);
	assert(skybox_hires_render(&target, &scenery, panorama_shapes, 3, &rotation, 1, 512, 0, 0));
	assert_cardinal_image(2, 0, 0, 0, 0);
}

enum {
	LEVEL_FOCAL_X = 200,
	LEVEL_FOCAL_Y = 128,
	LEVEL_CENTER_X = 159,
	LEVEL_CENTER_Y = 99,
	LEVEL_HALF_PIXEL_MATRIX_Z = TRIG_FIXED_ONE / (2 * LEVEL_FOCAL_Y * HIRES_SCALE),
	LEVEL_ALTITUDE = SKYBOX_ROLL_VECTOR_Z / 8,
	LEVEL_ALTITUDE_HORIZON_HALVES = 2 * LEVEL_FOCAL_Y * HIRES_SCALE / 8,
	LEVEL_NARROW_COLUMNS = 7,
	LEVEL_NO_STRIP = -1,
	LEVEL_ENHANCED_THEME = 3,
	LEVEL_ORIGINAL_THEME = 0,
	LEVEL_ALL_ORIGINAL_MASK = (1U << SKYBOX_IMAGE_COUNT) - 1,
	LEVEL_BACKGROUND_COLOR = 3,
	LEVEL_CLIP_LEFT = 11,
	LEVEL_CLIP_RIGHT_MARGIN = 17,
	LEVEL_CLIP_TOP = 5,
	LEVEL_CLIP_BOTTOM_MARGIN = 13,
	LEVEL_LOWEST_DETAIL = 4,
	LEVEL_TINY_BANK = 1
};

struct LEVEL_PANORAMA_CASE {
	legacy_s16 normal_sign, horizon_halves, direction, heading, altitude;
	legacy_s32 original, clipped, missing, narrow, detail;
};

static legacy_s32 floor_half_coordinate(legacy_s32 value)
{
	return value >= 0 ? value / 2 : (value - 1) / 2;
}

static legacy_u8 expected_level_panorama(legacy_s32 u, legacy_s32 band_y,
										 const struct LEVEL_PANORAMA_CASE *scene)
{
	legacy_s32 original_mask = scene->original ? LEVEL_ALL_ORIGINAL_MASK : 0;
	if (scene->narrow != LEVEL_NO_STRIP) {
		original_mask |= 1U << scene->narrow;
	}
	legacy_u8 expected = expected_panorama(u, band_y, original_mask, scene->detail);
	if (band_y >= 0 || scene->detail == LEVEL_LOWEST_DETAIL) {
		return expected;
	}
	legacy_s32 column =
		(u % (SKYBOX_IMAGE_FULL_WRAP * HIRES_SCALE) + SKYBOX_IMAGE_FULL_WRAP * HIRES_SCALE) %
		(SKYBOX_IMAGE_FULL_WRAP * HIRES_SCALE);
	legacy_s32 image = 0;
	while (column >= panorama_widths[image] * HIRES_SCALE) {
		column -= panorama_widths[image] * HIRES_SCALE;
		image++;
	}
	if (image == scene->missing ||
		(image == scene->narrow &&
		 column >= (panorama_widths[image] - LEVEL_NARROW_COLUMNS) * HIRES_SCALE)) {
		return scenery.sky_color;
	}
	return expected;
}

static void test_level_half_pixel_horizons(void)
{
	static const struct LEVEL_PANORAMA_CASE cases[] = {
		{1, 1, 1, 512, 0, 0, 0, LEVEL_NO_STRIP, LEVEL_NO_STRIP, 0},
		{-1, -1, 1, 192, 0, 0, 1, LEVEL_NO_STRIP, LEVEL_NO_STRIP, 0},
		{1, -3, -1, 704, LEVEL_ALTITUDE, 1, 1, LEVEL_NO_STRIP, LEVEL_NO_STRIP, 0},
		{-1, 3, -1, 0, -LEVEL_ALTITUDE, 0, 1, LEVEL_NO_STRIP, LEVEL_NO_STRIP, 0},
		{1, 0, 1, 192, LEVEL_ALTITUDE, 0, 1, 1, LEVEL_NO_STRIP, 0},
		{-1, 1, -1, 704, 0, 0, 0, 1, LEVEL_NO_STRIP, 0},
		{1, 1, 1, 192, 0, 0, 1, LEVEL_NO_STRIP, 1, 0},
		{-1, -3, -1, 704, LEVEL_ALTITUDE, 1, 0, LEVEL_NO_STRIP, 1, 0},
		{1, 3, 1, 192, -LEVEL_ALTITUDE, 0, 0, 2, 1, 0},
		{-1, 1, -1, 192, LEVEL_ALTITUDE, 0, 1, LEVEL_NO_STRIP, LEVEL_NO_STRIP,
		 LEVEL_LOWEST_DETAIL}};
	legacy_u16 saved_center_x = projection_center_x;
	legacy_u16 saved_center_y = projection_center_y;
	legacy_u16 saved_focal_x = projection_focal_length_x;
	legacy_u16 saved_focal_y = projection_focal_length_y;
	projection_center_x = LEVEL_CENTER_X;
	projection_center_y = LEVEL_CENTER_Y;
	projection_focal_length_x = LEVEL_FOCAL_X;
	projection_focal_length_y = LEVEL_FOCAL_Y;
	for (legacy_s32 index = 0; index < (legacy_s32)SDL_arraysize(cases); index++) {
		const struct LEVEL_PANORAMA_CASE *scene = &cases[index];
		reset_target();
		if (scene->clipped) {
			target.sprite_raster_left = LEVEL_CLIP_LEFT;
			target.sprite_raster_right = SKYBOX_SCREEN_WIDTH - LEVEL_CLIP_RIGHT_MARGIN;
			target.sprite_top = LEVEL_CLIP_TOP;
			target.sprite_bottom = SKYBOX_SCREEN_BOTTOM - LEVEL_CLIP_BOTTOM_MARGIN;
		}
		struct SHAPE2D *shapes[SKYBOX_IMAGE_COUNT];
		memcpy(shapes, panorama_shapes, sizeof(shapes));
		if (scene->missing != LEVEL_NO_STRIP) {
			shapes[scene->missing] = NULL;
		}
		struct SHAPE2D *narrow = NULL;
		if (scene->narrow != LEVEL_NO_STRIP) {
			legacy_s32 width = panorama_widths[scene->narrow] - LEVEL_NARROW_COLUMNS;
			legacy_s32 height = panorama_heights[scene->narrow];
			narrow = calloc(1, sizeof(*narrow) + width * height);
			assert(narrow != NULL);
			narrow->width = width;
			narrow->height = height;
			legacy_u8 *source = (legacy_u8 *)(narrow + 1);
			for (legacy_s32 y = 0; y < height; y++) {
				for (legacy_s32 x = 0; x < width; x++) {
					source[y * width + x] = original_color(scene->narrow, x, y);
				}
			}
			shapes[scene->narrow] = narrow;
		}
		struct MATRIX rotation = {0};
		rotation.m._22 = scene->normal_sign * TRIG_FIXED_ONE;
		rotation.m._32 = scene->horizon_halves * LEVEL_HALF_PIXEL_MATRIX_Z;
		rotation.m._33 = TRIG_FIXED_ONE;
		assert(skybox_hires_render(&target, &scenery, shapes,
								   scene->original ? LEVEL_ORIGINAL_THEME : LEVEL_ENHANCED_THEME,
								   &rotation, scene->direction, scene->heading, scene->altitude,
								   scene->detail));
		/* These focal lengths, matrix entries and altitudes put every horizon
		 * on an exact half pixel. Doubled integer coordinates independently
		 * check source rows, including samples exactly on the ground boundary. */
		legacy_s32 horizon_halves = scene->horizon_halves + scene->direction * scene->altitude /
																LEVEL_ALTITUDE *
																LEVEL_ALTITUDE_HORIZON_HALVES;
		legacy_s32 phase = (LEVEL_CENTER_X - ((scene->heading + ANGLE_HALF_TURN) & ANGLE_MASK) +
							(scene->direction < 0 ? ANGLE_HALF_TURN : 0)) *
						   HIRES_SCALE;
		const legacy_u8 *output = pixels();
		for (legacy_s32 y = 0; y < HIRES_HEIGHT; y++) {
			for (legacy_s32 x = 0; x < HIRES_WIDTH; x++) {
				legacy_u8 expected = LEVEL_BACKGROUND_COLOR;
				if (x >= target.sprite_raster_left * HIRES_SCALE &&
					x < target.sprite_raster_right * HIRES_SCALE &&
					y >= target.sprite_top * HIRES_SCALE &&
					y < target.sprite_bottom * HIRES_SCALE) {
					legacy_s32 u =
						floor_half_coordinate(scene->normal_sign *
											  (2 * x + 1 - 2 * LEVEL_CENTER_X * HIRES_SCALE)) +
						phase;
					legacy_s32 band_y = floor_half_coordinate(
						scene->normal_sign * (2 * y + 1 - 2 * LEVEL_CENTER_Y * HIRES_SCALE) -
						horizon_halves);
					expected = expected_level_panorama(u, band_y, scene);
				}
				assert(output[y * HIRES_WIDTH + x] == expected);
			}
		}
		assert_legacy_unchanged();
		free(narrow);
	}
	projection_center_x = saved_center_x;
	projection_center_y = saved_center_y;
	projection_focal_length_x = saved_focal_x;
	projection_focal_length_y = saved_focal_y;
}

static void test_banked_panorama_cache_changes(void)
{
	enum { PANORAMA_CACHE_STAGE_COUNT = 3 };
	reset_target();
	struct MATRIX rotation = roll_matrix(ANGLE_QUARTER_TURN);
	struct SHAPE2D *shapes[SKYBOX_IMAGE_COUNT];
	memcpy(shapes, panorama_shapes, sizeof(shapes));
	struct LEVEL_PANORAMA_CASE scene = {0};
	scene.missing = 1;
	scene.narrow = LEVEL_NO_STRIP;
	shapes[scene.missing] = NULL;
	legacy_u8 saved_sky_color = scenery.sky_color;
	legacy_s16 heading = ANGLE_HALF_TURN - SKYBOX_IMAGE_WIDTH;
	legacy_s32 phase = -((heading + ANGLE_HALF_TURN) & ANGLE_MASK) * HIRES_SCALE;
	for (legacy_s32 step = 0; step < PANORAMA_CACHE_STAGE_COUNT; step++) {
		if (step != 0) {
			scenery.sky_color = OBLIQUE_BACKGROUND_COLOR;
		}
		if (step == PANORAMA_CACHE_STAGE_COUNT - 1) {
			shapes[scene.missing] = panorama_shapes[scene.missing];
			scene.missing = LEVEL_NO_STRIP;
		}
		assert(skybox_hires_render(&target, &scenery, shapes, LEVEL_ENHANCED_THEME, &rotation,
								   OBLIQUE_FORWARD_DIRECTION, heading, OBLIQUE_CAMERA_ALTITUDE,
								   OBLIQUE_FULL_DETAIL));
		const legacy_u8 *output = pixels();
		for (legacy_s32 y = 0; y < HIRES_HEIGHT; y++) {
			for (legacy_s32 x = 0; x < HIRES_WIDTH; x++) {
				legacy_s32 u = (HIRES_WIDTH + HIRES_HEIGHT) / 2 - 1 - y + phase;
				legacy_s32 band_y = x - HIRES_WIDTH / 2;
				assert(output[y * HIRES_WIDTH + x] == expected_level_panorama(u, band_y, &scene));
			}
		}
		assert_legacy_unchanged();
	}
	scenery.sky_color = saved_sky_color;
}

static void test_tiny_bank_texel_boundaries(void)
{
	legacy_u16 saved_focal_x = projection_focal_length_x;
	legacy_u16 saved_focal_y = projection_focal_length_y;
	projection_focal_length_x = LEVEL_FOCAL_X;
	projection_focal_length_y = LEVEL_FOCAL_Y;
	for (legacy_s32 direction = -1; direction <= 1; direction += 2) {
		reset_target();
		struct MATRIX rotation = {0};
		rotation.m._12 = direction * LEVEL_TINY_BANK;
		rotation.m._22 = TRIG_FIXED_ONE;
		rotation.m._32 = LEVEL_HALF_PIXEL_MATRIX_Z;
		assert(skybox_hires_render(&target, &scenery, panorama_shapes, LEVEL_ENHANCED_THEME,
								   &rotation, 1, ANGLE_HALF_TURN, 0, 0));
		/* A one-unit bank shifts source rows to either side of their exact
		 * texel boundary. Its column displacement stays below half a texel.
		 * Treating this small bank as level would fail half of each image. */
		const legacy_u8 *output = pixels();
		for (legacy_s32 y = 0; y < HIRES_HEIGHT; y++) {
			for (legacy_s32 x = 0; x < HIRES_WIDTH; x++) {
				legacy_s32 shifted = direction * (2 * x + 1 - HIRES_WIDTH) > 0;
				legacy_s32 band_y = y - HIRES_HEIGHT / 2 - shifted;
				assert(output[y * HIRES_WIDTH + x] == expected_panorama(x, band_y, 0, 0));
			}
		}
		assert_legacy_unchanged();
	}
	projection_focal_length_x = saved_focal_x;
	projection_focal_length_y = saved_focal_y;
}

/* Reduced resolutions sample destination-pixel centers in the same panorama.
 * The smallest scale uses original artwork, including banked/inverted views. */
static void test_render_scale_roundtrip(void)
{
	static legacy_u8 full_resolution[HIRES_WIDTH * HIRES_HEIGHT];
	const legacy_s32 scales[] = {HIRES_SCALE, HIRES_MEDIUM_SCALE, HIRES_MINIMUM_SCALE,
								 HIRES_MEDIUM_SCALE, HIRES_SCALE};
	for (legacy_s32 original = 0; original <= 1; original++) {
		for (legacy_s32 quarter = 0; quarter < 4; quarter++) {
			reset_target();
			target.sprite_raster_left = OBLIQUE_CLIP_LEFT;
			target.sprite_raster_right = SKYBOX_SCREEN_WIDTH - OBLIQUE_CLIP_RIGHT_MARGIN;
			target.sprite_top = OBLIQUE_CLIP_TOP;
			target.sprite_bottom = SKYBOX_SCREEN_BOTTOM - OBLIQUE_CLIP_BOTTOM_MARGIN;
			struct MATRIX rotation = roll_matrix(quarter * ANGLE_QUARTER_TURN);
			for (legacy_u32 step = 0; step < SDL_arraysize(scales); step++) {
				legacy_s32 scale = scales[step];
				legacy_s32 source_step = HIRES_SCALE / scale;
				hires_set_render_scale(scale);
				assert(hires_enabled());
				assert(
					skybox_hires_render(&target, &scenery, panorama_shapes,
										original ? OBLIQUE_ORIGINAL_THEME : OBLIQUE_ENHANCED_THEME,
										&rotation, OBLIQUE_FORWARD_DIRECTION, ANGLE_HALF_TURN,
										OBLIQUE_CAMERA_ALTITUDE, OBLIQUE_FULL_DETAIL));
				const legacy_u8 *output = pixels();
				legacy_s32 width = hires_render_width();
				legacy_s32 height = hires_render_height();
				legacy_u32 drawn = 0;
				for (legacy_s32 y = 0; y < height; y++) {
					for (legacy_s32 x = 0; x < width; x++) {
						legacy_u8 expected = OBLIQUE_BACKGROUND_COLOR;
						if (x >= target.sprite_raster_left * scale &&
							x < target.sprite_raster_right * scale &&
							y >= target.sprite_top * scale && y < target.sprite_bottom * scale) {
							legacy_f64 sx = (x + HIRES_SAMPLE_CENTER_OFFSET) * source_step;
							legacy_f64 sy = (y + HIRES_SAMPLE_CENTER_OFFSET) * source_step;
							legacy_f64 u, band_y;
							switch (quarter) {
								case 0:
									u = sx;
									band_y = sy - HIRES_HEIGHT / 2;
									break;
								case 1:
									u = (HIRES_WIDTH + HIRES_HEIGHT) / 2 - sy;
									band_y = sx - HIRES_WIDTH / 2;
									break;
								case 2:
									u = HIRES_WIDTH - sx;
									band_y = HIRES_HEIGHT / 2 - sy;
									break;
								default:
									u = (HIRES_WIDTH - HIRES_HEIGHT) / 2 + sy;
									band_y = HIRES_WIDTH / 2 - sx;
									break;
							}
							expected = expected_panorama(
								(legacy_s32)SDL_floor(u), (legacy_s32)SDL_floor(band_y),
								original || scale == HIRES_MINIMUM_SCALE ? OBLIQUE_ALL_ORIGINAL_MASK
																		 : 0,
								OBLIQUE_FULL_DETAIL);
							drawn++;
						}
						assert(output[y * width + x] == expected);
					}
				}
				assert(drawn ==
					   (legacy_u32)(target.sprite_raster_right - target.sprite_raster_left) *
						   (target.sprite_bottom - target.sprite_top) * scale * scale);
				if (step == 0) {
					memcpy(full_resolution, output, sizeof(full_resolution));
				} else if (step == SDL_arraysize(scales) - 1) {
					assert(memcmp(full_resolution, output, sizeof(full_resolution)) == 0);
				}
				assert_legacy_unchanged();
			}
		}
	}
	reset_target();
	for (legacy_u32 step = 0; step < SDL_arraysize(scales); step++) {
		legacy_s32 scale = scales[step];
		legacy_s32 source_step = HIRES_SCALE / scale;
		hires_set_render_scale(scale);
		skybox_hires_draw(&target, 0, 0, 2, 2, -1, -1);
		const legacy_u8 *output = pixels();
		for (legacy_s32 y = 0; y < hires_render_height(); y++) {
			for (legacy_s32 x = 0; x < hires_render_width(); x++) {
				legacy_u8 expected =
					scale != HIRES_MINIMUM_SCALE && x < scale && y < scale
						? fixture_color(HIRES_SCALE + x * source_step + source_step / 2,
										HIRES_SCALE + y * source_step + source_step / 2)
						: OBLIQUE_BACKGROUND_COLOR;
				assert(output[y * hires_render_width() + x] == expected);
			}
		}
		assert_legacy_unchanged();
	}
}

static void test_original_scale_retains_enhanced_cache(void)
{
	static legacy_u8 enhanced[HIRES_WIDTH * HIRES_HEIGHT];
	const legacy_char *hidden_path = "skyboxes/city-scen.cached.png";
	reset_target();
	struct MATRIX rotation = roll_matrix(0);
	assert(skybox_hires_render(&target, &scenery, panorama_shapes, OBLIQUE_ENHANCED_THEME,
							   &rotation, OBLIQUE_FORWARD_DIRECTION, ANGLE_HALF_TURN,
							   OBLIQUE_CAMERA_ALTITUDE, OBLIQUE_FULL_DETAIL));
	memcpy(enhanced, pixels(), sizeof(enhanced));
	assert(rename(panorama_paths[0], hidden_path) == 0);
	hires_set_render_scale(HIRES_MINIMUM_SCALE);
	assert(skybox_hires_render(&target, &scenery, panorama_shapes, OBLIQUE_ENHANCED_THEME,
							   &rotation, OBLIQUE_FORWARD_DIRECTION, ANGLE_HALF_TURN,
							   OBLIQUE_CAMERA_ALTITUDE, OBLIQUE_FULL_DETAIL));
	legacy_s32 first_row = SKYBOX_SCREEN_BOTTOM / 2 - panorama_heights[0];
	assert(pixels()[first_row * hires_render_width()] == original_color(0, 0, 0));
	/* Recovery must use the cached PNG even while its source file is absent. */
	hires_set_render_scale(HIRES_SCALE);
	assert(skybox_hires_render(&target, &scenery, panorama_shapes, OBLIQUE_ENHANCED_THEME,
							   &rotation, OBLIQUE_FORWARD_DIRECTION, ANGLE_HALF_TURN,
							   OBLIQUE_CAMERA_ALTITUDE, OBLIQUE_FULL_DETAIL));
	assert(memcmp(enhanced, pixels(), sizeof(enhanced)) == 0);
	assert(rename(hidden_path, panorama_paths[0]) == 0);
	assert_legacy_unchanged();
}

legacy_int main(void)
{
	assert(SDL_Init(0));
	screen = dos_memory_make_pointer(0xA000, 0);
	target.sprite_bitmapptr = (struct SHAPE2D *)screen;
	target.sprite_lineofs = rows;
	target.sprite_pitch = 320;
	for (legacy_s32 y = 0; y < 200; y++) {
		LEGACY_WRITE_U16_LE(rows + y * 2, y * 320);
	}
	/* Keep fixtures away from installed or user-provided skybox images. */
	assert(SDL_CreateDirectory("skytest"));
	assert(chdir("skytest") == 0);
	assert(SDL_CreateDirectory("skyboxes"));
	write_fixture("skyboxes/desert-scen.png");
	write_fixture("skyboxes/sky2-0.png");
	create_panorama_fixtures();
	FILE *invalid = fopen("skyboxes/desert-sce3.png", "wb");
	assert(invalid != NULL);
	assert(fputs("not a PNG", invalid) >= 0);
	assert(fclose(invalid) == 0);
	legacy_u8 palette[768] = {0};
	for (legacy_s32 index = 0; index < 64; index++) {
		memset(palette + (16 + index) * 3, index, 3);
	}
	skybox_hires_set_palette(palette);
	test_palette_conversion_and_audio(palette);
	test_detail_clipping_and_legacy();
	test_fallback_and_toggle();
	test_cardinal_rotations_and_wrap();
	test_banked_roads_and_corkscrew();
	test_oblique_texels_and_original_fallback();
	test_pitch_poles_and_inverted_altitude();
	test_anisotropic_projection();
	test_oriented_clipping_fallback_and_toggle();
	test_mixed_artwork_and_view_transitions();
	test_level_half_pixel_horizons();
	test_banked_panorama_cache_changes();
	test_tiny_bank_texel_boundaries();
	test_render_scale_roundtrip();
	test_original_scale_retains_enhanced_cache();
	test_copies_overlays_and_theme_changes();
	skybox_hires_unload();
	hires_shutdown();
	destroy_panorama_fixtures();
	assert(remove("skyboxes/desert-scen.png") == 0);
	assert(remove("skyboxes/sky2-0.png") == 0);
	assert(remove("skyboxes/desert-sce3.png") == 0);
	assert(rmdir("skyboxes") == 0);
	assert(chdir("..") == 0);
	assert(rmdir("skytest") == 0);
	SDL_Quit();
	puts("High-resolution skybox loading and composition tests passed.");
	return 0;
}
