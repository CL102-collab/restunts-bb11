#include "externs.h"
#include "game_input.h"
#include "math.h"
#include "car_speed.h"
#include "physics_internal.h"

#define ACCELERATION_MASS_NUMERATOR 25L
#define ACCELERATION_DRAG_SCALE 200L
#define NORMAL_GEAR_KNOB_STEP 6
#define LOW_RATE_GEAR_KNOB_STEP 12
#define GEAR_RATIO_BYTE_SHIFT 8U
#define AERODYNAMIC_RESISTANCE_SPEED_SHIFT 10U
#define LOW_RATE_GEAR_CHANGE_RPM_DROP 80
#define NORMAL_GEAR_CHANGE_RPM_DROP 40
#define AIRBORNE_MAX_SPEED 64000U
#define AIRBORNE_ACCELERATION 768
#define IDLE_TORQUE_RPM_THRESHOLD 2600
#define TORQUE_CURVE_RPM_SHIFT 7U
#define ENGINE_LIMITER_BLEND_RPM 5000
#define TORQUE_ACCELERATION_SHIFT 4U
#define OPPONENT_SPEED_SCALE 200U
#define OPPONENT_DRAG_SHIFT 1U
#define ENGINE_LIMITER_ACCELERATION_THRESHOLD 296
#define ENGINE_LIMITER_SHORT_TICKS 5
#define OPPONENT_BRAKING_MULTIPLIER 2
#define WRAPPED_REVERSE_SPEED_LIMIT 62720U
#define WHEEL_SPEED_SYNC_THRESHOLD 5120
#define RAPID_RPM_CHANGE_THRESHOLD 2000
#define ENGINE_TORQUE_LIMIT_THRESHOLD 12000
#define ENGINE_LIMITER_LONG_TICKS 30
#define ENGINE_LIMITER_RECOVERY_TICKS 10
#define ENGINE_SPEED_CORRECTION 1280U
#define ACCELERATION_MASS_RESULT_SHIFT 1U
#define ENGINE_LIMITER_TORQUE_BLEND_SHIFT 1U
#define WHEEL_SPEED_AVERAGE_SHIFT 1U
#define MAX_RPM_MARGIN 1
#define ENGINE_LIMITER_INACTIVE 0
#define ENGINE_LIMITER_TICK_STEP 1
#define CAR_GEAR_NEUTRAL 0
#define CAR_GEAR_FIRST 1
#define CAR_GEAR_INDEX_STEP 1

enum CAR_GEAR_SHIFT_DIRECTION {
	CAR_GEAR_SHIFT_DOWN = -1,
	CAR_GEAR_SHIFT_NONE = 0,
	CAR_GEAR_SHIFT_UP = 1
};

#define GEAR_CHANGE_DELAY_HALF_SHIFT 1U
#define GEAR_CHANGE_DELAY_TICK_STEP 1
#define GEAR_KNOB_ALIGNED 0
#define OPPONENT_DRAG_NONE 0
#define GEAR_KNOB_DIRECTION_NEUTRAL 0
#define CAR_SPEED_DELTA_STATIONARY 0

static legacy_s16 powergear_bug_enabled = 1;

legacy_u16 powergear_bug_is_enabled(void)
{
	return powergear_bug_enabled != 0;
}

void configure_powergear_bug(legacy_s16 argc, legacy_s8 *argv[])
{
	static const legacy_s8 off_option[] = "--pg:off";
	static const legacy_s8 on_option[] = "--pg:on";
	powergear_bug_enabled = 1;
	for (legacy_s16 index = 1; index < argc; index++) {
		if (stricmp(argv[index], off_option) == 0) {
			powergear_bug_enabled = 0;
		} else if (stricmp(argv[index], on_option) == 0) {
			powergear_bug_enabled = 1;
		}
	}
}

