#include "widescreen.h"
#include "hires.h"
#include "../platform.h"
#include "../shape2d/shape2d_internal.h"
#include <string.h>

#define WIDESCREEN_UI_PIXELS (VIDEO_FRAMEBUFFER_WIDTH * VIDEO_FRAMEBUFFER_HEIGHT)
#define WIDESCREEN_RATIO_COUNT 3
#define WIDESCREEN_OUTPUT_ROUNDING_ROWS 1
#define WIDESCREEN_VGA_SEGMENT 0xA000U

struct WIDESCREEN_RATIO {
	legacy_s32 numerator, denominator;
	legacy_s32 width, height;
};

static const struct WIDESCREEN_RATIO widescreen_ratios[WIDESCREEN_RATIO_COUNT] = {
	{16, 9, 1280, 720}, {16, 10, 1280, 800}, {3, 2, 1080, 720}};
static legacy_s32 widescreen_option;
static legacy_s32 widescreen_race;
static legacy_s32 widescreen_width;
static legacy_s32 widescreen_height;
static legacy_u32 widescreen_revision;
static legacy_u16 widescreen_modal_depth;
static legacy_u8 modal_background[WIDESCREEN_UI_PIXELS];
static legacy_u8 modal_background_saved;
static legacy_u8 overlay_pixels[WIDESCREEN_OVERLAY_COUNT][WIDESCREEN_UI_PIXELS];
static legacy_u8 overlay_valid[WIDESCREEN_OVERLAY_COUNT][WIDESCREEN_UI_PIXELS];
static const legacy_u8 *overlay_base;
static legacy_u16 overlay_origin;
static enum WIDESCREEN_OVERLAY overlay_kind;
static legacy_u8 overlay_hide_edges;
static legacy_s32 overlay_output_width, overlay_output_height;
static legacy_s16 overlay_source_x[WIDESCREEN_OVERLAY_COUNT][HIRES_WIDTH];
static legacy_s16 overlay_source_y[HIRES_HEIGHT];

void widescreen_dimensions_for_output(legacy_s32 width, legacy_s32 height, legacy_s32 *render_width,
									  legacy_s32 *render_height)
{
	*render_width = 0;
	*render_height = 0;
	if (width <= 0 || height <= 0) {
		return;
	}
	for (legacy_s32 index = 0; index < WIDESCREEN_RATIO_COUNT; index++) {
		const struct WIDESCREEN_RATIO *ratio = &widescreen_ratios[index];
		/* SDL rounds fractional CSS canvas sizes to integer output pixels. One
		 * extra output row must not demote an otherwise fitting display ratio. */
		legacy_s64 rounding = (legacy_s64)WIDESCREEN_OUTPUT_ROUNDING_ROWS * ratio->numerator;
		if ((legacy_s64)width * ratio->denominator + rounding >=
			(legacy_s64)height * ratio->numerator) {
			*render_width = ratio->width;
			*render_height = ratio->height;
			return;
		}
	}
}

void widescreen_set_enabled(legacy_s32 enabled)
{
	enabled = enabled != 0;
	if (widescreen_option != enabled) {
		widescreen_option = enabled;
		widescreen_revision++;
		overlay_base = NULL;
	}
}

legacy_s32 widescreen_enabled(void)
{
	return widescreen_option;
}

void widescreen_set_racing(legacy_s32 racing)
{
	racing = racing != 0;
	if (widescreen_race != racing) {
		widescreen_race = racing;
		widescreen_revision++;
		overlay_base = NULL;
	}
}

legacy_s32 widescreen_racing(void)
{
	return widescreen_race;
}

