#include "hires.h"
#include "render_workers.h"
#include "../renderer/gpu_overlay.h"
#include "../platform.h"
#include "../shape2d/shape2d.h"
#include "../shape2d/shape2d_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <float.h>

#include "hires_internal.h"

#define HIRES_DEPTH_CLEAR_ROWS 32
#define HIRES_DEPTH_CLEAR_MIN_AREA 262144U

struct HIRES_DEPTH_CLEAR {
	legacy_s32 left, right, top, bottom;
};

static void hires_depth_clear_job(void *argument, legacy_s32 index)
{
	const struct HIRES_DEPTH_CLEAR *clear = argument;
	legacy_s32 top = clear->top + index * HIRES_DEPTH_CLEAR_ROWS;
	legacy_s32 bottom = top + HIRES_DEPTH_CLEAR_ROWS;
	if (bottom > clear->bottom) {
		bottom = clear->bottom;
	}
	for (legacy_s32 y = top; y < bottom; y++) {
		memset(hires_state.depth_family + (size_t)y * hires_state.render_width + clear->left, 0,
			   (size_t)(clear->right - clear->left) * sizeof(*hires_state.depth_family));
	}
}

/* Keep depth across the scene so overlapping tiles cannot paint through one
 * another. Family zero invalidates old depth without clearing the float array.
 * Bounds are clipped to the hires_state.active drawing target. */
void hires_depth_begin(legacy_s32 left, legacy_s32 right, legacy_s32 top, legacy_s32 bottom)
{
	hires_depth_reset();
	if (hires_state.active == NULL) {
		return;
	}
	legacy_s32 clip_left = hires_state.active_sprite.sprite_raster_left * hires_state.render_scale;
	legacy_s32 clip_right =
		hires_state.active_sprite.sprite_raster_right * hires_state.render_scale;
	legacy_s32 clip_top = hires_state.active_sprite.sprite_top * hires_state.render_scale;
	legacy_s32 clip_bottom = hires_state.active_sprite.sprite_bottom * hires_state.render_scale;
	if (clip_left < 0) {
		clip_left = 0;
	}
	if (clip_right > hires_state.render_width) {
		clip_right = hires_state.render_width;
	}
	if (clip_top < 0) {
		clip_top = 0;
	}
	if (clip_bottom > hires_state.render_height) {
		clip_bottom = hires_state.render_height;
	}
	if (left < clip_left) {
		left = clip_left;
	}
	if (right > clip_right) {
		right = clip_right;
	}
	if (top < clip_top) {
		top = clip_top;
	}
	if (bottom > clip_bottom) {
		bottom = clip_bottom;
	}
	if (left >= right || top >= bottom) {
		return;
	}
	if (hires_state.inverse_depth == NULL) {
		hires_state.inverse_depth =
			hires_allocate((size_t)hires_state.render_width * hires_state.render_height *
						   sizeof(*hires_state.inverse_depth));
		hires_state.depth_family =
			hires_allocate((size_t)hires_state.render_width * hires_state.render_height *
						   sizeof(*hires_state.depth_family));
	}
	hires_state.depth_left = left;
	hires_state.depth_right = right;
	hires_state.depth_top = top;
	hires_state.depth_bottom = bottom;
	struct HIRES_DEPTH_CLEAR clear = {left, right, top, bottom};
	legacy_s32 count = (bottom - top + HIRES_DEPTH_CLEAR_ROWS - 1) / HIRES_DEPTH_CLEAR_ROWS;
	if ((legacy_u32)(right - left) * (bottom - top) >= HIRES_DEPTH_CLEAR_MIN_AREA &&
		hires_render_workers_count() != 0) {
		hires_render_workers_run(count, hires_depth_clear_job, &clear);
	} else {
		for (legacy_s32 index = 0; index < count; index++) {
			hires_depth_clear_job(&clear, index);
		}
	}
}

static legacy_s32 hires_test_depth(legacy_f32 *depths, legacy_u32 *families, size_t index,
								   legacy_f64 inverse_z, legacy_u32 family, legacy_s32 mode)
{
	if (mode != HIRES_DEPTH_SURFACE && families[index] == family) {
		/* Resource overlays may sit behind their supporting surface. Keep
		 * that surface's occlusion depth while honoring authored paint order.
		 * Unsorted shapes share a family, so nearer hires_state.surfaces must also advance
		 * its depth before other shapes are tested against it. */
		if (mode == HIRES_DEPTH_ORDERED && inverse_z > depths[index]) {
			depths[index] = (legacy_f32)inverse_z;
		}
		return 1;
	}
	legacy_f32 depth = (legacy_f32)inverse_z;
	if (families[index] != 0 &&
		depth + HIRES_DEPTH_EPSILON_SCALE * FLT_EPSILON * depths[index] < depths[index]) {
		return 0;
	}
	depths[index] = depth;
	families[index] = family;
	return 1;
}

