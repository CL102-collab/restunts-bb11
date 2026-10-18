#ifndef RESTUNTS_AMIGA_H
#define RESTUNTS_AMIGA_H

#include "../../c/legacy.h"

#define AMIGA_WIDTH 320U
#define AMIGA_HEIGHT 200U
#define AMIGA_DEPTH 5U
#define AMIGA_COLORS 32U
#define AMIGA_ROW_BYTES 40U
#define AMIGA_PLANE_BYTES 8000U
#define AMIGA_SCREEN_BYTES 64000UL
#define AMIGA_SYSTEM_HEADROOM (8UL * 1024UL)

struct Window;

struct AMIGA_PALETTE {
	legacy_u8 rgb[256][3];
	legacy_u8 map[256];
	legacy_u16 base_colors[AMIGA_COLORS];
	legacy_u16 colors[AMIGA_COLORS];
	const legacy_u8 *original_map;
	legacy_u8 reference_peak;
	legacy_u8 initialized;
	legacy_u8 count, first_register, world;
};

const legacy_u16 *amiga_palette_original(const char *resource, const char *chunk);
void amiga_palette_select(struct AMIGA_PALETTE *palette, const char *resource, const char *chunk,
						  legacy_u8 count, legacy_u8 first_register);
void amiga_palette_update(struct AMIGA_PALETTE *palette, legacy_u16 first, legacy_u16 count,
						  const legacy_u8 *rgb);
void amiga_chunky_to_planar(const legacy_u8 *pixels, const legacy_u8 *map,
							legacy_u8 *planes[AMIGA_DEPTH], legacy_u16 bytes_per_row);
void amiga_chunky_to_planar_rows(const legacy_u8 *pixels, const legacy_u8 *map,
								 legacy_u8 *planes[AMIGA_DEPTH], legacy_u16 bytes_per_row,
								 legacy_u16 rows);
void amiga_video_palette_scene(const char *resource, const char *chunk);
void amiga_video_palette_race(const char *car, legacy_u8 active);
legacy_u16 amiga_video_color(legacy_u16 y, legacy_u8 pixel);
legacy_u8 *amiga_video_pixels(void);
legacy_u8 *amiga_video_memory_base(void);
legacy_u16 amiga_video_buffer_count(void);
struct Window *amiga_video_window(void);
void amiga_video_present(void);
void amiga_video_begin_frame(void);
legacy_u8 amiga_video_set_auto_refresh(legacy_u8 enabled);
void amiga_video_refresh(void);
void amiga_video_shutdown(void);
void amiga_input_pump(void);
void amiga_timer_pump(void);
legacy_u32 amiga_clock_ticks(void);
void amiga_platform_pump(void);
void amiga_platform_poll(void);
void amiga_platform_shutdown(void);
void amiga_memory_shutdown(void);
void amiga_file_shutdown(void);
legacy_s16 amiga_audio_selftest(void);
void amiga_audio_begin_update(void);
void amiga_audio_end_update(void);
legacy_s16 amiga_game_smoke(void);

#endif
