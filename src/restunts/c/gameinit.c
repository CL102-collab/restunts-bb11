#include "restunts.h"
#include "opponent.h"
#include "externs.h"
#include "crash_state.h"
#include "owoot.h"
#ifdef RESTUNTS_SDL3
#include "extended_track.h"
#include "extended_replay_runtime.h"
#endif

#define CAR_START_LONGITUDINAL_OFFSET 210
#define CAR_START_LATERAL_OFFSET 36
#define CAR_WORLD_POSITION_SHIFT 6U
#define CAR_WORLD_POSITION_SCALE 64L
#define CAR_INITIAL_BODY_HEIGHT 512L
#define CAR_INITIAL_GEAR_INDEX 1U
#define CAR_WHEELS_PER_AXLE (CARSTATE_WHEEL_COUNT / 2U)
#define CAR_INITIAL_SURFACE_GRIP 1000
#define GAMESTATE_CHECKPOINT_INTERVAL_SECONDS 30U
#define TIMER_TICKS_PER_SECOND 100U
#define INITIAL_CAMERA_LATERAL_OFFSET 512
#define INITIAL_CAMERA_DISTANCE 4096
#define INITIAL_CAMERA_TILE_SHIFT 10U
#define INITIAL_CAMERA_HEIGHT 960

enum GAMESTATE_CHECKPOINT_VALIDITY {
	GAMESTATE_CHECKPOINT_INVALID = 0,
	GAMESTATE_CHECKPOINT_VALID = 1
};

#define GAMESTATE_CHECKPOINT_INDEX_STEP 1U
#define GAMESTATE_INITIAL_TIMING_VALUE 1

static legacy_s16 angle_with_offset(legacy_s16 angle, legacy_s16 offset)
{
	return LEGACY_S16_WRAP_ADD(angle, offset);
}

static legacy_s32 track_coordinate_to_world(legacy_s16 base, legacy_s16 offset)
{
	legacy_s16 coordinate = LEGACY_S16_WRAP_ADD(base, offset);
	return LEGACY_S32_SHL((legacy_s32)coordinate, CAR_WORLD_POSITION_SHIFT);
}

static void calculate_car_start_offset(legacy_s16 track_direction, legacy_s16 side_direction,
									   legacy_s16 *column_offset, legacy_s16 *row_offset)
{
	*column_offset = LEGACY_S16_WRAP_ADD(
		multiply_and_scale(sin_fast(angle_with_offset(track_direction, ANGLE_HALF_TURN)),
						   CAR_START_LONGITUDINAL_OFFSET),
		multiply_and_scale(sin_fast(angle_with_offset(track_direction, side_direction)),
						   CAR_START_LATERAL_OFFSET));
	*row_offset = LEGACY_S16_WRAP_ADD(
		multiply_and_scale(cos_fast(angle_with_offset(track_direction, ANGLE_HALF_TURN)),
						   CAR_START_LONGITUDINAL_OFFSET),
		multiply_and_scale(cos_fast(angle_with_offset(track_direction, side_direction)),
						   CAR_START_LATERAL_OFFSET));
}

static void init_car_at_start(struct CARSTATE *carstate, struct SIMD *simd, legacy_s8 transmission,
							  legacy_s16 column_offset, legacy_s16 row_offset)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		legacy_s32 x = (legacy_s32)start_finish_column * 1024L + 512L + column_offset;
		legacy_s32 z = ((legacy_s32)extended_track_height() - 1L - start_finish_row) * 1024L +
					   512L + row_offset;
		legacy_s32 y = extended_track_elevation(start_finish_column, start_finish_row,
												512 + column_offset, 512 - row_offset);
		init_carstate_from_simd(carstate, simd, transmission, x * CAR_WORLD_POSITION_SCALE,
								y * CAR_WORLD_POSITION_SCALE, z * CAR_WORLD_POSITION_SCALE,
								LEGACY_S16_WRAP_NEGATE(track_angle));
		return;
	}
#endif
	init_carstate_from_simd(
		carstate, simd, transmission,
		track_coordinate_to_world(track_column_centers[start_finish_column], column_offset),
		LEGACY_S32_SHL((legacy_s32)hillHeightConsts[hillFlag], CAR_WORLD_POSITION_SHIFT),
		track_coordinate_to_world(track_row_centers[start_finish_row], row_offset),
		LEGACY_S16_WRAP_NEGATE(track_angle));
}

