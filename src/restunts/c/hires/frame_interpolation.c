#include "frame_interpolation.h"

static legacy_u32 interpolation_fraction(legacy_u32 fraction)
{
	return fraction > FRAME_INTERPOLATION_ONE ? FRAME_INTERPOLATION_ONE : fraction;
}

static legacy_s32 interpolate_position(legacy_s32 current, legacy_s32 previous, legacy_u32 fraction)
{
	/* The difference and Q16 product both need wider than signed 32 bits.
	 * A clamped fraction keeps the result between the two endpoints. */
	return (legacy_s32)(previous + ((legacy_s64)current - previous) * fraction /
									   (legacy_s64)FRAME_INTERPOLATION_ONE);
}

static legacy_s16 interpolate_word(legacy_s16 current, legacy_s16 previous, legacy_u32 fraction)
{
	return (legacy_s16)interpolate_position(current, previous, fraction);
}

static legacy_s16 interpolate_angle(legacy_s16 current, legacy_s16 previous, legacy_u32 fraction)
{
	if (fraction == 0) {
		return previous;
	}
	if (fraction == FRAME_INTERPOLATION_ONE) {
		return current;
	}
	legacy_s32 delta =
		(legacy_s32)((legacy_u32)((legacy_s32)current - previous + ANGLE_HALF_TURN) & ANGLE_MASK) -
		ANGLE_HALF_TURN;
	legacy_s32 angle =
		previous + delta * (legacy_s32)fraction / (legacy_s32)FRAME_INTERPOLATION_ONE;
	return (legacy_s16)((legacy_u32)angle & ANGLE_MASK);
}

void frame_interpolate_vector(struct VECTOR *result, const struct VECTOR *current,
							  const struct VECTOR *previous, legacy_u32 fraction)
{
	fraction = interpolation_fraction(fraction);
	result->x = interpolate_word(current->x, previous->x, fraction);
	result->y = interpolate_word(current->y, previous->y, fraction);
	result->z = interpolate_word(current->z, previous->z, fraction);
}

void frame_interpolate_car(struct CARSTATE *result, const struct CARSTATE *current,
						   const struct CARSTATE *previous, legacy_u32 fraction)
{
	*result = *current;
	fraction = interpolation_fraction(fraction);
	/* Confirmed crashes change the visible pose and effects together. */
	if (fraction == FRAME_INTERPOLATION_ONE ||
		current->car_crashBmpFlag != previous->car_crashBmpFlag) {
		return;
	}
	result->car_position.lx =
		interpolate_position(current->car_position.lx, previous->car_position.lx, fraction);
	result->car_position.ly =
		interpolate_position(current->car_position.ly, previous->car_position.ly, fraction);
	result->car_position.lz =
		interpolate_position(current->car_position.lz, previous->car_position.lz, fraction);
	result->car_rotate.x =
		interpolate_angle(current->car_rotate.x, previous->car_rotate.x, fraction);
	result->car_rotate.y =
		interpolate_angle(current->car_rotate.y, previous->car_rotate.y, fraction);
	result->car_rotate.z =
		interpolate_angle(current->car_rotate.z, previous->car_rotate.z, fraction);
	result->car_steeringAngle =
		interpolate_word(current->car_steeringAngle, previous->car_steeringAngle, fraction);
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		result->car_suspension_deflection[wheel] =
			interpolate_word(current->car_suspension_deflection[wheel],
							 previous->car_suspension_deflection[wheel], fraction);
	}
}

void frame_interpolate_state(struct GAMESTATE *result, const struct GAMESTATE *current,
							 const struct GAMESTATE *previous, legacy_u32 fraction)
{
	*result = *current;
	fraction = interpolation_fraction(fraction);
	if (fraction == FRAME_INTERPOLATION_ONE) {
		return;
	}
	frame_interpolate_car(&result->playerstate, &current->playerstate, &previous->playerstate,
						  fraction);
	frame_interpolate_car(&result->opponentstate, &current->opponentstate, &previous->opponentstate,
						  fraction);
	for (legacy_u16 car = 0; car < GAMESTATE_CAR_VECTOR_COUNT; car++) {
		const struct CARSTATE *current_car =
			car == PLAYER_CAR_INDEX ? &current->playerstate : &current->opponentstate;
		const struct CARSTATE *previous_car =
			car == PLAYER_CAR_INDEX ? &previous->playerstate : &previous->opponentstate;
		if (current->game_trackside_camera_index[car] ==
				previous->game_trackside_camera_index[car] &&
			current_car->car_crashBmpFlag == previous_car->car_crashBmpFlag) {
			frame_interpolate_vector(&result->game_follow_camera_position[car],
									 &current->game_follow_camera_position[car],
									 &previous->game_follow_camera_position[car], fraction);
		}
	}
	if (current->game_particles_active == 0 || previous->game_particles_active == 0) {
		return;
	}
	for (legacy_u16 part = 0; part < GAMESTATE_PARTICLE_SLOT_COUNT; part++) {
		/* Forward speed stays fixed for a particle's lifetime. A changed
		 * speed identifies a reused slot even if owner and shape match. */
		if (current->game_particle_forward_speed[part] == 0 ||
			current->game_particle_forward_speed[part] !=
				previous->game_particle_forward_speed[part] ||
			current->game_particle_owner[part] != previous->game_particle_owner[part] ||
			current->game_particle_shape_index[part] != previous->game_particle_shape_index[part]) {
			continue;
		}
		result->game_particle_x[part] = interpolate_position(
			current->game_particle_x[part], previous->game_particle_x[part], fraction);
		result->game_particle_y[part] = interpolate_position(
			current->game_particle_y[part], previous->game_particle_y[part], fraction);
		result->game_particle_z[part] = interpolate_position(
			current->game_particle_z[part], previous->game_particle_z[part], fraction);
		result->game_particle_rotation_x[part] =
			interpolate_angle(current->game_particle_rotation_x[part],
							  previous->game_particle_rotation_x[part], fraction);
		result->game_particle_rotation_y[part] =
			interpolate_angle(current->game_particle_rotation_y[part],
							  previous->game_particle_rotation_y[part], fraction);
		result->game_particle_heading[part] = interpolate_angle(
			current->game_particle_heading[part], previous->game_particle_heading[part], fraction);
	}
}
