#ifndef RESTUNTS_EXTENDED_REPLAY_RUNTIME_H
#define RESTUNTS_EXTENDED_REPLAY_RUNTIME_H

#include "csrpl.h"
#include "extended_replay.h"
#include "../legacy.h"
#include "extended_checkpoint.h"
#include "extended_unit_penalty.h"

struct EXTENDED_REPLAY_CLOCKS {
	uint32_t frame;
	uint32_t input_frame;
	uint32_t player_finish;
	uint32_t opponent_finish;
	uint32_t player_end;
	uint32_t penalty;
	struct EXTENDED_CHECKPOINT_PROGRESS checkpoints;
	struct EXTENDED_UNIT_PENALTY unit_penalty;
};

extern struct EXTENDED_REPLAY_CLOCKS extended_replay_clocks;

struct EXTENDED_REPLAY_COMMAND_BUFFER {
	legacy_u8 *data;
	legacy_u32 count;
	legacy_u32 capacity;
};

/* Moves ownership without copying large buffers. The caller separately saves
 * GAMESTATE and globals, then resumes after restoring those globals. */
struct EXTENDED_REPLAY_SUSPENDED {
	int active;
	legacy_u16 version;
	legacy_u16 driving_rules;
	legacy_u32 flags;
	legacy_u8 opponent_id[CSRPL_OPPONENT_ID_SIZE];
	struct EXTENDED_REPLAY_COMMAND_BUFFER commands;
	struct EXTENDED_REPLAY_CLOCKS clocks;
	struct EXTENDED_REPLAY_BUFFER recording;
	struct EXTENDED_REPLAY_HISTORY history;
};
void extended_replay_runtime_suspend(struct EXTENDED_REPLAY_SUSPENDED *saved);
void extended_replay_runtime_resume(struct EXTENDED_REPLAY_SUSPENDED *saved);
void extended_replay_runtime_discard(struct EXTENDED_REPLAY_SUSPENDED *saved);

int extended_replay_is_active(void);
/* Retained recording revision; zero when no extended replay owner is active. */
legacy_u16 extended_replay_version(void);
/* Retained recording settings; loading never changes configured driving rules. */
legacy_u16 extended_replay_driving_rules(void);
legacy_u32 extended_replay_flags(void);
const legacy_u8 *extended_replay_opponent_id(void);
void extended_replay_set_classic_track(legacy_int classic);
const legacy_u8 *extended_replay_opponent_commands(void);
legacy_u32 extended_replay_opponent_command_count(void);
/* Frame is the pre-step frame. The game advances its clock before the opponent
 * tick, so an opponent tick uses extended_replay_frame() - 1. Missing commands
 * return zero; playback must not invoke a live planner in that case. */
legacy_int extended_replay_record_opponent_command(legacy_u32 frame,
												   const struct CSRPL_OPPONENT_COMMAND *command);
legacy_int extended_replay_opponent_command_at(legacy_u32 frame,
											   struct CSRPL_OPPONENT_COMMAND *command);
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
