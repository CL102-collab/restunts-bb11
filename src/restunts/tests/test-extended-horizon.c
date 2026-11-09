#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "../c/legacy.h"
#include "../c/asset_path.h"
#include "../c/platform.h"
#include "../c/shape2d/shape2d.h"
#include "../c/skybox.h"
#include "../c/hires/hires.h"
#include "../c/hires/skybox_hires.h"
#include "../c/video/video_framebuffer.h"
#include "../c/extended/extended_horizon.h"
#include "../c/extended/extended_track.h"

#define TEST_BYTE_BITS 8U
#define TEST_PALETTE_COLORS 256U
#define TEST_PALETTE_CHANNELS 3U
#define TEST_VGA_CHANNEL_MAX 63U
#define TEST_SHADE_FIRST 17U
#define TEST_SHADE_COUNT 63U
#define TEST_SKY_COLOR 116U
#define TEST_GROUND_COLOR 108U
#define TEST_WATER_COLOR 31U
#define TEST_MATERIAL_COUNT 129U
#define TEST_GROUND_MATERIAL 16U
#define TEST_SKY_MATERIAL 17U
#define TEST_WATER_MATERIAL 100U
#define TEST_FALLBACK 2
#define TEST_SCREEN_WIDTH 320
#define TEST_SCREEN_HEIGHT 200
#define TEST_HORIZON_ROW 100
#define TEST_HALF_SCREEN_WIDTH (TEST_SCREEN_WIDTH / 2)
#define TEST_PNG_RGB 2U
#define TEST_PNG_INDEXED 3U
#define TEST_PNG_RGBA 6U
#define TEST_PNG_GRAYSCALE 0U
#define TEST_PNG_SIGNATURE_SIZE 8U
#define TEST_PNG_HEADER_SIZE 13U
#define TEST_PNG_CRC_POLYNOMIAL 0xedb88320UL
#define TEST_PNG_ADLER_MODULUS 65521UL
#define TEST_PNG_BLOCK_CAPACITY 65535U
#define TEST_PNG_DEPTH_OFFSET 24U
#define TEST_PNG_INTERLACE_OFFSET 28U
#define TEST_PNG_HEADER_CRC_OFFSET 29U
#define TEST_PNG_CHUNK_OVERHEAD 12U
#define TEST_PATTERN_RUN_WIDTH 7
#define TEST_PATTERN_ROW_STEP 11
#define TEST_PATTERN_SEED_STEP 13
#define TEST_TRANSPARENT_INDEX 1U
#define TEST_FIRST_SEED 1U
#define TEST_SECOND_SEED 2U
#define TEST_CUSTOM_NAME "Panorama.PNG"
#define TEST_SECOND_NAME "Second.png"
#define TEST_UTF8_NAME "árvíz.png"
#define TEST_MISSING_NAME "Missing.png"
#define TEST_FIXTURE_NAME "Fixture.png"
#define TEST_ROOT "horizon-fixtures"
#define TEST_BUNDLE_ROOT "bundle"
#define TEST_BUNDLED_NAME "Antarctica.png"
#define TEST_BUNDLED_ALIAS "antarct.png"
#define TEST_PATH_PREFIX "horizons/"
#define TEST_PATH_CAPACITY (sizeof(TEST_PATH_PREFIX) + CSTRK_MAX_HORIZON_NAME)
#define TEST_HASH_OFFSET 2166136261UL
#define TEST_HASH_PRIME 16777619UL
#define TEST_CLASSIC_PAGE_BYTES (LEGACY_U16_MAX + 1UL)
#define TEST_ANTARCTICA_PANEL_COUNT 4U
#define TEST_ANTARCTICA_JOIN_COLUMNS 8

struct SKYBOX skybox;
legacy_s8 skybox_resources_loaded;
legacy_s8 *skybox_res_ofs;
legacy_s8 loaded_skybox_index;
struct SHAPE2D *skyboxes[SKYBOX_IMAGE_COUNT];
legacy_u16 projection_center_x = TEST_HALF_SCREEN_WIDTH;
legacy_u16 projection_center_y = TEST_HORIZON_ROW;
legacy_u16 projection_focal_length_x = TEST_HALF_SCREEN_WIDTH;
legacy_u16 projection_focal_length_y = TEST_HALF_SCREEN_WIDTH;
static legacy_s16 materials[TEST_MATERIAL_COUNT];
legacy_s16 *material_clrlist_ptr = materials;
static struct CSTRK_TRACK test_track;
static legacy_s16 test_track_active = 1;
static legacy_char *bundled_base;
static legacy_u32 builtin_loads, builtin_frees, timer_pumps;
static legacy_s8 builtin_resource;
static struct SHAPE2D builtin_shapes[SKYBOX_IMAGE_COUNT];
static struct SPRITE target;
static legacy_u8 target_rows[TEST_SCREEN_HEIGHT * sizeof(legacy_u16)];
static legacy_u8 *screen;
static legacy_u8 vga_palette[TEST_PALETTE_COLORS * TEST_PALETTE_CHANNELS];
static const legacy_s32 strip_widths[SKYBOX_IMAGE_COUNT] = {320, 192, 320, 192};

const legacy_char *asset_path_base(void)
{
	return bundled_base;
}

