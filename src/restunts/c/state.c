#include "state_internal.h"
#include "game_input.h"
#include "crash_state.h"
#include "race_route.h"
#include "car_speed.h"
#include "externs.h"
#include "residue.h"

void update_player_tick(legacy_s8 input_flags)
{
	update_player_tick_with_legacy_si(input_flags, LEGACY_DEFAULT_PLAYER_TICK_SI);
}

void update_player_tick_with_legacy_si(legacy_s8 input_flags, legacy_s16 caller_si)
{
	update_penalty_display();

	state.playerstate.car_sound_flags = CAR_SOUND_ENGINE_ACTIVE_FLAG;
	if (state.playerstate.car_crashBmpFlag != CRASH_EVENT_NONE) {
		state.game_player_route_indicator = ROUTE_INDICATOR_NONE;
		input_flags = INPUT_BRAKE_FLAG;

		if (state.playerstate.car_actual_speed == CAR_SPEED_STOPPED) {
			state.playerstate.car_sound_flags = CAR_SOUND_NONE;

			if (state.playerstate.car_rev_speed == CAR_SPEED_STOPPED &&
				state.playerstate.car_wheel_vertical_speed[0] == 0 &&
				state.playerstate.car_wheel_vertical_speed[1] == 0 &&
				state.playerstate.car_wheel_vertical_speed[2] == 0 &&
				state.playerstate.car_wheel_vertical_speed[3] == 0) {
				return;
			}
		}
	}

	update_car_speed(input_flags, PLAYER_CAR_INDEX, &state.playerstate, &simd_player);
	legacy_execution_residue.grip_stack_words[LEGACY_RESIDUE_FIRST_WORD] =
		state.playerstate.car_lastrpm;
	legacy_execution_residue.grip_stack_words[LEGACY_RESIDUE_SECOND_WORD] =
		(legacy_s16)state.playerstate.car_rev_speed;
	legacy_execution_residue.grip_stack_words[LEGACY_RESIDUE_THIRD_WORD] =
		(legacy_s16)state.playerstate.car_gearratio;
	update_player_steering_input(LEGACY_S16_SAR((legacy_s16)input_flags, INPUT_STEERING_SHIFT) &
								 INPUT_PEDAL_MASK);
	legacy_u16 rev_speed_before_grip = state.playerstate.car_rev_speed;
	legacy_u16 actual_speed_before_grip = state.playerstate.car_actual_speed;
	update_grip(&state.playerstate, &simd_player, GRIP_BEHAVIOR_PLAYER);
	update_legacy_grip_stack_words(&state.playerstate, &simd_player, rev_speed_before_grip,
								   actual_speed_before_grip, caller_si);
	update_player_state(&state.playerstate, &simd_player, &state.opponentstate, &simd_opponent,
						PLAYER_CAR_INDEX);
	state.game_travDist = LEGACY_S32_WRAP_ADD(
		state.game_travDist, (legacy_s32)(legacy_u16)state.playerstate.car_actual_speed);
	legacy_s8 previous_route_status = state.game_player_route_status;
	update_player_route_penalty();
	update_player_route_guidance(previous_route_status);
}
