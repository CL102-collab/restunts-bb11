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

static legacy_s16 decay_suspension_target(struct CARSTATE *carstate, legacy_s16 wheel_index,
										  legacy_s16 decay)
{
	/* Decay the per-wheel target toward zero at the requested tick rate. */
	legacy_s16 target = (legacy_s16)carstate->car_suspension_target[wheel_index];
	if (target < 0) {
		target = LEGACY_S16_WRAP_ADD(target, decay);
		if (target > 0) {
			target = 0;
		}
	} else if (target > 0) {
		target = LEGACY_S16_WRAP_SUB(target, decay);
		if (target < 0) {
			target = 0;
		}
	}
	carstate->car_suspension_target[wheel_index] = target;
	return target;
}

static legacy_s16 return_wheel_suspension(struct CARSTATE *carstate, legacy_s16 wheel_index,
										  legacy_s16 target, legacy_s16 previous_deflection,
										  legacy_s16 return_step)
{
	legacy_s16 adjustment = 0;

	if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] > target) {
		carstate->car_suspension_deflection[wheel_index] =
			LEGACY_S16_WRAP_SUB(carstate->car_suspension_deflection[wheel_index], return_step);
		if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] < target) {
			carstate->car_suspension_deflection[wheel_index] = target;
		}
		adjustment = LEGACY_S16_WRAP_SUB(previous_deflection,
										 carstate->car_suspension_deflection[wheel_index]);
	} else if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] < target) {
		carstate->car_suspension_deflection[wheel_index] =
			LEGACY_S16_WRAP_ADD(carstate->car_suspension_deflection[wheel_index], return_step);
		if ((legacy_s16)carstate->car_suspension_deflection[wheel_index] > target) {
			carstate->car_suspension_deflection[wheel_index] = target;
		}
	}
	return adjustment;
}

static legacy_s16 update_wheel_suspension_step(struct CARSTATE *carstate,
											   legacy_s16 contact_delta_arg, legacy_s16 wheel_index,
											   legacy_s16 target_decay, legacy_s16 return_step)
{
	legacy_s16 previous_deflection = (legacy_s16)carstate->car_suspension_deflection[wheel_index];
	legacy_s16 contact_delta = (legacy_s16)contact_delta_arg;

	legacy_s16 target = decay_suspension_target(carstate, wheel_index, target_decay);

	if (contact_delta < 0 && (legacy_s16)carstate->car_suspension_deflection[wheel_index] >
								 LEGACY_S16_WRAP_NEGATE(contact_delta)) {
		contact_delta = 0;
	}

	legacy_s16 adjustment = 0;
	if (contact_delta == 0) {
		adjustment = return_wheel_suspension(carstate, wheel_index, target, previous_deflection,
											 return_step);
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

legacy_s16 update_wheel_suspension(struct CARSTATE *carstate, legacy_s16 contact_delta,
								   legacy_s16 wheel_index)
{
	return update_wheel_suspension_step(carstate, contact_delta, wheel_index,
										SUSPENSION_TARGET_DECAY, SUSPENSION_RETURN_STEP);
}

legacy_s16 update_wheel_suspension_fraction(struct CARSTATE *carstate, legacy_s16 contact_delta,
											legacy_s16 wheel_index, legacy_u32 fraction20)
{
	if (fraction20 == 0) {
		return carstate->car_suspension_deflection[wheel_index];
	}
	if (fraction20 > PHYSICS_FRACTION_ONE) {
		fraction20 = PHYSICS_FRACTION_ONE;
	}
	/* Contact distances are geometric constraints; only spring recovery and
	 * target decay are rates. Scaling penetration would leave wheels in ground. */
	legacy_s16 target_decay =
		(legacy_s16)((SUSPENSION_TARGET_DECAY * fraction20 + PHYSICS_FRACTION_ONE / 2) /
					 PHYSICS_FRACTION_ONE);
	legacy_s16 return_step =
		(legacy_s16)((SUSPENSION_RETURN_STEP * fraction20 + PHYSICS_FRACTION_ONE / 2) /
					 PHYSICS_FRACTION_ONE);
	return update_wheel_suspension_step(carstate, contact_delta, wheel_index, target_decay,
										return_step);
}
