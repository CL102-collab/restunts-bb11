#include <stdlib.h>
#include <string.h>
#include "car_zip.h"
#include "../../../../third_party/puff/puff.h"

/* Preserve puff's unsigned-long pointer ABI on both LP64 and LLP64 hosts. */
typedef unsigned long legacy_puff_size;

#define CAR_ZIP_MEMBER_LIMIT (16UL * 1024UL * 1024UL)
#define CAR_ZIP_NAME_LIMIT 12U
#define CAR_ZIP_COMMENT_LIMIT 65535UL
#define CAR_ZIP_END_SIZE 22U
#define CAR_ZIP_CENTRAL_SIZE 46U
#define CAR_ZIP_LOCAL_SIZE 30U
#define CAR_ZIP_EXTRA_HEADER_SIZE 4U
#define CAR_ZIP_DESCRIPTOR_SIZE 12U
#define CAR_ZIP_SIGNATURE_SIZE 4U
#define CAR_ZIP_END_SIGNATURE 0x06054b50UL
#define CAR_ZIP_CENTRAL_SIGNATURE 0x02014b50UL
#define CAR_ZIP_LOCAL_SIGNATURE 0x04034b50UL
#define CAR_ZIP_DESCRIPTOR_SIGNATURE 0x08074b50UL
#define CAR_ZIP_CRC_POLYNOMIAL 0xedb88320UL
#define CAR_ZIP_ALLOWED_FLAGS 0x080eU
#define CAR_ZIP_DEFLATE_OPTIONS 0x0006U
#define CAR_ZIP_DESCRIPTOR_FLAG 0x0008U
#define CAR_ZIP_STORED 0U
#define CAR_ZIP_DEFLATED 8U
#define CAR_ZIP_VERSION_LIMIT 20U
#define CAR_ZIP_STORED_VERSION 10U
#define CAR_ZIP_DEFLATED_VERSION 20U
#define CAR_ZIP_EXTRA_ZIP64 1U

enum CAR_ZIP_END_OFFSET {
	CAR_ZIP_END_DISK = 4,
	CAR_ZIP_END_DIRECTORY_DISK = 6,
	CAR_ZIP_END_DISK_ENTRIES = 8,
	CAR_ZIP_END_ENTRIES = 10,
	CAR_ZIP_END_DIRECTORY_SIZE = 12,
	CAR_ZIP_END_DIRECTORY_OFFSET = 16,
	CAR_ZIP_END_COMMENT_SIZE = 20
};

enum CAR_ZIP_CENTRAL_OFFSET {
	CAR_ZIP_CENTRAL_VERSION = 6,
	CAR_ZIP_CENTRAL_FLAGS = 8,
	CAR_ZIP_CENTRAL_METHOD = 10,
	CAR_ZIP_CENTRAL_CRC = 16,
	CAR_ZIP_CENTRAL_COMPRESSED = 20,
	CAR_ZIP_CENTRAL_EXPANDED = 24,
	CAR_ZIP_CENTRAL_NAME_SIZE = 28,
	CAR_ZIP_CENTRAL_EXTRA_SIZE = 30,
	CAR_ZIP_CENTRAL_COMMENT_SIZE = 32,
	CAR_ZIP_CENTRAL_DISK = 34,
	CAR_ZIP_CENTRAL_LOCAL_OFFSET = 42
};

enum CAR_ZIP_LOCAL_OFFSET {
	CAR_ZIP_LOCAL_VERSION = 4,
	CAR_ZIP_LOCAL_FLAGS = 6,
	CAR_ZIP_LOCAL_METHOD = 8,
	CAR_ZIP_LOCAL_CRC = 14,
	CAR_ZIP_LOCAL_COMPRESSED = 18,
	CAR_ZIP_LOCAL_EXPANDED = 22,
	CAR_ZIP_LOCAL_NAME_SIZE = 26,
	CAR_ZIP_LOCAL_EXTRA_SIZE = 28
};

struct car_zip_member {
	legacy_u16 version;
	legacy_u16 flags;
	legacy_u16 method;
	legacy_u32 crc;
	legacy_u32 compressed;
	legacy_u32 expanded;
	legacy_u32 local_offset;
	legacy_u16 name_length;
	legacy_u8 name[CAR_ZIP_NAME_LIMIT];
};

static legacy_u16 car_zip_u16(const legacy_u8 *bytes)
{
	return (legacy_u16)(bytes[0] | ((legacy_u16)bytes[1] << LEGACY_BYTE_BITS));
}

static legacy_u32 car_zip_u32(const legacy_u8 *bytes)
{
	return (legacy_u32)car_zip_u16(bytes) |
		   ((legacy_u32)car_zip_u16(bytes + LEGACY_WORD_BYTES) << LEGACY_WORD_BITS);
}

