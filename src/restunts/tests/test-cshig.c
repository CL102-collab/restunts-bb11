#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../c/cshig.h"

static void write32(uint8_t *data, uint32_t value)
{
	for (unsigned index = 0; index < 4U; index++) {
		data[index] = (uint8_t)(value >> (8U * index));
	}
}

static void write64(uint8_t *data, uint64_t value)
{
	write32(data, (uint32_t)value);
	write32(data + 4U, (uint32_t)(value >> 32U));
}

static struct CSHIG_ENTRY entry(const char *name, uint64_t ticks)
{
	struct CSHIG_ENTRY result;
	memset(&result, 0, sizeof(result));
	assert(strlen(name) < sizeof(result.player_name));
	memcpy(result.player_name, name, strlen(name));
	memcpy(result.car_name, "Porsche", 7U);
	memcpy(result.opponent, "Bernie", 6U);
	result.car_flag = 255U;
	result.ticks = ticks;
	return result;
}

static void make_track(struct CSTRK_TRACK *track)
{
	cstrk_init(track);
	assert(cstrk_create(track, 4U, 3U, NULL, 0));
	static const uint8_t positions[3][2] = {{3, 2}, {0, 0}, {2, 1}};
	for (unsigned index = 0; index < 3U; index++) {
		struct CSTRK_OBJECT object = {0};
		object.legacy_id = index == 0U ? 5U : 4U;
		object.x = positions[index][0];
		object.y = positions[index][1];
		object.width = object.height = object.occupied_mask = 1U;
		assert(cstrk_add_object(track, &object, NULL, 0));
	}
	assert(cstrk_set_notes(track, "original notes", 14U, NULL, 0));
}

static void assert_preserved(const struct CSHIG_TABLE *table, const struct CSHIG_TABLE *before,
							 const uint8_t *track_bytes)
{
	assert(table->track_data == before->track_data && table->track_size == before->track_size);
	assert(memcmp(table->entries, before->entries, sizeof(table->entries)) == 0);
	assert(memcmp(table->track_data, track_bytes, table->track_size) == 0);
}

static void test_identity(void)
{
	struct CSTRK_TRACK track;
	make_track(&track);
	struct CSHIG_TABLE table, other;
	cshig_init(&table);
	cshig_init(&other);
	assert(cshig_create(&table, &track, NULL, 0));
	assert(cshig_validate(&table, NULL, 0));
	for (unsigned index = 0; index < CSHIG_ENTRY_COUNT; index++) {
		assert(table.entries[index].ticks == CSHIG_EMPTY_TICKS);
		assert(table.entries[index].player_name[0] == 0U);
	}
	assert(cshig_matches_track(&table, &track));
	assert(cstrk_set_notes(&track, "new notes\n", 10U, NULL, 0));
	struct CSTRK_OBJECT temporary = track.objects[0];
	track.objects[0] = track.objects[2];
	track.objects[2] = temporary;
	assert(cshig_matches_track(&table, &track));
	assert(cshig_create(&other, &track, NULL, 0));
	assert(other.track_size == table.track_size);
	assert(memcmp(other.track_data, table.track_data, table.track_size) == 0);
	struct CSTRK_TRACK canonical;
	cstrk_init(&canonical);
	assert(cstrk_decode(table.track_data, table.track_size, &canonical, NULL, 0));
	assert(canonical.notes_length == 0U);
	cstrk_free(&canonical);
	track.cells[2].surface = CSTRK_TERRAIN_ICE;
	assert(!cshig_matches_track(&table, &track));
	track.cells[2].surface = CSTRK_TERRAIN_GRASS;
	track.cells[2].half_water_corner = CSTRK_WATER_SOUTHEAST;
	assert(!cshig_matches_track(&table, &track));
	track.cells[2].half_water_corner = CSTRK_WATER_NONE;
	track.corner_heights[4] = 1U;
	assert(!cshig_matches_track(&table, &track));
	track.corner_heights[4] = 0U;
	track.objects[0].surface = CSTRK_ROAD_DIRT;
	assert(!cshig_matches_track(&table, &track));
	track.objects[0].surface = CSTRK_ROAD_PAVED;
	track.objects[0].rotation = 1U;
	assert(!cshig_matches_track(&table, &track));
	track.objects[0].rotation = 0U;
	assert(cshig_matches_track(&table, &track));
	uint8_t *before_bytes = malloc(table.track_size);
	assert(before_bytes);
	memcpy(before_bytes, table.track_data, table.track_size);
	struct CSHIG_TABLE before = table;
	track.width = 0;
	assert(!cshig_create(&table, &track, NULL, 0));
	assert_preserved(&table, &before, before_bytes);
	track.width = 4;
	free(before_bytes);
	cshig_free(&table);
	cshig_free(&table);
	cshig_free(&other);
	cstrk_free(&track);
}