legacy_int extended_track_active(void)
{
	return test_track_active;
}
const struct CSTRK_TRACK *extended_track_model(void)
{
	return &test_track;
}
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
legacy_u16 shape2d_get_height(const struct SHAPE2D *shape)
{
	return shape->height;
}
void *file_load_shape2d_fatal(const legacy_s8 *name)
{
	assert(name && name[0]);
	builtin_loads++;
	return &builtin_resource;
}
void locate_many_resources(legacy_s8 *data, const legacy_s8 *names, legacy_s8 **result)
{
	assert(data == &builtin_resource);
	assert(strcmp((const legacy_char *)names, "scensce2sce3sce4") == 0);
	for (legacy_u16 index = 0; index < SKYBOX_IMAGE_COUNT; index++) {
		result[index] = (legacy_s8 *)&builtin_shapes[index];
	}
}
void *mmgr_free(legacy_s8 *pointer)
{
	/* SDL-owned custom buffers must never enter the DOS resource arena. */
	assert(pointer == &builtin_resource);
	builtin_frees++;
	return NULL;
}

static void write_be32(legacy_u8 *bytes, legacy_u32 value)
{
	bytes[0] = (legacy_u8)(value >> 24U);
	bytes[1] = (legacy_u8)(value >> 16U);
	bytes[2] = (legacy_u8)(value >> 8U);
	bytes[3] = (legacy_u8)value;
}

static legacy_u32 png_crc(const legacy_u8 *bytes, size_t size)
{
	legacy_u32 value = LEGACY_U32_MAX;
	for (size_t index = 0; index < size; index++) {
		value ^= bytes[index];
		for (legacy_u16 bit = 0; bit < TEST_BYTE_BITS; bit++) {
			value = (value >> 1U) ^ ((value & 1U) ? TEST_PNG_CRC_POLYNOMIAL : 0U);
		}
	}
	return value ^ LEGACY_U32_MAX;
}

static void write_chunk(FILE *file, const legacy_char type[4], const legacy_u8 *bytes, size_t size)
{
	legacy_u8 header[8];
	write_be32(header, (legacy_u32)size);
	memcpy(header + sizeof(legacy_u32), type, sizeof(legacy_u32));
	assert(fwrite(header, 1U, sizeof(header), file) == sizeof(header));
	if (size) {
		assert(fwrite(bytes, 1U, size, file) == size);
	}
	legacy_u8 *crc_bytes = SDL_malloc(sizeof(legacy_u32) + size);
	assert(crc_bytes);
	memcpy(crc_bytes, type, sizeof(legacy_u32));
	if (size) {
		memcpy(crc_bytes + sizeof(legacy_u32), bytes, size);
	}
	legacy_u8 crc[sizeof(legacy_u32)];
	write_be32(crc, png_crc(crc_bytes, size + sizeof(legacy_u32)));
	SDL_free(crc_bytes);
	assert(fwrite(crc, 1U, sizeof(crc), file) == sizeof(crc));
}

static legacy_u8 fixture_index(legacy_s32 x, legacy_s32 y, legacy_u16 seed)
{
	return (legacy_u8)(TEST_SHADE_FIRST + (x / TEST_PATTERN_RUN_WIDTH + y * TEST_PATTERN_ROW_STEP +
										   seed * TEST_PATTERN_SEED_STEP) %
											  TEST_SHADE_COUNT);
}

static void fixture_path(legacy_char path[TEST_PATH_CAPACITY], const legacy_char *name)
{
	legacy_s32 length = SDL_snprintf(path, TEST_PATH_CAPACITY, TEST_PATH_PREFIX "%s", name);
	assert(length >= 0 && (size_t)length < TEST_PATH_CAPACITY);
}

/* A tiny uncompressed DEFLATE writer keeps RGB fixtures RGB: SDL_SavePNG
 * intentionally converts non-indexed images to four-channel RGBA PNGs. */
