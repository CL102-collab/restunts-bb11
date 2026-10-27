#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../c/csrpl.h"
#include "../c/cstrk.h"
#include "../c/extended_replay.h"

static void test_recording_and_seek_storage(void)
{
	struct EXTENDED_REPLAY_BUFFER buffer;
	extended_replay_init(&buffer);
	for (uint32_t index = 0U; index < 100003U; index++) {
		assert(extended_replay_append(&buffer, (uint8_t)(index & 63U)));
	}
	assert(buffer.count == 100003U);
	assert(buffer.inputs[12000] == (12000U & 63U));
	assert(buffer.inputs[65536] == 0U);
	assert(!extended_replay_append(&buffer, 64U));
	assert(buffer.count == 100003U);
	assert(extended_replay_replace(&buffer, 99999U, 63U));
	assert(!extended_replay_replace(&buffer, buffer.count, 0U));
	assert(extended_replay_truncate(&buffer, 70001U));
	assert(extended_replay_append(&buffer, 17U));
	assert(buffer.inputs[70001U] == 17U && buffer.count == 70002U);
	assert(!extended_replay_truncate(&buffer, 70003U));
	/* Assign may borrow the old recording, including an overlapping slice. */
	assert(extended_replay_assign(&buffer, buffer.inputs + 65536U, 100U));
	assert(buffer.count == 100U && buffer.inputs[0] == 0U);
	extended_replay_free(&buffer);
	extended_replay_free(&buffer);
	assert(buffer.inputs == NULL && buffer.count == 0U);

	struct EXTENDED_REPLAY_HISTORY history;
	extended_replay_history_init(&history, sizeof(uint32_t));
	uint32_t value = 11U, frame = 999U;
	assert(!extended_replay_checkpoint_find(&history, 100000U, &frame));
	assert(frame == 999U);
	assert(extended_replay_checkpoint_store(&history, 0U, &value));
	value = 22U;
	assert(extended_replay_checkpoint_store(&history, 70000U, &value));
	value = 33U;
	assert(extended_replay_checkpoint_store(&history, 100000U, &value));
	const uint32_t *found = extended_replay_checkpoint_find(&history, 99999U, &frame);
	assert(found && *found == 22U && frame == 70000U);
	value = 44U;
	assert(extended_replay_checkpoint_store(&history, 70000U, &value));
	assert(history.count == 2U);
	found = extended_replay_checkpoint_find(&history, UINT32_MAX, &frame);
	assert(found && *found == 44U && frame == 70000U);
	extended_replay_history_truncate(&history, 69999U);
	assert(history.count == 1U);
	extended_replay_history_free(&history);
	extended_replay_history_free(&history);
}

static void test_large_timeline(void)
{
	assert(extended_replay_timeline_position(0U, 0U, 110U) == 0U);
	assert(extended_replay_timeline_position(35000U, 70000U, 110U) == 55U);
	assert(extended_replay_timeline_position(UINT32_MAX, UINT32_MAX, UINT32_MAX) == UINT32_MAX);
	assert(extended_replay_timeline_position(UINT32_MAX, 100U, 110U) == 110U);
	assert(extended_replay_rewind_interpolate(UINT32_MAX, UINT32_MAX, UINT32_MAX) == UINT32_MAX);
	assert(extended_replay_rewind_interpolate(100000U, 50000U, 100000U) == 50000U);
	assert(extended_replay_rewind_interpolate(100U, 200U, 100U) == 100U);
	assert(extended_replay_rewind_interpolate(100U, 1U, 0U) == 0U);
	char text[32] = "unchanged";
	assert(extended_replay_format_time(text, sizeof(text), 72005U, 20U, 1));
	assert(strcmp(text, "60:00.25") == 0);
	assert(extended_replay_format_time(text, sizeof(text), UINT32_MAX, 10U, 1));
	assert(strcmp(text, "7158278:49.50") == 0);
	assert(!extended_replay_format_time(text, 2U, 100U, 20U, 1));
	assert(strcmp(text, "7158278:49.50") == 0);
	assert(!extended_replay_format_time(text, sizeof(text), 100U, 0U, 1));
}

