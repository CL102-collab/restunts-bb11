#include "hires.h"
#include "render_workers.h"
#include "../platform.h"
#include "../shape2d/shape2d.h"
#include "../shape2d/shape2d_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <float.h>

#define HIRES_ADDRESS_COUNT 65536UL
#define HIRES_CELL_PIXELS (HIRES_SCALE * HIRES_SCALE)
#define HIRES_LOGICAL_WIDTH (HIRES_WIDTH / HIRES_SCALE)
#define HIRES_LOGICAL_HEIGHT (HIRES_HEIGHT / HIRES_SCALE)
#define HIRES_MAXIMUM_SCALE_SHIFT 2
#define HIRES_MEDIUM_SCALE_SHIFT 1
#define HIRES_CELL_DIMENSIONS 2
#define HIRES_DEPTH_EPSILON_SCALE 4
#define HIRES_MENU_WHITE_INDEX 15

enum HIRES_CELL_STATE { HIRES_CELL_EMPTY, HIRES_CELL_INDEXED, HIRES_CELL_ARGB };

/* Each legacy pixel carries scale*scale independently rasterized samples.
 * Keeping these attached to byte offsets lets ordinary clipped sprite copies,
 * saved backgrounds, and dashboard masks carry their detail with them. */
struct HIRES_SURFACE {
	const legacy_u8 *base;
	legacy_u8 *pixels;
	legacy_u32 *argb;
	legacy_u32 argb_cells;
	legacy_u8 valid[HIRES_ADDRESS_COUNT];
	struct HIRES_SURFACE *next;
};

static struct HIRES_SURFACE *surfaces;
static struct HIRES_SURFACE *active;
static struct SPRITE active_sprite;
static legacy_s32 enabled;
static legacy_s32 render_scale = HIRES_SCALE;
static legacy_s32 render_shift = HIRES_MAXIMUM_SCALE_SHIFT;
static legacy_s32 render_mask = HIRES_SCALE - 1;
static legacy_s32 render_cell_pixels = HIRES_CELL_PIXELS;
static legacy_s32 render_cell_shift = HIRES_MAXIMUM_SCALE_SHIFT * HIRES_CELL_DIMENSIONS;
static legacy_s32 render_width = HIRES_WIDTH;
static legacy_s32 render_height = HIRES_HEIGHT;
static legacy_u32 generation;
static legacy_u8 *framebuffer;
static legacy_u32 *argb_framebuffer;
static legacy_f32 *inverse_depth;
static legacy_u32 *depth_family;
static legacy_s32 depth_left, depth_right, depth_top, depth_bottom;

static void *hires_allocate(size_t size)
{
	void *result = calloc(1, size);
	if (result == NULL) {
		fputs("Cannot allocate high-resolution framebuffer\n", stderr);
		dos_process_exit(1);
	}
	return result;
}

static struct HIRES_SURFACE *hires_find(const legacy_u8 *base)
{
	for (struct HIRES_SURFACE *surface = surfaces; surface != NULL; surface = surface->next) {
		if (surface->base == base) {
			return surface;
		}
	}
	return NULL;
}

static struct HIRES_SURFACE *hires_create(const legacy_u8 *base)
{
	struct HIRES_SURFACE *surface = hires_find(base);
	if (surface == NULL) {
		surface = hires_allocate(sizeof(*surface));
		surface->base = base;
		surface->pixels = hires_allocate(HIRES_ADDRESS_COUNT * render_cell_pixels);
		surface->next = surfaces;
		surfaces = surface;
	}
	return surface;
}

static legacy_u8 *hires_cell(struct HIRES_SURFACE *surface, legacy_u16 offset)
{
	legacy_u8 *cell = surface->pixels + ((size_t)offset << render_cell_shift);
	if (!surface->valid[offset]) {
		memset(cell, surface->base[offset], render_cell_pixels);
		surface->valid[offset] = HIRES_CELL_INDEXED;
	}
	return cell;
}