static void write_fixture(const legacy_char *name, legacy_s32 width, legacy_s32 height,
						  legacy_u8 color_type, legacy_u16 seed, legacy_s16 transparency)
{
	legacy_u16 channels = color_type == TEST_PNG_RGB ? 3U : color_type == TEST_PNG_RGBA ? 4U : 1U;
	size_t row_bytes = 1U + (size_t)width * channels;
	size_t raw_size = row_bytes * height;
	legacy_u8 *raw = SDL_malloc(raw_size);
	assert(raw);
	for (legacy_s32 row = 0; row < height; row++) {
		legacy_u8 *pixels = raw + (size_t)row * row_bytes;
		*pixels++ = 0;
		for (legacy_s32 column = 0; column < width; column++) {
			legacy_u8 index = fixture_index(column, row, seed);
			if (transparency > 1 && !column && !row) {
				index = TEST_TRANSPARENT_INDEX;
			}
			if (channels == 1U) {
				*pixels++ = index;
			} else {
				for (legacy_u16 channel = 0; channel < TEST_PALETTE_CHANNELS; channel++) {
					*pixels++ = vga_palette[index * TEST_PALETTE_CHANNELS + channel] *
								SDL_ALPHA_OPAQUE / TEST_VGA_CHANNEL_MAX;
				}
				if (channels == 4U) {
					*pixels++ = SDL_ALPHA_OPAQUE;
				}
			}
		}
	}
	size_t blocks = (raw_size + TEST_PNG_BLOCK_CAPACITY - 1U) / TEST_PNG_BLOCK_CAPACITY;
	size_t deflate_size = 2U + blocks * 5U + raw_size + sizeof(legacy_u32);
	legacy_u8 *deflate = SDL_malloc(deflate_size);
	assert(deflate);
	legacy_u8 *destination = deflate;
	*destination++ = 0x78U;
	*destination++ = 0x01U;
	size_t offset = 0;
	while (offset < raw_size) {
		legacy_u16 count = (legacy_u16)SDL_min(raw_size - offset, TEST_PNG_BLOCK_CAPACITY);
		*destination++ = offset + count == raw_size ? 1U : 0U;
		*destination++ = (legacy_u8)count;
		*destination++ = (legacy_u8)(count >> TEST_BYTE_BITS);
		legacy_u16 inverse = (legacy_u16)~count;
		*destination++ = (legacy_u8)inverse;
		*destination++ = (legacy_u8)(inverse >> TEST_BYTE_BITS);
		memcpy(destination, raw + offset, count);
		destination += count;
		offset += count;
	}
	legacy_u32 first = 1U, second = 0U;
	for (size_t index = 0; index < raw_size; index++) {
		first = (first + raw[index]) % TEST_PNG_ADLER_MODULUS;
		second = (second + first) % TEST_PNG_ADLER_MODULUS;
	}
	write_be32(destination, second << 16U | first);
	destination += sizeof(legacy_u32);
	assert((size_t)(destination - deflate) == deflate_size);
	legacy_char path[TEST_PATH_CAPACITY];
	fixture_path(path, name);
	FILE *file = fopen(path, "wb");
	assert(file);
	static const legacy_u8 signature[TEST_PNG_SIGNATURE_SIZE] = {137U, 'P', 'N', 'G',
																 13U,  10U, 26U, 10U};
	assert(fwrite(signature, 1U, sizeof(signature), file) == sizeof(signature));
	legacy_u8 header[TEST_PNG_HEADER_SIZE] = {0};
	write_be32(header, (legacy_u32)width);
	write_be32(header + sizeof(legacy_u32), (legacy_u32)height);
	header[8] = TEST_BYTE_BITS;
	header[9] = color_type;
	write_chunk(file, "IHDR", header, sizeof(header));
	if (color_type == TEST_PNG_INDEXED) {
		legacy_u8 palette[sizeof(vga_palette)];
		for (size_t index = 0; index < sizeof(palette); index++) {
			palette[index] = vga_palette[index] * SDL_ALPHA_OPAQUE / TEST_VGA_CHANNEL_MAX;
		}
		write_chunk(file, "PLTE", palette, sizeof(palette));
		if (transparency) {
			legacy_u8 alpha[TEST_PALETTE_COLORS];
			memset(alpha, SDL_ALPHA_OPAQUE, sizeof(alpha));
			alpha[TEST_TRANSPARENT_INDEX] = SDL_ALPHA_TRANSPARENT;
			write_chunk(file, "tRNS", alpha, sizeof(alpha));
		}
	} else if (color_type == TEST_PNG_RGB && transparency) {
		const legacy_u8 key[TEST_PALETTE_CHANNELS * sizeof(legacy_u16)] = {0};
		write_chunk(file, "tRNS", key, sizeof(key));
	}
	write_chunk(file, "IDAT", deflate, deflate_size);
	write_chunk(file, "IEND", NULL, 0U);
	assert(fclose(file) == 0);
	SDL_free(deflate);
	SDL_free(raw);
}

static void assert_unloaded(void)
{
	assert(!extended_horizon_active());
	for (legacy_s16 image = 0; image < SKYBOX_IMAGE_COUNT; image++) {
		assert(!extended_horizon_image(image));
		assert(!extended_horizon_shape(image));
	}
}

static legacy_u8 expected_index(legacy_s32 x, legacy_s32 padded_y, legacy_s32 height,
								legacy_u16 seed)
{
	legacy_s32 padded_height = (height + EXTENDED_HORIZON_SAMPLE_SCALE - 1) /
							   EXTENDED_HORIZON_SAMPLE_SCALE * EXTENDED_HORIZON_SAMPLE_SCALE;
	legacy_s32 row = padded_y - (padded_height - height);
	return row < 0 ? TEST_SKY_COLOR : fixture_index(x, row, seed);
}

static void assert_panorama(legacy_s32 height, legacy_u16 seed)
{
	legacy_s32 padded = (height + EXTENDED_HORIZON_SAMPLE_SCALE - 1) /
						EXTENDED_HORIZON_SAMPLE_SCALE * EXTENDED_HORIZON_SAMPLE_SCALE;
	legacy_s32 offset = 0;
	legacy_u16 classic_page = dos_memory_pointer_segment(extended_horizon_shape(0));
	for (legacy_s16 image = 0; image < SKYBOX_IMAGE_COUNT; image++) {
		SDL_Surface *source = extended_horizon_image(image);
		struct SHAPE2D *shape = extended_horizon_shape(image);
		assert(source && shape);
		assert(source->w == strip_widths[image] * EXTENDED_HORIZON_SAMPLE_SCALE);
		assert(source->h == padded);
		assert(shape->width == strip_widths[image]);
		assert(shape->height == padded / EXTENDED_HORIZON_SAMPLE_SCALE);
		assert(dos_memory_pointer_segment(shape) == classic_page);
		assert((legacy_u32)dos_memory_pointer_offset(shape) + SHAPE2D_HEADER_SIZE +
				   (legacy_u32)shape->width * shape->height <=
			   TEST_CLASSIC_PAGE_BYTES);
		for (legacy_s32 row = 0; row < source->h; row++) {
			const legacy_u8 *pixels = (const legacy_u8 *)source->pixels + row * source->pitch;
			for (legacy_s32 column = 0; column < source->w; column++) {
				assert(pixels[column] == expected_index(offset + column, row, height, seed));
			}
		}
		const legacy_u8 *classic = (const legacy_u8 *)shape + SHAPE2D_HEADER_SIZE;
		for (legacy_s32 row = 0; row < shape->height; row++) {
			for (legacy_s32 column = 0; column < shape->width; column++) {
				assert(classic[row * shape->width + column] ==
					   expected_index(offset + column * EXTENDED_HORIZON_SAMPLE_SCALE +
										  EXTENDED_HORIZON_SAMPLE_SCALE / 2,
									  row * EXTENDED_HORIZON_SAMPLE_SCALE +
										  EXTENDED_HORIZON_SAMPLE_SCALE / 2,
									  height, seed));
			}
		}
		offset += source->w;
	}
	assert(offset == EXTENDED_HORIZON_WIDTH);
	assert(!extended_horizon_image(-1));
	assert(!extended_horizon_shape(SKYBOX_IMAGE_COUNT));
}

