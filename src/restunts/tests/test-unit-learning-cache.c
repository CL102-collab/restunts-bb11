#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/opponent/unit_learning_cache.h"
#include "../c/platform.h"

#define TEST_PIECES 3U
#define TEST_COMMANDS 2U
#define TEST_HEADER_BYTES 48U
#define TEST_METADATA_BYTES 96U
#define TEST_CHECKSUM_OFFSET 40U
#define TEST_CHECKSUM_BYTES 8U
#define TEST_PIECE_RAMP_OFFSET 32U
#define TEST_LARGE_PIECES 2048U
#define TEST_HASH_OFFSET 14695981039346656037ULL
#define TEST_HASH_PRIME 1099511628211ULL
#define TEST_HANDLE 5U
#define TEST_PATH_BYTES 1024U

static legacy_u8 *stored, *temporary;
static legacy_u32 stored_size, temporary_size, position;
static legacy_u8 creating, opened, fail_write, fail_read, fail_close, fail_replace, fail_open;
static legacy_u32 replacements, removals, writes;
static legacy_char stored_path[TEST_PATH_BYTES];

legacy_u16 dos_file_open(const legacy_s8 *path, legacy_s16 create)
{
	assert(!opened);
	if (fail_open || (create == DOS_FILE_OPEN_EXISTING &&
					  (!stored || strcmp((const legacy_char *)path, stored_path)))) {
		return 0;
	}
	creating = create == DOS_FILE_CREATE;
	if (creating) {
		free(temporary);
		temporary = NULL;
		temporary_size = 0;
	}
	position = 0;
	opened = 1;
	return TEST_HANDLE;
}

legacy_s16 dos_file_close(legacy_u16 handle)
{
	assert(opened && handle == TEST_HANDLE);
	opened = 0;
	return fail_close ? -1 : 0;
}

legacy_s16 dos_file_seek(legacy_u16 handle, legacy_s32 offset, legacy_s16 origin)
{
	assert(opened && !creating && handle == TEST_HANDLE && offset == 0);
	assert(origin == DOS_FILE_SEEK_END || origin == DOS_FILE_SEEK_BEGIN);
	position = origin == DOS_FILE_SEEK_END ? stored_size : 0;
	return 0;
}

legacy_s32 dos_file_tell(legacy_u16 handle)
{
	assert(opened && handle == TEST_HANDLE);
	return (legacy_s32)position;
}

legacy_u16 dos_file_read(legacy_u16 handle, void *destination, legacy_u16 length)
{
	assert(opened && !creating && handle == TEST_HANDLE && position + length <= stored_size);
	if (fail_read) {
		return 0;
	}
	memcpy(destination, stored + position, length);
	position += length;
	return length;
}

legacy_u16 dos_file_write(legacy_u16 handle, const void *source, legacy_u16 length)
{
	assert(opened && creating && handle == TEST_HANDLE);
	writes++;
	if (fail_write) {
		return 0;
	}
	legacy_u8 *resized = realloc(temporary, position + length);
	assert(resized);
	temporary = resized;
	memcpy(temporary + position, source, length);
	position += length;
	temporary_size = position;
	return length;
}

legacy_s16 dos_file_remove(const legacy_s8 *path)
{
	assert(strstr((const legacy_char *)path, ".tmp"));
	free(temporary);
	temporary = NULL;
	temporary_size = 0;
	removals++;
	return 0;
}

legacy_s16 sdl3_file_exists(const legacy_s8 *path)
{
	assert(strstr((const legacy_char *)path, ".tmp"));
	return 0;
}

legacy_s16 sdl3_file_replace(const legacy_s8 *source, const legacy_s8 *destination)
{
	assert(strstr((const legacy_char *)source, ".tmp"));
	if (fail_replace) {
		return -1;
	}
	assert(strlen((const legacy_char *)destination) < sizeof(stored_path));
	strcpy(stored_path, (const legacy_char *)destination);
	free(stored);
	stored = temporary;
	stored_size = temporary_size;
	temporary = NULL;
	temporary_size = 0;
	replacements++;
	return 0;
}

static struct UNIT_LEARNING_DATA example(void)
{
	struct UNIT_LEARNING_DATA data = {0};
	data.search = (struct UNIT_PLANNER_LEARNING){0.2, -0.1, 1.0, 450.25, 100U, 97U, 778U, 1U};
	data.best_profile = (struct UNIT_PLANNER_PROFILE){-0.2, 0.1, 1.0};
	data.native_samples = 23U;
	data.piece_count = TEST_PIECES;
	data.best_frames = data.command_count = TEST_COMMANDS;
	data.pieces = calloc(data.piece_count, sizeof(*data.pieces));
	data.commands = calloc(data.command_count, sizeof(*data.commands));
	assert(data.pieces && data.commands);
	data.pieces[1] = (struct UNIT_LEARNING_PIECE){105.0, 0.75, 115.0, -0.25, 3U};
	data.pieces[2].speed = 99.5;
	data.commands[0] = (struct CSRPL_OPPONENT_COMMAND){1U, 0U, -12, 1, 0U};
	data.commands[1] = (struct CSRPL_OPPONENT_COMMAND){1U, 0U, 20, 2, 1U};
	return data;
}