static void test_ranking_and_time(void)
{
	uint64_t normalized = 123U;
	assert(cshig_time_normalize(UINT32_MAX, 10U, &normalized));
	assert(normalized == UINT64_C(8589934590));
	assert(normalized > UINT32_MAX);
	assert(cshig_time_normalize(UINT32_MAX, 20U, &normalized));
	assert(normalized == UINT32_MAX);
	assert(!cshig_time_normalize(10U, 0U, &normalized));
	assert(normalized == UINT32_MAX);
	assert(!cshig_time_normalize(10U, 60U, &normalized));
	assert(!cshig_time_normalize(10U, 20U, NULL));
	struct CSTRK_TRACK track;
	make_track(&track);
	struct CSHIG_TABLE table;
	cshig_init(&table);
	assert(cshig_create(&table, &track, NULL, 0));
	assert(cshig_rank(&table, 0U) == -1);
	assert(cshig_rank(&table, CSHIG_EMPTY_TICKS) == -1);
	struct CSHIG_ENTRY first = entry("First", 100U), second = entry("Second", 100U);
	assert(cshig_insert(&table, &first, NULL, 0));
	assert(cshig_rank(&table, 100U) == 1);
	assert(cshig_insert(&table, &second, NULL, 0));
	assert(strcmp((char *)table.entries[0].player_name, "First") == 0);
	assert(strcmp((char *)table.entries[1].player_name, "Second") == 0);
	assert(cshig_insert(&table, &table.entries[0], NULL, 0));
	assert(strcmp((char *)table.entries[2].player_name, "First") == 0);
	for (uint64_t ticks = 101U; ticks <= 104U; ticks++) {
		struct CSHIG_ENTRY next = entry("Next", ticks);
		assert(cshig_insert(&table, &next, NULL, 0));
	}
	assert(cshig_rank(&table, 104U) == -1);
	assert(cshig_rank(&table, 100U) == 3);
	struct CSHIG_TABLE before = table;
	struct CSHIG_ENTRY slow = entry("Slow", UINT64_MAX - 1U);
	assert(!cshig_insert(&table, &slow, NULL, 0));
	assert(memcmp(table.entries, before.entries, sizeof(table.entries)) == 0);
	slow.ticks = 0U;
	assert(!cshig_insert(&table, &slow, NULL, 0));
	assert(memcmp(table.entries, before.entries, sizeof(table.entries)) == 0);
	cshig_free(&table);
	cstrk_free(&track);
}

