#include "video_framebuffer.h"
#ifdef RESTUNTS_SDL3
#include "../hires/hires.h"
#endif

const legacy_u8 *video_framebuffer(const legacy_u8 *legacy, legacy_s32 *width, legacy_s32 *height)
{
#ifdef RESTUNTS_SDL3
	if (hires_enabled()) {
		return hires_framebuffer(legacy, width, height);
	}
#endif
	*width = VIDEO_FRAMEBUFFER_WIDTH;
	*height = VIDEO_FRAMEBUFFER_HEIGHT;
	return legacy;
}

void video_copy_framebuffer_argb(const legacy_u8 *legacy, const legacy_u32 *palette,
								 legacy_u32 *destination, legacy_s32 pitch)
{
#ifdef RESTUNTS_SDL3
	if (hires_enabled()) {
		hires_copy_framebuffer_argb(legacy, palette, destination, pitch);
		return;
	}
#endif
	for (legacy_s32 row = 0; row < VIDEO_FRAMEBUFFER_HEIGHT; row++) {
		legacy_u32 *output = (legacy_u32 *)((legacy_u8 *)destination + row * pitch);
		for (legacy_s32 column = 0; column < VIDEO_FRAMEBUFFER_WIDTH; column++) {
			output[column] = palette[legacy[row * VIDEO_FRAMEBUFFER_WIDTH + column]];
		}
	}
}