static void init_car_wheels(struct CARSTATE *playerstate, legacy_s32 posX, legacy_s32 posY,
							legacy_s32 posZ)
{
	struct VECTOR whlPos;

	whlPos.x =
		LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_DIV_OR_ZERO(posX, CAR_WORLD_POSITION_SCALE));
	whlPos.y =
		LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_DIV_OR_ZERO(posY, CAR_WORLD_POSITION_SCALE));
	whlPos.z =
		LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_DIV_OR_ZERO(posZ, CAR_WORLD_POSITION_SCALE));

	for (legacy_u16 i = 0; i < CARSTATE_WHEEL_COUNT; ++i) {
		playerstate->car_surfaceWhl[i] = CAR_SURFACE_PAVED;
		playerstate->car_wheel_vertical_speed[i] = 0;
		playerstate->car_suspension_deflection[i] = 0;
		playerstate->car_reserved_wheel_state[i] = 0;
		playerstate->car_reserved_contact_state[i] = 0;
		playerstate->car_suspension_target[i] = 0;

		playerstate->car_wheel_contact_positions[i] = whlPos;
		playerstate->car_body_corner_positions[i] = whlPos;
	}
}

void init_carstate_from_simd(struct CARSTATE *playerstate, struct SIMD *simd,
							 legacy_s8 transmission, legacy_s32 posX, legacy_s32 posY,
							 legacy_s32 posZ, legacy_s16 track_angle)
{
	playerstate->car_position.lx = posX;
	playerstate->car_previous_position.lx = posX;
	playerstate->car_position.ly = LEGACY_S32_WRAP_ADD(posY, CAR_INITIAL_BODY_HEIGHT);
	playerstate->car_previous_position.ly = posY;
	playerstate->car_position.lz = posZ;
	playerstate->car_previous_position.lz = posZ;

	playerstate->car_rotate.x = track_angle;
	playerstate->car_rotate.y = 0;
	playerstate->car_rotate.z = 0;
	playerstate->car_velocity_heading_offset = 0;
	playerstate->car_pseudoGravity = 0;
	playerstate->car_steeringAngle = CAR_STEERING_CENTERED;
	playerstate->car_is_braking = CAR_PEDAL_RELEASED;
	playerstate->car_is_accelerating = CAR_PEDAL_RELEASED;
	playerstate->car_currpm = simd->idle_rpm;
	playerstate->car_lastrpm = playerstate->car_currpm;
	playerstate->car_initial_rpm = playerstate->car_currpm;
	playerstate->car_current_gear = CAR_INITIAL_GEAR_INDEX;
	playerstate->car_speeddiff = 0;
	playerstate->car_rev_speed = CAR_SPEED_STOPPED;
	playerstate->car_actual_speed = CAR_SPEED_STOPPED;
	playerstate->car_lastspeed = CAR_SPEED_STOPPED;
	playerstate->car_gearratio = simd->gear_ratios[CAR_INITIAL_GEAR_INDEX];
	playerstate->car_gearratioshr8 = playerstate->car_gearratio >> LEGACY_BYTE_BITS;
	playerstate->car_knob_x = simd->knob_points[CAR_INITIAL_GEAR_INDEX].px;
	playerstate->car_knob_x2 = playerstate->car_knob_x;
	playerstate->car_knob_y = simd->knob_points[CAR_INITIAL_GEAR_INDEX].py;
	playerstate->car_knob_y2 = playerstate->car_knob_y;
	playerstate->car_slide_yaw_delta = 0;
	playerstate->car_front_wheel_response_angle = 0;
	playerstate->car_slip_angle = 0;
	playerstate->car_route_heading_error = 0;
	playerstate->car_route_index = 0;
	playerstate->car_sumSurfFrontWheels = CAR_WHEELS_PER_AXLE;
	playerstate->car_sumSurfRearWheels = CAR_WHEELS_PER_AXLE;
	playerstate->car_sumSurfAllWheels = CARSTATE_WHEEL_COUNT;
	playerstate->car_demandedGrip = 0;
	playerstate->car_surfacegrip_sum = CAR_INITIAL_SURFACE_GRIP;

	init_car_wheels(playerstate, posX, posY, posZ);
	if (owoot_enabled) {
		playerstate->car_reserved_route_word1 = 0;
		playerstate->car_reserved_route_word2 = 0;
	}

	playerstate->car_engineLimiterTimer = 0;
	playerstate->car_slidingFlag = CAR_SLIDING_INACTIVE;
	playerstate->car_collision_latch = CAR_COLLISION_LATCH_CLEAR;
	playerstate->car_crashBmpFlag = CRASH_EVENT_NONE;
	playerstate->car_changing_gear = CAR_GEAR_CHANGE_INACTIVE;
	playerstate->car_gear_change_delay = GEAR_CHANGE_DELAY_EXPIRED;
	playerstate->car_transmission = transmission;
	playerstate->car_lap_count = 0;
	playerstate->car_route_point_index = ROUTE_POINT_FIRST;
	playerstate->car_sound_flags = CAR_SOUND_ENGINE_ACTIVE_FLAG;
}

