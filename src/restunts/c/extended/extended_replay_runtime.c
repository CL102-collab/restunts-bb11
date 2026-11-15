#include "extended_replay_runtime.h"
#include "extended_replay.h"
#include "../externs.h"
#include "../crash_state.h"
#include "../game_input.h"
#include "../driving_rules.h"

#include <string.h>
#include <stdlib.h>
/* Command histories can exceed the legacy 16-bit copy length. */
#undef memcpy

struct EXTENDED_REPLAY_CLOCKS extended_replay_clocks;

struct EXTENDED_RUNTIME_CHECKPOINT {
	struct GAMESTATE state;
	struct EXTENDED_REPLAY_CLOCKS clocks;
};

static int extended_active;
static legacy_u16 extended_version;
static legacy_u16 extended_driving_rules;
static legacy_u32 extended_flags;
static legacy_u8 extended_opponent_id[CSRPL_OPPONENT_ID_SIZE];
static struct EXTENDED_REPLAY_COMMAND_BUFFER extended_commands;
#define EXTENDED_COMMAND_INITIAL_CAPACITY 1024U
static struct EXTENDED_REPLAY_BUFFER extended_recording;
static struct EXTENDED_REPLAY_HISTORY extended_history = {
	NULL, sizeof(struct EXTENDED_RUNTIME_CHECKPOINT), 0U, 0U};

static void extended_replay_sync_legacy(void)
{
	state.game_frame = LEGACY_S16_FROM_BITS((legacy_u16)extended_replay_clocks.frame);
	elapsed_time2 = (legacy_u16)extended_replay_clocks.input_frame;
	elapsed_time1 = 0U;
	/* A nonzero legacy marker keeps menus outside the extended path aware of
	 * the recording. The authoritative count is always extended_recording.count. */
	gameconfig.game_recordedframes =
		(legacy_u16)(extended_recording.count > LEGACY_S16_MAX ? LEGACY_S16_MAX
															   : extended_recording.count);
}

int extended_replay_is_active(void)
{
	return extended_active;
}

legacy_u16 extended_replay_version(void)
{
	return extended_version;
}

legacy_u16 extended_replay_driving_rules(void)
{
	return extended_driving_rules;
}

legacy_u32 extended_replay_flags(void)
{
	return extended_flags;
}

const legacy_u8 *extended_replay_opponent_id(void)
{
	return extended_opponent_id;
}

void extended_replay_set_classic_track(legacy_int classic)
{
	if (extended_active && extended_version >= CSRPL_OPPONENT_COMMAND_VERSION) {
		if (classic) {
			extended_flags |= CSRPL_FLAG_CLASSIC_TRACK;
		} else {
			extended_flags &= (legacy_u32)~CSRPL_FLAG_CLASSIC_TRACK;
		}
	}
}

const legacy_u8 *extended_replay_opponent_commands(void)
{
	return extended_commands.data;
}

legacy_u32 extended_replay_opponent_command_count(void)
{
	return extended_commands.count;
}

static void extended_replay_commands_clear(void)
{
	free(extended_commands.data);
	memset(&extended_commands, 0, sizeof(extended_commands));
}

