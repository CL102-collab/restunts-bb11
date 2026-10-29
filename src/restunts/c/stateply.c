#include "legacy.h"
#include "math.h"
#include "physics_internal.h"
#include "phantom_physics.h"
#include "residue.h"
#include "state_internal.h"
#include "crash_state.h"
#include "track_collision.h"
#include "wheel_transform.h"
#include "owoot.h"
#include "car_audio.h"
#include "car_resources.h"
#include "externs.h"
#ifdef RESTUNTS_SDL3
#include "extended_track.h"
#include "extended_geometry.h"
#endif

#define PLAYER_PHYSICS_WHEEL_COUNT 4
#define PLAYER_PHYSICS_FRONT_WHEEL_COUNT 2
#define PLAYER_PHYSICS_FRONT_WHEEL_HEADING_SHIFT 2U

enum PLAYER_PHYSICS_WHEEL_INDEX {
	PLAYER_PHYSICS_FRONT_WHEEL_FIRST = 0,
	PLAYER_PHYSICS_FRONT_WHEEL_SECOND = 1,
	PLAYER_PHYSICS_REAR_WHEEL_FIRST = 2,
	PLAYER_PHYSICS_REAR_WHEEL_SECOND = 3
};

#define PLAYER_PHYSICS_WHEEL_CENTROID_SHIFT 2U
#define PLAYER_PHYSICS_POSE_VECTOR_COUNT 2

enum PLAYER_PHYSICS_POSE_VECTOR_INDEX {
	PLAYER_PHYSICS_POSE_POSITION_INDEX = 0,
	PLAYER_PHYSICS_POSE_ROTATION_INDEX = 1
};

#define PLAYER_PHYSICS_COLLISION_POINT_CAPACITY 32
#define PLAYER_PHYSICS_POSITION_SCALE_SHIFT 6U
#define PLAYER_PHYSICS_TRACK_COORDINATE_SHIFT 10U
#define PLAYER_PHYSICS_LOW_RATE_TRAVEL_DIVISOR 7680U
#define PLAYER_PHYSICS_NORMAL_RATE_TRAVEL_DIVISOR 15360U
#define PLAYER_PHYSICS_LOW_SPEED_LIMIT 7680U
#define PLAYER_PHYSICS_SUSPENSION_TRAVEL_LIMIT 384
#define PLAYER_PHYSICS_UP_VECTOR_LENGTH 30000
#define PLAYER_PHYSICS_INVERTED_WHEEL_ADJUSTMENT 192
#define PLAYER_PHYSICS_PSEUDO_GRAVITY_LENGTH 130
#define PLAYER_PHYSICS_WALL_PUSH_DISTANCE 768
#define PLAYER_PHYSICS_WALL_SPEED_ANGLE_MULTIPLIER 70
#define PLAYER_PHYSICS_WALL_SPEED_BIAS 100
#define PLAYER_PHYSICS_WALL_SPEED_SHIFT 8U
#define PLAYER_PHYSICS_WALL_SOUND_FLAG 16U
#define PLAYER_PHYSICS_SUSPENSION_SOUND_FLAG 32U
#define PLAYER_PHYSICS_CONTACT_DISTANCE_LIMIT PHYSICS_PLANE_CONTACT_TOLERANCE
#define PLAYER_PHYSICS_INVERTED_CONTACT_DISTANCE_LIMIT PHYSICS_UNDERSIDE_CLEARANCE
#define PLAYER_PHYSICS_PLANE_PENETRATION_BIAS 6
#define PLAYER_PHYSICS_PLANE_RETRACE_DISTANCE 64
#define PLAYER_PHYSICS_SUSPENSION_SOUND_THRESHOLD 250
#define PLAYER_PHYSICS_SUSPENSION_CRASH_THRESHOLD 23275
#define PLAYER_PHYSICS_WORLD_MIN_POSITION 3840L
#define PLAYER_PHYSICS_WORLD_MAX_POSITION 1962239L
#define PLAYER_PHYSICS_WORLD_MAX_EXCLUSIVE 1962240L
#define PLAYER_PHYSICS_TRACK_GRID_LAST_COORDINATE 29
#define PLAYER_PHYSICS_TRACK_GRID_SIZE 30
#define PLAYER_PHYSICS_START_FINISH_POLE_OFFSET 126
#define PLAYER_PHYSICS_COLLISION_RETRY_LIMIT 5
#define PLAYER_PHYSICS_RESIDUE_MATRIX_FIRST_VALUE 4U
#define PLAYER_PHYSICS_ROTATION_DEADBAND 2
#define PLAYER_PHYSICS_PLANE_INDEX_NONE (-1)
#define PLAYER_PHYSICS_GROUND_PLANE_INDEX 0
#define PLAYER_PHYSICS_WALL_INDEX_NONE (-1)
#define PLAYER_PHYSICS_ROADSIDE_SIGN_INDEX_NONE (-1)
#define PLAYER_PHYSICS_OBJECT_PARTICLE_KIND_OFFSET 2

/* A zero fraction leaves every authoritative 10/20 Hz operation unchanged.
 * Only the synchronous renderer wrapper installs a shorter 20 Hz time step. */
static legacy_u32 player_motion_fraction;
static struct GAMESTATE *player_motion_state = &state;

static legacy_s16 scale_player_fraction(legacy_s16 value, legacy_u32 fraction)
{
	if (fraction == PHANTOM_PHYSICS_ONE) {
		return value;
	}
	legacy_s32 product = (legacy_s32)value * (legacy_s32)fraction;
	legacy_s32 rounding = (legacy_s32)(PHANTOM_PHYSICS_ONE / 2);
	product += product < 0 ? -rounding : rounding;
	return (legacy_s16)(product / (legacy_s32)PHANTOM_PHYSICS_ONE);
}

static legacy_s16 scale_player_motion(legacy_s16 value)
{
	return player_motion_fraction == 0 ? value
									   : scale_player_fraction(value, player_motion_fraction);
}

static legacy_u32 player_tick_fraction(void)
{
	/* Suspension recovery and slide rotation run once per original tick;
	 * forward travel and gravity instead use a fixed 20 Hz unit. */
	return framespersec == GAME_FRAME_RATE_LOW
			   ? player_motion_fraction / (GAME_FRAME_RATE_NORMAL / GAME_FRAME_RATE_LOW)
			   : player_motion_fraction;
}

/* A speculative impact changes only its disposable race. Sounds, particles,
 * result totals and race termination remain tied to authoritative updates. */
static void player_motion_crash(legacy_s16 crash_event, legacy_s16 car_index)
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

/* Per-tick wheel geometry shared by contact correction and suspension. */
struct PLAYER_WHEEL_MOTION {
	struct VECTORLONG current[PLAYER_PHYSICS_WHEEL_COUNT];
	struct VECTORLONG previous[PLAYER_PHYSICS_WHEEL_COUNT];
	legacy_s16 headings[PLAYER_PHYSICS_WHEEL_COUNT];
	legacy_s16 contact_distances[PLAYER_PHYSICS_WHEEL_COUNT];
	legacy_s16 travel;
	legacy_s16 inverted_adjustment;
	legacy_u8 surface_adhesion;
	legacy_s16 car_index;
	struct VECTOR inverted_offset;
};

/* Hand a long-precision position back as the word-precision vector the rest
 * of the engine works with. */
