#include "replay.h"

#define REPLAY_PLAYER_CAR_OFFSET 0U
#define REPLAY_PLAYER_MATERIAL_OFFSET 4U
#define REPLAY_PLAYER_TRANSMISSION_OFFSET 5U
#define REPLAY_OPPONENT_TYPE_OFFSET 6U
#define REPLAY_OPPONENT_CAR_OFFSET 7U
#define REPLAY_OPPONENT_MATERIAL_OFFSET 11U
#define REPLAY_OPPONENT_TRANSMISSION_OFFSET 12U
#define REPLAY_TRACK_NAME_OFFSET 13U
#define REPLAY_FRAMES_PER_SECOND_OFFSET 22U
#define REPLAY_RECORDED_FRAMES_OFFSET 24U

/* The header stores the ids and the track name as plain byte runs. */
static void replay_read_bytes(legacy_s8 *destination, const legacy_u8 far *source, legacy_u16 count)
{
	for (legacy_u16 index = 0U; index < count; index++) {
		destination[index] = LEGACY_S8_FROM_BITS(source[index]);
	}
}

static void replay_write_bytes(legacy_u8 far *destination, const legacy_s8 *source,
							   legacy_u16 count)
{
	for (legacy_u16 index = 0U; index < count; index++) {
		destination[index] = (legacy_u8)source[index];
	}
}

void replay_gameinfo_decode(struct GAMEINFO *destination, const legacy_u8 far *source)
{
	replay_read_bytes(destination->game_playercarid, source + REPLAY_PLAYER_CAR_OFFSET,
					  REPLAY_CAR_ID_SIZE);
	destination->game_playermaterial = LEGACY_S8_FROM_BITS(source[REPLAY_PLAYER_MATERIAL_OFFSET]);
	destination->game_playertransmission =
		LEGACY_S8_FROM_BITS(source[REPLAY_PLAYER_TRANSMISSION_OFFSET]);
	destination->game_opponenttype = LEGACY_S8_FROM_BITS(source[REPLAY_OPPONENT_TYPE_OFFSET]);
	replay_read_bytes(destination->game_opponentcarid, source + REPLAY_OPPONENT_CAR_OFFSET,
					  REPLAY_CAR_ID_SIZE);
	destination->game_opponentmaterial =
		LEGACY_S8_FROM_BITS(source[REPLAY_OPPONENT_MATERIAL_OFFSET]);
	destination->game_opponenttransmission =
		LEGACY_S8_FROM_BITS(source[REPLAY_OPPONENT_TRANSMISSION_OFFSET]);
	replay_read_bytes(destination->game_trackname, source + REPLAY_TRACK_NAME_OFFSET,
					  REPLAY_TRACK_NAME_SIZE);
	destination->game_framespersec = LEGACY_READ_U16_LE(source + REPLAY_FRAMES_PER_SECOND_OFFSET);
	destination->game_recordedframes = LEGACY_READ_U16_LE(source + REPLAY_RECORDED_FRAMES_OFFSET);
}

void replay_gameinfo_encode(legacy_u8 far *destination, const struct GAMEINFO *source)
{
	replay_write_bytes(destination + REPLAY_PLAYER_CAR_OFFSET, source->game_playercarid,
					   REPLAY_CAR_ID_SIZE);
	destination[REPLAY_PLAYER_MATERIAL_OFFSET] = (legacy_u8)source->game_playermaterial;
	destination[REPLAY_PLAYER_TRANSMISSION_OFFSET] = (legacy_u8)source->game_playertransmission;
	destination[REPLAY_OPPONENT_TYPE_OFFSET] = (legacy_u8)source->game_opponenttype;
	replay_write_bytes(destination + REPLAY_OPPONENT_CAR_OFFSET, source->game_opponentcarid,
					   REPLAY_CAR_ID_SIZE);
	destination[REPLAY_OPPONENT_MATERIAL_OFFSET] = (legacy_u8)source->game_opponentmaterial;
	destination[REPLAY_OPPONENT_TRANSMISSION_OFFSET] = (legacy_u8)source->game_opponenttransmission;
	replay_write_bytes(destination + REPLAY_TRACK_NAME_OFFSET, source->game_trackname,
					   REPLAY_TRACK_NAME_SIZE);
	LEGACY_WRITE_U16_LE(destination + REPLAY_FRAMES_PER_SECOND_OFFSET, source->game_framespersec);
	LEGACY_WRITE_U16_LE(destination + REPLAY_RECORDED_FRAMES_OFFSET, source->game_recordedframes);
}

legacy_u32 replay_file_size(legacy_u16 recorded_frames)
{
	return (legacy_u32)REPLAY_INPUT_OFFSET + (legacy_u32)recorded_frames;
}

legacy_u16 replay_timeline_position(legacy_u16 frame, legacy_u16 recorded_frames, legacy_u16 width)
{
	if (recorded_frames == 0U) {
		return 0U;
	}
	if (frame > recorded_frames) {
		frame = recorded_frames;
	}
	return (legacy_u16)(((legacy_u32)frame * (legacy_u32)width) / (legacy_u32)recorded_frames);
}

legacy_u16 replay_rewind_interpolate(legacy_u16 rewind_amount, legacy_u16 frames_remaining,
									 legacy_u16 checkpoint_distance)
{
	if (checkpoint_distance == 0U) {
		return 0U;
	}
	return (legacy_u16)(((legacy_u32)rewind_amount * (legacy_u32)frames_remaining) /
						(legacy_u32)checkpoint_distance);
}
