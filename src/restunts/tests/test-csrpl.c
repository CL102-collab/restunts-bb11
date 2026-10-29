#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../c/csrpl.h"
#include "../c/cstrk.h"
#include "../c/extended_replay.h"

#define TEST_FUTURE_TRACK_VERSION 101U
#define TEST_REPLAY_ERROR_CAPACITY 128U

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

static void test_codec(legacy_u8 horizon, legacy_u16 version, legacy_u8 corner_surface)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	char error[128];
	assert(cstrk_create(&track, 90U, 90U, error, sizeof(error)));
	track.horizon = horizon;
	track.version = version;
	track.cells[0].surface = CSTRK_TERRAIN_GRASS;
	track.cells[0].half_water_corner = CSTRK_WATER_SOUTHEAST;
	track.cells[0].corner_surface = corner_surface;
	assert(cstrk_set_notes(&track, "embedded track", 14U, error, sizeof(error)));
	uint8_t *track_bytes = NULL;
	size_t track_size = 0U;
	assert(cstrk_encode(&track, &track_bytes, &track_size, error, sizeof(error)));

	struct CSRPL_METADATA metadata;
	memset(&metadata, 0, sizeof(metadata));
	metadata.version = CSRPL_VERSION;
	metadata.driving_rules =
		DRIVING_RULE_POWER_GEAR | DRIVING_RULE_LEFT_CORNER_BIAS | DRIVING_RULE_OWOOT;
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
	assert(csrpl_encode(&metadata, track_bytes, track_size, inputs, metadata.frame_count, &encoded,
						&size, error, sizeof(error)));
	assert(size == CSRPL_HEADER_SIZE + track_size + metadata.frame_count);
	/* The count uses all four bytes and is independent of the host ABI. */
	assert(encoded[20] == 0xa3U && encoded[21] == 0x86U && encoded[22] == 1U && encoded[23] == 0U);
	struct CSRPL_VIEW view;
	memset(&view, 0, sizeof(view));
	assert(csrpl_decode(encoded, size, &view, error, sizeof(error)));
	assert(view.metadata.version == metadata.version);
	assert(view.metadata.driving_rules == metadata.driving_rules);
	assert(view.metadata.frame_count == metadata.frame_count);
	assert(view.metadata.player_material == 129U);
	assert(memcmp(view.track_data, track_bytes, track_size) == 0);
	assert(memcmp(view.inputs, inputs, metadata.frame_count) == 0);
	struct CSTRK_TRACK embedded;
	cstrk_init(&embedded);
	assert(cstrk_decode(view.track_data, view.track_size, &embedded, error, sizeof(error)));
	assert(embedded.horizon == horizon);
	assert(embedded.version == (version < CSTRK_VERSION ? CSTRK_VERSION : version));
	assert(embedded.cells[0].surface == CSTRK_TERRAIN_GRASS);
	assert(embedded.cells[0].half_water_corner == CSTRK_WATER_SOUTHEAST);
	assert(embedded.cells[0].corner_surface == corner_surface);
	assert(strcmp(embedded.notes, track.notes) == 0);
	cstrk_free(&embedded);
	struct CSRPL_VIEW unchanged = view;
	for (size_t short_size = 0U; short_size < CSRPL_HEADER_SIZE; short_size++) {
		assert(!csrpl_decode(encoded, short_size, &view, error, sizeof(error)));
		assert(memcmp(&view, &unchanged, sizeof(view)) == 0);
	}
	assert(!csrpl_decode(encoded, size - 1U, &view, error, sizeof(error)));
	assert(!csrpl_decode(encoded, size + 1U, &view, error, sizeof(error)));
	const size_t corrupt_offsets[] = {0U, 10U, 12U, 50U};
	for (size_t index = 0U; index < sizeof(corrupt_offsets) / sizeof(corrupt_offsets[0]); index++) {
		size_t offset = corrupt_offsets[index];
		encoded[offset] ^= 1U;
		assert(!csrpl_decode(encoded, size, &view, error, sizeof(error)));
		encoded[offset] ^= 1U;
	}
	for (legacy_u16 old_version = 0; old_version < CSTRK_MIN_VERSION; old_version++) {
		LEGACY_WRITE_U16_LE(encoded + CSRPL_HEADER_SIZE + CSTRK_VERSION_OFFSET, old_version);
		assert(!csrpl_decode(encoded, size, &view, error, sizeof(error)));
		assert(strstr(error, "Unsupported track version") != NULL);
		assert(memcmp(&view, &unchanged, sizeof(view)) == 0);
	}
	LEGACY_WRITE_U16_LE(encoded + CSRPL_HEADER_SIZE + CSTRK_VERSION_OFFSET, version);
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
	for (legacy_u16 old_version = 0; old_version < CSTRK_MIN_VERSION; old_version++) {
		LEGACY_WRITE_U16_LE(track_bytes + CSTRK_VERSION_OFFSET, old_version);
		assert(!csrpl_encode(&metadata, track_bytes, track_size, inputs, metadata.frame_count,
							 &sentinel, &sentinel_size, error, sizeof(error)));
		assert(strstr(error, "Unsupported track version") != NULL);
		assert(sentinel == encoded && sentinel_size == size);
	}
	LEGACY_WRITE_U16_LE(track_bytes + CSTRK_VERSION_OFFSET, version);
	size_t output_size = 123U;
	assert(!csrpl_encoded_size(SIZE_MAX, UINT32_MAX, &output_size));
	assert(output_size == 123U);
	assert(!csrpl_encoded_size(0U, 1U, &output_size));
	free(encoded);
	free(inputs);

	metadata.frame_count = 0U;
	metadata.frames_per_second = 10U;
	assert(csrpl_encode(&metadata, track_bytes, track_size, NULL, 0U, &encoded, &size, error,
						sizeof(error)));
	assert(csrpl_decode(encoded, size, &view, error, sizeof(error)));
	assert(view.metadata.frame_count == 0U);
	free(encoded);
	free(track_bytes);
	cstrk_free(&track);
}

