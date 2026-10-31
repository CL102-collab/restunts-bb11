#include "intro_presentation.h"
#ifdef RESTUNTS_SDL3
#include "../intro_render_internal.h"
#include "../game_input.h"
#include "../externs.h"
#include "../platform.h"

#define INTRO_TIMER_TICK_NS (PRESENTATION_SECOND_NS / DOS_TIMER_REALTIME_TICKS_PER_SECOND)

static legacy_s16 intro_predict_coordinate(legacy_s16 previous, legacy_s16 current,
										   legacy_u64 elapsed, legacy_u64 interval, legacy_u8 angle)
{
	legacy_s16 change = LEGACY_S16_WRAP_SUB(current, previous);
	if (angle != 0) {
		change =
			(legacy_s16)(((legacy_u16)change + ANGLE_HALF_TURN) & ANGLE_MASK) - ANGLE_HALF_TURN;
	}
	legacy_s32 offset =
		(legacy_s32)((legacy_s64)change * (legacy_s64)elapsed / (legacy_s64)interval);
	return LEGACY_S16_WRAP_ADD(current, offset);
}

static void intro_predict_view(const struct INTRO_SESSION *intro, legacy_u64 now,
							   struct INTRO_VIEW *view)
{
	*view = intro->presentation.current_view;
	if (intro->presentation.view_interval == 0) {
		return;
	}
	legacy_u64 elapsed = now - intro->presentation.view_time;
	legacy_u64 maximum = (legacy_u64)(legacy_u16)timer_ticks_per_frame * INTRO_TIMER_TICK_NS;
	if (elapsed > maximum) {
		elapsed = maximum;
	}
	const struct INTRO_VIEW *previous = &intro->presentation.previous_view;
	view->camera.x = intro_predict_coordinate(previous->camera.x, view->camera.x, elapsed,
											  intro->presentation.view_interval, 0);
	view->camera.y = intro_predict_coordinate(previous->camera.y, view->camera.y, elapsed,
											  intro->presentation.view_interval, 0);
	view->camera.z = intro_predict_coordinate(previous->camera.z, view->camera.z, elapsed,
											  intro->presentation.view_interval, 0);
	view->car_position.x = intro_predict_coordinate(previous->car_position.x, view->car_position.x,
													elapsed, intro->presentation.view_interval, 0);
	view->car_position.y = intro_predict_coordinate(previous->car_position.y, view->car_position.y,
													elapsed, intro->presentation.view_interval, 0);
	view->car_position.z = intro_predict_coordinate(previous->car_position.z, view->car_position.z,
													elapsed, intro->presentation.view_interval, 0);
	view->car_rotation = intro_predict_coordinate(previous->car_rotation, view->car_rotation,
												  elapsed, intro->presentation.view_interval, 1);
	view->horizontal_angle =
		intro_predict_coordinate(previous->horizontal_angle, view->horizontal_angle, elapsed,
								 intro->presentation.view_interval, 1);
	view->vertical_angle = intro_predict_coordinate(previous->vertical_angle, view->vertical_angle,
													elapsed, intro->presentation.view_interval, 1);
}

static void intro_update_view(struct INTRO_SESSION *intro, legacy_u64 now)
{
	struct INTRO_VIEW current;
	intro_capture_view(intro, &current);
	/* A scene cut has no continuous motion to predict. The real animation and
	 * car state advance only in intro_advance_session at their original rate. */
	intro->presentation.view_interval = 0;
	if (intro->presentation.view_valid != 0 &&
		current.draw_car == intro->presentation.current_view.draw_car &&
		current.primary_logo == intro->presentation.current_view.primary_logo &&
		intro->frame_count > intro->presentation.view_frame) {
		intro->presentation.previous_view = intro->presentation.current_view;
		intro->presentation.view_interval =
			(legacy_u64)(intro->frame_count - intro->presentation.view_frame) *
			(legacy_u16)timer_ticks_per_frame * INTRO_TIMER_TICK_NS;
	}
	intro->presentation.current_view = current;
	intro->presentation.view_valid = 1;
	intro->presentation.view_frame = intro->frame_count;
	intro->presentation.view_time = now;
	intro->needs_render = 0;
}

void hires_intro_reset(struct INTRO_SESSION *intro)
{
	intro->presentation.view_valid = 0;
	presentation_reset(&intro->presentation.presentation_clock, presentation_now());
}

void hires_intro_begin(struct INTRO_SESSION *intro)
{
	intro->presentation.input_time = presentation_now();
	intro->presentation.input_delta = 0;
}

void hires_intro_present(struct INTRO_SESSION *intro)
{
	legacy_u64 now = presentation_now();
	legacy_s16 keyframe = intro->needs_render;
	if (keyframe != 0) {
		intro_update_view(intro, now);
	}
	if (supersight_enabled != 0) {
		if (presentation_due(&intro->presentation.presentation_clock, now)) {
			struct INTRO_VIEW view;
			intro_predict_view(intro, now, &view);
			intro_render_session(intro, &view);
		}
	} else if (keyframe != 0) {
		intro_render_session(intro, &intro->presentation.current_view);
	}
}

legacy_s16 hires_intro_input(struct INTRO_SESSION *intro, legacy_s16 delta)
{
	legacy_s16 key = 0;
	legacy_u64 input_now = presentation_now();
	if (supersight_enabled != 0) {
		intro->presentation.input_delta =
			LEGACY_S16_WRAP_ADD(intro->presentation.input_delta, delta);
		/* Presentation-only loops cannot sample devices between timer ticks. */
		if (input_now - intro->presentation.input_time >= INTRO_TIMER_TICK_NS) {
			key = input_do_checking(intro->presentation.input_delta);
			intro->presentation.input_delta = 0;
			intro->presentation.input_time = input_now;
		}
	} else {
		key = input_do_checking(delta);
		intro->presentation.input_delta = 0;
		intro->presentation.input_time = input_now;
	}
	return key;
}
#endif