legacy_s32 hires_depth_test(legacy_s32 x, legacy_s32 y, legacy_f64 inverse_z, legacy_u32 family,
							legacy_s32 mode)
{
	if (hires_state.active == NULL || x < hires_state.depth_left || x >= hires_state.depth_right ||
		y < hires_state.depth_top || y >= hires_state.depth_bottom || !(inverse_z > 0) ||
		inverse_z > FLT_MAX || family == HIRES_DEPTH_FAMILY_NONE) {
		return 0;
	}
	return hires_test_depth(hires_state.inverse_depth, hires_state.depth_family,
							(size_t)y * hires_state.render_width + x, inverse_z, family, mode);
}

/* The caller owns the whole legacy cell and accounts for cleared ARGB cells.
 * Raster workers accumulate locally instead of changing the surface counter. */
static legacy_u32 hires_paint_sample(struct HIRES_SURFACE *surface, legacy_u16 offset,
									 legacy_u32 sample, legacy_u8 color)
{
	legacy_u8 *cell = surface->pixels + ((size_t)offset << hires_state.render_cell_shift);
	cell[sample] = color;
	if (surface->valid[offset] == HIRES_CELL_ARGB) {
		legacy_u32 *argb = surface->argb + ((size_t)offset << hires_state.render_cell_shift);
		argb[sample] = 0;
		legacy_u32 remaining = 0;
		for (legacy_s32 index = 0; index < hires_state.render_cell_pixels; index++) {
			remaining |= argb[index];
		}
		if (remaining == 0) {
			surface->valid[offset] = HIRES_CELL_INDEXED;
			return 1;
		}
	}
	return 0;
}

void hires_pixel(legacy_s32 x, legacy_s32 y, legacy_u8 color)
{
	if (hires_state.active == NULL || x < 0 || y < 0 || x >= hires_state.render_width ||
		y >= hires_state.render_height ||
		x < hires_state.active_sprite.sprite_raster_left * hires_state.render_scale ||
		x >= hires_state.active_sprite.sprite_raster_right * hires_state.render_scale ||
		y < hires_state.active_sprite.sprite_top * hires_state.render_scale ||
		y >= hires_state.active_sprite.sprite_bottom * hires_state.render_scale) {
		return;
	}
	legacy_u16 row = LEGACY_READ_U16_LE(hires_state.active_sprite.sprite_lineofs +
										(y >> hires_state.render_shift) * LEGACY_WORD_BYTES);
	legacy_u16 offset = (legacy_u16)(row + (x >> hires_state.render_shift));
	hires_cell(hires_state.active, offset);
	legacy_u32 sample =
		(y & hires_state.render_mask) * hires_state.render_scale + (x & hires_state.render_mask);
	if (gpu_overlay_tracking) {
		gpu_overlay_write(hires_state.active->base, offset, sample);
	}
	if (hires_paint_sample(hires_state.active, offset, sample, color) != 0) {
		hires_state.active->argb_cells--;
	}
}

static legacy_s32 hires_raster_rows_disjoint(const struct HIRES_RASTER_TARGET *target)
{
	/* Ordinary framebuffers have ascending, nonoverlapping rows. Avoid a
	 * full address bitmap walk for this common case. */
	legacy_u32 previous_end = 0;
	legacy_s32 ascending = 1;
	for (legacy_s32 y = (target->top >> target->scale_shift);
		 y < (target->bottom >> target->scale_shift); y++) {
		legacy_u32 start = target->rows[y] + (target->left >> target->scale_shift);
		legacy_u32 end = target->rows[y] + (target->right >> target->scale_shift);
		if (start < previous_end || end > HIRES_ADDRESS_COUNT) {
			ascending = 0;
			break;
		}
		previous_end = end;
	}
	if (ascending) {
		return 1;
	}
	/* Row offsets can wrap at 64 KiB or arrive in arbitrary order. Two
	 * screen rows must never share any legacy cell, even partially. */
	legacy_u8 occupied[HIRES_ADDRESS_COUNT / LEGACY_BYTE_BITS] = {0};
	for (legacy_s32 y = (target->top >> target->scale_shift);
		 y < (target->bottom >> target->scale_shift); y++) {
		for (legacy_s32 x = (target->left >> target->scale_shift);
			 x < (target->right >> target->scale_shift); x++) {
			legacy_u16 offset = (legacy_u16)(target->rows[y] + x);
			legacy_u8 mask = (legacy_u8)(1U << (offset % LEGACY_BYTE_BITS));
			if ((occupied[offset / LEGACY_BYTE_BITS] & mask) != 0) {
				return 0;
			}
			occupied[offset / LEGACY_BYTE_BITS] |= mask;
		}
	}
	return 1;
}

