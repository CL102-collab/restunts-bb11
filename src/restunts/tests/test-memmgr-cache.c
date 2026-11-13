#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../c/fatal.h"
#include "../c/memmgr.h"
#include "../c/platform.h"

#define TEST_PARAGRAPH_BYTES 16U
#define TEST_ARENA_START 4096U
#define TEST_ARENA_END 28672U
#define TEST_MEMORY_BYTES (TEST_ARENA_END * TEST_PARAGRAPH_BYTES)
#define TEST_RESOURCE_SLOTS 48U
#define TEST_CHURN_COUNT 2000U
#define TEST_CHURN_RESOURCE_COUNT 53U
#define TEST_NAME_PARAGRAPHS 2U
#define TEST_NAME_PAYLOAD_ID 71U

extern struct MEMCHUNK *mmgr_live_sentinel;
extern struct MEMCHUNK *mmgr_last_live_chunk;
extern struct MEMCHUNK *mmgr_first_cached_chunk;
extern struct MEMCHUNK *mmgr_cache_sentinel;

static union {
	legacy_u16 alignment;
	legacy_u8 bytes[TEST_MEMORY_BYTES];
} test_memory;

void fatal_error(const legacy_s8 *format, ...)
{
	fprintf(stderr, "Unexpected memory manager error: %s\n", format);
	abort();
}

void *_memcpy(void *destination, const void *source, legacy_u16 length)
{
	return memmove(destination, source, length);
}

void far *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	size_t address = (size_t)segment * TEST_PARAGRAPH_BYTES + offset;
	assert(address < sizeof(test_memory.bytes));
	return &test_memory.bytes[address];
}

legacy_u16 dos_memory_pointer_segment(const void far *pointer)
{
	ptrdiff_t address = (const legacy_u8 *)pointer - test_memory.bytes;
	assert(address >= 0 && (size_t)address < sizeof(test_memory.bytes));
	return (legacy_u16)((size_t)address / TEST_PARAGRAPH_BYTES);
}

legacy_u16 dos_memory_pointer_offset(const void far *pointer)
{
	ptrdiff_t address = (const legacy_u8 *)pointer - test_memory.bytes;
	assert(address >= 0 && (size_t)address < sizeof(test_memory.bytes));
	return (legacy_u16)((size_t)address % TEST_PARAGRAPH_BYTES);
}

void far *dos_memory_get_psp(void)
{
	return dos_memory_make_pointer(TEST_ARENA_START - 16U, 0U);
}

legacy_u16 dos_memory_allocate(legacy_u16 paragraphs)
{
	assert(paragraphs <= TEST_ARENA_END - TEST_ARENA_START);
	return TEST_ARENA_START;
}

legacy_u16 dos_memory_resize(legacy_u16 segment, legacy_u16 paragraphs)
{
	assert(segment == TEST_ARENA_START);
	assert(paragraphs <= TEST_ARENA_END - TEST_ARENA_START);
	return paragraphs;
}

static void reset_arena(void)
{
	memset(test_memory.bytes, 0, sizeof(test_memory.bytes));
	mmgr_alloc_resmem(TEST_ARENA_END);
	assert(mmgr_last_live_chunk == mmgr_live_sentinel);
	assert(mmgr_first_cached_chunk == mmgr_cache_sentinel);
}

static void make_name(legacy_s8 name[MMGR_RESOURCE_NAME_LENGTH + 1], legacy_u32 resource_id)
{
	memset(name, 0, MMGR_RESOURCE_NAME_LENGTH + 1);
	snprintf((legacy_char *)name, MMGR_RESOURCE_NAME_LENGTH + 1, "RES%08" LEGACY_PRIu32,
			 resource_id);
}

static legacy_u8 payload_byte(legacy_u32 resource_id, size_t offset)
{
	return (legacy_u8)(resource_id * 37U + offset * 13U + 1U);
}