static void widescreen_capture_modal_background(void)
{
	legacy_u8 *legacy = dos_memory_make_pointer(WIDESCREEN_VGA_SEGMENT, 0);
	legacy_s32 width, height;
	/* Crop the world and centered cockpit first. Edge text is relocated below
	 * instead of leaving clipped glyphs along the narrower modal backdrop. */
	overlay_hide_edges = 1;
	const legacy_u8 *pixels = hires_framebuffer(legacy, &width, &height);
	overlay_hide_edges = 0;
	if (pixels == NULL) {
		return;
	}
	memcpy(modal_background, legacy, sizeof(modal_background));
	modal_background_saved = 1;
	legacy_s32 crop_width = height * WIDESCREEN_ASPECT_WIDTH / WIDESCREEN_ASPECT_HEIGHT;
	legacy_s32 left = (width - crop_width) / 2;
	/* Dialogs save and restore their own authored pixels. Seed their background
	 * from the centered 4:3 portion before changing companion dimensions. */
	for (legacy_s32 y = 0; y < VIDEO_FRAMEBUFFER_HEIGHT; y++) {
		legacy_s32 source_y = y * height / VIDEO_FRAMEBUFFER_HEIGHT;
		for (legacy_s32 x = 0; x < VIDEO_FRAMEBUFFER_WIDTH; x++) {
			legacy_s32 source_x = left + x * crop_width / VIDEO_FRAMEBUFFER_WIDTH;
			legacy_s32 offset = y * VIDEO_FRAMEBUFFER_WIDTH + x;
			legacy_u8 color = pixels[source_y * width + source_x];
			for (legacy_s32 kind = WIDESCREEN_OVERLAY_LEFT; kind < WIDESCREEN_OVERLAY_COUNT;
				 kind++) {
				if (overlay_valid[kind][offset]) {
					color = overlay_pixels[kind][offset];
				}
			}
			legacy[offset] = color;
		}
	}
	hires_forget(legacy);
}

void widescreen_push_modal(void)
{
	if (widescreen_modal_depth == LEGACY_U16_MAX) {
		return;
	}
	if (widescreen_modal_depth == 0) {
		modal_background_saved = 0;
		if (widescreen_active()) {
			widescreen_capture_modal_background();
		}
		widescreen_revision++;
		overlay_base = NULL;
	}
	widescreen_modal_depth++;
}

void widescreen_pop_modal(void)
{
	if (widescreen_modal_depth == 0) {
		return;
	}
	widescreen_modal_depth--;
	if (widescreen_modal_depth == 0) {
		if (modal_background_saved) {
			legacy_u8 *legacy = dos_memory_make_pointer(WIDESCREEN_VGA_SEGMENT, 0);
			memcpy(legacy, modal_background, sizeof(modal_background));
			hires_forget(legacy);
			modal_background_saved = 0;
		}
		widescreen_revision++;
		overlay_base = NULL;
	}
}

legacy_s32 widescreen_modal_active(void)
{
	return widescreen_modal_depth != 0;
}

void widescreen_set_output_size(legacy_s32 width, legacy_s32 height)
{
	legacy_s32 render_width, render_height;
	widescreen_dimensions_for_output(width, height, &render_width, &render_height);
	if (widescreen_width != render_width || widescreen_height != render_height) {
		widescreen_width = render_width;
		widescreen_height = render_height;
		widescreen_revision++;
		overlay_base = NULL;
	}
}

legacy_s32 widescreen_active(void)
{
	return widescreen_option && widescreen_race && widescreen_modal_depth == 0 && hires_enabled() &&
		   widescreen_width != 0;
}

legacy_s32 widescreen_render_width(void)
{
	return widescreen_active() ? widescreen_width : HIRES_WIDTH;
}

legacy_s32 widescreen_render_height(void)
{
	return widescreen_active() ? widescreen_height : HIRES_HEIGHT;
}

legacy_s32 widescreen_logical_width(void)
{
	return widescreen_render_width() / HIRES_SCALE;
}
legacy_s32 widescreen_logical_height(void)
{
	return widescreen_render_height() / HIRES_SCALE;
}
legacy_u32 widescreen_generation(void)
{
	return widescreen_revision;
}

void widescreen_begin_frame(void)
{
	overlay_base = NULL;
	if (widescreen_active()) {
		memset(overlay_valid, 0, sizeof(overlay_valid));
	}
}

