#include "restunts.h"
#include "game_input.h"
#include "track_objects.h"
#include "opponent.h"
#include "car_audio.h"
#include "externs.h"
#include "crash_state.h"
#include "state_internal.h"
#include "physics_internal.h"
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include "extended_replay_runtime.h"
#endif

#define ACTIVE_CAR_COUNT_WITHOUT_OPPONENT 1U
#define ACTIVE_CAR_COUNT_WITH_OPPONENT 2U
#define CAR_WORLD_POSITION_SHIFT 6U
#define CAMERA_TARGET_OVERRIDE_FIELD_MIN 128
#define CAMERA_TARGET_OVERRIDE_FIELD_END 896
#define CAMERA_TARGET_HEIGHT 270
#define CAMERA_VERTICAL_STEP_LIMIT 30
#define CAMERA_FOLLOW_DISTANCE 450
#define CAMERA_FULL_RATE_STEP_LIMIT 120
#define CAMERA_REDUCED_RATE_STEP_LIMIT 240
#define TRACK_POINT_UPDATE_DIVISOR_SHIFT 1U
#define TRACK_POINT_INITIAL_DISTANCE 10000
#define START_FLAG_ANIMATION_LIMIT 450
#define START_FLAG_AUTO_DRIVE_THRESHOLD 384
#define START_FLAG_ANIMATION_STEP 8
#define START_SEQUENCE_LINE_DISTANCE 228
#define START_SEQUENCE_AUTO_DRIVE_SPEED_LIMIT 1280

static void update_trackside_camera(legacy_u16 car_index, legacy_s16 car_x, legacy_s16 car_z)
{
	legacy_u16 divisor = LEGACY_U16_SAR(framespersec, TRACK_POINT_UPDATE_DIVISOR_SHIFT);
	if (divisor != 0 && (legacy_u16)state.game_frame % divisor != 0) {
		return;
	}
	legacy_s16 nearest_distance = TRACK_POINT_INITIAL_DISTANCE;
	for (legacy_u8 candidate = 0;
		 LEGACY_S8_FROM_BITS(candidate) < LEGACY_S8_FROM_BITS(trackside_camera_count);
		 candidate++) {
		legacy_s32 delta_x = LEGACY_S32_WRAP_SUB(
			(legacy_s32)trackside_camera_positions[candidate].x, (legacy_s32)car_x);
		legacy_s32 delta_z = LEGACY_S32_WRAP_SUB(
			(legacy_s32)trackside_camera_positions[candidate].z, (legacy_s32)car_z);
		legacy_s32 absolute_x = delta_x < 0 ? LEGACY_S32_WRAP_NEGATE(delta_x) : delta_x;
		if (absolute_x >= nearest_distance) {
			continue;
		}
		legacy_s32 absolute_z = delta_z < 0 ? LEGACY_S32_WRAP_NEGATE(delta_z) : delta_z;
		if (absolute_z >= nearest_distance) {
			continue;
		}
		legacy_s16 distance = polarRadius2D(LEGACY_S16_FROM_BITS((legacy_u16)delta_x),
											LEGACY_S16_FROM_BITS((legacy_u16)delta_z));
		if (distance < nearest_distance) {
			state.game_trackside_camera_index[car_index] = LEGACY_S8_FROM_BITS(candidate);
			nearest_distance = distance;
		}
	}
}

static legacy_s16 follow_camera_uses_car_target(const struct GAMESTATE *camera_state,
												legacy_u16 car_index,
												const struct CARSTATE *carstate)
{
	return (car_index == PLAYER_CAR_INDEX &&
			(camera_state->game_player_route_status != ROUTE_TRACKING_NORMAL ||
			 camera_state->game_route_confirmation_count != ROUTE_CONFIRMATION_NONE)) ||
		   carstate->car_route_has_reverse_path != 0 ||
		   carstate->car_crashBmpFlag != CRASH_EVENT_NONE ||
		   carstate->car_route_index == ROUTE_INDEX_NONE ||
		   (carstate->car_route_heading_error > CAMERA_TARGET_OVERRIDE_FIELD_MIN &&
			carstate->car_route_heading_error < CAMERA_TARGET_OVERRIDE_FIELD_END);
}