static void assert_render_scales(legacy_s32 source_height, legacy_u16 seed);

static void bind_loaded_shapes(void)
{
	for (legacy_s16 image = 0; image < SKYBOX_IMAGE_COUNT; image++) {
		skyboxes[image] = extended_horizon_shape(image);
		skybox.heights[image] = skyboxes[image]->height;
	}
	skybox.minimum_height = skybox.maximum_height = skybox.heights[0];
	skybox.sky_color = TEST_SKY_COLOR;
	skybox.ground_color = TEST_GROUND_COLOR;
	skybox.water_color = TEST_WATER_COLOR;
	loaded_skybox_index = TEST_FALLBACK;
}

static void test_dimensions_and_splits(void)
{
	static const legacy_s32 heights[] = {64, 65, 119, 120};
	static const legacy_u8 types[] = {TEST_PNG_RGB, TEST_PNG_INDEXED};
	for (legacy_u16 kind = 0; kind < SDL_arraysize(types); kind++) {
		for (legacy_u16 index = 0; index < SDL_arraysize(heights); index++) {
			write_fixture(TEST_FIXTURE_NAME, EXTENDED_HORIZON_WIDTH, heights[index], types[kind],
						  TEST_FIRST_SEED, 0);
			assert(extended_horizon_load((const legacy_s8 *)TEST_FIXTURE_NAME));
			assert(extended_horizon_matches((const legacy_s8 *)TEST_FIXTURE_NAME));
			assert_panorama(heights[index], TEST_FIRST_SEED);
			bind_loaded_shapes();
			assert_render_scales(heights[index], TEST_FIRST_SEED);
			extended_horizon_unload();
			assert_unloaded();
			extended_horizon_unload();
		}
	}
	assert(timer_pumps > 0U);
}

static void corrupt_header(legacy_u16 offset, legacy_u8 value, legacy_s16 fix_crc)
{
	legacy_char path[TEST_PATH_CAPACITY];
	fixture_path(path, TEST_FIXTURE_NAME);
	size_t size = 0;
	legacy_u8 *bytes = SDL_LoadFile(path, &size);
	assert(bytes && size > TEST_PNG_HEADER_CRC_OFFSET + sizeof(legacy_u32));
	bytes[offset] = fix_crc ? value : bytes[offset] ^ 1U;
	if (fix_crc) {
		write_be32(bytes + TEST_PNG_HEADER_CRC_OFFSET,
				   png_crc(bytes + TEST_PNG_SIGNATURE_SIZE + sizeof(legacy_u32),
						   sizeof(legacy_u32) + TEST_PNG_HEADER_SIZE));
	}
	assert(SDL_SaveFile(path, bytes, size));
	SDL_free(bytes);
}

static void test_validation_and_transparency(void)
{
	static const struct {
		legacy_s32 width, height;
		legacy_u8 type;
	} invalid[] = {{EXTENDED_HORIZON_WIDTH - 1, EXTENDED_HORIZON_MIN_HEIGHT, TEST_PNG_RGB},
				   {EXTENDED_HORIZON_WIDTH, EXTENDED_HORIZON_MIN_HEIGHT - 1, TEST_PNG_RGB},
				   {EXTENDED_HORIZON_WIDTH, EXTENDED_HORIZON_MAX_HEIGHT + 1, TEST_PNG_RGB},
				   {EXTENDED_HORIZON_WIDTH, EXTENDED_HORIZON_MIN_HEIGHT, TEST_PNG_RGBA},
				   {EXTENDED_HORIZON_WIDTH, EXTENDED_HORIZON_MIN_HEIGHT, TEST_PNG_GRAYSCALE}};
	for (legacy_u16 index = 0; index < SDL_arraysize(invalid); index++) {
		write_fixture(TEST_FIXTURE_NAME, invalid[index].width, invalid[index].height,
					  invalid[index].type, TEST_FIRST_SEED, 0);
		assert(!extended_horizon_load((const legacy_s8 *)TEST_FIXTURE_NAME));
		assert_unloaded();
	}
	write_fixture(TEST_FIXTURE_NAME, EXTENDED_HORIZON_WIDTH, EXTENDED_HORIZON_MIN_HEIGHT,
				  TEST_PNG_RGB, TEST_FIRST_SEED, 0);
	corrupt_header(TEST_PNG_HEADER_CRC_OFFSET, 0U, 0);
	assert(!extended_horizon_load((const legacy_s8 *)TEST_FIXTURE_NAME));
	write_fixture(TEST_FIXTURE_NAME, EXTENDED_HORIZON_WIDTH, EXTENDED_HORIZON_MIN_HEIGHT,
				  TEST_PNG_RGB, TEST_FIRST_SEED, 0);
	corrupt_header(TEST_PNG_INTERLACE_OFFSET, 1U, 1);
	assert(!extended_horizon_load((const legacy_s8 *)TEST_FIXTURE_NAME));
	write_fixture(TEST_FIXTURE_NAME, EXTENDED_HORIZON_WIDTH, EXTENDED_HORIZON_MIN_HEIGHT,
				  TEST_PNG_RGB, TEST_FIRST_SEED, 0);
	corrupt_header(TEST_PNG_DEPTH_OFFSET, 16U, 1);
	assert(!extended_horizon_load((const legacy_s8 *)TEST_FIXTURE_NAME));
	write_fixture(TEST_FIXTURE_NAME, EXTENDED_HORIZON_WIDTH, EXTENDED_HORIZON_MIN_HEIGHT,
				  TEST_PNG_INDEXED, TEST_FIRST_SEED, 1);
	assert(extended_horizon_load((const legacy_s8 *)TEST_FIXTURE_NAME));
	assert_panorama(EXTENDED_HORIZON_MIN_HEIGHT, TEST_FIRST_SEED);
	write_fixture(TEST_FIXTURE_NAME, EXTENDED_HORIZON_WIDTH, EXTENDED_HORIZON_MIN_HEIGHT,
				  TEST_PNG_INDEXED, TEST_FIRST_SEED, 2);
	assert(!extended_horizon_load((const legacy_s8 *)TEST_FIXTURE_NAME));
	assert_unloaded();
	write_fixture(TEST_FIXTURE_NAME, EXTENDED_HORIZON_WIDTH, EXTENDED_HORIZON_MIN_HEIGHT,
				  TEST_PNG_RGB, TEST_FIRST_SEED, 1);
	assert(extended_horizon_load((const legacy_s8 *)TEST_FIXTURE_NAME));
	assert_panorama(EXTENDED_HORIZON_MIN_HEIGHT, TEST_FIRST_SEED);
	write_fixture(TEST_FIXTURE_NAME, EXTENDED_HORIZON_WIDTH, EXTENDED_HORIZON_MIN_HEIGHT,
				  TEST_PNG_RGB, TEST_FIRST_SEED, 2);
	assert(!extended_horizon_load((const legacy_s8 *)TEST_FIXTURE_NAME));
	assert_unloaded();
	assert(!extended_horizon_load((const legacy_s8 *)"../Fixture.png"));
	assert(!extended_horizon_load((const legacy_s8 *)""));
	assert(!extended_horizon_load(NULL));
	assert(!extended_horizon_load((const legacy_s8 *)TEST_MISSING_NAME));
}