static void init_follow_camera_positions(void)
{
	state.game_follow_camera_position[PLAYER_CAR_INDEX].x = LEGACY_S16_WRAP_ADD(
		LEGACY_S16_WRAP_ADD(
			multiply_and_scale(sin_fast(angle_with_offset(track_angle, ANGLE_THREE_QUARTER_TURN)),
							   INITIAL_CAMERA_LATERAL_OFFSET),
			multiply_and_scale(sin_fast(angle_with_offset(track_angle, ANGLE_HALF_TURN)),
							   INITIAL_CAMERA_DISTANCE)),
		LEGACY_S16_SHL((legacy_s16)start_finish_column, INITIAL_CAMERA_TILE_SHIFT));
	state.game_follow_camera_position[PLAYER_CAR_INDEX].y =
		LEGACY_S16_WRAP_ADD(hillHeightConsts[hillFlag], INITIAL_CAMERA_HEIGHT);
	state.game_follow_camera_position[PLAYER_CAR_INDEX].z = LEGACY_S16_WRAP_ADD(
		LEGACY_S16_WRAP_ADD(
			multiply_and_scale(cos_fast(angle_with_offset(track_angle, ANGLE_HALF_TURN)),
							   INITIAL_CAMERA_DISTANCE),
			track_row_positions[start_finish_row]),
		multiply_and_scale(cos_fast(angle_with_offset(track_angle, ANGLE_THREE_QUARTER_TURN)),
						   INITIAL_CAMERA_LATERAL_OFFSET));

#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		state.game_follow_camera_position[PLAYER_CAR_INDEX].y =
			extended_track_elevation(start_finish_column, start_finish_row, 512, 512) +
			INITIAL_CAMERA_HEIGHT;
	}
#endif
	state.game_follow_camera_position[OPPONENT_CAR_INDEX] =
		state.game_follow_camera_position[PLAYER_CAR_INDEX];
	state.game_player_camera_previous = state.game_follow_camera_position[PLAYER_CAR_INDEX];
	state.game_opponent_camera_previous = state.game_follow_camera_position[PLAYER_CAR_INDEX];
}

static void init_race_cars(legacy_s16 initialization_mode)
{
	legacy_s16 start_row_offset;
	legacy_s16 start_column_offset;
	calculate_car_start_offset(track_angle, ANGLE_QUARTER_TURN, &start_column_offset,
							   &start_row_offset);

	init_car_at_start(&state.playerstate, &simd_player, gameconfig.game_playertransmission,
					  start_column_offset, start_row_offset);

	state.game_player_confirmed_route = 0;
	state.game_player_route_indicator = ROUTE_INDICATOR_NONE;
	state.game_opponent_route_indicator = ROUTE_INDICATOR_NONE;
	state.game_player_route_status = ROUTE_TRACKING_NORMAL;
	state.game_route_confirmation_count = ROUTE_CONFIRMATION_NONE;
	state.game_startcol = start_finish_column;
	state.game_startcol2 = start_finish_column;
	state.game_startrow = start_finish_row;
	state.game_startrow2 = start_finish_row;

	legacy_u8 route_point;
	if (initialization_mode != GAMESTATE_INIT_SKIP_ROUTE_SETUP) {
		route_point = (legacy_u8)state.playerstate.car_route_point_index;
		get_track_route_point(state.playerstate.car_route_index,
							  &state.playerstate.car_route_target, (legacy_s16)route_point, 0);
		state.playerstate.car_route_point_index = LEGACY_S8_WRAP_ADD(route_point, ROUTE_POINT_STEP);
	}

	calculate_car_start_offset(track_angle, ANGLE_THREE_QUARTER_TURN, &start_column_offset,
							   &start_row_offset);

	init_car_at_start(&state.opponentstate, &simd_opponent, TRANSMISSION_AUTOMATIC,
					  start_column_offset, start_row_offset);

	if (gameconfig.game_opponenttype && initialization_mode != GAMESTATE_INIT_SKIP_ROUTE_SETUP) {
		route_point = (legacy_u8)state.opponentstate.car_route_point_index;
		opponent_route_advance((legacy_s16)route_point);
		state.opponentstate.car_route_point_index =
			LEGACY_S8_WRAP_ADD(route_point, ROUTE_POINT_STEP);
	}
}