legacy_int extended_replay_record_opponent_command(legacy_u32 frame,
												   const struct CSRPL_OPPONENT_COMMAND *command)
{
	legacy_u8 wire[CSRPL_OPPONENT_COMMAND_SIZE];
	if (!extended_active || (extended_flags & CSRPL_FLAG_OPPONENT_COMMANDS) == 0U ||
		command == NULL || frame >= extended_recording.count) {
		return 0;
	}
	csrpl_opponent_command_write(wire, command);
	if (!csrpl_opponent_command_valid(wire)) {
		return 0;
	}
	legacy_u32 needed = frame + 1U;
	if (needed > extended_commands.capacity) {
		legacy_u32 capacity = extended_commands.capacity != 0U ? extended_commands.capacity
															   : EXTENDED_COMMAND_INITIAL_CAPACITY;
		while (capacity < needed) {
			if (capacity > LEGACY_U32_MAX / 2U) {
				capacity = needed;
				break;
			}
			capacity *= 2U;
		}
		if ((legacy_u64)capacity * CSRPL_OPPONENT_COMMAND_SIZE > SIZE_MAX) {
			return 0;
		}
		legacy_u8 *data =
			realloc(extended_commands.data, (size_t)capacity * CSRPL_OPPONENT_COMMAND_SIZE);
		if (data == NULL) {
			return 0;
		}
		extended_commands.data = data;
		extended_commands.capacity = capacity;
	}
	if (frame > extended_commands.count) {
		/* Waiting at the start does not run an opponent tick. Those samples
		 * are explicit no-ops, not holes in the recorded timeline. */
		memset(extended_commands.data +
				   (size_t)extended_commands.count * CSRPL_OPPONENT_COMMAND_SIZE,
			   0, (size_t)(frame - extended_commands.count) * CSRPL_OPPONENT_COMMAND_SIZE);
	}
	memcpy(extended_commands.data + (size_t)frame * CSRPL_OPPONENT_COMMAND_SIZE, wire,
		   sizeof(wire));
	if (needed > extended_commands.count) {
		extended_commands.count = needed;
	}
	return 1;
}

legacy_int extended_replay_opponent_command_at(legacy_u32 frame,
											   struct CSRPL_OPPONENT_COMMAND *command)
{
	if (!extended_active || command == NULL || frame >= extended_commands.count) {
		return 0;
	}
	csrpl_opponent_command_read(
		extended_commands.data + (size_t)frame * CSRPL_OPPONENT_COMMAND_SIZE, command);
	return 1;
}

void extended_replay_runtime_discard(struct EXTENDED_REPLAY_SUSPENDED *saved)
{
	free(saved->commands.data);
	extended_replay_free(&saved->recording);
	extended_replay_history_free(&saved->history);
	memset(saved, 0, sizeof(*saved));
}

void extended_replay_runtime_suspend(struct EXTENDED_REPLAY_SUSPENDED *saved)
{
	saved->active = extended_active;
	saved->version = extended_version;
	saved->driving_rules = extended_driving_rules;
	saved->flags = extended_flags;
	memcpy(saved->opponent_id, extended_opponent_id, sizeof(extended_opponent_id));
	saved->commands = extended_commands;
	memset(&extended_commands, 0, sizeof(extended_commands));
	saved->clocks = extended_replay_clocks;
	saved->recording = extended_recording;
	saved->history = extended_history;
	extended_active = 0;
	extended_version = 0;
	extended_driving_rules = 0;
	extended_flags = 0;
	memset(extended_opponent_id, 0, sizeof(extended_opponent_id));
	extended_replay_init(&extended_recording);
	extended_replay_history_init(&extended_history, sizeof(struct EXTENDED_RUNTIME_CHECKPOINT));
	memset(&extended_replay_clocks, 0, sizeof(extended_replay_clocks));
}

void extended_replay_runtime_resume(struct EXTENDED_REPLAY_SUSPENDED *saved)
{
	extended_replay_runtime_clear();
	extended_active = saved->active;
	extended_version = saved->version;
	extended_driving_rules = saved->driving_rules;
	extended_flags = saved->flags;
	memcpy(extended_opponent_id, saved->opponent_id, sizeof(extended_opponent_id));
	extended_commands = saved->commands;
	extended_replay_clocks = saved->clocks;
	extended_recording = saved->recording;
	extended_history = saved->history;
	memset(saved, 0, sizeof(*saved));
	if (extended_active) {
		extended_replay_sync_legacy();
	}
}

void extended_replay_runtime_begin(void)
{
	extended_active = 1;
	extended_version = CSRPL_VERSION;
	extended_driving_rules = driving_rules_current();
	extended_replay_commands_clear();
	extended_flags = 0;
	memset(extended_opponent_id, 0, sizeof(extended_opponent_id));
	if (gameconfig.game_opponenttype == CSRPL_UNIT_OPPONENT_TYPE) {
		extended_flags = CSRPL_FLAG_OPPONENT_COMMANDS;
		memcpy(extended_opponent_id, "UNIT", CSRPL_OPPONENT_ID_SIZE);
	}
	extended_replay_free(&extended_recording);
	extended_replay_history_free(&extended_history);
	memset(&extended_replay_clocks, 0, sizeof(extended_replay_clocks));
	extended_replay_sync_legacy();
}

