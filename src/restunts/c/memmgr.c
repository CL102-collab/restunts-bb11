#include "externs.h"
#include "memmgr.h"
#include "platform.h"
#include "fatal.h"
#if defined(RESTUNTS_SDL3) && !defined(RESTUNTS_HEADLESS)
#include "hires/memory.h"
#endif

#define MMGR_RESOURCE_SENTINEL_INDEX 49

enum MMGR_RESOURCE_STATE {
	MMGR_RESOURCE_STATE_FREE = 0,
	MMGR_RESOURCE_STATE_CACHED = 1,
	MMGR_RESOURCE_STATE_LIVE = 2
};

#define MMGR_INITIAL_ARENA_PARAS 100U
#define MMGR_ARENA_END_SEGMENT 40960U
#define DOS_PARAGRAPH_BYTES 16L
#define DOS_PARAGRAPHS_PER_COPY_CHUNK 4096
#define DOS_WORDS_PER_COPY_CHUNK 32768U
#define DOS_WORDS_PER_PARAGRAPH_SHIFT 3U
#define DOS_BYTES_PER_PARAGRAPH_SHIFT 4U
#define DOS_BYTES_PER_WORD_SHIFT 1U
#define DOS_BYTES_PER_WORD 2U
#define RESOURCE_IDENTIFIER_LENGTH 4

legacy_u16 mmgr_arena_end_segment = 0;	 // last para reserved by memmgr
legacy_u16 mmgr_arena_start_segment = 0; // first para reserved by memmgr
legacy_u16 mmgr_high_water_segment = 0;	 // highest segment reached by live allocations
legacy_u16 pspofs = 0;
legacy_u16 pspseg = 0;

