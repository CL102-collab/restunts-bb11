#include "audio.h"
#include "audio_internal.h"
#include "../externs.h"
#include "../game_input.h"
#include "../ghost/ghost.h"
#include "../legacy.h"
#include "../math/math.h"
#include "../platform.h"
#include "../camera.h"
#include "../car/car_audio.h"
#ifdef RESTUNTS_MULTI_REPLAY
#include "../crash_state.h"
#include "../replay/multi_replay.h"
#include "audio_control.h"
#endif

#define AUDIO_TRACK_CAMERA_VERTICAL_OFFSET 90
#define AUDIO_CAR_COUNT_WITH_OPPONENT 2

void audio_play_bump(legacy_s16 channel);
void audio_play_scrape(legacy_s16 channel);
void audio_start_engine(legacy_s16 channel);
void audio_stop_engine(legacy_s16 channel);
void audio_play_paved_skid(legacy_s16 channel);
void audio_play_offroad_skid(legacy_s16 channel);
void audio_stop_skid_sound(legacy_s16 channel);

static legacy_s16 audio_carstate_position(legacy_s32 position)
{
	return position_to_word(position);
}

/* Each car is logged as its offset from the camera, before and after. */
static void audio_carstate_offsets(struct VECTOR far *previous, struct VECTOR far *current,
								   const struct VECTOR *camera_previous,
								   const struct VECTOR *camera_current,
								   const struct VECTOR *car_previous,
								   const struct VECTOR *car_current)
{
	LEGACY_WRITE_U16_LE((legacy_u8 far *)&previous->x,
						LEGACY_S16_WRAP_SUB(camera_previous->x, car_previous->x));
	LEGACY_WRITE_U16_LE((legacy_u8 far *)&previous->y,
						LEGACY_S16_WRAP_SUB(camera_previous->y, car_previous->y));
	LEGACY_WRITE_U16_LE((legacy_u8 far *)&previous->z,
						LEGACY_S16_WRAP_SUB(camera_previous->z, car_previous->z));
	LEGACY_WRITE_U16_LE((legacy_u8 far *)&current->x,
						LEGACY_S16_WRAP_SUB(camera_current->x, car_current->x));
	LEGACY_WRITE_U16_LE((legacy_u8 far *)&current->y,
						LEGACY_S16_WRAP_SUB(camera_current->y, car_current->y));
	LEGACY_WRITE_U16_LE((legacy_u8 far *)&current->z,
						LEGACY_S16_WRAP_SUB(camera_current->z, car_current->z));
}

static legacy_u8 audio_carstate_update_flags(legacy_u8 desired, legacy_s16 channel, legacy_u8 flags)
{
	if ((desired & CAR_SOUND_ENGINE_ACTIVE_FLAG) != 0) {
		if ((flags & CAR_SOUND_ENGINE_ACTIVE_FLAG) == 0) {
			flags = (legacy_u8)(flags | CAR_SOUND_ENGINE_ACTIVE_FLAG);
			audio_start_engine(channel);
		}
	} else if ((flags & CAR_SOUND_ENGINE_ACTIVE_FLAG) != 0) {
		flags = (legacy_u8)(flags - CAR_SOUND_ENGINE_ACTIVE_FLAG);
		audio_stop_engine(channel);
	}

	if ((desired & CAR_SOUND_SKID_MASK) != 0) {
		if ((flags & CAR_SOUND_SKID_MASK) == (desired & CAR_SOUND_SKID_MASK)) {
			return flags;
		}
		if ((flags & CAR_SOUND_SKID_MASK) == 0) {
			if ((desired & CAR_SOUND_SKID_PAVED_FLAG) != 0) {
				audio_play_paved_skid(channel);
				return (legacy_u8)(flags + CAR_SOUND_SKID_PAVED_FLAG);
			}
			audio_play_offroad_skid(channel);
			return (legacy_u8)(flags + CAR_SOUND_SKID_OFFROAD_FLAG);
		}
	} else if ((flags & CAR_SOUND_SKID_MASK) == 0) {
		return flags;
	}

	if ((flags & CAR_SOUND_SKID_PAVED_FLAG) != 0) {
		flags = (legacy_u8)(flags - CAR_SOUND_SKID_PAVED_FLAG);
	}
	if ((flags & CAR_SOUND_SKID_OFFROAD_FLAG) != 0) {
		flags = (legacy_u8)(flags - CAR_SOUND_SKID_OFFROAD_FLAG);
	}
	audio_stop_skid_sound(channel);
	return flags;
}