void extended_replay_runtime_clear(void)
{
	extended_replay_commands_clear();
	extended_active = 0;
	extended_version = 0;
	extended_driving_rules = 0;
	extended_flags = 0;
	memset(extended_opponent_id, 0, sizeof(extended_opponent_id));
	extended_replay_free(&extended_recording);
	extended_replay_history_free(&extended_history);
	memset(&extended_replay_clocks, 0, sizeof(extended_replay_clocks));
}

int extended_replay_runtime_load(const struct CSRPL_VIEW *view)
{
	if (view == NULL || view->metadata.version < CSRPL_MIN_VERSION ||
		(view->metadata.driving_rules & (legacy_u16)~DRIVING_RULE_KNOWN_MASK) != 0U ||
		(view->metadata.flags & (legacy_u32)~CSRPL_KNOWN_FLAGS) != 0U ||
		(view->metadata.version < CSRPL_OPPONENT_COMMAND_VERSION && view->metadata.flags != 0U)) {
		return 0;
	}
	legacy_int has_commands = (view->metadata.flags & CSRPL_FLAG_OPPONENT_COMMANDS) != 0U;
	if (has_commands != (view->metadata.opponent_type > CSRPL_CLASSIC_OPPONENT_LAST) ||
		(has_commands &&
		 (view->metadata.opponent_type != CSRPL_UNIT_OPPONENT_TYPE ||
		  memcmp(view->metadata.opponent_id, "UNIT", CSRPL_OPPONENT_ID_SIZE) != 0))) {
		return 0;
	}
	struct EXTENDED_REPLAY_COMMAND_BUFFER commands = {0};
	if (has_commands && view->metadata.frame_count != 0U) {
		legacy_u64 bytes = (legacy_u64)view->metadata.frame_count * CSRPL_OPPONENT_COMMAND_SIZE;
		if (view->opponent_commands == NULL || bytes > SIZE_MAX) {
			return 0;
		}
		for (legacy_u32 index = 0; index < view->metadata.frame_count; index++) {
			if (!csrpl_opponent_command_valid(view->opponent_commands +
											  (size_t)index * CSRPL_OPPONENT_COMMAND_SIZE)) {
				return 0;
			}
		}
		commands.data = malloc((size_t)bytes);
		if (commands.data == NULL) {
			return 0;
		}
		memcpy(commands.data, view->opponent_commands, (size_t)bytes);
		commands.count = view->metadata.frame_count;
		commands.capacity = commands.count;
	}
	if (!extended_replay_assign(&extended_recording, view->inputs, view->metadata.frame_count)) {
		free(commands.data);
		return 0;
	}
	extended_replay_commands_clear();
	extended_commands = commands;
	extended_active = 1;
	extended_version = view->metadata.version;
	extended_driving_rules = view->metadata.driving_rules;
	extended_flags = view->metadata.flags;
	memcpy(extended_opponent_id, view->metadata.opponent_id, sizeof(extended_opponent_id));
	extended_replay_history_free(&extended_history);
	memset(&extended_replay_clocks, 0, sizeof(extended_replay_clocks));
	extended_replay_sync_legacy();
	return 1;
}

const uint8_t *extended_replay_inputs(void)
{
	return extended_recording.inputs;
}

uint32_t extended_replay_frame(void)
{
	return extended_active ? extended_replay_clocks.frame : (legacy_u16)state.game_frame;
}

uint32_t extended_replay_input_frame(void)
{
	return extended_active ? extended_replay_clocks.input_frame : elapsed_time2;
}

uint32_t extended_replay_recorded_frames(void)
{
	return extended_active ? extended_recording.count : gameconfig.game_recordedframes;
}