static void assert_equal(const struct UNIT_LEARNING_KEY *key, const struct UNIT_LEARNING_DATA *a,
						 const struct UNIT_LEARNING_DATA *b)
{
	legacy_u8 *left = NULL, *right = NULL;
	legacy_u32 left_size = 0, right_size = 0;
	assert(unit_learning_cache_encode(key, a, &left, &left_size));
	assert(unit_learning_cache_encode(key, b, &right, &right_size));
	assert(left_size == right_size && !memcmp(left, right, left_size));
	free(left);
	free(right);
}

static void checksum_update(legacy_u8 *bytes, legacy_u32 size)
{
	legacy_u64 hash = TEST_HASH_OFFSET;
	for (legacy_u32 index = 0; index < size; index++) {
		if (index < TEST_CHECKSUM_OFFSET || index >= TEST_CHECKSUM_OFFSET + TEST_CHECKSUM_BYTES) {
			hash = (hash ^ bytes[index]) * TEST_HASH_PRIME;
		}
	}
	for (legacy_u32 index = 0; index < sizeof(hash); index++) {
		bytes[TEST_CHECKSUM_OFFSET + index] = (legacy_u8)(hash >> (index * LEGACY_BYTE_BITS));
	}
}

static void test_codec(const struct UNIT_LEARNING_KEY *key, struct UNIT_LEARNING_DATA *source)
{
	legacy_u8 *bytes = NULL;
	legacy_u32 size = 0;
	assert(unit_learning_cache_encode(key, source, &bytes, &size));
	struct UNIT_LEARNING_DATA decoded = {0};
	assert(unit_learning_cache_decode(key, source->piece_count, bytes, size, &decoded));
	assert_equal(key, source, &decoded);
	unit_learning_cache_release(&decoded);
	struct UNIT_LEARNING_DATA sentinel = {.native_samples = 987U};
	for (legacy_u32 short_size = 0; short_size < size; short_size++) {
		assert(!unit_learning_cache_decode(key, source->piece_count, bytes, short_size, &sentinel));
	}
	for (legacy_u32 index = 0; index < size; index++) {
		bytes[index] ^= 1U;
		assert(!unit_learning_cache_decode(key, source->piece_count, bytes, size, &sentinel));
		bytes[index] ^= 1U;
	}
	struct UNIT_LEARNING_KEY other = *key;
	other.second++;
	assert(!unit_learning_cache_decode(&other, source->piece_count, bytes, size, &sentinel));
	assert(!unit_learning_cache_decode(key, source->piece_count + 1U, bytes, size, &sentinel));
	/* Recomputed checksums do not excuse invalid recipes or commands. */
	legacy_u32 ramp = TEST_HEADER_BYTES + TEST_METADATA_BYTES + TEST_PIECE_RAMP_OFFSET;
	bytes[ramp] = 1U;
	checksum_update(bytes, size);
	assert(!unit_learning_cache_decode(key, source->piece_count, bytes, size, &sentinel));
	bytes[ramp] = 0U;
	bytes[size - CSRPL_OPPONENT_COMMAND_SIZE + 1U] = 1U;
	checksum_update(bytes, size);
	assert(!unit_learning_cache_decode(key, source->piece_count, bytes, size, &sentinel));
	assert(sentinel.native_samples == 987U && !sentinel.pieces && !sentinel.commands);
	free(bytes);
	legacy_f64 speed = source->pieces[1].speed;
	source->pieces[1].speed = NAN;
	assert(!unit_learning_cache_encode(key, source, &bytes, &size));
	source->pieces[1].speed = speed;
	legacy_u32 count = source->piece_count;
	source->piece_count = UNIT_LEARNING_MAXIMUM_PIECES + 1U;
	assert(!unit_learning_cache_encode(key, source, &bytes, &size));
	source->piece_count = count;
}