static void write_payload(void *pointer, legacy_u16 paragraphs, legacy_u32 resource_id)
{
	legacy_u8 *bytes = (legacy_u8 *)pointer;
	for (size_t i = 0; i < (size_t)paragraphs * TEST_PARAGRAPH_BYTES; i++) {
		bytes[i] = payload_byte(resource_id, i);
	}
}

static void check_payload(const void *pointer, legacy_u16 paragraphs, legacy_u32 resource_id)
{
	const legacy_u8 *bytes = (const legacy_u8 *)pointer;
	assert(pointer != NULL);
	for (size_t i = 0; i < (size_t)paragraphs * TEST_PARAGRAPH_BYTES; i++) {
		if (bytes[i] != payload_byte(resource_id, i)) {
			fprintf(stderr,
					"Cached resource %" LEGACY_PRIu32 " corrupted at byte %zu: "
					"expected %" LEGACY_PRIu32 ", got %" LEGACY_PRIu32 "\n",
					resource_id, i, (legacy_u32)payload_byte(resource_id, i), (legacy_u32)bytes[i]);
			abort();
		}
	}
}

static void check_named_cache_round_trip(void *resource, const legacy_s8 *lookup,
										 const legacy_s8 *miss, const legacy_s8 *expected)
{
	assert(dos_memory_pointer_segment(resource) == TEST_ARENA_START);
	assert(mmgr_last_live_chunk->ressize == TEST_NAME_PARAGRAPHS);
	assert(memcmp(mmgr_last_live_chunk->resname, expected, MMGR_RESOURCE_NAME_LENGTH) == 0);
	check_payload(resource, TEST_NAME_PARAGRAPHS, TEST_NAME_PAYLOAD_ID);
	void *cached = mmgr_free((legacy_s8 *)resource);
	assert(dos_memory_pointer_segment(cached) == TEST_ARENA_END - TEST_NAME_PARAGRAPHS);
	assert(memcmp(mmgr_first_cached_chunk->resname, expected, MMGR_RESOURCE_NAME_LENGTH) == 0);
	check_payload(cached, TEST_NAME_PARAGRAPHS, TEST_NAME_PAYLOAD_ID);
	assert(mmgr_get_chunk_by_name(miss) == NULL);
	void *restored = mmgr_get_chunk_by_name(lookup);
	assert(restored == resource);
	assert(mmgr_get_chunk_size((legacy_s8 *)restored) == TEST_NAME_PARAGRAPHS);
	assert(memcmp(mmgr_last_live_chunk->resname, expected, MMGR_RESOURCE_NAME_LENGTH) == 0);
	check_payload(restored, TEST_NAME_PARAGRAPHS, TEST_NAME_PAYLOAD_ID);
	mmgr_release(restored);
	assert(mmgr_get_ofs_diff() == TEST_ARENA_END - TEST_ARENA_START);
}

static void test_resource_name_boundaries(void)
{
	/* Tight source objects let ASan detect reads beyond the first terminator. */
	const legacy_s8 short_name[] = "short";
	const legacy_s8 empty_name[] = "";
	const legacy_s8 path_name[] = "C:\\game/assets/misc.pre";
	const legacy_s8 full_name[] = "ABCDEFGH.EXT";
	const legacy_s8 long_name[] = "ABCDEFGH.EXTsuffix";
	const struct {
		const legacy_s8 *name;
		const legacy_s8 *lookup;
		const legacy_s8 *miss;
		legacy_s8 expected[MMGR_RESOURCE_NAME_LENGTH];
	} cases[] = {
		{short_name, short_name, "shorter", "short"},
		{empty_name, empty_name, "x", ""},
		{path_name, "misc", "mis", "misc.pre"},
		{full_name, full_name, "ABCDEFGH.EXZ", "ABCDEFGH.EXT"},
		{long_name, "ABCDEFGH.EXTdifferent", "ABCDEFGH.EXZ", "ABCDEFGH.EXT"},
	};

	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		reset_arena();
		void *resource = mmgr_alloc_pages(cases[index].name, TEST_NAME_PARAGRAPHS);
		write_payload(resource, TEST_NAME_PARAGRAPHS, TEST_NAME_PAYLOAD_ID);
		check_named_cache_round_trip(resource, cases[index].lookup, cases[index].miss,
									 cases[index].expected);

		reset_arena();
		resource = mmgr_alloc_pages(full_name, TEST_NAME_PARAGRAPHS);
		write_payload(resource, TEST_NAME_PARAGRAPHS, TEST_NAME_PAYLOAD_ID);
		/* Renaming a full descriptor must erase the previous name's suffix. */
		mmgr_rename_chunk((legacy_s8 *)resource, cases[index].name);
		check_named_cache_round_trip(resource, cases[index].lookup, cases[index].miss,
									 cases[index].expected);
	}
}