static void physics_position_to_vector(struct VECTOR *destination, const struct VECTORLONG *source)
{
	destination->x = position_to_word(source->lx);
	destination->y = position_to_word(source->ly);
	destination->z = position_to_word(source->lz);
}

/* Wheel positions are long-precision; the forces acting on them arrive as
 * word-precision vectors, so every step shifts the position the same way. */
static void physics_position_offset(struct VECTORLONG *destination, const struct VECTORLONG *source,
									const struct VECTOR *offset)
{
	destination->lx = LEGACY_S32_WRAP_ADD_S16(source->lx, offset->x);
	destination->ly = LEGACY_S32_WRAP_ADD_S16(source->ly, offset->y);
	destination->lz = LEGACY_S32_WRAP_ADD_S16(source->lz, offset->z);
}

static void physics_position_pull_back(struct VECTORLONG *position, const struct VECTOR *offset)
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

static legacy_s16 scaled_vector_separation(struct VECTOR *first, struct VECTOR *second,
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

static void initialize_working_car_pose(struct CARSTATE *carstate)
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

static void prepare_wheel_motion(struct CARSTATE *carstate, struct SIMD *simd,
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

static void retain_collision_plane_rotation(struct MATRIX *rotation, legacy_s16 car_index)
{
	if (car_index == PLAYER_CAR_INDEX) {
		for (legacy_s16 i = 0; i < PLAYER_PHYSICS_WHEEL_COUNT; i++) {
			legacy_execution_residue.wheel_angle_stack_words[i] =
				rotation->vals[PLAYER_PHYSICS_RESIDUE_MATRIX_FIRST_VALUE + i];
		}
	}
}

static void scale_wheel_displacement(struct VECTOR *displacement, struct VECTORLONG *current,
									 struct VECTORLONG *previous, legacy_s16 retained_travel,
									 legacy_s16 total_travel)
{
	displacement->x =
		scale_position_delta(current->lx, previous->lx, retained_travel, total_travel);
	displacement->y =
		scale_position_delta(current->ly, previous->ly, retained_travel, total_travel);
	displacement->z =
		scale_position_delta(current->lz, previous->lz, retained_travel, total_travel);
}

static void reposition_wheels_at_wall(struct PLAYER_WHEEL_MOTION *motion,
									  legacy_s16 retained_travel, struct VECTOR *wall_response)
{
	struct VECTOR retained;
	for (legacy_s16 i = 0; i < PLAYER_PHYSICS_WHEEL_COUNT; i++) {
		retained.x = 0;
		retained.y = 0;
		retained.z = 0;
		if (retained_travel != 0) {
			scale_wheel_displacement(&retained, &motion->current[i], &motion->previous[i],
									 retained_travel, motion->travel);
		}
		physics_position_offset(&motion->current[i], &motion->previous[i], &retained);
		physics_position_offset(&motion->current[i], &motion->current[i], wall_response);
	}
}

static void apply_wall_impact(struct CARSTATE *carstate, legacy_s16 wall_heading,
							  legacy_s16 car_index)
{
	legacy_s16 angle_distance = LEGACY_S16_FROM_BITS(
		(legacy_u16)LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_NEGATE(car_working_yaw), wall_heading) &
		ANGLE_MASK);
	legacy_s16 reverse_turn = angle_distance > ANGLE_QUARTER_TURN;
	if (reverse_turn) {
		angle_distance = LEGACY_S16_WRAP_SUB(ANGLE_FULL_TURN, angle_distance);
	}
	legacy_u16 wall_safe_speed = LEGACY_U16_SHL(
		(legacy_u8)LEGACY_S16_WRAP_NEGATE(LEGACY_S16_WRAP_SUB(
			LEGACY_S16_SAR(
				LEGACY_S16_WRAP_MUL(angle_distance, PLAYER_PHYSICS_WALL_SPEED_ANGLE_MULTIPLIER),
				PLAYER_PHYSICS_WALL_SPEED_SHIFT),
			PLAYER_PHYSICS_WALL_SPEED_BIAS)),
		PLAYER_PHYSICS_WALL_SPEED_SHIFT);
	if (carstate->car_actual_speed > wall_safe_speed) {
		legacy_s16 impact_turn =
			reverse_turn ? LEGACY_S16_WRAP_NEGATE(angle_distance) : angle_distance;
		carstate->car_velocity_heading_offset = LEGACY_S16_SHL(impact_turn, 1U);
		player_motion_crash(CRASH_EVENT_COLLISION, car_index);
	}
	carstate->car_sound_flags |= PLAYER_PHYSICS_WALL_SOUND_FLAG;
}

static legacy_s16 wall_response_heading(struct VECTOR *push, legacy_s16 remaining_travel,
										legacy_s16 reversed)
{
	legacy_s16 relative_heading = LEGACY_S16_FROM_BITS(
		(legacy_u16)LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_NEGATE(car_working_yaw), wallOrientation) &
		ANGLE_MASK);
	push->y = 0;
	push->z = remaining_travel;
	legacy_s16 wall_heading;
	if (relative_heading < ANGLE_QUARTER_TURN || relative_heading > ANGLE_THREE_QUARTER_TURN) {
		wall_heading = wallOrientation;
		push->x = PLAYER_PHYSICS_WALL_PUSH_DISTANCE;
	} else {
		wall_heading = LEGACY_S16_FROM_BITS(
			(legacy_u16)LEGACY_S16_WRAP_ADD(wallOrientation, ANGLE_HALF_TURN) & ANGLE_MASK);
		push->x = -PLAYER_PHYSICS_WALL_PUSH_DISTANCE;
	}
	if (reversed) {
		push->x = LEGACY_S16_WRAP_NEGATE(push->x);
	}
	return wall_heading;
}

/* A wall response moves all four wheels, so the caller must restart the scan. */
static legacy_s16 resolve_wheel_wall_collision(struct CARSTATE *carstate,
											   struct PLAYER_WHEEL_MOTION *motion,
											   legacy_s16 wheel_index, legacy_s16 car_index)
{
	struct VECTORLONG *current_wheel_position = &motion->current[wheel_index];

	if (wallindex == PLAYER_PHYSICS_WALL_INDEX_NONE || nextPosAndNormalIP <= elRdWallRelated ||
		nextPosAndNormalIP >= wallHeight) {
		return 0;
	}
	struct VECTOR previous_wall = carstate->car_wheel_contact_positions[wheel_index];
	struct VECTOR current_wall;
	physics_position_to_vector(&current_wall, current_wheel_position);
	struct VECTOR previous_relative;
	previous_relative.x = LEGACY_S16_WRAP_SUB(previous_wall.x, wallStartX);
	previous_relative.y = 0;
	previous_relative.z = LEGACY_S16_WRAP_SUB(previous_wall.z, wallStartZ);
	struct VECTOR current_relative;
	current_relative.x = LEGACY_S16_WRAP_SUB(current_wall.x, wallStartX);
	current_relative.y = 0;
	current_relative.z = LEGACY_S16_WRAP_SUB(current_wall.z, wallStartZ);

	struct MATRIX collision_plane_rotation;
	mat_rot_y(&collision_plane_rotation,
			  LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_NEGATE(wallOrientation), ANGLE_QUARTER_TURN));
	retain_collision_plane_rotation(&collision_plane_rotation, car_index);
	struct VECTOR start;
	mat_mul_vector(&previous_relative, &collision_plane_rotation, &start);
	struct VECTOR end;
	mat_mul_vector(&current_relative, &collision_plane_rotation, &end);
	if ((end.z > 0 && start.z > 0) || (end.z < 0 && start.z < 0)) {
		return 0;
	}
	legacy_s16 reversed = end.z > start.z;
	struct VECTOR temporary;
	if (reversed) {
		temporary = end;
		end = start;
		start = temporary;
	}
	legacy_s16 remaining_travel;
	struct VECTOR intersection_delta;
	legacy_s16 retained_travel;
	if (end.z == 0) {
		retained_travel = motion->travel;
		remaining_travel = 0;
	} else if (start.z == 0) {
		retained_travel = 0;
		remaining_travel = motion->travel;
	} else {
		remaining_travel = scaled_vector_separation(&end, &start, &temporary, &intersection_delta);
		retained_travel = LEGACY_S16_WRAP_SUB(motion->travel, remaining_travel);
	}
	struct VECTOR push;
	legacy_s16 wall_heading = wall_response_heading(&push, remaining_travel, reversed);
	struct MATRIX *response_rotation = mat_rot_zxy(LEGACY_S16_WRAP_NEGATE(car_working_roll),
												   LEGACY_S16_WRAP_NEGATE(car_working_pitch),
												   wall_heading, MATRIX_ROTATION_ORDER_ZXY);
	struct VECTOR response;
	mat_mul_vector(&push, response_rotation, &response);
	apply_wall_impact(carstate, wall_heading, car_index);
	reposition_wheels_at_wall(motion, retained_travel, &response);
	return 1;
}

