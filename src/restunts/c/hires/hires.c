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

#include "hires_internal.h"

struct HIRES_STATE hires_state = {
	.render_scale = HIRES_SCALE,
	.render_shift = HIRES_MAXIMUM_SCALE_SHIFT,
	.render_mask = HIRES_SCALE - 1,
	.render_cell_pixels = HIRES_CELL_PIXELS,
	.render_cell_shift = HIRES_MAXIMUM_SCALE_SHIFT * HIRES_CELL_DIMENSIONS,
	.render_width = HIRES_WIDTH,
	.render_height = HIRES_HEIGHT,
};

void *hires_allocate(size_t size)
{
	void *result = calloc(1, size);
	if (result == NULL) {
		fputs("Cannot allocate high-resolution hires_state.framebuffer\n", stderr);
		dos_process_exit(1);
	}
	return result;
}

static struct HIRES_SURFACE *hires_find(const legacy_u8 *base)
{
	for (struct HIRES_SURFACE *surface = hires_state.surfaces; surface != NULL;
		 surface = surface->next) {
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
		surface->pixels = hires_allocate(HIRES_ADDRESS_COUNT * hires_state.render_cell_pixels);
		surface->next = hires_state.surfaces;
		hires_state.surfaces = surface;
	}
	return surface;
}

legacy_u8 *hires_cell(struct HIRES_SURFACE *surface, legacy_u16 offset)
{
	legacy_u8 *cell = surface->pixels + ((size_t)offset << hires_state.render_cell_shift);
	if (!surface->valid[offset]) {
		memset(cell, surface->base[offset], hires_state.render_cell_pixels);
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
		surface->argb = malloc((size_t)HIRES_ADDRESS_COUNT * hires_state.render_cell_pixels *
							   sizeof(*surface->argb));
	}
	return surface->argb != NULL;
}

void hires_depth_reset(void)
{
	hires_state.depth_left = hires_state.depth_right = hires_state.depth_top =
		hires_state.depth_bottom = 0;
}

static void hires_release_buffers(void)
{
	while (hires_state.surfaces != NULL) {
		struct HIRES_SURFACE *next = hires_state.surfaces->next;
		free(hires_state.surfaces->pixels);
		free(hires_state.surfaces->argb);
		free(hires_state.surfaces);
		hires_state.surfaces = next;
	}
	free(hires_state.framebuffer);
	free(hires_state.argb_framebuffer);
	hires_state.argb_framebuffer = NULL;
	hires_state.framebuffer = NULL;
	free(hires_state.inverse_depth);
	free(hires_state.depth_family);
	hires_state.inverse_depth = NULL;
	hires_state.depth_family = NULL;
	hires_depth_reset();
	hires_state.active = NULL;
	hires_state.generation++;
}

static void hires_configure_scale(legacy_s32 scale)
{
	hires_state.render_scale = scale;
	hires_state.render_shift = scale == HIRES_SCALE			 ? HIRES_MAXIMUM_SCALE_SHIFT
							   : scale == HIRES_MEDIUM_SCALE ? HIRES_MEDIUM_SCALE_SHIFT
															 : 0;
	hires_state.render_mask = scale - 1;
	hires_state.render_cell_pixels = scale * scale;
	hires_state.render_cell_shift = hires_state.render_shift * HIRES_CELL_DIMENSIONS;
	hires_state.render_width = HIRES_LOGICAL_WIDTH * scale;
	hires_state.render_height = HIRES_LOGICAL_HEIGHT * scale;
}

void hires_shutdown(void)
{
	render_workers_shutdown();
	hires_release_buffers();
	hires_configure_scale(HIRES_SCALE);
	hires_state.enabled = 0;
}