static void test_free_last_resource_with_full_table(void)
{
	void *last_resource = NULL;

	reset_arena();
	legacy_s8 name[MMGR_RESOURCE_NAME_LENGTH + 1];
	for (legacy_u32 i = 0; i < TEST_RESOURCE_SLOTS; i++) {
		make_name(name, i);
		last_resource = mmgr_alloc_pages(name, 2U);
		write_payload(last_resource, 2U, i);
	}
	assert(mmgr_last_live_chunk + 1 == mmgr_first_cached_chunk);

	/* The last live block must survive when its descriptor becomes cached. */
	void *cached_resource = mmgr_free((legacy_s8 *)last_resource);
	assert(cached_resource != last_resource);
	check_payload(cached_resource, 2U, TEST_RESOURCE_SLOTS - 1U);
	last_resource = mmgr_get_chunk_by_name(name);
	check_payload(last_resource, 2U, TEST_RESOURCE_SLOTS - 1U);
	assert(mmgr_last_live_chunk + 1 == mmgr_first_cached_chunk);
}

static void test_free_non_last_resource_with_full_table(void)
{
	reset_arena();
	legacy_s8 first_name[MMGR_RESOURCE_NAME_LENGTH + 1] = "FIRST";
	void *first_resource = mmgr_alloc_pages(first_name, 2U);
	write_payload(first_resource, 2U, 900U);
	legacy_s8 last_name[MMGR_RESOURCE_NAME_LENGTH + 1] = "LAST";
	void *last_resource = mmgr_alloc_pages(last_name, 3U);
	write_payload(last_resource, 3U, 901U);
	void *cached_resource;
	legacy_s8 name[MMGR_RESOURCE_NAME_LENGTH + 1];
	for (legacy_u32 i = 0; i < TEST_RESOURCE_SLOTS - 2U; i++) {
		make_name(name, i);
		void *resource = mmgr_alloc_pages(name, 1U);
		write_payload(resource, 1U, i);
		cached_resource = mmgr_free((legacy_s8 *)resource);
		check_payload(cached_resource, 1U, i);
	}
	struct MEMCHUNK *last_chunk = mmgr_last_live_chunk;
	assert(last_chunk + 1 == mmgr_first_cached_chunk);

	/* Freeing an older block must not consume the last live descriptor. */
	mmgr_free((legacy_s8 *)first_resource);
	assert(mmgr_last_live_chunk == last_chunk);
	assert(last_chunk->resofs == dos_memory_pointer_segment(last_resource));
	assert(mmgr_get_chunk_size((legacy_s8 *)last_resource) == 3U);
	check_payload(last_resource, 3U, 901U);

	cached_resource = mmgr_free((legacy_s8 *)last_resource);
	check_payload(cached_resource, 3U, 901U);
	assert(mmgr_last_live_chunk == mmgr_live_sentinel);
	assert(mmgr_get_ofs_diff() == TEST_ARENA_END - TEST_ARENA_START);
	last_resource = mmgr_get_chunk_by_name(last_name);
	check_payload(last_resource, 3U, 901U);
	mmgr_release(last_resource);

	first_resource = mmgr_alloc_pages(first_name, 2U);
	assert(dos_memory_pointer_segment(first_resource) == TEST_ARENA_START);
	write_payload(first_resource, 2U, 902U);
	check_payload(first_resource, 2U, 902U);
	mmgr_release(first_resource);
	assert(mmgr_get_ofs_diff() == TEST_ARENA_END - TEST_ARENA_START);
}