static void measure_wheel_plane_distance(struct VECTORLONG *position)
{
	struct VECTOR wheel;

	physics_position_to_vector(&wheel, position);
	nextPosAndNormalIP = player_motion_state->game_inputmode == GAME_INPUT_MODE_INTRO
							 ? wheel.y
							 : plane_signed_distance(planindex, wheel.x, wheel.y, wheel.z);
}

static void apply_wheel_gravity(struct CARSTATE *carstate, struct VECTORLONG *position,
								legacy_s16 wheel_index)
{
	legacy_s16 step_count =
		framespersec == GAME_FRAME_RATE_LOW && player_motion_fraction == 0 ? 2 : 1;
	for (legacy_s16 step = 0; step < step_count; step++) {
		carstate->car_wheel_vertical_speed[wheel_index] =
			LEGACY_S16_WRAP_ADD(carstate->car_wheel_vertical_speed[wheel_index],
								scale_player_motion(wheel_gravity_steps[wheel_index]));
		position->ly = LEGACY_S32_WRAP_SUB_S16(
			position->ly, scale_player_motion(carstate->car_wheel_vertical_speed[wheel_index]));
	}
	measure_wheel_plane_distance(position);
	if (nextPosAndNormalIP > PLAYER_PHYSICS_CONTACT_DISTANCE_LIMIT) {
		carstate->car_surfaceWhl[wheel_index] = CAR_WHEEL_CONTACT_NONE;
	}
}

/* Hold only an actual nearby driving surface with the tires facing it. The
 * ordinary track lookup supplies its finite footprint; an upside-down car in
 * open air selects distant ground and cannot acquire an attachment. */
static legacy_s16 hold_wheel_to_surface(struct CARSTATE *carstate,
										struct PLAYER_WHEEL_MOTION *motion, legacy_s16 wheel_index)
{
	const struct CAR_SURFACE_ADHESION_SETTINGS *settings =
		&car_surface_adhesion_settings[motion->car_index == OPPONENT_CAR_INDEX];
	if (!motion->surface_adhesion || carstate->car_crashBmpFlag != CRASH_EVENT_NONE ||
		player_motion_state->game_inputmode == GAME_INPUT_MODE_INTRO ||
		current_surf_type < CAR_SURFACE_PAVED || current_surf_type > CAR_SURFACE_GRASS ||
		nextPosAndNormalIP < 0 || nextPosAndNormalIP > settings->max_distance ||
		(settings->surface_mask & (1U << (current_surf_type - CAR_SURFACE_PAVED))) == 0) {
		return 0;
	}

	struct VECTOR up = {0, TRIG_FIXED_ONE, 0};
	struct VECTOR car_up;
	struct VECTOR surface_up;
	mat_mul_vector(&up, &car_to_world_rotation, &car_up);
	mat_mul_vector2(&up, &current_planptr->plane_rotation, &surface_up);
	legacy_s32 alignment = (legacy_s32)car_up.x * surface_up.x +
						   (legacy_s32)car_up.y * surface_up.y +
						   (legacy_s32)car_up.z * surface_up.z;
	if (alignment < (legacy_s32)TRIG_FIXED_ONE * settings->min_alignment) {
		/* Fixed-point rotations can shorten even identical up vectors. Keep
		 * exact co-directional alignment valid at a configured zero-degree limit. */
		if (alignment <= 0 ||
			(legacy_s32)car_up.x * surface_up.y != (legacy_s32)car_up.y * surface_up.x ||
			(legacy_s32)car_up.x * surface_up.z != (legacy_s32)car_up.z * surface_up.x ||
			(legacy_s32)car_up.y * surface_up.z != (legacy_s32)car_up.z * surface_up.y) {
			return 0;
		}
	}

	/* Resolve the configured contact distance along the surface normal, rather
	 * than applying world gravity and letting an inverted wheel separate. */
	struct VECTOR normal = {0, 0, 0};
	normal.y = -nextPosAndNormalIP * (1 << PLAYER_PHYSICS_POSITION_SCALE_SHIFT);
	struct VECTOR correction;
	mat_mul_vector2(&normal, &current_planptr->plane_rotation, &correction);
	physics_position_offset(&motion->current[wheel_index], &motion->current[wheel_index],
							&correction);
	nextPosAndNormalIP = 0;
	return 1;
}

static void move_airborne_wheel(struct CARSTATE *carstate, struct PLAYER_WHEEL_MOTION *motion,
								legacy_s16 wheel_index)
{
	if (nextPosAndNormalIP > 0) {
		if (motion->inverted_adjustment > 0 &&
			nextPosAndNormalIP < PLAYER_PHYSICS_INVERTED_CONTACT_DISTANCE_LIMIT) {
			physics_position_offset(&motion->current[wheel_index], &motion->current[wheel_index],
									&motion->inverted_offset);
		} else {
			apply_wheel_gravity(carstate, &motion->current[wheel_index], wheel_index);
		}
	}
}

static void transform_wheel_plane_crossing(struct PLAYER_WHEEL_MOTION *motion,
										   legacy_s16 wheel_index, legacy_s16 car_index,
										   struct VECTOR *start, struct VECTOR *end)
{
	struct VECTORLONG *current_wheel_position = &motion->current[wheel_index];
	struct VECTORLONG *previous_wheel_position = &motion->previous[wheel_index];

	struct PLANE far *contact_plane = current_planptr;
	struct VECTOR plane_world_origin;
	plane_world_origin.x = LEGACY_S16_WRAP_ADD(contact_plane->plane_origin.x, elem_xCenter);
	plane_world_origin.y = LEGACY_S16_WRAP_ADD(contact_plane->plane_origin.y, terrainHeight);
	plane_world_origin.z = LEGACY_S16_WRAP_ADD(contact_plane->plane_origin.z, elem_zCenter);

