#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/platform.h"

#define TEST_DIRECTORY_MODE 0700
#define TEST_RESOURCE_SIZE 16U
#define TEST_CAR_CAPACITY 32U
#define TEST_PACK_HEADER_SIZE 10U
#define TEST_ESCAPE_VALUE 224U
#define TEST_ESCAPE_SETTINGS 129U
#define TEST_LARGE_SIZE 131089U
#define TEST_LARGE_MARKER 106U
#define TEST_DEFLATE_MARKER 49U
#define TEST_CHUNK_SIZE 32768U
#define TEST_MAX_ENTRIES 8U
#define TEST_ZIP_LOCAL_SIGNATURE 0x04034b50UL
#define TEST_ZIP_CENTRAL_SIGNATURE 0x02014b50UL
#define TEST_ZIP_END_SIGNATURE 0x06054b50UL
#define TEST_ZIP_VERSION 20U
#define TEST_ZIP_STORED 0U
#define TEST_ZIP_DEFLATED 8U
#define TEST_ZIP_UNSUPPORTED_METHOD 99U
#define TEST_ZIP_ENCRYPTED 1U
#define TEST_ZIP_DESCRIPTOR_FLAG 8U
#define TEST_ZIP_DESCRIPTOR_SIGNATURE 0x08074b50UL
#define TEST_ZIP_END_COMMENT_LENGTH 20U
#define TEST_ZIP_DESCRIPTOR_SIGNED 2U
#define TEST_ZIP_LOCAL_SIZE 30U
#define TEST_ZIP_CENTRAL_SIZE 46U
#define TEST_ZIP_END_SIZE 22U
#define TEST_ZIP_LOCAL_FLAGS 6U
#define TEST_ZIP_LOCAL_METHOD 8U
#define TEST_ZIP_LOCAL_CRC 14U
#define TEST_ZIP_LOCAL_COMPRESSED_SIZE 18U
#define TEST_ZIP_LOCAL_EXPANDED_SIZE 22U
#define TEST_ZIP_CENTRAL_FLAGS 8U
#define TEST_ZIP_CENTRAL_METHOD 10U
#define TEST_ZIP_CENTRAL_CRC 16U
#define TEST_ZIP_CENTRAL_COMPRESSED_SIZE 20U
#define TEST_ZIP_CENTRAL_EXPANDED_SIZE 24U
#define TEST_ZIP_CENTRAL_LOCAL_OFFSET 42U
#define TEST_ZIP_END_DISK 4U
#define TEST_ZIP_END_DIRECTORY_SIZE 12U
#define TEST_ZIP_END_DIRECTORY_OFFSET 16U
#define TEST_ZIP_CRC_POLYNOMIAL 0xedb88320UL
#define TEST_ZIP_MEMBER_LIMIT (16UL * 1024UL * 1024UL)

struct test_zip_entry {
	const legacy_char *name;
	const legacy_u8 *data;
	legacy_u32 length;
	const legacy_u8 *compressed;
	legacy_u32 compressed_length;
	legacy_u16 method;
	legacy_u16 descriptor;
};

struct test_zip_layout {
	legacy_u32 central;
	legacy_u32 end;
};

static legacy_char original_directory[FILE_CAR_PATH_SIZE];
static legacy_char temporary_directory[] = "/tmp/restunts-car-zip-XXXXXX";
/* Python zlib.compressobj(9, zlib.DEFLATED, -15), repeated byte payloads. */
static const legacy_u8 fixed_deflate[] = {0x33, 0x34, 0x44, 0x05, 0x00};
static const legacy_u8 dynamic_deflate[] = {
	0xed, 0xc1, 0x81, 0x00, 0x00, 0x00, 0x00, 0xc3, 0x20, 0xdf, 0xf9, 0x03, 0x9c, 0xe0, 0x06,
	0x55, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0x03,
};
static const legacy_u8 empty_deflate[] = {0x03, 0x00};