static legacy_s16 scale_acceleration_by_mass(legacy_s16 acceleration, legacy_s16 mass)
{
	legacy_s32 product = LEGACY_S32_WRAP_MUL((legacy_s32)acceleration, ACCELERATION_MASS_NUMERATOR);
	legacy_s32 quotient;
	if (powergear_bug_enabled != 0) {
		/* Preserve the original unsigned divide for replay compatibility. A negative
		 * force becomes a large positive dividend, causing power gear or anti-PG
		 * after the quotient is truncated to its signed low word. */
		quotient =
			LEGACY_S32_FROM_BITS(LEGACY_U32_DIV_OR_ZERO((legacy_u32)product, (legacy_u16)mass));
	} else {
		quotient = LEGACY_S32_DIV_OR_ZERO(product, (legacy_s32)mass);
	}
	legacy_s16 low_word = LEGACY_S16_FROM_BITS((legacy_u16)quotient);
	return LEGACY_S16_SAR(low_word, ACCELERATION_MASS_RESULT_SHIFT);
}

static legacy_s16 apply_opponent_acceleration_drag(legacy_s16 acceleration, legacy_u8 drag)
{
	legacy_s32 product =
		LEGACY_S32_WRAP_MUL((legacy_s32)(legacy_u16)drag, (legacy_s32)acceleration);
	legacy_s32 reduction = LEGACY_S32_DIV_OR_ZERO(product, ACCELERATION_DRAG_SCALE);
	return LEGACY_S16_WRAP_SUB(acceleration, LEGACY_S16_FROM_BITS((legacy_u16)reduction));
}

static legacy_s8 gear_change_delay(legacy_u16 frame_rate)
{
	legacy_s16 signed_rate = LEGACY_S8_FROM_BITS((legacy_u8)frame_rate);
	legacy_s16 half_rate = LEGACY_S16_SAR(signed_rate, GEAR_CHANGE_DELAY_HALF_SHIFT);
	return LEGACY_S8_FROM_BITS(
		(legacy_u8)LEGACY_U16_WRAP_ADD((legacy_u8)half_rate, (legacy_u8)frame_rate));
}

static legacy_s16 move_gear_knob_toward(legacy_s16 current, legacy_s16 target, legacy_s16 step)
{
	legacy_s16 difference = LEGACY_S16_WRAP_SUB(target, current);
	if (absolute_word(difference) <= step) {
		return target;
	}
	if (difference > GEAR_KNOB_DIRECTION_NEUTRAL) {
		return LEGACY_S16_WRAP_ADD(current, step);
	}
	return LEGACY_S16_WRAP_SUB(current, step);
}

legacy_u16 update_rpm_from_speed(legacy_u16 currpm, legacy_u16 speed, legacy_u16 gearratio,
								 legacy_s16 changing_gear, legacy_u16 idle_rpm)
{
	if (changing_gear == CAR_GEAR_CHANGE_INACTIVE) {
		currpm = (legacy_u16)(LEGACY_U32_WRAP_MUL(speed, gearratio) >> LEGACY_WORD_BITS);
	}

	if (currpm >= idle_rpm) {
		return currpm;
	}
	return idle_rpm;
}

static legacy_s16 gear_shift_direction(legacy_s8 input_flags, struct CARSTATE *carstate,
									   const struct SIMD *simd)
{
	legacy_s16 shift_direction = CAR_GEAR_SHIFT_NONE;
	if (carstate->car_transmission == TRANSMISSION_MANUAL &&
		carstate->car_changing_gear == CAR_GEAR_CHANGE_INACTIVE) {
		if ((input_flags & INPUT_SHIFT_UP_FLAG) != INPUT_NONE) {
			shift_direction = CAR_GEAR_SHIFT_UP;
		} else if ((input_flags & INPUT_SHIFT_DOWN_FLAG) != INPUT_NONE) {
			shift_direction = CAR_GEAR_SHIFT_DOWN;
		}
	} else if (carstate->car_current_gear != CAR_GEAR_NEUTRAL &&
			   carstate->car_changing_gear == CAR_GEAR_CHANGE_INACTIVE &&
			   carstate->car_sumSurfRearWheels != CAR_WHEEL_CONTACT_NONE) {
		if ((legacy_u16)carstate->car_currpm > (legacy_u16)simd->upshift_rpm) {
			shift_direction = CAR_GEAR_SHIFT_UP;
		} else if ((legacy_u16)carstate->car_currpm < (legacy_u16)simd->downshift_rpm) {
			shift_direction = CAR_GEAR_SHIFT_DOWN;
		}
	}
	return shift_direction;
}

