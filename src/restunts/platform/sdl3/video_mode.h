#ifndef RESTUNTS_SDL3_VIDEO_MODE_H
#define RESTUNTS_SDL3_VIDEO_MODE_H

#include "../../c/legacy.h"

struct SPRITE;
struct SHAPE2D;

enum SDL3_VIDEO_MODE { SDL3_VIDEO_MODE_VGA = 0, SDL3_VIDEO_MODE_EGA = 1, SDL3_VIDEO_MODE_CGA = 2 };
enum SDL3_VIDEO_NATIVE_COLOR {
	SDL3_VIDEO_NATIVE_PRIMARY = 0xF8,
	SDL3_VIDEO_NATIVE_SECONDARY = 0xF9,
	SDL3_VIDEO_NATIVE_TERTIARY = 0xFA
};

extern legacy_u8 sdl3_video_mode_selected;

/* Select classic graphics before startup; HyperVision always uses VGA. */
void sdl3_video_set_startup_mode(legacy_u8 mode);
void sdl3_video_mode_target(const struct SPRITE *sprite);
void sdl3_video_mode_source_begin(const struct SHAPE2D *shape, legacy_u16 draw_y);
void sdl3_video_mode_source_begin_at(const struct SHAPE2D *shape, legacy_u16 draw_x,
									 legacy_u16 draw_y);
void sdl3_video_mode_source_sample(legacy_u16 source_x, legacy_u16 source_y);
void sdl3_video_mode_source_end(void);

/* Update composed classic samples before the corresponding VGA-byte write.
 * A true result means a native-material pass consumed the write itself. */
legacy_u8 sdl3_video_mode_write(const legacy_u8 *base, legacy_u16 offset, legacy_u8 color);
legacy_u8 sdl3_video_mode_raster(const legacy_u8 *destination, legacy_u16 destination_offset,
								 const legacy_u8 *source, legacy_u16 source_offset,
								 legacy_u16 count, legacy_s16 operation, const legacy_u8 *palette);
void sdl3_video_mode_copy_sample(const legacy_u8 *destination, legacy_u16 destination_offset,
								 const legacy_u8 *source, legacy_u16 source_offset);
void sdl3_video_mode_xor_begin(const legacy_u8 *base, legacy_u16 offset, legacy_u8 mask);

void sdl3_video_mode_primitive_begin(void);
void sdl3_video_mode_primitive_capture(const legacy_u16 colors[3]);
void sdl3_video_mode_primitive_end(void);
void sdl3_video_mode_line_begin(void);
void sdl3_video_mode_line_end(void);
void sdl3_video_mode_point_begin(void);
void sdl3_video_mode_point_end(void);

/* Paint a procedural DOS color while keeping the ordinary VGA-byte writes. */
void sdl3_video_mode_direct_begin(legacy_u16 ega_word, legacy_u16 cga_word);
void sdl3_video_mode_direct_end(void);

/* Return hardware palette indices without changing the original VGA bytes. */
const legacy_u8 *sdl3_video_mode_framebuffer(const legacy_u8 *pixels);
void sdl3_video_mode_palette(const legacy_u32 *palette);
legacy_u32 sdl3_video_mode_generation(void);
void sdl3_video_mode_forget(const void *base, legacy_u32 size);
void sdl3_video_mode_shutdown(void);

#endif