void *_memcpy(void *destination, const void *source, legacy_u16 length)
{
	return memcpy(destination, source, length);
}

void fatal_error(const legacy_s8 *format, ...)
{
	fprintf(stderr, "Unexpected fatal error: %s\n", (const legacy_char *)format);
	abort();
}

void dos_process_exit(legacy_s16 status)
{
	exit(status);
}

static void write_u16(FILE *file, legacy_u16 value)
{
	assert(fputc(value & LEGACY_U8_MAX, file) != EOF);
	assert(fputc(value >> LEGACY_BYTE_BITS, file) != EOF);
}

static void write_u32(FILE *file, legacy_u32 value)
{
	write_u16(file, (legacy_u16)value);
	write_u16(file, (legacy_u16)(value >> LEGACY_WORD_BITS));
}

static legacy_u32 crc32_bytes(const legacy_u8 *bytes, legacy_u32 length)
{
	legacy_u32 crc = LEGACY_U32_MAX;
	for (legacy_u32 index = 0; index < length; index++) {
		crc ^= bytes[index];
		for (legacy_u16 bit = 0; bit < LEGACY_BYTE_BITS; bit++) {
			crc = (crc >> 1U) ^ ((crc & 1U) ? TEST_ZIP_CRC_POLYNOMIAL : 0U);
		}
	}
	return crc ^ LEGACY_U32_MAX;
}

static struct test_zip_layout write_zip(const legacy_char *path,
										const struct test_zip_entry *entries, legacy_u16 count)
{
	assert(count <= TEST_MAX_ENTRIES);
	legacy_u32 offsets[TEST_MAX_ENTRIES];
	FILE *file = fopen(path, "wb");
	assert(file != NULL);
	for (legacy_u16 index = 0; index < count; index++) {
		const struct test_zip_entry *entry = entries + index;
		legacy_u16 name_length = strlen(entry->name);
		legacy_u32 compressed_length =
			entry->compressed != NULL ? entry->compressed_length : entry->length;
		offsets[index] = ftell(file);
		write_u32(file, TEST_ZIP_LOCAL_SIGNATURE);
		write_u16(file, TEST_ZIP_VERSION);
		write_u16(file, entry->descriptor != 0 ? TEST_ZIP_DESCRIPTOR_FLAG : 0);
		write_u16(file, entry->method);
		write_u16(file, 0);
		write_u16(file, 0);
		write_u32(file, entry->descriptor != 0 ? 0 : crc32_bytes(entry->data, entry->length));
		write_u32(file, entry->descriptor != 0 ? 0 : compressed_length);
		write_u32(file, entry->descriptor != 0 ? 0 : entry->length);
		write_u16(file, name_length);
		write_u16(file, 0);
		assert(fwrite(entry->name, 1, name_length, file) == name_length);
		assert(fwrite(entry->compressed != NULL ? entry->compressed : entry->data, 1,
					  compressed_length, file) == compressed_length);
		if (entry->descriptor != 0) {
			if (entry->descriptor == TEST_ZIP_DESCRIPTOR_SIGNED) {
				write_u32(file, TEST_ZIP_DESCRIPTOR_SIGNATURE);
			}
			write_u32(file, crc32_bytes(entry->data, entry->length));
			write_u32(file, compressed_length);
			write_u32(file, entry->length);
		}
	}
	struct test_zip_layout layout;
	layout.central = ftell(file);
	for (legacy_u16 index = 0; index < count; index++) {
		const struct test_zip_entry *entry = entries + index;
		legacy_u16 name_length = strlen(entry->name);
		write_u32(file, TEST_ZIP_CENTRAL_SIGNATURE);
		write_u16(file, TEST_ZIP_VERSION);
		write_u16(file, TEST_ZIP_VERSION);
		write_u16(file, entry->descriptor != 0 ? TEST_ZIP_DESCRIPTOR_FLAG : 0);
		write_u16(file, entry->method);
		write_u16(file, 0);
		write_u16(file, 0);
		write_u32(file, crc32_bytes(entry->data, entry->length));
		write_u32(file, entry->compressed != NULL ? entry->compressed_length : entry->length);
		write_u32(file, entry->length);
		write_u16(file, name_length);
		write_u16(file, 0);
		write_u16(file, 0);
		write_u16(file, 0);
		write_u16(file, 0);
		write_u32(file, 0);
		write_u32(file, offsets[index]);
		assert(fwrite(entry->name, 1, name_length, file) == name_length);
	}
	layout.end = ftell(file);
	write_u32(file, TEST_ZIP_END_SIGNATURE);
	write_u16(file, 0);
	write_u16(file, 0);
	write_u16(file, count);
	write_u16(file, count);
	write_u32(file, layout.end - layout.central);
	write_u32(file, layout.central);
	write_u16(file, 0);
	assert(fclose(file) == 0);
	return layout;
}

