#include "../c/renderer/gpu_overlay.h"
#include "../c/hires/hires_internal.h"
#include "../c/shape2d/shape2d.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TEST_WIDTH HIRES_WIDTH
#define TEST_HEIGHT HIRES_HEIGHT
#define TEST_WORLD_BOTTOM (HIRES_HEIGHT * 3 / 4)
#define TEST_FRAME_COLOR 0xFF123456U
#define TEST_ALPHA_SHIFT 24U
#define TEST_LOGICAL_X 3U
#define TEST_LOGICAL_Y 5U
#define TEST_SHADOW_OPACITY 64U

static legacy_u8 offscreen[HIRES_ADDRESS_COUNT];
static legacy_u8 screen[HIRES_ADDRESS_COUNT];
static legacy_u8 opaque;

static void fill_page(legacy_u32 *pixels)
{
	for (size_t index = 0; index < (size_t)TEST_WIDTH * TEST_HEIGHT; index++) {
		pixels[index] = TEST_FRAME_COLOR;
	}
}

static legacy_u32 alpha_at(const legacy_u32 *pixels, legacy_s32 x, legacy_s32 y)
{
	return pixels[(size_t)y * TEST_WIDTH + x] >> TEST_ALPHA_SHIFT;
}

legacy_int main(void)
{
	legacy_u32 *pixels = malloc((size_t)TEST_WIDTH * TEST_HEIGHT * sizeof(*pixels));
	assert(pixels != NULL);
	legacy_s32 shift_x = 0, shift_y = 0;
	struct HIRES_SURFACE source = {0};
	struct HIRES_RASTER_TARGET target = {0};
	source.base = offscreen;
	target.surface = &source;
	target.scale = HIRES_SCALE;
	target.width = TEST_WIDTH;
	target.height = TEST_HEIGHT;
	target.right = TEST_WIDTH;
	target.bottom = TEST_WORLD_BOTTOM;
	for (legacy_s32 y = 0; y < HIRES_LOGICAL_HEIGHT; y++) {
		target.rows[y] = (legacy_u16)(y * HIRES_LOGICAL_WIDTH);
	}
	gpu_overlay_reset();
	fill_page(pixels);
	assert(!gpu_overlay_compose(offscreen, pixels, TEST_WIDTH, TEST_HEIGHT, &shift_x, &shift_y));
	assert(pixels[0] == TEST_FRAME_COLOR);

	gpu_overlay_begin(&target);
	assert(gpu_overlay_tracking);
	fill_page(pixels);
	assert(gpu_overlay_compose(offscreen, pixels, TEST_WIDTH, TEST_HEIGHT, &shift_x, &shift_y));
	assert(alpha_at(pixels, 0, 0) == 0);
	assert(alpha_at(pixels, TEST_WIDTH - 1, TEST_WORLD_BOTTOM - 1) == 0);
	assert(alpha_at(pixels, 0, TEST_WORLD_BOTTOM) == LEGACY_U8_MAX);
	assert((pixels[0] & 0x00FFFFFFU) == (TEST_FRAME_COLOR & 0x00FFFFFFU));

	legacy_u16 cell = TEST_LOGICAL_Y * HIRES_LOGICAL_WIDTH + TEST_LOGICAL_X;
	gpu_overlay_write(offscreen, cell, 1);
	gpu_overlay_shadow(offscreen, cell, 0, TEST_SHADOW_OPACITY);
	gpu_overlay_raster(screen, 0, offscreen, 0, HIRES_LOGICAL_WIDTH * HIRES_LOGICAL_HEIGHT,
					   SHAPE2D_RASTER_COPY, NULL);
	fill_page(pixels);
	assert(gpu_overlay_compose(screen, pixels, TEST_WIDTH, TEST_HEIGHT, &shift_x, &shift_y));
	assert(shift_x == 0 && shift_y == 0);
	assert(alpha_at(pixels, TEST_LOGICAL_X * HIRES_SCALE + 1, TEST_LOGICAL_Y * HIRES_SCALE) ==
		   LEGACY_U8_MAX);
	assert(alpha_at(pixels, TEST_LOGICAL_X * HIRES_SCALE + 2, TEST_LOGICAL_Y * HIRES_SCALE) == 0);
	assert(alpha_at(pixels, TEST_LOGICAL_X * HIRES_SCALE, TEST_LOGICAL_Y * HIRES_SCALE) ==
		   TEST_SHADOW_OPACITY);
	assert(
		(pixels[(size_t)TEST_LOGICAL_Y * HIRES_SCALE * TEST_WIDTH + TEST_LOGICAL_X * HIRES_SCALE] &
		 0x00FFFFFFU) == 0);

	opaque = LEGACY_U8_MAX;
	gpu_overlay_raster(screen, cell, &opaque, 0, 1, SHAPE2D_RASTER_AND, NULL);
	opaque = 0;
	gpu_overlay_raster(screen, cell, &opaque, 0, 1, SHAPE2D_RASTER_OR, NULL);
	fill_page(pixels);
	gpu_overlay_compose(screen, pixels, TEST_WIDTH, TEST_HEIGHT, &shift_x, &shift_y);
	assert(alpha_at(pixels, TEST_LOGICAL_X * HIRES_SCALE + 2, TEST_LOGICAL_Y * HIRES_SCALE) == 0);
	gpu_overlay_raster(screen, cell, &opaque, 0, 1, SHAPE2D_RASTER_AND, NULL);
	fill_page(pixels);
	gpu_overlay_compose(screen, pixels, TEST_WIDTH, TEST_HEIGHT, &shift_x, &shift_y);
	for (legacy_s32 y = 0; y < HIRES_SCALE; y++) {
		for (legacy_s32 x = 0; x < HIRES_SCALE; x++) {
			assert(alpha_at(pixels, TEST_LOGICAL_X * HIRES_SCALE + x,
							TEST_LOGICAL_Y * HIRES_SCALE + y) == LEGACY_U8_MAX);
		}
	}

	gpu_overlay_raster(screen, HIRES_LOGICAL_WIDTH, offscreen, 0, HIRES_LOGICAL_WIDTH,
					   SHAPE2D_RASTER_COPY, NULL);
	fill_page(pixels);
	gpu_overlay_compose(screen, pixels, TEST_WIDTH, TEST_HEIGHT, &shift_x, &shift_y);
	assert(shift_x == 0 && shift_y == HIRES_SCALE);
	/* The presentation path fuses alpha into its existing palette expansion. */
	legacy_u32 palette[LEGACY_U8_MAX + 1U];
	for (legacy_u32 index = 0; index <= LEGACY_U8_MAX; index++) {
		palette[index] = TEST_FRAME_COLOR;
	}
	legacy_u32 *composed = malloc((size_t)TEST_WIDTH * TEST_HEIGHT * sizeof(*composed));
	assert(composed != NULL);
	const legacy_u8 *mask = gpu_overlay_mask(screen, TEST_WIDTH, TEST_HEIGHT, NULL, NULL);
	assert(mask != NULL);
	gpu_overlay_compose_rows(screen, palette, NULL, mask, composed, TEST_WIDTH * sizeof(*composed),
							 HIRES_SCALE, 0, HIRES_LOGICAL_HEIGHT);
	assert(memcmp(composed, pixels, (size_t)TEST_WIDTH * TEST_HEIGHT * sizeof(*pixels)) == 0);
	free(composed);

	gpu_overlay_reset();
	fill_page(pixels);
	assert(!gpu_overlay_compose(screen, pixels, TEST_WIDTH, TEST_HEIGHT, &shift_x, &shift_y));
	assert(pixels[0] == TEST_FRAME_COLOR);
	/* Classic Vulkan uses the same mask lifetime at one sample per legacy pixel. */
	target.scale = 1;
	target.width = HIRES_LOGICAL_WIDTH;
	target.height = HIRES_LOGICAL_HEIGHT;
	target.right = HIRES_LOGICAL_WIDTH;
	target.bottom = HIRES_LOGICAL_HEIGHT / 2;
	gpu_overlay_begin(&target);
	mask = gpu_overlay_mask(offscreen, target.width, target.height, NULL, NULL);
	assert(mask != NULL && mask[0] == 0);
	assert(mask[HIRES_LOGICAL_WIDTH * target.bottom] == LEGACY_U8_MAX);
	gpu_overlay_write(offscreen, 1, -1);
	assert(mask[1] == LEGACY_U8_MAX && mask[2] == 0);
	/* Legacy copies traverse forward. Left overlaps preserve the remaining
	 * source; right overlaps deliberately propagate earlier writes. */
	gpu_overlay_shadow(offscreen, 3, 0, TEST_SHADOW_OPACITY);
	gpu_overlay_raster(offscreen, 0, offscreen, 1, 3, SHAPE2D_RASTER_COPY, NULL);
	assert(mask[0] == LEGACY_U8_MAX && mask[1] == 0 && mask[2] == TEST_SHADOW_OPACITY);
	gpu_overlay_raster(offscreen, 1, offscreen, 0, 3, SHAPE2D_RASTER_COPY, NULL);
	for (legacy_s32 index = 0; index < 4; index++) {
		assert(mask[index] == LEGACY_U8_MAX);
	}
	gpu_overlay_raster(offscreen, 0, offscreen, 0, 4, SHAPE2D_RASTER_COPY, NULL);
	assert(mask[0] == LEGACY_U8_MAX && mask[3] == LEGACY_U8_MAX);
	gpu_overlay_mask(offscreen, target.width, target.height, &shift_x, &shift_y);
	legacy_s32 previous_x = shift_x, previous_y = shift_y;
	offscreen[0] = LEGACY_U8_MAX;
	gpu_overlay_raster(offscreen, 10, offscreen, 0, 1, SHAPE2D_RASTER_AND, NULL);
	offscreen[0] = 0;
	gpu_overlay_raster(offscreen, 10, offscreen, 0, 1, SHAPE2D_RASTER_OR, NULL);
	/* Copying opaque HUD pixels from a tracked surface cannot translate the
	 * unrelated GPU scene still visible through the rest of that surface. */
	gpu_overlay_raster(offscreen, 11, offscreen, 0, 1, SHAPE2D_RASTER_COPY, NULL);
	legacy_u8 map[LEGACY_U8_MAX + 1U];
	memset(map, LEGACY_U8_MAX, sizeof(map));
	gpu_overlay_raster(offscreen, 12, offscreen, 4, 1, SHAPE2D_RASTER_MAP, map);
	gpu_overlay_mask(offscreen, target.width, target.height, &shift_x, &shift_y);
	assert(shift_x == previous_x && shift_y == previous_y && mask[10] == 0);
	gpu_overlay_forget(offscreen, sizeof(offscreen));
	/* Sprite row addresses wrap at 64 KiB, independently of sample scale. */
	target.rows[0] = HIRES_ADDRESS_COUNT - 2U;
	target.bottom = 1;
	gpu_overlay_reset();
	gpu_overlay_begin(&target);
	mask = gpu_overlay_mask(offscreen, target.width, target.height, NULL, NULL);
	assert(mask != NULL && mask[HIRES_ADDRESS_COUNT - 1U] == 0 && mask[0] == 0);
	assert(mask[HIRES_LOGICAL_WIDTH - 3U] == 0);
	assert(mask[HIRES_LOGICAL_WIDTH - 2U] == LEGACY_U8_MAX);
	gpu_overlay_shutdown();
	free(pixels);
	puts("GPU foreground masks preserve per-sample HUD writes, shadows, sprite copies and frame "
		 "lifetime.");
	return 0;
}
