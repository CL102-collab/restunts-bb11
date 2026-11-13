#include "../platform/sdl3/mt32_roms.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <string>

extern "C" legacy_char *mt32_rom_test_inventory(const legacy_char *const *rom_ids, size_t count,
												const legacy_char *model, legacy_char *error,
												size_t error_size);

typedef std::basic_string<legacy_char> Text;
static const size_t ERROR_CAPACITY = 1024U;
static const size_t UNKNOWN_ROM_BYTES = 32768U;
static const size_t SCAN_WORKER_COUNT = 8U;
static const size_t SCAN_WORKER_ITERATIONS = 16U;
static SDL_calloc_func original_calloc;
static legacy_u8 fail_next_calloc;

static void *SDLCALL mutex_test_calloc(size_t count, size_t size)
{
	if (fail_next_calloc) {
		fail_next_calloc = 0;
		return NULL;
	}
	return original_calloc(count, size);
}

static void test_mutex_allocation_failure()
{
	SDL_malloc_func original_malloc;
	SDL_realloc_func original_realloc;
	SDL_free_func original_free;
	SDL_GetMemoryFunctions(&original_malloc, &original_calloc, &original_realloc, &original_free);
	/* Only fail the mutex allocation. All successful allocations use SDL's
	 * original allocator, so restoring the callbacks preserves ownership. */
	assert(SDL_SetMemoryFunctions(original_malloc, mutex_test_calloc, original_realloc,
								  original_free));
	fail_next_calloc = 1;
	legacy_char *json = mt32_rom_inventory_json(NULL);
	assert(json != NULL);
	assert(SDL_strstr(json, "Cannot allocate MT-32 ROM library mutex") != NULL);
	mt32_rom_inventory_free(json);
	fail_next_calloc = 1;
	legacy_char error[ERROR_CAPACITY];
	assert(!mt32_rom_load(NULL, NULL, NULL, error, sizeof(error)));
	assert(SDL_strstr(error, "Cannot allocate MT-32 ROM library mutex") != NULL);
	assert(
		SDL_SetMemoryFunctions(original_malloc, original_calloc, original_realloc, original_free));
}

static legacy_int SDLCALL scan_worker(void *userdata)
{
	SDL_AtomicInt *start = static_cast<SDL_AtomicInt *>(userdata);
	while (SDL_GetAtomicInt(start) == 0) {
		SDL_Delay(0);
	}
	for (size_t index = 0; index < SCAN_WORKER_ITERATIONS; ++index) {
		legacy_char *json = mt32_rom_inventory_json(NULL);
		assert(json != NULL);
		assert(SDL_strstr(json, "Choose your MT-32 ROM folder") != NULL);
		mt32_rom_inventory_free(json);
	}
	return 0;
}

static void test_concurrent_scans_before_sdl_init()
{
	assert(SDL_WasInit(0) == 0);
	SDL_AtomicInt start = {};
	SDL_Thread *workers[SCAN_WORKER_COUNT];
	for (size_t index = 0; index < SCAN_WORKER_COUNT; ++index) {
		workers[index] = SDL_CreateThread(scan_worker, "MT32RomScan", &start);
		assert(workers[index] != NULL);
	}
	SDL_SetAtomicInt(&start, 1);
	for (size_t index = 0; index < SCAN_WORKER_COUNT; ++index) {
		SDL_WaitThread(workers[index], NULL);
	}
	assert(SDL_WasInit(0) == 0);
}

static size_t set_count(const Text &json)
{
	size_t count = 0;
	size_t position = 0;
	const Text marker = "\"id\":";
	while ((position = json.find(marker, position)) != Text::npos) {
		++count;
		position += marker.size();
	}
	return count;
}

template <size_t Count>
static Text inventory(const legacy_char *const (&rom_ids)[Count], const legacy_char *model,
					  legacy_char (&error)[ERROR_CAPACITY])
{
	legacy_char *result = mt32_rom_test_inventory(rom_ids, Count, model, error, sizeof(error));
	assert(result != NULL);
	Text text = result;
	mt32_rom_inventory_free(result);
	return text;
}

static void test_complete_sets()
{
	legacy_char error[ERROR_CAPACITY];
	const legacy_char *single[] = {"ctrl_mt32_1_07", "pcm_mt32"};
	Text json = inventory(single, NULL, error);
	assert(error[0] == 0);
	assert(set_count(json) == 1U);
	assert(json.find("MT-32 (old, v1.07)") != Text::npos);

	const legacy_char *duplicates[] = {"pcm_mt32", "ctrl_mt32_1_07", "pcm_mt32", "ctrl_mt32_1_07"};
	assert(inventory(duplicates, NULL, error) == json);
	assert(error[0] == 0);

	const legacy_char *several[] = {"ctrl_mt32_2_07", "pcm_mt32", "ctrl_mt32_1_07"};
	json = inventory(several, NULL, error);
	assert(set_count(json) == 2U);
	assert(SDL_strstr(error, "Multiple MT-32 ROM sets") != NULL);
	assert(SDL_strstr(error, "mt32_1_07") != NULL);
	assert(SDL_strstr(error, "mt32_2_07") != NULL);
	assert(inventory(several, "mt32_2_07", error) == json);
	assert(error[0] == 0);
	inventory(several, "mt32_1_06", error);
	assert(SDL_strstr(error, "unavailable or incomplete") != NULL);
}

