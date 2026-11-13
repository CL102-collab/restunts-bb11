#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "../c/platform.h"
#include "../platform/sdl3/audio_resources_worker.h"

#define TEST_RESOURCE_BYTES 70003U
#define TEST_REGISTRY_THREADS 8U
#define TEST_REGISTRY_BYTES 131072U
#define TEST_REGISTRY_ROUNDS 20000U
#define TEST_SEGMENT_ADVANCE 4096U
#define TEST_SEGMENT_WRAP_BYTES 65536U
#define TEST_SMALL_BYTES 17U
#define TEST_PARAGRAPH_BYTES 16U
#define TEST_HIGH_KEY 0x100000001ULL

static legacy_u8 expected_quiesce;
static legacy_u32 quiesce_calls;
static const void *quiesce_pointer;
static SDL_Semaphore *execution_ready;
static SDL_Semaphore *execution_continue;
static legacy_u8 *game_arena;

void dos_process_exit(legacy_s16 status)
{
	(void)status;
	abort();
}

static void test_quiesce(legacy_u8 usage)
{
	assert(usage == expected_quiesce);
	assert(quiesce_pointer == NULL || sdl3_audio_resource_remaining(quiesce_pointer) != 0);
	++quiesce_calls;
}

static void expect_reset(legacy_u8 usage)
{
	expected_quiesce = usage;
	quiesce_pointer = NULL;
	sdl3_audio_resources_reset();
	assert(sdl3_audio_resources_error()[0] == 0);
}

static void test_owned_copy_and_far_addresses(void)
{
	legacy_u8 *source = game_arena;
	for (size_t index = 0; index < TEST_RESOURCE_BYTES; ++index) {
		source[index] = (legacy_u8)(index * 13U + 7U);
	}
	legacy_u8 *copy =
		sdl3_audio_resource_clone(source, TEST_RESOURCE_BYTES, AUDIO_RESOURCE_USAGE_MUSIC);
	assert(copy != NULL && copy != source);
	assert(memcmp(source, copy, TEST_RESOURCE_BYTES) == 0);
	assert(sdl3_audio_resource_remaining(copy) == TEST_RESOURCE_BYTES);
	assert(sdl3_audio_resource_remaining(copy + TEST_RESOURCE_BYTES - 1U) == 1);
	assert(sdl3_audio_resource_remaining(copy + TEST_RESOURCE_BYTES) == 0);
	assert(sdl3_audio_resource_remaining(source) == 0);
	assert(sdl3_audio_resource_remaining(NULL) == 0);
	memset(source, 0, TEST_RESOURCE_BYTES);
	assert(copy[TEST_RESOURCE_BYTES - 1U] == (legacy_u8)((TEST_RESOURCE_BYTES - 1U) * 13U + 7U));

	legacy_u16 segment = dos_memory_pointer_segment(copy);
	assert(segment == AUDIO_RESOURCE_WORKER_FIRST_SEGMENT);
	assert(dos_memory_pointer_offset(copy) == 0);
	assert(dos_memory_make_pointer(segment, 0) == source);
	assert(sdl3_audio_resources_set_execution(1));
	assert(dos_memory_make_pointer(segment, 0) == copy);
	assert(dos_memory_make_pointer(segment, LEGACY_U16_MAX) == copy + LEGACY_U16_MAX);
	assert(dos_memory_make_pointer(segment + TEST_SEGMENT_ADVANCE, 0) ==
		   copy + TEST_SEGMENT_WRAP_BYTES);
	for (size_t index = TEST_SEGMENT_WRAP_BYTES - 8U; index < TEST_SEGMENT_WRAP_BYTES + 8U;
		 ++index) {
		const void *pointer = copy + index;
		assert(dos_memory_make_pointer(dos_memory_pointer_segment(pointer),
									   dos_memory_pointer_offset(pointer)) == pointer);
	}
	assert(sdl3_audio_resources_set_execution(0));
	assert(dos_memory_make_pointer(segment, 0) == source);
	expected_quiesce = AUDIO_RESOURCE_USAGE_MUSIC;
	quiesce_pointer = copy;
	sdl3_audio_resource_release(source);
	assert(sdl3_audio_resource_remaining(copy) == 0);
}

