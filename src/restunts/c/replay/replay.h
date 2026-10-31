#ifndef RESTUNTS_REPLAY_H
#define RESTUNTS_REPLAY_H

#include "../legacy.h"

#define REPLAY_GAMEINFO_SIZE 26U
#define REPLAY_TRACK_SIZE 1802U
#define REPLAY_INPUT_OFFSET (REPLAY_GAMEINFO_SIZE + REPLAY_TRACK_SIZE)
#define REPLAY_CAR_ID_SIZE 4U
#define REPLAY_TRACK_NAME_SIZE 9U
#define REPLAY_FILENAME_SIZE 128U

extern legacy_s8 replay_filename[REPLAY_FILENAME_SIZE];

enum REPLAY_MODE { REPLAY_MODE_LIVE = 0, REPLAY_MODE_PAUSED = 1, REPLAY_MODE_PLAYBACK = 2 };

enum REPLAY_PLAYBACK_RATE {
	REPLAY_PLAYBACK_NORMAL = 0,
	REPLAY_PLAYBACK_SLOW = 2,
	REPLAY_PLAYBACK_FAST = 3
};

#define REPLAY_EXIT_REQUESTED 2

#define REPLAY_RECORDING_ACTIVE_FLAG 1U
#define REPLAY_RECORDING_MODIFIED_FLAG 2U
#define REPLAY_RECORDING_RESTARTABLE_FLAG 4U
#define REPLAY_RECORDING_HIGHSCORE_INELIGIBLE_FLAGS                                                \
	(REPLAY_RECORDING_MODIFIED_FLAG | REPLAY_RECORDING_RESTARTABLE_FLAG)

#pragma pack(push, 1)

struct GAMEINFO {
	legacy_s8 game_playercarid[REPLAY_CAR_ID_SIZE];
	legacy_s8 game_playermaterial;
	legacy_s8 game_playertransmission;
	legacy_s8 game_opponenttype;
	legacy_s8 game_opponentcarid[REPLAY_CAR_ID_SIZE];
	legacy_s8 game_opponentmaterial;
	legacy_s8 game_opponenttransmission;
	legacy_s8 game_trackname[REPLAY_TRACK_NAME_SIZE];
	legacy_u16 game_framespersec;
	legacy_u16 game_recordedframes;
};

#pragma pack(pop)

typedef char
	legacy_gameinfo_must_be_26_bytes[(sizeof(struct GAMEINFO) == REPLAY_GAMEINFO_SIZE) ? 1 : -1];

void replay_gameinfo_decode(struct GAMEINFO *destination, const legacy_u8 far *source);
void replay_gameinfo_encode(legacy_u8 far *destination, const struct GAMEINFO *source);
legacy_u32 replay_file_size(legacy_u16 recorded_frames);
legacy_u16 replay_timeline_position(legacy_u16 frame, legacy_u16 recorded_frames, legacy_u16 width);
legacy_u16 replay_rewind_interpolate(legacy_u16 rewind_amount, legacy_u16 frames_remaining,
									 legacy_u16 checkpoint_distance);

#endif
