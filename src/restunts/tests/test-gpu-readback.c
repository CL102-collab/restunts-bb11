#include "../c/renderer/gpu_readback.h"
#include "../c/renderer/gpu_raster.h"
#include "../c/hires/hires_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_LOGICAL_WIDTH 7
#define TEST_LOGICAL_HEIGHT 6
#define TEST_ROW_STRIDE 320U
#define TEST_WRAPPED_ROW (HIRES_ADDRESS_COUNT - 3U)
#define TEST_OLD_COLOR 0x59U
#define TEST_NEW_COLOR 0xC7U
#define TEST_ARGB_COLOR 0xFF102030U
#define TEST_DEPTH_BITS 0x3F800000U
#define TEST_FAMILY 91U
#define TEST_LAYOUTS 3
#define TEST_CLIPS 3
#define TEST_TOUCH_MODES 3

static void reference_readback(const struct HIRES_RASTER_TARGET *target, const legacy_u32 *samples)
{
	struct HIRES_RASTER_CONTEXT context = {target, target->top, target->bottom, 0};
	for (legacy_s32 y = target->top; y < target->bottom; y++) {
		for (legacy_s32 x = target->left; x < target->right; x++) {
			size_t pixel = (size_t)y * target->width + x;
			const legacy_u32 *source = samples + pixel * GPU_RASTER_OUTPUT_WORDS;
			if ((source[0] & GPU_RASTER_TOUCHED) != 0) {
				hires_raster_pixel(&context, x, y, (legacy_u8)source[0]);
			}
			if (x >= target->depth_left && x < target->depth_right && y >= target->depth_top &&
				y < target->depth_bottom) {
				memcpy(&target->inverse_depth[pixel], source + 1U, sizeof(legacy_u32));
				target->depth_family[pixel] = source[2];
			}
		}
	}
	hires_raster_finish(target, context.cleared_argb_cells);
}