static void hires_clear_argb(struct HIRES_SURFACE *surface, legacy_u16 offset)
{
	if (surface->valid[offset] == HIRES_CELL_ARGB) {
		surface->argb_cells--;
		surface->valid[offset] = HIRES_CELL_INDEXED;
	}
}

static void hires_release_unused_argb(struct HIRES_SURFACE *surface)
{
	if (surface->argb != NULL && surface->argb_cells == 0) {
		free(surface->argb);
		surface->argb = NULL;
	}
}

static legacy_s32 hires_allocate_argb(struct HIRES_SURFACE *surface)
{
	if (surface->argb == NULL) {
		/* ARGB writers initialize each cell before marking it valid. A small
		 * overlay must not clear the entire four-megabyte buffer every frame. */
		surface->argb =
			malloc((size_t)HIRES_ADDRESS_COUNT * render_cell_pixels * sizeof(*surface->argb));
	}
	return surface->argb != NULL;
}

static void hires_depth_reset(void)
{
	depth_left = depth_right = depth_top = depth_bottom = 0;
}

static void hires_release_buffers(void)
{
	while (surfaces != NULL) {
		struct HIRES_SURFACE *next = surfaces->next;
		free(surfaces->pixels);
		free(surfaces->argb);
		free(surfaces);
		surfaces = next;
	}
	free(framebuffer);
	free(argb_framebuffer);
	argb_framebuffer = NULL;
	framebuffer = NULL;
	free(inverse_depth);
	free(depth_family);
	inverse_depth = NULL;
	depth_family = NULL;
	hires_depth_reset();
	active = NULL;
	generation++;
}

static void hires_configure_scale(legacy_s32 scale)
{
	render_scale = scale;
	render_shift = scale == HIRES_SCALE			 ? HIRES_MAXIMUM_SCALE_SHIFT
				   : scale == HIRES_MEDIUM_SCALE ? HIRES_MEDIUM_SCALE_SHIFT
												 : 0;
	render_mask = scale - 1;
	render_cell_pixels = scale * scale;
	render_cell_shift = render_shift * HIRES_CELL_DIMENSIONS;
	render_width = HIRES_LOGICAL_WIDTH * scale;
	render_height = HIRES_LOGICAL_HEIGHT * scale;
}

void hires_shutdown(void)
{
	render_workers_shutdown();
	hires_release_buffers();
	hires_configure_scale(HIRES_SCALE);
	enabled = 0;
}

void hires_set_render_scale(legacy_s32 scale)
{
	if (active != NULL || scale == render_scale ||
		(scale != HIRES_SCALE && scale != HIRES_MEDIUM_SCALE && scale != HIRES_MINIMUM_SCALE)) {
		return;
	}
	/* Callers have joined all raster work. Retire every representation together
	 * so no saved sprite, overlay or depth row retains the preceding stride. */
	hires_release_buffers();
	hires_configure_scale(scale);
}

legacy_s32 hires_render_scale(void)
{
	return render_scale;
}

legacy_s32 hires_render_width(void)
{
	return render_width;
}

legacy_s32 hires_render_height(void)
{
	return render_height;
}

void hires_set_enabled(legacy_s32 value)
{
	value = value != 0;
	if (value == enabled) {
		return;
	}
	hires_shutdown();
	enabled = value;
}

legacy_s32 hires_enabled(void)
{
	return enabled;
}

legacy_u32 hires_generation(void)
{
	return generation;
}

legacy_s32 hires_begin(const struct SPRITE *target)
{
	if (!enabled || active != NULL) {
		return 0;
	}
	const legacy_u8 *base =
		dos_memory_make_pointer(dos_memory_pointer_segment(target->sprite_bitmapptr), 0);
	active = hires_create(base);
	active_sprite = *target;
	hires_depth_reset();
	/* Seed the background before the legacy 3D pass overwrites its pixels. */
	for (legacy_u32 y = target->sprite_top; y < target->sprite_bottom && y < HIRES_LOGICAL_HEIGHT;
		 y++) {
		legacy_u16 row = LEGACY_READ_U16_LE(target->sprite_lineofs + y * LEGACY_WORD_BYTES);
		for (legacy_u32 x = target->sprite_raster_left;
			 x < target->sprite_raster_right && x < HIRES_LOGICAL_WIDTH; x++) {
			hires_cell(active, (legacy_u16)(row + x));
		}
	}
	return 1;
}

