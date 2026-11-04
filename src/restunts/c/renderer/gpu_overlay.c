#include "gpu_overlay.h"
#include "../hires/hires.h"
#include "../hires/hires_internal.h"
#include "../shape2d/shape2d.h"
#include "../platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define GPU_OVERLAY_OPAQUE 255U
#define GPU_OVERLAY_ALPHA_SHIFT 24U
#define GPU_OVERLAY_RGB_MASK 0x00FFFFFFU
#define GPU_OVERLAY_ALL_SAMPLES (-1)

struct GPU_OVERLAY_SURFACE {
	const legacy_u8 *base;
	legacy_u8 *mask;
	size_t capacity;
	legacy_u32 generation;
	legacy_s32 scale, cell_pixels, width, height, offset_x, offset_y;
	struct GPU_OVERLAY_SURFACE *next;
};

legacy_u8 gpu_overlay_tracking;
static legacy_u32 overlay_generation = 1;
static legacy_u8 cleanup_registered;
static struct GPU_OVERLAY_SURFACE *surfaces;

static struct GPU_OVERLAY_SURFACE *overlay_find(const legacy_u8 *base)
{
	for (struct GPU_OVERLAY_SURFACE *surface = surfaces; surface != NULL; surface = surface->next) {
		if (surface->base == base) {
			return surface;
		}
	}
	return NULL;
}

static struct GPU_OVERLAY_SURFACE *overlay_current(const legacy_u8 *base)
{
	struct GPU_OVERLAY_SURFACE *surface = overlay_find(base);
	return surface != NULL && surface->generation == overlay_generation ? surface : NULL;
}

void gpu_overlay_shutdown(void)
{
	while (surfaces != NULL) {
		struct GPU_OVERLAY_SURFACE *next = surfaces->next;
		free(surfaces->mask);
		free(surfaces);
		surfaces = next;
	}
	gpu_overlay_tracking = 0;
}

void gpu_overlay_reset(void)
{
	gpu_overlay_tracking = 0;
	if (++overlay_generation == 0) {
		overlay_generation = 1;
		for (struct GPU_OVERLAY_SURFACE *surface = surfaces; surface != NULL;
			 surface = surface->next) {
			surface->generation = 0;
		}
	}
}

static struct GPU_OVERLAY_SURFACE *overlay_prepare(const legacy_u8 *base, legacy_s32 scale,
												   legacy_s32 width, legacy_s32 height)
{
	struct GPU_OVERLAY_SURFACE *surface = overlay_find(base);
	if (surface == NULL) {
		surface = calloc(1, sizeof(*surface));
		if (surface == NULL) {
			fputs("Cannot allocate GPU foreground surface\n", stderr);
			dos_process_exit(1);
			return NULL;
		}
		surface->base = base;
		surface->next = surfaces;
		surfaces = surface;
	}
	size_t bytes = (size_t)HIRES_ADDRESS_COUNT * scale * scale;
	if (surface->capacity < bytes) {
		legacy_u8 *mask = realloc(surface->mask, bytes);
		if (mask == NULL) {
			fputs("Cannot allocate GPU foreground mask\n", stderr);
			dos_process_exit(1);
			return NULL;
		}
		surface->mask = mask;
		surface->capacity = bytes;
		surface->generation = 0;
	}
	if (surface->generation != overlay_generation || surface->scale != scale) {
		memset(surface->mask, GPU_OVERLAY_OPAQUE, bytes);
		surface->generation = overlay_generation;
		surface->offset_x = surface->offset_y = 0;
	}
	surface->scale = scale;
	surface->cell_pixels = scale * scale;
	surface->width = width;
	surface->height = height;
	if (!cleanup_registered) {
		if (atexit(gpu_overlay_shutdown) != 0) {
			fputs("Cannot register GPU foreground cleanup\n", stderr);
			dos_process_exit(1);
		}
		cleanup_registered = 1;
	}
	return surface;
}