	struct VECTOR previous_relative;
	previous_relative.x =
		LEGACY_S16_WRAP_SUB(position_to_word(previous_wheel_position->lx), plane_world_origin.x);
	previous_relative.y =
		LEGACY_S16_WRAP_SUB(position_to_word(previous_wheel_position->ly), plane_world_origin.y);
	previous_relative.z =
		LEGACY_S16_WRAP_SUB(position_to_word(previous_wheel_position->lz), plane_world_origin.z);

	struct VECTOR current_relative;
	current_relative.x =
		LEGACY_S16_WRAP_SUB(position_to_word(current_wheel_position->lx), plane_world_origin.x);
	current_relative.y =
		LEGACY_S16_WRAP_SUB(position_to_word(current_wheel_position->ly), plane_world_origin.y);
	current_relative.z =
		LEGACY_S16_WRAP_SUB(position_to_word(current_wheel_position->lz), plane_world_origin.z);

	struct MATRIX collision_plane_rotation = contact_plane->plane_rotation;
	retain_collision_plane_rotation(&collision_plane_rotation, car_index);
	struct MATRIX inverse_rotation;
	mat_invert(&collision_plane_rotation, &inverse_rotation);
	mat_mul_vector(&previous_relative, &inverse_rotation, start);

	mat_mul_vector(&current_relative, &inverse_rotation, end);
}

static void prepare_wheel_plane_travel(struct PLAYER_WHEEL_MOTION *motion, legacy_s16 wheel_index,
									   legacy_s16 distance)
{
	wheel_forward_travel.x = 0;
	wheel_forward_travel.y = 0;
	wheel_forward_travel.z = distance;
	planindex_copy = planindex;
	wheel_heading_offset = motion->headings[wheel_index];
	transform_wheel_travel_to_world();
}

/* Express height as negative Z for the existing Z-plane intersection helper. */
static void turn_plane_height_into_depth(struct VECTOR *position)
{
	legacy_s16 depth = position->z;

	position->z = LEGACY_S16_WRAP_NEGATE(position->y);
	position->y = depth;
}

static void split_wheel_plane_travel(struct CARSTATE *carstate, struct PLAYER_WHEEL_MOTION *motion,
									 legacy_s16 wheel_index, struct VECTOR *start,
									 struct VECTOR *end)
{
	turn_plane_height_into_depth(start);
	turn_plane_height_into_depth(end);
	struct VECTOR delta;
	struct VECTOR intersection;
	legacy_s16 remaining_travel = scaled_vector_separation(end, start, &intersection, &delta);
	legacy_s16 total_travel = LEGACY_S16_WRAP_ADD(
		scale_player_motion(carstate->car_wheel_vertical_speed[wheel_index]), motion->travel);
	legacy_s16 retained_travel = LEGACY_S16_WRAP_SUB(total_travel, remaining_travel);
	struct VECTOR retained;
	scale_wheel_displacement(&retained, &motion->current[wheel_index],
							 &motion->previous[wheel_index], retained_travel, total_travel);
	prepare_wheel_plane_travel(motion, wheel_index, remaining_travel);
	physics_position_offset(&motion->current[wheel_index], &motion->previous[wheel_index],
							&retained);
	physics_position_offset(&motion->current[wheel_index], &motion->current[wheel_index],
							&wheel_world_travel);
}

static void push_wheel_out_of_plane(struct VECTORLONG *position, legacy_s16 reverse_response)
{
	/* Unlike the intro's initial lookup, correction always measures the plane. */
	struct VECTOR wheel;
	physics_position_to_vector(&wheel, position);
	nextPosAndNormalIP = plane_signed_distance(planindex, wheel.x, wheel.y, wheel.z);
	struct VECTOR correction;
	if (nextPosAndNormalIP < 0) {
		if (reverse_response) {
			nextPosAndNormalIP = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_NEGATE(nextPosAndNormalIP),
													 PLAYER_PHYSICS_PLANE_PENETRATION_BIAS);
		}
		wheel.x = 0;
		wheel.y = LEGACY_S16_SHL(LEGACY_S16_WRAP_NEGATE(nextPosAndNormalIP),
								 PLAYER_PHYSICS_POSITION_SCALE_SHIFT);
		wheel.z = 0;
		mat_mul_vector2(&wheel, &current_planptr->plane_rotation, &correction);
		physics_position_offset(position, position, &correction);
	}
}

static void use_ground_plane_for_wheel(struct VECTORLONG *position)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		struct VECTOR wheel;
		physics_position_to_vector(&wheel, position);
		build_extended_track_terrain(&wheel);
		nextPosAndNormalIP = plane_signed_distance(planindex, wheel.x, wheel.y, wheel.z);
		return;
	}
#endif
	planindex = PLAYER_PHYSICS_GROUND_PLANE_INDEX;
	current_planptr = planptr;
	track_wall_collision_enabled = 1;
	struct VECTOR wheel;
	physics_position_to_vector(&wheel, position);
	nextPosAndNormalIP =
		plane_signed_distance(PLAYER_PHYSICS_GROUND_PLANE_INDEX, wheel.x, wheel.y, wheel.z);
}

/* Return false when the wheel is below an elevated plane and needs to be
 * tested against the ground instead. Ground correction cannot take this path. */
static legacy_s16 correct_wheel_plane_penetration(struct CARSTATE *carstate,
												  struct PLAYER_WHEEL_MOTION *motion,
												  legacy_s16 wheel_index, legacy_s16 car_index)
{
	struct VECTOR end;
	struct VECTOR start;
	transform_wheel_plane_crossing(motion, wheel_index, car_index, &start, &end);
	legacy_s16 reverse_response = 0;
	if (track_wall_collision_enabled == 0 && start.y < -PLAYER_PHYSICS_CONTACT_DISTANCE_LIMIT &&
		end.y < -PLAYER_PHYSICS_CONTACT_DISTANCE_LIMIT) {
		if (end.y <= -PLAYER_PHYSICS_INVERTED_CONTACT_DISTANCE_LIMIT) {
			use_ground_plane_for_wheel(&motion->current[wheel_index]);
			return 0;
		}
		player_motion_crash(CRASH_EVENT_IMMEDIATE_STOP, car_index);
		reverse_response = 1;
	}
	if (end.y == 0) {
		prepare_wheel_plane_travel(motion, wheel_index, PLAYER_PHYSICS_PLANE_RETRACE_DISTANCE);
		physics_position_pull_back(&motion->current[wheel_index], &wheel_world_travel);
	} else {
		if (start.y > 0 && end.y < 0) {
			split_wheel_plane_travel(carstate, motion, wheel_index, &start, &end);
		} else {
			prepare_wheel_plane_travel(motion, wheel_index, motion->travel);
			physics_position_offset(&motion->current[wheel_index], &motion->previous[wheel_index],
									&wheel_world_travel);
		}
		push_wheel_out_of_plane(&motion->current[wheel_index], reverse_response);
	}
	return 1;
}

static void apply_wheel_landing(struct CARSTATE *carstate, legacy_s16 wheel_index,
								legacy_s16 car_index)
{
	if (carstate->car_wheel_vertical_speed[wheel_index] >
		PLAYER_PHYSICS_SUSPENSION_SOUND_THRESHOLD) {
		carstate->car_sound_flags |= PLAYER_PHYSICS_SUSPENSION_SOUND_FLAG;
	}
	if (carstate->car_wheel_vertical_speed[wheel_index] >
		PLAYER_PHYSICS_SUSPENSION_CRASH_THRESHOLD) {
		player_motion_crash(CRASH_EVENT_COLLISION, car_index);
	}
	carstate->car_wheel_vertical_speed[wheel_index] = 0;
}

