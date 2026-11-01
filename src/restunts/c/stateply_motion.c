/* Prepare wheel travel and derive the working car pose in native update order. */
#include "legacy.h"
#include "math/math.h"
#include "physics_internal.h"
#include "residue.h"
#include "state_internal.h"
#include "crash_state.h"
#include "track/track_collision.h"
#include "wheel_transform.h"
#include "owoot/owoot.h"
#include "car/car_audio.h"
#include "car/car_resources.h"
#include "externs.h"
#ifdef RESTUNTS_SDL3
#include "extended/extended_stateply.h"
#endif

#include "stateply_internal.h"

legacy_s16 scale_player_fraction(legacy_s16 value, legacy_u32 fraction)
{
	if (fraction == PHYSICS_FRACTION_ONE) {
		return value;
	}
	legacy_s32 product = (legacy_s32)value * (legacy_s32)fraction;
	legacy_s32 rounding = (legacy_s32)(PHYSICS_FRACTION_ONE / 2);
	product += product < 0 ? -rounding : rounding;
	return (legacy_s16)(product / (legacy_s32)PHYSICS_FRACTION_ONE);
}

legacy_s16 scale_player_motion(legacy_s16 value)
{
	return player_motion_fraction == 0 ? value
									   : scale_player_fraction(value, player_motion_fraction);
}

legacy_u32 player_tick_fraction(void)
{
	/* Suspension recovery and slide rotation run once per original tick;
	 * forward travel and gravity instead use a fixed 20 Hz unit. */
	return framespersec == GAME_FRAME_RATE_LOW
			   ? player_motion_fraction / (GAME_FRAME_RATE_NORMAL / GAME_FRAME_RATE_LOW)
			   : player_motion_fraction;
}

/* A speculative impact changes only its disposable race. Sounds, particles,
 * result totals and race termination remain tied to authoritative updates. */
void player_motion_crash(legacy_s16 crash_event, legacy_s16 car_index)
{
	if (player_motion_fraction == 0) {
		update_crash_state(crash_event, car_index);
		return;
	}
	struct CARSTATE *carstate = car_index == PLAYER_CAR_INDEX ? &player_motion_state->playerstate
															  : &player_motion_state->opponentstate;
	if (carstate->car_crashBmpFlag != CRASH_EVENT_NONE) {
		return;
	}
	carstate->car_crashBmpFlag =
		crash_event == CRASH_EVENT_IMMEDIATE_STOP ? CRASH_EVENT_COLLISION : crash_event;
	if (car_index == PLAYER_CAR_INDEX) {
		player_motion_state->game_pEndFrame = player_motion_state->game_frame;
		player_motion_state->game_impactSpeed = carstate->car_actual_speed;
	} else {
		player_motion_state->game_oEndFrame = player_motion_state->game_frame;
	}
	if (crash_event == CRASH_EVENT_IMMEDIATE_STOP || crash_event == CRASH_EVENT_WATER) {
		carstate->car_actual_speed = CAR_SPEED_STOPPED;
		carstate->car_rev_speed = CAR_SPEED_STOPPED;
	}
}

/* Hand a long-precision position back as the word-precision vector the rest
 * of the engine works with. */
void physics_position_to_vector(struct VECTOR *destination, const struct VECTORLONG *source)
{
	destination->x = position_to_word(source->lx);
	destination->y = position_to_word(source->ly);
	destination->z = position_to_word(source->lz);
}

/* Wheel positions are long-precision; the forces acting on them arrive as
 * word-precision vectors, so every step shifts the position the same way. */
void physics_position_offset(struct VECTORLONG *destination, const struct VECTORLONG *source,
							 const struct VECTOR *offset)
{
	destination->lx = LEGACY_S32_WRAP_ADD_S16(source->lx, offset->x);
	destination->ly = LEGACY_S32_WRAP_ADD_S16(source->ly, offset->y);
	destination->lz = LEGACY_S32_WRAP_ADD_S16(source->lz, offset->z);
}

