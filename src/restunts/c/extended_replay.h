#ifndef RESTUNTS_EXTENDED_REPLAY_H
#define RESTUNTS_EXTENDED_REPLAY_H

#include <stddef.h>
#include <stdint.h>

/* SDL3 replay storage. The original GAMEINFO/GAMESTATE disk layouts and the
 * legacy 12,000-byte recording window are deliberately independent of this. */
struct EXTENDED_REPLAY_BUFFER {
	uint8_t *inputs;
	uint32_t count;
	uint32_t capacity;
};

void extended_replay_init(struct EXTENDED_REPLAY_BUFFER *buffer);
void extended_replay_free(struct EXTENDED_REPLAY_BUFFER *buffer);
int extended_replay_reserve(struct EXTENDED_REPLAY_BUFFER *buffer, uint32_t capacity);
int extended_replay_assign(struct EXTENDED_REPLAY_BUFFER *buffer, const uint8_t *inputs,
						   uint32_t count);
int extended_replay_append(struct EXTENDED_REPLAY_BUFFER *buffer, uint8_t input);
int extended_replay_replace(struct EXTENDED_REPLAY_BUFFER *buffer, uint32_t frame, uint8_t input);
int extended_replay_truncate(struct EXTENDED_REPLAY_BUFFER *buffer, uint32_t frame);

struct EXTENDED_REPLAY_CHECKPOINT {
	uint32_t frame;
	void *state;
};

/* State is an opaque complete snapshot. Runtime callers must include their
 * 32-bit frame/finish/penalty clocks and random seed along with GAMESTATE. */
struct EXTENDED_REPLAY_HISTORY {
	struct EXTENDED_REPLAY_CHECKPOINT *checkpoints;
	size_t state_size;
	uint32_t count;
	uint32_t capacity;
};

void extended_replay_history_init(struct EXTENDED_REPLAY_HISTORY *history, size_t state_size);
void extended_replay_history_free(struct EXTENDED_REPLAY_HISTORY *history);
int extended_replay_checkpoint_store(struct EXTENDED_REPLAY_HISTORY *history, uint32_t frame,
									 const void *state);
const void *extended_replay_checkpoint_find(const struct EXTENDED_REPLAY_HISTORY *history,
											uint32_t frame, uint32_t *checkpoint_frame);
void extended_replay_history_truncate(struct EXTENDED_REPLAY_HISTORY *history, uint32_t frame);

uint32_t extended_replay_timeline_position(uint32_t frame, uint32_t recorded_frames,
										   uint32_t width);
uint32_t extended_replay_rewind_interpolate(uint32_t amount, uint32_t remaining, uint32_t distance);
/* Formats m:ss or m:ss.cc, including durations beyond one hour. Returns 0 if
 * the output is too short or the rate is invalid; it never emits partial text. */
int extended_replay_format_time(char *out, size_t size, uint32_t frame, uint16_t fps,
								int include_fraction);

#endif
