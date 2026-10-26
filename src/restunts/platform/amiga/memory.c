#include "amiga.h"
#include "../../c/platform.h"
#include <exec/memory.h>
#include <proto/exec.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ARENA_FIRST_SEGMENT 0x1000U
#define ARENA_MAX_BYTES 0x90000UL
#define EXTERNAL_FIRST_SEGMENT 0xE000U
#define EXTERNAL_PAGE_COUNT 128U
#define ARENA_RETRY_STEP_BYTES 4096UL
#define PSP_BYTES 256U
#define AMIGA_VIDEO_BASE ((legacy_u32)AMIGA_VIDEO_SEGMENT * AMIGA_PARAGRAPH_BYTES)
static legacy_u8 *arena;
static legacy_u32 arena_bytes;
static legacy_u8 psp[PSP_BYTES];
static uintptr_t external_pages[EXTERNAL_PAGE_COUNT];
static legacy_u16 external_count;

static void memory_error(void)
{
	fputs("Invalid Amiga legacy memory address\n", stderr);
	dos_process_exit(1);
}

void *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	if (segment == 0 && offset == 0) {
		return NULL;
	}
	if (segment >= EXTERNAL_FIRST_SEGMENT) {
		legacy_u16 index = segment - EXTERNAL_FIRST_SEGMENT;
		if (index >= external_count) {
			memory_error();
			return NULL;
		}
		return (void *)(external_pages[index] + offset);
	}
	legacy_u32 address = (legacy_u32)segment * AMIGA_PARAGRAPH_BYTES + offset;
	if (address >= AMIGA_VIDEO_BASE && address < AMIGA_VIDEO_BASE + AMIGA_SCREEN_BYTES) {
		return amiga_video_pixels() + address - AMIGA_VIDEO_BASE;
	}
	if (address >= (legacy_u32)ARENA_FIRST_SEGMENT * AMIGA_PARAGRAPH_BYTES &&
		address - (legacy_u32)ARENA_FIRST_SEGMENT * AMIGA_PARAGRAPH_BYTES < arena_bytes) {
		return arena + address - (legacy_u32)ARENA_FIRST_SEGMENT * AMIGA_PARAGRAPH_BYTES;
	}
	memory_error();
	return NULL;
}

legacy_u16 dos_memory_pointer_segment(const void *pointer)
{
	uintptr_t address = (uintptr_t)pointer, base = (uintptr_t)arena;
	uintptr_t video = (uintptr_t)amiga_video_memory_base();
	if (pointer == NULL) {
		return 0;
	}
	if (video != 0 && address >= video && address < video + AMIGA_SCREEN_BYTES) {
		return AMIGA_VIDEO_SEGMENT + (legacy_u16)((address - video) >> AMIGA_PARAGRAPH_SHIFT);
	}
	if (arena != NULL && address >= base && address < base + arena_bytes) {
		return ARENA_FIRST_SEGMENT + (legacy_u16)((address - base) >> AMIGA_PARAGRAPH_SHIFT);
	}
	address &= ~(uintptr_t)LEGACY_U16_MAX;
	for (legacy_u16 index = 0; index < external_count; ++index) {
		if (external_pages[index] == address) {
			return EXTERNAL_FIRST_SEGMENT + index;
		}
	}
	if (external_count == EXTERNAL_PAGE_COUNT) {
		memory_error();
		return 0;
	}
	external_pages[external_count] = address;
	return EXTERNAL_FIRST_SEGMENT + external_count++;
}

legacy_u16 dos_memory_pointer_offset(const void *pointer)
{
	uintptr_t address = (uintptr_t)pointer, base = (uintptr_t)arena;
	uintptr_t video = (uintptr_t)amiga_video_memory_base();
	if (video != 0 && address >= video && address < video + AMIGA_SCREEN_BYTES) {
		return (legacy_u16)((address - video) & (AMIGA_PARAGRAPH_BYTES - 1UL));
	}
	if (arena != NULL && address >= base && address < base + arena_bytes) {
		return (legacy_u16)((address - base) & (AMIGA_PARAGRAPH_BYTES - 1UL));
	}
	return (legacy_u16)(address & LEGACY_U16_MAX);
}

void *dos_memory_make_near_pointer(legacy_u16 offset)
{
	if (offset != 0) {
		memory_error();
	}
	return NULL;
}

void *dos_memory_get_psp(void)
{
	return psp;
}

legacy_u16 dos_memory_allocate(legacy_u16 paragraphs)
{
	if (arena != NULL) {
		return paragraphs <= arena_bytes / AMIGA_PARAGRAPH_BYTES ? ARENA_FIRST_SEGMENT : 0;
	}
	/* Allocate actual OS/display resources before claiming the remaining
	 * public memory, keeping working space for audio, files and timer I/O. */
	dos_video_set_mode_13h();
	legacy_u32 available = AvailMem(MEMF_PUBLIC);
	legacy_u32 largest = AvailMem(MEMF_PUBLIC | MEMF_LARGEST);
	if (available <= AMIGA_SYSTEM_HEADROOM) {
		return 0;
	}
	available -= AMIGA_SYSTEM_HEADROOM;
	if (available > largest) {
		available = largest;
	}
	if (available > ARENA_MAX_BYTES) {
		available = ARENA_MAX_BYTES;
	}
	available &= ~(AMIGA_PARAGRAPH_BYTES - 1UL);
	/* Exec can fail when the largest free block changes between queries. */
	while (available >= (legacy_u32)paragraphs * AMIGA_PARAGRAPH_BYTES) {
		arena = AllocMem(available, MEMF_PUBLIC | MEMF_CLEAR);
		if (arena != NULL) {
			arena_bytes = available;
			return ARENA_FIRST_SEGMENT;
		}
		if (available < ARENA_RETRY_STEP_BYTES) {
			break;
		}
		available -= ARENA_RETRY_STEP_BYTES;
	}
	return 0;
}

legacy_u16 dos_memory_resize(legacy_u16 segment, legacy_u16 paragraphs)
{
	legacy_u16 available = (legacy_u16)(arena_bytes / AMIGA_PARAGRAPH_BYTES);
	if (segment != ARENA_FIRST_SEGMENT || arena == NULL) {
		return 0;
	}
	return paragraphs < available ? paragraphs : available;
}

void amiga_memory_shutdown(void)
{
	if (arena != NULL) {
		FreeMem(arena, arena_bytes);
	}
	arena = NULL;
	arena_bytes = 0;
	external_count = 0;
}
