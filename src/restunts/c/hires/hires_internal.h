#ifndef RESTUNTS_HIRES_INTERNAL_H
#define RESTUNTS_HIRES_INTERNAL_H

#include "hires.h"
#include "../shape2d/shape2d.h"
#include <stddef.h>
#include <string.h>

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

/* Each legacy pixel always carries 4x4 presentation samples. Reduced scene
 * samples cover larger blocks without changing artwork or saved-sprite detail.
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

struct HIRES_STATE {
	struct HIRES_SURFACE *surfaces;
	struct HIRES_SURFACE *active;
	struct SPRITE active_sprite;
	legacy_s32 enabled;
	legacy_s32 active_artwork;
	legacy_s32 render_scale;
	legacy_s32 render_shift;
	legacy_s32 render_mask;
	legacy_s32 render_cell_pixels;
	legacy_s32 render_cell_shift;
	legacy_s32 render_width;
	legacy_s32 render_height;
	legacy_s32 presentation_width;
	legacy_s32 presentation_height;
	legacy_u32 generation;
	legacy_u8 *framebuffer;
	legacy_u32 *argb_framebuffer;
	legacy_f32 *inverse_depth;
	legacy_u32 *depth_family;
	legacy_s32 depth_left, depth_right, depth_top, depth_bottom;
};

extern struct HIRES_STATE hires_state;
void *hires_allocate(size_t size);
struct HIRES_SURFACE *hires_surface_find(const legacy_u8 *base);
void hires_refresh_render_dimensions(void);
legacy_u8 *hires_cell(struct HIRES_SURFACE *surface, legacy_u16 offset);
void hires_depth_reset(void);

/* Input scene cells have scale*scale samples; stored presentation cells are4x4. */
static inline void hires_expand_cell(legacy_u8 *destination, const legacy_u8 *samples,
									 legacy_s32 scale)
{
	if (scale == HIRES_SCALE) {
		memcpy(destination, samples, HIRES_CELL_PIXELS);
		return;
	}
	legacy_s32 repeat = HIRES_SCALE / scale;
	for (legacy_s32 y = 0; y < HIRES_SCALE; y++) {
		for (legacy_s32 x = 0; x < HIRES_SCALE; x++) {
			destination[y * HIRES_SCALE + x] = samples[y / repeat * scale + x / repeat];
		}
	}
}

#endif
