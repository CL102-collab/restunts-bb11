#include "settings.h"
#include "../keyboard.h"
#include "../externs.h"
#include "../game_input.h"
#include "../frame/frame_internal.h"
#ifdef RESTUNTS_SDL3
#include "frame_adaptive.h"
#include "hires.h"
#endif

legacy_u8 supersight_enabled;

void hires_settings_select_display(legacy_s16 key)
{
#ifdef RESTUNTS_SDL3
	static const legacy_s8 *preset_names[] = {"Auto", "Full", "High", "Medium", "Low"};
	legacy_u8 was_enabled = supersight_enabled;
	if (key == KEY_SHIFT_F12) {
		legacy_u16 preset =
			supersight_enabled != 0 ? frame_adaptive.preset : FRAME_ADAPTIVE_PRESET_AUTO;
		preset = preset >= FRAME_ADAPTIVE_PRESET_LOW ? FRAME_ADAPTIVE_PRESET_FULL : preset + 1U;
		frame_adaptive_set_preset(&frame_adaptive, (enum FRAME_ADAPTIVE_PRESET)preset);
		supersight_enabled = 1;
	} else {
		supersight_enabled ^= 1U;
		frame_adaptive_set_preset(&frame_adaptive, FRAME_ADAPTIVE_PRESET_AUTO);
	}
	if (was_enabled != supersight_enabled) {
		frame_render_timing_reset();
		hires_set_enabled(supersight_enabled);
	}
	if (supersight_enabled != 0) {
		hires_set_render_scale(frame_adaptive_render_scale(&frame_adaptive));
	}
	frame_supersight_reset();
	frame_supersight_show_status(supersight_enabled != 0 ? preset_names[frame_adaptive.preset]
														 : (const legacy_s8 *)"Off");
	full_redraw_frames_remaining = (legacy_s8)video_page_count;
#else
	(void)key;
	supersight_enabled ^= 1U;
	frame_supersight_reset();
	frame_supersight_show_status(supersight_enabled != 0 ? "On" : "Off");
	full_redraw_frames_remaining = (legacy_s8)video_page_count;
#endif
}