legacy_s32 hires_raster_prepare(struct HIRES_RASTER_TARGET *target)
{
	memset(target, 0, sizeof(*target));
	if (hires_state.active == NULL) {
		return 0;
	}
	target->scale = hires_state.render_scale;
	target->scale_shift = hires_state.render_shift;
	target->scale_mask = hires_state.render_mask;
	target->cell_pixels = hires_state.render_cell_pixels;
	target->cell_shift = hires_state.render_cell_shift;
	target->width = hires_state.render_width;
	target->height = hires_state.render_height;
	target->left = hires_state.active_sprite.sprite_raster_left * hires_state.render_scale;
	target->right = hires_state.active_sprite.sprite_raster_right * hires_state.render_scale;
	target->top = hires_state.active_sprite.sprite_top * hires_state.render_scale;
	target->bottom = hires_state.active_sprite.sprite_bottom * hires_state.render_scale;
	if (target->left < 0) {
		target->left = 0;
	}
	if (target->right > hires_state.render_width) {
		target->right = hires_state.render_width;
	}
	if (target->top < 0) {
		target->top = 0;
	}
	if (target->bottom > hires_state.render_height) {
		target->bottom = hires_state.render_height;
	}
	if (target->left >= target->right || target->top >= target->bottom) {
		return 0;
	}
	for (legacy_s32 y = (target->top >> hires_state.render_shift);
		 y < (target->bottom >> hires_state.render_shift); y++) {
		target->rows[y] =
			LEGACY_READ_U16_LE(hires_state.active_sprite.sprite_lineofs + y * LEGACY_WORD_BYTES);
	}
	if (!hires_raster_rows_disjoint(target)) {
		return 0;
	}
	target->surface = hires_state.active;
	target->inverse_depth = hires_state.inverse_depth;
	target->depth_family = hires_state.depth_family;
	target->depth_left = hires_state.depth_left;
	target->depth_right = hires_state.depth_right;
	target->depth_top = hires_state.depth_top;
	target->depth_bottom = hires_state.depth_bottom;
	return 1;
}

legacy_s32 hires_raster_depth_test(struct HIRES_RASTER_CONTEXT *context, legacy_s32 x, legacy_s32 y,
								   legacy_f64 inverse_z, legacy_u32 family, legacy_s32 mode)
{
	const struct HIRES_RASTER_TARGET *target = context->target;
	if (x < target->depth_left || x >= target->depth_right || y < target->depth_top ||
		y >= target->depth_bottom || y < context->top || y >= context->bottom || !(inverse_z > 0) ||
		inverse_z > FLT_MAX || family == HIRES_DEPTH_FAMILY_NONE) {
		return 0;
	}
	return hires_test_depth(target->inverse_depth, target->depth_family,
							(size_t)y * target->width + x, inverse_z, family, mode);
}