static void stop_wheels_at_contact(struct PLAYER_WHEEL_MOTION *motion, legacy_s16 fraction,
								   legacy_s16 car_index)
{
	/* Stop all four wheels at the first body contact, before resolving their
	 * remaining ground contacts. Do not finish the tick beyond the obstacle. */
	for (legacy_s16 i = 0; i < PLAYER_PHYSICS_WHEEL_COUNT; i++) {
		struct VECTOR offset;
		scale_wheel_displacement(&offset, &motion->current[i], &motion->previous[i], fraction,
								 TRIG_FIXED_ONE);
		physics_position_offset(&motion->current[i], &motion->previous[i], &offset);
	}
	motion->travel = (legacy_s16)((legacy_s32)motion->travel * fraction / TRIG_FIXED_ONE);
	player_motion_crash(CRASH_EVENT_IMMEDIATE_STOP, car_index);
}

static legacy_s16 stop_at_track_underside(struct CARSTATE *carstate,
										  struct PLAYER_WHEEL_MOTION *motion,
										  legacy_s16 wheel_index, legacy_s16 car_index)
{
	if (player_motion_state->game_inputmode == GAME_INPUT_MODE_INTRO ||
		carstate->car_crashBmpFlag != CRASH_EVENT_NONE) {
		return 0;
	}
	struct VECTOR previous;
	struct VECTOR current;
	physics_position_to_vector(&previous, &motion->previous[wheel_index]);
	physics_position_to_vector(&current, &motion->current[wheel_index]);
	legacy_s16 fraction;
	if (!sweep_track_underside(&previous, &current, &fraction)) {
		return 0;
	}

	stop_wheels_at_contact(motion, fraction, car_index);
	return 1;
}

static legacy_s16 stop_at_track_wall_contact(struct CARSTATE *carstate,
											 struct PLAYER_WHEEL_MOTION *motion,
											 legacy_s16 car_index)
{
	if (player_motion_state->game_inputmode == GAME_INPUT_MODE_INTRO ||
		carstate->car_crashBmpFlag != CRASH_EVENT_NONE) {
		return 0;
	}
	legacy_s16 first_contact = TRIG_FIXED_ONE;
	legacy_s16 hit = 0;
	for (legacy_s16 i = 0; i < PLAYER_PHYSICS_WHEEL_COUNT; i++) {
		legacy_s16 next = (i + 1) % PLAYER_PHYSICS_WHEEL_COUNT;
		legacy_s16 fraction;
		if (sweep_track_solid_obstacle(&motion->previous[i], &motion->current[i], &fraction)) {
			if (fraction < first_contact) {
				first_contact = fraction;
			}
			hit = 1;
		}
		if (sweep_track_wall_span(&motion->previous[i], &motion->previous[next],
								  &motion->current[i], &motion->current[next], &fraction)) {
			if (fraction < first_contact) {
				first_contact = fraction;
			}
			hit = 1;
		}
	}
	if (hit) {
		stop_wheels_at_contact(motion, first_contact, car_index);
	}
	return hit;
}

/* A saved pose can retain a small plane residual after the original suspension
 * update. Relax that inherited residual over the original tick, while resolving
 * any penetration introduced by this short step immediately. */
static legacy_s16 inherited_wheel_plane_distance(struct VECTORLONG *position)
{
	if (player_motion_fraction == 0) {
		return 0;
	}
	struct VECTOR previous;
	physics_position_to_vector(&previous, position);
	legacy_s16 distance = plane_signed_distance(planindex, previous.x, previous.y, previous.z);
	if (distance >= 0 || player_motion_state->game_inputmode == GAME_INPUT_MODE_INTRO) {
		return distance;
	}
	/* A new ramp or ledge is a new contact, even when the old wheel lies below
	 * its infinite plane. Only retain an error on the same finite surface. */
	struct TRACK_COLLISION_SNAPSHOT selected;
	track_collision_capture(&selected);
	build_track_object(&previous, &previous);
	legacy_s16 same_surface =
		planindex == selected.plane_index && current_planptr == selected.plane &&
		terrainHeight == selected.terrain_height && elem_xCenter == selected.element_x &&
		elem_zCenter == selected.element_z;
	track_collision_restore(&selected);
	return same_surface ? distance : 0;
}

static legacy_s16 retained_wheel_plane_residual(legacy_s16 previous, legacy_s16 current,
												legacy_u32 fraction)
{
	if (previous >= 0 || current >= 0) {
		return 0;
	}
	legacy_s16 residual = previous - scale_player_fraction(previous, fraction);
	/* Moving out of an inherited overlap must never push the wheel back in. */
	return residual < current ? current : residual;
}

static void relax_wheel_plane_residual(struct PLAYER_WHEEL_MOTION *motion, legacy_s16 wheel_index,
									   legacy_s16 initial_distance)
{
	legacy_s16 distance = motion->contact_distances[wheel_index];
	legacy_u32 fraction = player_tick_fraction();
	legacy_s16 residual = retained_wheel_plane_residual(initial_distance, distance, fraction);
	if (residual != 0) {
		struct VECTOR normal;
		normal.x = 0;
		normal.y = LEGACY_S16_SHL(residual, PLAYER_PHYSICS_POSITION_SCALE_SHIFT);
		normal.z = 0;
		struct VECTOR offset;
		mat_mul_vector2(&normal, &current_planptr->plane_rotation, &offset);
		physics_position_offset(&motion->current[wheel_index], &motion->current[wheel_index],
								&offset);
		motion->contact_distances[wheel_index] = distance - residual;
	} else if (distance > 0) {
		/* Spring extension is a rate. A newly crossed contact still receives
		 * the complete negative contact distance and collision correction. */
		motion->contact_distances[wheel_index] = scale_player_fraction(distance, fraction);
	}
}

static legacy_s16 resolve_wheel_plane_contact(struct CARSTATE *carstate,
											  struct PLAYER_WHEEL_MOTION *motion,
											  legacy_s16 wheel_index, legacy_s16 car_index)
{
	legacy_s16 resolved;

	do {
		legacy_s16 initial_distance =
			inherited_wheel_plane_distance(&motion->previous[wheel_index]);
		if (!hold_wheel_to_surface(carstate, motion, wheel_index)) {
			move_airborne_wheel(carstate, motion, wheel_index);
		}
		if (stop_at_track_underside(carstate, motion, wheel_index, car_index)) {
			return 0;
		}
		/* Suspension uses the distance before penetration correction. */
		motion->contact_distances[wheel_index] = nextPosAndNormalIP;
		resolved = nextPosAndNormalIP >= 0 ||
				   correct_wheel_plane_penetration(carstate, motion, wheel_index, car_index);
		if (player_motion_fraction != 0 && resolved) {
			relax_wheel_plane_residual(motion, wheel_index, initial_distance);
		}
	} while (!resolved);

	if (motion->contact_distances[wheel_index] <= 0) {
		apply_wheel_landing(carstate, wheel_index, car_index);
	}
	return 1;
}