static void request_gear_change(legacy_s8 input_flags, struct CARSTATE *carstate,
								const struct SIMD *simd)
{
	legacy_s16 shift_direction = gear_shift_direction(input_flags, carstate, simd);
	if (shift_direction == CAR_GEAR_SHIFT_UP && carstate->car_current_gear != simd->num_gears) {
		carstate->car_current_gear =
			LEGACY_S8_WRAP_ADD(carstate->car_current_gear, CAR_GEAR_INDEX_STEP);
	} else if (shift_direction == CAR_GEAR_SHIFT_DOWN &&
			   carstate->car_current_gear > CAR_GEAR_FIRST) {
		carstate->car_current_gear =
			LEGACY_S8_WRAP_SUB(carstate->car_current_gear, CAR_GEAR_INDEX_STEP);
	} else {
		shift_direction = CAR_GEAR_SHIFT_NONE;
	}
	if (shift_direction != CAR_GEAR_SHIFT_NONE) {
		carstate->car_changing_gear = CAR_GEAR_CHANGE_ACTIVE;
		carstate->car_gear_change_delay = gear_change_delay(framespersec);
		carstate->car_knob_x2 = simd->knob_points[carstate->car_current_gear].px;
		carstate->car_knob_y2 = simd->knob_points[carstate->car_current_gear].py;
	}
}

static void update_gear_knob(struct CARSTATE *carstate, const struct SIMD *simd)
{
	legacy_s16 gear_knob_step =
		framespersec == GAME_FRAME_RATE_NORMAL ? NORMAL_GEAR_KNOB_STEP : LOW_RATE_GEAR_KNOB_STEP;
	if (carstate->car_changing_gear != CAR_GEAR_CHANGE_INACTIVE) {
		if (carstate->car_knob_x == carstate->car_knob_x2) {
			legacy_s16 knob_delta =
				LEGACY_S16_WRAP_SUB(carstate->car_knob_y2, carstate->car_knob_y);
			if (knob_delta == GEAR_KNOB_ALIGNED) {
				carstate->car_changing_gear = CAR_GEAR_CHANGE_INACTIVE;
				carstate->car_gearratio = simd->gear_ratios[carstate->car_current_gear];
				carstate->car_gearratioshr8 = carstate->car_gearratio >> GEAR_RATIO_BYTE_SHIFT;
			} else {
				carstate->car_knob_y = move_gear_knob_toward(carstate->car_knob_y,
															 carstate->car_knob_y2, gear_knob_step);
			}
		} else if (simd->knob_points[CAR_GEAR_NEUTRAL].py == carstate->car_knob_y) {
			carstate->car_knob_x =
				move_gear_knob_toward(carstate->car_knob_x, carstate->car_knob_x2, gear_knob_step);
		} else {
			carstate->car_knob_y = move_gear_knob_toward(
				carstate->car_knob_y, simd->knob_points[CAR_GEAR_NEUTRAL].py, gear_knob_step);
		}
	} else if (carstate->car_gear_change_delay != GEAR_CHANGE_DELAY_EXPIRED) {
		carstate->car_gear_change_delay =
			LEGACY_S8_WRAP_SUB(carstate->car_gear_change_delay, GEAR_CHANGE_DELAY_TICK_STEP);
	}
}

