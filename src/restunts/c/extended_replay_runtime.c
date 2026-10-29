#include "extended_replay_runtime.h"
#include "extended_replay.h"
#include "externs.h"
#include "crash_state.h"
#include "game_input.h"
#include "driving_rules.h"

#include <string.h>

struct EXTENDED_REPLAY_CLOCKS extended_replay_clocks;

struct EXTENDED_RUNTIME_CHECKPOINT {
	struct GAMESTATE state;
	struct EXTENDED_REPLAY_CLOCKS clocks;
};

static int extended_active;
static legacy_u16 extended_version;
static legacy_u16 extended_driving_rules;
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

void extended_replay_runtime_suspend(struct EXTENDED_REPLAY_SUSPENDED *saved)
{
	saved->active = extended_active;
	saved->version = extended_version;
	saved->driving_rules = extended_driving_rules;
	saved->clocks = extended_replay_clocks;
	saved->recording = extended_recording;
	saved->history = extended_history;
	extended_active = 0;
	extended_version = 0;
	extended_driving_rules = 0;
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
	extended_replay_free(&extended_recording);
	extended_replay_history_free(&extended_history);
	memset(&extended_replay_clocks, 0, sizeof(extended_replay_clocks));
	extended_replay_sync_legacy();
}

void extended_replay_runtime_clear(void)
{
	extended_active = 0;
	extended_version = 0;
	extended_driving_rules = 0;
	extended_replay_free(&extended_recording);
	extended_replay_history_free(&extended_history);
	memset(&extended_replay_clocks, 0, sizeof(extended_replay_clocks));
}

int extended_replay_runtime_load(const struct CSRPL_VIEW *view)
{
	if (view == NULL || view->metadata.version < CSRPL_MIN_VERSION ||
		(view->metadata.driving_rules & (legacy_u16)~DRIVING_RULE_KNOWN_MASK) != 0U ||
		!extended_replay_assign(&extended_recording, view->inputs, view->metadata.frame_count)) {
		return 0;
	}
	extended_active = 1;
	extended_version = view->metadata.version;
	extended_driving_rules = view->metadata.driving_rules;
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