static void test_overpass_surfaces_in_replay(legacy_u16 track_version)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	legacy_char error[TEST_REPLAY_ERROR_CAPACITY];
	assert(cstrk_create(&track, 1U, 1U, error, sizeof(error)));
	track.version = track_version;
	legacy_u8 base_height =
		track_version < CSTRK_NINE_LEVEL_VERSION ? CSTRK_LEGACY_MAX_HEIGHT : CSTRK_MAX_HEIGHT;
	memset(track.corner_heights, base_height, (size_t)(track.width + 1U) * (track.height + 1U));
	legacy_u8 expected_lower =
		track_version < CSTRK_LOWER_SURFACE_VERSION ? CSTRK_ROAD_DIRT : CSTRK_ROAD_ICE;
	struct CSTRK_OBJECT object = {0};
	object.legacy_id = CSTRK_OVERPASS_FIRST_ID;
	object.width = object.height = object.occupied_mask = 1;
	object.surface = CSTRK_ROAD_DIRT;
	object.lower_surface = expected_lower;
	object.base_height = base_height;
	assert(cstrk_add_object(&track, &object, error, sizeof(error)));
	legacy_u8 *track_data = NULL;
	size_t track_size = 0;
	assert(cstrk_encode(&track, &track_data, &track_size, error, sizeof(error)));
	static const legacy_u8 inputs[] = {1U, 2U};
	struct CSRPL_METADATA metadata = {0};
	metadata.version = CSRPL_VERSION;
	metadata.frames_per_second = 20U;
	metadata.frame_count = sizeof(inputs);
	legacy_u8 *encoded = NULL;
	size_t size = 0;
	assert(csrpl_encode(&metadata, track_data, track_size, inputs, sizeof(inputs), &encoded, &size,
						error, sizeof(error)));
	struct CSRPL_VIEW view;
	assert(csrpl_decode(encoded, size, &view, error, sizeof(error)));
	assert(view.metadata.version == CSRPL_VERSION);
	struct CSTRK_TRACK embedded;
	cstrk_init(&embedded);
	assert(cstrk_decode(view.track_data, view.track_size, &embedded, error, sizeof(error)));
	assert(embedded.version == CSTRK_VERSION && embedded.object_count == 1U);
	assert(embedded.objects[0].surface == CSTRK_ROAD_DIRT);
	assert(embedded.objects[0].base_height == base_height);
	for (size_t vertex = 0; vertex < (size_t)(embedded.width + 1U) * (embedded.height + 1U);
		 vertex++) {
		assert(embedded.corner_heights[vertex] == base_height);
	}
	/* A v3 overpass used its one selected material for both roads. */
	assert(embedded.objects[0].lower_surface == expected_lower);
	/* Resaving keeps the lower road choice and the recording's revision. */
	legacy_u8 *saved = NULL;
	size_t saved_size = 0;
	assert(csrpl_encode(&view.metadata, view.track_data, view.track_size, view.inputs,
						view.metadata.frame_count, &saved, &saved_size, error, sizeof(error)));
	assert(saved_size == size && memcmp(saved, encoded, size) == 0);
	free(saved);
	free(encoded);
	free(track_data);
	cstrk_free(&embedded);
	cstrk_free(&track);
}