static void test_codec(void)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	char error[128];
	assert(cstrk_create(&track, 90U, 90U, error, sizeof(error)));
	assert(cstrk_set_notes(&track, "embedded track", 14U, error, sizeof(error)));
	uint8_t *track_bytes = NULL;
	size_t track_size = 0U;
	assert(cstrk_encode(&track, &track_bytes, &track_size, error, sizeof(error)));

	struct CSRPL_METADATA metadata;
	memset(&metadata, 0, sizeof(metadata));
	memcpy(metadata.player_car_id, "P962", 4U);
	metadata.player_material = 129U;
	metadata.player_transmission = 1U;
	memcpy(metadata.opponent_car_id, "ANSX", 4U);
	memcpy(metadata.track_name, "EXAMPLE", 7U);
	metadata.frames_per_second = 20U;
	metadata.frame_count = 100003U;
	uint8_t *inputs = malloc(metadata.frame_count);
	assert(inputs);
	for (uint32_t frame = 0; frame < metadata.frame_count; frame++) {
		inputs[frame] = (uint8_t)(frame & 63U);
	}
	uint8_t *encoded = NULL;
	size_t size = 0U;
	assert(csrpl_encode(&metadata, track_bytes, track_size, inputs, metadata.frame_count,
		&encoded, &size, error, sizeof(error)));
	assert(size == CSRPL_HEADER_SIZE + track_size + metadata.frame_count);
	/* The count uses all four bytes and is independent of the host ABI. */
	assert(encoded[20] == 0xa3U && encoded[21] == 0x86U && encoded[22] == 1U && encoded[23] == 0U);
	struct CSRPL_VIEW view;
	memset(&view, 0, sizeof(view));
	assert(csrpl_decode(encoded, size, &view, error, sizeof(error)));
	assert(view.metadata.frame_count == metadata.frame_count);
	assert(view.metadata.player_material == 129U);
	assert(memcmp(view.track_data, track_bytes, track_size) == 0);
	assert(memcmp(view.inputs, inputs, metadata.frame_count) == 0);
	struct CSRPL_VIEW unchanged = view;
	for (size_t short_size = 0U; short_size < CSRPL_HEADER_SIZE; short_size++) {
		assert(!csrpl_decode(encoded, short_size, &view, error, sizeof(error)));
		assert(memcmp(&view, &unchanged, sizeof(view)) == 0);
	}
	assert(!csrpl_decode(encoded, size - 1U, &view, error, sizeof(error)));
	assert(!csrpl_decode(encoded, size + 1U, &view, error, sizeof(error)));
	const size_t corrupt_offsets[] = {0U, 8U, 10U, 12U, 26U, 50U};
	for (size_t index = 0U; index < sizeof(corrupt_offsets) / sizeof(corrupt_offsets[0]); index++) {
		size_t offset = corrupt_offsets[index];
		encoded[offset] ^= 1U;
		assert(!csrpl_decode(encoded, size, &view, error, sizeof(error)));
		encoded[offset] ^= 1U;
	}
	encoded[CSRPL_HEADER_SIZE] ^= 1U;
	assert(!csrpl_decode(encoded, size, &view, error, sizeof(error)));
	encoded[CSRPL_HEADER_SIZE] ^= 1U;
	encoded[size - 1U] = 64U;
	assert(!csrpl_decode(encoded, size, &view, error, sizeof(error)));
	encoded[size - 1U] = inputs[metadata.frame_count - 1U];
	assert(csrpl_decode(encoded, size, &view, error, sizeof(error)));
	uint8_t *sentinel = encoded;
	size_t sentinel_size = size;
	assert(!csrpl_encode(&metadata, track_bytes, track_size, inputs, metadata.frame_count - 1U,
		&sentinel, &sentinel_size, error, sizeof(error)));
	assert(sentinel == encoded && sentinel_size == size);
	size_t output_size = 123U;
	assert(!csrpl_encoded_size(SIZE_MAX, UINT32_MAX, &output_size));
	assert(output_size == 123U);
	assert(!csrpl_encoded_size(0U, 1U, &output_size));
	free(encoded);
	free(inputs);

	metadata.frame_count = 0U;
	metadata.frames_per_second = 10U;
	assert(csrpl_encode(&metadata, track_bytes, track_size, NULL, 0U,
		&encoded, &size, error, sizeof(error)));
	assert(csrpl_decode(encoded, size, &view, error, sizeof(error)));
	assert(view.metadata.frame_count == 0U);
	free(encoded);
	free(track_bytes);
	cstrk_free(&track);
}

int main(void)
{
	test_recording_and_seek_storage();
	test_large_timeline();
	test_codec();
	return 0;
}