static void look_up_wheel_surface(struct CARSTATE *carstate, struct PLAYER_WHEEL_MOTION *motion,
								  legacy_s16 wheel_index)
{
	struct VECTOR wheel;

	physics_position_to_vector(&wheel, &motion->current[wheel_index]);
	if (player_motion_state->game_inputmode == GAME_INPUT_MODE_INTRO) {
		wallindex = PLAYER_PHYSICS_WALL_INDEX_NONE;
		current_surf_type = CAR_SURFACE_PAVED;
		planindex = PLAYER_PHYSICS_GROUND_PLANE_INDEX;
		current_planptr = planptr;
	} else {
		build_track_object(&wheel, &carstate->car_wheel_contact_positions[wheel_index]);
	}
	carstate->car_surfaceWhl[wheel_index] = current_surf_type;
	measure_wheel_plane_distance(&motion->current[wheel_index]);
}

/* Stop at the first wall response: its displacement invalidates the whole scan. */
static legacy_s16 resolve_wheel_contact_pass(struct CARSTATE *carstate,
											 struct PLAYER_WHEEL_MOTION *motion,
											 legacy_s16 car_index)
{
	for (legacy_s16 i = 0; i < PLAYER_PHYSICS_WHEEL_COUNT; i++) {
		look_up_wheel_surface(carstate, motion, i);
		if (resolve_wheel_wall_collision(carstate, motion, i, car_index)) {
			return 0;
		}
		if (!resolve_wheel_plane_contact(carstate, motion, i, car_index)) {
			return 0;
		}
	}
	/* Preserve ordinary wall sliding first; then catch walls between wheel
	 * paths and thin solids crossed entirely within this tick. */
	return !stop_at_track_wall_contact(carstate, motion, car_index);
}

static void resolve_wheel_contacts(struct CARSTATE *carstate, struct PLAYER_WHEEL_MOTION *motion,
								   legacy_s16 car_index)
{
	/* Four scans are allowed; the fifth attempt crashes without scanning. */
	legacy_s16 pass = 1;
	legacy_s16 resolved = 0;
	while (pass < PLAYER_PHYSICS_COLLISION_RETRY_LIMIT && !resolved) {
		resolved = resolve_wheel_contact_pass(carstate, motion, car_index);
		pass++;
	}
	if (!resolved) {
		carstate->car_velocity_heading_offset = ANGLE_HALF_TURN;
		player_motion_crash(CRASH_EVENT_COLLISION, car_index);
	}
	if (carstate->car_surfaceWhl[PLAYER_PHYSICS_FRONT_WHEEL_FIRST] == CAR_SURFACE_WATER &&
		carstate->car_surfaceWhl[PLAYER_PHYSICS_FRONT_WHEEL_SECOND] == CAR_SURFACE_WATER &&
		carstate->car_surfaceWhl[PLAYER_PHYSICS_REAR_WHEEL_FIRST] == CAR_SURFACE_WATER &&
		carstate->car_surfaceWhl[PLAYER_PHYSICS_REAR_WHEEL_SECOND] == CAR_SURFACE_WATER) {
		player_motion_crash(CRASH_EVENT_WATER, car_index);
	}
}

static void apply_wheel_suspension(struct CARSTATE *carstate, struct PLAYER_WHEEL_MOTION *motion)
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

static legacy_s32 clamp_car_world_coordinate(legacy_s32 position, legacy_s16 dimension)
{
	/* Preserve the original strict upper-bound comparison. */
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		legacy_s32 maximum = (legacy_s32)dimension * 65536L - 3840L;
		if (position > maximum) {
			return maximum - 1L;
		}
		if (position < PLAYER_PHYSICS_WORLD_MIN_POSITION) {
			return PLAYER_PHYSICS_WORLD_MIN_POSITION;
		}
		return position;
	}
#else
	(void)dimension;
#endif
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
	car_working_x = clamp_car_world_coordinate(car_working_x,
#ifdef RESTUNTS_SDL3
											   extended_track_width()
#else
											   30
#endif
	);
	car_working_z = clamp_car_world_coordinate(car_working_z,
#ifdef RESTUNTS_SDL3
											   extended_track_height()
#else
											   30
#endif
	);
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

static void derive_car_pose_from_wheels(struct PLAYER_WHEEL_MOTION *motion)
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

static void sum_front_and_rear_wheel_surfaces(struct CARSTATE *carstate)
{
	carstate->car_sumSurfFrontWheels =
		LEGACY_S8_WRAP_ADD(carstate->car_surfaceWhl[PLAYER_PHYSICS_FRONT_WHEEL_FIRST],
						   carstate->car_surfaceWhl[PLAYER_PHYSICS_FRONT_WHEEL_SECOND]);
	carstate->car_sumSurfRearWheels =
		LEGACY_S8_WRAP_ADD(carstate->car_surfaceWhl[PLAYER_PHYSICS_REAR_WHEEL_FIRST],
						   carstate->car_surfaceWhl[PLAYER_PHYSICS_REAR_WHEEL_SECOND]);
}

static void play_car_impact_sounds(struct CARSTATE *carstate, legacy_s16 car_index)
{
#ifndef RESTUNTS_HEADLESS
	if (player_motion_fraction == 0 && is_in_replay == 0) {
		audio_play_car_events(carstate->car_sound_flags, car_index == PLAYER_CAR_INDEX
															 ? audio_player_engine_channel
															 : audio_opponent_engine_channel);
	}
#else
	(void)carstate;
	(void)car_index;
#endif
}

static void check_body_corner_plane(struct CARSTATE *carstate, struct VECTOR *current_position,
									legacy_s16 corner_index, legacy_s16 car_index)
{
	struct VECTOR sample = *current_position;
	build_track_object(&sample, &carstate->car_body_corner_positions[corner_index]);
	legacy_s16 current_distance = plane_signed_distance(planindex, sample.x, sample.y, sample.z);
	int height_only = planindex < PHYSICS_HEIGHT_ONLY_PLANE_COUNT;
#ifdef RESTUNTS_SDL3
	/* A translated or tilted ground plane retains the original solid-ground body
	 * test. Initial wheel contacts can start just below the two-unit bias. */
	if (extended_track_active() && (planindex == EXTENDED_GEOMETRY_PLANE_INDEX ||
									planindex == EXTENDED_GEOMETRY_PLANE_INDEX + 1 ||
									planindex == EXTENDED_GEOMETRY_PLANE_INDEX + 2 ||
									planindex == EXTENDED_GEOMETRY_PLANE_INDEX + 3)) {
		height_only = 1;
	}
#endif
	if (height_only) {
		if (current_distance <= 0) {
			player_motion_crash(CRASH_EVENT_IMMEDIATE_STOP, car_index);
		}
	} else {
		legacy_s16 current_plane_index = planindex;
		sample = carstate->car_body_corner_positions[corner_index];
		build_track_object(&sample, current_position);
		if (current_plane_index == planindex) {
			legacy_s16 previous_distance =
				plane_signed_distance(planindex, sample.x, sample.y, sample.z);
			if (game_replay_mode != REPLAY_MODE_PAUSED &&
				((current_distance < 0 && previous_distance > 0) ||
				 (current_distance > 0 && previous_distance < 0)) &&
				body_plane_crossing_is_collision(&carstate->car_body_corner_positions[corner_index],
												 current_position)) {
				player_motion_crash(CRASH_EVENT_IMMEDIATE_STOP, car_index);
			}
		}
	}
	carstate->car_body_corner_positions[corner_index] = *current_position;
}