struct AUDIO_CAR_POSITIONS {
	struct VECTOR player_previous;
	struct VECTOR player_current;
	struct VECTOR opponent_previous;
	struct VECTOR opponent_current;
	struct VECTOR camera_previous;
	struct VECTOR camera_current;
};

static void audio_carstate_enter_replay(void)
{
	if (audio_car_state_ready != 0) {
		audio_car_state_read_index = audio_car_state_write_index;
		if (((legacy_u8)audio_player_car_flags & CAR_SOUND_SKID_MASK) != 0) {
			audio_stop_skid_sound(audio_player_engine_channel);
		}
		if (((legacy_u8)audio_player_car_flags & CAR_SOUND_ENGINE_ACTIVE_FLAG) != 0) {
			audio_stop_engine(audio_player_engine_channel);
		}
		if (gameconfig.game_opponenttype != 0) {
			if (((legacy_u8)audio_opponent_car_flags & CAR_SOUND_SKID_MASK) != 0) {
				audio_stop_skid_sound(audio_opponent_engine_channel);
			}
			if (((legacy_u8)audio_opponent_car_flags & CAR_SOUND_ENGINE_ACTIVE_FLAG) != 0) {
				audio_stop_engine(audio_opponent_engine_channel);
			}
		}
		audio_car_state_ready = 0;
		audio_player_car_flags = 0;
		audio_opponent_car_flags = 0;
	}
	if ((legacy_u8)audio_previous_replay_mode != (legacy_u8)is_in_replay) {
		audio_reset_channels();
	}
	audio_previous_replay_mode = (legacy_u8)is_in_replay;
}

static void audio_carstate_read_positions(const struct CARSTATE *carstate, struct VECTOR *previous,
										  struct VECTOR *current)
{
	previous->x = audio_carstate_position((legacy_s32)carstate->car_previous_position.lx);
	previous->y = audio_carstate_position((legacy_s32)carstate->car_previous_position.ly);
	previous->z = audio_carstate_position((legacy_s32)carstate->car_previous_position.lz);
	current->x = audio_carstate_position((legacy_s32)carstate->car_position.lx);
	current->y = audio_carstate_position((legacy_s32)carstate->car_position.ly);
	current->z = audio_carstate_position((legacy_s32)carstate->car_position.lz);
}

static void audio_carstate_trackside_positions(struct AUDIO_CAR_POSITIONS *positions,
											   legacy_s16 track_index)
{
	positions->camera_current = trackside_camera_positions[track_index];
	positions->camera_current.y = LEGACY_S16_WRAP_ADD(
		LEGACY_S16_WRAP_ADD(positions->camera_current.y, camera_track_height_offset),
		AUDIO_TRACK_CAMERA_VERTICAL_OFFSET);
	positions->camera_previous = positions->camera_current;
}

static void audio_carstate_camera_positions(struct AUDIO_CAR_POSITIONS *positions,
											const struct GHOST_CAMERA_STATE *ghost_camera)
{
	legacy_u8 follow =
		followOpponentFlag != 0 && (gameconfig.game_opponenttype != 0 || ghost_camera != 0);
	if (cameramode == CAMERA_MODE_FOLLOW) {
		if (follow != 0 && ghost_camera != 0) {
			positions->camera_current = ghost_camera->follow_position;
			positions->camera_previous = ghost_camera->previous_position;
		} else {
			positions->camera_current = state.game_follow_camera_position[follow];
			positions->camera_previous = follow != 0 ? state.game_opponent_camera_previous
													 : state.game_player_camera_previous;
		}
	} else if (cameramode == CAMERA_MODE_TRACKSIDE) {
		legacy_s16 track_index =
			follow != 0 && ghost_camera != 0
				? ghost_camera->trackside_index
				: LEGACY_S16_FROM_BITS(
					  (legacy_u16)(legacy_s8)state.game_trackside_camera_index[follow]);
		audio_carstate_trackside_positions(positions, track_index);
	} else if (follow != 0) {
		positions->camera_current = positions->opponent_current;
		positions->camera_previous = positions->opponent_previous;
	} else {
		positions->camera_current = positions->player_current;
		positions->camera_previous = positions->player_previous;
	}
}