void physics_position_pull_back(struct VECTORLONG *position, const struct VECTOR *offset)
{
	position->lx = LEGACY_S32_WRAP_SUB_S16(position->lx, offset->x);
	position->ly = LEGACY_S32_WRAP_SUB_S16(position->ly, offset->y);
	position->lz = LEGACY_S32_WRAP_SUB_S16(position->lz, offset->z);
}

static void prepare_opponent_rear_wheel(struct VECTOR *wheel, struct VECTOR *rotated,
										struct MATRIX *angle_rotation, legacy_s16 wheel_index,
										legacy_s16 y_adjustment)
{
	*wheel = simd_opponent.wheel_coords[wheel_index];
	wheel->y = LEGACY_S16_WRAP_ADD(
		LEGACY_S16_WRAP_NEGATE(LEGACY_S16_WRAP_ADD(
			player_motion_state->opponentstate.car_suspension_deflection[wheel_index],
			PLAYER_PHYSICS_SUSPENSION_TRAVEL_LIMIT)),
		y_adjustment);
	if ((player_motion_state->opponentstate.car_slide_yaw_delta & ANGLE_MASK) != 0) {
		mat_mul_vector(wheel, angle_rotation, rotated);
		*wheel = *rotated;
	}
}

legacy_s16 scaled_vector_separation(struct VECTOR *first, struct VECTOR *second,
									struct VECTOR *intersection, struct VECTOR *delta)
{
	interpolate_collision_at_z(first, second, intersection, 0);
	delta->x = LEGACY_S16_SHL(LEGACY_S16_WRAP_SUB(first->x, intersection->x),
							  PLAYER_PHYSICS_POSITION_SCALE_SHIFT);
	delta->y = LEGACY_S16_SHL(LEGACY_S16_WRAP_SUB(first->y, intersection->y),
							  PLAYER_PHYSICS_POSITION_SCALE_SHIFT);
	delta->z = LEGACY_S16_SHL(LEGACY_S16_WRAP_SUB(first->z, intersection->z),
							  PLAYER_PHYSICS_POSITION_SCALE_SHIFT);
	return polarRadius3D(delta);
}

void initialize_working_car_pose(struct CARSTATE *carstate)
{
	car_working_x = carstate->car_position.lx;
	car_working_y = carstate->car_position.ly;
	car_working_z = carstate->car_position.lz;
	carstate->car_previous_position.lx = carstate->car_position.lx;
	carstate->car_previous_position.ly = carstate->car_position.ly;
	carstate->car_previous_position.lz = carstate->car_position.lz;
	car_working_roll = carstate->car_rotate.z;
	car_initial_roll = carstate->car_rotate.z;
	car_working_pitch = carstate->car_rotate.y;
	car_initial_pitch = carstate->car_rotate.y;
	car_working_yaw = carstate->car_rotate.x;
	car_initial_yaw = carstate->car_rotate.x;
}

/* The original zero-speed crash transition reused opponent wheel coordinates
 * as headings. Keep that compatibility detail separate from normal motion. */
