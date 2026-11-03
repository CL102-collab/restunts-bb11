#include "resource.h"
#include "fatal.h"
#include "resource_bytes.h"
#include "platform.h"

legacy_u16 resource_read_u16le(const legacy_u8 far *source)
{
	return LEGACY_READ_U16_LE(source);
}

legacy_u32 resource_read_u32le(const legacy_u8 far *source)
{
	return LEGACY_READ_U32_LE(source);
}

enum RESOURCE_NOT_FOUND_BEHAVIOR {
	RESOURCE_NOT_FOUND_IGNORE = 0,
	RESOURCE_NOT_FOUND_SHAPE_FATAL = 1
};

#define RESOURCE_IDENTIFIER_PADDING ' '
#define RESOURCE_PARAGRAPH_SHIFT 4U
#define RESOURCE_PARAGRAPH_OFFSET_MASK 15U

static const legacy_u8 far *resource_file_offset_bytes(const legacy_u8 far *resource,
													   legacy_u16 count, legacy_u16 index)
{
	return resource + RESOURCE_FILE_DIRECTORY_OFFSET +
		   LEGACY_U16_WRAP_MUL(count, RESOURCE_FILE_IDENTIFIER_SIZE) +
		   LEGACY_U16_WRAP_MUL(index, RESOURCE_FILE_OFFSET_SIZE);
}

void resource_file_set_size(legacy_u8 far *resource, legacy_u32 size)
{
	LEGACY_WRITE_U32_LE(resource + RESOURCE_FILE_SIZE_OFFSET, size);
}

legacy_u16 resource_file_count(const legacy_u8 far *resource)
{
	return LEGACY_READ_U16_LE(resource + RESOURCE_FILE_COUNT_OFFSET);
}

const legacy_u8 far *resource_file_identifier(const legacy_u8 far *resource, legacy_u16 index)
{
	return resource + RESOURCE_FILE_DIRECTORY_OFFSET +
		   LEGACY_U16_WRAP_MUL(index, RESOURCE_FILE_IDENTIFIER_SIZE);
}

void resource_file_set_offset(legacy_u8 far *resource, legacy_u16 count, legacy_u16 index,
							  legacy_u32 offset)
{
	LEGACY_WRITE_U32_LE((legacy_u8 far *)resource_file_offset_bytes(resource, count, index),
						offset);
}

legacy_u16 resource_file_data_start(legacy_u16 count)
{
	return LEGACY_U16_WRAP_ADD(
		RESOURCE_FILE_DIRECTORY_OFFSET,
		LEGACY_U16_WRAP_MUL(count, RESOURCE_FILE_IDENTIFIER_SIZE + RESOURCE_FILE_OFFSET_SIZE));
}

legacy_u8 far *resource_file_data(legacy_u8 far *resource, legacy_u16 index)
{
	legacy_u16 count = resource_file_count(resource);
	legacy_u8 huge *result = resource;
	result += (legacy_u32)resource_file_data_start(count) +
			  LEGACY_READ_U32_LE(resource_file_offset_bytes(resource, count, index));
#if defined(RESTUNTS_DOS16)
	/* A huge-pointer addition need not normalize its offset in Watcom. The
	 * original resource lookup returned paragraph-normalized pointers, and
	 * the 16-bit blitters deliberately wrap offsets within that segment.
	 * Preserve that representation so a payload near a 64 KiB boundary is
	 * read continuously rather than wrapping into an earlier resource. */
	return (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(result) +
			(dos_memory_pointer_offset(result) >> RESOURCE_PARAGRAPH_SHIFT),
		dos_memory_pointer_offset(result) & RESOURCE_PARAGRAPH_OFFSET_MASK);
#else
	return (legacy_u8 far *)result;
#endif
}