static legacy_s16 follow_camera_step(legacy_s16 movement, legacy_u32 fraction20)
{
	if (fraction20 == 0) {
		return movement;
	}
	legacy_u32 tick_fraction =
		framespersec == GAME_FRAME_RATE_LOW ? PHYSICS_LOW_RATE_TICK_FRACTION : PHYSICS_FRACTION_ONE;
	return (legacy_s16)((legacy_s32)movement * (legacy_s32)fraction20 / (legacy_s32)tick_fraction);
}

static void update_follow_camera_height(struct GAMESTATE *camera_state, legacy_u16 car_index,
										legacy_s16 car_y, legacy_u32 fraction20)
{
	legacy_s16 target_y = LEGACY_S16_WRAP_ADD(car_y, CAMERA_TARGET_HEIGHT);
	legacy_s16 delta_y =
		LEGACY_S16_WRAP_SUB(camera_state->game_follow_camera_position[car_index].y, target_y);
	if (delta_y != 0) {
		legacy_s16 limit = follow_camera_step(CAMERA_VERTICAL_STEP_LIMIT, fraction20);
		if (delta_y > limit) {
			delta_y = limit;
		} else if (delta_y < -limit) {
			delta_y = -limit;
		}
		camera_state->game_follow_camera_position[car_index].y =
			LEGACY_S16_WRAP_SUB(camera_state->game_follow_camera_position[car_index].y, delta_y);
	}
}

static void update_car_follow_camera(struct GAMESTATE *camera_state, legacy_u16 car_index,
									 legacy_u32 fraction20)
{
	struct VECTOR *previous_position = car_index == PLAYER_CAR_INDEX
										   ? &camera_state->game_player_camera_previous
										   : &camera_state->game_opponent_camera_previous;
	*previous_position = camera_state->game_follow_camera_position[car_index];
	struct CARSTATE *carstate =
		car_index == PLAYER_CAR_INDEX ? &camera_state->playerstate : &camera_state->opponentstate;
	legacy_s16 car_x = LEGACY_S16_FROM_BITS(
		(legacy_u16)LEGACY_S32_SAR(carstate->car_position.lx, CAR_WORLD_POSITION_SHIFT));
	legacy_s16 car_y = LEGACY_S16_FROM_BITS(
		(legacy_u16)LEGACY_S32_SAR(carstate->car_position.ly, CAR_WORLD_POSITION_SHIFT));
	legacy_s16 car_z = LEGACY_S16_FROM_BITS(
		(legacy_u16)LEGACY_S32_SAR(carstate->car_position.lz, CAR_WORLD_POSITION_SHIFT));
	struct VECTOR target = carstate->car_route_target;
	if (follow_camera_uses_car_target(camera_state, car_index, carstate)) {
		target.x = car_x;
		target.y = car_y;
		target.z = car_z;
	}

	update_follow_camera_height(camera_state, car_index, car_y, fraction20);

	legacy_s16 angle = (legacy_s16)polarAngle(
		LEGACY_S16_WRAP_SUB(target.x, camera_state->game_follow_camera_position[car_index].x),
		LEGACY_S16_WRAP_SUB(target.z, camera_state->game_follow_camera_position[car_index].z));
	legacy_s16 distance = (legacy_s16)polarRadius2D(
		LEGACY_S16_WRAP_SUB(car_x, camera_state->game_follow_camera_position[car_index].x),
		LEGACY_S16_WRAP_SUB(car_z, camera_state->game_follow_camera_position[car_index].z));
	if (distance > CAMERA_FOLLOW_DISTANCE) {
		legacy_s16 adjustment = LEGACY_S16_WRAP_SUB(distance, CAMERA_FOLLOW_DISTANCE);
		/* The distance error already includes the short step's car motion.
		 * Scale the camera's speed limit, not that displacement a second time. */
		legacy_s16 limit = follow_camera_step(framespersec == GAME_FRAME_RATE_NORMAL
												  ? CAMERA_FULL_RATE_STEP_LIMIT
												  : CAMERA_REDUCED_RATE_STEP_LIMIT,
											  fraction20);
		if (adjustment > limit) {
			adjustment = limit;
		}
		camera_state->game_follow_camera_position[car_index].x =
			LEGACY_S16_WRAP_ADD(camera_state->game_follow_camera_position[car_index].x,
								multiply_and_scale(adjustment, sin_fast((legacy_u16)angle)));
		camera_state->game_follow_camera_position[car_index].z =
			LEGACY_S16_WRAP_ADD(camera_state->game_follow_camera_position[car_index].z,
								multiply_and_scale(adjustment, cos_fast((legacy_u16)angle)));
	}

	if (fraction20 == 0) {
		update_trackside_camera(car_index, car_x, car_z);
	}
}

