#include "menu_background.h"
#include "hires.h"
#include "shape2d.h"
#include "shape2d_internal.h"
#include "asset_path.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define MENU_BACKGROUND_WIDTH 320
#define MENU_BACKGROUND_MAIN_HEIGHT 200
#define MENU_BACKGROUND_SHOWROOM_HEIGHT 103
#define MENU_BACKGROUND_OPPONENT_HEIGHT 182
#define MENU_BACKGROUND_ASSET_SCALE 2
#define MENU_BACKGROUND_PATH_SIZE 1024
#define MENU_BACKGROUND_DIRECTORY "menus/"
#define PNG_DIMENSION_HEADER_SIZE 24
#define PNG_IHDR_LENGTH_OFFSET 8
#define PNG_IHDR_TYPE_OFFSET 12
#define PNG_IHDR_DATA_LENGTH 13
#define PNG_IHDR_WIDTH_OFFSET 16
#define PNG_IHDR_HEIGHT_OFFSET 20
#define PNG_CHUNK_TYPE_SIZE 4

static SDL_Surface *images[MENU_BACKGROUND_COUNT];
static legacy_u8 attempted[MENU_BACKGROUND_COUNT];
static const legacy_char *const filenames[MENU_BACKGROUND_COUNT] = {"main.png", "showroom.png",
																	"opponent.png"};
static const legacy_s32 heights[MENU_BACKGROUND_COUNT] = {
	MENU_BACKGROUND_MAIN_HEIGHT, MENU_BACKGROUND_SHOWROOM_HEIGHT, MENU_BACKGROUND_OPPONENT_HEIGHT};

void menu_background_unload(void)
{
	for (legacy_s32 index = 0; index < MENU_BACKGROUND_COUNT; index++) {
		SDL_DestroySurface(images[index]);
		images[index] = NULL;
		attempted[index] = 0;
	}
}

static legacy_u32 menu_background_read_be32(const legacy_u8 *bytes)
{
	return ((legacy_u32)bytes[0] << LEGACY_THREE_BYTE_BITS) |
		   ((legacy_u32)bytes[1] << LEGACY_WORD_BITS) | ((legacy_u32)bytes[2] << LEGACY_BYTE_BITS) |
		   bytes[3];
}

static SDL_Surface *menu_background_load(const legacy_char *directory,
										 enum MENU_BACKGROUND background)
{
	legacy_char path[MENU_BACKGROUND_PATH_SIZE];
	legacy_s32 length = snprintf(path, sizeof(path), "%s%s", directory, filenames[background]);
	if (length < 0 || (size_t)length >= sizeof(path)) {
		return NULL;
	}
	FILE *file = asset_path_open_file(path, "rb");
	if (file == NULL) {
		return NULL;
	}
	/* Reject incorrect or excessive dimensions before the decoder allocates pixels. */
	legacy_u8 header[PNG_DIMENSION_HEADER_SIZE];
	size_t read = fread(header, 1, sizeof(header), file);
	fclose(file);
	static const legacy_u8 signature[] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
	if (read != sizeof(header) || memcmp(header, signature, sizeof(signature)) != 0 ||
		menu_background_read_be32(header + PNG_IHDR_LENGTH_OFFSET) != PNG_IHDR_DATA_LENGTH ||
		memcmp(header + PNG_IHDR_TYPE_OFFSET, "IHDR", PNG_CHUNK_TYPE_SIZE) != 0 ||
		menu_background_read_be32(header + PNG_IHDR_WIDTH_OFFSET) !=
			MENU_BACKGROUND_WIDTH * MENU_BACKGROUND_ASSET_SCALE ||
		menu_background_read_be32(header + PNG_IHDR_HEIGHT_OFFSET) !=
			(legacy_u32)(heights[background] * MENU_BACKGROUND_ASSET_SCALE)) {
		return NULL;
	}
	SDL_Surface *source = SDL_LoadPNG(path);
	if (source == NULL) {
		return NULL;
	}
	SDL_Surface *result = SDL_ConvertSurface(source, SDL_PIXELFORMAT_ARGB8888);
	SDL_DestroySurface(source);
	return result;
}

static SDL_Surface *menu_background_image(enum MENU_BACKGROUND background)
{
	if (attempted[background] != 0) {
		return images[background];
	}
	attempted[background] = 1;
	SDL_Surface *source = menu_background_load(MENU_BACKGROUND_DIRECTORY, background);
	if (source == NULL) {
		const legacy_char *base = asset_path_base();
		legacy_char directory[MENU_BACKGROUND_PATH_SIZE];
		if (base != NULL) {
			legacy_s32 length =
				snprintf(directory, sizeof(directory), "%s%s", base, MENU_BACKGROUND_DIRECTORY);
			if (length >= 0 && (size_t)length < sizeof(directory)) {
				source = menu_background_load(directory, background);
			}
		}
	}
#ifdef RESTUNTS_MENU_DIRECTORY
	if (source == NULL) {
		source = menu_background_load(RESTUNTS_MENU_DIRECTORY "/", background);
	}
#endif
	images[background] = source;
	return source;
}