static void restore_crash_wheel_headings(legacy_s16 *wheel_plane_headings)
{
	car_to_world_rotation =
		*mat_rot_zxy(LEGACY_S16_WRAP_NEGATE(player_motion_state->opponentstate.car_rotate.z),
					 LEGACY_S16_WRAP_NEGATE(player_motion_state->opponentstate.car_rotate.y),
					 LEGACY_S16_WRAP_NEGATE(player_motion_state->opponentstate.car_rotate.x),
					 MATRIX_ROTATION_ORDER_ZXY);
	struct VECTOR transformed_vector;
	legacy_s16 inverted_wheel_adjustment = 0;
	struct VECTOR wheel_vector;
	if (player_motion_state->opponentstate.car_sumSurfAllWheels != CAR_WHEEL_CONTACT_NONE &&
		player_motion_state->opponentstate.car_actual_speed <= PLAYER_PHYSICS_LOW_SPEED_LIMIT) {
		wheel_vector.x = 0;
		wheel_vector.y = PLAYER_PHYSICS_UP_VECTOR_LENGTH;
		wheel_vector.z = 0;
		mat_mul_vector(&wheel_vector, &car_to_world_rotation, &transformed_vector);
		if (transformed_vector.y < 0) {
			inverted_wheel_adjustment = PLAYER_PHYSICS_INVERTED_WHEEL_ADJUSTMENT;
		}
	}

	/* Prepare the optional auxiliary wheel rotation once for both wheels. */
	struct MATRIX wheel_adjustment_rotation;
	if ((player_motion_state->opponentstate.car_slide_yaw_delta & ANGLE_MASK) != 0) {
		wheel_adjustment_rotation = *mat_rot_zxy(
			0, 0, LEGACY_S16_WRAP_NEGATE(player_motion_state->opponentstate.car_slide_yaw_delta),
			MATRIX_ROTATION_ORDER_ZXY);
	}

	/*
	 * Rebuild the first rear wheel in car-local coordinates, including
	 * suspension travel and the low-speed inverted-car adjustment.
	 */
	prepare_opponent_rear_wheel(&wheel_vector, &transformed_vector, &wheel_adjustment_rotation,
								PLAYER_PHYSICS_REAR_WHEEL_FIRST, inverted_wheel_adjustment);

	/*
	 * Rotate the first rear wheel into world axes and recover the high word
	 * of its world Z coordinate, the first aliased stack word.
	 */
	mat_mul_vector(&wheel_vector, &car_to_world_rotation, &transformed_vector);
	wheel_plane_headings[LEGACY_RESIDUE_FIRST_WORD] =
		(legacy_u16)((legacy_u32)LEGACY_S32_WRAP_ADD_S16(
						 player_motion_state->opponentstate.car_position.lz,
						 transformed_vector.z) >>
					 LEGACY_WORD_BITS);
	legacy_execution_residue.wheel_plane_angles[LEGACY_RESIDUE_FIRST_WORD] =
		wheel_plane_headings[LEGACY_RESIDUE_FIRST_WORD];

	/* Rebuild the second rear wheel with the same local adjustments. */
	prepare_opponent_rear_wheel(&wheel_vector, &transformed_vector, &wheel_adjustment_rotation,
								PLAYER_PHYSICS_REAR_WHEEL_SECOND, inverted_wheel_adjustment);

	/*
	 * Rotate the second rear wheel into world axes and recover the low word
	 * of its world X coordinate, the second aliased stack word.
	 */
	mat_mul_vector(&wheel_vector, &car_to_world_rotation, &transformed_vector);
	wheel_plane_headings[LEGACY_RESIDUE_SECOND_WORD] = (legacy_u16)LEGACY_S32_WRAP_ADD_S16(
		player_motion_state->opponentstate.car_position.lx, transformed_vector.x);

	/* Commit the second word and recover world X's high word as the third. */
	legacy_execution_residue.wheel_plane_angles[LEGACY_RESIDUE_SECOND_WORD] =
		wheel_plane_headings[LEGACY_RESIDUE_SECOND_WORD];
	wheel_plane_headings[LEGACY_RESIDUE_THIRD_WORD] =
		(legacy_u16)((legacy_u32)LEGACY_S32_WRAP_ADD_S16(
						 player_motion_state->opponentstate.car_position.lx,
						 transformed_vector.x) >>
					 LEGACY_WORD_BITS);

	/* Commit the third word and recover world Y's low word as the fourth. */
	legacy_execution_residue.wheel_plane_angles[LEGACY_RESIDUE_THIRD_WORD] =
		wheel_plane_headings[LEGACY_RESIDUE_THIRD_WORD];
	wheel_plane_headings[LEGACY_RESIDUE_FOURTH_WORD] = (legacy_u16)LEGACY_S32_WRAP_ADD_S16(
		player_motion_state->opponentstate.car_position.ly, transformed_vector.y);
	legacy_execution_residue.wheel_plane_angles[LEGACY_RESIDUE_FOURTH_WORD] =
		wheel_plane_headings[LEGACY_RESIDUE_FOURTH_WORD];
}

