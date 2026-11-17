#include "unit_pilot_controls.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include "../game_input.h"
#include "../externs.h"
#include "../state_internal.h"
#include "../extended/csrpl.h"

static legacy_u8 preview_mask, preview_value;

legacy_u8 unit_controls_manual_shift(const struct CARSTATE *car, const struct SIMD *simd)
{
	if (car->car_transmission != TRANSMISSION_MANUAL || car->car_changing_gear) {
		return INPUT_NONE;
	}
	if (car->car_current_gear == 0 ||
		(car->car_current_gear < simd->num_gears && car->car_currpm >= simd->upshift_rpm)) {
		return INPUT_SHIFT_UP_FLAG;
	}
	if (car->car_current_gear > 1 && car->car_currpm < simd->downshift_rpm) {
		return INPUT_SHIFT_DOWN_FLAG;
	}
	return INPUT_NONE;
}

void unit_opponent_pilot_input_override(legacy_u8 mask, legacy_u8 value)
{
	preview_mask = 0;
	if ((mask & INPUT_STEERING_MASK) == INPUT_STEERING_MASK) {
		preview_mask |= INPUT_STEERING_MASK;
	}
	if ((mask & INPUT_PEDAL_MASK) == INPUT_PEDAL_MASK) {
		preview_mask |= INPUT_PEDAL_MASK;
	}
	preview_value = value;
}

void unit_pilot_controls_apply(const struct CARSTATE *car, legacy_u8 steering_rate,
							   struct CSRPL_OPPONENT_COMMAND *command)
{
	legacy_u8 mask = preview_mask, value = preview_value;
	preview_mask = preview_value = 0;
	if (!car || !command) {
		return;
	}
	if (mask & INPUT_STEERING_MASK) {
		legacy_u8 direction = value & INPUT_STEERING_MASK;
		if (direction == INPUT_STEERING_MASK) {
			direction = INPUT_NONE;
		}
		command->steering = player_steering_next_angle(
			car, (legacy_s8)(direction >> INPUT_STEERING_SHIFT), steering_rate);
	}
	if (mask & INPUT_PEDAL_MASK) {
		legacy_u8 pedal = value & INPUT_PEDAL_MASK;
		if (pedal & INPUT_BRAKE_FLAG) {
			pedal = INPUT_BRAKE_FLAG;
		}
		command->input = (legacy_u8)((command->input & ~INPUT_PEDAL_MASK) | pedal);
	}
}
#endif
