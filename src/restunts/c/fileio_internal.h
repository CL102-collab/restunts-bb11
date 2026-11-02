#ifndef RESTUNTS_FILEIO_INTERNAL_H
#define RESTUNTS_FILEIO_INTERNAL_H

#include "fileio.h"

#ifdef RESTUNTS_HEADLESS
extern const legacy_s8 headless_file_error[];
extern const legacy_s8 headless_write_error[];
extern const legacy_s8 headless_size_error[];
extern const legacy_s8 headless_pack_error[];
#define file_read_error_format headless_file_error
#define file_write_error_format headless_write_error
#define file_size_error_format headless_size_error
#define invalid_pack_type_error_format headless_pack_error
#endif

#ifdef RESTUNTS_AMIGA_TRACE
#define AMIGA_DECOMP_TRACE(stage)                                                                  \
	do {                                                                                           \
		puts("Amiga decompress: " stage);                                                          \
		fflush(stdout);                                                                            \
	} while (0)
#else
#define AMIGA_DECOMP_TRACE(stage) ((void)0)
#endif

#define FILE_IO_PAGE_SIZE 16384
#define FILE_IO_PAGE_SEGMENT_GAP 1024
#define DOS_FILENAME_LENGTH 13
#ifdef RESTUNTS_SDL3
#include "extended/extended_file_dialog.h"
#define FILE_FIND_NAME_SIZE EXTENDED_FILE_NAME_CAPACITY
#else
#define FILE_FIND_NAME_SIZE DOS_FILENAME_LENGTH
#endif
#define FILE_FIND_PATH_SIZE FILE_CAR_PATH_SIZE
#define FILE_COMBINE_PATH_SIZE 80
#define RESOURCE_NAME_BUFFER_SIZE 80

#define RS_RLE_ESCLEN_MAX 16
#define RS_RLE_ESCLOOKUP_LEN 256
#define RS_RLE_ESCSEQ_POS 1

#define RS_VLE_ESC_LEN 16
#define RS_VLE_ALPH_LEN 256
#define RS_VLE_ESC_WIDTH 64
#define RS_VLE_NUM_SYMB 128

#define DOS_PARAGRAPH_SHIFT 4U
#define DOS_PARAGRAPH_MASK 15L
#define BYTE_HIGH_BIT 128U
#define BYTE_SHIFT LEGACY_BYTE_BITS
#define BYTE_BIT_COUNT LEGACY_BYTE_BITS

enum RLE_COUNT_CODE { RLE_BYTE_COUNT_CODE = 1, RLE_WORD_COUNT_CODE = 3 };

#define RLE_SEQUENCE_TRAILER_SIZE 2
#define VLE_EXTENDED_CODE_START_WIDTH 7U
#define VLE_ALPHABET_BRANCH_FACTOR 2U
#define VLE_CODE_WORD_SECOND_BYTE_OFFSET 1U
#define COMPRESSION_WORKSPACE_PARAGRAPHS 4U

enum COMPRESSION_TYPE { COMPRESSION_RLE_TYPE = 1, COMPRESSION_VLE_TYPE = 2 };

#define FILE_ERROR_DIALOG_ABORT 2

#define COMPR_HEADER_SIZE 4U
#define COMPR_SIZE_LOW_OFFSET 1U
#define COMPR_SIZE_HIGH_OFFSET 3U

#define COMPR_RLE_HEADER_SIZE 5U
#define COMPR_RLE_SIZE_HIGH_OFFSET 2U
#define COMPR_RLE_ESCLEN_OFFSET 4U

typedef legacy_u16 fileio_handle;

#define FILEIO_INVALID_HANDLE 0U

legacy_u16 file_bytes_to_paras(legacy_s32 length);
const legacy_s8 *file_track_query(const legacy_s8 *filename);
const legacy_s8 *file_track_find_first(const legacy_s8 *query);
const legacy_s8 *file_track_find_next(void);
legacy_u16 file_car_resolve(const legacy_s8 *filename, legacy_s8 path[FILE_CAR_PATH_SIZE]);
void far *file_alloc_tail_pages(const legacy_s8 *filename, legacy_u16 paragraphs,
								legacy_u16 tail_bytes);
void far *file_decomp_internal(const legacy_s8 *filename, legacy_s16 fatal, legacy_u16 tail_bytes);

#endif