static void test_recorded_versions(void)
{
	static const legacy_u16 track_versions[] = {CSTRK_MIN_VERSION, CSTRK_LOWER_SURFACE_VERSION,
												CSTRK_VERSION, TEST_FUTURE_TRACK_VERSION};
	static const legacy_u16 replay_versions[] = {CSRPL_VERSION, CSRPL_VERSION + 1U, 15U, 101U,
												 LEGACY_U16_MAX};
	static const legacy_u8 inputs[] = {1U, 2U};
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	char error[128];
	assert(cstrk_create(&track, 1U, 1U, error, sizeof(error)));
	for (size_t track_index = 0; track_index < sizeof(track_versions) / sizeof(track_versions[0]);
		 track_index++) {
		track.version = track_versions[track_index];
		legacy_u8 *track_data = NULL;
		size_t track_size = 0;
		assert(cstrk_encode(&track, &track_data, &track_size, error, sizeof(error)));
		for (size_t index = 0; index < sizeof(replay_versions) / sizeof(replay_versions[0]);
			 index++) {
			struct CSRPL_METADATA metadata = {0};
			metadata.version = replay_versions[index];
			metadata.frames_per_second = 20U;
			metadata.frame_count = sizeof(inputs);
			legacy_u8 *encoded = NULL;
			size_t size = 0;
			assert(csrpl_encode(&metadata, track_data, track_size, inputs, sizeof(inputs), &encoded,
								&size, error, sizeof(error)));
			assert(LEGACY_READ_U16_LE(encoded + CSRPL_VERSION_OFFSET) == metadata.version);
			struct CSRPL_VIEW view;
			memset(&view, 0, sizeof(view));
			assert(csrpl_decode(encoded, size, &view, error, sizeof(error)));
			assert(view.metadata.version == metadata.version);
			assert(LEGACY_READ_U16_LE(view.track_data + CSTRK_VERSION_OFFSET) == track.version);
			assert(view.track_size == track_size &&
				   memcmp(view.track_data, track_data, track_size) == 0);
			assert(view.metadata.frame_count == sizeof(inputs));
			assert(memcmp(view.inputs, inputs, sizeof(inputs)) == 0);
			/* Saving a decoded replay preserves its own revision even when the
			 * embedded track or the running game's revision is different. */
			legacy_u8 *saved = NULL;
			size_t saved_size = 0;
			assert(csrpl_encode(&view.metadata, view.track_data, view.track_size, view.inputs,
								view.metadata.frame_count, &saved, &saved_size, error,
								sizeof(error)));
			assert(saved_size == size && memcmp(saved, encoded, size) == 0);
			free(saved);
			struct CSRPL_VIEW unchanged = view;
			/* Every revision older than the running game is unsupported. */
			for (legacy_u16 version = 0; version < CSRPL_VERSION; version++) {
				LEGACY_WRITE_U16_LE(encoded + CSRPL_VERSION_OFFSET, version);
				assert(!csrpl_decode(encoded, size, &view, error, sizeof(error)));
				assert(strstr(error, "Unsupported Chocolate Stunts replay version") != NULL);
				assert(memcmp(&view, &unchanged, sizeof(view)) == 0);
				metadata.version = version;
				legacy_u8 *sentinel = encoded;
				size_t sentinel_size = size;
				assert(!csrpl_encode(&metadata, track_data, track_size, inputs, sizeof(inputs),
									 &sentinel, &sentinel_size, error, sizeof(error)));
				assert(strstr(error, "Unsupported Chocolate Stunts replay version") != NULL);
				assert(sentinel == encoded && sentinel_size == size);
			}
			LEGACY_WRITE_U16_LE(encoded + CSRPL_VERSION_OFFSET, replay_versions[index]);
			/* Accepting an unfamiliar revision never relaxes structural checks. */
			encoded[12] = 1U;
			assert(!csrpl_decode(encoded, size, &view, error, sizeof(error)));
			assert(memcmp(&view, &unchanged, sizeof(view)) == 0);
			encoded[12] = 0;
			assert(csrpl_decode(encoded, size, &view, error, sizeof(error)));
			assert(view.metadata.version == replay_versions[index]);
			free(encoded);
		}
		free(track_data);
	}
	cstrk_free(&track);
}

