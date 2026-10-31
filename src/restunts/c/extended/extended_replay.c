#include "extended_replay.h"
#include "csrpl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void extended_replay_init(struct EXTENDED_REPLAY_BUFFER *buffer)
{
	memset(buffer, 0, sizeof(*buffer));
}

void extended_replay_free(struct EXTENDED_REPLAY_BUFFER *buffer)
{
	free(buffer->inputs);
	extended_replay_init(buffer);
}

static uint32_t extended_replay_grown_capacity(uint32_t current, uint32_t needed)
{
	uint32_t capacity = current == 0U ? 1024U : current;
	while (capacity < needed) {
		if (capacity > UINT32_MAX / 2U) {
			return needed;
		}
		capacity *= 2U;
	}
	return capacity;
}

int extended_replay_reserve(struct EXTENDED_REPLAY_BUFFER *buffer, uint32_t needed)
{
	if (needed <= buffer->capacity) {
		return 1;
	}
	uint32_t capacity = extended_replay_grown_capacity(buffer->capacity, needed);
#if SIZE_MAX < UINT32_MAX
	if (capacity > SIZE_MAX) {
		return 0;
	}
#endif
	uint8_t *inputs = (uint8_t *)realloc(buffer->inputs, (size_t)capacity);
	if (inputs == NULL) {
		return 0;
	}
	buffer->inputs = inputs;
	buffer->capacity = capacity;
	return 1;
}

int extended_replay_assign(struct EXTENDED_REPLAY_BUFFER *buffer, const uint8_t *inputs,
						   uint32_t count)
{
	if (count != 0U && inputs == NULL) {
		return 0;
	}
	for (uint32_t index = 0U; index < count; index++) {
		if ((inputs[index] & (uint8_t)~CSRPL_INPUT_MASK) != 0U) {
			return 0;
		}
	}
	/* Allocate before releasing existing storage so assignment may borrow it. */
	uint8_t *copy = NULL;
	if (count != 0U) {
		copy = (uint8_t *)malloc(count);
		if (copy == NULL) {
			return 0;
		}
		memcpy(copy, inputs, count);
	}
	free(buffer->inputs);
	buffer->inputs = copy;
	buffer->count = count;
	buffer->capacity = count;
	return 1;
}

int extended_replay_append(struct EXTENDED_REPLAY_BUFFER *buffer, uint8_t input)
{
	if ((input & (uint8_t)~CSRPL_INPUT_MASK) != 0U || buffer->count == UINT32_MAX ||
		!extended_replay_reserve(buffer, buffer->count + 1U)) {
		return 0;
	}
	buffer->inputs[buffer->count++] = input;
	return 1;
}

int extended_replay_replace(struct EXTENDED_REPLAY_BUFFER *buffer, uint32_t frame, uint8_t input)
{
	if (frame >= buffer->count || (input & (uint8_t)~CSRPL_INPUT_MASK) != 0U) {
		return 0;
	}
	buffer->inputs[frame] = input;
	return 1;
}

int extended_replay_truncate(struct EXTENDED_REPLAY_BUFFER *buffer, uint32_t frame)
{
	if (frame > buffer->count) {
		return 0;
	}
	buffer->count = frame;
	return 1;
}

void extended_replay_history_init(struct EXTENDED_REPLAY_HISTORY *history, size_t state_size)
{
	memset(history, 0, sizeof(*history));
	history->state_size = state_size;
}

void extended_replay_history_free(struct EXTENDED_REPLAY_HISTORY *history)
{
	for (uint32_t index = 0U; index < history->count; index++) {
		free(history->checkpoints[index].state);
	}
	free(history->checkpoints);
	extended_replay_history_init(history, history->state_size);
}

/* Index of the first checkpoint strictly later than frame. */
static uint32_t extended_replay_checkpoint_end(const struct EXTENDED_REPLAY_HISTORY *history,
											   uint32_t frame)
{
	uint32_t low = 0U, high = history->count;
	while (low < high) {
		uint32_t middle = low + (high - low) / 2U;
		if (history->checkpoints[middle].frame <= frame) {
			low = middle + 1U;
		} else {
			high = middle;
		}
	}
	return low;
}