void update_follow_cameras(void)
{
	legacy_u16 car_count = gameconfig.game_opponenttype == 0 ? ACTIVE_CAR_COUNT_WITHOUT_OPPONENT
															 : ACTIVE_CAR_COUNT_WITH_OPPONENT;
	for (legacy_u16 car_index = 0; car_index < car_count; car_index++) {
		update_car_follow_camera(&state, car_index, 0);
	}
}

void update_follow_cameras_fraction(struct GAMESTATE *camera_state, legacy_u32 fraction20)
{
	if (fraction20 == 0) {
		return;
	}
	legacy_u16 car_count = gameconfig.game_opponenttype == 0 ? ACTIVE_CAR_COUNT_WITHOUT_OPPONENT
															 : ACTIVE_CAR_COUNT_WITH_OPPONENT;
	for (legacy_u16 car_index = 0; car_index < car_count; car_index++) {
		update_car_follow_camera(camera_state, car_index, fraction20);
	}
}

static void update_race_end_timing(void)
{
	if (state.game_end_event != 0 && state.game_frame_in_sec < state.game_frames_per_sec) {
		state.game_frame_in_sec = LEGACY_S16_WRAP_ADD(state.game_frame_in_sec, 1);
		if (state.game_frame_in_sec == state.game_frames_per_sec && race_exit_request == 0) {
			if (state.playerstate.car_crashBmpFlag == 1 &&
				state.playerstate.car_actual_speed != CAR_SPEED_STOPPED) {
				state.game_frames_per_sec = LEGACY_S16_WRAP_ADD(state.game_frames_per_sec, 1);
			} else if (game_replay_mode == REPLAY_MODE_LIVE) {
				race_exit_request = 1;
			}
		}
	}
}