void hires_set_render_scale(legacy_s32 scale)
{
	if (hires_state.active != NULL || scale == hires_state.render_scale ||
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
	return hires_state.render_scale;
}

legacy_s32 hires_render_width(void)
{
	return hires_state.render_width;
}

legacy_s32 hires_render_height(void)
{
	return hires_state.render_height;
}

void hires_set_enabled(legacy_s32 value)
{
	value = value != 0;
	if (value == hires_state.enabled) {
		return;
	}
	hires_shutdown();
	hires_state.enabled = value;
}

legacy_s32 hires_enabled(void)
{
	return hires_state.enabled;
}

legacy_u32 hires_generation(void)
{
	return hires_state.generation;
}

legacy_s32 hires_begin(const struct SPRITE *target)
{
	if (!hires_state.enabled || hires_state.active != NULL) {
		return 0;
	}
	const legacy_u8 *base =
		dos_memory_make_pointer(dos_memory_pointer_segment(target->sprite_bitmapptr), 0);
	hires_state.active = hires_create(base);
	hires_state.active_sprite = *target;
	hires_depth_reset();
	/* Seed the background before the legacy 3D pass overwrites its pixels. */
	for (legacy_u32 y = target->sprite_top; y < target->sprite_bottom && y < HIRES_LOGICAL_HEIGHT;
		 y++) {
		legacy_u16 row = LEGACY_READ_U16_LE(target->sprite_lineofs + y * LEGACY_WORD_BYTES);
		for (legacy_u32 x = target->sprite_raster_left;
			 x < target->sprite_raster_right && x < HIRES_LOGICAL_WIDTH; x++) {
			hires_cell(hires_state.active, (legacy_u16)(row + x));
		}
	}
	return 1;
}

legacy_s32 hires_begin_argb(const struct SPRITE *target)
{
	if (!hires_begin(target)) {
		return 0;
	}
	if (!hires_allocate_argb(hires_state.active)) {
		hires_end();
		return 0;
	}
	return 1;
}

void hires_end(void)
{
	hires_depth_reset();
	if (hires_state.active != NULL) {
		hires_release_unused_argb(hires_state.active);
		hires_state.active = NULL;
		hires_state.generation++;
	}
}

void hires_fill_pixel(legacy_s32 x, legacy_s32 y, legacy_u8 color)
{
	if (hires_state.active == NULL || x < 0 || y < 0 || x >= HIRES_LOGICAL_WIDTH ||
		y >= HIRES_LOGICAL_HEIGHT || x < hires_state.active_sprite.sprite_raster_left ||
		x >= hires_state.active_sprite.sprite_raster_right ||
		y < hires_state.active_sprite.sprite_top || y >= hires_state.active_sprite.sprite_bottom) {
		return;
	}
	legacy_u16 row =
		LEGACY_READ_U16_LE(hires_state.active_sprite.sprite_lineofs + y * LEGACY_WORD_BYTES);
	legacy_u16 offset = (legacy_u16)(row + x);
	memset(hires_cell(hires_state.active, offset), color, hires_state.render_cell_pixels);
	hires_clear_argb(hires_state.active, offset);
}

void hires_write_pixel(legacy_s32 x, legacy_s32 y, const legacy_u8 *samples)
{
	if (hires_state.active == NULL || x < 0 || y < 0 || x >= HIRES_LOGICAL_WIDTH ||
		y >= HIRES_LOGICAL_HEIGHT || x < hires_state.active_sprite.sprite_raster_left ||
		x >= hires_state.active_sprite.sprite_raster_right ||
		y < hires_state.active_sprite.sprite_top || y >= hires_state.active_sprite.sprite_bottom) {
		return;
	}
	legacy_u16 row =
		LEGACY_READ_U16_LE(hires_state.active_sprite.sprite_lineofs + y * LEGACY_WORD_BYTES);
	legacy_u16 offset = (legacy_u16)(row + x);
	memcpy(hires_cell(hires_state.active, offset), samples, hires_state.render_cell_pixels);
	hires_clear_argb(hires_state.active, offset);
}

static legacy_u32 *hires_argb_sample(legacy_s32 x, legacy_s32 y)
{
	if (hires_state.active == NULL || hires_state.active->argb == NULL || x < 0 || y < 0 ||
		x >= hires_state.render_width || y >= hires_state.render_height ||
		x < hires_state.active_sprite.sprite_raster_left * hires_state.render_scale ||
		x >= hires_state.active_sprite.sprite_raster_right * hires_state.render_scale ||
		y < hires_state.active_sprite.sprite_top * hires_state.render_scale ||
		y >= hires_state.active_sprite.sprite_bottom * hires_state.render_scale) {
		return NULL;
	}
	legacy_u16 row = LEGACY_READ_U16_LE(hires_state.active_sprite.sprite_lineofs +
										(y >> hires_state.render_shift) * LEGACY_WORD_BYTES);
	legacy_u16 offset = (legacy_u16)(row + (x >> hires_state.render_shift));
	legacy_u32 *cell = hires_state.active->argb + ((size_t)offset << hires_state.render_cell_shift);
	if (hires_state.active->valid[offset] != HIRES_CELL_ARGB) {
		memset(cell, 0, hires_state.render_cell_pixels * sizeof(*cell));
		hires_state.active->valid[offset] = HIRES_CELL_ARGB;
		hires_state.active->argb_cells++;
	}
	return cell + (y & hires_state.render_mask) * hires_state.render_scale +
		   (x & hires_state.render_mask);
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
	return hires_state.enabled && hires_state.active != NULL &&
		   hires_allocate_argb(hires_state.active);
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
	if (!hires_state.enabled || hires_state.active != NULL) {
		return;
	}
	struct HIRES_SURFACE *surface = hires_find(base);
	if (surface != NULL && surface->valid[offset]) {
		/* A 2D write replaces the whole pixel even when its colour is unchanged. */
		hires_clear_argb(surface, offset);
		surface->valid[offset] = HIRES_CELL_EMPTY;
		hires_release_unused_argb(surface);
		hires_state.generation++;
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
		memcpy(source_argb, src->argb + ((size_t)source << hires_state.render_cell_shift),
			   hires_state.render_cell_pixels * sizeof(*source_argb));
	}
	legacy_u32 *cell = dst->argb + ((size_t)dest << hires_state.render_cell_shift);
	if (dst->valid[dest] != HIRES_CELL_ARGB) {
		memset(cell, 0, hires_state.render_cell_pixels * sizeof(*source_argb));
	}
	legacy_u32 remaining = 0;
	for (legacy_s32 sample = 0; sample < hires_state.render_cell_pixels; sample++) {
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
	if (!hires_state.enabled || hires_state.active != NULL || count == 0) {
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
			memcpy(samples, src->pixels + ((size_t)so << hires_state.render_cell_shift),
				   hires_state.render_cell_pixels);
		} else {
			memset(samples, source_color, hires_state.render_cell_pixels);
		}
		if (argb_present && (dst->valid[dest] == HIRES_CELL_ARGB ||
							 (src != NULL && src->valid[so] == HIRES_CELL_ARGB))) {
			hires_raster_argb(dst, dest, src, so, samples, operation, palette);
		}
		for (legacy_s32 sample = 0; sample < hires_state.render_cell_pixels; sample++) {
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
	hires_state.generation++;
}

void hires_forget(const void *base)
{
	struct HIRES_SURFACE **link = &hires_state.surfaces;
	while (*link != NULL) {
		struct HIRES_SURFACE *surface = *link;
		if (surface->base == base) {
			if (surface == hires_state.active) {
				hires_state.active = NULL;
			}
			*link = surface->next;
			free(surface->pixels);
			free(surface->argb);
			free(surface);
			hires_state.generation++;
			return;
		}
		link = &surface->next;
	}
}

void hires_forget_range(const void *base, legacy_u32 size)
{
	if (!hires_state.enabled || size == 0) {
		return;
	}
	uintptr_t start = (uintptr_t)base;
	uintptr_t end = start + size;
	struct HIRES_SURFACE **link = &hires_state.surfaces;
	while (*link != NULL) {
		struct HIRES_SURFACE *surface = *link;
		uintptr_t address = (uintptr_t)surface->base;
		if (address >= start && address < end) {
			if (surface == hires_state.active) {
				hires_state.active = NULL;
			}
			*link = surface->next;
			free(surface->pixels);
			free(surface->argb);
			free(surface);
			hires_state.generation++;
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
			hires_state.generation++;
		}
		link = &surface->next;
	}
}

const legacy_u8 *hires_framebuffer(const legacy_u8 *legacy, legacy_s32 *width, legacy_s32 *height)
{
	*width = hires_state.enabled ? hires_state.render_width : HIRES_LOGICAL_WIDTH;
	*height = hires_state.enabled ? hires_state.render_height : HIRES_LOGICAL_HEIGHT;
	if (!hires_state.enabled) {
		return legacy;
	}
	if (hires_state.framebuffer == NULL) {
		hires_state.framebuffer =
			hires_allocate(hires_state.render_width * hires_state.render_height);
	}
	struct HIRES_SURFACE *surface = hires_find(legacy);
	const legacy_s32 scale = hires_state.render_scale, stride = hires_state.render_width;
	const legacy_s32 cell_shift = hires_state.render_cell_shift;
	for (legacy_s32 y = 0; y < HIRES_LOGICAL_HEIGHT; y++) {
		for (legacy_s32 x = 0; x < HIRES_LOGICAL_WIDTH; x++) {
			legacy_u32 offset = y * HIRES_LOGICAL_WIDTH + x;
			const legacy_u8 *cell = surface != NULL && surface->valid[offset]
										? surface->pixels + ((size_t)offset << cell_shift)
										: NULL;
			for (legacy_s32 row = 0; row < scale; row++) {
				legacy_u8 *out = hires_state.framebuffer + (y * scale + row) * stride + x * scale;
				if (cell != NULL) {
					memcpy(out, cell + row * scale, scale);
				} else {
					memset(out, legacy[offset], scale);
				}
			}
		}
	}
	return hires_state.framebuffer;
}

static void hires_compose_argb(const legacy_u8 *legacy, const legacy_u32 *palette,
							   const struct HIRES_SURFACE *surface, legacy_u32 *destination,
							   legacy_s32 pitch)
{
	/* Palette entry 15 is the menu's white. Match its fade so artwork follows
	 * the same black-to-white transition as the original indexed pixels. */
	legacy_u32 fade = palette[HIRES_MENU_WHITE_INDEX];
	const legacy_s32 scale = hires_state.render_scale, cell_shift = hires_state.render_cell_shift;
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
	if (hires_state.enabled) {
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
	struct HIRES_SURFACE *surface = hires_state.enabled ? hires_find(legacy) : NULL;
	if (surface == NULL || surface->argb_cells == 0) {
		free(hires_state.argb_framebuffer);
		hires_state.argb_framebuffer = NULL;
		return NULL;
	}
	if (hires_state.argb_framebuffer == NULL) {
		hires_state.argb_framebuffer =
			malloc((size_t)hires_state.render_width * hires_state.render_height *
				   sizeof(*hires_state.argb_framebuffer));
		if (hires_state.argb_framebuffer == NULL) {
			return NULL;
		}
	}
	hires_compose_argb(legacy, palette, surface, hires_state.argb_framebuffer,
					   hires_state.render_width * sizeof(*hires_state.argb_framebuffer));
	return hires_state.argb_framebuffer;
}