static legacy_s16 grounded_acceleration_delta(legacy_s16 car_index, struct CARSTATE *carstate,
											  const struct SIMD *simd, legacy_s16 speed_delta)
{
	legacy_u8 torque_or_drag;

	if (carstate->car_current_gear <= CAR_GEAR_FIRST &&
		carstate->car_currpm < IDLE_TORQUE_RPM_THRESHOLD) {
		torque_or_drag = simd->idle_torque;
	} else {
		torque_or_drag =
			simd->torque_curve[(legacy_u16)carstate->car_currpm >> TORQUE_CURVE_RPM_SHIFT];
	}
	if (carstate->car_engineLimiterTimer != ENGINE_LIMITER_INACTIVE &&
		carstate->car_currpm < ENGINE_LIMITER_BLEND_RPM) {
		torque_or_drag =
			((legacy_u8)simd->idle_torque + torque_or_drag) >> ENGINE_LIMITER_TORQUE_BLEND_SHIFT;
	}
	speed_delta = LEGACY_S16_WRAP_ADD(
		speed_delta,
		LEGACY_S16_FROM_BITS(
			(legacy_u16)(LEGACY_U16_WRAP_MUL(carstate->car_gearratioshr8, torque_or_drag) >>
						 TORQUE_ACCELERATION_SHIFT)));
	speed_delta = scale_acceleration_by_mass(speed_delta, simd->car_mass);
	if (car_index == OPPONENT_CAR_INDEX) {
		torque_or_drag = (legacy_u16)(OPPONENT_SPEED_SCALE - *oppnentSped) >> OPPONENT_DRAG_SHIFT;
		if (torque_or_drag != OPPONENT_DRAG_NONE) {
			speed_delta = apply_opponent_acceleration_drag(speed_delta, torque_or_drag);
		}
	}
	if (speed_delta > ENGINE_LIMITER_ACCELERATION_THRESHOLD) {
		carstate->car_engineLimiterTimer = ENGINE_LIMITER_SHORT_TICKS;
	}
	return speed_delta;
}

static legacy_s16 accelerate_car(legacy_s16 car_index, struct CARSTATE *carstate,
								 const struct SIMD *simd, legacy_s16 speed_delta)
{
	if (carstate->car_changing_gear != CAR_GEAR_CHANGE_INACTIVE) {
		carstate->car_engineLimiterTimer = ENGINE_LIMITER_INACTIVE;
		if (framespersec == GAME_FRAME_RATE_LOW) {
			carstate->car_currpm =
				LEGACY_S16_WRAP_SUB(carstate->car_currpm, LOW_RATE_GEAR_CHANGE_RPM_DROP);
		} else {
			carstate->car_currpm =
				LEGACY_S16_WRAP_SUB(carstate->car_currpm, NORMAL_GEAR_CHANGE_RPM_DROP);
		}
	} else if (carstate->car_sumSurfRearWheels == CAR_WHEEL_CONTACT_NONE) {
		if ((legacy_u16)carstate->car_currpm < (legacy_u16)simd->max_rpm &&
			carstate->car_rev_speed < AIRBORNE_MAX_SPEED) {
			speed_delta = LEGACY_S16_WRAP_ADD(speed_delta, AIRBORNE_ACCELERATION);
		}
	} else {
		speed_delta = grounded_acceleration_delta(car_index, carstate, simd, speed_delta);
	}
	return speed_delta;
}

