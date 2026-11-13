#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../c/platform.h"
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__) &&                                       \
	(!defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__))
#include <SDL3/SDL.h>
#endif
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__)
#include "audio_resources_worker.h"
#endif

#define MEMORY_BYTES 0x100000UL
#define MEMORY_FIRST_SEGMENT 0x1000U
#define MEMORY_END_SEGMENT 0xA000U
#define EXTERNAL_FIRST_SEGMENT 0xE000U
#define EXTERNAL_PAGE_COUNT 512U
#define EXTERNAL_PAGE_MASK LEGACY_U16_MAX
#define MEMORY_PARAGRAPH_BYTES 16U
#define MEMORY_PARAGRAPH_SHIFT 4U
#define MEMORY_PSP_ADDRESS 0x028E0UL

/* Resource files contain real 16:16 addresses after relocation. Keep their
 * address space independent of the host pointer size, including VGA A000:0.
 * Native static/stack objects use registered pages outside the resource arena. */
static legacy_u8 memory[MEMORY_BYTES];
static uintptr_t external_pages[EXTERNAL_PAGE_COUNT];
static legacy_u16 external_page_count;

#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__) &&                                       \
	(!defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__))
/* This registry exists for the process lifetime and can be used before SDL_Init.
 * Publish its mutex atomically so independent first registrations are safe. */
static void *external_mutex;

static SDL_Mutex *memory_registry_lock(void)
{
	SDL_Mutex *mutex = SDL_GetAtomicPointer(&external_mutex);
	if (mutex == NULL) {
		SDL_Mutex *created = SDL_CreateMutex();
		if (created == NULL) {
			return NULL;
		}
		if (SDL_CompareAndSwapAtomicPointer(&external_mutex, NULL, created)) {
			mutex = created;
		} else {
			SDL_DestroyMutex(created);
			mutex = SDL_GetAtomicPointer(&external_mutex);
		}
	}
	SDL_LockMutex(mutex);
	return mutex;
}
#endif

static void memory_error(void)
{
	fputs("Invalid legacy memory address\n", stderr);
	dos_process_exit(1);
}

static legacy_u16 external_segment(uintptr_t address)
{
	uintptr_t page = address & ~(uintptr_t)EXTERNAL_PAGE_MASK;
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__) &&                                       \
	(!defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__))
	SDL_Mutex *mutex = memory_registry_lock();
	if (mutex == NULL) {
		memory_error();
		return 0;
	}
#endif
	legacy_u16 result = 0;
	for (legacy_u16 index = 0; index < external_page_count; index++) {
		if (external_pages[index] == page) {
			result = EXTERNAL_FIRST_SEGMENT + index;
			break;
		}
	}
	if (result == 0 && external_page_count < EXTERNAL_PAGE_COUNT) {
		external_pages[external_page_count] = page;
		result = EXTERNAL_FIRST_SEGMENT + external_page_count++;
	}
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__) &&                                       \
	(!defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__))
	SDL_UnlockMutex(mutex);
#endif
	if (result == 0) {
		memory_error();
	}
	return result;
}

void *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	if (segment == 0 && offset == 0) {
		return NULL;
	}
	if (segment >= EXTERNAL_FIRST_SEGMENT) {
		legacy_u16 index = segment - EXTERNAL_FIRST_SEGMENT;
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__) &&                                       \
	(!defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__))
		SDL_Mutex *mutex = memory_registry_lock();
		if (mutex == NULL) {
			memory_error();
			return NULL;
		}
#endif
		legacy_u8 valid = index < external_page_count;
		uintptr_t page = valid ? external_pages[index] : 0;
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__) &&                                       \
	(!defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__))
		SDL_UnlockMutex(mutex);
#endif
		if (valid == 0) {
			memory_error();
			return NULL;
		}
		return (void *)(page + offset);
	}
	legacy_u32 address = (legacy_u32)segment * MEMORY_PARAGRAPH_BYTES + offset;
	if (address >= MEMORY_BYTES) {
		memory_error();
		return NULL;
	}
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__)
	if (sdl3_audio_resources_is_execution()) {
		return sdl3_audio_resources_arena() + address;
	}
#endif
	return memory + address;
}

legacy_u16 dos_memory_pointer_segment(const void *pointer)
{
	uintptr_t address = (uintptr_t)pointer;
	uintptr_t base = (uintptr_t)memory;
	if (pointer == NULL) {
		return 0;
	}
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__)
	uintptr_t audio_base = (uintptr_t)sdl3_audio_resources_arena();
	if (address >= audio_base && address - audio_base < AUDIO_RESOURCE_WORKER_MEMORY_BYTES) {
		return (legacy_u16)((address - audio_base) >> MEMORY_PARAGRAPH_SHIFT);
	}
#endif
	if (address >= base && address < base + MEMORY_BYTES) {
		return (legacy_u16)((address - base) >> MEMORY_PARAGRAPH_SHIFT);
	}
	return external_segment(address);
}

legacy_u16 dos_memory_pointer_offset(const void *pointer)
{
	uintptr_t address = (uintptr_t)pointer;
	uintptr_t base = (uintptr_t)memory;
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__)
	uintptr_t audio_base = (uintptr_t)sdl3_audio_resources_arena();
	if (address >= audio_base && address - audio_base < AUDIO_RESOURCE_WORKER_MEMORY_BYTES) {
		return (legacy_u16)((address - audio_base) & (MEMORY_PARAGRAPH_BYTES - 1U));
	}
#endif
	if (address >= base && address < base + MEMORY_BYTES) {
		return (legacy_u16)((address - base) & (MEMORY_PARAGRAPH_BYTES - 1U));
	}
	return (legacy_u16)(address & EXTERNAL_PAGE_MASK);
}

void *dos_memory_make_near_pointer(legacy_u16 offset)
{
	/* Native callers use real pointers; this is only the legacy null case. */
	if (offset != 0) {
		memory_error();
	}
	return NULL;
}

void *dos_memory_get_psp(void)
{
	return memory + MEMORY_PSP_ADDRESS;
}

legacy_u16 dos_memory_allocate(legacy_u16 paragraphs)
{
	return paragraphs <= MEMORY_END_SEGMENT - MEMORY_FIRST_SEGMENT ? MEMORY_FIRST_SEGMENT : 0;
}

legacy_u16 dos_memory_resize(legacy_u16 segment, legacy_u16 paragraphs)
{
	if (segment != MEMORY_FIRST_SEGMENT) {
		return 0;
	}
	legacy_u16 available = MEMORY_END_SEGMENT - segment;
	return paragraphs < available ? paragraphs : available;
}