static void select_reference(const legacy_char *name)
{
	SDL_strlcpy((legacy_char *)test_track.custom_horizon, name, sizeof(test_track.custom_horizon));
	test_track.horizon = TEST_FALLBACK;
	test_track_active = 1;
	load_skybox(TEST_FALLBACK);
	assert(strcmp((const legacy_char *)test_track.custom_horizon, name) == 0);
	assert(loaded_skybox_index == TEST_FALLBACK);
}

static struct MATRIX test_rotation(legacy_s16 roll)
{
	legacy_f64 radians = roll * (2.0 * SDL_PI_D / ANGLE_FULL_TURN);
	legacy_s16 cosine = (legacy_s16)SDL_lround(SDL_cos(radians) * TRIG_FIXED_ONE);
	legacy_s16 sine = (legacy_s16)SDL_lround(SDL_sin(radians) * TRIG_FIXED_ONE);
	struct MATRIX rotation = {0};
	rotation.m._11 = cosine;
	rotation.m._21 = sine;
	rotation.m._12 = -sine;
	rotation.m._22 = cosine;
	rotation.m._33 = TRIG_FIXED_ONE;
	return rotation;
}

static const legacy_u8 *render_panorama(legacy_s32 scale, legacy_s16 roll, legacy_s32 *width,
										legacy_s32 *height)
{
	hires_set_enabled(1);
	hires_set_render_scale(scale);
	struct MATRIX rotation = test_rotation(roll);
	assert(skybox_hires_render(&target, &skybox, skyboxes, loaded_skybox_index, &rotation, 1,
							   ANGLE_HALF_TURN, 0, 0));
	const legacy_u8 *pixels = video_framebuffer(screen, width, height);
	assert(*width == TEST_SCREEN_WIDTH * scale && *height == TEST_SCREEN_HEIGHT * scale);
	return pixels;
}

static legacy_u32 frame_hash(legacy_s32 scale, legacy_s16 roll)
{
	legacy_s32 width, height;
	const legacy_u8 *pixels = render_panorama(scale, roll, &width, &height);
	legacy_u32 hash = TEST_HASH_OFFSET;
	for (size_t index = 0; index < (size_t)width * height; index++) {
		hash = (hash ^ pixels[index]) * TEST_HASH_PRIME;
	}
	return hash;
}

static void assert_render_scales(legacy_s32 source_height, legacy_u16 seed)
{
	static const legacy_s32 scales[] = {HIRES_SCALE, HIRES_MEDIUM_SCALE, HIRES_MINIMUM_SCALE};
	legacy_s32 padded_height = (source_height + EXTENDED_HORIZON_SAMPLE_SCALE - 1) /
							   EXTENDED_HORIZON_SAMPLE_SCALE * EXTENDED_HORIZON_SAMPLE_SCALE;
	for (legacy_u16 index = 0; index < SDL_arraysize(scales); index++) {
		legacy_s32 width, height, scale = scales[index];
		const legacy_u8 *pixels = render_panorama(scale, 0, &width, &height);
		legacy_s32 source_step = EXTENDED_HORIZON_SAMPLE_SCALE / scale;
		for (legacy_s32 row = 0; row < height; row++) {
			for (legacy_s32 column = 0; column < width; column++) {
				legacy_u8 expected = TEST_SKY_COLOR;
				if (row >= TEST_HORIZON_ROW * scale) {
					expected = TEST_GROUND_COLOR;
				} else if (row >=
						   (TEST_HORIZON_ROW - padded_height / EXTENDED_HORIZON_SAMPLE_SCALE) *
							   scale) {
					legacy_s32 source_y =
						row * source_step + source_step / 2 -
						(TEST_HORIZON_ROW * EXTENDED_HORIZON_SAMPLE_SCALE - padded_height);
					expected = expected_index(column * source_step + source_step / 2, source_y,
											  source_height, seed);
				}
				assert(pixels[row * width + column] == expected);
			}
		}
	}
}