struct MEMCHUNK resources[] = {
	{{' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}, 0, 0, MMGR_RESOURCE_STATE_LIVE},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},

	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},

	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{{' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
	 0,
	 0,
	 MMGR_RESOURCE_STATE_CACHED},
};
struct MEMCHUNK *mmgr_first_cached_chunk = &resources[MMGR_RESOURCE_SENTINEL_INDEX];
struct MEMCHUNK *mmgr_cache_sentinel = &resources[MMGR_RESOURCE_SENTINEL_INDEX];
struct MEMCHUNK *mmgr_live_sentinel = resources;
struct MEMCHUNK *mmgr_last_live_chunk = resources;

const legacy_s8 *mmgr_path_to_name(const legacy_s8 *filename)
{
	const legacy_s8 *result = filename;
	for (const legacy_s8 *c = filename; *c; c++) {
		if (*c == ':' || *c == '\\' || *c == '/') {
			result = c + 1;
		}
	}

	return result;
}

static void mmgr_copy_resource_name(legacy_s8 *destination, const legacy_s8 *name)
{
	const legacy_s8 *basename = mmgr_path_to_name(name);
	legacy_s16 name_index = 0;
	/* Keep all twelve characters of full-length names; bytes after a shorter
	 * name's terminator do not participate in cache matching. */
	for (; name_index < MMGR_RESOURCE_NAME_LENGTH && basename[name_index] != 0; name_index++) {
		destination[name_index] = basename[name_index];
	}
	for (; name_index < MMGR_RESOURCE_NAME_LENGTH; name_index++) {
		destination[name_index] = 0;
	}
}

void far *mmgr_alloc_pages(const legacy_s8 *name, legacy_u16 paragraphs)
{
	struct MEMCHUNK *live_chunk = mmgr_last_live_chunk;
	struct MEMCHUNK *cached_chunk = mmgr_first_cached_chunk;
	legacy_u16 start_segment = live_chunk->resofs + live_chunk->ressize;

	live_chunk++;
	if (cached_chunk <= live_chunk) {
		if (cached_chunk == mmgr_cache_sentinel) {
			fatal_error("reservememory - OUT OF MEMORY SLOTS RESERVING %s", name);
		}

		cached_chunk++;
		mmgr_first_cached_chunk = cached_chunk;
	}

	mmgr_last_live_chunk = live_chunk;
	mmgr_copy_resource_name(live_chunk->resname, name);

	legacy_u16 size_or_end_segment = paragraphs;
	live_chunk->resofs = start_segment;
	live_chunk->ressize = size_or_end_segment;
	live_chunk->resstate = MMGR_RESOURCE_STATE_LIVE;

	size_or_end_segment += start_segment;
	if (size_or_end_segment > mmgr_high_water_segment) {
		mmgr_high_water_segment = size_or_end_segment;
	}

	if (size_or_end_segment > cached_chunk->resofs) {
		cached_chunk = mmgr_first_cached_chunk;
		live_chunk = mmgr_last_live_chunk;
		size_or_end_segment = live_chunk->resofs + live_chunk->ressize;

		while (size_or_end_segment > cached_chunk->resofs) {
			if (cached_chunk == mmgr_cache_sentinel) {
				fatal_error("reservememory - OUT OF MEMORY RESERVING %s P=%x HW=%x\r\n", name,
							live_chunk->ressize, mmgr_high_water_segment);
			}

			cached_chunk->resstate = MMGR_RESOURCE_STATE_FREE;
			cached_chunk++;
			mmgr_first_cached_chunk = cached_chunk;
		}
	}

#if defined(RESTUNTS_SDL3) && !defined(RESTUNTS_HEADLESS)
	hires_forget_paragraphs(start_segment, paragraphs);
#endif
	return dos_memory_make_pointer(start_segment, 0);
}

void far *mmgr_alloc_resbytes(const legacy_s8 *name, legacy_s32 size)
{
	/* The original allocator always reserves one paragraph after division. */
	return mmgr_alloc_pages(name, (legacy_u16)LEGACY_S32_WRAP_ADD(
									  LEGACY_S32_DIV_OR_ZERO(size, DOS_PARAGRAPH_BYTES), 1L));
}

void mmgr_alloc_resmem(legacy_u16 end_segment)
{
	void far *psp = dos_memory_get_psp();
	pspseg = dos_memory_pointer_segment(psp);
	pspofs = dos_memory_pointer_offset(psp);

	if (mmgr_arena_end_segment == 0) {
		mmgr_live_sentinel->resofs = dos_memory_allocate(MMGR_INITIAL_ARENA_PARAS);
		mmgr_arena_start_segment = mmgr_live_sentinel->resofs;
		legacy_u16 maxblocks =
			dos_memory_resize(mmgr_live_sentinel->resofs, end_segment - mmgr_live_sentinel->resofs);
		maxblocks = dos_memory_resize(mmgr_live_sentinel->resofs, maxblocks);
		mmgr_cache_sentinel->resofs = mmgr_arena_start_segment + maxblocks;
		mmgr_arena_end_segment = mmgr_cache_sentinel->resofs;
		//fatal_error("%u\n", mmgr_arena_end_segment - mmgr_arena_start_segment);
	}
#if defined(RESTUNTS_SDL3) && !defined(RESTUNTS_HEADLESS)
	hires_forget_paragraphs(mmgr_arena_start_segment,
							mmgr_arena_end_segment - mmgr_arena_start_segment);
#endif
	mmgr_first_cached_chunk = mmgr_cache_sentinel;
	mmgr_last_live_chunk = mmgr_live_sentinel;

	struct MEMCHUNK *chunk = mmgr_live_sentinel;
	for (;;) {
		chunk++;
		if (chunk == mmgr_cache_sentinel) {
			break;
		}
		chunk->resstate = MMGR_RESOURCE_STATE_FREE;
	}
}

void mmgr_init_conventional_arena(void)
{
	mmgr_alloc_resmem(MMGR_ARENA_END_SEGMENT);
}

legacy_u16 mmgr_get_ofs_diff(void)
{
	return mmgr_cache_sentinel->resofs - mmgr_last_live_chunk->resofs -
		   mmgr_last_live_chunk->ressize;
}

legacy_u16 mmgr_available_paragraphs(void)
{
	return mmgr_get_ofs_diff();
}

legacy_u16 mmgr_allocated_paragraphs(void)
{
	return (legacy_u16)(mmgr_last_live_chunk->resofs + mmgr_last_live_chunk->ressize -
						mmgr_live_sentinel->resofs);
}

#define MMGR_FIND_ARENA_CHUNK(chunk, segment)                                                      \
	while (1) {                                                                                    \
		if (chunk == mmgr_live_sentinel) {                                                         \
			fatal_error("memory manager - BLOCK NOT FOUND at SEG= %x", segment);                   \
		}                                                                                          \
		if (chunk->resofs == segment) {                                                            \
			break;                                                                                 \
		}                                                                                          \
		chunk--;                                                                                   \
	}

void far *mmgr_free(legacy_s8 far *ptr)
{
	struct MEMCHUNK *chunk = mmgr_last_live_chunk;
	legacy_u16 ptrseg = dos_memory_pointer_segment(ptr);

	MMGR_FIND_ARENA_CHUNK(chunk, ptrseg);

#if defined(RESTUNTS_SDL3) && !defined(RESTUNTS_HEADLESS)
	hires_forget_paragraphs(chunk->resofs, chunk->ressize);
#endif

	ptrseg = 0;
	chunk->resstate = MMGR_RESOURCE_STATE_FREE;
	legacy_u16 free_paragraphs = mmgr_first_cached_chunk->resofs - mmgr_last_live_chunk->resofs -
								 mmgr_last_live_chunk->ressize;
	/* Only the last live block can reuse its own descriptor for caching. */
	if (chunk == mmgr_last_live_chunk ||
		(mmgr_first_cached_chunk > mmgr_last_live_chunk + 1 && free_paragraphs >= chunk->ressize)) {
		/* A full table reuses the source descriptor for the cached block. */
		legacy_u16 source_segment = chunk->resofs;
		legacy_u16 source_paragraphs = chunk->ressize;
		ptrseg = mmgr_first_cached_chunk->resofs - source_paragraphs;
		mmgr_first_cached_chunk--;
		mmgr_first_cached_chunk->resofs = ptrseg;
		mmgr_first_cached_chunk->ressize = source_paragraphs;
		mmgr_first_cached_chunk->resstate = MMGR_RESOURCE_STATE_CACHED;

		for (legacy_s16 name_index = 0; name_index < MMGR_RESOURCE_NAME_LENGTH; name_index++) {
			mmgr_first_cached_chunk->resname[name_index] = chunk->resname[name_index];
		}

		copy_paras_reverse(source_segment, ptrseg, source_paragraphs);
	}

	if (chunk == mmgr_last_live_chunk) {
		do {
			chunk--;
		} while (chunk->resstate == MMGR_RESOURCE_STATE_FREE);
		mmgr_last_live_chunk = chunk;
	}

	return dos_memory_make_pointer(ptrseg, dos_memory_pointer_offset(ptr));
}

// `paras` is signed here but the original treats it as unsigned: it steps the
// count down by 4096 paragraphs and tests the borrow with jnb. Counts from
// 32768 through 36863 wrap into the non-negative range after that subtraction
// and still chunk correctly here; the divergence starts at 36864 (576 KiB),
// where the signed remainder test is taken despite no unsigned borrow. Nothing
// asks either copier for that much at once.
void mmgr_copy_paras(legacy_u16 srcseg, legacy_u16 destseg, legacy_s16 paras)
{
#if defined(RESTUNTS_SDL3) && !defined(RESTUNTS_HEADLESS)
	hires_forget_paragraphs(destseg, (legacy_u16)paras);
#endif

	legacy_u16 count; // number of words to copy

	while (paras != 0) {
		count = DOS_WORDS_PER_COPY_CHUNK;
		paras -= DOS_PARAGRAPHS_PER_COPY_CHUNK;
		if (paras < 0) {
			count = (paras + DOS_PARAGRAPHS_PER_COPY_CHUNK) << DOS_WORDS_PER_PARAGRAPH_SHIFT;
			paras = 0;
		}
		legacy_u16 far *srcptr = dos_memory_make_pointer(srcseg, 0);
		legacy_u16 far *destptr = dos_memory_make_pointer(destseg, 0);

		while (count) {
			*destptr = *srcptr;
			srcptr++;
			destptr++;
			count--;
		}

		srcseg += DOS_PARAGRAPHS_PER_COPY_CHUNK;
		destseg += DOS_PARAGRAPHS_PER_COPY_CHUNK;
	}
}

// Same signedness caveat as mmgr_copy_paras above: the original's loop guard
// subtracts 4096 paragraphs and uses an unsigned no-borrow branch.
void copy_paras_reverse(legacy_u16 srcseg, legacy_u16 destseg, legacy_s16 paras)
{
#if defined(RESTUNTS_SDL3) && !defined(RESTUNTS_HEADLESS)
	hires_forget_paragraphs(destseg, (legacy_u16)paras);
#endif

	srcseg += paras;
	destseg += paras;

	while (paras != 0) {
		legacy_u16 count = DOS_PARAGRAPHS_PER_COPY_CHUNK;
		paras -= DOS_PARAGRAPHS_PER_COPY_CHUNK;
		if (paras < 0) {
			count = paras + DOS_PARAGRAPHS_PER_COPY_CHUNK;
			paras = 0;
		}
		srcseg -= count;
		destseg -= count;
		count <<= DOS_WORDS_PER_PARAGRAPH_SHIFT;
		legacy_u16 ofs = (count << DOS_BYTES_PER_WORD_SHIFT) - DOS_BYTES_PER_WORD;

		legacy_u16 far *srcptr = dos_memory_make_pointer(srcseg, ofs);
		legacy_u16 far *destptr = dos_memory_make_pointer(destseg, ofs);
		while (count) {
			*destptr = *srcptr;
			srcptr--;
			destptr--;
			count--;
		}
	}
}

void mmgr_find_free(void)
{
	struct MEMCHUNK *source_chunk = mmgr_cache_sentinel;
	struct MEMCHUNK *destination_chunk = source_chunk;
	legacy_u16 free_paragraphs = 0;

	do {
		if ((source_chunk->resstate & 1) == 0) {
			free_paragraphs += source_chunk->ressize;
		} else {
			if (free_paragraphs != 0) {
				destination_chunk++;
				legacy_u16 destination_segment = destination_chunk->resofs - source_chunk->ressize;
				destination_chunk--;
				destination_chunk->ressize = source_chunk->ressize;
				destination_chunk->resofs = destination_segment;
				legacy_u16 resstate = source_chunk->resstate;
				source_chunk->resstate = MMGR_RESOURCE_STATE_FREE;
				destination_chunk->resstate = resstate;
				for (legacy_s16 name_index = 0; name_index < MMGR_RESOURCE_NAME_LENGTH;
					 name_index++) {
					destination_chunk->resname[name_index] = source_chunk->resname[name_index];
				}
				copy_paras_reverse(source_chunk->resofs, destination_segment,
								   source_chunk->ressize);
			}

			destination_chunk--;
		}
		source_chunk--;
		// `cmp si, mmgr_first_cached_chunk / jnb` - the entry at mmgr_first_cached_chunk is the
		// last one
		// the original visits, not one past the end.
	} while (source_chunk >= mmgr_first_cached_chunk);

	destination_chunk++;
	mmgr_first_cached_chunk = destination_chunk;
}

void far *mmgr_get_chunk_by_name(const legacy_s8 *name)
{
	const legacy_s8 *wanted_name = mmgr_path_to_name(name);

	struct MEMCHUNK *cached_chunk = mmgr_first_cached_chunk;

	legacy_s16 found = 0;
	struct MEMCHUNK *live_chunk;
	for (; cached_chunk < mmgr_cache_sentinel; cached_chunk++) {
		if (cached_chunk->resstate == MMGR_RESOURCE_STATE_FREE) {
			return 0;
		}

		legacy_s16 name_index = 0;
		for (; name_index < MMGR_RESOURCE_NAME_LENGTH; name_index++) {
			if (wanted_name[name_index] == 0) {
				if (cached_chunk->resname[name_index] == '.' ||
					cached_chunk->resname[name_index] == 0) {
					found = 1;
				}
				break;
			}
			if (wanted_name[name_index] != cached_chunk->resname[name_index]) {
				break;
			}
		}
		if (name_index == MMGR_RESOURCE_NAME_LENGTH || found == 1) {
			/* Restore the cached block exactly as the original allocator does. */
			legacy_u16 srcofs = cached_chunk->resofs;
			legacy_u16 srcsize = cached_chunk->ressize;
			legacy_u16 destofs = mmgr_last_live_chunk->resofs + mmgr_last_live_chunk->ressize;
			cached_chunk->resstate = MMGR_RESOURCE_STATE_FREE;
			live_chunk = mmgr_last_live_chunk + 1;
			mmgr_last_live_chunk = live_chunk;
			live_chunk->resofs = destofs;
			live_chunk->ressize = srcsize;
			live_chunk->resstate = MMGR_RESOURCE_STATE_LIVE;
			memcpy(live_chunk->resname, cached_chunk->resname,
				   sizeof(legacy_s8[MMGR_RESOURCE_NAME_LENGTH]));
			if (live_chunk == mmgr_first_cached_chunk) {
				mmgr_first_cached_chunk++;
			}
			mmgr_copy_paras(srcofs, destofs, srcsize);
			legacy_s16 allocation_end_segment = destofs + srcsize;
			while (allocation_end_segment > mmgr_first_cached_chunk->resofs) {
				mmgr_first_cached_chunk->resstate = MMGR_RESOURCE_STATE_FREE;
				mmgr_first_cached_chunk++;
			}
			mmgr_find_free();
			return dos_memory_make_pointer(live_chunk->resofs, 0);
		}
	}

	return 0;
}

legacy_u16 mmgr_has_cached_resource(const legacy_s8 *name)
{
	const legacy_s8 *wanted = mmgr_path_to_name(name);
	struct MEMCHUNK *chunk = mmgr_first_cached_chunk;
	legacy_s16 i;
	while (chunk < mmgr_cache_sentinel) {
		if (chunk->resstate == MMGR_RESOURCE_STATE_FREE) {
			return 0;
		}
		for (i = 0; i < MMGR_RESOURCE_NAME_LENGTH; i++) {
			if (wanted[i] == 0) {
				if (chunk->resname[i] == '.' || chunk->resname[i] == 0) {
					return 1;
				}
				break;
			}
			if (wanted[i] != chunk->resname[i]) {
				break;
			}
		}
		if (i == MMGR_RESOURCE_NAME_LENGTH) {
			return 1;
		}
		chunk++;
	}
	return 0;
}

void mmgr_release(void far *ptr)
{
	legacy_u16 segment = dos_memory_pointer_segment(ptr);
	struct MEMCHUNK *chunk = mmgr_last_live_chunk;

	MMGR_FIND_ARENA_CHUNK(chunk, segment);

#if defined(RESTUNTS_SDL3) && !defined(RESTUNTS_HEADLESS)
	hires_forget_paragraphs(chunk->resofs, chunk->ressize);
#endif

	chunk->resstate = MMGR_RESOURCE_STATE_FREE;
	if (chunk == mmgr_last_live_chunk) {
		do {
			chunk--;
		} while (chunk->resstate == MMGR_RESOURCE_STATE_FREE);
		mmgr_last_live_chunk = chunk;
	}
}

// Rename a live arena chunk, so a buffer filled under one name can be handed
// on under the name the caller expects.
void mmgr_rename_chunk(legacy_s8 far *ptr, const legacy_s8 *name)
{
	legacy_u16 segment = dos_memory_pointer_segment(ptr);
	struct MEMCHUNK *chunk = mmgr_last_live_chunk;

	MMGR_FIND_ARENA_CHUNK(chunk, segment);

	mmgr_copy_resource_name(chunk->resname, name);
}

legacy_u16 mmgr_get_chunk_size(legacy_s8 far *ptr)
{
	legacy_u16 segment = dos_memory_pointer_segment(ptr);
	struct MEMCHUNK *chunk = mmgr_last_live_chunk;

	MMGR_FIND_ARENA_CHUNK(chunk, segment);
	return chunk->ressize;
}

legacy_u16 mmgr_resize_memory(legacy_u16 unused_offset, legacy_u16 segment, legacy_u16 paragraphs)
{
	(void)unused_offset;
	legacy_u16 size_or_end_segment = segment;
	struct MEMCHUNK *chunk = mmgr_last_live_chunk;

	MMGR_FIND_ARENA_CHUNK(chunk, segment);

#if defined(RESTUNTS_SDL3) && !defined(RESTUNTS_HEADLESS)
	hires_resize_paragraphs(chunk->resofs, chunk->ressize, paragraphs);
#endif
	size_or_end_segment = paragraphs;
	if (size_or_end_segment <= chunk->ressize) {
		chunk->ressize = size_or_end_segment;
		return size_or_end_segment;
	}

	if (chunk != mmgr_last_live_chunk) {
		fatal_error("resizememory - CANNOT EXPAND BLOCK NOT AT TOP");
	}
	chunk->ressize = size_or_end_segment;
	struct MEMCHUNK *limit_chunk = mmgr_first_cached_chunk;
	size_or_end_segment += chunk->resofs;
	if (size_or_end_segment >= mmgr_high_water_segment) {
		mmgr_high_water_segment = size_or_end_segment;
	}

	if (size_or_end_segment <= limit_chunk->resofs) {
		return 0;
	}

	chunk = mmgr_first_cached_chunk;
	limit_chunk = mmgr_last_live_chunk;
	size_or_end_segment = limit_chunk->resofs + limit_chunk->ressize;

	for (;;) {
		if (size_or_end_segment <= chunk->resofs) {
			break;
		}
		if (chunk == mmgr_cache_sentinel) {
			fatal_error("resizememory - NO MEMORY LEFT TO EXPAND HW=%x", mmgr_high_water_segment);
		}

		chunk->resstate = MMGR_RESOURCE_STATE_FREE;
		chunk++;
		mmgr_first_cached_chunk = chunk;
	}
	return 0;
}

void far *mmgr_compact_live_chunk(legacy_s8 far *ptr)
{
	legacy_u16 destination_segment = dos_memory_pointer_segment(ptr);
	struct MEMCHUNK *source_chunk = mmgr_last_live_chunk;

	MMGR_FIND_ARENA_CHUNK(source_chunk, destination_segment);

	struct MEMCHUNK *destination_chunk = source_chunk;
	destination_chunk--;
	if (destination_chunk->resstate == MMGR_RESOURCE_STATE_FREE) {
		do {
			destination_chunk--;
		} while (destination_chunk->resstate == MMGR_RESOURCE_STATE_FREE);

		source_chunk->resstate = MMGR_RESOURCE_STATE_FREE;
		destination_segment = destination_chunk->resofs + destination_chunk->ressize;
		destination_chunk++;
		if (source_chunk == mmgr_last_live_chunk) {
			mmgr_last_live_chunk = destination_chunk;
		}

		destination_chunk->resofs = destination_segment;
		destination_chunk->ressize = source_chunk->ressize;
		destination_chunk->resstate = MMGR_RESOURCE_STATE_LIVE;
		for (legacy_s16 name_index = 0; name_index < MMGR_RESOURCE_NAME_LENGTH; name_index++) {
			destination_chunk->resname[name_index] = source_chunk->resname[name_index];
		}
#if defined(RESTUNTS_SDL3) && !defined(RESTUNTS_HEADLESS)
		hires_forget_paragraphs(source_chunk->resofs, source_chunk->ressize);
#endif
		mmgr_copy_paras(source_chunk->resofs, destination_segment, source_chunk->ressize);

	} else {
		destination_chunk = source_chunk;
	}

	return dos_memory_make_pointer(destination_chunk->resofs, 0);
}

legacy_u32 mmgr_get_res_ofs_diff_scaled(void)
{
	legacy_u32 result = mmgr_get_ofs_diff();
	return result << DOS_BYTES_PER_PARAGRAPH_SHIFT;
}

legacy_u32 mmgr_get_chunk_size_bytes(legacy_s8 far *ptr)
{
	legacy_u32 result = mmgr_get_chunk_size(ptr);
	return result << DOS_BYTES_PER_PARAGRAPH_SHIFT;
}
//#endif

void locate_many_resources(legacy_s8 far *data, const legacy_s8 *names, legacy_s8 far **result)
{
	while (*names != 0) {
		*result = locate_shape_fatal(data, names);
		names += RESOURCE_IDENTIFIER_LENGTH;
		result++;
	}
}

static void locate_many_resources_nofatal(legacy_s8 far *data, const legacy_s8 *names,
										  legacy_s8 far **result)
{
	while (*names != 0) {
		*result = locate_shape_nofatal(data, names);
		names += RESOURCE_IDENTIFIER_LENGTH;
		result++;
	}
}

void locate_many_shapes_nofatal(legacy_s8 far *data, const legacy_s8 *names, legacy_s8 far **result)
{
	locate_many_resources_nofatal(data, names, result);
}

void locate_many_sounds_fatal(legacy_s8 far *data, const legacy_s8 *names, legacy_s8 far **result)
{
	while (*names != 0) {
		*result = locate_sound_fatal(data, names);
		names += RESOURCE_IDENTIFIER_LENGTH;
		result++;
	}
}

void locate_many_resources_optional(legacy_s8 far *data, const legacy_s8 *names,
									legacy_s8 far **result)
{
	locate_many_resources_nofatal(data, names, result);
}

legacy_s8 far *locate_text_res(legacy_s8 far *data, const legacy_s8 *name)
{
	legacy_s8 textname[4];
	textname[0] = textresprefix;
	textname[1] = name[0];
	textname[2] = name[1];
	textname[3] = name[2];
	return locate_shape_fatal(data, textname);
}
