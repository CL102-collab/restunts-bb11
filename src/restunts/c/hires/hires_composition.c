#include "hires_internal.h"
#include "widescreen.h"
#include "render_workers.h"
#include <stdlib.h>
#include <string.h>

const legacy_u8 *hires_framebuffer(const legacy_u8 *legacy, legacy_s32 *width, legacy_s32 *height)
{
	hires_refresh_render_dimensions();
	if (!hires_state.enabled) {
		return NULL;
	}
	*width = hires_state.presentation_width;
	*height = hires_state.presentation_height;
	if (hires_state.framebuffer == NULL) {
		hires_state.framebuffer =
			hires_allocate(hires_state.presentation_width * hires_state.presentation_height);
	}
	struct HIRES_SURFACE *surface = hires_surface_find(legacy);
	const legacy_s32 scale = HIRES_SCALE, stride = hires_state.presentation_width;
	const legacy_s32 cell_shift = hires_state.render_cell_shift;
	for (legacy_s32 y = 0; y < hires_state.presentation_height / scale; y++) {
		for (legacy_s32 x = 0; x < hires_state.presentation_width / scale; x++) {
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
	if (widescreen_active()) {
		for (legacy_s32 y = 0; y < hires_state.presentation_height; y++) {
			for (legacy_s32 x = 0; x < hires_state.presentation_width; x++) {
				legacy_u8 color;
				if (widescreen_overlay_sample(x, y, hires_state.presentation_width,
											  hires_state.presentation_height, &color)) {
					hires_state.framebuffer[y * stride + x] = color;
				}
			}
		}
	}
	return hires_state.framebuffer;
}

#define HIRES_COMPOSE_JOB_ROWS 8
#define HIRES_COMPOSE_JOB_COUNT                                                                    \
	((HIRES_LOGICAL_HEIGHT + HIRES_COMPOSE_JOB_ROWS - 1) / HIRES_COMPOSE_JOB_ROWS)

struct HIRES_COMPOSE_JOB {
	const legacy_u8 *legacy;
	const legacy_u32 *palette;
	const struct HIRES_SURFACE *surface;
	legacy_u32 *destination;
	legacy_s32 pitch;
};

/* Indexed cells have only the supported 4x4, 2x2, or 1x1 layouts. Fixed-width
 * stores expose the common rows without a dynamic inner sample loop. */
static void hires_compose_palette_cell(const legacy_u8 *indices, const legacy_u8 *fallback,
									   const legacy_u32 *palette, legacy_u32 *output,
									   legacy_s32 pitch, legacy_s32 scale)
{
	/* Uniform companion cells can share one palette lookup. Their index may
	 * differ from the legacy fallback, and every palette bit must survive. */
	if (indices != NULL && scale == HIRES_SCALE) {
		legacy_u32 words[HIRES_CELL_PIXELS / sizeof(legacy_u32)];
		memcpy(words, indices, sizeof(words));
		legacy_u32 repeated = indices[0] * (LEGACY_U32_MAX / LEGACY_U8_MAX);
		if (((words[0] ^ repeated) | (words[1] ^ repeated) | (words[2] ^ repeated) |
			 (words[3] ^ repeated)) == 0) {
			fallback = indices;
			indices = NULL;
		}
	}
	if (indices == NULL) {
		legacy_u32 color = palette[*fallback];
		if (scale == HIRES_SCALE) {
			for (legacy_s32 row = 0; row < HIRES_SCALE; row++) {
				output[0] = color;
				output[1] = color;
				output[2] = color;
				output[3] = color;
				output = (legacy_u32 *)((legacy_u8 *)output + pitch);
			}
		} else if (scale == HIRES_MEDIUM_SCALE) {
			for (legacy_s32 row = 0; row < HIRES_MEDIUM_SCALE; row++) {
				output[0] = color;
				output[1] = color;
				output = (legacy_u32 *)((legacy_u8 *)output + pitch);
			}
		} else {
			for (legacy_s32 row = 0; row < scale; row++) {
				for (legacy_s32 column = 0; column < scale; column++) {
					output[column] = color;
				}
				output = (legacy_u32 *)((legacy_u8 *)output + pitch);
			}
		}
		return;
	}
	if (scale == HIRES_SCALE) {
		for (legacy_s32 row = 0; row < HIRES_SCALE; row++) {
			output[0] = palette[indices[0]];
			output[1] = palette[indices[1]];
			output[2] = palette[indices[2]];
			output[3] = palette[indices[3]];
			indices += HIRES_SCALE;
			output = (legacy_u32 *)((legacy_u8 *)output + pitch);
		}
	} else if (scale == HIRES_MEDIUM_SCALE) {
		for (legacy_s32 row = 0; row < HIRES_MEDIUM_SCALE; row++) {
			output[0] = palette[indices[0]];
			output[1] = palette[indices[1]];
			indices += HIRES_MEDIUM_SCALE;
			output = (legacy_u32 *)((legacy_u8 *)output + pitch);
		}
	} else {
		for (legacy_s32 row = 0; row < scale; row++) {
			for (legacy_s32 column = 0; column < scale; column++) {
				output[column] = palette[indices[row * scale + column]];
			}
			output = (legacy_u32 *)((legacy_u8 *)output + pitch);
		}
	}
}

static void hires_compose_argb_rows(const struct HIRES_COMPOSE_JOB *job, legacy_s32 top,
									legacy_s32 bottom)
{
	const legacy_u8 *legacy = job->legacy;
	const legacy_u32 *palette = job->palette;
	const struct HIRES_SURFACE *surface = job->surface;
	legacy_u32 *destination = job->destination;
	legacy_s32 pitch = job->pitch;
	/* Palette entry 15 is the menu's white. Match its fade so artwork follows
	 * the same black-to-white transition as the original indexed pixels. */
	legacy_u32 fade = palette[HIRES_MENU_WHITE_INDEX];
	const legacy_s32 scale = HIRES_SCALE, cell_shift = hires_state.render_cell_shift;
	for (legacy_s32 y = top; y < bottom; y++) {
		for (legacy_s32 x = 0; x < hires_state.presentation_width / scale; x++) {
			legacy_u32 offset = y * HIRES_LOGICAL_WIDTH + x;
			legacy_u8 valid = surface != NULL ? surface->valid[offset] : HIRES_CELL_EMPTY;
			const legacy_u8 *indices =
				valid != HIRES_CELL_EMPTY ? surface->pixels + ((size_t)offset << cell_shift) : NULL;
			legacy_u32 *output =
				(legacy_u32 *)((legacy_u8 *)destination + y * scale * pitch) + x * scale;
			if (valid != HIRES_CELL_ARGB) {
				/* Full-color overlays retain their general alpha/fade path. */
				hires_compose_palette_cell(indices, legacy + offset, palette, output, pitch, scale);
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

static void hires_compose_argb_job(void *argument, legacy_s32 index)
{
	const struct HIRES_COMPOSE_JOB *job = argument;
	legacy_s32 top = index * HIRES_COMPOSE_JOB_ROWS;
	legacy_s32 bottom = top + HIRES_COMPOSE_JOB_ROWS;
	if (bottom > hires_state.presentation_height / HIRES_SCALE) {
		bottom = hires_state.presentation_height / HIRES_SCALE;
	}
	hires_compose_argb_rows(job, top, bottom);
}

static void hires_compose_argb(const legacy_u8 *legacy, const legacy_u32 *palette,
							   const struct HIRES_SURFACE *surface, legacy_u32 *destination,
							   legacy_s32 pitch)
{
	struct HIRES_COMPOSE_JOB job = {legacy, palette, surface, destination, pitch};
	/* Composition reads frozen surfaces and owns complete output rows. Leave
	 * unusual overlapping pitches and the smallest framebuffer serial. */
	if (HIRES_SCALE > HIRES_MINIMUM_SCALE &&
		pitch >= (legacy_s32)(hires_state.presentation_width * sizeof(*destination)) &&
		hires_render_workers_count() != 0) {
		hires_render_workers_run(HIRES_COMPOSE_JOB_COUNT, hires_compose_argb_job, &job);
	} else {
		hires_compose_argb_rows(&job, 0, hires_state.presentation_height / HIRES_SCALE);
	}
	if (widescreen_active()) {
		for (legacy_s32 y = 0; y < hires_state.presentation_height; y++) {
			legacy_u32 *row = (legacy_u32 *)((legacy_u8 *)destination + y * pitch);
			for (legacy_s32 x = 0; x < hires_state.presentation_width; x++) {
				legacy_u8 color;
				if (widescreen_overlay_sample(x, y, hires_state.presentation_width,
											  hires_state.presentation_height, &color)) {
					row[x] = palette[color];
				}
			}
		}
	}
}

void hires_copy_framebuffer_argb(const legacy_u8 *legacy, const legacy_u32 *palette,
								 legacy_u32 *destination, legacy_s32 pitch)
{
	hires_refresh_render_dimensions();
	if (!hires_state.enabled) {
		return;
	}
	hires_compose_argb(legacy, palette, hires_surface_find(legacy), destination, pitch);
}

const legacy_u32 *hires_framebuffer_argb(const legacy_u8 *legacy, const legacy_u32 *palette)
{
	hires_refresh_render_dimensions();
	struct HIRES_SURFACE *surface = hires_state.enabled ? hires_surface_find(legacy) : NULL;
	if (surface == NULL || surface->argb_cells == 0) {
		free(hires_state.argb_framebuffer);
		hires_state.argb_framebuffer = NULL;
		return NULL;
	}
	if (hires_state.argb_framebuffer == NULL) {
		hires_state.argb_framebuffer =
			malloc((size_t)hires_state.presentation_width * hires_state.presentation_height *
				   sizeof(*hires_state.argb_framebuffer));
		if (hires_state.argb_framebuffer == NULL) {
			return NULL;
		}
	}
	hires_compose_argb(legacy, palette, surface, hires_state.argb_framebuffer,
					   hires_state.presentation_width * sizeof(*hires_state.argb_framebuffer));
	return hires_state.argb_framebuffer;
}