static legacy_u16 car_zip_read(FILE *archive, legacy_u32 offset, void *buffer, legacy_u32 count)
{
	return fseek(archive, offset, SEEK_SET) == 0 && fread(buffer, 1, count, archive) == count;
}

static legacy_u8 car_zip_upper(legacy_u8 value)
{
	return value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value;
}

static legacy_u16 car_zip_name_length(const legacy_s8 *name)
{
	if (name == NULL) {
		return 0;
	}
	legacy_u16 length = 0;
	while (name[length] != 0 && length < CAR_ZIP_NAME_LIMIT) {
		legacy_u8 value = (legacy_u8)name[length];
		if (value <= ' ' || value >= LEGACY_S8_MAX || strchr("/\\:\"<>|?*", value) != NULL) {
			return 0;
		}
		length++;
	}
	if (name[length] != 0 || length == 0 || name[0] == '.' || name[length - 1U] == '.') {
		return 0;
	}
	return length;
}

static legacy_u16 car_zip_name_equal(const legacy_u8 *left, const legacy_s8 *right,
									 legacy_u16 length)
{
	for (legacy_u16 index = 0; index < length; index++) {
		if (car_zip_upper(left[index]) != car_zip_upper((legacy_u8)right[index])) {
			return 0;
		}
	}
	return 1;
}

/* Extra fields are length-prefixed; reject ZIP64 even when no sentinel was used. */
static legacy_u16 car_zip_extras(FILE *archive, legacy_u32 offset, legacy_u16 length)
{
	while (length != 0) {
		legacy_u8 header[CAR_ZIP_EXTRA_HEADER_SIZE];
		if (length < sizeof(header) || !car_zip_read(archive, offset, header, sizeof(header))) {
			return 0;
		}
		legacy_u16 payload = car_zip_u16(header + LEGACY_WORD_BYTES);
		length -= sizeof(header);
		if (car_zip_u16(header) == CAR_ZIP_EXTRA_ZIP64 || payload > length) {
			return 0;
		}
		offset += sizeof(header) + payload;
		length -= payload;
	}
	return 1;
}

static legacy_u16 car_zip_directory(FILE *archive, legacy_u32 file_length, legacy_u32 *offset,
									legacy_u32 *size, legacy_u16 *entries)
{
	legacy_u32 tail_length = CAR_ZIP_END_SIZE + CAR_ZIP_COMMENT_LIMIT;
	if (tail_length > file_length) {
		tail_length = file_length;
	}
	if (tail_length < CAR_ZIP_END_SIZE) {
		return 0;
	}
	legacy_u8 *tail = malloc(tail_length);
	if (tail == NULL) {
		return 0;
	}
	legacy_u16 valid = 0;
	if (car_zip_read(archive, file_length - tail_length, tail, tail_length)) {
		legacy_u32 index = tail_length - CAR_ZIP_END_SIZE;
		do {
			const legacy_u8 *end = tail + index;
			if (car_zip_u32(end) == CAR_ZIP_END_SIGNATURE &&
				car_zip_u16(end + CAR_ZIP_END_COMMENT_SIZE) ==
					tail_length - index - CAR_ZIP_END_SIZE) {
				*entries = car_zip_u16(end + CAR_ZIP_END_ENTRIES);
				*offset = car_zip_u32(end + CAR_ZIP_END_DIRECTORY_OFFSET);
				*size = car_zip_u32(end + CAR_ZIP_END_DIRECTORY_SIZE);
				legacy_u32 end_offset = file_length - tail_length + index;
				valid = car_zip_u16(end + CAR_ZIP_END_DISK) == 0 &&
						car_zip_u16(end + CAR_ZIP_END_DIRECTORY_DISK) == 0 &&
						car_zip_u16(end + CAR_ZIP_END_DISK_ENTRIES) == *entries &&
						*entries != LEGACY_U16_MAX && *offset <= end_offset &&
						*size == end_offset - *offset && *entries <= *size / CAR_ZIP_CENTRAL_SIZE;
				break;
			}
		} while (index-- != 0);
	}
	free(tail);
	return valid;
}

