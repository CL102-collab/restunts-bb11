#if defined(RESTUNTS_SDL3)
#include "showroom.h"
#include "../menu/menu_common.h"
#include "../video/video_frame.h"

void hires_showroom_presentation_initialize(struct HIRES_SHOWROOM *showroom)
{
	hires_showroom_initialize(showroom);
	showroom->prepare_started = 0;
	showroom->prepared_render_ns = 0;
	showroom->complete_frame = 0;
}

void hires_showroom_prepare_begin(struct HIRES_SHOWROOM *showroom)
{
	showroom->prepare_started = hires_presentation_now();
}

void hires_showroom_prepare_end(struct HIRES_SHOWROOM *showroom)
{
	/* Preview preparation contributes to render timing in both display modes.
	 * Exclude input handling and the wait before deferred rasterization. */
	showroom->prepared_render_ns = hires_presentation_now() - showroom->prepare_started;
}

void hires_showroom_present_begin(struct HIRES_SHOWROOM *showroom, legacy_u8 blit_mode)
{
	showroom->complete_frame = blit_mode == MENU_BLIT_MODE_REFRESH;
	if (showroom->complete_frame != 0) {
		/* Geometry is already queued at the current scale. Keep that scale and
		 * leave the initial dissolve free to present each of its phases. */
		sdl3_video_begin_track_frame(0);
		sdl3_video_add_render_work(showroom->prepared_render_ns);
	}
	showroom->prepared_render_ns = 0;
}

void hires_showroom_present_end(struct HIRES_SHOWROOM *showroom)
{
	if (showroom->complete_frame != 0) {
		sdl3_video_end_frame();
	}
}

#endif