static struct test_zip_layout write_single_zip(const legacy_char *path, const legacy_char *name,
											   legacy_u8 marker)
{
	legacy_u8 bytes[TEST_RESOURCE_SIZE];
	memset(bytes, marker, sizeof(bytes));
	struct test_zip_entry entry = {name, bytes, sizeof(bytes), NULL, 0, TEST_ZIP_STORED};
	return write_zip(path, &entry, 1);
}

static void write_loose(const legacy_char *path, legacy_u8 marker, legacy_u16 packed)
{
	legacy_u8 bytes[TEST_PACK_HEADER_SIZE + TEST_RESOURCE_SIZE] = {0};
	legacy_u16 offset = 0;
	if (packed) {
		bytes[0] = 1;
		bytes[1] = TEST_RESOURCE_SIZE;
		bytes[4] = TEST_RESOURCE_SIZE;
		bytes[8] = TEST_ESCAPE_SETTINGS;
		bytes[9] = TEST_ESCAPE_VALUE;
		offset = TEST_PACK_HEADER_SIZE;
	}
	memset(bytes + offset, marker, TEST_RESOURCE_SIZE);
	FILE *file = fopen(path, "wb");
	assert(file != NULL);
	assert(fwrite(bytes, 1, offset + TEST_RESOURCE_SIZE, file) == offset + TEST_RESOURCE_SIZE);
	assert(fclose(file) == 0);
}

static void assert_resource(const legacy_char *name, legacy_u8 marker)
{
	legacy_u8 bytes[TEST_RESOURCE_SIZE];
	assert(file_read_nofatal((const legacy_s8 *)name, bytes) == bytes);
	for (legacy_u16 index = 0; index < sizeof(bytes); index++) {
		assert(bytes[index] == marker);
	}
}

static void assert_loaded(const legacy_char *stem, legacy_u8 marker)
{
	legacy_u8 *resource = file_load_resfile((const legacy_s8 *)stem);
	assert(resource != NULL);
	for (legacy_u16 index = 0; index < TEST_RESOURCE_SIZE; index++) {
		assert(resource[index] == marker);
	}
	mmgr_release(resource);
}

static void assert_discovery(legacy_u16 expected)
{
	legacy_s8 ids[TEST_CAR_CAPACITY][FILE_CAR_ID_BUFFER_SIZE];
	legacy_u16 count = file_find_car_ids(ids, TEST_CAR_CAPACITY);
	assert(count == expected);
	for (legacy_u16 index = 0; index < count; index++) {
		for (legacy_u16 other = index + 1U; other < count; other++) {
			assert(strcmp((const legacy_char *)ids[index], (const legacy_char *)ids[other]) != 0);
		}
	}
	assert(file_find_car_ids(ids, 0) == 0);
	assert(file_find_car_ids(ids, 1) == 1);
}

