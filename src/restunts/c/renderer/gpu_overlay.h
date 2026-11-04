#ifndef RESTUNTS_GPU_OVERLAY_H
#define RESTUNTS_GPU_OVERLAY_H

#include "../legacy.h"
#include <stddef.h>

struct HIRES_RASTER_TARGET;
struct HIRES_SURFACE;
struct SPRITE;

/* Zero means the Software/Metal hot paths pay only an inactive branch. */
extern legacy_u8 gpu_overlay_tracking;
void gpu_overlay_reset(void);
void gpu_overlay_shutdown(void);
void gpu_overlay_begin(const struct HIRES_RASTER_TARGET *target);
void gpu_overlay_begin_sprite(const struct SPRITE *sprite, legacy_s32 width, legacy_s32 height);
void gpu_overlay_span(const struct HIRES_RASTER_TARGET *target, legacy_s32 left, legacy_s32 right,
					  legacy_s32 y);
void gpu_overlay_write(const legacy_u8 *base, legacy_u16 offset, legacy_s32 sample);
void gpu_overlay_shadow(const legacy_u8 *base, legacy_u16 offset, legacy_s32 sample,
						legacy_u8 opacity);
void gpu_overlay_raster(const legacy_u8 *destination, legacy_u16 destination_offset,
						const legacy_u8 *source, legacy_u16 source_offset, legacy_u16 count,
						legacy_s16 operation, const legacy_u8 *palette);
void gpu_overlay_forget(const void *base, legacy_u32 size);
/* Snapshot metadata before joined CPU composition. Returned masks are cell-major. */
const legacy_u8 *gpu_overlay_mask(const legacy_u8 *base, legacy_s32 width, legacy_s32 height,
								  legacy_s32 *offset_x, legacy_s32 *offset_y);
void gpu_overlay_compose_rows(const legacy_u8 *legacy, const legacy_u32 *palette,
							  const struct HIRES_SURFACE *surface, const legacy_u8 *mask,
							  legacy_u32 *destination, legacy_s32 pitch, legacy_s32 scale,
							  legacy_s32 top, legacy_s32 bottom);
/* Converts the completed CPU page to a foreground alpha texture. RGB remains
 * usable for the opaque background draw; only partial black shadows need RGB0. */
legacy_s32 gpu_overlay_compose(const legacy_u8 *base, legacy_u32 *pixels, legacy_s32 width,
							   legacy_s32 height, legacy_s32 *offset_x, legacy_s32 *offset_y);

#endif
