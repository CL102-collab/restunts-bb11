#ifndef RESTUNTS_RESOURCE_H
#define RESTUNTS_RESOURCE_H

#include "legacy.h"

#define RESOURCE_FILE_SIZE_OFFSET 0U
#define RESOURCE_FILE_COUNT_OFFSET 4U
#define RESOURCE_FILE_DIRECTORY_OFFSET 6U
#define RESOURCE_FILE_COUNT_SIZE 2U
#define RESOURCE_FILE_IDENTIFIER_SIZE 4U
#define RESOURCE_FILE_OFFSET_SIZE 4U

void resource_file_set_size(legacy_u8 far *resource, legacy_u32 size);
legacy_u16 resource_file_count(const legacy_u8 far *resource);
const legacy_u8 far *resource_file_identifier(const legacy_u8 far *resource, legacy_u16 index);
void resource_file_set_offset(legacy_u8 far *resource, legacy_u16 count, legacy_u16 index,
							  legacy_u32 offset);
legacy_u16 resource_file_data_start(legacy_u16 count);
legacy_u8 far *resource_file_data(legacy_u8 far *resource, legacy_u16 index);

/* Match a NUL-terminated string within a validated resource payload. The name
 * must be a NUL-terminated identifier containing exactly four characters. */
legacy_u16 resource_text_equals(const legacy_u8 huge *resource, legacy_u32 length,
								const legacy_s8 *name, const legacy_s8 *expected);

legacy_s8 far *locate_resource(legacy_s8 far *data, const legacy_s8 *name, legacy_u16 fatal);
legacy_s8 far *locate_shape_nofatal(legacy_s8 far *data, const legacy_s8 *name);
legacy_s8 far *locate_shape_fatal(legacy_s8 far *data, const legacy_s8 *name);
legacy_s8 far *locate_shape_alt(legacy_s8 far *data, const legacy_s8 *name);

extern const legacy_s8 missing_shape_error_format[];

#endif