static void create_complete_archives(void)
{
	legacy_u8 bytes[TEST_RESOURCE_SIZE];
	memset(bytes, TEST_DEFLATE_MARKER, sizeof(bytes));
	struct test_zip_entry entries[] = {
		{"CaRTeSt.ReS", bytes, sizeof(bytes), fixed_deflate, sizeof(fixed_deflate),
		 TEST_ZIP_DEFLATED},
		{"sTtEsT.3sH", bytes, sizeof(bytes), NULL, 0, TEST_ZIP_STORED},
		{"StDaTeSt.VsH", bytes, sizeof(bytes), NULL, 0, TEST_ZIP_STORED},
		{"StDbTeSt.VsH", bytes, sizeof(bytes), NULL, 0, TEST_ZIP_STORED},
		{"StTeSt.H3d", bytes, sizeof(bytes), NULL, 0, TEST_ZIP_STORED}};
	write_zip("Data/TeSt.ZiP", entries, sizeof(entries) / sizeof(entries[0]));
	legacy_u8 packed[TEST_PACK_HEADER_SIZE + TEST_RESOURCE_SIZE] = {0};
	packed[0] = 1;
	packed[1] = TEST_RESOURCE_SIZE;
	packed[4] = TEST_RESOURCE_SIZE;
	packed[8] = TEST_ESCAPE_SETTINGS;
	packed[9] = TEST_ESCAPE_VALUE;
	memset(packed + TEST_PACK_HEADER_SIZE, TEST_DEFLATE_MARKER, TEST_RESOURCE_SIZE);
	struct test_zip_entry packed_entries[] = {
		{"carpack.pre", packed, sizeof(packed), NULL, 0, TEST_ZIP_STORED},
		{"stpack.p3s", packed, sizeof(packed), NULL, 0, TEST_ZIP_STORED},
		{"stdapack.pvs", packed, sizeof(packed), NULL, 0, TEST_ZIP_STORED},
		{"stdbpack.pvs", packed, sizeof(packed), NULL, 0, TEST_ZIP_STORED}};
	write_zip("Data/CaRs/PaCk.ZiP", packed_entries,
			  sizeof(packed_entries) / sizeof(packed_entries[0]));
}

static void test_resources_and_precedence(void)
{
	assert_discovery(6);
	assert_resource("cartest.res", TEST_DEFLATE_MARKER);
	assert_loaded("cartest", TEST_DEFLATE_MARKER);
	assert_resource("sttest.3sh", TEST_DEFLATE_MARKER);
	assert_resource("stdatest.vsh", TEST_DEFLATE_MARKER);
	assert_resource("stdbtest.vsh", TEST_DEFLATE_MARKER);
	assert_resource("sttest.h3d", TEST_DEFLATE_MARKER);
	assert_resource("carexe1.res", 2);
	assert_resource("carsub1.res", 3);
	assert_resource("carexe2.res", 4);
	assert_resource("carbnd1.res", 5);
	assert_loaded("carpack", TEST_DEFLATE_MARKER);
	const legacy_char *const packed_names[] = {"stpack.p3s", "stdapack.pvs", "stdbpack.pvs"};
	for (legacy_u16 index = 0; index < sizeof(packed_names) / sizeof(packed_names[0]); index++) {
		legacy_u8 *resource = file_decomp_nofatal((const legacy_s8 *)packed_names[index]);
		assert(resource != NULL && resource[0] == TEST_DEFLATE_MARKER);
		mmgr_release(resource);
	}
	assert(file_find((const legacy_s8 *)"stdatest.vsh") != NULL);
	assert(file_find_next() == NULL);
	assert(file_find((const legacy_s8 *)"stdapack.pvs") != NULL);
	assert(file_find_next() == NULL);
	assert(file_find((const legacy_s8 *)"stdapack.vsh") == NULL);
	assert(file_open_resource((const legacy_s8 *)"carpack.res") == 0);
	assert(file_open_resource((const legacy_s8 *)"stpack.3sh") == 0);
	assert(file_open_resource((const legacy_s8 *)"missing/cartest.res") == 0);
	assert(file_open_resource((const legacy_s8 *)"misc.res") == 0);
	assert(access("CaRTeSt.ReS", F_OK) != 0);
	assert(access("CaRs/carpack.pre", F_OK) != 0);
	/* Loose variants in a root mask the same family from its archive. */
	write_loose("CARTEST.PRE", 6, 1);
	assert_loaded("cartest", 6);
	assert(file_open_resource((const legacy_s8 *)"cartest.res") == 0);
	assert_discovery(6);
	assert(remove("CARTEST.PRE") == 0);
	assert_loaded("cartest", TEST_DEFLATE_MARKER);
	write_loose("STTEST.3SH", 7, 0);
	assert_resource("sttest.3sh", 7);
	assert(file_open_resource((const legacy_s8 *)"sttest.h3d") == 0);
	write_loose("STTEST.H3D", 8, 0);
	assert_resource("sttest.h3d", 8);
	assert(remove("STTEST.H3D") == 0);
	assert(remove("STTEST.3SH") == 0);
	assert_resource("sttest.h3d", TEST_DEFLATE_MARKER);
	/* A root archive outranks loose resources in later roots. */
	write_loose("../App/CARTEST.RES", 9, 0);
	assert_resource("cartest.res", TEST_DEFLATE_MARKER);
	write_loose("../App/CARPACK.RES", 10, 0);
	assert_loaded("carpack", 10);
	assert(remove("../App/CARPACK.RES") == 0);
	assert(remove("../App/CARTEST.RES") == 0);
	assert(remove("TeSt.ZiP") == 0);
	assert_discovery(6);
	assert_resource("cartest.res", 11);
	assert(remove("../App/cars/TEST.zip") == 0);
	assert_discovery(5);
	assert(file_open_resource((const legacy_s8 *)"cartest.res") == 0);
}

