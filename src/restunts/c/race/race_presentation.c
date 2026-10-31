#include "race_internal.h"
#include "../extended/extended_replay_ui.h"
#ifdef RESTUNTS_AMIGA_TRACE
#include <stdio.h>
#endif
#ifdef RESTUNTS_SDL3
#include "../../platform/sdl3/sdl3.h"
#include "../../platform/sdl3/touch_game.h"
#include "../../platform/sdl3/controller_game.h"
#include "../hires/frame_interpolation.h"
#include "../hires/frame_adaptive.h"
#include "../hires/hires.h"
#include "../presentation.h"
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
#include "../frame_internal.h"

legacy_u32 race_ghost_frame(void)
{
	/* The truck sequence advances simulation frames before the race clock starts. */
	return game_replay_mode == REPLAY_MODE_PAUSED ? 0 : race_current_frame() + elapsed_time1;
}

#ifdef RESTUNTS_SDL3
struct RACE_PRESENTATION race_presentation;

/* Replay speed changes the interval between captured keyframes, not the visual rate. */
static legacy_u64 race_presentation_interval(void)
{
	legacy_u16 rate = framespersec > 0 ? (legacy_u16)framespersec : GAME_FRAME_RATE_NORMAL;
	legacy_u64 interval = PRESENTATION_SECOND_NS / rate;
	if (game_replay_mode == REPLAY_MODE_PLAYBACK && replay_playback_speed == REPLAY_PLAYBACK_SLOW) {
		interval *= RACE_PRESENTATION_SLOW_INTERVAL_SCALE;
	}
	return interval;
}

static legacy_u32 race_presentation_fraction(legacy_u64 now)
{
	if (race_presentation.history_valid == 0 ||
		race_presentation.sample_frame != state.game_frame) {
		return FRAME_INTERPOLATION_ONE;
	}
	legacy_u64 age = now > race_presentation.sample_time ? now - race_presentation.sample_time : 0;
	legacy_u64 interval = race_presentation_interval();
	if (age >= interval) {
		return FRAME_INTERPOLATION_ONE;
	}
	/* Each pair starts one visual slot into its completed physics interval:
	 * thirds at 20 Hz, sixths at 10 Hz. Keep the 60 Hz period rational so
	 * fractional slots share presentation_due's exact deadlines and the final
	 * slot reaches the keyframe. Late draws skip slots and hold the endpoint. */
	legacy_u64 elapsed =
		(age * PRESENTATION_RATE / PRESENTATION_SECOND_NS + 1U) * PRESENTATION_SECOND_NS;
	interval *= PRESENTATION_RATE;
	return elapsed < interval ? (legacy_u32)(elapsed * FRAME_INTERPOLATION_ONE / interval)
							  : FRAME_INTERPOLATION_ONE;
}

void race_presentation_sync(legacy_s16 rewinding)
{
	legacy_s16 view[RACE_PRESENTATION_VIEW_COUNT] = {supersight_enabled,
													 cameramode,
													 followOpponentFlag,
													 game_replay_mode,
													 is_in_replay,
													 replay_playback_speed,
													 framespersec,
													 rewinding,
													 elapsed_time1,
													 custom_camera.distance,
													 custom_camera.elevation_angle,
													 custom_camera.azimuth_angle,
													 camera_track_height_offset};
	legacy_u8 changed = 0;
	for (legacy_u16 index = 0; index < sizeof(view) / sizeof(view[0]); index++) {
		if (race_presentation.view[index] != view[index]) {
			changed = 1;
			race_presentation.view[index] = view[index];
		}
	}
	if (changed != 0) {
		race_presentation.history_valid = 0;
		race_presentation.current = state;
		race_presentation.sample_frame = state.game_frame;
		race_presentation.sample_time = presentation_now();
		race_presentation.control_time = 0;
		race_presentation.ghost_valid = 0;
		race_presentation.started = 0;
		presentation_reset(&race_presentation.clock, presentation_now());
	}
}