static legacy_s16 audio_carstate_write_record(struct AUDIO_CAR_POSITIONS *positions)
{
	struct AUDIO_CAR_STATE far *record = &audio_car_state_records[audio_car_state_write_index];
	audio_carstate_offsets(&record->player_previous, &record->player_current,
						   &positions->camera_previous, &positions->camera_current,
						   &positions->player_previous, &positions->player_current);
	LEGACY_WRITE_U16_LE((legacy_u8 far *)&record->player_rpm, state.playerstate.car_currpm);

	legacy_s16 car_count = 1;
	if (gameconfig.game_opponenttype != 0) {
		audio_carstate_offsets(&record->opponent_previous, &record->opponent_current,
							   &positions->camera_previous, &positions->camera_current,
							   &positions->opponent_previous, &positions->opponent_current);
		LEGACY_WRITE_U16_LE((legacy_u8 far *)&record->opponent_rpm, state.opponentstate.car_currpm);
		car_count = AUDIO_CAR_COUNT_WITH_OPPONENT;
	}
	return car_count;
}

static void audio_carstate_update_car_flags(legacy_s16 car_count)
{
	struct CARSTATE *carstate;
	for (legacy_s16 car_index = 0; car_index < car_count; car_index++) {
		legacy_s16 channel;
		legacy_u8 flags;
		if (car_index == 0) {
			carstate = &state.playerstate;
			channel = audio_player_engine_channel;
			flags = (legacy_u8)audio_player_car_flags;
		} else {
			carstate = &state.opponentstate;
			channel = audio_opponent_engine_channel;
			flags = (legacy_u8)audio_opponent_car_flags;
		}
		flags = audio_carstate_update_flags((legacy_u8)carstate->car_sound_flags, channel, flags);
		if (car_index == 0) {
			audio_player_car_flags = (legacy_s8)flags;
		} else {
			audio_opponent_car_flags = (legacy_s8)flags;
		}
	}
}

static void audio_carstate_finish_record(void)
{
	audio_car_state_ready = 1;
	audio_car_state_write_index = LEGACY_S16_WRAP_ADD(audio_car_state_write_index, 1);
	if (audio_car_state_write_index == AUDIO_CAR_STATE_RECORD_COUNT) {
		audio_car_state_write_index = 0;
	}
	audio_previous_replay_mode = (legacy_u8)is_in_replay;
}

#ifdef RESTUNTS_MULTI_REPLAY
static legacy_u8 multi_replay_audio_initialized;
static legacy_u32 multi_replay_audio_frame;

void audio_multi_replay_reset(void)
{
	if (!multi_replay_audio_initialized) {
		return;
	}
	const struct MULTI_REPLAY_CAR *car = multi_replay_target_car();
	multi_replay_audio_frame = car == NULL ? 0U : car->camera.frame;

	audio_car_state_read_index = audio_car_state_write_index;
	audio_car_state_interval = 0;
	if (audio_car_state_ready != 0) {
		audio_stop_skid_sound(audio_player_engine_channel);
		audio_stop_engine(audio_player_engine_channel);
		/* Also discard an impact still playing from the old view. */
		audio_reset_channels();
	}
	audio_car_state_ready = 0;
	audio_player_car_flags = CAR_SOUND_NONE;
	audio_previous_replay_mode = (legacy_u8)is_in_replay;
}

void audio_multi_replay_begin(void)
{
	multi_replay_audio_initialized = 1;
	audio_multi_replay_reset();
}

void audio_multi_replay_end(void)
{
	audio_multi_replay_reset();
	multi_replay_audio_initialized = 0;
}

static legacy_s16 audio_replay_previous_coordinate(legacy_s16 previous, legacy_s16 current,
												   legacy_u16 frame_rate)
{
	legacy_s32 delta = LEGACY_S16_WRAP_SUB(current, previous);
	delta = delta * frame_rate / MULTI_REPLAY_FRAME_RATE;
	return LEGACY_S16_WRAP_SUB(current, LEGACY_S16_FROM_BITS((legacy_u16)delta));
}

static void audio_replay_adjust_motion(struct VECTOR *previous, const struct VECTOR *current,
									   legacy_u16 frame_rate)
{
	previous->x = audio_replay_previous_coordinate(previous->x, current->x, frame_rate);
	previous->y = audio_replay_previous_coordinate(previous->y, current->y, frame_rate);
	previous->z = audio_replay_previous_coordinate(previous->z, current->z, frame_rate);
}