static void check_car_body_planes(struct CARSTATE *carstate, struct SIMD *simd,
								  legacy_s16 car_index)
{
	struct MATRIX *rotation = mat_rot_zxy(
		LEGACY_S16_WRAP_NEGATE(car_working_roll), LEGACY_S16_WRAP_NEGATE(car_working_pitch),
		LEGACY_S16_WRAP_NEGATE(car_working_yaw), MATRIX_ROTATION_ORDER_ZXY);
	struct VECTOR position;
	struct VECTOR local;
	struct VECTOR rotated;
	for (legacy_s16 i = 0; i < PLAYER_PHYSICS_WHEEL_COUNT; i++) {
		local = simd->wheel_coords[i];
		local.y = LEGACY_S16_SHL(simd->collide_points[0].py, PLAYER_PHYSICS_POSITION_SCALE_SHIFT);
		mat_mul_vector(&local, rotation, &rotated);
		position.x = position_to_word(LEGACY_S32_WRAP_ADD_S16(car_working_x, rotated.x));
		position.y = position_to_word(LEGACY_S32_WRAP_ADD_S16(car_working_y, rotated.y));
		position.z = position_to_word(LEGACY_S32_WRAP_ADD_S16(car_working_z, rotated.z));
		check_body_corner_plane(carstate, &position, i, car_index);
	}
}

static void update_car_surface_contact(struct CARSTATE *carstate, legacy_s16 car_index)
{
	legacy_s8 surface_sum =
		LEGACY_S8_WRAP_ADD(carstate->car_sumSurfFrontWheels, carstate->car_sumSurfRearWheels);

	if (car_index != OPPONENT_CAR_INDEX && surface_sum == 0 &&
		carstate->car_sumSurfAllWheels != CAR_WHEEL_CONTACT_NONE) {
		player_motion_state->game_jumpCount =
			LEGACY_S16_WRAP_ADD(player_motion_state->game_jumpCount, 1);
	}
	carstate->car_sumSurfAllWheels = surface_sum;
}

static void prepare_car_collision_pose(struct VECTOR *pose)
{
	pose[PLAYER_PHYSICS_POSE_POSITION_INDEX].x = position_to_word(car_working_x);
	pose[PLAYER_PHYSICS_POSE_POSITION_INDEX].y = position_to_word(car_working_y);
	pose[PLAYER_PHYSICS_POSE_POSITION_INDEX].z = position_to_word(car_working_z);
	/* Collision routines take roll, pitch, yaw, unlike CARSTATE's rotation. */
	pose[PLAYER_PHYSICS_POSE_ROTATION_INDEX].x = car_working_roll;
	pose[PLAYER_PHYSICS_POSE_ROTATION_INDEX].y = car_working_pitch;
	pose[PLAYER_PHYSICS_POSE_ROTATION_INDEX].z = car_working_yaw;
}

/* Any overlap prevents the working pose from being committed, even when a
 * collision is already latched or the speed response does not cause a crash. */
static legacy_s16 handle_other_car_collision(struct CARSTATE *carstate, struct SIMD *simd,
											 struct CARSTATE *other_carstate,
											 struct SIMD *other_simd, struct VECTOR *car_pose,
											 legacy_s16 car_index)
{
	if (gameconfig.game_opponenttype == 0) {
		return 0;
	}
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		legacy_s32 dx = car_working_x - other_carstate->car_position.lx;
		legacy_s32 dz = car_working_z - other_carstate->car_position.lz;
		if (dx < -262144L || dx > 262144L || dz < -262144L || dz > 262144L) {
			return 0;
		}
	}
#endif
	struct VECTOR other_pose[PLAYER_PHYSICS_POSE_VECTOR_COUNT];
	physics_position_to_vector(&other_pose[PLAYER_PHYSICS_POSE_POSITION_INDEX],
							   &other_carstate->car_position);
	other_pose[PLAYER_PHYSICS_POSE_ROTATION_INDEX].x = other_carstate->car_rotate.z;
	other_pose[PLAYER_PHYSICS_POSE_ROTATION_INDEX].y = other_carstate->car_rotate.y;
	other_pose[PLAYER_PHYSICS_POSE_ROTATION_INDEX].z = other_carstate->car_rotate.x;
	if (car_collision_boxes_overlap(simd->collide_points, car_pose, other_simd->collide_points,
									other_pose) == 0) {
		return 0;
	}
	if (carstate->car_collision_latch == CAR_COLLISION_LATCH_CLEAR &&
		resolve_car_collision_speeds(carstate, other_carstate) != 0) {
		player_motion_crash(CRASH_EVENT_COLLISION, car_index);
		player_motion_crash(CRASH_EVENT_COLLISION,
							car_index == PLAYER_CAR_INDEX ? OPPONENT_CAR_INDEX : PLAYER_CAR_INDEX);
	}
	return 1;
}

static legacy_s16 handle_auxiliary_obstacles(struct CARSTATE *carstate, struct SIMD *simd,
											 struct VECTOR *car_pose, struct VECTOR *obstacle_pose,
											 legacy_s16 column, legacy_s16 row,
											 legacy_s16 car_index)
{
	struct VECTOR positions[PLAYER_PHYSICS_COLLISION_POINT_CAPACITY];
	legacy_s8 obstacle_count = get_track_collision_points(column, row, positions);
	for (legacy_s16 i = 0; i < obstacle_count; i++) {
		obstacle_pose[PLAYER_PHYSICS_POSE_POSITION_INDEX] = positions[i];
		if (car_collision_boxes_overlap(simd->collide_points, car_pose,
										track_auxiliary_obstacle_bounds, obstacle_pose) != 0) {
			carstate->car_velocity_heading_offset =
				LEGACY_S16_WRAP_SUB(carstate->car_velocity_heading_offset, ANGLE_HALF_TURN);
			player_motion_crash(CRASH_EVENT_COLLISION, car_index);
			return 1;
		}
	}
	return 0;
}

static void handle_breakable_object(struct CARSTATE *carstate, struct SIMD *simd,
									struct VECTOR *car_pose, struct VECTOR *obstacle_pose,
									legacy_s16 column, legacy_s16 row)
{
	legacy_s16 object_index = (legacy_s8)roadside_sign_indices_by_tile[trackrows[row] + column];

	if (object_index == PLAYER_PHYSICS_ROADSIDE_SIGN_INDEX_NONE ||
		player_motion_state->game_object_destroyed[object_index] != 0) {
		return;
	}
	obstacle_pose[PLAYER_PHYSICS_POSE_POSITION_INDEX] = roadside_sign_positions[object_index];
	if (car_collision_boxes_overlap(simd->collide_points, car_pose, breakable_object_bounds,
									obstacle_pose) != 0) {
		player_motion_state->game_object_destroyed[object_index] = 1;
		if (player_motion_fraction == 0) {
			emit_crash_particles(
				LEGACY_S16_WRAP_ADD(object_index, PLAYER_PHYSICS_OBJECT_PARTICLE_KIND_OFFSET),
				LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.x),
				scale_speed_to_travel(carstate->car_actual_speed,
									  PLAYER_PHYSICS_NORMAL_RATE_TRAVEL_DIVISOR));
		}
	}
}