static void test_large_members_and_handles(void)
{
	legacy_u8 *bytes = malloc(TEST_LARGE_SIZE);
	assert(bytes != NULL);
	memset(bytes, TEST_LARGE_MARKER, TEST_LARGE_SIZE);
	struct test_zip_entry entries[] = {
		{"carhuge.res", bytes, TEST_LARGE_SIZE, dynamic_deflate, sizeof(dynamic_deflate),
		 TEST_ZIP_DEFLATED},
		{"sthuge.h3d", bytes, TEST_LARGE_SIZE, NULL, 0, TEST_ZIP_STORED},
		{"stdahuge.vsh", bytes, 0, empty_deflate, sizeof(empty_deflate), TEST_ZIP_DEFLATED}};
	write_zip("HUGE.zip", entries, sizeof(entries) / sizeof(entries[0]));
	memset(bytes, 0, TEST_LARGE_SIZE);
	assert(file_read_nofatal((const legacy_s8 *)"carhuge.res", bytes) == bytes);
	for (legacy_u32 index = 0; index < TEST_LARGE_SIZE; index++) {
		assert(bytes[index] == TEST_LARGE_MARKER);
	}
	legacy_u16 first = file_open_resource((const legacy_s8 *)"carhuge.res");
	legacy_u16 second = file_open_resource((const legacy_s8 *)"sthuge.h3d");
	assert(first != 0 && second != 0 && first != second);
	assert(dos_file_tell(first) == 0 && dos_file_tell(second) == 0);
	assert(dos_file_read(first, bytes, TEST_CHUNK_SIZE) == TEST_CHUNK_SIZE);
	assert(dos_file_tell(first) == TEST_CHUNK_SIZE && dos_file_tell(second) == 0);
	assert(dos_file_seek(first, -1, DOS_FILE_SEEK_END) == 0);
	assert(dos_file_tell(first) == TEST_LARGE_SIZE - 1U);
	assert(dos_file_read(first, bytes, TEST_CHUNK_SIZE) == 1);
	assert(bytes[0] == TEST_LARGE_MARKER);
	assert(dos_file_read(first, bytes, TEST_CHUNK_SIZE) == 0);
	assert(dos_file_error() == 0);
	assert(dos_file_seek(second, TEST_LARGE_SIZE, DOS_FILE_SEEK_BEGIN) == 0);
	assert(dos_file_read(second, bytes, 1) == 0);
	assert(dos_file_seek(second, 1, DOS_FILE_SEEK_CURRENT) == 0);
	assert(dos_file_read(second, bytes, 1) == 0);
	assert(dos_file_seek(second, -1, DOS_FILE_SEEK_BEGIN) != 0);
	assert(dos_file_error() != 0);
	assert(dos_file_write(first, bytes, 1) == 0);
	assert(dos_file_error() != 0);
	assert(dos_file_close(first) == 0);
	assert(dos_file_close(second) == 0);
	assert(dos_file_read(first, bytes, 1) == 0);
	assert(dos_file_error() != 0);
	legacy_u16 empty = file_open_resource((const legacy_s8 *)"stdahuge.vsh");
	assert(empty != 0);
	assert(dos_file_tell(empty) == 0);
	assert(dos_file_read(empty, bytes, 1) == 0);
	assert(dos_file_close(empty) == 0);
	assert(dos_file_open((const legacy_s8 *)"HUGE.zip/carhuge.res", DOS_FILE_CREATE) == 0);
	assert(dos_file_error() != 0);
	assert(remove("HUGE.zip") == 0);
	assert(file_open_resource((const legacy_s8 *)"carhuge.res") == 0);
	free(bytes);
}