static void restore_stopped_wheel_headings(struct CARSTATE *carstate,
										   struct PLAYER_WHEEL_MOTION *motion, legacy_s16 car_index)
{
	if (motion->travel != 0) {
		return;
	}
	for (legacy_s16 i = 0; i < PLAYER_PHYSICS_WHEEL_COUNT; i++) {
		motion->headings[i] = car_index == OPPONENT_CAR_INDEX
								  ? legacy_execution_residue.wheel_angle_stack_words[i]
								  : legacy_execution_residue.wheel_plane_angles[i];
	}
	/* The physics-only caller reconstructs overwritten opponent coordinates.
	 * A renderer caller has already supplied the later writes to these slots. */
	if (!legacy_render_player_headings_active && car_index == PLAYER_CAR_INDEX &&
		gameconfig.game_opponenttype != 0 && carstate->car_lastspeed != CAR_SPEED_STOPPED &&
		carstate->car_crashBmpFlag != CRASH_EVENT_NONE) {
		restore_crash_wheel_headings(motion->headings);
	}
}

static void prepare_wheel_rotation(struct CARSTATE *carstate, struct PLAYER_WHEEL_MOTION *motion)
{
	car_to_world_rotation = *mat_rot_zxy(
		LEGACY_S16_WRAP_NEGATE(car_working_roll), LEGACY_S16_WRAP_NEGATE(car_working_pitch),
		LEGACY_S16_WRAP_NEGATE(car_working_yaw), MATRIX_ROTATION_ORDER_ZXY);
	struct VECTOR transformed_vector;
	struct VECTOR wheel_vector;
	if (car_working_pitch != 0 || car_working_roll != 0) {
		wheel_vector.x = 0;
		wheel_vector.y = 0;
		wheel_vector.z = PLAYER_PHYSICS_PSEUDO_GRAVITY_LENGTH;
		mat_mul_vector(&wheel_vector, &car_to_world_rotation, &transformed_vector);
		carstate->car_pseudoGravity = LEGACY_S16_WRAP_NEGATE(transformed_vector.y);
	} else {
		carstate->car_pseudoGravity = 0;
	}

	wheel_vector.x = 0;
	wheel_vector.y = PLAYER_PHYSICS_UP_VECTOR_LENGTH;
	wheel_vector.z = 0;
	mat_mul_vector(&wheel_vector, &car_to_world_rotation, &transformed_vector);
	/* Attachment is applied once per native tick, even at 10 Hz. */
	legacy_s16 inverted_step = player_motion_fraction == 0
								   ? PLAYER_PHYSICS_INVERTED_WHEEL_ADJUSTMENT
								   : scale_player_fraction(PLAYER_PHYSICS_INVERTED_WHEEL_ADJUSTMENT,
														   player_tick_fraction());
	if (carstate->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE || transformed_vector.y >= 0) {
		motion->inverted_adjustment = 0;
	} else if (carstate->car_actual_speed <= PLAYER_PHYSICS_LOW_SPEED_LIMIT) {
		motion->inverted_adjustment = -inverted_step;
	} else {
		motion->inverted_adjustment = inverted_step;
		wheel_vector.y = -inverted_step;
		mat_mul_vector(&wheel_vector, &car_to_world_rotation, &motion->inverted_offset);
	}
}

static legacy_s16 prepare_wheel_travel(struct CARSTATE *carstate,
									   struct PLAYER_WHEEL_MOTION *motion)
{
	for (legacy_s16 wheel_index = 0; wheel_index < PLAYER_PHYSICS_WHEEL_COUNT; wheel_index++) {
		motion->contact_distances[wheel_index] =
			legacy_execution_residue.grip_stack_words[wheel_index];
	}
	legacy_s16 front_wheel_heading_offset =
		carstate->car_sumSurfAllWheels != CAR_WHEEL_CONTACT_NONE
			? scale_cornering_angle(carstate->car_front_wheel_response_angle,
									PLAYER_PHYSICS_FRONT_WHEEL_HEADING_SHIFT)
			: 0;
	motion->travel =
		scale_speed_to_travel(carstate->car_actual_speed,
							  framespersec == GAME_FRAME_RATE_LOW && player_motion_fraction == 0
								  ? PLAYER_PHYSICS_LOW_RATE_TRAVEL_DIVISOR
								  : PLAYER_PHYSICS_NORMAL_RATE_TRAVEL_DIVISOR);
	motion->travel = scale_player_motion(motion->travel);
	return front_wheel_heading_offset;
}

