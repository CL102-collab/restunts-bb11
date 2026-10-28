#ifndef RESTUNTS_EXTENDED_REPLAY_RUNTIME_H
#define RESTUNTS_EXTENDED_REPLAY_RUNTIME_H

#include "csrpl.h"
#include "extended_replay.h"
#include "legacy.h"

struct EXTENDED_REPLAY_CLOCKS {
	uint32_t frame;
	uint32_t input_frame;
	uint32_t player_finish;
	uint32_t opponent_finish;
	uint32_t player_end;
	uint32_t opponent_end;
	uint32_t penalty;
};

extern struct EXTENDED_REPLAY_CLOCKS extended_replay_clocks;

/* Moves ownership without copying large buffers. The caller separately saves
 * GAMESTATE and globals, then resumes after restoring those globals. */
struct EXTENDED_REPLAY_SUSPENDED {
	int active;
	struct EXTENDED_REPLAY_CLOCKS clocks;
	struct EXTENDED_REPLAY_BUFFER recording;
	struct EXTENDED_REPLAY_HISTORY history;
};
void extended_replay_runtime_suspend(struct EXTENDED_REPLAY_SUSPENDED *saved);
void extended_replay_runtime_resume(struct EXTENDED_REPLAY_SUSPENDED *saved);

int extended_replay_is_active(void);
void extended_replay_runtime_begin(void);
void extended_replay_runtime_clear(void);
int extended_replay_runtime_load(const struct CSRPL_VIEW *view);
const uint8_t *extended_replay_inputs(void);
uint32_t extended_replay_frame(void);
uint32_t extended_replay_input_frame(void);
uint32_t extended_replay_recorded_frames(void);
void extended_replay_set_input_frame(uint32_t frame);
int extended_replay_record_input(uint8_t input);
int extended_replay_advance_input(void);
uint8_t extended_replay_input_at(uint32_t frame);
int extended_replay_replace_input(uint32_t frame, uint8_t input);
void extended_replay_reset_simulation(int reset_checkpoints);
void extended_replay_before_step(int store_checkpoints);
void extended_replay_after_step(void);
int extended_replay_seek(uint32_t frame);
int extended_replay_branch(uint32_t frame);
void extended_replay_format_runtime_time(legacy_s8 *out, uint32_t frame, int fraction);

#endif
