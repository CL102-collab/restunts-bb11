/* Resolve wheel-plane and wall contacts before native suspension accounting. */
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
#ifdef RESTUNTS_SDL3
	if (extended_player_measure_plane(position, player_motion_state->game_inputmode)) {
		return;
	}
#endif
	struct VECTOR wheel;

	physics_position_to_vector(&wheel, position);
	nextPosAndNormalIP = player_motion_state->game_inputmode == GAME_INPUT_MODE_INTRO
							 ? wheel.y
							 : plane_signed_distance(planindex, wheel.x, wheel.y, wheel.z);
}

static void apply_wheel_gravity(struct CARSTATE *carstate, struct VECTORLONG *position,
								legacy_s16 wheel_index)
{
	legacy_s16 gravity = wheel_gravity_steps[wheel_index];
#ifdef RESTUNTS_SDL3
	gravity = extended_player_wheel_gravity(carstate, gravity);
#endif
	legacy_s16 step_count =
		framespersec == GAME_FRAME_RATE_LOW && player_motion_fraction == 0 ? 2 : 1;
	for (legacy_s16 step = 0; step < step_count; step++) {
		carstate->car_wheel_vertical_speed[wheel_index] = LEGACY_S16_WRAP_ADD(
			carstate->car_wheel_vertical_speed[wheel_index], scale_player_motion(gravity));
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
	if (extended_player_use_ground_plane(position)) {
		measure_wheel_plane_distance(position);
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
#ifdef RESTUNTS_SDL3
	extended_player_contact_distance(position, player_motion_state->game_inputmode, &distance);
#endif
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
#ifdef RESTUNTS_SDL3
		if (extended_player_correct_wheel_contact(&motion->current[wheel_index],
												  &motion->previous[wheel_index],
												  player_motion_state->game_inputmode)) {
			resolved = 1;
		} else
#endif
		{
			resolved = nextPosAndNormalIP >= 0 ||
					   correct_wheel_plane_penetration(carstate, motion, wheel_index, car_index);
		}
#ifdef RESTUNTS_SDL3
		extended_player_finish_wheel_contact(&motion->current[wheel_index],
											 player_motion_state->game_inputmode, resolved);
#endif
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

void resolve_wheel_contacts(struct CARSTATE *carstate, struct PLAYER_WHEEL_MOTION *motion,
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