void init_game_state(legacy_s16 initialization_mode)
{
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
	if (extended_replay_is_active() && initialization_mode != GAMESTATE_INIT_TIMING_ONLY) {
		extended_replay_reset_simulation(initialization_mode == GAMESTATE_INIT_RESET_CHECKPOINTS);
	}
#endif
	if (initialization_mode == GAMESTATE_INIT_RESET_CHECKPOINTS) {
		elapsed_time1 = 0;
		for (legacy_u16 i = 0; i < GAMESTATE_CHECKPOINT_COUNT; ++i) {
			cvxptr[i].game_checkpoint_valid = GAMESTATE_CHECKPOINT_INVALID;
		}
	}

	if (framespersec == GAME_FRAME_RATE_LOW) {
		steerWhlRespTable_ptr = steerWhlRespTable_10fps;
	} else {
		steerWhlRespTable_ptr = steerWhlRespTable_20fps;
	}

	checkpoint_frame_interval = LEGACY_S16_FROM_BITS(
		LEGACY_U16_WRAP_MUL(framespersec, GAMESTATE_CHECKPOINT_INTERVAL_SECONDS));
	timer_ticks_per_frame =
		LEGACY_S16_FROM_BITS(LEGACY_U16_DIV_OR_ZERO(TIMER_TICKS_PER_SECOND, framespersec));

	if (initialization_mode != GAMESTATE_INIT_TIMING_ONLY) {
		reset_race_loop_state();

		state.game_checkpoint_valid = GAMESTATE_CHECKPOINT_VALID;
		state.game_frames_per_sec = GAMESTATE_INITIAL_TIMING_VALUE;
		state.game_inputmode = GAME_INPUT_MODE_WAITING;
		state.game_end_event = 0;
		state.game_frame_in_sec = 0;
		state.game_player_previous_route = 0;
		state.game_trackside_camera_index[0] = 0;
		state.game_trackside_camera_index[1] = 0;

		for (legacy_u16 i = 0; i < GAMESTATE_BREAKABLE_OBJECT_COUNT; ++i) {
			state.game_object_destroyed[i] = 0;
		}
		for (legacy_u16 i = 0; i < GAMESTATE_PARTICLE_SLOT_COUNT; ++i) {
			state.game_particle_forward_speed[i] = 0;
		}

		init_follow_camera_positions();
		state.game_travDist = 0;
		state.game_frame = 0;
		state.game_total_finish = 0;
		state.game_opponent_finish_time = 0;
		state.game_pEndFrame = 0;
		state.game_oEndFrame = 0;
		state.game_penalty = 0;
		state.game_impactSpeed = 0;
		state.game_topSpeed = 0;
		state.game_jumpCount = 0;

		init_race_cars(initialization_mode);

		state.game_particles_active = 0;
	}
}

static void init_game_state_with_rates(legacy_u16 frame_rate, legacy_u16 stored_frame_rate)
{
	framespersec = frame_rate;
	gameconfig.game_framespersec = stored_frame_rate;
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
}

void init_game_state_with_frame_rate(legacy_u16 frame_rate)
{
	init_game_state_with_rates(frame_rate, frame_rate);
}

void init_game_state_with_frame_rate_byte(legacy_u16 frame_rate)
{
	init_game_state_with_rates(
		frame_rate, LEGACY_U16_REPLACE_LOW_BYTE(gameconfig.game_framespersec, frame_rate));
}

void restore_gamestate(legacy_u16 frame)
{
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
	if (extended_replay_is_active()) {
		(void)extended_replay_seek(frame);
		return;
	}
#endif
	if (frame == 0 && elapsed_time1 == 0 &&
		cvxptr[0].game_checkpoint_valid == GAMESTATE_CHECKPOINT_INVALID) {
		init_game_state(GAMESTATE_INIT_NORMAL);
		return;
	}

	legacy_u16 curframe = LEGACY_U16_DIV_OR_ZERO(frame, checkpoint_frame_interval);
	if (curframe == GAMESTATE_CHECKPOINT_COUNT) {
		curframe = LEGACY_U16_WRAP_SUB(curframe, GAMESTATE_CHECKPOINT_INDEX_STEP);
	}

	/* Find the newest valid checkpoint preceding the requested frame. Frame zero
	 * must restore its saved random seed even if the current frame is zero. */
	if (frame != 0 && frame >= state.game_frame) {
		while (1) {
			if (LEGACY_U16_WRAP_MUL(curframe, checkpoint_frame_interval) <= state.game_frame) {
				return;
			}
			if (cvxptr[curframe].game_checkpoint_valid != GAMESTATE_CHECKPOINT_INVALID) {
				break;
			}
			/* A newly loaded replay has no checkpoints yet.  Stop at the
			 * initial state instead of wrapping before the checkpoint array. */
			if (curframe == 0) {
				return;
			}
			curframe = LEGACY_U16_WRAP_SUB(curframe, GAMESTATE_CHECKPOINT_INDEX_STEP);
		}
	}

	state = cvxptr[curframe];
	init_kevinrandom(state.kevinseed);
	elapsed_time2 = state.game_frame;
}