static void test_codec_and_corruption(void)
{
	struct CSTRK_TRACK track;
	make_track(&track);
	struct CSHIG_TABLE table, decoded;
	cshig_init(&table);
	cshig_init(&decoded);
	assert(cshig_create(&table, &track, NULL, 0));
	struct CSHIG_ENTRY first = entry("Font", UINT64_C(0x123456789abcdef0));
	first.player_name[0] = 128U;
	first.car_name[7] = 0U;
	first.car_name[8] = 255U; /* Multiple bounded legacy strings remain supported. */
	assert(cshig_insert(&table, &first, NULL, 0));
	struct CSHIG_ENTRY last = entry("Last", UINT64_MAX - 1U);
	assert(cshig_insert(&table, &last, NULL, 0));
	uint8_t *data = NULL;
	size_t size = 0;
	char error[128] = "old error";
	assert(cshig_encode(&table, &data, &size, error, sizeof(error)));
	assert(error[0] == '\0');
	assert(size == CSHIG_HEADER_SIZE + table.track_size + 7U * 58U);
	assert(memcmp(data, "CSHIG\032\r\n", 8U) == 0);
	size_t records = CSHIG_HEADER_SIZE + table.track_size;
	const uint8_t expected_ticks[] = {0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12};
	assert(memcmp(data + records + 50U, expected_ticks, 8U) == 0);
	assert(cshig_decode(data, size, &decoded, error, sizeof(error)));
	assert(memcmp(decoded.entries, table.entries, sizeof(table.entries)) == 0);
	assert(cshig_matches_track(&decoded, &track));
	uint8_t *again = NULL;
	size_t again_size = 0;
	assert(cshig_encode(&decoded, &again, &again_size, NULL, 0));
	assert(size == again_size && memcmp(data, again, size) == 0);
	free(again);
	uint8_t *identity = malloc(decoded.track_size);
	uint8_t *corrupt = malloc(size + 1U);
	assert(identity && corrupt);
	memcpy(identity, decoded.track_data, decoded.track_size);
	const struct CSHIG_TABLE before = decoded;
	for (size_t truncated = 0; truncated < size; truncated++) {
		assert(!cshig_decode(data, truncated, &decoded, error, sizeof(error)));
		assert_preserved(&decoded, &before, identity);
	}
	assert(!cshig_decode(data, SIZE_MAX, &decoded, error, sizeof(error)));
	const size_t offsets[] = {0U,
							  7U,
							  8U,
							  10U,
							  12U,
							  16U,
							  20U,
							  22U,
							  24U,
							  28U,
							  CSHIG_HEADER_SIZE + 12U,
							  CSHIG_HEADER_SIZE + 32U};
	for (unsigned index = 0; index < sizeof(offsets) / sizeof(offsets[0]); index++) {
		memcpy(corrupt, data, size);
		corrupt[offsets[index]] ^= 4U;
		assert(!cshig_decode(corrupt, size, &decoded, error, sizeof(error)));
		assert_preserved(&decoded, &before, identity);
	}
	memcpy(corrupt, data, size);
	corrupt[size] = 0U;
	assert(!cshig_decode(corrupt, size + 1U, &decoded, error, sizeof(error)));
	for (unsigned field = 0; field < 3U; field++) {
		static const unsigned starts[] = {0U, 17U, 42U}, lengths[] = {17U, 24U, 8U};
		memcpy(corrupt, data, size);
		memset(corrupt + records + starts[field], 'X', lengths[field]);
		assert(!cshig_decode(corrupt, size, &decoded, error, sizeof(error)));
	}
	memcpy(corrupt, data, size);
	corrupt[records + 16U] = 1U; /* Controls are rejected even after an earlier NUL. */
	assert(!cshig_decode(corrupt, size, &decoded, error, sizeof(error)));
	memcpy(corrupt, data, size);
	corrupt[records + 42U] = 127U;
	assert(!cshig_decode(corrupt, size, &decoded, error, sizeof(error)));
	for (unsigned empty_gap = 0; empty_gap < 3U; empty_gap++) {
		memcpy(corrupt, data, size);
		write64(corrupt + records + 50U, empty_gap == 0	  ? UINT64_MAX
										 : empty_gap == 1 ? 0U
														  : UINT64_MAX - 1U);
		if (empty_gap == 2U) {
			write64(corrupt + records + CSHIG_ENTRY_SIZE + 50U, 5U);
		}
		assert(!cshig_decode(corrupt, size, &decoded, error, sizeof(error)));
	}
	/* A valid track with object records out of canonical order is rejected. */
	memcpy(corrupt, data, size);
	size_t objects = CSHIG_HEADER_SIZE + CSTRK_HEADER_SIZE + 5U * 4U + 4U * 3U * 2U;
	uint8_t object[CSTRK_OBJECT_SIZE];
	memcpy(object, corrupt + objects, sizeof(object));
	memcpy(corrupt + objects, corrupt + objects + CSTRK_OBJECT_SIZE, sizeof(object));
	memcpy(corrupt + objects + CSTRK_OBJECT_SIZE, object, sizeof(object));
	assert(!cshig_decode(corrupt, size, &decoded, error, sizeof(error)));
	/* Noncanonical notes are rejected even when every enclosing length is correct. */
	uint8_t *noted_track = NULL;
	size_t noted_size = 0;
	assert(cstrk_encode(&track, &noted_track, &noted_size, NULL, 0));
	size_t noted_file_size = CSHIG_HEADER_SIZE + noted_size + CSHIG_ENTRY_COUNT * CSHIG_ENTRY_SIZE;
	uint8_t *noted_file = malloc(noted_file_size);
	assert(noted_file);
	memcpy(noted_file, data, CSHIG_HEADER_SIZE);
	memcpy(noted_file + CSHIG_HEADER_SIZE, noted_track, noted_size);
	memcpy(noted_file + CSHIG_HEADER_SIZE + noted_size, data + records,
		   CSHIG_ENTRY_COUNT * CSHIG_ENTRY_SIZE);
	write32(noted_file + 16U, (uint32_t)noted_size);
	write32(noted_file + 24U, (uint32_t)(noted_file_size - CSHIG_HEADER_SIZE));
	assert(!cshig_decode(noted_file, noted_file_size, &decoded, error, sizeof(error)));
	assert_preserved(&decoded, &before, identity);
	uint8_t *sentinel = data;
	size_t sentinel_size = size;
	table.entries[0].ticks = 0U;
	assert(!cshig_encode(&table, &sentinel, &sentinel_size, error, sizeof(error)));
	assert(sentinel == data && sentinel_size == size);
	assert(!cshig_decode(NULL, 0, &decoded, NULL, 0));
	assert(!cshig_decode(data, size, NULL, NULL, 0));
	assert(!cshig_encode(NULL, &sentinel, &sentinel_size, NULL, 0));
	assert(!cshig_encode(&decoded, NULL, &sentinel_size, NULL, 0));
	assert(!cshig_encode(&decoded, &sentinel, NULL, NULL, 0));
	free(noted_file);
	free(noted_track);
	free(identity);
	free(corrupt);
	free(data);
	cshig_free(&table);
	cshig_free(&decoded);
	cstrk_free(&track);
}

