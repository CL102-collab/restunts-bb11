#ifndef RESTUNTS_HIRES_VIDEO_FRAMEBUFFER_H
#define RESTUNTS_HIRES_VIDEO_FRAMEBUFFER_H

#include "../legacy.h"

const legacy_u8 *hires_video_framebuffer(const legacy_u8 *legacy, legacy_s32 *width,
										 legacy_s32 *height);
void hires_video_copy_framebuffer_argb(const legacy_u8 *legacy, const legacy_u32 *palette,
									   legacy_u32 *destination, legacy_s32 pitch);

#endif