static legacy_s16 pedal_speed_delta(legacy_s8 input_flags, legacy_s16 car_index,
									struct CARSTATE *carstate, const struct SIMD *simd)
{
	legacy_s16 speed_delta = LEGACY_S16_WRAP_SUB(
		carstate->car_pseudoGravity,
		simd->aerorestable[carstate->car_rev_speed >> AERODYNAMIC_RESISTANCE_SPEED_SHIFT]);
	if ((legacy_u16)carstate->car_currpm > (legacy_u16)simd->max_rpm) {
		carstate->car_currpm = LEGACY_S16_WRAP_SUB(simd->max_rpm, MAX_RPM_MARGIN);
		speed_delta = LEGACY_S16_WRAP_SUB(speed_delta, simd->braking_eff);
	} else if ((input_flags & INPUT_PEDAL_MASK) == INPUT_ACCELERATE_FLAG) {
		carstate->car_is_braking = CAR_PEDAL_RELEASED;
		carstate->car_is_accelerating = CAR_PEDAL_PRESSED;
		speed_delta = accelerate_car(car_index, carstate, simd, speed_delta);
	} else if ((input_flags & INPUT_PEDAL_MASK) == INPUT_BRAKE_FLAG) {
		carstate->car_is_accelerating = CAR_PEDAL_RELEASED;
		carstate->car_engineLimiterTimer = ENGINE_LIMITER_INACTIVE;
		carstate->car_is_braking = CAR_PEDAL_PRESSED;
		if (car_index == PLAYER_CAR_INDEX) {
			speed_delta = LEGACY_S16_WRAP_SUB(speed_delta, simd->braking_eff);
		} else {
			speed_delta = LEGACY_S16_WRAP_SUB(
				speed_delta, LEGACY_S16_WRAP_MUL(simd->braking_eff, OPPONENT_BRAKING_MULTIPLIER));
		}
	} else {
		carstate->car_is_accelerating = CAR_PEDAL_RELEASED;
		carstate->car_is_braking = CAR_PEDAL_RELEASED;
	}
	if (framespersec == GAME_FRAME_RATE_LOW) {
		speed_delta = LEGACY_S16_WRAP_ADD(speed_delta, speed_delta);
	}
	return speed_delta;
}

static legacy_u16 apply_speed_delta(legacy_u16 updated_speed, legacy_s16 speed_delta)
{
	if (speed_delta >= CAR_SPEED_DELTA_STATIONARY) {
		if (updated_speed < LEGACY_U16_SIGN_BIT) {
			updated_speed = LEGACY_U16_WRAP_ADD(updated_speed, speed_delta);
		} else {
			updated_speed = LEGACY_U16_WRAP_ADD(updated_speed, speed_delta);
			if (updated_speed < LEGACY_U16_SIGN_BIT ||
				updated_speed > WRAPPED_REVERSE_SPEED_LIMIT) {
				updated_speed = WRAPPED_REVERSE_SPEED_LIMIT;
			}
		}
	} else if ((legacy_u16)LEGACY_S16_WRAP_NEGATE(speed_delta) > updated_speed) {
		updated_speed = CAR_SPEED_STOPPED;
	} else {
		updated_speed = LEGACY_U16_WRAP_ADD(updated_speed, speed_delta);
	}

	return updated_speed;
}

static void synchronize_wheel_speed(struct CARSTATE *carstate, legacy_u16 updated_speed)
{
	if (carstate->car_sumSurfRearWheels == CAR_WHEEL_CONTACT_NONE) {
		carstate->car_rev_speed = updated_speed;
	} else {
		legacy_s16 speed_difference =
			absolute_word(LEGACY_S16_WRAP_SUB(carstate->car_actual_speed, updated_speed));
		if (speed_difference > WHEEL_SPEED_SYNC_THRESHOLD) {
			carstate->car_rev_speed =
				(legacy_u16)(LEGACY_U32_WRAP_ADD(carstate->car_rev_speed,
												 carstate->car_actual_speed) >>
							 WHEEL_SPEED_AVERAGE_SHIFT);
			carstate->car_actual_speed = carstate->car_rev_speed;
			carstate->car_engineLimiterTimer = ENGINE_LIMITER_SHORT_TICKS;
		} else {
			carstate->car_rev_speed = updated_speed;
			carstate->car_actual_speed = updated_speed;
		}
	}
}