void race_presentation_capture(void)
{
	if (race_presentation.sample_frame == state.game_frame) {
		return;
	}
	legacy_s16 span = LEGACY_S16_WRAP_SUB(state.game_frame, race_presentation.sample_frame);
	race_presentation.history_valid = span == 1 || (span == RACE_PRESENTATION_FAST_FRAME_SPAN &&
													game_replay_mode == REPLAY_MODE_PLAYBACK &&
													replay_playback_speed == REPLAY_PLAYBACK_FAST);
	race_presentation.sample_span = race_presentation.history_valid != 0 ? (legacy_u16)span : 1;
	race_presentation.previous = race_presentation.current;
	race_presentation.current = state;
	race_presentation.sample_frame = state.game_frame;
	race_presentation.sample_time = presentation_now();
	presentation_reset(&race_presentation.clock, race_presentation.sample_time);
}

static void race_presentation_interpolate_ghost(legacy_u32 fraction)
{
	const struct CARSTATE *car = ghost_car_state();
	const struct GHOST_CAMERA_STATE *camera = ghost_camera_state();
	race_presentation.ghost_valid = car != 0 && camera != 0;
	if (race_presentation.ghost_valid == 0) {
		return;
	}
	race_presentation.ghost_interpolated = *car;
	race_presentation.ghost_camera_interpolated = *camera;
	if (race_presentation.history_valid == 0) {
		return;
	}
	legacy_u32 lag = race_presentation.sample_span * (FRAME_INTERPOLATION_ONE - fraction);
	/* Recorded ghosts can have a different tick rate. Sample their cached
	 * replay at this same visual instant instead of blending repeated poses. */
	(void)ghost_sample_render_pose(race_ghost_frame(), framespersec, lag,
								   &race_presentation.ghost_interpolated,
								   &race_presentation.ghost_camera_interpolated);
}

#endif

void race_draw_frame(void)
{
#ifdef RESTUNTS_AMIGA
	amiga_video_begin_frame();
#endif
#ifdef RESTUNTS_SDL3
	legacy_s32 render_scale =
		supersight_enabled != 0 ? frame_adaptive_render_scale(&frame_adaptive) : HIRES_SCALE;
	if (hires_render_scale() != render_scale) {
		hires_set_render_scale(render_scale);
		/* Companion pixels were invalidated; rebuild static dashboard/replay art. */
		full_redraw_frames_remaining = video_page_count;
	}
	sdl3_video_begin_track_frame(supersight_enabled);
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
	if (race_presentation.interpolating != 0) {
		update_frame_snapshot(
			frame_buffer_index, &rect_windshield, &race_presentation.interpolated,
			race_presentation.ghost_valid != 0 ? &race_presentation.ghost_interpolated : 0,
			race_presentation.ghost_valid != 0 ? &race_presentation.ghost_camera_interpolated : 0);
	} else
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

#ifdef RESTUNTS_SDL3
void race_draw_visual(void)
{
	legacy_u32 fraction = race_presentation_fraction(presentation_now());
	frame_interpolate_state(&race_presentation.interpolated, &state, &race_presentation.previous,
							fraction);
	race_presentation_interpolate_ghost(fraction);

	if (video_uses_page_flipping != 0) {
		sprite_select_mcga_backbuffer();
		dashboard_buffer_index = frame_buffer_index;
	} else {
		sprite_select_render_window();
	}
	race_presentation.interpolating = 1;
	race_draw_frame();
	race_presentation.interpolating = 0;
}

void race_present_interpolation(void)
{
	/* Extra presentations blend captured states without stepping physics.
	 * Replay, controls and timer callbacks retain their authoritative cadence. */
	if (supersight_enabled == 0 || race_presentation.started == 0 ||
		race_current_frame() != race_input_frame() ||
		state.game_inputmode == GAME_INPUT_MODE_WAITING ||
		(game_replay_mode == REPLAY_MODE_PLAYBACK && is_in_replay != 0) || race_exit_request != 0) {
		return;
	}
	if (presentation_due(&race_presentation.clock, presentation_now()) != 0) {
		race_draw_visual();
	}
}
#endif
