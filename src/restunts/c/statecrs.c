#include "externs.h"
#include "legacy.h"
#include "math.h"
#include "replay.h"
#include "crash_state.h"
#include "race_stats.h"
#include "car_audio.h"
#include "audio_control.h"
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include "extended_replay_runtime.h"
#ifndef RESTUNTS_HEADLESS
#include "extended_race_stats.h"
#endif
#endif

#define CAR_CRASH_PARTICLE_KIND_COUNT 2
#define CAR_CRASH_PARTICLE_LIMIT 18
#define CAR_CRASH_PARTICLE_LIFETIME_SCALE 6
#define OBJECT_PARTICLE_ANGLE_OFFSET 96
#define OBJECT_PARTICLE_ANGULAR_RANGE 192
#define OBJECT_PARTICLE_LIMIT 8
#define OBJECT_PARTICLE_TYPE_BASE 0
#define OBJECT_PARTICLE_LIFETIME_SCALE 1
#define PARTICLE_TYPE_VARIANT_COUNT 4
#define PARTICLE_TYPE_VARIANT_MASK 3U
#define PARTICLE_RANDOM_ROTATION_SCALE 4
#define PARTICLE_RANDOM_SPEED_SCALE 6
#define PARTICLE_FORWARD_SPEED_BIAS 384
#define PARTICLE_GRAVITY_STEP 19
#define PARTICLE_ROTATION_STEP 16
#define PARTICLE_TIMER_INACTIVE 0

enum PARTICLE_SYSTEM_STATE { PARTICLE_SYSTEM_INACTIVE = 0, PARTICLE_SYSTEM_ACTIVE = 1 };

#define CRASH_FRAME_RATE_SCALE_SHIFT 2U

enum CRASH_CAR_MOTION_POLICY { CRASH_CAR_MOTION_PRESERVED = 0, CRASH_CAR_MOTION_STOPPED = 1 };

#define CRASH_EXIT_TIMING_VALUE 1

#ifndef RESTUNTS_HEADLESS
#endif

#ifndef RESTUNTS_HEADLESS
static void stop_car_engine_audio(legacy_s16 player_flag)
{
	if (is_in_replay != 0 || audio_car_state_ready == 0) {
		return;
	}

	if (player_flag == PLAYER_CAR_INDEX) {
		audio_play_crash_and_stop_engine(audio_player_engine_channel);
	} else {
		audio_play_crash_and_stop_engine(audio_opponent_engine_channel);
	}
}
#endif

struct CRASH_PARTICLE_EMISSION {
	legacy_s16 kind;
	legacy_s16 type_base;
	legacy_s16 angular_range;
	legacy_s16 base_angle;
	legacy_s16 energy_offset;
	legacy_s16 lifetime_scale;
	legacy_s16 free_count;
};

static void emit_crash_particle(const struct CRASH_PARTICLE_EMISSION *emission, legacy_s16 slot,
								legacy_s16 emitted)
{
	state.game_particle_owner[slot] = (legacy_u8)emission->kind;
	state.game_particle_shape_index[slot] =
		(legacy_u8)(((legacy_u8)emitted & PARTICLE_TYPE_VARIANT_MASK) +
					(legacy_u8)emission->type_base);
	state.game_particle_x[slot] = 0;
	state.game_particle_y[slot] = 0;
	state.game_particle_z[slot] = 0;

	legacy_s16 random_value = (legacy_s16)get_kevinrandom();
	state.game_particle_rotation_x[slot] =
		LEGACY_S16_WRAP_MUL(random_value, PARTICLE_RANDOM_ROTATION_SCALE);
	random_value = (legacy_s16)get_kevinrandom();
	state.game_particle_rotation_y[slot] =
		LEGACY_S16_WRAP_MUL(random_value, PARTICLE_RANDOM_ROTATION_SCALE);

	legacy_s16 particle_angle = LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_DIV_OR_ZERO(
		LEGACY_S32_WRAP_MUL((legacy_s32)emission->angular_range, (legacy_s32)emitted),
		(legacy_s32)emission->free_count));
	particle_angle = LEGACY_S16_WRAP_ADD(particle_angle, emission->base_angle);
	state.game_particle_heading[slot] =
		LEGACY_S16_FROM_BITS((legacy_u16)particle_angle & ANGLE_MASK);

	random_value = (legacy_s16)get_kevinrandom();
	legacy_s16 particle_timer = LEGACY_S16_WRAP_ADD(
		LEGACY_S16_SAR2(LEGACY_S16_WRAP_MUL(random_value, PARTICLE_RANDOM_SPEED_SCALE)),
		emission->energy_offset);
	particle_timer = LEGACY_S16_WRAP_ADD(particle_timer, PARTICLE_FORWARD_SPEED_BIAS);
	state.game_particle_forward_speed[slot] = particle_timer;

	legacy_s16 particle_lifetime =
		LEGACY_S16_SAR2(LEGACY_S16_WRAP_MUL(emission->lifetime_scale, particle_timer));
	LEGACY_WRITE_U16_LE(&state.game_particle_vertical_speed[slot * LEGACY_WORD_BYTES],
						particle_lifetime);
}