static void audio_multi_replay_positions(const struct MULTI_REPLAY_CAR *car,
										 struct AUDIO_CAR_POSITIONS *positions)
{
	audio_carstate_read_positions(&car->state, &positions->player_previous,
								  &positions->player_current);
	if (cameramode == CAMERA_MODE_FOLLOW) {
		positions->camera_previous = car->camera.previous_position;
		positions->camera_current = car->camera.follow_position;
	} else if (cameramode == CAMERA_MODE_TRACKSIDE) {
		audio_carstate_trackside_positions(positions, car->camera.trackside_index);
	} else {
		positions->camera_previous = positions->player_previous;
		positions->camera_current = positions->player_current;
	}

	if (car->frame_rate != MULTI_REPLAY_FRAME_RATE) {
		/* Convert slower replays' per-tick motion to the audio mix interval. */
		audio_replay_adjust_motion(&positions->player_previous, &positions->player_current,
								   car->frame_rate);
		audio_replay_adjust_motion(&positions->camera_previous, &positions->camera_current,
								   car->frame_rate);
	}
}

static void audio_multi_replay_carstate(void)
{
	if (!multi_replay_audio_initialized) {
		return;
	}
	const struct MULTI_REPLAY_CAR *car = multi_replay_target_car();
	if (is_in_replay != 0 || car->finished) {
		audio_multi_replay_reset();
		return;
	}

	struct AUDIO_CAR_POSITIONS positions;
	audio_multi_replay_positions(car, &positions);
	struct AUDIO_CAR_STATE far *record = &audio_car_state_records[audio_car_state_write_index];
	audio_carstate_offsets(&record->player_previous, &record->player_current,
						   &positions.camera_previous, &positions.camera_current,
						   &positions.player_previous, &positions.player_current);
	LEGACY_WRITE_U16_LE((legacy_u8 far *)&record->player_rpm, car->state.car_currpm);

	legacy_u8 sound_flags = (legacy_u8)car->state.car_sound_flags;
	legacy_int crashed = car->state.car_crashBmpFlag == CRASH_EVENT_COLLISION ||
						 car->state.car_crashBmpFlag == CRASH_EVENT_WATER;
	if (crashed) {
		sound_flags &= (legacy_u8)~CAR_SOUND_ENGINE_ACTIVE_FLAG;
	}
	audio_player_car_flags = (legacy_s8)audio_carstate_update_flags(
		sound_flags, audio_player_engine_channel, (legacy_u8)audio_player_car_flags);
	audio_carstate_finish_record();

	/* Native ticks can repeat at a higher playback rate. Switches and seeks
	 * prime this cursor at the displayed frame so past events stay silent. */
	if (car->camera.frame > multi_replay_audio_frame) {
		if (crashed && car->camera.crash_frame > multi_replay_audio_frame &&
			car->camera.crash_frame <= car->camera.frame) {
			audio_play_crash_and_stop_engine(audio_player_engine_channel);
		}
		audio_play_car_events(sound_flags, audio_player_engine_channel);
		multi_replay_audio_frame = car->camera.frame;
	}
}
#endif

void audio_carstate(void)
{
#ifdef RESTUNTS_MULTI_REPLAY
	if (multi_replay_is_active()) {
		audio_multi_replay_carstate();
		return;
	}
#endif
	if (is_in_replay != 0) {
		audio_carstate_enter_replay();
		return;
	}

	struct AUDIO_CAR_POSITIONS positions;
	audio_carstate_read_positions(&state.playerstate, &positions.player_previous,
								  &positions.player_current);

	const struct GHOST_CAMERA_STATE *ghost_camera = 0;
	if (gameconfig.game_opponenttype != 0) {
		audio_carstate_read_positions(&state.opponentstate, &positions.opponent_previous,
									  &positions.opponent_current);
	} else if (followOpponentFlag != 0) {
		/* The timer also calls this function: use the last rendered ghost snapshot,
		 * never refresh its on-disk trajectory from the audio callback. */
		struct CARSTATE *ghost = ghost_car_state();
		if (ghost != 0) {
			ghost_camera = ghost_camera_state();
			audio_carstate_read_positions(ghost, &positions.opponent_previous,
										  &positions.opponent_current);
		}
	}

	audio_carstate_camera_positions(&positions, ghost_camera);
	if (ghost_camera != 0) {
		ghost_adjust_camera_motion(&positions.camera_previous, &positions.camera_current);
	}

	legacy_s16 car_count = audio_carstate_write_record(&positions);

	audio_carstate_update_car_flags(car_count);

	audio_carstate_finish_record();
}
