#include "../legacy.h"
#include "../math/math.h"
#include "physics_internal.h"
#include "../residue.h"
#include "../track/trackdata_layout.h"
#include "../track/track_objects.h"
#include "../track/track_collision.h"
#include "../externs.h"
#include "physics_collision_internal.h"

#define SUSPENSION_TARGET_DECAY 4
#define SUSPENSION_RETURN_STEP 128
#define CONTACT_DELTA_LIMIT 192
#define SUSPENSION_TRAVEL_LIMIT 384
#define SUSPENSION_SOFT_CONTACT_THRESHOLD (-288)
#define HARD_CONTACT_SCALE_NUMERATOR 3

static legacy_s16 decay_suspension_target(struct CARSTATE *carstate, legacy_s16 wheel_index)
{
	/* Decay the per-wheel target toward zero once per physics tick. */
	legacy_s16 target = (legacy_s16)carstate->car_suspension_target[wheel_index];
	if (target < 0) {
		target = LEGACY_S16_WRAP_ADD(target, SUSPENSION_TARGET_DECAY);
		if (target > 0) {
			target = 0;
		}
	} else if (target > 0) {
		target = LEGACY_S16_WRAP_SUB(target, SUSPENSION_TARGET_DECAY);
		if (target < 0) {
			target = 0;
		}
	}
	carstate->car_suspension_target[wheel_index] = target;
	return target;
}

static legacy_s16 return_wheel_suspension(struct CARSTATE *carstate, legacy_s16 wheel_index,
										  legacy_s16 target, legacy_s16 previous_deflection)
{
	legacy_s16 adjustment = 0;

	if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] > target) {
		carstate->car_suspension_deflection[wheel_index] = LEGACY_S16_WRAP_SUB(
			carstate->car_suspension_deflection[wheel_index], SUSPENSION_RETURN_STEP);
		if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] < target) {
			carstate->car_suspension_deflection[wheel_index] = target;
		}
		adjustment = LEGACY_S16_WRAP_SUB(previous_deflection,
										 carstate->car_suspension_deflection[wheel_index]);
	} else if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] < target) {
		carstate->car_suspension_deflection[wheel_index] = LEGACY_S16_WRAP_ADD(
			carstate->car_suspension_deflection[wheel_index], SUSPENSION_RETURN_STEP);
		if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] > target) {
			carstate->car_suspension_deflection[wheel_index] = target;
		}
	}
	return adjustment;
}

legacy_s16 update_wheel_suspension(struct CARSTATE *carstate, legacy_s16 contact_delta_arg,
								   legacy_s16 wheel_index)
{
	legacy_s16 previous_deflection = (legacy_s16)carstate->car_suspension_deflection[wheel_index];
	legacy_s16 contact_delta = (legacy_s16)contact_delta_arg;

	legacy_s16 target = decay_suspension_target(carstate, wheel_index);

	if (contact_delta < 0 && (legacy_s16)carstate->car_suspension_deflection[wheel_index] >
								 LEGACY_S16_WRAP_NEGATE(contact_delta)) {
		contact_delta = 0;
	}

	legacy_s16 adjustment = 0;
	if (contact_delta == 0) {
		adjustment = return_wheel_suspension(carstate, wheel_index, target, previous_deflection);
	} else if (contact_delta > 0) {
		if (contact_delta > CONTACT_DELTA_LIMIT) {
			contact_delta = CONTACT_DELTA_LIMIT;
		}
		carstate->car_suspension_deflection[wheel_index] =
			LEGACY_S16_WRAP_ADD(carstate->car_suspension_deflection[wheel_index], contact_delta);
		if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] >
			SUSPENSION_TRAVEL_LIMIT) {
			carstate->car_suspension_deflection[wheel_index] = SUSPENSION_TRAVEL_LIMIT;
		}
		carstate->car_reserved_contact_state[wheel_index] = 0;
	} else {
		if (LEGACY_S16_WRAP_ADD(contact_delta, carstate->car_suspension_deflection[wheel_index]) >
			SUSPENSION_SOFT_CONTACT_THRESHOLD) {
			carstate->car_suspension_deflection[wheel_index] = LEGACY_S16_WRAP_ADD(
				carstate->car_suspension_deflection[wheel_index], contact_delta);
		} else {
			legacy_s16 scaled_delta =
				LEGACY_S16_SAR2(LEGACY_S16_WRAP_MUL(contact_delta, HARD_CONTACT_SCALE_NUMERATOR));
			carstate->car_suspension_deflection[wheel_index] =
				LEGACY_S16_WRAP_ADD(carstate->car_suspension_deflection[wheel_index], scaled_delta);
			if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] <
				-SUSPENSION_TRAVEL_LIMIT) {
				carstate->car_suspension_deflection[wheel_index] = -SUSPENSION_TRAVEL_LIMIT;
			}
		}
		adjustment = LEGACY_S16_WRAP_ADD(
			LEGACY_S16_WRAP_SUB(previous_deflection,
								carstate->car_suspension_deflection[wheel_index]),
			contact_delta);
	}

	return LEGACY_S16_WRAP_ADD(previous_deflection, adjustment);
}