static void update_engine_limiter(struct CARSTATE *carstate, const struct SIMD *simd)
{
	if (carstate->car_sumSurfAllWheels != CAR_WHEEL_CONTACT_NONE &&
		carstate->car_lastrpm > carstate->car_currpm) {
		if (LEGACY_S16_WRAP_SUB(carstate->car_lastrpm, carstate->car_currpm) >
			RAPID_RPM_CHANGE_THRESHOLD) {
			if (simd->idle_torque * carstate->car_gearratioshr8 > ENGINE_TORQUE_LIMIT_THRESHOLD) {
				carstate->car_engineLimiterTimer = ENGINE_LIMITER_LONG_TICKS;
			}
		} else if (LEGACY_S16_WRAP_SUB(carstate->car_currpm, carstate->car_lastrpm) >
				   RAPID_RPM_CHANGE_THRESHOLD) {
			carstate->car_engineLimiterTimer = ENGINE_LIMITER_RECOVERY_TICKS;
			carstate->car_actual_speed =
				LEGACY_U16_WRAP_SUB(carstate->car_actual_speed, ENGINE_SPEED_CORRECTION);
		}
	}
}

void update_car_speed(legacy_s8 input_flags, legacy_s16 car_index, struct CARSTATE *carstate,
					  struct SIMD *simd)
{
	if (carstate->car_engineLimiterTimer != ENGINE_LIMITER_INACTIVE) {
		carstate->car_engineLimiterTimer =
			LEGACY_S8_WRAP_SUB(carstate->car_engineLimiterTimer, ENGINE_LIMITER_TICK_STEP);
	}
	carstate->car_speeddiff =
		LEGACY_S16_WRAP_SUB(carstate->car_actual_speed, carstate->car_lastspeed);
	carstate->car_lastspeed = carstate->car_actual_speed;
	carstate->car_lastrpm = carstate->car_currpm;

	request_gear_change(input_flags, carstate, simd);
	update_gear_knob(carstate, simd);
	legacy_s16 speed_delta = pedal_speed_delta(input_flags, car_index, carstate, simd);
	legacy_u16 updated_speed = apply_speed_delta(carstate->car_rev_speed, speed_delta);
	synchronize_wheel_speed(carstate, updated_speed);
	carstate->car_currpm =
		update_rpm_from_speed(carstate->car_currpm, carstate->car_rev_speed,
							  carstate->car_gearratio, carstate->car_changing_gear, simd->idle_rpm);
	update_engine_limiter(carstate, simd);

	if (carstate->car_actual_speed > state.game_topSpeed) {
		state.game_topSpeed = carstate->car_actual_speed;
	}
}

void update_car_speed_fraction(legacy_s8 input_flags, legacy_s16 car_index,
							   struct CARSTATE *carstate, struct SIMD *simd, legacy_u32 fraction20)
{
	if (fraction20 == 0) {
		return;
	}
	/* Gear changes and their timers belong to real ticks. Continue the force
	 * from the held pedals without generating another shift request. */
	legacy_s16 previous_rpm = carstate->car_currpm;
	legacy_s16 delta = pedal_speed_delta(input_flags, car_index, carstate, simd);
	legacy_u32 interval =
		framespersec == GAME_FRAME_RATE_LOW ? PHYSICS_LOW_RATE_TICK_FRACTION : PHYSICS_FRACTION_ONE;
	carstate->car_currpm = previous_rpm + (legacy_s32)(carstate->car_currpm - previous_rpm) *
											  (legacy_s32)fraction20 / (legacy_s32)interval;
	delta = (legacy_s16)((legacy_s32)delta * (legacy_s32)fraction20 / (legacy_s32)interval);
	/* Ground contact synchronizes both wheel and road speeds at once. Once
	 * equal, later fractional steps cannot repeat the mismatch correction. */
	synchronize_wheel_speed(carstate, apply_speed_delta(carstate->car_rev_speed, delta));
	carstate->car_currpm =
		update_rpm_from_speed(carstate->car_currpm, carstate->car_rev_speed,
							  carstate->car_gearratio, carstate->car_changing_gear, simd->idle_rpm);
}