void prepare_wheel_motion(struct CARSTATE *carstate, struct SIMD *simd,
						  struct PLAYER_WHEEL_MOTION *motion, legacy_s16 car_index)
{
	legacy_s16 front_wheel_heading_offset = prepare_wheel_travel(carstate, motion);
	restore_stopped_wheel_headings(carstate, motion, car_index);
	prepare_wheel_rotation(carstate, motion);
	motion->car_index = car_index;
	motion->surface_adhesion = car_surface_adhesion[car_index == OPPONENT_CAR_INDEX];
	if (motion->surface_adhesion) {
		/* The optional fan replaces the original speed-dependent loop attachment.
		 * Its contact test below also works when the car is stopped or vertical. */
		motion->inverted_adjustment = 0;
	}

	legacy_s16 slide_rotation =
		player_motion_fraction == 0
			? carstate->car_slide_yaw_delta
			: scale_player_fraction(carstate->car_slide_yaw_delta, player_tick_fraction());
	legacy_s16 has_slide_rotation = (slide_rotation & ANGLE_MASK) != 0;
	struct MATRIX wheel_adjustment_rotation;
	if (has_slide_rotation) {
		wheel_adjustment_rotation =
			*mat_rot_zxy(0, 0, LEGACY_S16_WRAP_NEGATE(slide_rotation), MATRIX_ROTATION_ORDER_ZXY);
	}
	wheel_forward_travel.x = 0;
	wheel_forward_travel.y = 0;
	planindex_copy = PLAYER_PHYSICS_PLANE_INDEX_NONE;
	struct VECTORLONG *previous_wheel_position;
	struct VECTOR wheel_vector;
	struct VECTOR transformed_vector;
	struct VECTORLONG *current_wheel_position;
	for (legacy_s16 wheel_index = 0; wheel_index < PLAYER_PHYSICS_WHEEL_COUNT; wheel_index++) {
		current_wheel_position = &motion->current[wheel_index];
		previous_wheel_position = &motion->previous[wheel_index];
		wheel_vector = simd->wheel_coords[wheel_index];
		wheel_vector.y = LEGACY_S16_WRAP_NEGATE(
			LEGACY_S16_WRAP_ADD(carstate->car_suspension_deflection[wheel_index],
								PLAYER_PHYSICS_SUSPENSION_TRAVEL_LIMIT));
		if (motion->inverted_adjustment < 0) {
			wheel_vector.y = LEGACY_S16_WRAP_SUB(wheel_vector.y, motion->inverted_adjustment);
		}
		if (has_slide_rotation != 0) {
			mat_mul_vector(&wheel_vector, &wheel_adjustment_rotation, &transformed_vector);
			wheel_vector = transformed_vector;
		}
		mat_mul_vector(&wheel_vector, &car_to_world_rotation, &transformed_vector);
		current_wheel_position->lx = LEGACY_S32_WRAP_ADD_S16(car_working_x, transformed_vector.x);
		current_wheel_position->ly = LEGACY_S32_WRAP_ADD_S16(car_working_y, transformed_vector.y);
		current_wheel_position->lz = LEGACY_S32_WRAP_ADD_S16(car_working_z, transformed_vector.z);

		previous_wheel_position->lx = current_wheel_position->lx;
		previous_wheel_position->ly = current_wheel_position->ly;
		previous_wheel_position->lz = current_wheel_position->lz;
		if (motion->travel != 0) {
			wheel_forward_travel.z = motion->travel;
			wheel_heading_offset = carstate->car_velocity_heading_offset;
			if (front_wheel_heading_offset != 0 && wheel_index < PLAYER_PHYSICS_FRONT_WHEEL_COUNT) {
				wheel_heading_offset = LEGACY_S16_WRAP_SUB(carstate->car_velocity_heading_offset,
														   front_wheel_heading_offset);
			}
			motion->headings[wheel_index] = wheel_heading_offset;
			legacy_execution_residue.wheel_plane_angles[wheel_index] = wheel_heading_offset;
			if (car_index == OPPONENT_CAR_INDEX) {
				legacy_execution_residue.wheel_angle_stack_words[wheel_index] =
					wheel_heading_offset;
			}
			transform_wheel_travel_to_world();
			physics_position_offset(current_wheel_position, current_wheel_position,
									&wheel_world_travel);
		}
	}
}