static void update_race_start_sequence(void)
{
	if (race_start_sequence_state != RACE_START_SEQUENCE_INACTIVE) {
		if (start_flag_animation < START_FLAG_ANIMATION_LIMIT) {
			start_flag_animation =
				LEGACY_S16_WRAP_ADD(start_flag_animation, START_FLAG_ANIMATION_STEP);
		}
		if (race_start_sequence_state == RACE_START_SEQUENCE_FLAG_ANIMATION &&
			start_flag_animation > START_FLAG_AUTO_DRIVE_THRESHOLD) {
			race_start_sequence_state = RACE_START_SEQUENCE_AUTO_DRIVE;
		}
		if (race_start_sequence_state == RACE_START_SEQUENCE_AUTO_DRIVE) {
			/* The original auto-drive path retains this distance in SI for player_op. */
			legacy_s16 start_line_distance = LEGACY_S16_WRAP_ADD(
				multiply_and_scale(
					cos_fast(track_angle),
					LEGACY_S16_WRAP_SUB(
						track_row_centers[start_finish_row],
						LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_SAR(
							state.playerstate.car_position.lz, CAR_WORLD_POSITION_SHIFT)))),
				multiply_and_scale(
					sin_fast(track_angle),
					LEGACY_S16_WRAP_SUB(
						track_column_centers[start_finish_column],
						LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_SAR(
							state.playerstate.car_position.lx, CAR_WORLD_POSITION_SHIFT)))));
			if (start_line_distance <= START_SEQUENCE_LINE_DISTANCE) {
				if (state.playerstate.car_rev_speed != CAR_SPEED_STOPPED) {
					update_player_tick_with_legacy_si(INPUT_BRAKE_FLAG, start_line_distance);
				} else {
					race_start_sequence_state = RACE_START_SEQUENCE_INACTIVE;
				}
			} else if (state.playerstate.car_rev_speed < START_SEQUENCE_AUTO_DRIVE_SPEED_LIMIT) {
				update_player_tick_with_legacy_si(INPUT_ACCELERATE_FLAG, start_line_distance);
			} else {
				update_player_tick_with_legacy_si(INPUT_NONE, start_line_distance);
			}
		}
	}
}

static void update_gamestate_impl(legacy_s16 caller_si, legacy_s16 update_audio,
								  legacy_s16 store_checkpoints)
{
#ifdef RESTUNTS_HEADLESS
	(void)update_audio;
#endif
	legacy_s8 car_input;
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
	if (extended_replay_is_active()) {
		car_input = (legacy_s8)extended_replay_input_at(extended_replay_frame());
		extended_replay_before_step(store_checkpoints);
	} else
#endif
	{
		car_input = replay_input_buffer[(legacy_u16)state.game_frame];
	}
	if (car_input != INPUT_NONE) {
		state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	}

	if (
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
		!extended_replay_is_active() &&
#endif
		(checkpoint_frame_interval == 0 ||
		 ((legacy_u16)state.game_frame % (legacy_u16)checkpoint_frame_interval) == 0)) {
		get_kevinrandom_seed(state.kevinseed);
		legacy_u16 checkpoint_index =
			LEGACY_U16_DIV_OR_ZERO(state.game_frame, checkpoint_frame_interval);
		/* The original checkpoint copy retains its index in SI until this tick returns. */
		caller_si = LEGACY_S16_FROM_BITS(checkpoint_index);
		if (store_checkpoints != 0) {
			fmemcpy(&cvxptr[checkpoint_index], &state, sizeof(struct GAMESTATE));
		}
	}

	state.game_frame = LEGACY_S16_WRAP_ADD(state.game_frame, 1);
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
	if (extended_replay_is_active()) {
		extended_replay_after_step();
	}
#endif
	update_race_end_timing();

	if (state.game_inputmode != GAME_INPUT_MODE_WAITING) {
		update_player_tick_with_legacy_si(car_input, caller_si);
		if (gameconfig.game_opponenttype != 0) {
			update_opponent_tick();
		}
		update_follow_cameras();
		if (state.game_particles_active != 0) {
			update_crash_particles();
		}
#ifndef RESTUNTS_HEADLESS
		if (update_audio != 0) {
			audio_carstate();
		}
#endif
	} else if (game_replay_mode == REPLAY_MODE_PAUSED) {
#ifndef RESTUNTS_HEADLESS
		if (update_audio != 0) {
			audio_carstate();
		}
#endif
		update_race_start_sequence();
	}
}

void update_gamestate(void)
{
	update_gamestate_impl(LEGACY_DEFAULT_PLAYER_TICK_SI, 1, 1);
}

void update_gamestate_with_legacy_si(legacy_s16 caller_si)
{
	update_gamestate_impl(caller_si, 1, 1);
}

void update_gamestate_silent(void)
{
	update_gamestate_impl(LEGACY_DEFAULT_PLAYER_TICK_SI, 0, 0);
}