void gpu_overlay_begin(const struct HIRES_RASTER_TARGET *target)
{
	if (target == NULL || target->surface == NULL || target->scale <= 0) {
		return;
	}
	struct GPU_OVERLAY_SURFACE *surface =
		overlay_prepare(target->surface->base, target->scale, target->width, target->height);
	if (surface == NULL) {
		return;
	}
	if (target->left % target->scale == 0 && target->right % target->scale == 0 &&
		target->top % target->scale == 0 && target->bottom % target->scale == 0) {
		for (legacy_s32 y = target->top / target->scale; y < target->bottom / target->scale; y++) {
			legacy_u16 offset = (legacy_u16)(target->rows[y] + target->left / target->scale);
			legacy_u32 cells = (target->right - target->left) / target->scale;
			legacy_u32 tail = HIRES_ADDRESS_COUNT - offset;
			if (cells > tail) {
				memset(surface->mask, 0, (size_t)(cells - tail) * surface->cell_pixels);
				cells = tail;
			}
			memset(surface->mask + (size_t)offset * surface->cell_pixels, 0,
				   (size_t)cells * surface->cell_pixels);
		}
		gpu_overlay_tracking = 1;
		return;
	}
	for (legacy_s32 y = target->top; y < target->bottom; y++) {
		legacy_s32 logical_y = y / target->scale;
		legacy_s32 sample_y = y % target->scale;
		for (legacy_s32 x = target->left; x < target->right;) {
			legacy_s32 logical_x = x / target->scale;
			legacy_s32 sample_x = x % target->scale;
			legacy_s32 count = target->scale - sample_x;
			if (count > target->right - x) {
				count = target->right - x;
			}
			legacy_u16 offset = (legacy_u16)(target->rows[logical_y] + logical_x);
			memset(surface->mask + (size_t)offset * surface->cell_pixels +
					   sample_y * target->scale + sample_x,
				   0, count);
			x += count;
		}
	}
	gpu_overlay_tracking = 1;
}

void gpu_overlay_begin_sprite(const struct SPRITE *sprite, legacy_s32 width, legacy_s32 height)
{
	if (sprite == NULL || width <= 0 || width % HIRES_LOGICAL_WIDTH != 0) {
		return;
	}
	legacy_s32 scale = width / HIRES_LOGICAL_WIDTH;
	if (scale != 1 && scale != HIRES_MEDIUM_SCALE && scale != HIRES_SCALE) {
		return;
	}
	const legacy_u8 *base =
		dos_memory_make_pointer(dos_memory_pointer_segment(sprite->sprite_bitmapptr), 0);
	struct HIRES_SURFACE fake_surface = {0};
	struct HIRES_RASTER_TARGET target = {0};
	fake_surface.base = base;
	target.surface = &fake_surface;
	target.scale = scale;
	target.width = width;
	target.height = height;
	target.left = sprite->sprite_raster_left * scale;
	target.right = sprite->sprite_raster_right * scale;
	target.top = sprite->sprite_top * scale;
	target.bottom = sprite->sprite_bottom * scale;
	for (legacy_s32 y = 0; y < HIRES_LOGICAL_HEIGHT && y < sprite->sprite_bottom; y++) {
		target.rows[y] = LEGACY_READ_U16_LE(sprite->sprite_lineofs + y * LEGACY_WORD_BYTES);
	}
	gpu_overlay_begin(&target);
}

void gpu_overlay_span(const struct HIRES_RASTER_TARGET *target, legacy_s32 left, legacy_s32 right,
					  legacy_s32 y)
{
	struct GPU_OVERLAY_SURFACE *surface = overlay_current(target->surface->base);
	if (surface == NULL || surface->scale != target->scale) {
		return;
	}
	legacy_u16 row = target->rows[y / target->scale];
	legacy_s32 sample_y = y % target->scale;
	while (left < right) {
		legacy_s32 cell_x = left / target->scale;
		legacy_s32 sample_x = left % target->scale;
		legacy_s32 count = target->scale - sample_x;
		if (count > right - left) {
			count = right - left;
		}
		legacy_u16 offset = (legacy_u16)(row + cell_x);
		memset(surface->mask + (size_t)offset * surface->cell_pixels + sample_y * target->scale +
				   sample_x,
			   GPU_OVERLAY_OPAQUE, count);
		left += count;
	}
}