static void test_descriptor_members(void)
{
	legacy_u8 bytes[TEST_RESOURCE_SIZE];
	memset(bytes, TEST_DEFLATE_MARKER, sizeof(bytes));
	struct test_zip_entry entries[] = {{"cardesc.res", bytes, sizeof(bytes), fixed_deflate,
										sizeof(fixed_deflate), TEST_ZIP_DEFLATED, 1},
									   {"stdadesc.vsh", bytes, sizeof(bytes), fixed_deflate,
										sizeof(fixed_deflate), TEST_ZIP_DEFLATED,
										TEST_ZIP_DESCRIPTOR_SIGNED}};
	struct test_zip_layout layout =
		write_zip("DESC.zip", entries, sizeof(entries) / sizeof(entries[0]));
	assert_resource("cardesc.res", TEST_DEFLATE_MARKER);
	assert_resource("stdadesc.vsh", TEST_DEFLATE_MARKER);
	/* ZIP comments can contain apparent signatures; the actual end offset wins. */
	static const legacy_u8 comment[] = {'P', 'K', 5, 6, ' ', 'c', 'a', 'r', 's'};
	FILE *file = fopen("DESC.zip", "r+b");
	assert(file != NULL);
	assert(fseek(file, layout.end + TEST_ZIP_END_COMMENT_LENGTH, SEEK_SET) == 0);
	write_u16(file, sizeof(comment));
	assert(fseek(file, 0, SEEK_END) == 0);
	assert(fwrite(comment, 1, sizeof(comment), file) == sizeof(comment));
	assert(fclose(file) == 0);
	assert_resource("cardesc.res", TEST_DEFLATE_MARKER);
	/* Descriptor integrity is required even when central metadata is intact. */
	file = fopen("DESC.zip", "r+b");
	assert(file != NULL);
	assert(fseek(file, TEST_ZIP_LOCAL_SIZE + strlen(entries[0].name) + sizeof(fixed_deflate),
				 SEEK_SET) == 0);
	write_u32(file, 0);
	assert(fclose(file) == 0);
	assert(file_open_resource((const legacy_s8 *)"cardesc.res") == 0);
	assert(remove("DESC.zip") == 0);
}

static void patch_u16(legacy_u32 offset, legacy_u16 value)
{
	FILE *file = fopen("BAD1.zip", "r+b");
	assert(file != NULL && fseek(file, offset, SEEK_SET) == 0);
	write_u16(file, value);
	assert(fclose(file) == 0);
}

