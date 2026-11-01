#include "race_internal.h"
#include "../extended/extended_replay_ui.h"
#ifdef RESTUNTS_AMIGA_TRACE
#include <stdio.h>
#endif
#ifdef RESTUNTS_SDL3
#include "../../platform/sdl3/sdl3.h"
#include "../../platform/sdl3/touch_game.h"
#include "../../platform/sdl3/controller_game.h"
#include "../extended/extended_replay_runtime.h"
#endif
#ifdef RESTUNTS_AMIGA
#include "../../platform/amiga/amiga.h"
#endif
#include "../dashboard.h"
#include "../fileio.h"
#include "../game_input.h"
#include "../memmgr.h"
#include "../platform.h"
#include "race.h"
#include "../ghost.h"
#include "race_resources.h"
#include "../replay/replay.h"
#include "../replay/replay_record.h"
#include "../replay/replay_viewer.h"
#include "../shape2d/shape2d.h"
#include "../ui/ui_dialog.h"
#include "../ui/ui_text.h"
#include "../shape3d/shape3d.h"
#include "../crash_state.h"
#include "../track/track_objects.h"
#include "../camera.h"
#include "../video_frame.h"
#include "../car/car_audio.h"
#include "../audio/audio_control.h"
#include "../externs.h"
#include "../keyboard.h"
#include "../timing.h"
#include "../frame/frame_internal.h"

legacy_u32 race_ghost_frame(void)
{
	/* The truck sequence advances simulation frames before the race clock starts. */
	return game_replay_mode == REPLAY_MODE_PAUSED ? 0 : race_current_frame() + elapsed_time1;
}

void race_draw_frame(void)
{
#ifdef RESTUNTS_AMIGA
	amiga_video_begin_frame();
#endif
#ifdef RESTUNTS_SDL3
	race_presentation_begin_frame();
#endif
	if (full_redraw_frames_remaining != 0) {
		replay_controls_drawn[dashboard_buffer_index] = 0;
		if (dashboard_visible != 0) {
			sprite_set_target_clip_bounds(0, RACE_SCREEN_WIDTH, dashbmp_y_copy,
										  height_above_replaybar);
			setup_car_shapes(DASHBOARD_OPERATION_REDRAW_STATIC);
		}

		if (replaybar_enabled != 0) {
			sprite_set_target_clip_bounds(0, RACE_SCREEN_WIDTH, 0, RACE_SCREEN_HEIGHT);
			loop_game(REPLAY_LOOP_DRAW_CONTROLS, state.game_frame, state.game_frame);
		}
	} else {
		if (replaybar_enabled == 0) {
			replay_controls_drawn[dashboard_buffer_index] = 0;
		}
	}

#ifdef RESTUNTS_SDL3
	if (race_presentation_draw_snapshot() == 0)
#endif
	{
		update_frame(frame_buffer_index, &rect_windshield);
	}
	frame_present(&rect_windshield);
	frame_fps_present_roof();
	if (dashboard_visible != 0) {
		sprite_set_target_clip_bounds(0, RACE_SCREEN_WIDTH, dashbmp_y_copy, height_above_replaybar);
		setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
		sprite_set_target_clip_bounds(0, RACE_SCREEN_WIDTH, 0, RACE_SCREEN_HEIGHT);
	}
	if (frame_display_overlay_active() != 0) {
		/* Keep diagnostic text above every layer of custom dashboard artwork. */
		mouse_draw_opaque_check();
		frame_fps_draw_text();
		mouse_draw_transparent_check();
	}

	if (full_redraw_frames_remaining != 0) {
		full_redraw_frames_remaining--;
	}

	if (video_uses_page_flipping != 0) {
		mouse_draw_opaque_check();
		sprite_present_mcga_backbuffer();
		frame_buffer_index ^= 1;
		dashboard_buffer_index = frame_buffer_index;
		mouse_draw_transparent_check();
	}
#ifdef RESTUNTS_AMIGA
	amiga_video_present();
#endif
	frame_fps_record_presented();
#ifdef RESTUNTS_SDL3
	sdl3_video_end_frame();
#endif
}

void race_select_render_target(void)
{
	if (video_uses_page_flipping != 0) {
		sprite_select_mcga_backbuffer();
		dashboard_buffer_index = frame_buffer_index;
	} else {
		sprite_select_render_window();
	}
}
