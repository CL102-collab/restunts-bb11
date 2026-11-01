#ifndef RESTUNTS_CAR_AUDIO_H
#define RESTUNTS_CAR_AUDIO_H

#include <stddef.h>
#include "../math/math.h"

/* Car sound samples and engine definitions shared with race and replay state. */

#define AUDIO_CAR_STATE_RECORD_COUNT 40U
#define AUDIO_CAR_STATE_RECORD_SIZE 34U
#define AUDIO_CAR_STATE_RESERVED_PREFIX_SIZE 6U
#define AUDIO_CAR_STATE_PLAYER_PREVIOUS_OFFSET 6U
#define AUDIO_CAR_STATE_PLAYER_CURRENT_OFFSET 12U
#define AUDIO_CAR_STATE_OPPONENT_PREVIOUS_OFFSET 18U
#define AUDIO_CAR_STATE_OPPONENT_CURRENT_OFFSET 24U
#define AUDIO_CAR_STATE_PLAYER_RPM_OFFSET 30U
#define AUDIO_CAR_STATE_OPPONENT_RPM_OFFSET 32U

enum AUDIO_ENGINE_LEGACY_TYPE { OPPONENT_ENGINE_LEGACY_TYPE = 32, PLAYER_ENGINE_LEGACY_TYPE = 33 };

#pragma pack(push, 1)

/* One entry of the ring buffer the engine sound is driven from: each car's
 * position relative to the camera before and after the frame, plus its rev
 * counter. The asm mixer reads these records, so the layout is fixed. */
struct AUDIO_CAR_STATE {
	legacy_u8 reserved_prefix[AUDIO_CAR_STATE_RESERVED_PREFIX_SIZE];
	struct VECTOR player_previous;
	struct VECTOR player_current;
	struct VECTOR opponent_previous;
	struct VECTOR opponent_current;
	legacy_s16 player_rpm;
	legacy_s16 opponent_rpm;
};

#pragma pack(pop)

typedef char audio_car_state_must_be_34_bytes
	[(sizeof(struct AUDIO_CAR_STATE) == AUDIO_CAR_STATE_RECORD_SIZE) ? 1 : -1];
typedef char audio_car_state_player_previous_offset_must_match
	[(offsetof(struct AUDIO_CAR_STATE, player_previous) == AUDIO_CAR_STATE_PLAYER_PREVIOUS_OFFSET)
		 ? 1
		 : -1];
typedef char audio_car_state_player_current_offset_must_match
	[(offsetof(struct AUDIO_CAR_STATE, player_current) == AUDIO_CAR_STATE_PLAYER_CURRENT_OFFSET)
		 ? 1
		 : -1];
typedef char
	audio_car_state_opponent_previous_offset_must_match[(offsetof(struct AUDIO_CAR_STATE,
																  opponent_previous) ==
														 AUDIO_CAR_STATE_OPPONENT_PREVIOUS_OFFSET)
															? 1
															: -1];
typedef char audio_car_state_opponent_current_offset_must_match
	[(offsetof(struct AUDIO_CAR_STATE, opponent_current) == AUDIO_CAR_STATE_OPPONENT_CURRENT_OFFSET)
		 ? 1
		 : -1];
typedef char audio_car_state_player_rpm_offset_must_match
	[(offsetof(struct AUDIO_CAR_STATE, player_rpm) == AUDIO_CAR_STATE_PLAYER_RPM_OFFSET) ? 1 : -1];
typedef char audio_car_state_opponent_rpm_offset_must_match
	[(offsetof(struct AUDIO_CAR_STATE, opponent_rpm) == AUDIO_CAR_STATE_OPPONENT_RPM_OFFSET) ? 1
																							 : -1];

#pragma pack(push, 1)
struct FULL_AUDIO_ENGINE_DEFINITION {
	legacy_u16 sample_count;
	legacy_u8 reserved_parameters[4];
	legacy_u8 initialized;
	legacy_u8 reserved_initialization_byte;
	const legacy_s8 far *resource_ids[10];
};
#pragma pack(pop)

/* The resource-id fields are 16-bit far pointers in the DOS ABI. */
#if defined(RESTUNTS_DOS16)
typedef char full_audio_engine_definition_must_be_48_bytes
	[(sizeof(struct FULL_AUDIO_ENGINE_DEFINITION) == 48) ? 1 : -1];
#endif

extern legacy_s8 audio_car_state_ready;
extern legacy_s8 audio_player_car_flags;
extern legacy_s8 audio_opponent_car_flags;
extern legacy_s16 audio_player_engine_channel;
extern legacy_s16 audio_opponent_engine_channel;
extern legacy_s16 audio_car_state_read_index;
extern legacy_s16 audio_car_state_write_index;
extern legacy_s16 audio_car_state_interval;
extern struct AUDIO_CAR_STATE far *audio_car_state_records;
extern legacy_u8 audio_previous_replay_mode;

void audio_apply_car_state_sample(const legacy_u8 far *sample, legacy_s16 interval);

legacy_s16 audio_init_engine(legacy_s16 timer_index, void far *first, void far *second,
							 void far *third);

void audio_carstate(void);

void audio_play_car_events(legacy_u8 flags, legacy_s16 channel);
extern struct FULL_AUDIO_ENGINE_DEFINITION player_engine_definition;
extern struct FULL_AUDIO_ENGINE_DEFINITION opponent_engine_definition;

#endif