void hires_coverage_pixel(struct HIRES_RASTER_CONTEXT *context, legacy_s32 x, legacy_s32 y,
						  legacy_f64 inverse_z, legacy_u32 family, legacy_s32 mode, legacy_u8 color,
						  const struct HIRES_DEPTH_PLANE *ground)
{
	legacy_f32 *depths;
	legacy_u32 *families;
	legacy_s32 width;
	if (context != NULL) {
		const struct HIRES_RASTER_TARGET *target = context->target;
		if (x < target->depth_left || x >= target->depth_right || y < target->depth_top ||
			y >= target->depth_bottom || y < context->top || y >= context->bottom) {
			return;
		}
		depths = target->inverse_depth;
		families = target->depth_family;
		width = target->width;
	} else {
		if (hires_state.active == NULL || x < hires_state.depth_left ||
			x >= hires_state.depth_right || y < hires_state.depth_top ||
			y >= hires_state.depth_bottom) {
			return;
		}
		depths = hires_state.inverse_depth;
		families = hires_state.depth_family;
		width = hires_state.render_width;
	}
	if (!(inverse_z > 0) || inverse_z > FLT_MAX || family == HIRES_DEPTH_FAMILY_NONE) {
		return;
	}
	size_t index = (size_t)y * width + x;
	if (families[index] == HIRES_DEPTH_FAMILY_NONE) {
		legacy_f64 ground_depth = ground->x_step * x + ground->y_step * y + ground->origin;
		if (inverse_z + HIRES_DEPTH_EPSILON_SCALE * FLT_EPSILON * ground_depth < ground_depth) {
			return;
		}
	}
	if (hires_test_depth(depths, families, index, inverse_z, family, mode)) {
		if (context != NULL) {
			hires_raster_pixel(context, x, y, color);
		} else {
			hires_pixel(x, y, color);
		}
	}
}

void hires_raster_pixel(struct HIRES_RASTER_CONTEXT *context, legacy_s32 x, legacy_s32 y,
						legacy_u8 color)
{
	const struct HIRES_RASTER_TARGET *target = context->target;
	if (x < target->left || x >= target->right || y < target->top || y >= target->bottom ||
		y < context->top || y >= context->bottom) {
		return;
	}
	legacy_u16 offset =
		(legacy_u16)(target->rows[(y >> target->scale_shift)] + (x >> target->scale_shift));
	legacy_u32 sample = (y & target->scale_mask) * target->scale + (x & target->scale_mask);
	if (gpu_overlay_tracking) {
		gpu_overlay_write(target->surface->base, offset, sample);
	}
	if (hires_paint_sample(target->surface, offset, sample, color) != 0) {
		context->cleared_argb_cells++;
	}
}

static void hires_raster_retire_argb(struct HIRES_RASTER_CONTEXT *context,
									 struct HIRES_SURFACE *surface, legacy_u16 offset,
									 const legacy_u32 *argb)
{
	legacy_u32 remaining = 0;
	for (legacy_s32 index = 0; index < context->target->cell_pixels; index++) {
		remaining |= argb[index];
	}
	if (remaining == 0) {
		surface->valid[offset] = HIRES_CELL_INDEXED;
		context->cleared_argb_cells++;
	}
}

/* Dispatch solid spans once, leaving patterned spans on their original loop.
 * Cells without overlays need only the ordered depth test and indexed write. */
static void hires_raster_solid_span(struct HIRES_RASTER_CONTEXT *context, legacy_s32 left,
									legacy_s32 right, legacy_s32 y, legacy_f64 inverse_z,
									legacy_f64 depth_step, legacy_u32 family, legacy_s32 depth_mode,
									legacy_u8 color, legacy_s32 depth_test)
{
	const struct HIRES_RASTER_TARGET *target = context->target;
	struct HIRES_SURFACE *surface = target->surface;
	const legacy_s32 shift = target->scale_shift;
	const legacy_s32 mask = target->scale_mask;
	const legacy_s32 cell_shift = target->cell_shift;
	const size_t depth_row = (size_t)y * target->width;
	if (gpu_overlay_tracking && !depth_test) {
		gpu_overlay_span(target, left, right, y);
	}
	legacy_u16 row = target->rows[y >> shift];
	legacy_u32 sample_row = (y & mask) << shift;
	for (legacy_s32 x = left; x < right;) {
		legacy_u16 offset = (legacy_u16)(row + (x >> shift));
		legacy_u32 sample = sample_row + (x & mask);
		legacy_s32 end = (x | mask) + 1;
		if (end > right) {
			end = right;
		}
		legacy_u8 *cell = surface->pixels + ((size_t)offset << cell_shift);
		legacy_u32 *argb = surface->valid[offset] == HIRES_CELL_ARGB
							   ? surface->argb + ((size_t)offset << cell_shift)
							   : NULL;
		legacy_s32 painted = 0;
		if (!depth_test) {
			memset(cell + sample, color, (size_t)(end - x));
			if (argb != NULL) {
				memset(argb + sample, 0, (size_t)(end - x) * sizeof(*argb));
				painted = 1;
			}
			x = end;
		} else if (argb == NULL) {
			for (; x < end; x++, sample++, inverse_z += depth_step) {
				if (inverse_z > 0 && inverse_z <= FLT_MAX &&
					hires_test_depth(target->inverse_depth, target->depth_family, depth_row + x,
									 inverse_z, family, depth_mode)) {
					cell[sample] = color;
					if (gpu_overlay_tracking) {
						gpu_overlay_write(surface->base, offset, sample);
					}
				}
			}
		} else {
			for (; x < end; x++, sample++, inverse_z += depth_step) {
				if (inverse_z > 0 && inverse_z <= FLT_MAX &&
					hires_test_depth(target->inverse_depth, target->depth_family, depth_row + x,
									 inverse_z, family, depth_mode)) {
					cell[sample] = color;
					if (gpu_overlay_tracking) {
						gpu_overlay_write(surface->base, offset, sample);
					}
					argb[sample] = 0;
					painted = 1;
				}
			}
		}
		if (painted) {
			hires_raster_retire_argb(context, surface, offset, argb);
		}
	}
}