static void test_palette_recapture(void)
{
	struct SHAPE2D *classic = extended_horizon_shape(0);
	SDL_Surface *enhanced = extended_horizon_image(0);
	assert(classic && enhanced);
	void *pixels = enhanced->pixels;
	legacy_u8 black_palette[sizeof(vga_palette)] = {0};
	skybox_hires_set_palette(black_palette);
	assert(extended_horizon_active());
	assert(extended_horizon_shape(0) == classic);
	assert(extended_horizon_image(0) == enhanced);
	assert(enhanced->pixels == pixels);
	for (legacy_s16 image = 0; image < SKYBOX_IMAGE_COUNT; image++) {
		SDL_Surface *source = extended_horizon_image(image);
		const struct SHAPE2D *shape = extended_horizon_shape(image);
		for (legacy_s32 row = 0; row < source->h; row++) {
			const legacy_u8 *indexed = (const legacy_u8 *)source->pixels + row * source->pitch;
			for (legacy_s32 column = 0; column < source->w; column++) {
				assert(indexed[column] == 0U);
			}
		}
		const legacy_u8 *legacy_pixels = (const legacy_u8 *)shape + SHAPE2D_HEADER_SIZE;
		for (size_t index = 0; index < (size_t)shape->width * shape->height; index++) {
			assert(legacy_pixels[index] == 0U);
		}
	}
	skybox_hires_set_palette(vga_palette);
	assert_panorama(65, TEST_FIRST_SEED);
}

static void test_resource_switching_and_rendering(void)
{
	write_fixture(TEST_CUSTOM_NAME, EXTENDED_HORIZON_WIDTH, 65, TEST_PNG_RGB, TEST_FIRST_SEED, 0);
	write_fixture(TEST_SECOND_NAME, EXTENDED_HORIZON_WIDTH, 119, TEST_PNG_INDEXED, TEST_SECOND_SEED,
				  0);
	write_fixture(TEST_UTF8_NAME, EXTENDED_HORIZON_WIDTH, 120, TEST_PNG_RGB, TEST_SECOND_SEED, 0);
	legacy_u32 initial_loads = builtin_loads, initial_frees = builtin_frees;
	select_reference(TEST_CUSTOM_NAME);
	assert(extended_horizon_active());
	assert(skybox_res_ofs == NULL);
	assert(builtin_loads == initial_loads && builtin_frees == initial_frees);
	assert_render_scales(65, TEST_FIRST_SEED);
	test_palette_recapture();
	legacy_u32 first_banked = frame_hash(HIRES_SCALE, ANGLE_EIGHTH_TURN);
	struct SHAPE2D *cached = skyboxes[0];
	select_reference(TEST_CUSTOM_NAME);
	assert(skyboxes[0] == cached);
	select_reference(TEST_SECOND_NAME);
	assert_render_scales(119, TEST_SECOND_SEED);
	legacy_u32 second_banked = frame_hash(HIRES_SCALE, ANGLE_EIGHTH_TURN);
	assert(first_banked != second_banked);
	select_reference(TEST_CUSTOM_NAME);
	assert(frame_hash(HIRES_SCALE, ANGLE_EIGHTH_TURN) == first_banked);
	assert(builtin_loads == initial_loads && builtin_frees == initial_frees);
	select_reference("panorama.png");
	assert(extended_horizon_active());
	assert_panorama(65, TEST_FIRST_SEED);
	select_reference(TEST_UTF8_NAME);
	assert_panorama(120, TEST_SECOND_SEED);
	write_fixture("panorama.png", EXTENDED_HORIZON_WIDTH, 65, TEST_PNG_RGB, TEST_SECOND_SEED, 0);
	unload_skybox();
	assert(!extended_horizon_load((const legacy_s8 *)TEST_CUSTOM_NAME));
	assert_unloaded();
	assert(remove(TEST_PATH_PREFIX "panorama.png") == 0);

	select_reference(TEST_MISSING_NAME);
	assert_unloaded();
	assert(skybox_res_ofs == &builtin_resource);
	assert(builtin_loads == initial_loads + 1U && builtin_frees == initial_frees);
	select_reference(TEST_MISSING_NAME);
	assert(builtin_loads == initial_loads + 1U && builtin_frees == initial_frees);
	select_reference(TEST_CUSTOM_NAME);
	assert(extended_horizon_active());
	assert(builtin_frees == initial_frees + 1U);
	test_track_active = 0;
	load_skybox(TEST_FALLBACK);
	assert_unloaded();
	assert(builtin_loads == initial_loads + 2U);
	assert(strcmp((const legacy_char *)test_track.custom_horizon, TEST_CUSTOM_NAME) == 0);
	unload_skybox();
	assert(builtin_frees == initial_frees + 2U);
	unload_skybox();
	assert(builtin_frees == initial_frees + 2U);
}