static legacy_u16 car_zip_find(FILE *archive, const legacy_s8 *name, legacy_u16 name_length,
							   legacy_u32 offset, legacy_u32 size, legacy_u16 entries,
							   struct car_zip_member *member)
{
	legacy_u32 end = offset + size;
	legacy_u16 found = 0;
	for (legacy_u16 index = 0; index < entries; index++) {
		legacy_u8 header[CAR_ZIP_CENTRAL_SIZE];
		if (offset > end || end - offset < sizeof(header) ||
			!car_zip_read(archive, offset, header, sizeof(header)) ||
			car_zip_u32(header) != CAR_ZIP_CENTRAL_SIGNATURE) {
			return 0;
		}
		legacy_u16 version = car_zip_u16(header + CAR_ZIP_CENTRAL_VERSION);
		legacy_u16 flags = car_zip_u16(header + CAR_ZIP_CENTRAL_FLAGS);
		legacy_u16 method = car_zip_u16(header + CAR_ZIP_CENTRAL_METHOD);
		legacy_u16 current_length = car_zip_u16(header + CAR_ZIP_CENTRAL_NAME_SIZE);
		legacy_u16 extra_length = car_zip_u16(header + CAR_ZIP_CENTRAL_EXTRA_SIZE);
		legacy_u16 comment_length = car_zip_u16(header + CAR_ZIP_CENTRAL_COMMENT_SIZE);
		legacy_u32 variable_length = (legacy_u32)current_length + extra_length + comment_length;
		offset += sizeof(header);
		if (version > CAR_ZIP_VERSION_LIMIT || version < CAR_ZIP_STORED_VERSION ||
			(method == CAR_ZIP_DEFLATED && version < CAR_ZIP_DEFLATED_VERSION) ||
			(flags & ~CAR_ZIP_ALLOWED_FLAGS) != 0 ||
			(method != CAR_ZIP_STORED && method != CAR_ZIP_DEFLATED) ||
			(method == CAR_ZIP_STORED && (flags & CAR_ZIP_DEFLATE_OPTIONS) != 0) ||
			car_zip_u16(header + CAR_ZIP_CENTRAL_DISK) != 0 || variable_length > end - offset ||
			car_zip_u32(header + CAR_ZIP_CENTRAL_COMPRESSED) == LEGACY_U32_MAX ||
			car_zip_u32(header + CAR_ZIP_CENTRAL_EXPANDED) == LEGACY_U32_MAX ||
			car_zip_u32(header + CAR_ZIP_CENTRAL_LOCAL_OFFSET) == LEGACY_U32_MAX ||
			!car_zip_extras(archive, offset + current_length, extra_length)) {
			return 0;
		}
		if (current_length == name_length) {
			legacy_u8 current[CAR_ZIP_NAME_LIMIT];
			if (!car_zip_read(archive, offset, current, current_length)) {
				return 0;
			}
			if (car_zip_name_equal(current, name, name_length)) {
				if (found) {
					return 0;
				}
				found = 1;
				member->version = version;
				member->flags = flags;
				member->method = method;
				member->crc = car_zip_u32(header + CAR_ZIP_CENTRAL_CRC);
				member->compressed = car_zip_u32(header + CAR_ZIP_CENTRAL_COMPRESSED);
				member->expanded = car_zip_u32(header + CAR_ZIP_CENTRAL_EXPANDED);
				member->local_offset = car_zip_u32(header + CAR_ZIP_CENTRAL_LOCAL_OFFSET);
				member->name_length = name_length;
				memcpy(member->name, current, name_length);
			}
		}
		offset += variable_length;
	}
	return found && offset == end;
}

static legacy_u16 car_zip_descriptor(const legacy_u8 *descriptor,
									 const struct car_zip_member *member)
{
	return car_zip_u32(descriptor) == member->crc &&
		   car_zip_u32(descriptor + LEGACY_DWORD_BYTES) == member->compressed &&
		   car_zip_u32(descriptor + LEGACY_DWORD_BYTES * 2U) == member->expanded;
}

