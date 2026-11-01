#include "video_framebuffer.h"
#include "hires.h"
#include "../video/video_framebuffer.h"

const legacy_u8 *hires_video_framebuffer(const legacy_u8 *legacy, legacy_s32 *width,
										 legacy_s32 *height)
{
	if (hires_enabled()) {
		return hires_framebuffer(legacy, width, height);
	}
	return video_legacy_framebuffer(legacy, width, height);
}

void hires_video_copy_framebuffer_argb(const legacy_u8 *legacy, const legacy_u32 *palette,
									   legacy_u32 *destination, legacy_s32 pitch)
{
	if (hires_enabled()) {
		hires_copy_framebuffer_argb(legacy, palette, destination, pitch);
		return;
	}
	video_copy_legacy_framebuffer_argb(legacy, palette, destination, pitch);
}
