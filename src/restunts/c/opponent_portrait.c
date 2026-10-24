#include "opponent_portrait.h"
#include "opponent.h"
#include "hires.h"
#include "shape2d.h"
#include "asset_path.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define PORTRAIT_WIDTH 80
#define PORTRAIT_HEIGHT 83
#define PORTRAIT_LEFT 2
#define PORTRAIT_TOP 2
#define PORTRAIT_INNER_WIDTH 74
#define PORTRAIT_INNER_HEIGHT 79
#define PORTRAIT_DIGIT_COLOR 39
#define PORTRAIT_DIGIT_LEFT 66
#define PORTRAIT_DIGIT_RIGHT 71
#define PORTRAIT_DIGIT_TOP 4
#define PORTRAIT_DIGIT_BOTTOM 12
#define PORTRAIT_ORIGINAL_SCALE 2
#define PORTRAIT_PATH_SIZE 1024
#define PORTRAIT_MAX_DIMENSION 4096U
#ifdef __DJGPP__
#define PORTRAIT_DIRECTORY "opponent/"
#else
#define PORTRAIT_DIRECTORY "opponents/"
#endif

#define PNG_DIMENSION_HEADER_SIZE 24
#define PNG_IHDR_LENGTH_OFFSET 8
#define PNG_IHDR_TYPE_OFFSET 12
#define PNG_IHDR_DATA_LENGTH 13
#define PNG_IHDR_WIDTH_OFFSET 16
#define PNG_IHDR_HEIGHT_OFFSET 20
#define PNG_CHUNK_TYPE_SIZE 4

static SDL_Surface *portrait;
static legacy_u8 attempted_opponent;

void opponent_portrait_unload(void)
{
	SDL_DestroySurface(portrait);
	portrait = NULL;
	attempted_opponent = 0;
}

static legacy_u32 portrait_read_be32(const legacy_u8 *bytes)
{
	return ((legacy_u32)bytes[0] << LEGACY_THREE_BYTE_BITS) |
		   ((legacy_u32)bytes[1] << LEGACY_WORD_BITS) | ((legacy_u32)bytes[2] << LEGACY_BYTE_BITS) |
		   bytes[3];
}

static SDL_Surface *portrait_load(const legacy_char *directory, legacy_u8 opponent,
								  legacy_u8 prepared)
{
	legacy_char path[PORTRAIT_PATH_SIZE];
	legacy_s32 length = snprintf(path, sizeof(path), "%s%sopp%u.png", directory,
								 prepared != 0 ? "game/" : "", opponent);
	if (length < 0 || (size_t)length >= sizeof(path)) {
		return NULL;
	}
	/* Check dimensions before decoding optional artwork, so a malformed or
	 * oversized replacement cannot trigger a large image allocation. */
	FILE *file = asset_path_open_file(path, "rb");
	if (file == NULL) {
		return NULL;
	}
	legacy_u8 header[PNG_DIMENSION_HEADER_SIZE];
	size_t read = fread(header, 1, sizeof(header), file);
	fclose(file);
	static const legacy_u8 signature[] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
	if (read != sizeof(header) || memcmp(header, signature, sizeof(signature)) != 0 ||
		portrait_read_be32(header + PNG_IHDR_LENGTH_OFFSET) != PNG_IHDR_DATA_LENGTH ||
		memcmp(header + PNG_IHDR_TYPE_OFFSET, "IHDR", PNG_CHUNK_TYPE_SIZE) != 0) {
		return NULL;
	}
	legacy_u32 width = portrait_read_be32(header + PNG_IHDR_WIDTH_OFFSET);
	legacy_u32 height = portrait_read_be32(header + PNG_IHDR_HEIGHT_OFFSET);
	if (width == 0 || height == 0 || width > PORTRAIT_MAX_DIMENSION ||
		height > PORTRAIT_MAX_DIMENSION) {
		return NULL;
	}
	legacy_u8 interior = width == PORTRAIT_INNER_WIDTH * HIRES_SCALE &&
						 height == PORTRAIT_INNER_HEIGHT * HIRES_SCALE;
	legacy_u8 full_tile = width == PORTRAIT_WIDTH * PORTRAIT_ORIGINAL_SCALE &&
						  height == PORTRAIT_HEIGHT * PORTRAIT_ORIGINAL_SCALE;
	if (prepared != 0 && !interior && !full_tile) {
		return NULL;
	}
	return SDL_LoadPNG(path);
}

static SDL_Surface *portrait_find(legacy_u8 opponent, legacy_u8 prepared)
{
	SDL_Surface *source = portrait_load(PORTRAIT_DIRECTORY, opponent, prepared);
	if (source == NULL) {
		const legacy_char *base = asset_path_base();
		legacy_char directory[PORTRAIT_PATH_SIZE];
		if (base != NULL) {
			legacy_s32 length =
				snprintf(directory, sizeof(directory), "%s%s", base, PORTRAIT_DIRECTORY);
			if (length >= 0 && (size_t)length < sizeof(directory)) {
				source = portrait_load(directory, opponent, prepared);
			}
		}
	}
#ifdef RESTUNTS_OPPONENT_DIRECTORY
	if (source == NULL) {
		source = portrait_load(RESTUNTS_OPPONENT_DIRECTORY "/", opponent, prepared);
	}
#endif
	return source;
}