void extended_replay_history_truncate(struct EXTENDED_REPLAY_HISTORY *history, uint32_t frame)
{
	uint32_t count = extended_replay_checkpoint_end(history, frame);
	for (uint32_t index = count; index < history->count; index++) {
		free(history->checkpoints[index].state);
	}
	history->count = count;
}

int extended_replay_checkpoint_store(struct EXTENDED_REPLAY_HISTORY *history, uint32_t frame,
									 const void *state)
{
	if (state == NULL || history->state_size == 0U) {
		return 0;
	}
	uint32_t index = extended_replay_checkpoint_end(history, frame);
	if (index != 0U && history->checkpoints[index - 1U].frame == frame) {
		index--;
	}
	if (index == UINT32_MAX) {
		return 0;
	}
	void *snapshot = malloc(history->state_size);
	if (snapshot == NULL) {
		return 0;
	}
	memcpy(snapshot, state, history->state_size);
	if (index == history->capacity) {
		uint32_t capacity = extended_replay_grown_capacity(history->capacity, index + 1U);
		if (sizeof(*history->checkpoints) > SIZE_MAX / (size_t)capacity) {
			free(snapshot);
			return 0;
		}
		struct EXTENDED_REPLAY_CHECKPOINT *checkpoints =
			(struct EXTENDED_REPLAY_CHECKPOINT *)realloc(history->checkpoints,
														 (size_t)capacity * sizeof(*checkpoints));
		if (checkpoints == NULL) {
			free(snapshot);
			return 0;
		}
		history->checkpoints = checkpoints;
		history->capacity = capacity;
	}
	/* Rewriting the past invalidates every later checkpoint. */
	for (uint32_t obsolete = index; obsolete < history->count; obsolete++) {
		free(history->checkpoints[obsolete].state);
	}
	history->checkpoints[index].frame = frame;
	history->checkpoints[index].state = snapshot;
	history->count = index + 1U;
	return 1;
}

const void *extended_replay_checkpoint_find(const struct EXTENDED_REPLAY_HISTORY *history,
											uint32_t frame, uint32_t *checkpoint_frame)
{
	uint32_t end = extended_replay_checkpoint_end(history, frame);
	if (end == 0U) {
		return NULL;
	}
	if (checkpoint_frame != NULL) {
		*checkpoint_frame = history->checkpoints[end - 1U].frame;
	}
	return history->checkpoints[end - 1U].state;
}

uint32_t extended_replay_timeline_position(uint32_t frame, uint32_t recorded_frames, uint32_t width)
{
	if (recorded_frames == 0U) {
		return 0U;
	}
	if (frame > recorded_frames) {
		frame = recorded_frames;
	}
	return (uint32_t)(((uint64_t)frame * width) / recorded_frames);
}

uint32_t extended_replay_rewind_interpolate(uint32_t amount, uint32_t remaining, uint32_t distance)
{
	if (distance == 0U) {
		return 0U;
	}
	if (remaining > distance) {
		remaining = distance;
	}
	return (uint32_t)(((uint64_t)amount * remaining) / distance);
}

int extended_replay_format_time(char *out, size_t size, uint32_t frame, uint16_t fps,
								int include_fraction)
{
	if (out == NULL || size == 0U || (fps != 10U && fps != 20U)) {
		return 0;
	}
	uint32_t seconds = frame / fps;
	unsigned fraction = (unsigned)((frame % fps) * 100U / fps);
	char text[32];
	int length;
	if (include_fraction) {
		length = snprintf(text, sizeof(text), "%lu:%02u.%02u", (unsigned long)(seconds / 60U),
						  (unsigned)(seconds % 60U), fraction);
	} else {
		length = snprintf(text, sizeof(text), "%lu:%02u", (unsigned long)(seconds / 60U),
						  (unsigned)(seconds % 60U));
	}
	if (length < 0 || (size_t)length >= size || (size_t)length >= sizeof(text)) {
		return 0;
	}
	memcpy(out, text, (size_t)length + 1U);
	return 1;
}