void emit_crash_particles(legacy_s16 kind_arg, legacy_s16 base_angle_arg,
						  legacy_s16 energy_offset_arg)
{
	struct CRASH_PARTICLE_EMISSION emission;
	emission.kind = (legacy_s16)kind_arg;
	emission.base_angle = (legacy_s16)base_angle_arg;
	emission.energy_offset = (legacy_s16)energy_offset_arg;
	legacy_s16 particle_limit;
	if (emission.kind < CAR_CRASH_PARTICLE_KIND_COUNT) {
		emission.angular_range = ANGLE_FULL_TURN;
		particle_limit = CAR_CRASH_PARTICLE_LIMIT;
		emission.type_base =
			LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_MUL(emission.kind, PARTICLE_TYPE_VARIANT_COUNT),
								PARTICLE_TYPE_VARIANT_COUNT);
		emission.lifetime_scale = CAR_CRASH_PARTICLE_LIFETIME_SCALE;
	} else {
		emission.base_angle =
			LEGACY_S16_WRAP_SUB(emission.base_angle, OBJECT_PARTICLE_ANGLE_OFFSET);
		emission.angular_range = OBJECT_PARTICLE_ANGULAR_RANGE;
		particle_limit = OBJECT_PARTICLE_LIMIT;
		emission.type_base = OBJECT_PARTICLE_TYPE_BASE;
		emission.lifetime_scale = OBJECT_PARTICLE_LIFETIME_SCALE;
	}

	state.game_particles_active = PARTICLE_SYSTEM_ACTIVE;
	emission.free_count = 0;
	for (legacy_u16 slot = 0; slot < GAMESTATE_PARTICLE_SLOT_COUNT; slot++) {
		if (state.game_particle_forward_speed[slot] == PARTICLE_TIMER_INACTIVE) {
			emission.free_count = LEGACY_S16_WRAP_ADD(emission.free_count, 1);
		}
	}
	if (emission.free_count > particle_limit) {
		emission.free_count = particle_limit;
	}

	legacy_s16 emitted = 0;
	for (legacy_u16 slot = 0; slot < GAMESTATE_PARTICLE_SLOT_COUNT && emitted < emission.free_count;
		 slot++) {
		if (state.game_particle_forward_speed[slot] != 0) {
			continue;
		}

		emit_crash_particle(&emission, slot, emitted);
		emitted = LEGACY_S16_WRAP_ADD(emitted, 1);
	}
}