void menu_background_draw(const struct SPRITE *target, const struct SHAPE2D *original,
						  enum MENU_BACKGROUND background)
{
	if (!hires_enabled() || (legacy_u32)background >= MENU_BACKGROUND_COUNT ||
		shape2d_get_width(original) != MENU_BACKGROUND_WIDTH ||
		shape2d_get_height(original) != heights[background]) {
		return;
	}
	SDL_Surface *source = menu_background_image(background);
	if (source == NULL || !hires_begin_argb(target)) {
		return;
	}
	legacy_s32 x = (legacy_s16)shape2d_get_pos_x(original);
	legacy_s32 y = (legacy_s16)shape2d_get_pos_y(original);
	legacy_s32 left = target->sprite_raster_left > x ? target->sprite_raster_left - x : 0;
	legacy_s32 top = target->sprite_top > y ? target->sprite_top - y : 0;
	legacy_s32 right = target->sprite_raster_right - x;
	legacy_s32 bottom = target->sprite_bottom - y;
	if (right > MENU_BACKGROUND_WIDTH) {
		right = MENU_BACKGROUND_WIDTH;
	}
	if (bottom > heights[background]) {
		bottom = heights[background];
	}
	legacy_s32 scale = hires_render_scale();
	/* Keep the authored 2x pixels crisp at every adaptive render scale. At 1x,
	 * sample the center of each original cell; at 4x, repeat each artwork pixel. */
	legacy_s32 sample_center = MENU_BACKGROUND_ASSET_SCALE / scale / 2;
	for (legacy_s32 row = top * scale; row < bottom * scale; row++) {
		legacy_s32 source_y = row * MENU_BACKGROUND_ASSET_SCALE / scale + sample_center;
		const legacy_u32 *pixels =
			(const legacy_u32 *)((const legacy_u8 *)source->pixels + source_y * source->pitch);
		for (legacy_s32 column = left * scale; column < right * scale; column++) {
			legacy_s32 source_x = column * MENU_BACKGROUND_ASSET_SCALE / scale + sample_center;
			hires_argb_pixel(x * scale + column, y * scale + row, pixels[source_x]);
		}
	}
	hires_end();
}

void menu_background_draw_overlay(const struct SPRITE *target, const struct SHAPE2D *overlay,
								  enum MENU_BACKGROUND background)
{
	if (!hires_enabled() || (legacy_u32)background >= MENU_BACKGROUND_COUNT) {
		return;
	}
	legacy_s32 x = (legacy_s16)shape2d_get_pos_x(overlay);
	legacy_s32 y = (legacy_s16)shape2d_get_pos_y(overlay);
	legacy_s32 width = shape2d_get_width(overlay);
	legacy_s32 height = shape2d_get_height(overlay);
	legacy_s32 left = x > target->sprite_raster_left ? x : target->sprite_raster_left;
	legacy_s32 top = y > target->sprite_top ? y : target->sprite_top;
	legacy_s32 right =
		x + width < target->sprite_raster_right ? x + width : target->sprite_raster_right;
	legacy_s32 bottom = y + height < target->sprite_bottom ? y + height : target->sprite_bottom;
	if (right > MENU_BACKGROUND_WIDTH) {
		right = MENU_BACKGROUND_WIDTH;
	}
	if (bottom > heights[background]) {
		bottom = heights[background];
	}
	if (left >= right || top >= bottom) {
		return;
	}
	SDL_Surface *source = menu_background_image(background);
	if (source == NULL || !hires_begin_argb(target)) {
		return;
	}
	const legacy_u8 *mask = (const legacy_u8 *)overlay + SHAPE2D_HEADER_SIZE;
	legacy_s32 scale = hires_render_scale();
	legacy_s32 sample_center = MENU_BACKGROUND_ASSET_SCALE / scale / 2;
	for (legacy_s32 row = top * scale; row < bottom * scale; row++) {
		const legacy_u8 *mask_row = mask + (row / scale - y) * width;
		legacy_s32 source_y = row * MENU_BACKGROUND_ASSET_SCALE / scale + sample_center;
		const legacy_u32 *pixels =
			(const legacy_u32 *)((const legacy_u8 *)source->pixels + source_y * source->pitch);
		for (legacy_s32 column = left * scale; column < right * scale; column++) {
			if (sprite_palette_map[mask_row[column / scale - x]] == SHAPE2D_TRANSPARENT_COLOR) {
				continue;
			}
			legacy_s32 source_x = column * MENU_BACKGROUND_ASSET_SCALE / scale + sample_center;
			hires_argb_pixel(column, row, pixels[source_x]);
		}
	}
	hires_end();
}