static void test_directory_case_and_ambiguity(void)
{
	assert(rename("horizons", "Horizons") == 0);
	assert(extended_horizon_load((const legacy_s8 *)TEST_CUSTOM_NAME));
	assert_panorama(65, TEST_FIRST_SEED);
	extended_horizon_unload();
	assert(SDL_CreateDirectory("horizons"));
	assert(!extended_horizon_load((const legacy_s8 *)TEST_CUSTOM_NAME));
	assert_unloaded();
	assert(rmdir("horizons") == 0);
	assert(extended_horizon_load((const legacy_s8 *)TEST_CUSTOM_NAME));
	extended_horizon_unload();
	assert(rename("Horizons", "horizons") == 0);
}

static void test_bundled_lookup(void)
{
	assert(SDL_CreateDirectory(TEST_BUNDLE_ROOT "/horizons"));
	assert(chdir(TEST_BUNDLE_ROOT) == 0);
	bundled_base = SDL_GetCurrentDirectory();
	assert(bundled_base);
	write_fixture(TEST_BUNDLED_NAME, EXTENDED_HORIZON_WIDTH, 65, TEST_PNG_RGB, TEST_FIRST_SEED, 0);
	assert(chdir("..") == 0);
	assert(extended_horizon_load((const legacy_s8 *)TEST_BUNDLED_NAME));
	assert_panorama(65, TEST_FIRST_SEED);
	extended_horizon_unload();

	/* User artwork takes precedence, including across a case-only spelling. */
	write_fixture("antarctica.PNG", EXTENDED_HORIZON_WIDTH, 119, TEST_PNG_INDEXED, TEST_SECOND_SEED,
				  0);
	assert(extended_horizon_load((const legacy_s8 *)TEST_BUNDLED_NAME));
	assert_panorama(119, TEST_SECOND_SEED);
	extended_horizon_unload();
	/* An invalid local override deliberately shows the saved built-in fallback. */
	write_fixture("antarctica.PNG", EXTENDED_HORIZON_WIDTH, 63, TEST_PNG_RGB, TEST_SECOND_SEED, 0);
	assert(!extended_horizon_load((const legacy_s8 *)TEST_BUNDLED_NAME));
	assert_unloaded();
	write_fixture(TEST_BUNDLED_NAME, EXTENDED_HORIZON_WIDTH, 120, TEST_PNG_RGB, TEST_SECOND_SEED,
				  0);
	assert(!extended_horizon_load((const legacy_s8 *)TEST_BUNDLED_NAME));
	assert(remove(TEST_PATH_PREFIX "antarctica.PNG") == 0);
	assert(remove(TEST_PATH_PREFIX TEST_BUNDLED_NAME) == 0);
	assert(rename("horizons", "Horizons") == 0);
	assert(SDL_CreateDirectory("horizons"));
	assert(!extended_horizon_load((const legacy_s8 *)TEST_BUNDLED_NAME));
	assert(rmdir("horizons") == 0);
	assert(rename("Horizons", "horizons") == 0);

	/* A DOS32 bundle can use its 8.3 filename without changing the saved name. */
	assert(rename(TEST_BUNDLE_ROOT "/horizons/" TEST_BUNDLED_NAME,
				  TEST_BUNDLE_ROOT "/horizons/" TEST_BUNDLED_ALIAS) == 0);
	assert(extended_horizon_load((const legacy_s8 *)TEST_BUNDLED_NAME));
	assert_panorama(65, TEST_FIRST_SEED);
	extended_horizon_unload();
	assert(chdir(TEST_BUNDLE_ROOT) == 0);
	write_fixture(TEST_BUNDLED_NAME, EXTENDED_HORIZON_WIDTH, 120, TEST_PNG_RGB, TEST_SECOND_SEED,
				  0);
	assert(chdir("..") == 0);
	assert(extended_horizon_load((const legacy_s8 *)TEST_BUNDLED_NAME));
	assert_panorama(120, TEST_SECOND_SEED);
	extended_horizon_unload();
	assert(remove(TEST_BUNDLE_ROOT "/horizons/" TEST_BUNDLED_NAME) == 0);
	assert(remove(TEST_BUNDLE_ROOT "/horizons/" TEST_BUNDLED_ALIAS) == 0);
	SDL_free(bundled_base);
	bundled_base = NULL;
	assert(rmdir(TEST_BUNDLE_ROOT "/horizons") == 0);
	assert(rmdir(TEST_BUNDLE_ROOT) == 0);
}

#ifdef RESTUNTS_HORIZON_DIRECTORY
static legacy_u8 bundled_panorama_pixel(legacy_s32 column, legacy_s32 row, legacy_s16 classic)
{
	legacy_s32 scale = classic ? 1 : EXTENDED_HORIZON_SAMPLE_SCALE;
	legacy_s32 panorama_width = SKYBOX_IMAGE_FULL_WRAP * scale;
	column = (column + panorama_width) % panorama_width;
	for (legacy_s16 image = 0; image < SKYBOX_IMAGE_COUNT; image++) {
		legacy_s32 width = strip_widths[image] * scale;
		if (column < width) {
			if (classic) {
				const legacy_u8 *pixels =
					(const legacy_u8 *)extended_horizon_shape(image) + SHAPE2D_HEADER_SIZE;
				return pixels[row * width + column];
			}
			SDL_Surface *strip = extended_horizon_image(image);
			return ((const legacy_u8 *)strip->pixels)[row * strip->pitch + column];
		}
		column -= width;
	}
	assert(!"Panorama column must resolve to one native strip");
	return 0;
}

