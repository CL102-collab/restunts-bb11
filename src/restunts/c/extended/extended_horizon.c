#include "extended_horizon.h"

#ifdef RESTUNTS_SDL3
#include "cstrk.h"
#include "../hires/skybox_hires.h"
#include "../shape2d/shape2d.h"
#include "../asset_path.h"
#include "../../platform/sdl3/sdl3.h"
#include <SDL3/SDL.h>

#define HORIZON_DIRECTORY_NAME "horizons"
#define HORIZON_BUNDLED_ANTARCTICA "Antarctica.png"
#define HORIZON_BUNDLED_ANTARCTICA_DOS "antarct.png"
#define HORIZON_SPRITE_PAGE_SIZE (LEGACY_U16_MAX + 1UL)
#define HORIZON_PNG_SIGNATURE_SIZE 8U
#define HORIZON_PNG_IHDR_SIZE 13U
#define HORIZON_PNG_CHUNK_OVERHEAD 12U
#define HORIZON_PNG_HEADER_SIZE                                                                    \
	(HORIZON_PNG_SIGNATURE_SIZE + HORIZON_PNG_CHUNK_OVERHEAD + HORIZON_PNG_IHDR_SIZE)
#define HORIZON_PNG_CRC_POLYNOMIAL 0xedb88320UL
#define HORIZON_PNG_RGB 2U
#define HORIZON_PNG_INDEXED 3U
#define HORIZON_PNG_BIT_DEPTH 8U
#define HORIZON_RGB_CHANNELS 3U
#define HORIZON_RGBA_CHANNELS 4U
#define HORIZON_PNG_PALETTE_COLORS 256U
#define HORIZON_PNG_RGB_TRANSPARENCY_SIZE 6U
#define HORIZON_ALPHA_CHANNEL 3U
#define HORIZON_PNG_WIDTH_OFFSET 16U
#define HORIZON_PNG_HEIGHT_OFFSET 20U
#define HORIZON_PNG_DEPTH_OFFSET 24U
#define HORIZON_PNG_COLOR_OFFSET 25U
#define HORIZON_PNG_COMPRESSION_OFFSET 26U
#define HORIZON_PNG_FILTER_OFFSET 27U
#define HORIZON_PNG_INTERLACE_OFFSET 28U

static SDL_Surface *horizon_panorama;
static SDL_Surface *horizon_source;
static SDL_Surface *horizon_images[SKYBOX_IMAGE_COUNT];
static struct SHAPE2D *horizon_shapes[SKYBOX_IMAGE_COUNT];
static legacy_u8 *horizon_classic_pixels;
static legacy_s8 horizon_name[CSTRK_MAX_HORIZON_NAME + 1U];

static void horizon_service_audio(void)
{
	sdl3_timer_pump();
#ifdef __EMSCRIPTEN__
	sdl3_browser_yield_if_due();
#endif
}

void extended_horizon_unload(void)
{
	/* The banked renderer retains borrowed strip pointers until invalidated. */
	skybox_hires_unload();
	for (legacy_u16 image = 0; image < SKYBOX_IMAGE_COUNT; image++) {
		SDL_DestroySurface(horizon_images[image]);
		horizon_images[image] = NULL;
		horizon_shapes[image] = NULL;
	}
	SDL_aligned_free(horizon_classic_pixels);
	horizon_classic_pixels = NULL;
	SDL_DestroySurface(horizon_panorama);
	horizon_panorama = NULL;
	SDL_DestroySurface(horizon_source);
	horizon_source = NULL;
	horizon_name[0] = 0;
}

legacy_s16 extended_horizon_active(void)
{
	return horizon_panorama != NULL;
}

legacy_s16 extended_horizon_matches(const legacy_s8 *name)
{
	return extended_horizon_active() && name &&
		   SDL_strcmp((const legacy_char *)name, (const legacy_char *)horizon_name) == 0;
}

