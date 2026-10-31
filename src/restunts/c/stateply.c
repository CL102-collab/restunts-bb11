#include "legacy.h"
#include "math.h"
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

legacy_u32 player_motion_fraction;
struct GAMESTATE *player_motion_state = &state;

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
	legacy_s16 height_only = planindex < PHYSICS_HEIGHT_ONLY_PLANE_COUNT;
#ifdef RESTUNTS_SDL3
	height_only = extended_player_body_height_only(height_only);
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
	if (extended_player_other_car_distant(other_carstate)) {
		return 0;
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
	extended_player_start_finish_pole(obstacle_pose, angle,
									  PLAYER_PHYSICS_START_FINISH_POLE_OFFSET);
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
	legacy_s16 valid_tile = column >= 0 && column < PLAYER_PHYSICS_TRACK_GRID_SIZE && row >= 0 &&
							row < PLAYER_PHYSICS_TRACK_GRID_SIZE;
#ifdef RESTUNTS_SDL3
	extended_player_scenery_tile(&car_pose[PLAYER_PHYSICS_POSE_POSITION_INDEX], &column, &row,
								 &valid_tile);
#endif
	struct VECTOR obstacle_pose[PLAYER_PHYSICS_POSE_VECTOR_COUNT];
	obstacle_pose[PLAYER_PHYSICS_POSE_ROTATION_INDEX].x = 0;
	obstacle_pose[PLAYER_PHYSICS_POSE_ROTATION_INDEX].y = 0;
	obstacle_pose[PLAYER_PHYSICS_POSE_ROTATION_INDEX].z = 0;
	if (!valid_tile) {
		return 0;
	}
	if (handle_auxiliary_obstacles(carstate, simd, car_pose, obstacle_pose, column, row,
								   car_index)) {
		return 1;
	}
	handle_breakable_object(carstate, simd, car_pose, obstacle_pose, column, row);
	return handle_start_finish_poles(simd, car_pose, obstacle_pose, column, row, car_index);
}

void update_player_state(struct CARSTATE *carstate, struct SIMD *simd,
						 struct CARSTATE *other_carstate, struct SIMD *other_simd,
						 legacy_s16 car_index)
{
	initialize_working_car_pose(carstate);
#ifdef RESTUNTS_SDL3
	extended_player_begin(carstate, player_motion_state, player_motion_fraction);
#endif
	struct PLAYER_WHEEL_MOTION motion;
	prepare_wheel_motion(carstate, simd, &motion, car_index);
	resolve_wheel_contacts(carstate, &motion, car_index);
#ifdef RESTUNTS_SDL3
	struct EXTENDED_PLAYER_SUSPENSION_STATE saved_suspension;
	extended_player_capture_suspension(carstate, &saved_suspension);
#endif
	apply_wheel_suspension(carstate, &motion);
#ifdef RESTUNTS_SDL3
	extended_player_restore_suspension(carstate, &saved_suspension);
#endif
	derive_car_pose_from_wheels(&motion);
	sum_front_and_rear_wheel_surfaces(carstate);
#ifdef RESTUNTS_SDL3
	extended_player_finish(carstate, player_motion_state, player_motion_fraction);
#endif

	struct VECTOR car_pose[PLAYER_PHYSICS_POSE_VECTOR_COUNT];
	if (player_motion_state->game_inputmode != GAME_INPUT_MODE_INTRO) {
		play_car_impact_sounds(carstate, car_index);
		check_car_body_planes(carstate, simd, car_index);
#ifdef RESTUNTS_SDL3
		extended_player_clear_crashed_motion(carstate);
#endif
		update_car_surface_contact(carstate, car_index);
		prepare_car_collision_pose(car_pose);
		if (handle_other_car_collision(carstate, simd, other_carstate, other_simd, car_pose,
									   car_index) ||
			handle_scenery_collisions(carstate, simd, car_pose, car_index)) {
#ifdef RESTUNTS_SDL3
			extended_player_clear_motion(carstate);
#endif
			return;
		}
	}
	commit_working_car_pose(carstate);
	if (player_motion_fraction == 0) {
		owoot_update_player(carstate, car_index);
	}
}