legacy_s32 hires_begin_argb(const struct SPRITE *target)
{
	if (!hires_begin(target)) {
		return 0;
	}
	if (!hires_allocate_argb(active)) {
		hires_end();
		return 0;
	}
	return 1;
}

void hires_end(void)
{
	hires_depth_reset();
	if (active != NULL) {
		hires_release_unused_argb(active);
		active = NULL;
		generation++;
	}
}

/* Keep depth across the scene so overlapping tiles cannot paint through one
 * another. Family zero invalidates old depth without clearing the float array.
 * Bounds are clipped to the active drawing target. */
void hires_depth_begin(legacy_s32 left, legacy_s32 right, legacy_s32 top, legacy_s32 bottom)
{
	hires_depth_reset();
	if (active == NULL) {
		return;
	}
	legacy_s32 clip_left = active_sprite.sprite_raster_left * render_scale;
	legacy_s32 clip_right = active_sprite.sprite_raster_right * render_scale;
	legacy_s32 clip_top = active_sprite.sprite_top * render_scale;
	legacy_s32 clip_bottom = active_sprite.sprite_bottom * render_scale;
	if (clip_left < 0) {
		clip_left = 0;
	}
	if (clip_right > render_width) {
		clip_right = render_width;
	}
	if (clip_top < 0) {
		clip_top = 0;
	}
	if (clip_bottom > render_height) {
		clip_bottom = render_height;
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
	if (inverse_depth == NULL) {
		inverse_depth =
			hires_allocate((size_t)render_width * render_height * sizeof(*inverse_depth));
		depth_family = hires_allocate((size_t)render_width * render_height * sizeof(*depth_family));
	}
	depth_left = left;
	depth_right = right;
	depth_top = top;
	depth_bottom = bottom;
	for (legacy_s32 y = top; y < bottom; y++) {
		memset(depth_family + (size_t)y * render_width + left, 0,
			   (size_t)(right - left) * sizeof(*depth_family));
	}
}

static legacy_s32 hires_test_depth(legacy_f32 *depths, legacy_u32 *families, size_t index,
								   legacy_f64 inverse_z, legacy_u32 family, legacy_s32 mode)
{
	if (mode != HIRES_DEPTH_SURFACE && families[index] == family) {
		/* Resource overlays may sit behind their supporting surface. Keep
		 * that surface's occlusion depth while honoring authored paint order.
		 * Unsorted shapes share a family, so nearer surfaces must also advance
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
	if (active == NULL || x < depth_left || x >= depth_right || y < depth_top ||
		y >= depth_bottom || !(inverse_z > 0) || inverse_z > FLT_MAX ||
		family == HIRES_DEPTH_FAMILY_NONE) {
		return 0;
	}
	return hires_test_depth(inverse_depth, depth_family, (size_t)y * render_width + x, inverse_z,
							family, mode);
}

/* The caller owns the whole legacy cell and accounts for cleared ARGB cells.
 * Raster workers accumulate locally instead of changing the surface counter. */
static legacy_u32 hires_paint_sample(struct HIRES_SURFACE *surface, legacy_u16 offset,
									 legacy_u32 sample, legacy_u8 color)
{
	legacy_u8 *cell = surface->pixels + ((size_t)offset << render_cell_shift);
	cell[sample] = color;
	if (surface->valid[offset] == HIRES_CELL_ARGB) {
		legacy_u32 *argb = surface->argb + ((size_t)offset << render_cell_shift);
		argb[sample] = 0;
		legacy_u32 remaining = 0;
		for (legacy_s32 index = 0; index < render_cell_pixels; index++) {
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
	if (active == NULL || x < 0 || y < 0 || x >= render_width || y >= render_height ||
		x < active_sprite.sprite_raster_left * render_scale ||
		x >= active_sprite.sprite_raster_right * render_scale ||
		y < active_sprite.sprite_top * render_scale ||
		y >= active_sprite.sprite_bottom * render_scale) {
		return;
	}
	legacy_u16 row =
		LEGACY_READ_U16_LE(active_sprite.sprite_lineofs + (y >> render_shift) * LEGACY_WORD_BYTES);
	legacy_u16 offset = (legacy_u16)(row + (x >> render_shift));
	hires_cell(active, offset);
	legacy_u32 sample = (y & render_mask) * render_scale + (x & render_mask);
	if (hires_paint_sample(active, offset, sample, color) != 0) {
		active->argb_cells--;
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
	if (active == NULL) {
		return 0;
	}
	target->scale = render_scale;
	target->scale_shift = render_shift;
	target->scale_mask = render_mask;
	target->cell_pixels = render_cell_pixels;
	target->cell_shift = render_cell_shift;
	target->width = render_width;
	target->height = render_height;
	target->left = active_sprite.sprite_raster_left * render_scale;
	target->right = active_sprite.sprite_raster_right * render_scale;
	target->top = active_sprite.sprite_top * render_scale;
	target->bottom = active_sprite.sprite_bottom * render_scale;
	if (target->left < 0) {
		target->left = 0;
	}
	if (target->right > render_width) {
		target->right = render_width;
	}
	if (target->top < 0) {
		target->top = 0;
	}
	if (target->bottom > render_height) {
		target->bottom = render_height;
	}
	if (target->left >= target->right || target->top >= target->bottom) {
		return 0;
	}
	for (legacy_s32 y = (target->top >> render_shift); y < (target->bottom >> render_shift); y++) {
		target->rows[y] = LEGACY_READ_U16_LE(active_sprite.sprite_lineofs + y * LEGACY_WORD_BYTES);
	}
	if (!hires_raster_rows_disjoint(target)) {
		return 0;
	}
	target->surface = active;
	target->inverse_depth = inverse_depth;
	target->depth_family = depth_family;
	target->depth_left = depth_left;
	target->depth_right = depth_right;
	target->depth_top = depth_top;
	target->depth_bottom = depth_bottom;
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
		if (active == NULL || x < depth_left || x >= depth_right || y < depth_top ||
			y >= depth_bottom) {
			return;
		}
		depths = inverse_depth;
		families = depth_family;
		width = render_width;
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
				}
			}
		} else {
			for (; x < end; x++, sample++, inverse_z += depth_step) {
				if (inverse_z > 0 && inverse_z <= FLT_MAX &&
					hires_test_depth(target->inverse_depth, target->depth_family, depth_row + x,
									 inverse_z, family, depth_mode)) {
					cell[sample] = color;
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

void hires_fill_pixel(legacy_s32 x, legacy_s32 y, legacy_u8 color)
{
	if (active == NULL || x < 0 || y < 0 || x >= HIRES_LOGICAL_WIDTH || y >= HIRES_LOGICAL_HEIGHT ||
		x < active_sprite.sprite_raster_left || x >= active_sprite.sprite_raster_right ||
		y < active_sprite.sprite_top || y >= active_sprite.sprite_bottom) {
		return;
	}
	legacy_u16 row = LEGACY_READ_U16_LE(active_sprite.sprite_lineofs + y * LEGACY_WORD_BYTES);
	legacy_u16 offset = (legacy_u16)(row + x);
	memset(hires_cell(active, offset), color, render_cell_pixels);
	hires_clear_argb(active, offset);
}

void hires_write_pixel(legacy_s32 x, legacy_s32 y, const legacy_u8 *samples)
{
	if (active == NULL || x < 0 || y < 0 || x >= HIRES_LOGICAL_WIDTH || y >= HIRES_LOGICAL_HEIGHT ||
		x < active_sprite.sprite_raster_left || x >= active_sprite.sprite_raster_right ||
		y < active_sprite.sprite_top || y >= active_sprite.sprite_bottom) {
		return;
	}
	legacy_u16 row = LEGACY_READ_U16_LE(active_sprite.sprite_lineofs + y * LEGACY_WORD_BYTES);
	legacy_u16 offset = (legacy_u16)(row + x);
	memcpy(hires_cell(active, offset), samples, render_cell_pixels);
	hires_clear_argb(active, offset);
}

static legacy_u32 *hires_argb_sample(legacy_s32 x, legacy_s32 y)
{
	if (active == NULL || active->argb == NULL || x < 0 || y < 0 || x >= render_width ||
		y >= render_height || x < active_sprite.sprite_raster_left * render_scale ||
		x >= active_sprite.sprite_raster_right * render_scale ||
		y < active_sprite.sprite_top * render_scale ||
		y >= active_sprite.sprite_bottom * render_scale) {
		return NULL;
	}
	legacy_u16 row =
		LEGACY_READ_U16_LE(active_sprite.sprite_lineofs + (y >> render_shift) * LEGACY_WORD_BYTES);
	legacy_u16 offset = (legacy_u16)(row + (x >> render_shift));
	legacy_u32 *cell = active->argb + ((size_t)offset << render_cell_shift);
	if (active->valid[offset] != HIRES_CELL_ARGB) {
		memset(cell, 0, render_cell_pixels * sizeof(*cell));
		active->valid[offset] = HIRES_CELL_ARGB;
		active->argb_cells++;
	}
	return cell + (y & render_mask) * render_scale + (x & render_mask);
}

void hires_argb_pixel(legacy_s32 x, legacy_s32 y, legacy_u32 color)
{
	legacy_u32 *sample = hires_argb_sample(x, y);
	if (sample != NULL) {
		*sample = color;
	}
}

legacy_s32 hires_shadow_begin(void)
{
	return enabled && active != NULL && hires_allocate_argb(active);
}

void hires_shadow_pixel(legacy_s32 x, legacy_s32 y, legacy_u8 opacity)
{
	if (opacity == 0) {
		return;
	}
	legacy_u32 *sample = hires_argb_sample(x, y);
	if (sample == NULL) {
		return;
	}
	/* Store black over the existing straight-alpha overlay. Its transparent
	 * portion continues to reveal the unchanged indexed sample underneath. */
	legacy_u32 retained = (*sample >> LEGACY_THREE_BYTE_BITS) * (LEGACY_U8_MAX - opacity);
	legacy_u32 alpha = opacity + retained / LEGACY_U8_MAX;
	legacy_u32 color = alpha << LEGACY_THREE_BYTE_BITS;
	for (legacy_u32 shift = 0; shift < LEGACY_THREE_BYTE_BITS; shift += LEGACY_BYTE_BITS) {
		legacy_u32 channel =
			((*sample >> shift) & LEGACY_U8_MAX) * retained / (alpha * LEGACY_U8_MAX);
		color |= channel << shift;
	}
	*sample = color;
}

void hires_write(const legacy_u8 *base, legacy_u16 offset, legacy_u8 color)
{
	(void)color;
	if (!enabled || active != NULL) {
		return;
	}
	struct HIRES_SURFACE *surface = hires_find(base);
	if (surface != NULL && surface->valid[offset]) {
		/* A 2D write replaces the whole pixel even when its colour is unchanged. */
		hires_clear_argb(surface, offset);
		surface->valid[offset] = HIRES_CELL_EMPTY;
		hires_release_unused_argb(surface);
		generation++;
	}
}

static void hires_raster_argb(struct HIRES_SURFACE *dst, legacy_u16 dest, struct HIRES_SURFACE *src,
							  legacy_u16 source, const legacy_u8 *samples, legacy_s16 operation,
							  const legacy_u8 *palette)
{
	if (!hires_allocate_argb(dst)) {
		return;
	}
	/* Snapshot first: the source and destination may be the same pixel. */
	legacy_u32 source_argb[HIRES_CELL_PIXELS] = {0};
	if (src != NULL && src->valid[source] == HIRES_CELL_ARGB) {
		memcpy(source_argb, src->argb + ((size_t)source << render_cell_shift),
			   render_cell_pixels * sizeof(*source_argb));
	}
	legacy_u32 *cell = dst->argb + ((size_t)dest << render_cell_shift);
	if (dst->valid[dest] != HIRES_CELL_ARGB) {
		memset(cell, 0, render_cell_pixels * sizeof(*source_argb));
	}
	legacy_u32 remaining = 0;
	for (legacy_s32 sample = 0; sample < render_cell_pixels; sample++) {
		legacy_u8 value = samples[sample];
		if (operation == SHAPE2D_RASTER_AND) {
			if (value != LEGACY_U8_MAX) {
				cell[sample] = 0;
			}
		} else if (operation == SHAPE2D_RASTER_OR) {
			if (value != 0) {
				cell[sample] = 0;
			}
		} else if (operation != SHAPE2D_RASTER_MAP || palette[value] != LEGACY_U8_MAX) {
			cell[sample] = operation != SHAPE2D_RASTER_MAP || palette[value] == value
							   ? source_argb[sample]
							   : 0;
		}
		remaining |= cell[sample];
	}
	hires_clear_argb(dst, dest);
	if (remaining != 0) {
		dst->valid[dest] = HIRES_CELL_ARGB;
		dst->argb_cells++;
	}
}

void hires_raster(const legacy_u8 *destination, legacy_u16 destination_offset,
				  const legacy_u8 *source, legacy_u16 source_offset, legacy_u16 count,
				  legacy_s16 operation, const legacy_u8 *palette)
{
	if (!enabled || active != NULL || count == 0) {
		return;
	}
	struct HIRES_SURFACE *src = hires_find(source);
	struct HIRES_SURFACE *dst = hires_find(destination);
	if (src == NULL && dst == NULL) {
		return;
	}
	if (dst == NULL) {
		dst = hires_create(destination);
	}
	legacy_s32 argb_present = dst->argb_cells != 0 || (src != NULL && src->argb_cells != 0);
	for (legacy_u32 index = 0; index < count; index++) {
		legacy_u16 so = (legacy_u16)(source_offset + index);
		legacy_u16 dest = (legacy_u16)(destination_offset + index);
		legacy_u8 source_color = source[so];
		legacy_u8 *cell = hires_cell(dst, dest);
		/* Match the game's forward traversal, including overlapping copies. */
		legacy_u8 samples[HIRES_CELL_PIXELS];
		if (src != NULL && src->valid[so]) {
			memcpy(samples, src->pixels + ((size_t)so << render_cell_shift), render_cell_pixels);
		} else {
			memset(samples, source_color, render_cell_pixels);
		}
		if (argb_present && (dst->valid[dest] == HIRES_CELL_ARGB ||
							 (src != NULL && src->valid[so] == HIRES_CELL_ARGB))) {
			hires_raster_argb(dst, dest, src, so, samples, operation, palette);
		}
		for (legacy_s32 sample = 0; sample < render_cell_pixels; sample++) {
			legacy_u8 value = samples[sample];
			if (operation == SHAPE2D_RASTER_AND) {
				cell[sample] &= value;
			} else if (operation == SHAPE2D_RASTER_OR) {
				cell[sample] |= value;
			} else if (operation == SHAPE2D_RASTER_MAP) {
				value = palette[value];
				if (value != LEGACY_U8_MAX) {
					cell[sample] = value;
				}
			} else {
				cell[sample] = value;
			}
		}
	}
	hires_release_unused_argb(dst);
	generation++;
}

void hires_forget(const void *base)
{
	struct HIRES_SURFACE **link = &surfaces;
	while (*link != NULL) {
		struct HIRES_SURFACE *surface = *link;
		if (surface->base == base) {
			if (surface == active) {
				active = NULL;
			}
			*link = surface->next;
			free(surface->pixels);
			free(surface->argb);
			free(surface);
			generation++;
			return;
		}
		link = &surface->next;
	}
}

void hires_forget_range(const void *base, legacy_u32 size)
{
	if (!enabled || size == 0) {
		return;
	}
	uintptr_t start = (uintptr_t)base;
	uintptr_t end = start + size;
	struct HIRES_SURFACE **link = &surfaces;
	while (*link != NULL) {
		struct HIRES_SURFACE *surface = *link;
		uintptr_t address = (uintptr_t)surface->base;
		if (address >= start && address < end) {
			if (surface == active) {
				active = NULL;
			}
			*link = surface->next;
			free(surface->pixels);
			free(surface->argb);
			free(surface);
			generation++;
			continue;
		}
		if (address < start && start - address < HIRES_ADDRESS_COUNT) {
			/* Paragraph aliases may start before the released bytes. Do not
			 * discard neighbouring live sprites that share the 64 KiB window. */
			size_t first = start - address;
			size_t last = end - address;
			if (last > HIRES_ADDRESS_COUNT) {
				last = HIRES_ADDRESS_COUNT;
			}
			for (size_t offset = first; offset < last; offset++) {
				hires_clear_argb(surface, (legacy_u16)offset);
			}
			memset(surface->valid + first, 0, last - first);
			hires_release_unused_argb(surface);
			generation++;
		}
		link = &surface->next;
	}
}

const legacy_u8 *hires_framebuffer(const legacy_u8 *legacy, legacy_s32 *width, legacy_s32 *height)
{
	*width = enabled ? render_width : HIRES_LOGICAL_WIDTH;
	*height = enabled ? render_height : HIRES_LOGICAL_HEIGHT;
	if (!enabled) {
		return legacy;
	}
	if (framebuffer == NULL) {
		framebuffer = hires_allocate(render_width * render_height);
	}
	struct HIRES_SURFACE *surface = hires_find(legacy);
	const legacy_s32 scale = render_scale, stride = render_width;
	const legacy_s32 cell_shift = render_cell_shift;
	for (legacy_s32 y = 0; y < HIRES_LOGICAL_HEIGHT; y++) {
		for (legacy_s32 x = 0; x < HIRES_LOGICAL_WIDTH; x++) {
			legacy_u32 offset = y * HIRES_LOGICAL_WIDTH + x;
			const legacy_u8 *cell = surface != NULL && surface->valid[offset]
										? surface->pixels + ((size_t)offset << cell_shift)
										: NULL;
			for (legacy_s32 row = 0; row < scale; row++) {
				legacy_u8 *out = framebuffer + (y * scale + row) * stride + x * scale;
				if (cell != NULL) {
					memcpy(out, cell + row * scale, scale);
				} else {
					memset(out, legacy[offset], scale);
				}
			}
		}
	}
	return framebuffer;
}

static void hires_compose_argb(const legacy_u8 *legacy, const legacy_u32 *palette,
							   const struct HIRES_SURFACE *surface, legacy_u32 *destination,
							   legacy_s32 pitch)
{
	/* Palette entry 15 is the menu's white. Match its fade so artwork follows
	 * the same black-to-white transition as the original indexed pixels. */
	legacy_u32 fade = palette[HIRES_MENU_WHITE_INDEX];
	const legacy_s32 scale = render_scale, cell_shift = render_cell_shift;
	for (legacy_s32 y = 0; y < HIRES_LOGICAL_HEIGHT; y++) {
		for (legacy_s32 x = 0; x < HIRES_LOGICAL_WIDTH; x++) {
			legacy_u32 offset = y * HIRES_LOGICAL_WIDTH + x;
			legacy_u8 valid = surface != NULL ? surface->valid[offset] : HIRES_CELL_EMPTY;
			const legacy_u8 *indices =
				valid != HIRES_CELL_EMPTY ? surface->pixels + ((size_t)offset << cell_shift) : NULL;
			legacy_u32 *output =
				(legacy_u32 *)((legacy_u8 *)destination + y * scale * pitch) + x * scale;
			if (valid != HIRES_CELL_ARGB) {
				/* Most cells have no full-color overlay. Convert their palette
				 * samples directly, without per-sample alpha/fade bookkeeping. */
				for (legacy_s32 row = 0; row < scale; row++) {
					for (legacy_s32 column = 0; column < scale; column++) {
						legacy_u8 index = valid != HIRES_CELL_EMPTY ? indices[row * scale + column]
																	: legacy[offset];
						output[column] = palette[index];
					}
					output = (legacy_u32 *)((legacy_u8 *)output + pitch);
				}
				continue;
			}
			const legacy_u32 *colors = surface->argb + ((size_t)offset << cell_shift);
			for (legacy_s32 row = 0; row < scale; row++) {
				for (legacy_s32 column = 0; column < scale; column++) {
					legacy_u32 sample = row * scale + column;
					legacy_u32 background = palette[indices[sample]];
					legacy_u32 color = colors[sample];
					legacy_u32 alpha = color >> LEGACY_THREE_BYTE_BITS;
					if (alpha == 0) {
						output[column] = background;
						continue;
					}
					legacy_u32 blended = ((legacy_u32)LEGACY_U8_MAX << LEGACY_THREE_BYTE_BITS);
					for (legacy_s32 shift = 0; shift < (legacy_s32)LEGACY_THREE_BYTE_BITS;
						 shift += (legacy_s32)LEGACY_BYTE_BITS) {
						legacy_u32 channel = ((color >> shift) & LEGACY_U8_MAX) *
											 ((fade >> shift) & LEGACY_U8_MAX) / LEGACY_U8_MAX;
						channel = (channel * alpha + ((background >> shift) & LEGACY_U8_MAX) *
														 (LEGACY_U8_MAX - alpha)) /
								  LEGACY_U8_MAX;
						blended |= channel << shift;
					}
					output[column] = blended;
				}
				output = (legacy_u32 *)((legacy_u8 *)output + pitch);
			}
		}
	}
}

void hires_copy_framebuffer_argb(const legacy_u8 *legacy, const legacy_u32 *palette,
								 legacy_u32 *destination, legacy_s32 pitch)
{
	if (enabled) {
		hires_compose_argb(legacy, palette, hires_find(legacy), destination, pitch);
		return;
	}
	for (legacy_s32 row = 0; row < HIRES_LOGICAL_HEIGHT; row++) {
		legacy_u32 *output = (legacy_u32 *)((legacy_u8 *)destination + row * pitch);
		for (legacy_s32 column = 0; column < HIRES_LOGICAL_WIDTH; column++) {
			output[column] = palette[legacy[row * HIRES_LOGICAL_WIDTH + column]];
		}
	}
}

const legacy_u32 *hires_framebuffer_argb(const legacy_u8 *legacy, const legacy_u32 *palette)
{
	struct HIRES_SURFACE *surface = enabled ? hires_find(legacy) : NULL;
	if (surface == NULL || surface->argb_cells == 0) {
		free(argb_framebuffer);
		argb_framebuffer = NULL;
		return NULL;
	}
	if (argb_framebuffer == NULL) {
		argb_framebuffer = malloc((size_t)render_width * render_height * sizeof(*argb_framebuffer));
		if (argb_framebuffer == NULL) {
			return NULL;
		}
	}
	hires_compose_argb(legacy, palette, surface, argb_framebuffer,
					   render_width * sizeof(*argb_framebuffer));
	return argb_framebuffer;
}