void gpu_overlay_write(const legacy_u8 *base, legacy_u16 offset, legacy_s32 sample)
{
	struct GPU_OVERLAY_SURFACE *surface = overlay_current(base);
	if (surface == NULL) {
		return;
	}
	legacy_u8 *mask = surface->mask + (size_t)offset * surface->cell_pixels;
	if (sample == GPU_OVERLAY_ALL_SAMPLES) {
		memset(mask, GPU_OVERLAY_OPAQUE, surface->cell_pixels);
	} else if (sample >= 0 && sample < surface->cell_pixels) {
		mask[sample] = GPU_OVERLAY_OPAQUE;
	}
}

void gpu_overlay_shadow(const legacy_u8 *base, legacy_u16 offset, legacy_s32 sample,
						legacy_u8 opacity)
{
	struct GPU_OVERLAY_SURFACE *surface = overlay_current(base);
	if (surface == NULL || sample < 0 || sample >= surface->cell_pixels) {
		return;
	}
	legacy_u8 *mask = surface->mask + (size_t)offset * surface->cell_pixels + sample;
	if (*mask != GPU_OVERLAY_OPAQUE) {
		*mask = opacity + (legacy_u32)*mask * (GPU_OVERLAY_OPAQUE - opacity) / GPU_OVERLAY_OPAQUE;
	}
}

static void overlay_transfer_offset(struct GPU_OVERLAY_SURFACE *dst,
									const struct GPU_OVERLAY_SURFACE *src, legacy_u16 destination,
									legacy_u16 source)
{
	dst->offset_x = src->offset_x + ((legacy_s32)(destination % HIRES_LOGICAL_WIDTH) -
									 (legacy_s32)(source % HIRES_LOGICAL_WIDTH)) *
										dst->scale;
	dst->offset_y = src->offset_y + ((legacy_s32)(destination / HIRES_LOGICAL_WIDTH) -
									 (legacy_s32)(source / HIRES_LOGICAL_WIDTH)) *
										dst->scale;
}

static void overlay_copy_offset(struct GPU_OVERLAY_SURFACE *dst,
								const struct GPU_OVERLAY_SURFACE *src, legacy_u16 destination,
								legacy_u16 source, legacy_u16 count)
{
	/* Opaque CPU artwork copied from a scene surface must not move the world.
	 * Ordinary page copies retain their origin and avoid another mask scan. */
	legacy_s32 x = src->offset_x + ((legacy_s32)(destination % HIRES_LOGICAL_WIDTH) -
									(legacy_s32)(source % HIRES_LOGICAL_WIDTH)) *
									   dst->scale;
	legacy_s32 y = src->offset_y + ((legacy_s32)(destination / HIRES_LOGICAL_WIDTH) -
									(legacy_s32)(source / HIRES_LOGICAL_WIDTH)) *
									   dst->scale;
	if (dst->offset_x == x && dst->offset_y == y) {
		return;
	}
	for (legacy_u32 index = 0; index < count; index++) {
		legacy_u16 offset = (legacy_u16)(source + index);
		const legacy_u8 *mask = src->mask + (size_t)offset * src->cell_pixels;
		for (legacy_s32 sample = 0; sample < src->cell_pixels; sample++) {
			if (mask[sample] != GPU_OVERLAY_OPAQUE) {
				dst->offset_x = x;
				dst->offset_y = y;
				return;
			}
		}
	}
}