legacy_u16 resource_text_equals(const legacy_u8 huge *resource, legacy_u32 length,
								const legacy_s8 *name, const legacy_s8 *expected)
{
	if (!resource || !name || !expected || length < RESOURCE_FILE_DIRECTORY_OFFSET) {
		return 0;
	}
	for (legacy_u16 character = 0; character < RESOURCE_FILE_IDENTIFIER_SIZE; ++character) {
		if (!name[character]) {
			return 0;
		}
	}
	if (name[RESOURCE_FILE_IDENTIFIER_SIZE]) {
		return 0;
	}

	legacy_u32 declared_length = LEGACY_READ_U32_LE(resource + RESOURCE_FILE_SIZE_OFFSET);
	if (declared_length < RESOURCE_FILE_DIRECTORY_OFFSET || declared_length > length) {
		return 0;
	}
	legacy_u32 count = LEGACY_READ_U16_LE(resource + RESOURCE_FILE_COUNT_OFFSET);
	legacy_u32 entry_size = RESOURCE_FILE_IDENTIFIER_SIZE + RESOURCE_FILE_OFFSET_SIZE;
	if (count > (declared_length - RESOURCE_FILE_DIRECTORY_OFFSET) / entry_size) {
		return 0;
	}
	/* The existing directory accessors intentionally reproduce 16-bit wrapping.
	 * Validate external data with wide arithmetic and huge pointers instead. */
	legacy_u32 offsets_start =
		RESOURCE_FILE_DIRECTORY_OFFSET + count * RESOURCE_FILE_IDENTIFIER_SIZE;
	legacy_u32 data_start = RESOURCE_FILE_DIRECTORY_OFFSET + count * entry_size;
	legacy_u32 data_length = declared_length - data_start;
	legacy_u32 text_start = 0;
	legacy_u16 found = 0;
	for (legacy_u32 index = 0; index < count; ++index) {
		legacy_u32 offset =
			LEGACY_READ_U32_LE(resource + offsets_start + index * RESOURCE_FILE_OFFSET_SIZE);
		if (offset > data_length) {
			return 0;
		}
		const legacy_u8 huge *identifier =
			resource + RESOURCE_FILE_DIRECTORY_OFFSET + index * RESOURCE_FILE_IDENTIFIER_SIZE;
		legacy_u16 character;
		for (character = 0; character < RESOURCE_FILE_IDENTIFIER_SIZE; ++character) {
			if (identifier[character] != (legacy_u8)name[character]) {
				break;
			}
		}
		if (character == RESOURCE_FILE_IDENTIFIER_SIZE) {
			if (found) {
				return 0;
			}
			found = 1;
			text_start = offset;
		}
	}
	if (!found) {
		return 0;
	}

	/* Directory order need not match payload order. Do not let a string run
	 * into the next resource, even when a later byte would terminate it. */
	legacy_u32 text_end = data_length;
	for (legacy_u32 index = 0; index < count; ++index) {
		legacy_u32 offset =
			LEGACY_READ_U32_LE(resource + offsets_start + index * RESOURCE_FILE_OFFSET_SIZE);
		if (offset > text_start && offset < text_end) {
			text_end = offset;
		}
	}
	const legacy_u8 huge *text = resource + data_start + text_start;
	for (legacy_u32 character = 0; character < text_end - text_start; ++character) {
		if (text[character] != (legacy_u8)expected[character]) {
			return 0;
		}
		if (!expected[character]) {
			return 1;
		}
	}
	return 0;
}

legacy_s8 far *locate_resource(legacy_s8 far *data, const legacy_s8 *name, legacy_u16 fatal)
{
	legacy_u16 chunk_count = resource_file_count((const legacy_u8 far *)data);
	/* Compare through a local padded key.  Several callers pass string
	 * literals, so the original in-place padding is not portable. */
	legacy_u16 padding = 0;
	legacy_s8 padded_name[RESOURCE_FILE_IDENTIFIER_SIZE];
	legacy_u16 character;
	for (character = 0; character < RESOURCE_FILE_IDENTIFIER_SIZE; character++) {
		if (padding == 0 && name[character] != 0) {
			padded_name[character] = name[character];
		} else {
			padding = 1;
			padded_name[character] = RESOURCE_IDENTIFIER_PADDING;
		}
	}

	/* The original runs this compare chunks+1 times.  The extra slot is the
	 * first offset dword, which is zero for normal resources and cannot match
	 * any space-padded name used by callers. */
	for (legacy_u16 index = 0; index < chunk_count; index++) {
		const legacy_u8 far *identifier =
			resource_file_identifier((const legacy_u8 far *)data, index);
		for (character = 0; character < RESOURCE_FILE_IDENTIFIER_SIZE; character++) {
			if (identifier[character] != (legacy_u8)padded_name[character]) {
				break;
			}
		}
		if (character == RESOURCE_FILE_IDENTIFIER_SIZE ||
			(identifier[character] == 0 && padded_name[character] == RESOURCE_IDENTIFIER_PADDING)) {
			return (legacy_s8 far *)resource_file_data((legacy_u8 far *)data, index);
		}
	}

	if (fatal == RESOURCE_NOT_FOUND_SHAPE_FATAL) {
		fatal_error(missing_shape_error_format, name);
	}
	return 0;
}

legacy_s8 far *locate_shape_nofatal(legacy_s8 far *data, const legacy_s8 *name)
{
	return locate_resource(data, name, RESOURCE_NOT_FOUND_IGNORE);
}

legacy_s8 far *locate_shape_fatal(legacy_s8 far *data, const legacy_s8 *name)
{
	return locate_resource(data, name, RESOURCE_NOT_FOUND_SHAPE_FATAL);
}

legacy_s8 far *locate_shape_alt(legacy_s8 far *data, const legacy_s8 *name)
{
	return locate_shape_fatal(data, name);
}
