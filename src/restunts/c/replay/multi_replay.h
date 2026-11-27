#ifndef RESTUNTS_MULTI_REPLAY_H
#define RESTUNTS_MULTI_REPLAY_H

#include "../externs.h"
#include "../ghost/ghost.h"
#include "replay.h"

#define MULTI_REPLAY_FRAME_RATE GAME_FRAME_RATE_NORMAL

struct MULTI_REPLAY_CAR {
	struct CARSTATE state;
	struct GHOST_CAMERA_STATE camera;
	struct SIMD simd;
	legacy_s8 id[REPLAY_CAR_ID_SIZE + 1U];
	legacy_s8 material;
};

legacy_int multi_replay_open(const legacy_s8 *manifest_path, legacy_char *error, size_t error_size);
void multi_replay_close(void);
legacy_int multi_replay_is_active(void);

size_t multi_replay_car_count(void);
size_t multi_replay_target_index(void);
const struct MULTI_REPLAY_CAR *multi_replay_car(size_t index);
void multi_replay_cycle_target(legacy_s16 direction);

legacy_u32 multi_replay_frame(void);
legacy_u32 multi_replay_input_frame(void);
legacy_u32 multi_replay_recorded_frames(void);
void multi_replay_seek(legacy_u32 frame);
legacy_int multi_replay_queue_frame(void);
void multi_replay_present_input_frame(void);
/* Produce one visual pose for every car. Lag is in Q16 presentation-clock frames.
 * The current replay frame, target and recorded events remain authoritative. */
void multi_replay_interpolate(legacy_u32 lag, struct MULTI_REPLAY_CAR *destination);

#endif
