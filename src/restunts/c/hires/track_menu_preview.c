#include "track_menu_preview.h"

#ifdef RESTUNTS_SDL3
#include "../platform.h"
#include "../shape2d/shape2d.h"
#include "../shape3d/shape3d.h"
#include "../skybox.h"
#include "../menu/menu_internal.h"
#include "../frame/frame_fps.h"
#include "../video/video_frame.h"

#define TRACK_MENU_PREVIEW_SCREEN_WIDTH 320U
#define TRACK_MENU_PREVIEW_SCREEN_HEIGHT 200U
#define TRACK_MENU_PREVIEW_TOP 0U
#define TRACK_MENU_PREVIEW_BOTTOM 166U
#define TRACK_MENU_PREVIEW_FRAMES_PER_SECOND 25UL
#define TRACK_MENU_PREVIEW_FRAME_TICKS                                                             \
	(DOS_TIMER_REALTIME_TICKS_PER_SECOND / TRACK_MENU_PREVIEW_FRAMES_PER_SECOND)

void hires_track_menu_preview_retain(struct HIRES_TRACK_MENU_PREVIEW *preview)
{
	preview->live = 1;
	preview->tick = dos_timer_get_realtime_counter();
	frame_fps_reset();
	frame_render_timing_reset();
}

void hires_track_menu_preview_release(struct HIRES_TRACK_MENU_PREVIEW *preview)
{
	if (preview->live != 0) {
		shape3d_free_all();
		unload_skybox();
		preview->live = 0;
	}
}

void hires_track_menu_preview_cache_score(struct HIRES_TRACK_MENU_PREVIEW *preview,
										  const legacy_s8 *text, const legacy_u8 *offsets)
{
	if (preview->live != 0) {
		preview->score_visible = 1;
		memcpy(preview->score_offsets, offsets, sizeof(preview->score_offsets));
		memcpy(preview->score_text, text, sizeof(preview->score_text));
	}
}

void hires_track_menu_preview_present(struct HIRES_TRACK_MENU_PREVIEW *preview,
									  HIRES_TRACK_MENU_DRAW_HEADER draw_header)
{
	if (preview->live == 0) {
		return;
	}
	legacy_u32 tick = dos_timer_get_realtime_counter();
	if (tick - preview->tick < TRACK_MENU_PREVIEW_FRAME_TICKS) {
		return;
	}
	preview->tick = tick;
	/* Keep the selected render scale and measure only completed preview frames,
	 * excluding the pacing wait above and presentation synchronization. */
	sdl3_video_begin_track_frame(0);
	/* The scene fills the header too; repaint its text without rereading scores. */
	mouse_draw_opaque_check();
	sprite_select_render_window();
	draw_track_preview();
	draw_header(preview);
	sprite_select_screen_compat();
	sprite_set_target_clip_bounds(0, TRACK_MENU_PREVIEW_SCREEN_WIDTH, TRACK_MENU_PREVIEW_TOP,
								  TRACK_MENU_PREVIEW_BOTTOM);
	sprite_putimage(render_window_sprite->sprite_bitmapptr);
	sprite_set_target_clip_bounds(0, TRACK_MENU_PREVIEW_SCREEN_WIDTH, 0,
								  TRACK_MENU_PREVIEW_SCREEN_HEIGHT);
	mouse_draw_transparent_check();
	frame_fps_record_presented();
	sdl3_video_end_frame();
}
#endif