void apply_wheel_suspension(struct CARSTATE *carstate, struct PLAYER_WHEEL_MOTION *motion)
{
	struct VECTOR rotated_offset;
	struct VECTOR offset;
	for (legacy_s16 i = 0; i < PLAYER_PHYSICS_WHEEL_COUNT; i++) {
		physics_position_to_vector(&carstate->car_wheel_contact_positions[i], &motion->current[i]);
		legacy_s16 deflection =
			player_motion_fraction == 0
				? update_wheel_suspension(carstate, motion->contact_distances[i], i)
				: update_wheel_suspension_fraction(carstate, motion->contact_distances[i], i,
												   player_tick_fraction());
		offset.x = 0;
		offset.y = LEGACY_S16_WRAP_ADD(deflection, PLAYER_PHYSICS_SUSPENSION_TRAVEL_LIMIT);
		offset.z = 0;
		if (car_working_roll != 0 || car_working_pitch != 0) {
			mat_mul_vector(&offset, &car_to_world_rotation, &rotated_offset);
			physics_position_offset(&motion->current[i], &motion->current[i], &rotated_offset);
		} else {
			motion->current[i].ly = LEGACY_S32_WRAP_ADD_S16(motion->current[i].ly, offset.y);
		}
	}
}

static legacy_s32 wheel_axis_average(legacy_s32 front_first, legacy_s32 front_second,
									 legacy_s32 rear_first, legacy_s32 rear_second)
{
	return LEGACY_S32_SAR(LEGACY_S32_WRAP_ADD(LEGACY_S32_WRAP_ADD(front_first, front_second),
											  LEGACY_S32_WRAP_ADD(rear_first, rear_second)),
						  PLAYER_PHYSICS_WHEEL_CENTROID_SHIFT);
}

static legacy_s32 clamp_car_world_coordinate(legacy_s32 position)
{
	/* Preserve the original strict upper-bound comparison. */
	if (position > PLAYER_PHYSICS_WORLD_MAX_EXCLUSIVE) {
		return PLAYER_PHYSICS_WORLD_MAX_POSITION;
	}
	if (position < PLAYER_PHYSICS_WORLD_MIN_POSITION) {
		return PLAYER_PHYSICS_WORLD_MIN_POSITION;
	}
	return position;
}

static void derive_car_position_from_wheels(struct PLAYER_WHEEL_MOTION *motion,
											struct VECTOR *wheel_offsets)
{
	struct VECTORLONG *wheels = motion->current;