static void test_readback(legacy_s32 scale, legacy_s32 layout, legacy_s32 clip, legacy_s32 touched)
{
	struct HIRES_SURFACE expected = {0};
	struct HIRES_SURFACE actual = {0};
	struct HIRES_RASTER_TARGET target = {0};
	target.scale = hires_state.render_scale = scale;
	target.scale_shift = hires_state.render_shift = scale == HIRES_SCALE ? HIRES_MAXIMUM_SCALE_SHIFT
													: scale == HIRES_MEDIUM_SCALE ? 1
																				  : 0;
	target.scale_mask = hires_state.render_mask = scale - 1;
	target.cell_pixels = hires_state.render_cell_pixels = scale * scale;
	target.cell_shift = hires_state.render_cell_shift = target.scale_shift * HIRES_CELL_DIMENSIONS;
	target.width = TEST_LOGICAL_WIDTH * scale;
	target.height = TEST_LOGICAL_HEIGHT * scale;
	target.left = clip == 0 ? 0 : scale - 1;
	target.right = target.width - (clip == 0 ? 0 : 1);
	target.top = clip == 0 ? 0 : 1;
	target.bottom = target.height - (clip == 0 ? 0 : scale - 1);
	target.depth_left = scale;
	target.depth_right = target.width - scale;
	target.depth_top = scale;
	target.depth_bottom = target.height - scale;
	if (clip == TEST_CLIPS - 1) {
		target.depth_left = target.depth_right = 0;
	}
	for (legacy_s32 y = 0; y < TEST_LOGICAL_HEIGHT; y++) {
		legacy_u32 row = layout == 1 ? TEST_WRAPPED_ROW : 0U;
		legacy_s32 selected = layout == 2 ? TEST_LOGICAL_HEIGHT - 1 - y : y;
		target.rows[y] = (legacy_u16)(row + selected * TEST_ROW_STRIDE);
	}
	size_t color_bytes = (size_t)HIRES_ADDRESS_COUNT * target.cell_pixels;
	expected.pixels = malloc(color_bytes);
	expected.argb = calloc(color_bytes, sizeof(*expected.argb));
	actual.pixels = malloc(color_bytes);
	actual.argb = malloc(color_bytes * sizeof(*actual.argb));
	assert(expected.pixels != NULL && expected.argb != NULL && actual.pixels != NULL &&
		   actual.argb != NULL);
	memset(expected.pixels, TEST_OLD_COLOR, color_bytes);
	memset(expected.valid, HIRES_CELL_INDEXED, sizeof(expected.valid));
	for (legacy_s32 y = 0; y < TEST_LOGICAL_HEIGHT; y++) {
		for (legacy_s32 x = 0; x < TEST_LOGICAL_WIDTH; x++) {
			legacy_u16 offset = (legacy_u16)(target.rows[y] + x);
			if ((x + y) % TEST_TOUCH_MODES == 0) {
				expected.valid[offset] = HIRES_CELL_ARGB;
				expected.argb_cells++;
				for (legacy_s32 sample = 0; sample < target.cell_pixels; sample++) {
					expected.argb[((size_t)offset << target.cell_shift) + sample] =
						(sample + x) % TEST_TOUCH_MODES == 0 ? TEST_ARGB_COLOR : 0;
				}
			}
		}
	}
	memcpy(actual.pixels, expected.pixels, color_bytes);
	memcpy(actual.argb, expected.argb, color_bytes * sizeof(*actual.argb));
	memcpy(actual.valid, expected.valid, sizeof(actual.valid));
	actual.argb_cells = expected.argb_cells;
	size_t pixels = (size_t)target.width * target.height;
	legacy_u32 *samples = calloc(pixels * GPU_RASTER_OUTPUT_WORDS, sizeof(*samples));
	legacy_f32 *expected_depth = calloc(pixels, sizeof(*expected_depth));
	legacy_f32 *actual_depth = calloc(pixels, sizeof(*actual_depth));
	legacy_u32 *expected_family = calloc(pixels, sizeof(*expected_family));
	legacy_u32 *actual_family = calloc(pixels, sizeof(*actual_family));
	assert(samples != NULL && expected_depth != NULL && actual_depth != NULL &&
		   expected_family != NULL && actual_family != NULL);
	for (size_t pixel = 0; pixel < pixels; pixel++) {
		samples[pixel * GPU_RASTER_OUTPUT_WORDS] =
			((TEST_NEW_COLOR + (legacy_u32)pixel) & LEGACY_U8_MAX) |
			(touched == 0 || (touched == 1 && pixel % TEST_TOUCH_MODES != 0) ? GPU_RASTER_TOUCHED
																			 : 0U);
		samples[pixel * GPU_RASTER_OUTPUT_WORDS + 1U] = TEST_DEPTH_BITS + (legacy_u32)pixel;
		samples[pixel * GPU_RASTER_OUTPUT_WORDS + 2U] = TEST_FAMILY + (legacy_u32)pixel;
	}
	target.surface = &expected;
	target.inverse_depth = clip == TEST_CLIPS - 1 ? NULL : expected_depth;
	target.depth_family = clip == TEST_CLIPS - 1 ? NULL : expected_family;
	reference_readback(&target, samples);
	target.surface = &actual;
	target.inverse_depth = clip == TEST_CLIPS - 1 ? NULL : actual_depth;
	target.depth_family = clip == TEST_CLIPS - 1 ? NULL : actual_family;
	gpu_readback_hires(&target, samples);
	assert(expected.argb_cells == actual.argb_cells);
	assert(memcmp(expected.pixels, actual.pixels, color_bytes) == 0);
	assert(memcmp(expected.argb, actual.argb, color_bytes * sizeof(*actual.argb)) == 0);
	assert(memcmp(expected.valid, actual.valid, sizeof(expected.valid)) == 0);
	assert(memcmp(expected_depth, actual_depth, pixels * sizeof(*actual_depth)) == 0);
	assert(memcmp(expected_family, actual_family, pixels * sizeof(*actual_family)) == 0);
	free(expected.pixels);
	free(expected.argb);
	free(actual.pixels);
	free(actual.argb);
	free(samples);
	free(expected_depth);
	free(actual_depth);
	free(expected_family);
	free(actual_family);
}

legacy_int main(void)
{
	for (legacy_s32 scale = HIRES_MINIMUM_SCALE; scale <= HIRES_SCALE; scale *= 2) {
		for (legacy_s32 layout = 0; layout < TEST_LAYOUTS; layout++) {
			for (legacy_s32 clip = 0; clip < TEST_CLIPS; clip++) {
				for (legacy_s32 touched = 0; touched < TEST_TOUCH_MODES; touched++) {
					test_readback(scale, layout, clip, touched);
				}
			}
		}
	}
	puts("GPU bulk readback clipping, scale, wrapped rows, ARGB retirement and depth passed.");
	return 0;
}
