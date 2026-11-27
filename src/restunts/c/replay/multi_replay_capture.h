#ifndef RESTUNTS_MULTI_REPLAY_CAPTURE_H
#define RESTUNTS_MULTI_REPLAY_CAPTURE_H

#include <stddef.h>
#include "../externs.h"
#include "../extended/csrpl.h"
#include "../ghost/ghost.h"

#define MULTI_REPLAY_CARS_PER_RECORDING 2U

struct MULTI_REPLAY_SOURCE {
	struct GAMEINFO config;
	struct CSRPL_VIEW extended;
	legacy_u8 *bytes;
	const legacy_u8 *track_data;
	const legacy_u8 *inputs;
	size_t track_size;
	legacy_u32 frame_count;
	legacy_s8 track_name[CSRPL_TRACK_NAME_SIZE + 1U];
	legacy_u8 classic_track;
	legacy_u8 is_extended;
};

struct MULTI_REPLAY_CAPTURE_POSE {
	struct CARSTATE car;
	struct GHOST_CAMERA_STATE camera;
};

struct MULTI_REPLAY_CAPTURE_FRAME {
	struct MULTI_REPLAY_CAPTURE_POSE cars[MULTI_REPLAY_CARS_PER_RECORDING];
};

struct MULTI_REPLAY_CAPTURE {
	struct GAMEINFO config;
	struct SIMD simd[MULTI_REPLAY_CARS_PER_RECORDING];
	struct MULTI_REPLAY_CAPTURE_FRAME *frames;
	legacy_u32 frame_count;
	legacy_u8 car_count;
};

/* Read an entire recording without changing the loaded game or track. The
 * caller initializes the destination to zero and owns it after success. */
legacy_int multi_replay_source_read(const legacy_s8 *path, struct MULTI_REPLAY_SOURCE *source,
									legacy_char *error, size_t error_size);
void multi_replay_source_free(struct MULTI_REPLAY_SOURCE *source);
legacy_int multi_replay_source_same_track(const struct MULTI_REPLAY_SOURCE *first,
										  const struct MULTI_REPLAY_SOURCE *second);
legacy_int multi_replay_source_activate_track(const struct MULTI_REPLAY_SOURCE *source,
											  legacy_char *error, size_t error_size);

/* Check the same loose and archived car resources used by native playback. */
legacy_int multi_replay_car_available(const legacy_s8 *car_id);
legacy_int multi_replay_car_has_dashboard(const legacy_s8 *car_id);

/* Capture at the source's native tick rate before loading race presentation
 * resources. Car resources, shared track, and legacy checkpoint allocation
 * must be ready. */
legacy_int multi_replay_capture(const struct MULTI_REPLAY_SOURCE *source,
								struct MULTI_REPLAY_CAPTURE *capture, legacy_char *error,
								size_t error_size);
void multi_replay_capture_free(struct MULTI_REPLAY_CAPTURE *capture);

#endif