static legacy_s16 overlaps_start_finish_pole(struct SIMD *simd, struct VECTOR *car_pose,
											 struct VECTOR *obstacle_pose, legacy_s16 angle)
{
	obstacle_pose[PLAYER_PHYSICS_POSE_POSITION_INDEX].x = LEGACY_S16_WRAP_ADD(
		track_column_centers[start_finish_column],
		multiply_and_scale(sin_fast(angle), PLAYER_PHYSICS_START_FINISH_POLE_OFFSET));
	obstacle_pose[PLAYER_PHYSICS_POSE_POSITION_INDEX].z = LEGACY_S16_WRAP_ADD(
		track_row_centers[start_finish_row],
		multiply_and_scale(cos_fast(angle), PLAYER_PHYSICS_START_FINISH_POLE_OFFSET));
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		const struct CSTRK_OBJECT *object =
			extended_track_object_at(start_finish_column, start_finish_row);
		if (object) {
			struct VECTOR origin;
			extended_geometry_object_origin(extended_track_model(), object, &origin);
			obstacle_pose[PLAYER_PHYSICS_POSE_POSITION_INDEX] = (struct VECTOR){
				LEGACY_S16_WRAP_ADD(
					origin.x,
					multiply_and_scale(sin_fast(angle), PLAYER_PHYSICS_START_FINISH_POLE_OFFSET)),
				origin.y,
				LEGACY_S16_WRAP_ADD(
					origin.z,
					multiply_and_scale(cos_fast(angle), PLAYER_PHYSICS_START_FINISH_POLE_OFFSET))};
			obstacle_pose[PLAYER_PHYSICS_POSE_ROTATION_INDEX] = (struct VECTOR){0, 0, 0};
		}
	}
#endif
	return car_collision_boxes_overlap(simd->collide_points, car_pose, start_finish_pole_bounds,
									   obstacle_pose) != 0;
}

static legacy_s16 handle_start_finish_poles(struct SIMD *simd, struct VECTOR *car_pose,
											struct VECTOR *obstacle_pose, legacy_s16 column,
											legacy_s16 row, legacy_s16 car_index)
{
	if (column != start_finish_column || row != start_finish_row) {
		return 0;
	}
	obstacle_pose[PLAYER_PHYSICS_POSE_POSITION_INDEX].y = hillHeightConsts[hillFlag];
	if (overlaps_start_finish_pole(simd, car_pose, obstacle_pose,
								   LEGACY_S16_WRAP_ADD(track_angle, ANGLE_QUARTER_TURN)) ||
		overlaps_start_finish_pole(simd, car_pose, obstacle_pose,
								   LEGACY_S16_WRAP_ADD(track_angle, ANGLE_THREE_QUARTER_TURN))) {
		player_motion_crash(CRASH_EVENT_COLLISION, car_index);
		return 1;
	}
	return 0;
}

static legacy_s16 handle_scenery_collisions(struct CARSTATE *carstate, struct SIMD *simd,
											struct VECTOR *car_pose, legacy_s16 car_index)
{
	legacy_s16 column = LEGACY_S16_SAR(car_pose[PLAYER_PHYSICS_POSE_POSITION_INDEX].x,
									   PLAYER_PHYSICS_TRACK_COORDINATE_SHIFT);
	legacy_s16 row = LEGACY_S16_WRAP_NEGATE(
		LEGACY_S16_WRAP_SUB(LEGACY_S16_SAR(car_pose[PLAYER_PHYSICS_POSE_POSITION_INDEX].z,
										   PLAYER_PHYSICS_TRACK_COORDINATE_SHIFT),
							PLAYER_PHYSICS_TRACK_GRID_LAST_COORDINATE));
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		legacy_s32 world_x, world_z;
		extended_geometry_world_position(&car_pose[PLAYER_PHYSICS_POSE_POSITION_INDEX], &world_x,
										 &world_z);
		column = (legacy_s16)(world_x / 1024);
		row = (legacy_s16)(extended_track_height() - 1 - world_z / 1024);
	}
#endif
	struct VECTOR obstacle_pose[PLAYER_PHYSICS_POSE_VECTOR_COUNT];
	obstacle_pose[PLAYER_PHYSICS_POSE_ROTATION_INDEX].x = 0;
	obstacle_pose[PLAYER_PHYSICS_POSE_ROTATION_INDEX].y = 0;
	obstacle_pose[PLAYER_PHYSICS_POSE_ROTATION_INDEX].z = 0;
	if (column < 0 ||
		column >=
#ifdef RESTUNTS_SDL3
			extended_track_width()
#else
			PLAYER_PHYSICS_TRACK_GRID_SIZE
#endif
		|| row < 0 ||
		row >=
#ifdef RESTUNTS_SDL3
			extended_track_height()
#else
			PLAYER_PHYSICS_TRACK_GRID_SIZE
#endif
	) {
		return 0;
	}
	if (handle_auxiliary_obstacles(carstate, simd, car_pose, obstacle_pose, column, row,
								   car_index)) {
		return 1;
	}
	handle_breakable_object(carstate, simd, car_pose, obstacle_pose, column, row);
	return handle_start_finish_poles(simd, car_pose, obstacle_pose, column, row, car_index);
}

static void commit_working_car_pose(struct CARSTATE *carstate)
{
	carstate->car_position.lx = car_working_x;
	carstate->car_position.ly = car_working_y;
	carstate->car_position.lz = car_working_z;
	carstate->car_rotate.z = car_working_roll;
	carstate->car_rotate.y = car_working_pitch;
	carstate->car_rotate.x = car_working_yaw;
	carstate->car_collision_latch = CAR_COLLISION_LATCH_CLEAR;
}

void update_player_state(struct CARSTATE *carstate, struct SIMD *simd,
						 struct CARSTATE *other_carstate, struct SIMD *other_simd,
						 legacy_s16 car_index)
{
	initialize_working_car_pose(carstate);
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		extended_geometry_set_context(carstate->car_position.lx, carstate->car_position.lz);
	}
#endif
	struct PLAYER_WHEEL_MOTION motion;
	prepare_wheel_motion(carstate, simd, &motion, car_index);
	resolve_wheel_contacts(carstate, &motion, car_index);
	apply_wheel_suspension(carstate, &motion);
	derive_car_pose_from_wheels(&motion);
	sum_front_and_rear_wheel_surfaces(carstate);

	struct VECTOR car_pose[PLAYER_PHYSICS_POSE_VECTOR_COUNT];
	if (player_motion_state->game_inputmode != GAME_INPUT_MODE_INTRO) {
		play_car_impact_sounds(carstate, car_index);
		check_car_body_planes(carstate, simd, car_index);
		update_car_surface_contact(carstate, car_index);
		prepare_car_collision_pose(car_pose);
		if (handle_other_car_collision(carstate, simd, other_carstate, other_simd, car_pose,
									   car_index) ||
			handle_scenery_collisions(carstate, simd, car_pose, car_index)) {
			return;
		}
	}
	commit_working_car_pose(carstate);
	if (player_motion_fraction == 0) {
		owoot_update_player(carstate, car_index);
	}
}

void update_player_state_fraction(struct GAMESTATE *branch, legacy_s16 car_index,
								  legacy_u32 fraction20)
{
	if (fraction20 == 0) {
		return;
	}
	legacy_u32 saved_fraction = player_motion_fraction;
	struct GAMESTATE *saved_state = player_motion_state;
	player_motion_fraction = fraction20 > PHANTOM_PHYSICS_ONE ? PHANTOM_PHYSICS_ONE : fraction20;
	player_motion_state = branch;
	if (car_index == PLAYER_CAR_INDEX) {
		update_player_state(&branch->playerstate, &simd_player, &branch->opponentstate,
							&simd_opponent, car_index);
	} else {
		update_player_state(&branch->opponentstate, &simd_opponent, &branch->playerstate,
							&simd_player, car_index);
	}
	player_motion_fraction = saved_fraction;
	player_motion_state = saved_state;
}