void update_crash_particles(void)
{
	legacy_u8 any_active = PARTICLE_SYSTEM_INACTIVE;
	struct MATRIX *rotation;
	struct VECTOR direction;
	struct VECTOR movement;
	for (legacy_u16 slot = 0; slot < GAMESTATE_PARTICLE_SLOT_COUNT; slot++) {
		if (state.game_particle_forward_speed[slot] == PARTICLE_TIMER_INACTIVE) {
			continue;
		}

		direction.x = 0;
		direction.y = 0;
		direction.z = state.game_particle_forward_speed[slot];
		rotation = mat_rot_zxy(0, 0, state.game_particle_heading[slot], MATRIX_ROTATION_ORDER_YXZ);
		mat_mul_vector(&direction, rotation, &movement);
		state.game_particle_x[slot] =
			LEGACY_S32_WRAP_ADD_S16(state.game_particle_x[slot], movement.x);
		state.game_particle_z[slot] =
			LEGACY_S32_WRAP_ADD_S16(state.game_particle_z[slot], movement.z);

		legacy_s16 particle_velocity = LEGACY_S16_FROM_BITS(
			LEGACY_READ_U16_LE(&state.game_particle_vertical_speed[slot * LEGACY_WORD_BYTES]));
		particle_velocity = LEGACY_S16_WRAP_SUB(particle_velocity, PARTICLE_GRAVITY_STEP);
		LEGACY_WRITE_U16_LE(&state.game_particle_vertical_speed[slot * LEGACY_WORD_BYTES],
							particle_velocity);
		state.game_particle_y[slot] =
			LEGACY_S32_WRAP_ADD_S16(state.game_particle_y[slot], particle_velocity);

		if (framespersec == GAME_FRAME_RATE_LOW) {
			particle_velocity = LEGACY_S16_WRAP_SUB(particle_velocity, PARTICLE_GRAVITY_STEP);
			LEGACY_WRITE_U16_LE(&state.game_particle_vertical_speed[slot * LEGACY_WORD_BYTES],
								particle_velocity);
			state.game_particle_y[slot] =
				LEGACY_S32_WRAP_ADD_S16(state.game_particle_y[slot], particle_velocity);
		}

		legacy_s32 ground_position = LEGACY_S32_WRAP_ADD(
			(legacy_s32)state.game_particle_y[slot], (legacy_s32)state.playerstate.car_position.ly);
		if (ground_position < 0) {
			state.game_particle_forward_speed[slot] = PARTICLE_TIMER_INACTIVE;
			continue;
		}

		any_active = PARTICLE_SYSTEM_ACTIVE;
		state.game_particle_rotation_x[slot] =
			LEGACY_S16_WRAP_ADD(state.game_particle_rotation_x[slot], PARTICLE_ROTATION_STEP);
		state.game_particle_rotation_y[slot] =
			LEGACY_S16_WRAP_ADD(state.game_particle_rotation_y[slot], PARTICLE_ROTATION_STEP);
	}

	state.game_particles_active = any_active;
}

static void finish_crash_event(struct CARSTATE *carstate, legacy_s16 crash_event,
							   legacy_s16 car_index, legacy_s8 stop_car)
{
	if (stop_car == CRASH_CAR_MOTION_STOPPED) {
		carstate->car_actual_speed = CAR_SPEED_STOPPED;
		carstate->car_rev_speed = CAR_SPEED_STOPPED;
	}
	if (car_index == PLAYER_CAR_INDEX) {
		state.game_pEndFrame = state.game_frame;
	} else {
		state.game_oEndFrame = state.game_frame;
	}
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
	if (extended_replay_is_active()) {
		if (car_index == PLAYER_CAR_INDEX) {
			extended_replay_clocks.player_end = extended_replay_frame();
		} else {
			extended_replay_clocks.opponent_end = extended_replay_frame();
		}
	}
#endif
	if (state.game_end_event == 0 && car_index == PLAYER_CAR_INDEX) {
		state.game_end_event = crash_event;
	}
#ifndef RESTUNTS_HEADLESS
	if (((legacy_u8)replay_recording_flags & REPLAY_RECORDING_RESTARTABLE_FLAG) == 0) {
		// These copied values are used by the evaluation screen.
		gState_travDist = state.game_travDist;
		gState_frame = state.game_frame;
		gState_total_finish_time = state.game_total_finish;
		gState_opponent_finish_time = state.game_opponent_finish_time;
		gState_pEndFrame = state.game_pEndFrame;
		gState_oEndFrame = state.game_oEndFrame;
		gState_penalty = state.game_penalty;
		gState_impactSpeed = state.game_impactSpeed;
		gState_topSpeed = state.game_topSpeed;
		gState_jumpCount = state.game_jumpCount;
#ifdef RESTUNTS_SDL3
		if (extended_replay_is_active()) {
			extended_race_stats_capture();
		}
#endif
	}
#endif
}