static void test_recorded_driving_rules(void)
{
	static const legacy_u16 versions[] = {CSRPL_VERSION, CSRPL_VERSION + 1U};
	static const legacy_u8 inputs[] = {1U, 2U};
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	legacy_char error[128];
	assert(cstrk_create(&track, 1U, 1U, error, sizeof(error)));
	legacy_u8 *track_data = NULL;
	size_t track_size = 0;
	assert(cstrk_encode(&track, &track_data, &track_size, error, sizeof(error)));
	for (size_t index = 0; index < sizeof(versions) / sizeof(versions[0]); index++) {
		for (legacy_u16 rules = 0; rules <= DRIVING_RULE_KNOWN_MASK; rules++) {
			struct CSRPL_METADATA metadata = {0};
			metadata.version = versions[index];
			metadata.driving_rules = rules;
			metadata.frames_per_second = 20U;
			metadata.frame_count = sizeof(inputs);
			legacy_u8 *encoded = NULL;
			size_t size = 0;
			assert(csrpl_encode(&metadata, track_data, track_size, inputs, sizeof(inputs), &encoded,
								&size, error, sizeof(error)));
			assert(LEGACY_READ_U16_LE(encoded + CSRPL_DRIVING_RULES_OFFSET) == rules);
			struct CSRPL_VIEW view;
			memset(&view, 0, sizeof(view));
			assert(csrpl_decode(encoded, size, &view, error, sizeof(error)));
			assert(view.metadata.version == metadata.version);
			assert(view.metadata.driving_rules == rules);
			assert(view.track_size == track_size &&
				   memcmp(view.track_data, track_data, track_size) == 0);
			assert(view.metadata.frame_count == sizeof(inputs));
			assert(memcmp(view.inputs, inputs, sizeof(inputs)) == 0);
			/* All-off is a real recorded rule set, not an absent-field default. */
			legacy_u8 *saved = NULL;
			size_t saved_size = 0;
			assert(csrpl_encode(&view.metadata, view.track_data, view.track_size, view.inputs,
								view.metadata.frame_count, &saved, &saved_size, error,
								sizeof(error)));
			assert(saved_size == size && memcmp(saved, encoded, size) == 0);
			free(saved);
			if (rules == DRIVING_RULE_KNOWN_MASK) {
				struct CSRPL_VIEW unchanged = view;
				for (legacy_u16 flag = DRIVING_RULE_KNOWN_MASK + 1U; flag != 0U;
					 flag = (legacy_u16)(flag << 1U)) {
					metadata.driving_rules = rules | flag;
					LEGACY_WRITE_U16_LE(encoded + CSRPL_DRIVING_RULES_OFFSET,
										metadata.driving_rules);
					assert(!csrpl_decode(encoded, size, &view, error, sizeof(error)));
					assert(strstr(error, "driving rules") != NULL);
					assert(memcmp(&view, &unchanged, sizeof(view)) == 0);
					legacy_u8 *sentinel = encoded;
					size_t sentinel_size = size;
					assert(!csrpl_encode(&metadata, track_data, track_size, inputs, sizeof(inputs),
										 &sentinel, &sentinel_size, error, sizeof(error)));
					assert(strstr(error, "driving rules") != NULL);
					assert(sentinel == encoded && sentinel_size == size);
				}
				LEGACY_WRITE_U16_LE(encoded + CSRPL_DRIVING_RULES_OFFSET, rules);
				assert(csrpl_decode(encoded, size, &view, error, sizeof(error)));
				assert(view.metadata.driving_rules == rules);
			}
			free(encoded);
		}
	}
	free(track_data);
	cstrk_free(&track);
}

int main(void)
{
	test_recording_and_seek_storage();
	test_large_timeline();
	test_recorded_versions();
	test_overpass_surfaces_in_replay(CSTRK_MIN_VERSION);
	test_overpass_surfaces_in_replay(CSTRK_LOWER_SURFACE_VERSION);
	test_overpass_surfaces_in_replay(CSTRK_VERSION);
	test_recorded_driving_rules();
	/* Replay storage preserves wire revisions while decoding upgrades old tracks. */
	test_codec(CSTRK_HORIZON_DEFAULT, CSTRK_MIN_VERSION, CSTRK_TERRAIN_WATER);
	test_codec(CSTRK_HORIZON_DEFAULT, CSTRK_VERSION, CSTRK_TERRAIN_WATER);
	test_codec(CSTRK_HORIZON_DEFAULT, CSTRK_VERSION, CSTRK_TERRAIN_DIRT);
	test_codec(CSTRK_HORIZON_COUNTRY, TEST_FUTURE_TRACK_VERSION, CSTRK_TERRAIN_ICE);
	return 0;
}