static void test_split_images()
{
	legacy_char error[ERROR_CAPACITY];
	const legacy_char *complete[] = {"ctrl_mt32_1_07_b", "pcm_mt32_h", "ctrl_mt32_1_07_a",
									 "pcm_mt32_l"};
	Text json = inventory(complete, NULL, error);
	assert(set_count(json) == 1U);
	assert(error[0] == 0);
	assert(json.find("mt32_1_07") != Text::npos);

	const legacy_char *missing[] = {"ctrl_mt32_1_07_a", "pcm_mt32"};
	assert(set_count(inventory(missing, NULL, error)) == 0U);
	assert(SDL_strstr(error, "both halves") != NULL);

	const legacy_char *mismatched[] = {"ctrl_mt32_1_06_a", "ctrl_mt32_1_07_b", "pcm_mt32"};
	assert(set_count(inventory(mismatched, NULL, error)) == 0U);
	assert(SDL_strstr(error, "both halves") != NULL);

	/* The complete MT-32 PCM image is also the first half of a CM-32L image.
	 * Resolving by a filename or taking its first metadata match misses this. */
	const legacy_char *cm_pair[] = {"ctrl_cm32l_1_02", "pcm_mt32", "pcm_cm32l_h"};
	json = inventory(cm_pair, NULL, error);
	assert(set_count(json) == 1U);
	assert(error[0] == 0);
	assert(json.find("CM-32L / LAPC-I (v1.02)") != Text::npos);

	const legacy_char *wrong_pcm[] = {"ctrl_cm32l_1_02", "pcm_mt32"};
	assert(set_count(inventory(wrong_pcm, NULL, error)) == 0U);
	assert(SDL_strstr(error, "complete compatible ROM set") != NULL);
}

static void test_missing_images()
{
	legacy_char error[ERROR_CAPACITY];
	const legacy_char *control[] = {"ctrl_mt32_2_04"};
	assert(set_count(inventory(control, NULL, error)) == 0U);
	assert(SDL_strstr(error, "Compatible PCM ROM missing") != NULL);
	const legacy_char *pcm[] = {"pcm_mt32"};
	assert(set_count(inventory(pcm, NULL, error)) == 0U);
	assert(SDL_strstr(error, "Compatible control ROM missing") != NULL);
}

static void test_filesystem_scan()
{
	legacy_char *missing = mt32_rom_inventory_json(NULL);
	assert(missing != NULL);
	assert(SDL_strstr(missing, "Choose your MT-32 ROM folder") != NULL);
	mt32_rom_inventory_free(missing);

	legacy_char *base = SDL_GetPrefPath("ChocolateStunts", "MT32RomTests");
	assert(base != NULL);
	legacy_char name[ERROR_CAPACITY];
	SDL_snprintf(name, sizeof(name), "fixture-%" SDL_PRIu64, SDL_GetTicksNS());
	Text directory = Text(base) + name;
	SDL_free(base);
	Text nested = directory + "/nested";
	assert(SDL_CreateDirectory(nested.c_str()));
	Text file_path = nested + "/MT32_CONTROL.ROM";
	SDL_IOStream *file = SDL_IOFromFile(file_path.c_str(), "wb");
	assert(file != NULL);
	legacy_u8 contents[UNKNOWN_ROM_BYTES] = {};
	assert(SDL_WriteIO(file, contents, sizeof(contents)) == sizeof(contents));
	assert(SDL_CloseIO(file));
	legacy_char *json = mt32_rom_inventory_json(directory.c_str());
	assert(json != NULL);
	assert(SDL_strstr(json, "No supported MT-32 ROM images") != NULL);
	assert(set_count(json) == 0U);
	mt32_rom_inventory_free(json);
	assert(SDL_RemovePath(file_path.c_str()));
	assert(SDL_RemovePath(nested.c_str()));
	assert(SDL_RemovePath(directory.c_str()));

	/* Mute and normal AdLib startup must never call this loader. When called
	 * explicitly, unavailable ROM folders fail without inventing a default. */
	mt32emu_report_handler_i handler = {};
	mt32emu_context context = mt32emu_create_context(handler, NULL);
	assert(context != NULL);
	legacy_char error[ERROR_CAPACITY];
	assert(!mt32_rom_load(context, directory.c_str(), NULL, error, sizeof(error)));
	assert(SDL_strstr(error, "Cannot open MT-32 ROM folder") != NULL);
	mt32emu_free_context(context);
	mt32_rom_release(context);
}

legacy_int main()
{
	test_mutex_allocation_failure();
	test_concurrent_scans_before_sdl_init();
	test_complete_sets();
	test_split_images();
	test_missing_images();
	test_filesystem_scan();
	return 0;
}
