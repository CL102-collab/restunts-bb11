#ifndef RESTUNTS_MEMMGR_H
#define RESTUNTS_MEMMGR_H

#include "legacy.h"
#include "resource.h"

#define MMGR_RESOURCE_NAME_LENGTH 12
#define MMGR_LEGACY_MEMCHUNK_SIZE_BYTES 18

#pragma pack(push, 1)
struct MEMCHUNK {
	legacy_s8 resname[MMGR_RESOURCE_NAME_LENGTH];
	legacy_u16 ressize;
	legacy_u16 resofs;
	legacy_u16 resstate;
};
#pragma pack(pop)

typedef char legacy_memchunk_must_be_18_bytes
	[(sizeof(struct MEMCHUNK) == MMGR_LEGACY_MEMCHUNK_SIZE_BYTES) ? 1 : -1];

const legacy_s8 *mmgr_path_to_name(const legacy_s8 *filename);
void far *mmgr_alloc_pages(const legacy_s8 *name, legacy_u16 paragraphs);
void mmgr_alloc_resmem(legacy_u16 end_segment);
void mmgr_init_conventional_arena(void);
legacy_u16 mmgr_get_ofs_diff(void);
void far *mmgr_free(legacy_s8 far *ptr);
void mmgr_copy_paras(legacy_u16 srcseg, legacy_u16 destseg, legacy_s16 paras);
void copy_paras_reverse(legacy_u16 srcseg, legacy_u16 destseg, legacy_s16 paras);
void mmgr_find_free();
void far *mmgr_get_chunk_by_name(const legacy_s8 *name);
void mmgr_release(void far *ptr);
legacy_u16 mmgr_get_chunk_size(legacy_s8 far *ptr);
legacy_u16 mmgr_resize_memory(legacy_u16 unused_offset, legacy_u16 segment, legacy_u16 paragraphs);
void far *mmgr_compact_live_chunk(legacy_s8 far *ptr);
void far *mmgr_alloc_resbytes(const legacy_s8 *name, legacy_s32 size);
legacy_u32 mmgr_get_res_ofs_diff_scaled(void);
legacy_u32 mmgr_get_chunk_size_bytes(legacy_s8 far *ptr);
/* Bytes remaining in an allocated chunk, including an interior pointer. */
legacy_u32 mmgr_get_pointer_size_bytes(const void far *ptr);

void mmgr_rename_chunk(legacy_s8 far *ptr, const legacy_s8 *name);

void locate_many_resources(legacy_s8 far *data, const legacy_s8 *names, legacy_s8 far **result);
legacy_s8 far *locate_text_res(legacy_s8 far *data, const legacy_s8 *name);

#endif
