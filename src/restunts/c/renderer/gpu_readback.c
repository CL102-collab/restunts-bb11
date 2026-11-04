#include "gpu_readback.h"
#include "gpu_raster.h"
#include "../hires/hires_internal.h"
#include <string.h>

static void gpu_readback_depth(const struct HIRES_RASTER_TARGET *target, const legacy_u32 *samples)
{
	legacy_s32 left = target->left > target->depth_left ? target->left : target->depth_left;
	legacy_s32 right = target->right < target->depth_right ? target->right : target->depth_right;
	legacy_s32 top = target->top > target->depth_top ? target->top : target->depth_top;
	legacy_s32 bottom =
		target->bottom < target->depth_bottom ? target->bottom : target->depth_bottom;
	if (left >= right || top >= bottom) {
		return;
	}
	for (legacy_s32 y = top; y < bottom; y++) {
		const size_t row = (size_t)y * target->width;
		const legacy_u32 *restrict source = samples + (row + left) * GPU_RASTER_OUTPUT_WORDS;
		legacy_f32 *restrict depths = target->inverse_depth + row + left;
		legacy_u32 *restrict families = target->depth_family + row + left;
		for (legacy_s32 x = left; x < right; x++, source += GPU_RASTER_OUTPUT_WORDS) {
			memcpy(depths++, source + 1U, sizeof(*depths));
			*families++ = source[2];
		}
	}
}

/* Full four-sample rows are common at native HyperVision resolution. Resolve
 * their touched bits once and let the compiler combine the indexed writes. */
static void gpu_readback_indexed(legacy_u8 *destination, const legacy_u32 *source, legacy_s32 count)
{
	if (count == HIRES_SCALE) {
		legacy_u32 first = source[0];
		legacy_u32 second = source[GPU_RASTER_OUTPUT_WORDS];
		legacy_u32 third = source[GPU_RASTER_OUTPUT_WORDS * 2U];
		legacy_u32 fourth = source[GPU_RASTER_OUTPUT_WORDS * 3U];
		if ((first & second & third & fourth & GPU_RASTER_TOUCHED) != 0) {
			destination[0] = (legacy_u8)first;
			destination[1] = (legacy_u8)second;
			destination[2] = (legacy_u8)third;
			destination[3] = (legacy_u8)fourth;
			return;
		}
	}
	for (legacy_s32 sample = 0; sample < count; sample++) {
		legacy_u32 value = source[(size_t)sample * GPU_RASTER_OUTPUT_WORDS];
		if ((value & GPU_RASTER_TOUCHED) != 0) {
			destination[sample] = (legacy_u8)value;
		}
	}
}

static legacy_u32 gpu_readback_cell(const struct HIRES_RASTER_TARGET *target,
									const legacy_u32 *samples, legacy_s32 left, legacy_s32 right,
									legacy_s32 top, legacy_s32 bottom, legacy_u16 offset)
{
	struct HIRES_SURFACE *surface = target->surface;
	legacy_u8 *cell = surface->pixels + ((size_t)offset << target->cell_shift);
	legacy_s32 start = left & target->scale_mask;
	legacy_s32 count = right - left;
	if (surface->valid[offset] != HIRES_CELL_ARGB) {
		for (legacy_s32 y = top; y < bottom; y++) {
			legacy_u32 row = (y & target->scale_mask) << target->scale_shift;
			const legacy_u32 *source =
				samples + ((size_t)y * target->width + left) * GPU_RASTER_OUTPUT_WORDS;
			gpu_readback_indexed(cell + row + start, source, count);
		}
		return 0;
	}
	/* An overlay is retired once after its whole cell has been imported. The
	 * per-pixel API scans that same cell after every touched sample. */
	legacy_u32 *argb = surface->argb + ((size_t)offset << target->cell_shift);
	legacy_u32 painted = 0;
	for (legacy_s32 y = top; y < bottom; y++) {
		legacy_u32 row = ((y & target->scale_mask) << target->scale_shift) + start;
		const legacy_u32 *source =
			samples + ((size_t)y * target->width + left) * GPU_RASTER_OUTPUT_WORDS;
		for (legacy_s32 x = 0; x < count; x++) {
			legacy_u32 value = source[(size_t)x * GPU_RASTER_OUTPUT_WORDS];
			if ((value & GPU_RASTER_TOUCHED) != 0) {
				cell[row + x] = (legacy_u8)value;
				argb[row + x] = 0;
				painted = 1;
			}
		}
	}
	if (painted) {
		legacy_u32 remaining = 0;
		for (legacy_s32 sample = 0; sample < target->cell_pixels; sample++) {
			remaining |= argb[sample];
		}
		if (remaining == 0) {
			surface->valid[offset] = HIRES_CELL_INDEXED;
			return 1;
		}
	}
	return 0;
}

void gpu_readback_hires(const struct HIRES_RASTER_TARGET *target, const legacy_u32 *samples)
{
	gpu_readback_depth(target, samples);
	legacy_u32 cleared_argb_cells = 0;
	const legacy_s32 mask = target->scale_mask;
	const legacy_s32 shift = target->scale_shift;
	for (legacy_s32 top = target->top; top < target->bottom;) {
		legacy_s32 bottom = (top | mask) + 1;
		if (bottom > target->bottom) {
			bottom = target->bottom;
		}
		legacy_u16 row = target->rows[top >> shift];
		for (legacy_s32 left = target->left; left < target->right;) {
			legacy_s32 right = (left | mask) + 1;
			if (right > target->right) {
				right = target->right;
			}
			legacy_u16 offset = (legacy_u16)(row + (left >> shift));
			cleared_argb_cells +=
				gpu_readback_cell(target, samples, left, right, top, bottom, offset);
			left = right;
		}
		top = bottom;
	}
	hires_raster_finish(target, cleared_argb_cells);
}