	car_working_x = wheel_axis_average(
		wheels[PLAYER_PHYSICS_FRONT_WHEEL_FIRST].lx, wheels[PLAYER_PHYSICS_FRONT_WHEEL_SECOND].lx,
		wheels[PLAYER_PHYSICS_REAR_WHEEL_FIRST].lx, wheels[PLAYER_PHYSICS_REAR_WHEEL_SECOND].lx);
	car_working_y = wheel_axis_average(
		wheels[PLAYER_PHYSICS_FRONT_WHEEL_FIRST].ly, wheels[PLAYER_PHYSICS_FRONT_WHEEL_SECOND].ly,
		wheels[PLAYER_PHYSICS_REAR_WHEEL_FIRST].ly, wheels[PLAYER_PHYSICS_REAR_WHEEL_SECOND].ly);
	car_working_z = wheel_axis_average(
		wheels[PLAYER_PHYSICS_FRONT_WHEEL_FIRST].lz, wheels[PLAYER_PHYSICS_FRONT_WHEEL_SECOND].lz,
		wheels[PLAYER_PHYSICS_REAR_WHEEL_FIRST].lz, wheels[PLAYER_PHYSICS_REAR_WHEEL_SECOND].lz);
	/* Orientation uses offsets from the unclamped wheel centroid. */
	for (legacy_s16 i = 0; i < PLAYER_PHYSICS_WHEEL_COUNT; i++) {
		wheel_offsets[i].x = physics_difference_word(wheels[i].lx, car_working_x);
		wheel_offsets[i].y = physics_difference_word(wheels[i].ly, car_working_y);
		wheel_offsets[i].z = physics_difference_word(wheels[i].lz, car_working_z);
	}
	if (car_working_y < 0) {
		car_working_y = 0;
	}
#ifdef RESTUNTS_SDL3
	if (!extended_player_clamp_world_pose(&car_working_x, &car_working_z))
#endif
	{
		car_working_x = clamp_car_world_coordinate(car_working_x);
		car_working_z = clamp_car_world_coordinate(car_working_z);
	}
}

static legacy_s16 apply_rotation_deadband(legacy_s16 angle)
{
	legacy_s16 magnitude = angle < 0 ? LEGACY_S16_WRAP_NEGATE(angle) : angle;

	return magnitude < PLAYER_PHYSICS_ROTATION_DEADBAND ? 0 : angle;
}

static void rotate_wheel_offsets(struct VECTOR *offsets, struct MATRIX *rotation)
{
	struct VECTOR input;

	for (legacy_s16 i = 0; i < PLAYER_PHYSICS_WHEEL_COUNT; i++) {
		input = offsets[i];
		mat_mul_vector(&input, rotation, &offsets[i]);
	}
}

static void derive_car_yaw(struct VECTOR *wheel_offsets)
{
	legacy_s16 lateral_delta = wheel_pair_delta(wheel_offsets[PLAYER_PHYSICS_REAR_WHEEL_SECOND].x,
												wheel_offsets[PLAYER_PHYSICS_REAR_WHEEL_FIRST].x,
												wheel_offsets[PLAYER_PHYSICS_FRONT_WHEEL_FIRST].x,
												wheel_offsets[PLAYER_PHYSICS_FRONT_WHEEL_SECOND].x);
	legacy_s16 longitudinal_delta =
		wheel_pair_delta(wheel_offsets[PLAYER_PHYSICS_REAR_WHEEL_SECOND].z,
						 wheel_offsets[PLAYER_PHYSICS_REAR_WHEEL_FIRST].z,
						 wheel_offsets[PLAYER_PHYSICS_FRONT_WHEEL_FIRST].z,
						 wheel_offsets[PLAYER_PHYSICS_FRONT_WHEEL_SECOND].z);
	car_working_yaw = LEGACY_S16_FROM_BITS(
		(legacy_u16)polarAngle(lateral_delta, LEGACY_S16_WRAP_NEGATE(longitudinal_delta)) &
		ANGLE_MASK);
}