void gpu_overlay_raster(const legacy_u8 *destination, legacy_u16 destination_offset,
						const legacy_u8 *source, legacy_u16 source_offset, legacy_u16 count,
						legacy_s16 operation, const legacy_u8 *palette)
{
	struct GPU_OVERLAY_SURFACE *src = overlay_current(source);
	struct GPU_OVERLAY_SURFACE *dst = overlay_current(destination);
	if (src == NULL && dst == NULL) {
		return;
	}
	if (dst == NULL) {
		dst = overlay_prepare(destination, src->scale, src->width, src->height);
		if (dst == NULL) {
			return;
		}
	}
	if (operation == SHAPE2D_RASTER_COPY && src != NULL && src->scale == dst->scale) {
		overlay_copy_offset(dst, src, destination_offset, source_offset, count);
	}
	if (operation == SHAPE2D_RASTER_COPY && count <= HIRES_ADDRESS_COUNT - destination_offset &&
		count <= HIRES_ADDRESS_COUNT - source_offset) {
		legacy_u8 *out = dst->mask + (size_t)destination_offset * dst->cell_pixels;
		size_t bytes = (size_t)count * dst->cell_pixels;
		if (src != NULL && src->scale == dst->scale) {
			const legacy_u8 *in = src->mask + (size_t)source_offset * src->cell_pixels;
			if (src != dst || destination_offset >= (legacy_u32)source_offset + count ||
				source_offset >= (legacy_u32)destination_offset + count) {
				memcpy(out, in, bytes);
			} else if (destination_offset <= source_offset) {
				memmove(out, in, bytes);
			} else {
				/* Match forward legacy copies rather than memmove's snapshot. */
				for (size_t index = 0; index < bytes; index++) {
					out[index] = in[index];
				}
			}
		} else {
			memset(out, GPU_OVERLAY_OPAQUE, bytes);
		}
		return;
	}
	const struct HIRES_SURFACE *source_surface = hires_state.surfaces;
	while (source_surface != NULL && source_surface->base != source) {
		source_surface = source_surface->next;
	}
	legacy_u8 copied_scene = 0;
	for (legacy_u32 index = 0; index < count; index++) {
		legacy_u16 so = (legacy_u16)(source_offset + index);
		legacy_u16 dest = (legacy_u16)(destination_offset + index);
		legacy_u8 *out = dst->mask + (size_t)dest * dst->cell_pixels;
		const legacy_u8 *in = src != NULL && src->scale == dst->scale
								  ? src->mask + (size_t)so * src->cell_pixels
								  : NULL;
		const legacy_u8 *samples = source_surface != NULL && source_surface->valid[so]
									   ? source_surface->pixels + (size_t)so * dst->cell_pixels
									   : NULL;
		for (legacy_s32 sample = 0; sample < dst->cell_pixels; sample++) {
			legacy_u8 value = samples != NULL ? samples[sample] : source[so];
			if (operation == SHAPE2D_RASTER_AND) {
				if (value != LEGACY_U8_MAX) {
					out[sample] = GPU_OVERLAY_OPAQUE;
				}
			} else if (operation == SHAPE2D_RASTER_OR) {
				if (value != 0) {
					out[sample] = GPU_OVERLAY_OPAQUE;
				}
			} else if (operation != SHAPE2D_RASTER_MAP || palette[value] != LEGACY_U8_MAX) {
				out[sample] = in != NULL ? in[sample] : GPU_OVERLAY_OPAQUE;
				copied_scene |= out[sample] != GPU_OVERLAY_OPAQUE;
			}
		}
	}
	if (operation != SHAPE2D_RASTER_COPY && copied_scene) {
		overlay_transfer_offset(dst, src, destination_offset, source_offset);
	}
}

void gpu_overlay_forget(const void *base, legacy_u32 size)
{
	uintptr_t start = (uintptr_t)base;
	struct GPU_OVERLAY_SURFACE **link = &surfaces;
	while (*link != NULL) {
		struct GPU_OVERLAY_SURFACE *surface = *link;
		uintptr_t address = (uintptr_t)surface->base;
		if (address == start || (size != 0 && address >= start && address - start < size)) {
			*link = surface->next;
			free(surface->mask);
			free(surface);
		} else {
			link = &surface->next;
		}
	}
}

const legacy_u8 *gpu_overlay_mask(const legacy_u8 *base, legacy_s32 width, legacy_s32 height,
								  legacy_s32 *offset_x, legacy_s32 *offset_y)
{
	if (!gpu_overlay_tracking) {
		return NULL;
	}
	const struct GPU_OVERLAY_SURFACE *surface = overlay_current(base);
	if (surface == NULL || surface->width != width || surface->height != height) {
		return NULL;
	}
	if (offset_x != NULL) {
		*offset_x = surface->offset_x;
	}
	if (offset_y != NULL) {
		*offset_y = surface->offset_y;
	}
	return surface->mask;
}

