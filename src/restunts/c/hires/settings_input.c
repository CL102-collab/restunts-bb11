#if defined(RESTUNTS_SDL3)
#include "settings.h"
#include "../keyboard.h"
#include "../externs.h"
#include "../frame/frame_internal.h"
#include "frame_overlay.h"
#include "frame_adaptive.h"
#include "hires.h"

legacy_u8 hires_active;

void hires_settings_select_display(legacy_s16 key)
{
	static const legacy_s8 *preset_names[] = {"Auto", "Full", "High", "Medium", "Low"};
	legacy_u8 was_enabled = hires_active;
	if (key == KEY_SHIFT_F12) {
		legacy_u16 preset =
			hires_active != 0 ? frame_hires_adaptive.preset : FRAME_HIRES_ADAPTIVE_PRESET_AUTO;
		preset = preset >= FRAME_HIRES_ADAPTIVE_PRESET_LOW ? FRAME_HIRES_ADAPTIVE_PRESET_FULL
														   : preset + 1U;
		frame_hires_adaptive_set_preset(&frame_hires_adaptive,
										(enum FRAME_HIRES_ADAPTIVE_PRESET)preset);
		hires_active = 1;
	} else {
		hires_active ^= 1U;
		frame_hires_adaptive_set_preset(&frame_hires_adaptive, FRAME_HIRES_ADAPTIVE_PRESET_AUTO);
	}
	if (was_enabled != hires_active) {
		frame_render_timing_reset();
		hires_set_enabled(hires_active);
	}
	if (hires_active != 0) {
		hires_set_render_scale(frame_hires_adaptive_render_scale(&frame_hires_adaptive));
	}
	frame_hires_reset();
	frame_hires_show_status(hires_active != 0 ? preset_names[frame_hires_adaptive.preset]
											  : (const legacy_s8 *)"Off");
	full_redraw_frames_remaining = (legacy_s8)video_page_count;
}
#endif
