#include "video_framebuffer.h"
#ifdef RESTUNTS_SDL3
#include "../hires/video_framebuffer.h"
#endif

const legacy_u8 *video_legacy_framebuffer(const legacy_u8 *legacy, legacy_s32 *width,
										  legacy_s32 *height)
{
	*width = VIDEO_FRAMEBUFFER_WIDTH;
	*height = VIDEO_FRAMEBUFFER_HEIGHT;
	return legacy;
}

void video_copy_legacy_framebuffer_argb(const legacy_u8 *legacy, const legacy_u32 *palette,
										legacy_u32 *destination, legacy_s32 pitch)
{
	for (legacy_s32 row = 0; row < VIDEO_FRAMEBUFFER_HEIGHT; row++) {
		legacy_u32 *output = (legacy_u32 *)((legacy_u8 *)destination + row * pitch);
		for (legacy_s32 column = 0; column < VIDEO_FRAMEBUFFER_WIDTH; column++) {
			output[column] = palette[legacy[row * VIDEO_FRAMEBUFFER_WIDTH + column]];
		}
	}
}

const legacy_u8 *video_framebuffer(const legacy_u8 *legacy, legacy_s32 *width, legacy_s32 *height)
{
#ifdef RESTUNTS_SDL3
	return hires_video_framebuffer(legacy, width, height);
#else
	return video_legacy_framebuffer(legacy, width, height);
#endif
}

void video_copy_framebuffer_argb(const legacy_u8 *legacy, const legacy_u32 *palette,
								 legacy_u32 *destination, legacy_s32 pitch)
{
#ifdef RESTUNTS_SDL3
	hires_video_copy_framebuffer_argb(legacy, palette, destination, pitch);
#else
	video_copy_legacy_framebuffer_argb(legacy, palette, destination, pitch);
#endif
}
