#include "race_presentation_internal.h"
#ifdef RESTUNTS_SDL3
#include "../race/race_internal.h"
#include "../../platform/sdl3/sdl3.h"
#include "frame_interpolation.h"
#include "frame_adaptive.h"
#include "hires.h"
#include "settings.h"
#include "../externs.h"
#include "../camera.h"
#include "../ghost.h"
#include "../replay/replay.h"
#include "../replay/replay_viewer.h"
#include "../race/race_resources.h"
#include "../shape2d/shape2d.h"
#include "../frame_internal.h"
#include "../game_input.h"
#include "../timing.h"
#endif
#ifdef RESTUNTS_SDL3
static struct RACE_PRESENTATION race_presentation;

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
#ifdef RESTUNTS_SDL3
void race_draw_visual(void)
{
	legacy_u32 fraction = race_presentation_fraction(presentation_now());
	frame_interpolate_state(&race_presentation.interpolated, &state, &race_presentation.previous,
							fraction);
	race_presentation_interpolate_ghost(fraction);

	race_select_render_target();
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

#ifdef RESTUNTS_SDL3
void race_presentation_initialize(void)
{
	race_presentation = (struct RACE_PRESENTATION){0};
	presentation_reset(&race_presentation.clock, presentation_now());
}

void race_presentation_begin_frame(void)
{
	legacy_s32 render_scale =
		supersight_enabled != 0 ? frame_adaptive_render_scale(&frame_adaptive) : HIRES_SCALE;
	if (hires_render_scale() != render_scale) {
		hires_set_render_scale(render_scale);
		/* Companion pixels were invalidated; rebuild static dashboard/replay art. */
		full_redraw_frames_remaining = video_page_count;
	}
	sdl3_video_begin_track_frame(supersight_enabled);
}

legacy_s16 race_presentation_draw_snapshot(void)
{
	if (race_presentation.interpolating != 0) {
		update_frame_snapshot(
			frame_buffer_index, &rect_windshield, &race_presentation.interpolated,
			race_presentation.ghost_valid != 0 ? &race_presentation.ghost_interpolated : 0,
			race_presentation.ghost_valid != 0 ? &race_presentation.ghost_camera_interpolated : 0);
		return 1;
	}
	return 0;
}

legacy_s16 race_presentation_poll(legacy_s64 *last_processed_frame, legacy_s16 *wait_for_sample,
								  legacy_u16 *ready)
{
	if (supersight_enabled != 0 && (game_replay_mode == REPLAY_MODE_PAUSED ||
									state.game_inputmode == GAME_INPUT_MODE_WAITING)) {
		legacy_u64 now = presentation_now();
		if (now < race_presentation.control_time) {
			sdl3_platform_delay(RACE_CONTROL_POLL_DELAY_MS);
			sdl3_platform_pump();
			*ready = 0;
			return 1;
		}
		legacy_u16 rate = framespersec > 0 ? (legacy_u16)framespersec : GAME_FRAME_RATE_NORMAL;
		race_presentation.control_time = now + PRESENTATION_SECOND_NS / rate;
		*last_processed_frame = race_current_frame();
		*ready = 1;
		return 1;
	}
	if (supersight_enabled != 0 && game_replay_mode == REPLAY_MODE_PLAYBACK && is_in_replay == 0) {
		*wait_for_sample = 1;
	}
	return 0;
}

legacy_s16 race_presentation_draw(legacy_s16 rewind_active)
{
	race_presentation_capture();
	if (supersight_enabled == 0) {
		return 0;
	}
	if (rewind_active != 0 || (game_replay_mode == REPLAY_MODE_PLAYBACK && is_in_replay != 0) ||
		presentation_due(&race_presentation.clock, presentation_now()) != 0) {
		race_draw_visual();
		race_presentation_frame_complete();
	}
	return 1;
}

void race_presentation_frame_complete(void)
{
	race_presentation.started = 1;
}

void race_presentation_finish(void)
{
	/* Auto restores menu resolution; a manually locked scale stays selected. */
	hires_set_render_scale(frame_adaptive.preset == FRAME_ADAPTIVE_PRESET_AUTO
							   ? HIRES_SCALE
							   : frame_adaptive_render_scale(&frame_adaptive));
}
#endif
