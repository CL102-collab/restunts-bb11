#include "hires.h"
#include "widescreen.h"
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
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS)
#include "../../platform/sdl3/video_mode.h"
#endif

struct HIRES_STATE hires_state = {
	.render_scale = HIRES_SCALE,
	.render_shift = HIRES_MAXIMUM_SCALE_SHIFT,
	.render_mask = HIRES_SCALE - 1,
	.render_cell_pixels = HIRES_CELL_PIXELS,
	.render_cell_shift = HIRES_MAXIMUM_SCALE_SHIFT * HIRES_CELL_DIMENSIONS,
	.render_width = HIRES_WIDTH,
	.render_height = HIRES_HEIGHT,
	.presentation_width = HIRES_WIDTH,
	.presentation_height = HIRES_HEIGHT,
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

struct HIRES_SURFACE *hires_surface_find(const legacy_u8 *base)
{
	for (struct HIRES_SURFACE *surface = hires_state.surfaces; surface != NULL;
		 surface = surface->next) {
		if (surface->base == base) {
			return surface;
		}
	}
	return NULL;
}

static struct HIRES_SURFACE *hires_create_surface(const legacy_u8 *base, legacy_u32 cell_count,
												  legacy_u32 offset_mask)
{
	struct HIRES_SURFACE *surface = hires_allocate(sizeof(*surface) + cell_count);
	surface->base = base;
	surface->cell_count = cell_count;
	surface->offset_mask = offset_mask;
	surface->pixels = hires_allocate((size_t)cell_count * hires_state.render_cell_pixels);
	return surface;
}

static void hires_free_surface(struct HIRES_SURFACE *surface)
{
	free(surface->pixels);
	free(surface->argb);
	free(surface);
}

static struct HIRES_SURFACE *hires_create(const legacy_u8 *base)
{
	struct HIRES_SURFACE *surface = hires_surface_find(base);
	if (surface == NULL) {
		surface = hires_create_surface(base, HIRES_ADDRESS_COUNT, LEGACY_U16_MAX);
		surface->next = hires_state.surfaces;
		hires_state.surfaces = surface;
	}
	return surface;
}

legacy_u8 *hires_cell(struct HIRES_SURFACE *surface, legacy_u32 offset)
{
	legacy_u8 *cell = surface->pixels + ((size_t)offset << hires_state.render_cell_shift);
	if (!surface->valid[offset]) {
		memset(cell, surface->base != NULL ? surface->base[offset] : 0,
			   hires_state.render_cell_pixels);
		surface->valid[offset] = HIRES_CELL_INDEXED;
	}
	return cell;
}

static void hires_clear_argb(struct HIRES_SURFACE *surface, legacy_u32 offset)
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
		surface->argb = malloc((size_t)surface->cell_count * hires_state.render_cell_pixels *
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
		hires_free_surface(hires_state.surfaces);
		hires_state.surfaces = next;
	}
	if (hires_state.wide_scene != NULL) {
		hires_free_surface(hires_state.wide_scene);
		hires_state.wide_scene = NULL;
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
	hires_state.render_cell_pixels = HIRES_CELL_PIXELS;
	hires_state.render_cell_shift = HIRES_MAXIMUM_SCALE_SHIFT * HIRES_CELL_DIMENSIONS;
	hires_state.render_width = widescreen_logical_width() * scale;
	hires_state.render_height = widescreen_logical_height() * scale;
	hires_state.presentation_width = widescreen_logical_width() * HIRES_SCALE;
	hires_state.presentation_height = widescreen_logical_height() * HIRES_SCALE;
}

void hires_refresh_render_dimensions(void)
{
	if (hires_state.active == NULL &&
		(hires_state.render_width != widescreen_logical_width() * hires_state.render_scale ||
		 hires_state.render_height != widescreen_logical_height() * hires_state.render_scale)) {
		hires_release_buffers();
		hires_configure_scale(hires_state.render_scale);
	}
}

void hires_shutdown(void)
{
	hires_render_workers_shutdown();
	hires_release_buffers();
	hires_configure_scale(HIRES_SCALE);
	hires_state.enabled = 0;
}

void hires_set_render_scale(legacy_s32 scale)
{
	hires_refresh_render_dimensions();
	if (hires_state.active != NULL || scale == hires_state.render_scale ||
		(scale != HIRES_SCALE && scale != HIRES_MEDIUM_SCALE && scale != HIRES_MINIMUM_SCALE)) {
		return;
	}
	/* Retire joined scene work while preserving fixed-size companion cells,
	 * artwork and saved backgrounds across adaptive quality changes. */
	free(hires_state.inverse_depth);
	free(hires_state.depth_family);
	hires_state.inverse_depth = NULL;
	hires_state.depth_family = NULL;
	hires_depth_reset();
	hires_configure_scale(scale);
	hires_state.generation++;
}

legacy_s32 hires_render_scale(void)
{
	return hires_state.render_scale;
}

legacy_s32 hires_render_width(void)
{
	hires_refresh_render_dimensions();
	return hires_state.render_width;
}

legacy_s32 hires_render_height(void)
{
	hires_refresh_render_dimensions();
	return hires_state.render_height;
}

legacy_s32 hires_presentation_width(void)
{
	hires_refresh_render_dimensions();
	return hires_state.presentation_width;
}

legacy_s32 hires_presentation_height(void)
{
	hires_refresh_render_dimensions();
	return hires_state.presentation_height;
}

legacy_s32 hires_artwork_scale(void)
{
	return HIRES_SCALE;
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
	return hires_state.generation + widescreen_overlay_generation();
}

static legacy_s32 hires_begin_target(const struct SPRITE *target, legacy_s32 scene)
{
	hires_refresh_render_dimensions();
	if (!hires_state.enabled || hires_state.active != NULL) {
		return 0;
	}
	legacy_s32 width = HIRES_LOGICAL_WIDTH;
	legacy_s32 height = HIRES_LOGICAL_HEIGHT;
	if (scene && hires_state.presentation_width > HIRES_WIDTH) {
		width = hires_state.presentation_width / HIRES_SCALE;
		height = hires_state.presentation_height / HIRES_SCALE;
		if (hires_state.wide_scene == NULL) {
			hires_state.wide_scene = hires_create_surface(NULL, width * height, LEGACY_U32_MAX);
		}
		hires_state.active = hires_state.wide_scene;
	} else {
		const legacy_u8 *base =
			dos_memory_make_pointer(dos_memory_pointer_segment(target->sprite_bitmapptr), 0);
		hires_state.active = hires_create(base);
	}
	hires_state.active_sprite = *target;
	hires_state.active_artwork = 0;
	hires_depth_reset();
	/* Seed the background before the legacy 3D pass overwrites its pixels. */
	for (legacy_s32 y = target->sprite_top; y < target->sprite_bottom && y < height; y++) {
		legacy_u32 row = hires_active_row(y);
		for (legacy_s32 x = target->sprite_raster_left;
			 x < target->sprite_raster_right && x < width; x++) {
			hires_cell(hires_state.active, (row + x) & hires_state.active->offset_mask);
		}
	}
	return 1;
}

legacy_s32 hires_begin(const struct SPRITE *target)
{
	return hires_begin_target(target, 0);
}

legacy_s32 hires_begin_scene(const struct SPRITE *target)
{
	return hires_begin_target(target, 1);
}

legacy_s32 hires_wide_scene_active(void)
{
	return hires_state.active != NULL && hires_state.active == hires_state.wide_scene;
}

legacy_s32 hires_begin_argb(const struct SPRITE *target)
{
	if (!hires_begin(target)) {
		return 0;
	}
	hires_state.active_artwork = 1;
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

static legacy_s32 hires_contains_logical_pixel(legacy_s32 x, legacy_s32 y)
{
	legacy_s32 width = HIRES_LOGICAL_WIDTH;
	legacy_s32 height = HIRES_LOGICAL_HEIGHT;
	if (hires_wide_scene_active()) {
		width = hires_state.presentation_width / HIRES_SCALE;
		height = hires_state.presentation_height / HIRES_SCALE;
	}
	return hires_state.active != NULL && x >= 0 && x < width && y >= 0 && y < height &&
		   x >= hires_state.active_sprite.sprite_raster_left &&
		   x < hires_state.active_sprite.sprite_raster_right &&
		   y >= hires_state.active_sprite.sprite_top && y < hires_state.active_sprite.sprite_bottom;
}

void hires_fill_pixel(legacy_s32 x, legacy_s32 y, legacy_u8 color)
{
	if (!hires_contains_logical_pixel(x, y)) {
		return;
	}
	legacy_u32 offset = hires_active_offset(x, y);
	memset(hires_cell(hires_state.active, offset), color, hires_state.render_cell_pixels);
	hires_clear_argb(hires_state.active, offset);
}

void hires_write_pixel(legacy_s32 x, legacy_s32 y, const legacy_u8 *samples)
{
	if (!hires_contains_logical_pixel(x, y)) {
		return;
	}
	legacy_u32 offset = hires_active_offset(x, y);
	hires_expand_cell(hires_cell(hires_state.active, offset), samples, hires_state.render_scale);
	hires_clear_argb(hires_state.active, offset);
}

/* Scene samples cover fixed presentation blocks; artwork uses scale four. */
static legacy_u32 *hires_argb_sample(legacy_s32 x, legacy_s32 y, legacy_s32 scale)
{
	legacy_s32 width = widescreen_logical_width() * scale;
	legacy_s32 height = widescreen_logical_height() * scale;
	if (hires_state.active == NULL || hires_state.active->argb == NULL || x < 0 || y < 0 ||
		x >= width || y >= height || x < hires_state.active_sprite.sprite_raster_left * scale ||
		x >= hires_state.active_sprite.sprite_raster_right * scale ||
		y < hires_state.active_sprite.sprite_top * scale ||
		y >= hires_state.active_sprite.sprite_bottom * scale) {
		return NULL;
	}
	legacy_u32 offset = hires_active_offset(x / scale, y / scale);
	legacy_u32 *cell = hires_state.active->argb + ((size_t)offset << hires_state.render_cell_shift);
	if (hires_state.active->valid[offset] != HIRES_CELL_ARGB) {
		memset(cell, 0, HIRES_CELL_PIXELS * sizeof(*cell));
		hires_state.active->valid[offset] = HIRES_CELL_ARGB;
		hires_state.active->argb_cells++;
	}
	legacy_s32 repeat = HIRES_SCALE / scale;
	return cell + (y % scale) * repeat * HIRES_SCALE + (x % scale) * repeat;
}

void hires_argb_pixel(legacy_s32 x, legacy_s32 y, legacy_u32 color)
{
	legacy_s32 scale = hires_state.active_artwork ? HIRES_SCALE : hires_state.render_scale;
	legacy_u32 *sample = hires_argb_sample(x, y, scale);
	if (sample != NULL) {
		legacy_s32 repeat = HIRES_SCALE / scale;
		for (legacy_s32 row = 0; row < repeat; row++) {
			for (legacy_s32 column = 0; column < repeat; column++) {
				sample[row * HIRES_SCALE + column] = color;
			}
		}
	}
}

legacy_s32 hires_shadow_begin(void)
{
	return hires_state.enabled && hires_state.active != NULL &&
		   hires_allocate_argb(hires_state.active);
}

static void hires_shadow_sample(legacy_u32 *sample, legacy_u8 opacity)
{
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

void hires_shadow_pixel(legacy_s32 x, legacy_s32 y, legacy_u8 opacity)
{
	if (opacity == 0) {
		return;
	}
	legacy_u32 *sample = hires_argb_sample(x, y, hires_state.render_scale);
	if (sample == NULL) {
		return;
	}
	legacy_s32 repeat = HIRES_SCALE / hires_state.render_scale;
	for (legacy_s32 row = 0; row < repeat; row++) {
		for (legacy_s32 column = 0; column < repeat; column++) {
			hires_shadow_sample(sample + row * HIRES_SCALE + column, opacity);
		}
	}
}

legacy_s32 hires_write(const legacy_u8 *base, legacy_u16 offset, legacy_u8 color)
{
	if (widescreen_overlay_write(base, offset, color)) {
		if (widescreen_overlay_only()) {
			hires_state.generation++;
			return 1;
		}
		/* The legacy byte is about to change; retain its scene sample. */
		hires_cell(hires_create(base), offset);
		hires_state.generation++;
		return 0;
	}
	if (!hires_state.enabled || hires_state.active != NULL) {
		return 0;
	}
	struct HIRES_SURFACE *surface = hires_surface_find(base);
	if (surface != NULL && surface->valid[offset]) {
		/* A 2D write replaces the whole pixel even when its colour is unchanged. */
		hires_clear_argb(surface, offset);
		surface->valid[offset] = HIRES_CELL_EMPTY;
		hires_release_unused_argb(surface);
		hires_state.generation++;
	}
	return 0;
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

/* Copy companion cells in the same forward order as the legacy raster.
 * Distinct offsets name disjoint cells, including when the address wraps. */
static void hires_raster_copy_indexed(struct HIRES_SURFACE *dst, legacy_u16 destination_offset,
									  struct HIRES_SURFACE *src, const legacy_u8 *source,
									  legacy_u16 source_offset, legacy_u16 count)
{
	for (legacy_u32 index = 0; index < count; index++) {
		legacy_u16 so = (legacy_u16)(source_offset + index);
		legacy_u16 dest = (legacy_u16)(destination_offset + index);
		legacy_u8 *cell = dst->pixels + ((size_t)dest << hires_state.render_cell_shift);
		if (src != NULL && src->valid[so]) {
			const legacy_u8 *samples = src->pixels + ((size_t)so << hires_state.render_cell_shift);
			if (cell != samples) {
				memcpy(cell, samples, HIRES_CELL_PIXELS);
			}
		} else {
			memset(cell, source[so], hires_state.render_cell_pixels);
		}
		dst->valid[dest] = HIRES_CELL_INDEXED;
	}
}

legacy_s32 hires_raster(const legacy_u8 *destination, legacy_u16 destination_offset,
						const legacy_u8 *source, legacy_u16 source_offset, legacy_u16 count,
						legacy_s16 operation, const legacy_u8 *palette)
{
	if (widescreen_overlay_raster(destination, destination_offset, source, source_offset, count,
								  operation, palette)) {
		if (widescreen_overlay_only()) {
			hires_state.generation++;
			return 1;
		}
		struct HIRES_SURFACE *scene = hires_create(destination);
		for (legacy_u32 index = 0; index < count; index++) {
			hires_cell(scene, (legacy_u16)(destination_offset + index));
		}
		hires_state.generation++;
		return 0;
	}
	if (!hires_state.enabled || hires_state.active != NULL || count == 0) {
		return 0;
	}
	struct HIRES_SURFACE *src = hires_surface_find(source);
	struct HIRES_SURFACE *dst = hires_surface_find(destination);
	if (src == NULL && dst == NULL) {
		return 0;
	}
	if (dst == NULL) {
		dst = hires_create(destination);
	}
	legacy_s32 argb_present = dst->argb_cells != 0 || (src != NULL && src->argb_cells != 0);
	if (!argb_present && operation == SHAPE2D_RASTER_COPY) {
		hires_raster_copy_indexed(dst, destination_offset, src, source, source_offset, count);
	} else {
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
	}
	hires_release_unused_argb(dst);
	hires_state.generation++;
	return 0;
}

void hires_forget(const void *base)
{
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS)
	sdl3_video_mode_forget(base, 0);
#endif
	struct HIRES_SURFACE **link = &hires_state.surfaces;
	while (*link != NULL) {
		struct HIRES_SURFACE *surface = *link;
		if (surface->base == base) {
			if (surface == hires_state.active) {
				hires_state.active = NULL;
			}
			*link = surface->next;
			hires_free_surface(surface);
			hires_state.generation++;
			return;
		}
		link = &surface->next;
	}
}

void hires_forget_range(const void *base, legacy_u32 size)
{
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS)
	sdl3_video_mode_forget(base, size);
#endif
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
			hires_free_surface(surface);
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
