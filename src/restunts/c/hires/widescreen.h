#ifndef RESTUNTS_WIDESCREEN_H
#define RESTUNTS_WIDESCREEN_H

#include "../legacy.h"

#define WIDESCREEN_ASPECT_WIDTH 4
#define WIDESCREEN_ASPECT_HEIGHT 3
#define WIDESCREEN_CLASSIC_PRESENTATION_HEIGHT 240

enum WIDESCREEN_OVERLAY {
	WIDESCREEN_OVERLAY_CENTER,
	WIDESCREEN_OVERLAY_LEFT,
	WIDESCREEN_OVERLAY_RIGHT,
	WIDESCREEN_OVERLAY_COUNT
};

void widescreen_set_enabled(legacy_s32 enabled);
legacy_s32 widescreen_enabled(void);
void widescreen_set_racing(legacy_s32 racing);
legacy_s32 widescreen_racing(void);
/* Modal UI retains authored 320x200 coordinates and 4:3 presentation. */
void widescreen_push_modal(void);
void widescreen_pop_modal(void);
legacy_s32 widescreen_modal_active(void);
void widescreen_set_output_size(legacy_s32 width, legacy_s32 height);
void widescreen_dimensions_for_output(legacy_s32 width, legacy_s32 height, legacy_s32 *render_width,
									  legacy_s32 *render_height);
legacy_s32 widescreen_active(void);
/* Maximum dimensions; HyperVision quality selects full, half or quarter. */
legacy_s32 widescreen_render_width(void);
legacy_s32 widescreen_render_height(void);
legacy_s32 widescreen_logical_width(void);
legacy_s32 widescreen_logical_height(void);
legacy_u32 widescreen_generation(void);

/* Artwork uses authored 320x200 coordinates, with 4:3 proportions. Redraw its
 * independent layer every wide frame; HUD layers move to the corresponding edge. */
void widescreen_begin_frame(void);
void widescreen_overlay_begin(enum WIDESCREEN_OVERLAY overlay);
void widescreen_overlay_end(void);
legacy_s32 widescreen_overlay_write(const legacy_u8 *base, legacy_u16 offset, legacy_u8 color);
legacy_s32 widescreen_overlay_raster(const legacy_u8 *destination, legacy_u16 destination_offset,
									 const legacy_u8 *source, legacy_u16 source_offset,
									 legacy_u16 count, legacy_s16 operation,
									 const legacy_u8 *palette);
legacy_s32 widescreen_overlay_sample(legacy_s32 x, legacy_s32 y, legacy_s32 width,
									 legacy_s32 height, legacy_u8 *color);

#endif