static SDL_Surface *portrait_convert(SDL_Surface *source)
{
	if (source->w == PORTRAIT_INNER_WIDTH * HIRES_SCALE &&
		source->h == PORTRAIT_INNER_HEIGHT * HIRES_SCALE) {
		/* Preserve the prepared palette colors and pixel blocks exactly. */
		return SDL_ConvertSurface(source, SDL_PIXELFORMAT_ARGB8888);
	}
	SDL_Surface *result =
		SDL_CreateSurface(PORTRAIT_INNER_WIDTH * HIRES_SCALE, PORTRAIT_INNER_HEIGHT * HIRES_SCALE,
						  SDL_PIXELFORMAT_ARGB8888);
	SDL_Rect interior = {PORTRAIT_LEFT * PORTRAIT_ORIGINAL_SCALE,
						 PORTRAIT_TOP * PORTRAIT_ORIGINAL_SCALE,
						 PORTRAIT_INNER_WIDTH * PORTRAIT_ORIGINAL_SCALE,
						 PORTRAIT_INNER_HEIGHT * PORTRAIT_ORIGINAL_SCALE};
	legacy_u8 original_upscale = source->w == PORTRAIT_WIDTH * PORTRAIT_ORIGINAL_SCALE &&
								 source->h == PORTRAIT_HEIGHT * PORTRAIT_ORIGINAL_SCALE;
	/* A complete doubled tile keeps each interior pixel
	 * as a solid 2x2 block; the original frame and number are drawn by the menu. */
	if (result != NULL &&
		(!SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE) ||
		 !SDL_BlitSurfaceScaled(source, original_upscale ? &interior : NULL, result, NULL,
								original_upscale ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR))) {
		SDL_DestroySurface(result);
		return NULL;
	}
	return result;
}

static SDL_Surface *portrait_image(legacy_u8 opponent)
{
	if (attempted_opponent == opponent) {
		return portrait;
	}
	opponent_portrait_unload();
	attempted_opponent = opponent;
	/* Search all game artwork roots before the optional master portraits. */
	for (legacy_s32 prepared = 1; prepared >= 0; prepared--) {
		SDL_Surface *source = portrait_find(opponent, (legacy_u8)prepared);
		if (source != NULL) {
			portrait = portrait_convert(source);
			SDL_DestroySurface(source);
			if (portrait != NULL) {
				return portrait;
			}
		}
	}
	return NULL;
}

void opponent_portrait_draw(const struct SPRITE *target, const struct SHAPE2D *original,
							legacy_u8 opponent)
{
	opponent_portrait_draw_at(target, original, opponent, shape2d_get_pos_x(original),
							  shape2d_get_pos_y(original));
}

void opponent_portrait_draw_at(const struct SPRITE *target, const struct SHAPE2D *original,
							   legacy_u8 opponent, legacy_s16 tile_x, legacy_s16 tile_y)
{
	if (!hires_enabled() || opponent < OPPONENT_FIRST || opponent > OPPONENT_LAST ||
		shape2d_get_width(original) != PORTRAIT_WIDTH ||
		shape2d_get_height(original) != PORTRAIT_HEIGHT) {
		return;
	}
	SDL_Surface *source = portrait_image(opponent);
	if (source == NULL || !hires_begin_argb(target)) {
		return;
	}
	legacy_s32 x = tile_x + PORTRAIT_LEFT;
	legacy_s32 y = tile_y + PORTRAIT_TOP;
	const legacy_u8 *indexed = (const legacy_u8 *)original + SHAPE2D_HEADER_SIZE;
	legacy_s32 scale = hires_render_scale();
	legacy_s32 source_step = HIRES_SCALE / scale;
	legacy_s32 scale_shift = 0;
	for (legacy_s32 samples = scale; samples > 1; samples /= 2) {
		scale_shift++;
	}
	legacy_s32 width = PORTRAIT_INNER_WIDTH * scale;
	legacy_s32 height = PORTRAIT_INNER_HEIGHT * scale;
	for (legacy_s32 row = 0; row < height; row++) {
		const legacy_u32 *pixels =
			(const legacy_u32 *)((const legacy_u8 *)source->pixels +
								 (row * source_step + source_step / 2) * source->pitch);
		for (legacy_s32 column = 0; column < width; column++) {
			legacy_s32 original_x = (column >> scale_shift) + PORTRAIT_LEFT;
			legacy_s32 original_y = (row >> scale_shift) + PORTRAIT_TOP;
			/* Keep the authored red number, including its exact pixel contours.
			 * Its surrounding photograph remains continuous through the mask. */
			if (original_x >= PORTRAIT_DIGIT_LEFT && original_x <= PORTRAIT_DIGIT_RIGHT &&
				original_y >= PORTRAIT_DIGIT_TOP && original_y <= PORTRAIT_DIGIT_BOTTOM &&
				indexed[original_y * PORTRAIT_WIDTH + original_x] == PORTRAIT_DIGIT_COLOR) {
				continue;
			}
			hires_argb_pixel(x * scale + column, y * scale + row,
							 pixels[column * source_step + source_step / 2]);
		}
	}
	hires_end();
}