static void patch_u32(legacy_u32 offset, legacy_u32 value)
{
	FILE *file = fopen("BAD1.zip", "r+b");
	assert(file != NULL && fseek(file, offset, SEEK_SET) == 0);
	write_u32(file, value);
	assert(fclose(file) == 0);
}

static struct test_zip_layout reset_bad_zip(void)
{
	return write_single_zip("BAD1.zip", "carbad1.res", TEST_DEFLATE_MARKER);
}

static void assert_bad_zip(void)
{
	assert(dos_file_open((const legacy_s8 *)"BAD1.zip/carbad1.res", DOS_FILE_OPEN_EXISTING) == 0);
	assert(dos_file_error() != 0);
	assert_discovery(5);
}

static void test_invalid_archives(void)
{
	struct test_zip_layout layout = reset_bad_zip();
	patch_u16(TEST_ZIP_LOCAL_FLAGS, TEST_ZIP_ENCRYPTED);
	patch_u16(layout.central + TEST_ZIP_CENTRAL_FLAGS, TEST_ZIP_ENCRYPTED);
	assert_bad_zip();
	layout = reset_bad_zip();
	patch_u16(TEST_ZIP_LOCAL_METHOD, TEST_ZIP_UNSUPPORTED_METHOD);
	patch_u16(layout.central + TEST_ZIP_CENTRAL_METHOD, TEST_ZIP_UNSUPPORTED_METHOD);
	assert_bad_zip();
	layout = reset_bad_zip();
	patch_u32(TEST_ZIP_LOCAL_CRC, 0);
	patch_u32(layout.central + TEST_ZIP_CENTRAL_CRC, 0);
	assert_bad_zip();
	layout = reset_bad_zip();
	patch_u32(TEST_ZIP_LOCAL_EXPANDED_SIZE, TEST_ZIP_MEMBER_LIMIT + 1U);
	patch_u32(layout.central + TEST_ZIP_CENTRAL_EXPANDED_SIZE, TEST_ZIP_MEMBER_LIMIT + 1U);
	assert_bad_zip();
	layout = reset_bad_zip();
	patch_u32(TEST_ZIP_LOCAL_COMPRESSED_SIZE, TEST_ZIP_MEMBER_LIMIT + 1U);
	patch_u32(layout.central + TEST_ZIP_CENTRAL_COMPRESSED_SIZE, TEST_ZIP_MEMBER_LIMIT + 1U);
	assert_bad_zip();
	layout = reset_bad_zip();
	patch_u16(TEST_ZIP_LOCAL_METHOD, TEST_ZIP_DEFLATED);
	assert_bad_zip();
	layout = reset_bad_zip();
	patch_u32(layout.central + TEST_ZIP_CENTRAL_LOCAL_OFFSET, layout.central);
	assert_bad_zip();
	layout = reset_bad_zip();
	patch_u16(layout.end + TEST_ZIP_END_DISK, 1);
	assert_bad_zip();
	layout = reset_bad_zip();
	patch_u32(layout.end + TEST_ZIP_END_DIRECTORY_SIZE, LEGACY_U32_MAX);
	assert_bad_zip();
	layout = reset_bad_zip();
	patch_u32(layout.end + TEST_ZIP_END_DIRECTORY_OFFSET, LEGACY_U32_MAX);
	assert_bad_zip();
	layout = reset_bad_zip();
	assert(truncate("BAD1.zip", layout.end + TEST_ZIP_END_SIZE - 1U) == 0);
	assert_bad_zip();
	layout = reset_bad_zip();
	patch_u32(TEST_ZIP_LOCAL_SIZE + strlen("carbad1.res"), 0);
	assert_bad_zip();
	/* Both headers agree about a nested name: traversal is still never exposed. */
	write_single_zip("BAD1.zip", "../carbad1.res", TEST_DEFLATE_MARKER);
	assert_bad_zip();
	assert(dos_file_open((const legacy_s8 *)"BAD1.zip/../carbad1.res", DOS_FILE_OPEN_EXISTING) ==
		   0);
	assert(dos_file_error() != 0);
	legacy_u8 bytes[TEST_RESOURCE_SIZE];
	memset(bytes, TEST_DEFLATE_MARKER, sizeof(bytes));
	struct test_zip_entry duplicate_entries[] = {
		{"carbad1.res", bytes, sizeof(bytes), NULL, 0, TEST_ZIP_STORED},
		{"CARBAD1.RES", bytes, sizeof(bytes), NULL, 0, TEST_ZIP_STORED}};
	write_zip("BAD1.zip", duplicate_entries,
			  sizeof(duplicate_entries) / sizeof(duplicate_entries[0]));
	assert_bad_zip();
	static const legacy_u8 invalid_deflate[] = {7};
	struct test_zip_entry invalid_entry = {"carbad1.res",			bytes,
										   sizeof(bytes),			invalid_deflate,
										   sizeof(invalid_deflate), TEST_ZIP_DEFLATED};
	write_zip("BAD1.zip", &invalid_entry, 1);
	assert_bad_zip();
	assert(remove("BAD1.zip") == 0);
}