static void test_group_lifetime_and_first_fit(void)
{
	legacy_u8 bytes[48];
	memset(bytes, 0x73, sizeof(bytes));
	legacy_u8 *music =
		sdl3_audio_resource_clone_key(1, bytes, TEST_SMALL_BYTES, AUDIO_RESOURCE_USAGE_MUSIC);
	legacy_u8 *engine = sdl3_audio_resource_clone_key(2, bytes, 33, AUDIO_RESOURCE_USAGE_ENGINE);
	legacy_u8 *shared = sdl3_audio_resource_clone_key(3, bytes, 19, AUDIO_RESOURCE_USAGE_MUSIC);
	assert(music != NULL && engine != NULL && shared != NULL);
	assert(sdl3_audio_resource_clone_key(3, bytes, 19, AUDIO_RESOURCE_USAGE_ENGINE) == shared);
	shared[0] = 0x5A;
	/* Deduplication must preserve mapping already performed on the owned copy. */
	assert(sdl3_audio_resource_clone_key(3, bytes, 19, AUDIO_RESOURCE_USAGE_MUSIC) == shared);
	assert(shared[0] == 0x5A);
	assert(sdl3_audio_resource_clone_key(3, bytes, 20, AUDIO_RESOURCE_USAGE_MUSIC) == NULL);
	assert(sdl3_audio_resource_remaining(shared) == 19);

	expected_quiesce = AUDIO_RESOURCE_USAGE_MUSIC;
	quiesce_pointer = music;
	legacy_u32 previous_calls = quiesce_calls;
	sdl3_audio_resource_release_key(1);
	assert(quiesce_calls == previous_calls + 1U);
	assert(sdl3_audio_resource_remaining(music) == 0);
	assert(sdl3_audio_resource_remaining(shared) == 19);
	assert(sdl3_audio_resource_remaining(engine) == 33);
	legacy_u8 *replacement =
		sdl3_audio_resource_clone_key(4, bytes, TEST_SMALL_BYTES, AUDIO_RESOURCE_USAGE_MUSIC);
	assert(replacement == music);
	expected_quiesce = AUDIO_RESOURCE_USAGE_ENGINE;
	quiesce_pointer = shared;
	sdl3_audio_resource_release_key(2);
	assert(sdl3_audio_resource_remaining(engine) == 0);
	assert(sdl3_audio_resource_remaining(shared) == 0);
	assert(sdl3_audio_resource_remaining(replacement) == TEST_SMALL_BYTES);
	previous_calls = quiesce_calls;
	sdl3_audio_resource_release_key(99);
	assert(quiesce_calls == previous_calls);
	expect_reset(AUDIO_RESOURCE_USAGE_MUSIC);
}

static void test_allocation_limits(void)
{
	legacy_u8 bytes[TEST_SMALL_BYTES] = {0};
	assert(sdl3_audio_resource_clone_key(0, bytes, sizeof(bytes), AUDIO_RESOURCE_USAGE_MUSIC) ==
		   NULL);
	assert(sdl3_audio_resource_clone_key(1, NULL, sizeof(bytes), AUDIO_RESOURCE_USAGE_MUSIC) ==
		   NULL);
	assert(sdl3_audio_resource_clone_key(1, bytes, 0, AUDIO_RESOURCE_USAGE_MUSIC) == NULL);
	assert(sdl3_audio_resource_clone_key(1, bytes, sizeof(bytes), 0) == NULL);
	assert(sdl3_audio_resource_clone_key(1, bytes, sizeof(bytes), 4U) == NULL);
	for (legacy_u64 key = 1; key <= AUDIO_RESOURCE_WORKER_CHUNK_COUNT; ++key) {
		assert(sdl3_audio_resource_clone_key(key, bytes, sizeof(bytes),
											 AUDIO_RESOURCE_USAGE_MUSIC) != NULL);
	}
	assert(sdl3_audio_resource_clone_key(AUDIO_RESOURCE_WORKER_CHUNK_COUNT + 1U, bytes,
										 sizeof(bytes), AUDIO_RESOURCE_USAGE_MUSIC) == NULL);
	expect_reset(AUDIO_RESOURCE_USAGE_MUSIC);
	size_t capacity = (AUDIO_RESOURCE_WORKER_END_SEGMENT - AUDIO_RESOURCE_WORKER_FIRST_SEGMENT) *
					  TEST_PARAGRAPH_BYTES;
	legacy_u8 *large = SDL_malloc(capacity);
	assert(large != NULL);
	memset(large, 0xC7, capacity);
	void *copy = sdl3_audio_resource_clone_key(1, large, capacity, AUDIO_RESOURCE_USAGE_ENGINE);
	assert(copy != NULL && sdl3_audio_resource_remaining(copy) == capacity);
	assert(sdl3_audio_resource_clone_key(2, bytes, 1, AUDIO_RESOURCE_USAGE_ENGINE) == NULL);
	assert(sdl3_audio_resource_remaining(copy) == capacity);
	expect_reset(AUDIO_RESOURCE_USAGE_ENGINE);
	assert(sdl3_audio_resource_clone_key(1, large, capacity + 1U, AUDIO_RESOURCE_USAGE_ENGINE) ==
		   NULL);
	SDL_free(large);
}