static void test_repeated_cache_churn(void)
{
	reset_arena();
	legacy_s8 pinned_name[MMGR_RESOURCE_NAME_LENGTH + 1] = "PINNED";
	void *pinned_resource = mmgr_alloc_pages(pinned_name, 7U);
	write_payload(pinned_resource, 7U, 999U);
	legacy_u32 cache_hits = 0;
	legacy_s8 name[MMGR_RESOURCE_NAME_LENGTH + 1];
	for (legacy_u32 iteration = 0; iteration < TEST_CHURN_COUNT; iteration++) {
		legacy_u32 resource_id = iteration % TEST_CHURN_RESOURCE_COUNT;
		legacy_u16 paragraphs = (legacy_u16)(1U + resource_id % 5U);

		make_name(name, resource_id);
		void *resource = mmgr_get_chunk_by_name(name);
		if (resource != NULL) {
			cache_hits++;
			check_payload(resource, paragraphs, resource_id);
		} else {
			resource = mmgr_alloc_pages(name, paragraphs);
			write_payload(resource, paragraphs, resource_id);
		}
		void *cached_resource = mmgr_free((legacy_s8 *)resource);
		check_payload(cached_resource, paragraphs, resource_id);
		check_payload(pinned_resource, 7U, 999U);
		assert(mmgr_get_ofs_diff() == TEST_ARENA_END - TEST_ARENA_START - 7U);
		assert(mmgr_last_live_chunk == mmgr_live_sentinel + 1);
		assert(mmgr_first_cached_chunk > mmgr_last_live_chunk);
	}
	assert(cache_hits > TEST_CHURN_COUNT / 2U);
	mmgr_release(pinned_resource);
	assert(mmgr_get_ofs_diff() == TEST_ARENA_END - TEST_ARENA_START);
}

static void test_pointer_allocation_extents(void)
{
	reset_arena();
	assert(mmgr_get_pointer_size_bytes(NULL) == 0);
	assert(mmgr_get_pointer_size_bytes(test_memory.bytes) == 0);
	const legacy_u32 size = TEST_NAME_PARAGRAPHS * TEST_PARAGRAPH_BYTES;
	legacy_u8 *resource = mmgr_alloc_pages("EXTENT", TEST_NAME_PARAGRAPHS);
	assert(mmgr_get_pointer_size_bytes(resource) == size);
	assert(mmgr_get_pointer_size_bytes(resource + 1) == size - 1U);
	assert(mmgr_get_pointer_size_bytes(resource + size - 1U) == 1U);
	assert(mmgr_get_pointer_size_bytes(resource + size) == 0);
	assert(mmgr_get_pointer_size_bytes(resource - 1) == 0);
	legacy_u8 *cached = mmgr_free((legacy_s8 *)resource);
	assert(mmgr_get_pointer_size_bytes(resource) == 0);
	assert(mmgr_get_pointer_size_bytes(cached) == size);
	assert(mmgr_get_pointer_size_bytes(cached + TEST_PARAGRAPH_BYTES + 1U) ==
		   TEST_PARAGRAPH_BYTES - 1U);
	assert(mmgr_get_pointer_size_bytes(cached - 1) == 0);
	/* Reclaiming moves the allocation back into the live range. */
	resource = mmgr_get_chunk_by_name("EXTENT");
	assert(mmgr_get_pointer_size_bytes(resource) == size);
	assert(mmgr_get_pointer_size_bytes(cached) == 0);
	mmgr_release(resource);
	assert(mmgr_get_pointer_size_bytes(resource) == 0);
}

int main(void)
{
	test_pointer_allocation_extents();
	test_resource_name_boundaries();
	test_free_last_resource_with_full_table();
	test_free_non_last_resource_with_full_table();
	test_repeated_cache_churn();
	return 0;
}