legacy_int main(void)
{
	assert(getcwd(original_directory, sizeof(original_directory)) != NULL);
	assert(mkdtemp(temporary_directory) != NULL);
	assert(chdir(temporary_directory) == 0);
	const legacy_char *const directories[] = {"Data", "App", "Data/CaRs", "App/cars", "Bundled"};
	for (legacy_u16 index = 0; index < sizeof(directories) / sizeof(directories[0]); index++) {
		assert(mkdir(directories[index], TEST_DIRECTORY_MODE) == 0);
	}
	create_complete_archives();
	write_single_zip("App/EXE1.zip", "carexe1.res", 2);
	write_single_zip("Data/CaRs/SuB1.zip", "carsub1.res", 3);
	write_single_zip("App/cars/EXE2.zip", "carexe2.res", 4);
	write_single_zip("Bundled/BND1.zip", "carbnd1.res", 5);
	write_single_zip("App/cars/TEST.zip", "cartest.res", 11);
	write_single_zip("Data/TOOLONG.zip", "cartool.res", 12);
	write_single_zip("Data/SHO.zip", "carshor.res", 13);
	write_single_zip("Data/MISS.zip", "carnope.res", 14);
	write_single_zip("Data/CaRs/NEST.zip", "folder/carnest.res", 15);
	assert(chdir("App") == 0);
	legacy_char executable_directory[FILE_CAR_PATH_SIZE];
	assert(getcwd(executable_directory, sizeof(executable_directory)) != NULL);
	file_car_set_base((const legacy_s8 *)executable_directory);
	assert(chdir("../Data") == 0);
	mmgr_init_conventional_arena();
	test_resources_and_precedence();
	test_large_members_and_handles();
	test_descriptor_members();
	test_invalid_archives();
	const legacy_char *const files[] = {"../App/EXE1.zip",
										"CaRs/SuB1.zip",
										"../App/cars/EXE2.zip",
										"../Bundled/BND1.zip",
										"CaRs/PaCk.ZiP",
										"TOOLONG.zip",
										"SHO.zip",
										"MISS.zip",
										"CaRs/NEST.zip"};
	for (legacy_u16 index = 0; index < sizeof(files) / sizeof(files[0]); index++) {
		assert(remove(files[index]) == 0);
	}
	assert(rmdir("CaRs") == 0);
	assert(rmdir("../App/cars") == 0);
	assert(chdir("..") == 0);
	assert(rmdir("Data") == 0);
	assert(rmdir("App") == 0);
	assert(rmdir("Bundled") == 0);
	file_car_set_base(NULL);
	assert(chdir(original_directory) == 0);
	assert(rmdir(temporary_directory) == 0);
	puts("ZIP car discovery, resource precedence, decompression and bounded handle I/O passed");
	return 0;
}
