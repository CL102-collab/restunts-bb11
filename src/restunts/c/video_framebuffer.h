#ifndef RESTUNTS_VIDEO_FRAMEBUFFER_H
#define RESTUNTS_VIDEO_FRAMEBUFFER_H

#include "legacy.h"

#define VIDEO_FRAMEBUFFER_WIDTH 320
#define VIDEO_FRAMEBUFFER_HEIGHT 200

/* Select the active raster without changing the original indexed framebuffer. */
const legacy_u8 *video_framebuffer(const legacy_u8 *legacy, legacy_s32 *width, legacy_s32 *height);
/* Convert the active raster into caller-owned rows. Pitch is measured in bytes. */
void video_copy_framebuffer_argb(const legacy_u8 *legacy, const legacy_u32 *palette,
								 legacy_u32 *destination, legacy_s32 pitch);

#endif