static void test_maximum_track(void)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 90U, 90U, NULL, 0));
	track.object_count = 8100U;
	track.objects = calloc(track.object_count, sizeof(*track.objects));
	assert(track.objects);
	for (unsigned index = 0; index < track.object_count; index++) {
		struct CSTRK_OBJECT *object = &track.objects[index];
		object->legacy_id = 4U;
		object->x = (uint8_t)(index % 90U);
		object->y = (uint8_t)(index / 90U);
		object->width = object->height = object->occupied_mask = 1U;
	}
	struct CSHIG_TABLE table, decoded;
	cshig_init(&table);
	cshig_init(&decoded);
	assert(cshig_create(&table, &track, NULL, 0));
	assert(table.track_size == CSHIG_MAX_TRACK_SIZE);
	uint8_t *data = NULL;
	size_t size = 0;
	assert(cshig_encode(&table, &data, &size, NULL, 0));
	assert(size == CSHIG_MAX_FILE_SIZE);
	assert(cshig_decode(data, size, &decoded, NULL, 0));
	assert(cshig_matches_track(&decoded, &track));
	free(data);
	cshig_free(&table);
	cshig_free(&decoded);
	cstrk_free(&track);
}

static void test_text_and_tick_boundaries(void)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 1U, 1U, NULL, 0));
	struct CSHIG_TABLE table, decoded;
	cshig_init(&table);
	cshig_init(&decoded);
	assert(cshig_create(&table, &track, NULL, 0));
	struct CSHIG_ENTRY record = entry("1234567890123456", 1U);
	memset(record.car_name, 'C', sizeof(record.car_name) - 1U);
	record.car_name[sizeof(record.car_name) - 1U] = 0U;
	memset(record.opponent, 'O', sizeof(record.opponent) - 1U);
	record.opponent[sizeof(record.opponent) - 1U] = 0U;
	assert(cshig_insert(&table, &record, NULL, 0));
	uint8_t *data = NULL;
	size_t size = 0;
	assert(cshig_encode(&table, &data, &size, NULL, 0));
	assert(cshig_decode(data, size, &decoded, NULL, 0));
	assert(decoded.entries[0].ticks == 1U);
	assert(memcmp(decoded.entries, table.entries, sizeof(table.entries)) == 0);
	struct CSHIG_TABLE before = table;
	record.player_name[sizeof(record.player_name) - 1U] = 'X';
	assert(!cshig_insert(&table, &record, NULL, 0));
	record.player_name[sizeof(record.player_name) - 1U] = 0U;
	record.car_name[sizeof(record.car_name) - 1U] = 'X';
	assert(!cshig_insert(&table, &record, NULL, 0));
	record.car_name[sizeof(record.car_name) - 1U] = 0U;
	record.opponent[sizeof(record.opponent) - 1U] = 'X';
	assert(!cshig_insert(&table, &record, NULL, 0));
	assert(memcmp(table.entries, before.entries, sizeof(table.entries)) == 0);
	free(data);
	cshig_free(&table);
	cshig_free(&decoded);
	cstrk_free(&track);
}

int main(void)
{
	test_identity();
	test_ranking_and_time();
	test_codec_and_corruption();
	test_maximum_track();
	test_text_and_tick_boundaries();
	puts("CSHIG canonical identity, 64-bit times, ranking, and transactional validation passed");
	return 0;
}