void hires_raster_span(struct HIRES_RASTER_CONTEXT *context, legacy_s32 left, legacy_s32 right,
					   legacy_s32 y, legacy_f64 inverse_z, legacy_f64 depth_step, legacy_u32 family,
					   legacy_s32 depth_mode, legacy_u16 color, legacy_u16 alternate,
					   legacy_u16 pattern, legacy_s32 paint_mode, legacy_s32 depth_test)
{
	const struct HIRES_RASTER_TARGET *target = context->target;
	if (y < target->top || y >= target->bottom || y < context->top || y >= context->bottom) {
		return;
	}
	legacy_s32 clip_left = target->left;
	legacy_s32 clip_right = target->right;
	if (depth_test) {
		if (family == HIRES_DEPTH_FAMILY_NONE || y < target->depth_top ||
			y >= target->depth_bottom) {
			return;
		}
		if (clip_left < target->depth_left) {
			clip_left = target->depth_left;
		}
		if (clip_right > target->depth_right) {
			clip_right = target->depth_right;
		}
	}
	if (right > clip_right) {
		right = clip_right;
	}
	/* Repeated addition is intentional: clipping must not change rounding
	 * from the original per-pixel interpolation, including patterned holes. */
	while (left < clip_left && left < right) {
		inverse_z += depth_step;
		left++;
	}
	if (paint_mode == HIRES_PAINT_SOLID) {
		hires_raster_solid_span(context, left, right, y, inverse_z, depth_step, family, depth_mode,
								(legacy_u8)color, depth_test);
		return;
	}
	const legacy_s32 shift = target->scale_shift;
	const legacy_s32 mask = target->scale_mask;
	const legacy_s32 cell_shift = target->cell_shift;
	const size_t depth_row = (size_t)y * target->width;
	legacy_u16 row = target->rows[y >> shift];
	legacy_u32 sample_row = (y & mask) << shift;
	legacy_u32 pattern_row = (y & HIRES_PATTERN_ROW_MASK) == 0 ? HIRES_PATTERN_WIDTH : 0U;
	struct HIRES_SURFACE *surface = target->surface;
	for (legacy_s32 x = left; x < right;) {
		legacy_u16 offset = (legacy_u16)(row + (x >> shift));
		legacy_u32 first_sample = sample_row + (x & mask);
		legacy_s32 end = (x | mask) + 1;
		if (end > right) {
			end = right;
		}
		legacy_u8 *cell = surface->pixels + ((size_t)offset << cell_shift);
		legacy_u32 *argb = surface->valid[offset] == HIRES_CELL_ARGB
							   ? surface->argb + ((size_t)offset << cell_shift)
							   : NULL;
		legacy_u32 sample = first_sample;
		legacy_s32 painted = 0;
		for (; x < end; x++, sample++, inverse_z += depth_step) {
			legacy_u8 sample_color = (legacy_u8)color;
			if (paint_mode != HIRES_PAINT_SOLID) {
				if ((pattern & (1U << (pattern_row + (HIRES_PATTERN_WIDTH - 1U) -
									   (x & HIRES_PATTERN_COLUMN_MASK)))) != 0) {
					if (paint_mode == HIRES_PAINT_ALTERNATE) {
						sample_color = (legacy_u8)alternate;
					}
				} else if (paint_mode != HIRES_PAINT_ALTERNATE) {
					continue;
				}
			}
			if (depth_test && (!(inverse_z > 0) || inverse_z > FLT_MAX ||
							   !hires_test_depth(target->inverse_depth, target->depth_family,
												 depth_row + x, inverse_z, family, depth_mode))) {
				continue;
			}
			cell[sample] = sample_color;
			if (argb != NULL) {
				argb[sample] = 0;
				painted = 1;
			}
		}
		/* No other job can change this cell. Retire full-color coverage once
		 * after this run, rather than scanning its samples per pixel. */
		if (painted) {
			hires_raster_retire_argb(context, surface, offset, argb);
		}
	}
}