void gpu_overlay_compose_rows(const legacy_u8 *legacy, const legacy_u32 *palette,
							  const struct HIRES_SURFACE *surface, const legacy_u8 *mask,
							  legacy_u32 *destination, legacy_s32 pitch, legacy_s32 scale,
							  legacy_s32 top, legacy_s32 bottom)
{
	const legacy_u32 fade = palette[HIRES_MENU_WHITE_INDEX];
	const legacy_s32 cell_pixels = scale * scale;
	for (legacy_s32 y = top; y < bottom; y++) {
		for (legacy_s32 x = 0; x < HIRES_LOGICAL_WIDTH; x++) {
			legacy_u32 offset = y * HIRES_LOGICAL_WIDTH + x;
			legacy_u8 valid = surface != NULL ? surface->valid[offset] : HIRES_CELL_EMPTY;
			const legacy_u8 *indices =
				valid != HIRES_CELL_EMPTY ? surface->pixels + (size_t)offset * cell_pixels : NULL;
			const legacy_u32 *colors =
				valid == HIRES_CELL_ARGB ? surface->argb + (size_t)offset * cell_pixels : NULL;
			const legacy_u8 *coverage = mask + (size_t)offset * cell_pixels;
			for (legacy_s32 row = 0; row < scale; row++) {
				legacy_u32 *out =
					(legacy_u32 *)((legacy_u8 *)destination + (size_t)(y * scale + row) * pitch) +
					x * scale;
				for (legacy_s32 column = 0; column < scale; column++) {
					legacy_s32 sample = row * scale + column;
					legacy_u32 alpha = coverage[sample];
					if (alpha != 0 && alpha != GPU_OVERLAY_OPAQUE) {
						out[column] = alpha << GPU_OVERLAY_ALPHA_SHIFT;
						continue;
					}
					legacy_u8 index = indices != NULL ? indices[sample] : legacy[offset];
					legacy_u32 color = palette[index];
					if (colors != NULL && (colors[sample] >> GPU_OVERLAY_ALPHA_SHIFT) != 0) {
						legacy_u32 overlay = colors[sample];
						legacy_u32 opacity = overlay >> GPU_OVERLAY_ALPHA_SHIFT;
						legacy_u32 blended = 0;
						for (legacy_u32 shift = 0; shift < GPU_OVERLAY_ALPHA_SHIFT;
							 shift += LEGACY_BYTE_BITS) {
							legacy_u32 channel = ((overlay >> shift) & LEGACY_U8_MAX) *
												 ((fade >> shift) & LEGACY_U8_MAX) / LEGACY_U8_MAX;
							channel = (channel * opacity + ((color >> shift) & LEGACY_U8_MAX) *
															   (LEGACY_U8_MAX - opacity)) /
									  LEGACY_U8_MAX;
							blended |= channel << shift;
						}
						color = blended;
					}
					out[column] =
						(color & GPU_OVERLAY_RGB_MASK) | (alpha << GPU_OVERLAY_ALPHA_SHIFT);
				}
			}
		}
	}
}

legacy_s32 gpu_overlay_compose(const legacy_u8 *base, legacy_u32 *pixels, legacy_s32 width,
							   legacy_s32 height, legacy_s32 *offset_x, legacy_s32 *offset_y)
{
	if (!gpu_overlay_tracking) {
		return 0;
	}
	const struct GPU_OVERLAY_SURFACE *surface = overlay_current(base);
	if (surface == NULL || surface->width != width || surface->height != height) {
		return 0;
	}
	*offset_x = surface->offset_x;
	*offset_y = surface->offset_y;
	for (legacy_s32 y = 0; y < HIRES_LOGICAL_HEIGHT; y++) {
		for (legacy_s32 x = 0; x < HIRES_LOGICAL_WIDTH; x++) {
			legacy_u32 offset = y * HIRES_LOGICAL_WIDTH + x;
			const legacy_u8 *mask = surface->mask + (size_t)offset * surface->cell_pixels;
			for (legacy_s32 row = 0; row < surface->scale; row++) {
				legacy_u32 *out =
					pixels + (size_t)(y * surface->scale + row) * width + x * surface->scale;
				for (legacy_s32 column = 0; column < surface->scale; column++) {
					legacy_u32 alpha = *mask++;
					out[column] =
						(out[column] & GPU_OVERLAY_RGB_MASK) | (alpha << GPU_OVERLAY_ALPHA_SHIFT);
					if (alpha != 0 && alpha != GPU_OVERLAY_OPAQUE) {
						out[column] &= ~GPU_OVERLAY_RGB_MASK;
					}
				}
			}
		}
	}
	return 1;
}