void widescreen_overlay_begin(enum WIDESCREEN_OVERLAY overlay)
{
	overlay_base = NULL;
	if (widescreen_active() && overlay >= WIDESCREEN_OVERLAY_CENTER &&
		overlay < WIDESCREEN_OVERLAY_COUNT) {
		overlay_kind = overlay;
		overlay_base =
			dos_memory_make_pointer(dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
		overlay_origin = LEGACY_READ_U16_LE(drawing_sprite.sprite_lineofs);
	}
}

void widescreen_overlay_end(void)
{
	overlay_base = NULL;
}

static legacy_s32 widescreen_overlay_offset(const legacy_u8 *base, legacy_u16 offset)
{
	if (overlay_base == NULL || base != overlay_base || !widescreen_active()) {
		return -1;
	}
	legacy_u16 relative = (legacy_u16)(offset - overlay_origin);
	return relative < WIDESCREEN_UI_PIXELS ? relative : -1;
}

legacy_s32 widescreen_overlay_write(const legacy_u8 *base, legacy_u16 offset, legacy_u8 color)
{
	legacy_s32 index = widescreen_overlay_offset(base, offset);
	if (index < 0) {
		return 0;
	}
	overlay_pixels[overlay_kind][index] = color;
	overlay_valid[overlay_kind][index] = 1;
	return 1;
}

legacy_s32 widescreen_overlay_raster(const legacy_u8 *destination, legacy_u16 destination_offset,
									 const legacy_u8 *source, legacy_u16 source_offset,
									 legacy_u16 count, legacy_s16 operation,
									 const legacy_u8 *palette)
{
	if (overlay_base == NULL || destination != overlay_base || !widescreen_active()) {
		return 0;
	}
	for (legacy_u32 pixel = 0; pixel < count; pixel++) {
		legacy_s32 index =
			widescreen_overlay_offset(destination, (legacy_u16)(destination_offset + pixel));
		if (index < 0) {
			continue;
		}
		legacy_u8 value = source[(legacy_u16)(source_offset + pixel)];
		if ((operation == SHAPE2D_RASTER_AND && value == LEGACY_U8_MAX) ||
			(operation == SHAPE2D_RASTER_OR && value == 0)) {
			continue;
		}
		if (operation == SHAPE2D_RASTER_MAP) {
			value = palette[value];
			if (value == LEGACY_U8_MAX) {
				continue;
			}
		} else if (operation == SHAPE2D_RASTER_AND) {
			value &= overlay_valid[overlay_kind][index] ? overlay_pixels[overlay_kind][index] : 0;
		} else if (operation == SHAPE2D_RASTER_OR && overlay_valid[overlay_kind][index]) {
			value |= overlay_pixels[overlay_kind][index];
		}
		overlay_pixels[overlay_kind][index] = value;
		overlay_valid[overlay_kind][index] = 1;
	}
	return 1;
}

legacy_s32 widescreen_overlay_sample(legacy_s32 x, legacy_s32 y, legacy_s32 width,
									 legacy_s32 height, legacy_u8 *color)
{
	if (width <= 0 || height <= 0 || width > HIRES_WIDTH || height > HIRES_HEIGHT || x < 0 ||
		y < 0 || x >= width || y >= height) {
		return 0;
	}
	if (overlay_output_width != width || overlay_output_height != height) {
		overlay_output_width = width;
		overlay_output_height = height;
		legacy_s32 artwork_width = height * WIDESCREEN_ASPECT_WIDTH / WIDESCREEN_ASPECT_HEIGHT;
		for (legacy_s32 row = 0; row < height; row++) {
			overlay_source_y[row] = (legacy_s16)(row * VIDEO_FRAMEBUFFER_HEIGHT / height);
		}
		for (legacy_s32 kind = 0; kind < WIDESCREEN_OVERLAY_COUNT; kind++) {
			legacy_s32 left = kind == WIDESCREEN_OVERLAY_LEFT	 ? 0
							  : kind == WIDESCREEN_OVERLAY_RIGHT ? width - artwork_width
																 : (width - artwork_width) / 2;
			for (legacy_s32 column = 0; column < width; column++) {
				legacy_s32 relative = column - left;
				overlay_source_x[kind][column] =
					relative < 0 || relative >= artwork_width
						? -1
						: (legacy_s16)(relative * VIDEO_FRAMEBUFFER_WIDTH / artwork_width);
			}
		}
	}
	legacy_s32 source_y = overlay_source_y[y];
	legacy_s32 found = 0;
	for (legacy_s32 kind = WIDESCREEN_OVERLAY_CENTER; kind < WIDESCREEN_OVERLAY_COUNT; kind++) {
		if (overlay_hide_edges && kind != WIDESCREEN_OVERLAY_CENTER) {
			continue;
		}
		legacy_s32 source_x = overlay_source_x[kind][x];
		if (source_x < 0) {
			continue;
		}
		legacy_s32 offset = source_y * VIDEO_FRAMEBUFFER_WIDTH + source_x;
		if (overlay_valid[kind][offset]) {
			*color = overlay_pixels[kind][offset];
			found = 1;
		}
	}
	return found;
}