static void test_bundled_antarctica_seams(void)
{
	/* Four authored compass panels join at different offsets than the native
	 * 1280/768-pixel strips. Check every artwork join, including wraparound. */
	for (legacy_u16 panel = 0; panel < TEST_ANTARCTICA_PANEL_COUNT; panel++) {
		legacy_s32 seam = panel * EXTENDED_HORIZON_WIDTH / TEST_ANTARCTICA_PANEL_COUNT;
		for (legacy_s32 row = 0; row < EXTENDED_HORIZON_MAX_HEIGHT; row++) {
			legacy_u8 expected = bundled_panorama_pixel(seam - 1, row, 0);
			for (legacy_s32 offset = -TEST_ANTARCTICA_JOIN_COLUMNS / 2;
				 offset < TEST_ANTARCTICA_JOIN_COLUMNS / 2; offset++) {
				assert(bundled_panorama_pixel(seam + offset, row, 0) == expected);
			}
		}
		seam /= EXTENDED_HORIZON_SAMPLE_SCALE;
		for (legacy_s32 row = 0; row < EXTENDED_HORIZON_MAX_HEIGHT / EXTENDED_HORIZON_SAMPLE_SCALE;
			 row++) {
			assert(bundled_panorama_pixel(seam - 1, row, 1) ==
				   bundled_panorama_pixel(seam, row, 1));
		}
	}
}

static void test_source_bundle(void)
{
	test_track_active = 1;
	select_reference(TEST_BUNDLED_NAME);
	assert(extended_horizon_active());
	assert(extended_horizon_matches((const legacy_s8 *)TEST_BUNDLED_NAME));
	legacy_u32 scenery_pixels = 0;
	for (legacy_s16 image = 0; image < SKYBOX_IMAGE_COUNT; image++) {
		SDL_Surface *strip = extended_horizon_image(image);
		assert(strip && strip->w == strip_widths[image] * EXTENDED_HORIZON_SAMPLE_SCALE);
		assert(strip->h == EXTENDED_HORIZON_MAX_HEIGHT);
		for (legacy_s32 row = 0; row < strip->h; row++) {
			const legacy_u8 *pixels = (const legacy_u8 *)strip->pixels + row * strip->pitch;
			for (legacy_s32 column = 0; column < strip->w; column++) {
				scenery_pixels += pixels[column] != TEST_SKY_COLOR;
			}
		}
	}
	assert(scenery_pixels > 0);
	test_bundled_antarctica_seams();
	for (legacy_s16 scale = HIRES_MINIMUM_SCALE; scale <= HIRES_SCALE; scale *= 2) {
		assert(frame_hash(scale, 0) != frame_hash(scale, ANGLE_EIGHTH_TURN));
	}
	unload_skybox();
	assert_unloaded();
}
#endif

legacy_int main(void)
{
	assert(SDL_Init(0));
	assert(SDL_CreateDirectory(TEST_ROOT));
	assert(chdir(TEST_ROOT) == 0);
	assert(SDL_CreateDirectory("horizons"));
	for (legacy_u16 shade = 0; shade <= TEST_VGA_CHANNEL_MAX; shade++) {
		memset(vga_palette + (16U + shade) * TEST_PALETTE_CHANNELS, shade, TEST_PALETTE_CHANNELS);
	}
	vga_palette[TEST_SKY_COLOR * TEST_PALETTE_CHANNELS] = 23U;
	vga_palette[TEST_SKY_COLOR * TEST_PALETTE_CHANNELS + 1U] = TEST_VGA_CHANNEL_MAX;
	vga_palette[TEST_SKY_COLOR * TEST_PALETTE_CHANNELS + 2U] = TEST_VGA_CHANNEL_MAX;
	skybox_hires_set_palette(vga_palette);
	materials[TEST_GROUND_MATERIAL] = TEST_GROUND_COLOR;
	materials[TEST_SKY_MATERIAL] = TEST_SKY_COLOR;
	materials[TEST_WATER_MATERIAL] = TEST_WATER_COLOR;
	for (legacy_u16 index = 0; index < SKYBOX_IMAGE_COUNT; index++) {
		builtin_shapes[index].width = (legacy_u16)strip_widths[index];
		builtin_shapes[index].height = EXTENDED_HORIZON_MIN_HEIGHT / EXTENDED_HORIZON_SAMPLE_SCALE;
	}
	screen = dos_memory_make_pointer(0xA000, 0);
	target.sprite_bitmapptr = (struct SHAPE2D *)screen;
	target.sprite_lineofs = target_rows;
	target.sprite_pitch = TEST_SCREEN_WIDTH;
	target.sprite_raster_right = TEST_SCREEN_WIDTH;
	target.sprite_bottom = TEST_SCREEN_HEIGHT;
	for (legacy_u16 row = 0; row < TEST_SCREEN_HEIGHT; row++) {
		LEGACY_WRITE_U16_LE(target_rows + row * sizeof(legacy_u16), row * TEST_SCREEN_WIDTH);
	}
	test_dimensions_and_splits();
	test_validation_and_transparency();
	test_resource_switching_and_rendering();
	test_directory_case_and_ambiguity();
	test_bundled_lookup();
#ifdef RESTUNTS_HORIZON_DIRECTORY
	test_source_bundle();
#endif
	assert_unloaded();
	hires_shutdown();
	static const legacy_char *names[] = {TEST_FIXTURE_NAME, TEST_CUSTOM_NAME, TEST_SECOND_NAME,
										 TEST_UTF8_NAME};
	for (legacy_u16 index = 0; index < SDL_arraysize(names); index++) {
		legacy_char path[TEST_PATH_CAPACITY];
		fixture_path(path, names[index]);
		assert(remove(path) == 0);
	}
	assert(rmdir("horizons") == 0);
	assert(chdir("..") == 0);
	assert(rmdir(TEST_ROOT) == 0);
	SDL_Quit();
	puts("Custom horizon decoding, fallback, ownership, and rendering tests passed.");
	return 0;
}