static void test_keys(void)
{
	struct UNIT_LEARNING_KEY a, b;
	unit_learning_key_begin(&a);
	unit_learning_key_bytes(&a, "track", 5U);
	unit_learning_key_u32(&a, 20U);
	b = a;
	unit_learning_key_f64(&a, 0.0);
	unit_learning_key_f64(&b, -0.0);
	assert(a.first == b.first && a.second == b.second);
	unit_learning_key_u32(&b, 10U);
	assert(a.first != b.first && a.second != b.second);
	unit_learning_key_begin(&a);
	unit_learning_key_begin(&b);
	unit_learning_key_bytes(&a, "ab", 2U);
	unit_learning_key_bytes(&a, "c", 1U);
	unit_learning_key_bytes(&b, "a", 1U);
	unit_learning_key_bytes(&b, "bc", 2U);
	assert(a.first != b.first && a.second != b.second);
}

static void test_partial_recipes(const struct UNIT_LEARNING_KEY *key)
{
	struct UNIT_LEARNING_DATA source = example();
	/* Grouped ramps can accept an approach while retaining the route's speed.
	 * Native profile setters also accept exit and barrel hints independently. */
	source.pieces[0] = (struct UNIT_LEARNING_PIECE){0.0, 0.75, 0.0, -0.25, 3U};
	source.pieces[1] = (struct UNIT_LEARNING_PIECE){0.0, 0.0, 115.0, 0.0, 4U};
	source.pieces[2] = (struct UNIT_LEARNING_PIECE){105.0, 0.5, 0.0, 0.0, 2U};
	legacy_u8 *bytes = NULL;
	legacy_u32 size = 0;
	assert(unit_learning_cache_encode(key, &source, &bytes, &size));
	struct UNIT_LEARNING_DATA decoded = {0};
	assert(unit_learning_cache_decode(key, source.piece_count, bytes, size, &decoded));
	assert_equal(key, &source, &decoded);
	unit_learning_cache_release(&source);
	unit_learning_cache_release(&decoded);
	free(bytes);
}

static void test_storage(const struct UNIT_LEARNING_KEY *key, struct UNIT_LEARNING_DATA *data)
{
	assert(!unit_learning_cache_enabled());
	assert(!unit_learning_cache_save(key, data));
	assert(unit_learning_cache_set_directory((const legacy_s8 *)"test-\xc3\xa1"));
	assert(unit_learning_cache_enabled());
	struct UNIT_LEARNING_DATA loaded = {0};
	assert(!unit_learning_cache_load(key, data->piece_count, &loaded));
	assert(unit_learning_cache_save(key, data));
	assert(replacements == 1U && strstr(stored_path, "test-\xc3\xa1/"));
	assert(unit_learning_cache_load(key, data->piece_count, &loaded));
	assert_equal(key, data, &loaded);
	unit_learning_cache_release(&loaded);
	for (legacy_u32 failure = 0; failure < 3U; failure++) {
		legacy_u8 *original = stored;
		fail_write = failure == 0U;
		fail_close = failure == 1U;
		fail_replace = failure == 2U;
		assert(!unit_learning_cache_save(key, data));
		assert(stored == original && replacements == 1U && !temporary);
	}
	fail_write = fail_close = fail_replace = 0;
	assert(removals == 3U);
	fail_read = 1U;
	assert(!unit_learning_cache_load(key, data->piece_count, &loaded));
	fail_read = 0;
	fail_open = 1U;
	assert(!unit_learning_cache_save(key, data));
	fail_open = 0;
	assert(unit_learning_cache_load(key, data->piece_count, &loaded));
	assert_equal(key, data, &loaded);
	unit_learning_cache_release(&loaded);
	struct UNIT_LEARNING_DATA large = {0};
	large.search.lookahead = 1.0;
	large.piece_count = TEST_LARGE_PIECES;
	large.pieces = calloc(large.piece_count, sizeof(*large.pieces));
	assert(large.pieces);
	writes = 0;
	assert(unit_learning_cache_save(key, &large) && writes > 1U);
	assert(unit_learning_cache_load(key, large.piece_count, &loaded));
	assert_equal(key, &large, &loaded);
	unit_learning_cache_release(&large);
	unit_learning_cache_release(&loaded);
	assert(unit_learning_cache_set_directory((const legacy_s8 *)""));
	assert(!unit_learning_cache_enabled());
	assert(unit_learning_cache_set_directory(NULL));
	assert(!unit_learning_cache_enabled());
}

legacy_int main(void)
{
	test_keys();
	struct UNIT_LEARNING_KEY key;
	unit_learning_key_begin(&key);
	unit_learning_key_bytes(&key, "track/car/rules", 15U);
	struct UNIT_LEARNING_DATA data = example();
	test_codec(&key, &data);
	test_partial_recipes(&key);
	test_storage(&key, &data);
	unit_learning_cache_release(&data);
	free(stored);
	free(temporary);
	puts("UNIT portable learning codec, identity, corruption and safe storage tests passed.");
	return 0;
}