// previously set_AV_event_triggers
void update_crash_state(legacy_s16 crash_event, legacy_s16 car_index)
{
	struct CARSTATE *carstate;
	if (car_index == PLAYER_CAR_INDEX) {
		carstate = &state.playerstate;
	} else if (car_index == OPPONENT_CAR_INDEX) {
		carstate = &state.opponentstate;
	} else {
		return;
	}
	if (carstate->car_crashBmpFlag != CRASH_EVENT_NONE) {
		return;
	}

	legacy_s8 stop_car = CRASH_CAR_MOTION_PRESERVED;
	switch (crash_event) {
		case CRASH_EVENT_IMMEDIATE_STOP:
			crash_event = CRASH_EVENT_COLLISION;
			stop_car = CRASH_CAR_MOTION_STOPPED;
			/* fall through */
		case CRASH_EVENT_COLLISION:
			carstate->car_crashBmpFlag = CRASH_EVENT_COLLISION;
			emit_crash_particles(car_index, carstate->car_rotate.x, 0);
			if (car_index == PLAYER_CAR_INDEX) {
				state.game_impactSpeed = carstate->car_actual_speed;
				state.game_frames_per_sec = LEGACY_S16_FROM_BITS(
					LEGACY_U16_SHL(framespersec, CRASH_FRAME_RATE_SCALE_SHIFT));
			}
#ifndef RESTUNTS_HEADLESS
			stop_car_engine_audio(car_index);
#endif
			break;

		case CRASH_EVENT_WATER:
#ifndef RESTUNTS_HEADLESS
			stop_car_engine_audio(car_index);
#endif
			carstate->car_crashBmpFlag = CRASH_EVENT_WATER;
			stop_car = CRASH_CAR_MOTION_STOPPED;
			if (car_index == PLAYER_CAR_INDEX) {
				state.game_impactSpeed = carstate->car_actual_speed;
				state.game_frames_per_sec = LEGACY_S16_FROM_BITS(
					LEGACY_U16_SHL(framespersec, CRASH_FRAME_RATE_SCALE_SHIFT));
			}
			break;

		case CRASH_EVENT_FINISH:
			carstate->car_crashBmpFlag = CRASH_EVENT_FINISH;
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
			if (extended_replay_is_active()) {
				if (car_index == PLAYER_CAR_INDEX) {
					uint32_t frame = extended_replay_frame();
					uint32_t penalty = extended_replay_clocks.penalty;
					extended_replay_clocks.player_finish =
						frame > UINT32_MAX - penalty ? UINT32_MAX : frame + penalty;
				} else {
					extended_replay_clocks.opponent_finish = extended_replay_frame();
				}
			}
#endif
			if (car_index == PLAYER_CAR_INDEX) {
				state.game_total_finish = LEGACY_S16_WRAP_ADD(
					LEGACY_S16_WRAP_ADD(state.game_frame, state.game_penalty), elapsed_time1);
				state.game_frames_per_sec = framespersec;
			} else {
				state.game_opponent_finish_time =
					LEGACY_S16_WRAP_ADD(state.game_frame, elapsed_time1);
			}
			break;

		case CRASH_EVENT_EXIT:
			state.game_frame_in_sec = CRASH_EXIT_TIMING_VALUE;
			state.game_frames_per_sec = CRASH_EXIT_TIMING_VALUE;
			break;
	}

	finish_crash_event(carstate, crash_event, car_index, stop_car);
	return;
}