static void derive_car_pitch(struct VECTOR *wheel_offsets)
{
	legacy_s16 longitudinal_delta =
		wheel_pair_delta(wheel_offsets[PLAYER_PHYSICS_REAR_WHEEL_SECOND].z,
						 wheel_offsets[PLAYER_PHYSICS_REAR_WHEEL_FIRST].z,
						 wheel_offsets[PLAYER_PHYSICS_FRONT_WHEEL_FIRST].z,
						 wheel_offsets[PLAYER_PHYSICS_FRONT_WHEEL_SECOND].z);
	legacy_s16 vertical_delta =
		wheel_pair_delta(wheel_offsets[PLAYER_PHYSICS_REAR_WHEEL_SECOND].y,
						 wheel_offsets[PLAYER_PHYSICS_REAR_WHEEL_FIRST].y,
						 wheel_offsets[PLAYER_PHYSICS_FRONT_WHEEL_FIRST].y,
						 wheel_offsets[PLAYER_PHYSICS_FRONT_WHEEL_SECOND].y);
	car_working_pitch = 0;
	if (vertical_delta != 0 || longitudinal_delta >= 0) {
		car_working_pitch = apply_rotation_deadband(LEGACY_S16_WRAP_SUB(
			polarAngle(LEGACY_S16_WRAP_NEGATE(longitudinal_delta), vertical_delta),
			ANGLE_QUARTER_TURN));
	}
}

static void derive_car_roll(struct VECTOR *wheel_offsets)
{
	legacy_s16 lateral_delta = wheel_pair_delta(wheel_offsets[PLAYER_PHYSICS_FRONT_WHEEL_SECOND].x,
												wheel_offsets[PLAYER_PHYSICS_REAR_WHEEL_FIRST].x,
												wheel_offsets[PLAYER_PHYSICS_FRONT_WHEEL_FIRST].x,
												wheel_offsets[PLAYER_PHYSICS_REAR_WHEEL_SECOND].x);
	legacy_s16 vertical_delta = wheel_pair_delta(wheel_offsets[PLAYER_PHYSICS_FRONT_WHEEL_SECOND].y,
												 wheel_offsets[PLAYER_PHYSICS_REAR_WHEEL_FIRST].y,
												 wheel_offsets[PLAYER_PHYSICS_FRONT_WHEEL_FIRST].y,
												 wheel_offsets[PLAYER_PHYSICS_REAR_WHEEL_SECOND].y);

	car_working_roll = 0;
	if (vertical_delta != 0 || lateral_delta <= 0) {
		car_working_roll = apply_rotation_deadband(
			LEGACY_S16_WRAP_SUB(polarAngle(lateral_delta, vertical_delta), ANGLE_QUARTER_TURN));
	}
}

void derive_car_pose_from_wheels(struct PLAYER_WHEEL_MOTION *motion)
{
	struct VECTOR wheel_offsets[PLAYER_PHYSICS_WHEEL_COUNT];
	derive_car_position_from_wheels(motion, wheel_offsets);
	derive_car_yaw(wheel_offsets);
	struct MATRIX rotation;
	mat_rot_y(&rotation, car_working_yaw);
	rotate_wheel_offsets(wheel_offsets, &rotation);
	derive_car_pitch(wheel_offsets);
	if (car_working_pitch != 0) {
		mat_rot_x(&rotation, car_working_pitch);
		rotate_wheel_offsets(wheel_offsets, &rotation);
	}
	derive_car_roll(wheel_offsets);
}

void sum_front_and_rear_wheel_surfaces(struct CARSTATE *carstate)
{
	carstate->car_sumSurfFrontWheels =
		LEGACY_S8_WRAP_ADD(carstate->car_surfaceWhl[PLAYER_PHYSICS_FRONT_WHEEL_FIRST],
						   carstate->car_surfaceWhl[PLAYER_PHYSICS_FRONT_WHEEL_SECOND]);
	carstate->car_sumSurfRearWheels =
		LEGACY_S8_WRAP_ADD(carstate->car_surfaceWhl[PLAYER_PHYSICS_REAR_WHEEL_FIRST],
						   carstate->car_surfaceWhl[PLAYER_PHYSICS_REAR_WHEEL_SECOND]);
}

void commit_working_car_pose(struct CARSTATE *carstate)
{
	carstate->car_position.lx = car_working_x;
	carstate->car_position.ly = car_working_y;
	carstate->car_position.lz = car_working_z;
	carstate->car_rotate.z = car_working_roll;
	carstate->car_rotate.y = car_working_pitch;
	carstate->car_rotate.x = car_working_yaw;
	carstate->car_collision_latch = CAR_COLLISION_LATCH_CLEAR;
}