SDL_Surface *extended_horizon_image(legacy_s16 image)
{
	return image >= 0 && image < SKYBOX_IMAGE_COUNT ? horizon_images[image] : NULL;
}

struct SHAPE2D *extended_horizon_shape(legacy_s16 image)
{
	return image >= 0 && image < SKYBOX_IMAGE_COUNT ? horizon_shapes[image] : NULL;
}

static legacy_u8 horizon_ascii_fold(legacy_u8 value)
{
	return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

static legacy_s16 horizon_name_equal(const legacy_char *first, const legacy_char *second)
{
	while (*first && *second &&
		   horizon_ascii_fold((legacy_u8)*first) == horizon_ascii_fold((legacy_u8)*second)) {
		first++;
		second++;
	}
	return *first == *second;
}

struct HORIZON_NAME_LOOKUP {
	const legacy_char *wanted;
	legacy_char found[CSTRK_MAX_HORIZON_NAME + 1U];
	legacy_u32 matches;
	SDL_PathType type;
};

static SDL_EnumerationResult SDLCALL horizon_find_name(void *userdata, const legacy_char *directory,
													   const legacy_char *filename)
{
	struct HORIZON_NAME_LOOKUP *lookup = userdata;
	size_t length = SDL_strlen(filename);
	if (length <= CSTRK_MAX_HORIZON_NAME && horizon_name_equal(filename, lookup->wanted)) {
		legacy_char *path = NULL;
		if (SDL_asprintf(&path, "%s%s", directory, filename) < 0) {
			return SDL_ENUM_FAILURE;
		}
		SDL_PathInfo info;
		legacy_s16 matches = SDL_GetPathInfo(path, &info) && info.type == lookup->type;
		SDL_free(path);
		if (matches) {
			lookup->matches++;
			SDL_strlcpy(lookup->found, filename, sizeof(lookup->found));
		}
	}
	return SDL_ENUM_CONTINUE;
}

static legacy_u32 horizon_read_be32(const legacy_u8 *bytes)
{
	return (legacy_u32)bytes[0] << 24U | (legacy_u32)bytes[1] << 16U | (legacy_u32)bytes[2] << 8U |
		   bytes[3];
}

static legacy_u32 horizon_crc(const legacy_u8 *bytes, size_t size)
{
	legacy_u32 value = LEGACY_U32_MAX;
	for (size_t index = 0; index < size; index++) {
		value ^= bytes[index];
		for (legacy_u16 bit = 0; bit < HORIZON_PNG_BIT_DEPTH; bit++) {
			value = (value >> 1U) ^ ((value & 1U) ? HORIZON_PNG_CRC_POLYNOMIAL : 0U);
		}
	}
	return value ^ LEGACY_U32_MAX;
}

static legacy_s16 horizon_png_valid(const legacy_u8 *bytes, size_t size)
{
	static const legacy_u8 signature[HORIZON_PNG_SIGNATURE_SIZE] = {137U, 'P', 'N', 'G',
																	13U,  10U, 26U, 10U};
	if (size < HORIZON_PNG_HEADER_SIZE || SDL_memcmp(bytes, signature, sizeof(signature)) != 0 ||
		horizon_read_be32(bytes + HORIZON_PNG_SIGNATURE_SIZE) != HORIZON_PNG_IHDR_SIZE ||
		SDL_memcmp(bytes + HORIZON_PNG_SIGNATURE_SIZE + sizeof(legacy_u32), "IHDR",
				   sizeof(legacy_u32)) != 0 ||
		horizon_read_be32(bytes + HORIZON_PNG_WIDTH_OFFSET) != EXTENDED_HORIZON_WIDTH ||
		horizon_read_be32(bytes + HORIZON_PNG_HEIGHT_OFFSET) < EXTENDED_HORIZON_MIN_HEIGHT ||
		horizon_read_be32(bytes + HORIZON_PNG_HEIGHT_OFFSET) > EXTENDED_HORIZON_MAX_HEIGHT ||
		bytes[HORIZON_PNG_DEPTH_OFFSET] != HORIZON_PNG_BIT_DEPTH ||
		(bytes[HORIZON_PNG_COLOR_OFFSET] != HORIZON_PNG_RGB &&
		 bytes[HORIZON_PNG_COLOR_OFFSET] != HORIZON_PNG_INDEXED) ||
		bytes[HORIZON_PNG_COMPRESSION_OFFSET] != 0 || bytes[HORIZON_PNG_FILTER_OFFSET] != 0 ||
		bytes[HORIZON_PNG_INTERLACE_OFFSET] != 0) {
		return 0;
	}
	legacy_s16 has_image_data = 0, ended_image_data = 0, has_palette = 0, has_transparency = 0;
	legacy_u32 palette_size = 0;
	size_t offset = HORIZON_PNG_SIGNATURE_SIZE;
	while (offset <= size && size - offset >= HORIZON_PNG_CHUNK_OVERHEAD) {
		legacy_u32 length = horizon_read_be32(bytes + offset);
		if (length > size - offset - HORIZON_PNG_CHUNK_OVERHEAD) {
			return 0;
		}
		const legacy_u8 *chunk = bytes + offset + sizeof(legacy_u32);
		for (legacy_u16 index = 0; index < sizeof(legacy_u32); index++) {
			if ((chunk[index] < 'A' || chunk[index] > 'Z') &&
				(chunk[index] < 'a' || chunk[index] > 'z')) {
				return 0;
			}
		}
		if (horizon_crc(chunk, length + sizeof(legacy_u32)) !=
			horizon_read_be32(chunk + sizeof(legacy_u32) + length)) {
			return 0;
		}
		offset += length + HORIZON_PNG_CHUNK_OVERHEAD;
		if (SDL_memcmp(chunk, "IHDR", sizeof(legacy_u32)) == 0) {
			if (offset != HORIZON_PNG_HEADER_SIZE) {
				return 0;
			}
		} else if (SDL_memcmp(chunk, "PLTE", sizeof(legacy_u32)) == 0) {
			if (has_image_data || has_palette || length == 0 || length % HORIZON_RGB_CHANNELS ||
				length > HORIZON_PNG_PALETTE_COLORS * HORIZON_RGB_CHANNELS) {
				return 0;
			}
			has_palette = 1;
			palette_size = length / HORIZON_RGB_CHANNELS;
		} else if (SDL_memcmp(chunk, "tRNS", sizeof(legacy_u32)) == 0) {
			if (has_image_data || has_transparency ||
				(bytes[HORIZON_PNG_COLOR_OFFSET] == HORIZON_PNG_RGB &&
				 length != HORIZON_PNG_RGB_TRANSPARENCY_SIZE) ||
				(bytes[HORIZON_PNG_COLOR_OFFSET] == HORIZON_PNG_INDEXED &&
				 (!has_palette || !length || length > palette_size))) {
				return 0;
			}
			has_transparency = 1;
		} else if (SDL_memcmp(chunk, "IDAT", sizeof(legacy_u32)) == 0) {
			if (ended_image_data ||
				(bytes[HORIZON_PNG_COLOR_OFFSET] == HORIZON_PNG_INDEXED && !has_palette)) {
				return 0;
			}
			has_image_data = 1;
		} else if (SDL_memcmp(chunk, "IEND", sizeof(legacy_u32)) == 0) {
			return has_image_data && length == 0 && offset == size;
		} else if (chunk[0] >= 'A' && chunk[0] <= 'Z') {
			return 0;
		}
		if (has_image_data && SDL_memcmp(chunk, "IDAT", sizeof(legacy_u32)) != 0) {
			ended_image_data = 1;
		}
		horizon_service_audio();
	}
	return 0;
}

/* Return -1 for an unreadable or ambiguous lookup, zero for missing, and one
 * for a unique match. An invalid user override must not expose a bundled image. */
static legacy_s16 horizon_find_path(const legacy_char *root, const legacy_char *name,
									SDL_PathType type, legacy_char **path)
{
	struct HORIZON_NAME_LOOKUP lookup;
	lookup.wanted = name;
	lookup.found[0] = 0;
	lookup.matches = 0;
	lookup.type = type;
	if (!SDL_EnumerateDirectory(root, horizon_find_name, &lookup) || lookup.matches > 1U) {
		return -1;
	}
	if (!lookup.matches) {
		return 0;
	}
	if (SDL_asprintf(path, "%s%s%s", root, lookup.found,
					 type == SDL_PATHTYPE_DIRECTORY ? "/" : "") < 0) {
		return -1;
	}
	return 1;
}

static legacy_u8 *horizon_read_directory(const legacy_char *directory, const legacy_s8 *name,
										 legacy_s16 bundled, size_t *size, legacy_s16 *found)
{
	legacy_char *path = NULL;
	*found = horizon_find_path(directory, (const legacy_char *)name, SDL_PATHTYPE_FILE, &path);
	/* DOS32 deliverables use an 8.3 alias, while tracks retain their full name.
	 * Aliases apply only in bundled locations, after the canonical filename. */
	if (!*found && bundled &&
		horizon_name_equal((const legacy_char *)name, HORIZON_BUNDLED_ANTARCTICA)) {
		*found =
			horizon_find_path(directory, HORIZON_BUNDLED_ANTARCTICA_DOS, SDL_PATHTYPE_FILE, &path);
	}
	legacy_u8 *bytes = *found == 1 ? SDL_LoadFile(path, size) : NULL;
	SDL_free(path);
	return bytes;
}

static legacy_u8 *horizon_read_root(const legacy_char *root, const legacy_s8 *name,
									legacy_s16 bundled, size_t *size, legacy_s16 *found)
{
	legacy_char *directory = NULL;
	*found = horizon_find_path(root, HORIZON_DIRECTORY_NAME, SDL_PATHTYPE_DIRECTORY, &directory);
	legacy_u8 *bytes =
		*found == 1 ? horizon_read_directory(directory, name, bundled, size, found) : NULL;
	SDL_free(directory);
	return bytes;
}

static legacy_u8 *horizon_read_file(const legacy_s8 *name, size_t *size)
{
	/* Anchor mobile filesystem operations to the selected game-data CWD. */
	legacy_char *root = SDL_GetCurrentDirectory();
	if (!root) {
		return NULL;
	}
	legacy_s16 found = 0;
	legacy_u8 *bytes = horizon_read_root(root, name, 0, size, &found);
	SDL_free(root);
	if (found) {
		return bytes;
	}
	const legacy_char *base = asset_path_base();
	if (base) {
		bytes = horizon_read_root(base, name, 1, size, &found);
		if (found) {
			return bytes;
		}
	}
#ifdef RESTUNTS_HORIZON_DIRECTORY
	bytes = horizon_read_directory(RESTUNTS_HORIZON_DIRECTORY "/", name, 1, size, &found);
#endif
	return bytes;
}

static legacy_s16 horizon_opaque(SDL_Surface *source)
{
	/* SDL surface conversion turns both PNG tRNS color keys and palette alpha
	 * into pixel alpha. Unused transparent palette entries remain admissible. */
	SDL_Surface *rgba = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
	if (!rgba) {
		return 0;
	}
	legacy_s16 opaque = 1;
	for (legacy_s32 row = 0; opaque && row < rgba->h; row++) {
		const legacy_u8 *pixels = (const legacy_u8 *)rgba->pixels + row * rgba->pitch;
		for (legacy_s32 column = 0; column < rgba->w; column++) {
			if (pixels[column * HORIZON_RGBA_CHANNELS + HORIZON_ALPHA_CHANNEL] !=
				SDL_ALPHA_OPAQUE) {
				opaque = 0;
				break;
			}
		}
		horizon_service_audio();
	}
	SDL_DestroySurface(rgba);
	return opaque;
}

static void horizon_fill_classic(void)
{
	for (legacy_u16 image = 0; image < SKYBOX_IMAGE_COUNT; image++) {
		struct SHAPE2D *shape = horizon_shapes[image];
		legacy_u16 width = LEGACY_READ_U16_LE((const legacy_u8 *)&shape->width);
		legacy_u16 height = LEGACY_READ_U16_LE((const legacy_u8 *)&shape->height);
		legacy_u8 *pixels = (legacy_u8 *)shape + SHAPE2D_HEADER_SIZE;
		const SDL_Surface *strip = horizon_images[image];
		for (legacy_s32 row = 0; row < height; row++) {
			const legacy_u8 *source =
				(const legacy_u8 *)strip->pixels +
				(row * EXTENDED_HORIZON_SAMPLE_SCALE + EXTENDED_HORIZON_SAMPLE_SCALE / 2) *
					strip->pitch;
			for (legacy_s32 column = 0; column < width; column++) {
				pixels[row * width + column] = source[column * EXTENDED_HORIZON_SAMPLE_SCALE +
													  EXTENDED_HORIZON_SAMPLE_SCALE / 2];
			}
		}
	}
}

/* Palette recapture is rare (the production call is at startup), but retaining
 * the opaque RGB source also makes it safe while a preview owns these sprites.
 * Replace pixel contents in place, preserving every global resource pointer. */
void extended_horizon_palette_changed(void)
{
	if (!horizon_source || !extended_horizon_active()) {
		return;
	}
	SDL_Surface *converted = skybox_hires_convert_surface(horizon_source);
	if (!converted) {
		return;
	}
	SDL_Palette *palette = SDL_GetSurfacePalette(converted);
	legacy_s16 ready = SDL_SetSurfacePalette(horizon_panorama, palette);
	for (legacy_u16 image = 0; ready && image < SKYBOX_IMAGE_COUNT; image++) {
		ready = SDL_SetSurfacePalette(horizon_images[image], palette);
	}
	if (ready) {
		for (legacy_s32 row = 0; row < horizon_panorama->h; row++) {
			SDL_memcpy((legacy_u8 *)horizon_panorama->pixels + row * horizon_panorama->pitch,
					   (const legacy_u8 *)converted->pixels + row * converted->pitch,
					   EXTENDED_HORIZON_WIDTH);
		}
		horizon_fill_classic();
	}
	SDL_DestroySurface(converted);
}

static legacy_s16 horizon_make_strips(void)
{
	static const legacy_s32 widths[SKYBOX_IMAGE_COUNT] = {
		SKYBOX_IMAGE_WIDTH, SKYBOX_IMAGE_HALF_WRAP - SKYBOX_IMAGE_WIDTH, SKYBOX_IMAGE_WIDTH,
		SKYBOX_IMAGE_FULL_WRAP - SKYBOX_IMAGE_ONE_AND_HALF_WIDTH};
	legacy_s32 height = horizon_panorama->h / EXTENDED_HORIZON_SAMPLE_SCALE;
	size_t size =
		(size_t)SKYBOX_IMAGE_FULL_WRAP * height + SKYBOX_IMAGE_COUNT * SHAPE2D_HEADER_SIZE;
	/* Legacy blitters address native resources through one external 64KiB
	 * segment. Keep the complete classic set in one aligned page so their
	 * 16-bit source offsets can never wrap into unrelated heap memory. */
	if (size > HORIZON_SPRITE_PAGE_SIZE) {
		return 0;
	}
	horizon_classic_pixels = SDL_aligned_alloc(HORIZON_SPRITE_PAGE_SIZE, size);
	if (!horizon_classic_pixels) {
		return 0;
	}
	SDL_memset(horizon_classic_pixels, 0, size);
	legacy_s32 offset = 0;
	size_t shape_offset = 0;
	for (legacy_u16 image = 0; image < SKYBOX_IMAGE_COUNT; image++) {
		legacy_s32 width = widths[image];
		horizon_images[image] = SDL_CreateSurfaceFrom(
			width * EXTENDED_HORIZON_SAMPLE_SCALE, horizon_panorama->h, SDL_PIXELFORMAT_INDEX8,
			(legacy_u8 *)horizon_panorama->pixels + offset, horizon_panorama->pitch);
		if (!horizon_images[image] ||
			!SDL_SetSurfacePalette(horizon_images[image],
								   SDL_GetSurfacePalette(horizon_panorama))) {
			return 0;
		}
		struct SHAPE2D *shape = (struct SHAPE2D *)(horizon_classic_pixels + shape_offset);
		shape_offset += SHAPE2D_HEADER_SIZE + (size_t)width * height;
		horizon_shapes[image] = shape;
		LEGACY_WRITE_U16_LE((legacy_u8 *)&shape->width, (legacy_u16)width);
		LEGACY_WRITE_U16_LE((legacy_u8 *)&shape->height, (legacy_u16)height);

		offset += width * EXTENDED_HORIZON_SAMPLE_SCALE;
	}
	horizon_fill_classic();
	return 1;
}

legacy_s16 extended_horizon_load(const legacy_s8 *name)
{
	extended_horizon_unload();
	size_t length = name ? SDL_strnlen((const legacy_char *)name, CSTRK_MAX_HORIZON_NAME + 1U) : 0U;
	if (!name || !length || !cstrk_custom_horizon_name_valid(name, length)) {
		return 0;
	}
	size_t size = 0U;
	legacy_u8 *bytes = horizon_read_file(name, &size);
	if (!bytes) {
		return 0;
	}
	SDL_Surface *source = NULL;
	if (horizon_png_valid(bytes, size)) {
		SDL_IOStream *stream = SDL_IOFromConstMem(bytes, size);
		if (stream) {
			source = SDL_LoadPNG_IO(stream, true);
		}
	}
	SDL_free(bytes);
	if (!source || source->w != EXTENDED_HORIZON_WIDTH || source->h < EXTENDED_HORIZON_MIN_HEIGHT ||
		source->h > EXTENDED_HORIZON_MAX_HEIGHT || !horizon_opaque(source)) {
		SDL_DestroySurface(source);
		return 0;
	}
	legacy_s32 height = (source->h + EXTENDED_HORIZON_SAMPLE_SCALE - 1) /
						EXTENDED_HORIZON_SAMPLE_SCALE * EXTENDED_HORIZON_SAMPLE_SCALE;
	SDL_Surface *padded = SDL_CreateSurface(EXTENDED_HORIZON_WIDTH, height, SDL_PIXELFORMAT_RGB24);
	SDL_Rect destination = {0, height - source->h, source->w, source->h};
	legacy_s16 copied = padded &&
						SDL_FillSurfaceRect(padded, NULL,
											SDL_MapSurfaceRGB(padded, EXTENDED_HORIZON_SKY_RED,
															  EXTENDED_HORIZON_SKY_GREEN,
															  EXTENDED_HORIZON_SKY_BLUE)) &&
						SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE) &&
						SDL_BlitSurface(source, NULL, padded, &destination);
	SDL_DestroySurface(source);
	if (copied) {
		horizon_panorama = skybox_hires_convert_surface(padded);
	}
	horizon_source = padded;
	if (!horizon_panorama || !horizon_make_strips()) {
		extended_horizon_unload();
		return 0;
	}
	SDL_strlcpy((legacy_char *)horizon_name, (const legacy_char *)name, sizeof(horizon_name));
	return 1;
}
#endif