static legacy_u16 car_zip_local(FILE *archive, const struct car_zip_member *member,
								legacy_u32 directory_offset, legacy_u32 *data_offset)
{
	legacy_u8 header[CAR_ZIP_LOCAL_SIZE];
	if (member->local_offset > directory_offset ||
		directory_offset - member->local_offset < sizeof(header) ||
		!car_zip_read(archive, member->local_offset, header, sizeof(header)) ||
		car_zip_u32(header) != CAR_ZIP_LOCAL_SIGNATURE ||
		car_zip_u16(header + CAR_ZIP_LOCAL_VERSION) != member->version ||
		car_zip_u16(header + CAR_ZIP_LOCAL_FLAGS) != member->flags ||
		car_zip_u16(header + CAR_ZIP_LOCAL_METHOD) != member->method ||
		car_zip_u16(header + CAR_ZIP_LOCAL_NAME_SIZE) != member->name_length) {
		return 0;
	}
	legacy_u32 crc = car_zip_u32(header + CAR_ZIP_LOCAL_CRC);
	legacy_u32 compressed = car_zip_u32(header + CAR_ZIP_LOCAL_COMPRESSED);
	legacy_u32 expanded = car_zip_u32(header + CAR_ZIP_LOCAL_EXPANDED);
	if ((crc != member->crc && (!(member->flags & CAR_ZIP_DESCRIPTOR_FLAG) || crc != 0)) ||
		(compressed != member->compressed &&
		 (!(member->flags & CAR_ZIP_DESCRIPTOR_FLAG) || compressed != 0)) ||
		(expanded != member->expanded &&
		 (!(member->flags & CAR_ZIP_DESCRIPTOR_FLAG) || expanded != 0))) {
		return 0;
	}
	legacy_u16 extra_length = car_zip_u16(header + CAR_ZIP_LOCAL_EXTRA_SIZE);
	legacy_u32 name_offset = member->local_offset + sizeof(header);
	legacy_u32 variable_length = (legacy_u32)member->name_length + extra_length;
	if (variable_length > directory_offset - name_offset) {
		return 0;
	}
	legacy_u8 name[CAR_ZIP_NAME_LIMIT];
	if (!car_zip_read(archive, name_offset, name, member->name_length) ||
		memcmp(name, member->name, member->name_length) != 0 ||
		!car_zip_extras(archive, name_offset + member->name_length, extra_length)) {
		return 0;
	}
	*data_offset = name_offset + variable_length;
	if (member->compressed > directory_offset - *data_offset) {
		return 0;
	}
	if (member->flags & CAR_ZIP_DESCRIPTOR_FLAG) {
		legacy_u8 descriptor[CAR_ZIP_DESCRIPTOR_SIZE];
		legacy_u32 descriptor_offset = *data_offset + member->compressed;
		if (directory_offset - descriptor_offset < sizeof(descriptor) ||
			!car_zip_read(archive, descriptor_offset, descriptor, sizeof(descriptor))) {
			return 0;
		}
		/* A signature-less descriptor may itself have the signature as its CRC. */
		if (!car_zip_descriptor(descriptor, member) &&
			car_zip_u32(descriptor) == CAR_ZIP_DESCRIPTOR_SIGNATURE) {
			descriptor_offset += CAR_ZIP_SIGNATURE_SIZE;
			if (directory_offset - descriptor_offset < sizeof(descriptor) ||
				!car_zip_read(archive, descriptor_offset, descriptor, sizeof(descriptor))) {
				return 0;
			}
		}
		if (!car_zip_descriptor(descriptor, member)) {
			return 0;
		}
	}
	return 1;
}

static legacy_u32 car_zip_crc(const legacy_u8 *bytes, legacy_u32 length)
{
	legacy_u32 crc = LEGACY_U32_MAX;
	for (legacy_u32 index = 0; index < length; index++) {
		crc ^= bytes[index];
		for (legacy_u16 bit = 0; bit < LEGACY_BYTE_BITS; bit++) {
			crc = (crc >> 1U) ^ ((crc & 1U) ? CAR_ZIP_CRC_POLYNOMIAL : 0U);
		}
	}
	return crc ^ LEGACY_U32_MAX;
}

legacy_u8 *sdl3_car_zip_read(FILE *archive, const legacy_s8 *name, legacy_u32 *length)
{
	if (length == NULL) {
		return NULL;
	}
	*length = 0;
	legacy_u16 name_length = car_zip_name_length(name);
	if (archive == NULL || name_length == 0 || fseek(archive, 0, SEEK_END) != 0) {
		return NULL;
	}
	legacy_s64 file_length = ftell(archive);
	legacy_u32 directory_offset;
	legacy_u32 directory_size;
	legacy_u16 entries;
	struct car_zip_member member;
	legacy_u32 data_offset;
	if (file_length < 0 || file_length > (legacy_s64)LEGACY_S32_MAX ||
		!car_zip_directory(archive, file_length, &directory_offset, &directory_size, &entries) ||
		!car_zip_find(archive, name, name_length, directory_offset, directory_size, entries,
					  &member) ||
		member.compressed > CAR_ZIP_MEMBER_LIMIT || member.expanded > CAR_ZIP_MEMBER_LIMIT ||
		(member.method == CAR_ZIP_STORED && member.compressed != member.expanded) ||
		!car_zip_local(archive, &member, directory_offset, &data_offset)) {
		return NULL;
	}
	legacy_u8 *output = malloc(member.expanded != 0 ? member.expanded : 1U);
	if (output == NULL) {
		return NULL;
	}
	legacy_u16 valid;
	if (member.method == CAR_ZIP_STORED) {
		valid = car_zip_read(archive, data_offset, output, member.expanded);
	} else {
		legacy_u8 *compressed = malloc(member.compressed != 0 ? member.compressed : 1U);
		if (compressed == NULL) {
			free(output);
			return NULL;
		}
		legacy_puff_size input_length = member.compressed;
		legacy_puff_size output_length = member.expanded;
		valid = car_zip_read(archive, data_offset, compressed, member.compressed) &&
				puff(output, &output_length, compressed, &input_length) == 0 &&
				input_length == member.compressed && output_length == member.expanded;
		free(compressed);
	}
	if (!valid || car_zip_crc(output, member.expanded) != member.crc) {
		free(output);
		return NULL;
	}
	*length = member.expanded;
	return output;
}