void extended_replay_set_input_frame(uint32_t frame)
{
	if (extended_active) {
		extended_replay_clocks.input_frame =
			frame <= extended_recording.count ? frame : extended_recording.count;
		extended_replay_sync_legacy();
	} else {
		elapsed_time2 = (legacy_u16)frame;
	}
}

int extended_replay_record_input(uint8_t input)
{
	uint32_t frame = extended_replay_clocks.input_frame;
	int result = frame < extended_recording.count
					 ? extended_replay_replace(&extended_recording, frame, input)
					 : frame == extended_recording.count &&
						   extended_replay_append(&extended_recording, input);
	if (!result) {
		return 0;
	}
	extended_replay_clocks.input_frame++;
	extended_replay_sync_legacy();
	return 1;
}

int extended_replay_advance_input(void)
{
	if (extended_replay_clocks.input_frame >= extended_recording.count) {
		return 0;
	}
	extended_replay_clocks.input_frame++;
	extended_replay_sync_legacy();
	return 1;
}

uint8_t extended_replay_input_at(uint32_t frame)
{
	return frame < extended_recording.count ? extended_recording.inputs[frame] : INPUT_NONE;
}

int extended_replay_replace_input(uint32_t frame, uint8_t input)
{
	return extended_replay_replace(&extended_recording, frame, input);
}

void extended_replay_reset_simulation(int reset_checkpoints)
{
	if (!extended_active) {
		return;
	}
	if (reset_checkpoints) {
		extended_replay_history_free(&extended_history);
	}
	memset(&extended_replay_clocks, 0, sizeof(extended_replay_clocks));
	extended_replay_sync_legacy();
}

void extended_replay_before_step(int store_checkpoints)
{
	uint32_t interval = framespersec > 0 ? (uint32_t)framespersec * 30U : 600U;
	if (store_checkpoints && extended_replay_clocks.frame % interval == 0U) {
		get_kevinrandom_seed(state.kevinseed);
		struct EXTENDED_RUNTIME_CHECKPOINT snapshot;
		snapshot.state = state;
		snapshot.clocks = extended_replay_clocks;
		if (!extended_replay_checkpoint_store(&extended_history, extended_replay_clocks.frame,
											  &snapshot)) {
			/* Losing rewind history must not corrupt the recorded input stream. */
			return;
		}
	}
}

void extended_replay_after_step(void)
{
	if (extended_replay_clocks.frame != UINT32_MAX) {
		extended_replay_clocks.frame++;
	} else {
		race_exit_request = 1;
	}
	extended_replay_sync_legacy();
}

int extended_replay_seek(uint32_t frame)
{
	if (!extended_active || frame > extended_recording.count) {
		return 0;
	}
	const struct EXTENDED_RUNTIME_CHECKPOINT *snapshot =
		(const struct EXTENDED_RUNTIME_CHECKPOINT *)extended_replay_checkpoint_find(
			&extended_history, frame, NULL);
	if (snapshot != NULL) {
		state = snapshot->state;
		extended_replay_clocks = snapshot->clocks;
		init_kevinrandom(state.kevinseed);
	} else {
		init_game_state(GAMESTATE_INIT_NORMAL);
	}
	extended_replay_set_input_frame(frame);
	while (extended_replay_clocks.frame < frame) {
		update_gamestate();
	}
	return 1;
}

int extended_replay_branch(uint32_t frame)
{
	if (!extended_active || !extended_replay_truncate(&extended_recording, frame)) {
		return 0;
	}
	extended_replay_history_truncate(&extended_history, frame);
	if (extended_commands.count > frame) {
		extended_commands.count = frame;
	}
	extended_replay_clocks.input_frame = frame;
	extended_replay_sync_legacy();
	return 1;
}

void extended_replay_format_runtime_time(legacy_s8 *out, uint32_t frame, int fraction)
{
	if (!extended_replay_format_time((char *)out, 32U, frame, (uint16_t)framespersec, fraction)) {
		out[0] = 0;
	}
}