static void test_full_width_keys(void)
{
	legacy_u8 bytes[TEST_SMALL_BYTES] = {0};
	void *high = sdl3_audio_resource_clone_key(TEST_HIGH_KEY, bytes, sizeof(bytes),
											   AUDIO_RESOURCE_USAGE_MUSIC);
	void *low = sdl3_audio_resource_clone_key(1, bytes, sizeof(bytes), AUDIO_RESOURCE_USAGE_ENGINE);
	assert(high != NULL && low != NULL && high != low);
	expected_quiesce = AUDIO_RESOURCE_USAGE_ENGINE;
	quiesce_pointer = low;
	sdl3_audio_resource_release_key(1);
	assert(sdl3_audio_resource_remaining(high) == sizeof(bytes));
	assert(sdl3_audio_resource_remaining(low) == 0);
	expected_quiesce = AUDIO_RESOURCE_USAGE_MUSIC;
	quiesce_pointer = high;
	sdl3_audio_resource_release_key(TEST_HIGH_KEY);
	assert(sdl3_audio_resource_remaining(high) == 0);
}

static legacy_int SDLCALL test_execution_thread(void *unused)
{
	(void)unused;
	assert(!sdl3_audio_resources_is_execution());
	assert(sdl3_audio_resources_set_execution(1));
	SDL_SignalSemaphore(execution_ready);
	SDL_WaitSemaphore(execution_continue);
	assert(sdl3_audio_resources_is_execution());
	assert(dos_memory_make_pointer(AUDIO_RESOURCE_WORKER_FIRST_SEGMENT, 0) ==
		   sdl3_audio_resources_arena() +
			   AUDIO_RESOURCE_WORKER_FIRST_SEGMENT * TEST_PARAGRAPH_BYTES);
	assert(sdl3_audio_resources_set_execution(0));
	return 0;
}

static legacy_int SDLCALL test_registry_thread(void *data)
{
	legacy_u8 *bytes = data;
	for (size_t index = 0; index < TEST_REGISTRY_ROUNDS; ++index) {
		void *pointer = bytes + (index * 1709U) % TEST_REGISTRY_BYTES;
		assert(dos_memory_make_pointer(dos_memory_pointer_segment(pointer),
									   dos_memory_pointer_offset(pointer)) == pointer);
	}
	return 0;
}

static void test_thread_isolation_and_registry(void)
{
	execution_ready = SDL_CreateSemaphore(0);
	execution_continue = SDL_CreateSemaphore(0);
	assert(execution_ready != NULL && execution_continue != NULL);
	SDL_Thread *thread = SDL_CreateThread(test_execution_thread, "audio-memory-realm", NULL);
	assert(thread != NULL);
	SDL_WaitSemaphore(execution_ready);
	assert(!sdl3_audio_resources_is_execution());
	assert(dos_memory_make_pointer(AUDIO_RESOURCE_WORKER_FIRST_SEGMENT, 0) == game_arena);
	SDL_SignalSemaphore(execution_continue);
	SDL_WaitThread(thread, NULL);
	SDL_DestroySemaphore(execution_ready);
	SDL_DestroySemaphore(execution_continue);

	SDL_Thread *threads[TEST_REGISTRY_THREADS];
	void *buffers[TEST_REGISTRY_THREADS];
	for (size_t index = 0; index < TEST_REGISTRY_THREADS; ++index) {
		buffers[index] = SDL_malloc(TEST_REGISTRY_BYTES);
		assert(buffers[index] != NULL);
		threads[index] =
			SDL_CreateThread(test_registry_thread, "legacy-memory-registry", buffers[index]);
		assert(threads[index] != NULL);
	}
	for (size_t index = 0; index < TEST_REGISTRY_THREADS; ++index) {
		SDL_WaitThread(threads[index], NULL);
		SDL_free(buffers[index]);
	}
}

legacy_int main(void)
{
	game_arena = dos_memory_make_pointer(dos_memory_allocate(100), 0);
	sdl3_audio_resources_set_quiesce(test_quiesce);
	assert(!sdl3_audio_resources_is_execution());
	test_owned_copy_and_far_addresses();
	test_group_lifetime_and_first_fit();
	test_allocation_limits();
	test_full_width_keys();
	test_thread_isolation_and_registry();
	sdl3_audio_resources_set_quiesce(NULL);
	puts("Private audio resource ownership, far pointers and thread isolation passed");
	return 0;
}