/* Common opaque indexed rows have no overlay to retire. Emit full sample
 * words directly between the two partial edge cells. */
static void hires_raster_indexed_span(const struct HIRES_RASTER_TARGET *target, legacy_s32 left,
									  legacy_s32 right, legacy_s32 y, legacy_u8 color)
{
	const legacy_s32 shift = target->scale_shift, mask = target->scale_mask;
	const legacy_s32 cell_shift = target->cell_shift;
	const legacy_u16 row = target->rows[y >> shift];
	const legacy_u32 sample_row = (y & mask) << shift;
	legacy_u8 *pixels = target->surface->pixels;
	legacy_s32 x = left;
	if (shift == 0) {
		while (x < right) {
			legacy_u16 offset = (legacy_u16)(row + x);
			legacy_u32 count = HIRES_ADDRESS_COUNT - offset;
			if (count > (legacy_u32)(right - x)) {
				count = (legacy_u32)(right - x);
			}
			memset(pixels + offset, color, count);
			x += count;
		}
		return;
	}
	if ((x & mask) != 0 && x < right) {
		legacy_s32 end = (x | mask) + 1;
		if (end > right) {
			end = right;
		}
		legacy_u8 *cell = pixels + ((size_t)(legacy_u16)(row + (x >> shift)) << cell_shift);
		memset(cell + sample_row + (x & mask), color, (size_t)(end - x));
		x = end;
	}
	legacy_s32 aligned_right = right & ~mask;
	if (target->scale == HIRES_SCALE) {
		legacy_u32 word = color * (LEGACY_U32_MAX / LEGACY_U8_MAX);
		for (; x < aligned_right; x += HIRES_SCALE) {
			legacy_u8 *cell = pixels + ((size_t)(legacy_u16)(row + (x >> shift)) << cell_shift);
			memcpy(cell + sample_row, &word, sizeof(word));
		}
	} else {
		legacy_u16 word = color * (LEGACY_U16_MAX / LEGACY_U8_MAX);
		for (; x < aligned_right; x += HIRES_MEDIUM_SCALE) {
			legacy_u8 *cell = pixels + ((size_t)(legacy_u16)(row + (x >> shift)) << cell_shift);
			memcpy(cell + sample_row, &word, sizeof(word));
		}
	}
	if (x < right) {
		legacy_u8 *cell = pixels + ((size_t)(legacy_u16)(row + (x >> shift)) << cell_shift);
		memset(cell + sample_row, color, (size_t)(right - x));
	}
}

