#if !defined(RESTUNTS_SDL3)
#include "settings.h"
#include "frame_overlay.h"
#include "../externs.h"

legacy_u8 supersight_enabled;

void supersight_settings_select_display(legacy_s16 key)
{
	(void)key;
	supersight_enabled ^= 1U;
	frame_supersight_reset();
	frame_supersight_show_status(supersight_enabled != 0 ? "On" : "Off");
	full_redraw_frames_remaining = (legacy_s8)video_page_count;
}
#endif
