#include "replay_viewer_private.h"

static legacy_s32 replay_scrub_accumulate(legacy_s32 accumulated, legacy_s16 speed,
										  legacy_u16 delta)
{
	legacy_s16 increment = LEGACY_S16_WRAP_MUL(LEGACY_S16_FROM_BITS(delta), speed);
	return LEGACY_S32_WRAP_ADD_S16(accumulated, increment);
}

static legacy_s16 replay_scrub_speed(legacy_s32 accumulated)
{
	legacy_s32 quotient = LEGACY_S32_DIV_OR_ZERO(accumulated, REPLAY_SCRUB_ACCELERATION_DIVISOR);
	return LEGACY_S16_WRAP_ADD(LEGACY_S16_FROM_BITS((legacy_u16)quotient),
							   REPLAY_SCRUB_INITIAL_SPEED);
}

static legacy_u16 replay_scrub_amount(legacy_s32 accumulated)
{
	return (legacy_u16)LEGACY_S32_DIV_OR_ZERO(accumulated, REPLAY_SCRUB_FIXED_SCALE);
}

legacy_s32 replay_scrub_begin(legacy_u8 selection)
{
	is_in_replay = 1;
	audio_carstate();
	replay_controls_select(selection);
	(void)timer_get_delta_alt();
	return REPLAY_SCRUB_FIXED_SCALE;
}

static legacy_s32 replay_scrub_advance(legacy_s32 accumulated, legacy_u16 *delta)
{
	legacy_s16 speed = replay_scrub_speed(accumulated);
	if (speed > REPLAY_SCRUB_MAX_SPEED) {
		speed = REPLAY_SCRUB_MAX_SPEED;
	}
	*delta = (legacy_u16)timer_get_delta_alt();
	return replay_scrub_accumulate(accumulated, speed, *delta);
}

void replay_fast_forward(void)
{
#ifdef RESTUNTS_SDL3
	if (extended_replay_ui_scrub(1)) {
		return;
	}
#endif
	legacy_s32 accumulated = replay_scrub_begin(REPLAY_CONTROL_FAST_FORWARD);
	legacy_u16 amount;
	legacy_u16 delta;
	legacy_u16 remaining;
#ifdef RESTUNTS_SDL3
	legacy_u8 menu_requested = 0;
#endif
	while (((legacy_u8)input_combined_flags & INPUT_ACTION_BUTTON_MASK) != 0) {
#ifdef RESTUNTS_SDL3
		if (sdl3_controller_take_menu_request() != 0) {
			menu_requested = 1;
			break;
		}
#endif
		accumulated = replay_scrub_advance(accumulated, &delta);
		remaining = LEGACY_U16_WRAP_SUB(gameconfig.game_recordedframes, elapsed_time2);
		amount = replay_scrub_amount(accumulated);
		if (amount > remaining) {
			accumulated = LEGACY_S32_WRAP_MUL((legacy_s32)remaining, REPLAY_SCRUB_FIXED_SCALE);
		}
		amount = replay_scrub_amount(accumulated);
		replay_controls_draw(state.game_frame, LEGACY_U16_WRAP_ADD(elapsed_time2, amount));
		input_do_checking(LEGACY_S16_FROM_BITS(delta));
	}

	remaining = LEGACY_U16_WRAP_SUB(gameconfig.game_recordedframes, elapsed_time2);
	amount = replay_scrub_amount(accumulated);
	if (amount > remaining) {
		accumulated = LEGACY_S32_WRAP_MUL((legacy_s32)remaining, REPLAY_SCRUB_FIXED_SCALE);
		amount = remaining;
	}
	legacy_u16 target = LEGACY_U16_WRAP_ADD(elapsed_time2, amount);
	if (LEGACY_S16_FROM_BITS(target) > LEGACY_S16_FROM_BITS(gameconfig.game_recordedframes)) {
		target = gameconfig.game_recordedframes;
	}
	frame_supersight_reset();
	restore_gamestate(target);
	elapsed_time2 = target;
	replay_controls_select(REPLAY_CONTROL_PAUSE);
	replay_draw_waiting();
	while ((legacy_u16)state.game_frame != elapsed_time2) {
		update_gamestate();
		replay_controls_draw(state.game_frame, elapsed_time2);
	}
	input_do_checking(REPLAY_INPUT_SETTLE_DELTA);
#ifdef RESTUNTS_SDL3
	if (menu_requested != 0) {
		replay_pause_menu();
	}
#endif
}

void replay_rewind(void)
{
#ifdef RESTUNTS_SDL3
	if (extended_replay_ui_scrub(-1)) {
		return;
	}
#endif
	legacy_s32 accumulated = replay_scrub_begin(REPLAY_CONTROL_REWIND);
	legacy_u16 amount;
	legacy_u16 delta;
#ifdef RESTUNTS_SDL3
	legacy_u8 menu_requested = 0;
#endif
	while (((legacy_u8)input_combined_flags & INPUT_ACTION_BUTTON_MASK) != 0) {
#ifdef RESTUNTS_SDL3
		if (sdl3_controller_take_menu_request() != 0) {
			menu_requested = 1;
			break;
		}
#endif
		accumulated = replay_scrub_advance(accumulated, &delta);
		amount = replay_scrub_amount(accumulated);
		if (amount > elapsed_time2) {
			accumulated = LEGACY_S32_WRAP_MUL((legacy_s32)elapsed_time2, REPLAY_SCRUB_FIXED_SCALE);
		}
		amount = replay_scrub_amount(accumulated);
		replay_controls_draw(state.game_frame, LEGACY_U16_WRAP_SUB(elapsed_time2, amount));
		input_do_checking(LEGACY_S16_FROM_BITS(delta));
	}

	amount = replay_scrub_amount(accumulated);
	if (amount > elapsed_time2) {
		amount = elapsed_time2;
	}
	replay_controls_select(REPLAY_CONTROL_PAUSE);
	if (amount != 0) {
		replay_draw_waiting();
		legacy_u16 target = LEGACY_U16_WRAP_SUB(elapsed_time2, amount);
		frame_supersight_reset();
		restore_gamestate(target);
		elapsed_time2 = target;
		legacy_s16 frames_to_catch_up =
			LEGACY_S16_WRAP_SUB(LEGACY_S16_FROM_BITS(target), state.game_frame);
		legacy_s16 frames_remaining = frames_to_catch_up;
		while ((legacy_u16)state.game_frame != elapsed_time2) {
			update_gamestate();
			frames_remaining = LEGACY_S16_WRAP_SUB(frames_remaining, REPLAY_SINGLE_FRAME_DELTA);
			legacy_u16 displayed_frame = LEGACY_U16_WRAP_ADD(
				elapsed_time2, replay_rewind_interpolate(amount, (legacy_u16)frames_remaining,
														 (legacy_u16)frames_to_catch_up));
			replay_controls_draw(displayed_frame, elapsed_time2);
			input_do_checking(REPLAY_SINGLE_FRAME_DELTA);
		}
	}
	replay_controls_draw(replay_current_frame(), replay_current_frame());
	input_do_checking(REPLAY_INPUT_SETTLE_DELTA);
#ifdef RESTUNTS_SDL3
	if (menu_requested != 0) {
		replay_pause_menu();
	}
#endif
}