void hires_raster_resolved_span(struct HIRES_RASTER_CONTEXT *context, legacy_s32 left,
								legacy_s32 right, legacy_s32 y, legacy_f64 inverse_z,
								legacy_f64 depth_step, legacy_u32 family, legacy_u16 color,
								legacy_u16 alternate, legacy_u16 pattern, legacy_s32 paint_mode)
{
	const struct HIRES_RASTER_TARGET *target = context->target;
	if (gpu_overlay_tracking) {
		gpu_overlay_span(target, left, right, y);
	}
	struct HIRES_SURFACE *surface = target->surface;
	const legacy_s32 shift = target->scale_shift;
	const legacy_s32 mask = target->scale_mask;
	const legacy_s32 cell_shift = target->cell_shift;
	legacy_u16 row = target->rows[y >> shift];
	legacy_u32 sample_row = (y & mask) << shift;
	legacy_u32 pattern_row = (y & HIRES_PATTERN_ROW_MASK) == 0 ? HIRES_PATTERN_WIDTH : 0U;
	legacy_f32 *depths = target->inverse_depth + (size_t)y * target->width;
	legacy_u32 *families = target->depth_family + (size_t)y * target->width;
	/* Separate contiguous depth output from the legacy-cell pixel layout.
	 * Independent samples permit vectorization without a serial recurrence. */
	for (legacy_s32 x = left; x < right; x++) {
		depths[x] = (legacy_f32)(inverse_z + (x - left) * depth_step);
		families[x] = family;
	}
	if (paint_mode == HIRES_PAINT_SOLID && surface->argb_cells == 0) {
		hires_raster_indexed_span(target, left, right, y, (legacy_u8)color);
		return;
	}
	legacy_u32 packed_color = (legacy_u8)color * (LEGACY_U32_MAX / LEGACY_U8_MAX);
	for (legacy_s32 x = left; x < right;) {
		legacy_u16 offset = (legacy_u16)(row + (x >> shift));
		legacy_u32 sample = sample_row + (x & mask);
		legacy_s32 end = (x | mask) + 1;
		if (end > right) {
			end = right;
		}
		legacy_u8 *cell = surface->pixels + ((size_t)offset << cell_shift);
		legacy_u32 *argb = surface->valid[offset] == HIRES_CELL_ARGB
							   ? surface->argb + ((size_t)offset << cell_shift)
							   : NULL;
		if (argb != NULL) {
			memset(argb + sample, 0, (size_t)(end - x) * sizeof(*argb));
		}
		if (paint_mode == HIRES_PAINT_SOLID) {
			if (end - x == HIRES_SCALE) {
				memcpy(cell + sample, &packed_color, HIRES_SCALE);
			} else {
				for (legacy_s32 pixel = x; pixel < end; pixel++) {
					cell[sample + pixel - x] = (legacy_u8)color;
				}
			}
			x = end;
		} else {
			for (; x < end; x++, sample++) {
				legacy_u32 bit =
					pattern_row + HIRES_PATTERN_WIDTH - 1U - (x & HIRES_PATTERN_COLUMN_MASK);
				cell[sample] = (legacy_u8)((pattern & (1U << bit)) != 0U ? alternate : color);
			}
		}
		if (argb != NULL) {
			hires_raster_retire_argb(context, surface, offset, argb);
		}
	}
}

/* Disjoint logical rows can replace contiguous cell blocks without repeating
 * sprite clipping, row lookup, and overlay bookkeeping for every cell. */
static void hires_raster_replace_row(struct HIRES_RASTER_CONTEXT *context, legacy_s32 y,
									 const legacy_u8 *samples, legacy_u8 color)
{
	const struct HIRES_RASTER_TARGET *target = context->target;
	const legacy_s32 shift = target->scale_shift;
	if (y < (context->top >> shift) || y >= (context->bottom >> shift) ||
		y < (target->top >> shift) || y >= (target->bottom >> shift)) {
		return;
	}
	if (gpu_overlay_tracking) {
		for (legacy_s32 row = 0; row < target->scale; row++) {
			gpu_overlay_span(target, target->left, target->right, y * target->scale + row);
		}
	}
	struct HIRES_SURFACE *surface = target->surface;
	legacy_u32 remaining = (legacy_u32)(target->right - target->left) >> shift;
	legacy_u16 offset = (legacy_u16)(target->rows[y] + (target->left >> shift));
	while (remaining != 0) {
		legacy_u32 count = HIRES_ADDRESS_COUNT - offset;
		if (count > remaining) {
			count = remaining;
		}
		legacy_u8 *output = surface->pixels + ((size_t)offset << target->cell_shift);
		size_t bytes = (size_t)count << target->cell_shift;
		if (samples != NULL) {
			memcpy(output, samples, bytes);
			samples += bytes;
		} else {
			memset(output, color, bytes);
		}
		if (surface->argb_cells != 0) {
			for (legacy_u32 cell = 0; cell < count; cell++) {
				context->cleared_argb_cells += surface->valid[offset + cell] == HIRES_CELL_ARGB;
			}
		}
		memset(surface->valid + offset, HIRES_CELL_INDEXED, count);
		remaining -= count;
		offset = 0;
	}
}

void hires_raster_fill_row(struct HIRES_RASTER_CONTEXT *context, legacy_s32 y, legacy_u8 color)
{
	hires_raster_replace_row(context, y, NULL, color);
}

void hires_raster_write_row(struct HIRES_RASTER_CONTEXT *context, legacy_s32 y,
							const legacy_u8 *samples)
{
	hires_raster_replace_row(context, y, samples, 0);
}

void hires_raster_finish(const struct HIRES_RASTER_TARGET *target, legacy_u32 cleared_argb_cells)
{
	target->surface->argb_cells -= cleared_argb_cells;
}
