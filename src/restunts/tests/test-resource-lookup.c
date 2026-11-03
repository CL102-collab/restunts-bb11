#include <assert.h>
#include <stdarg.h>
#include <string.h>

#include "../c/memmgr.h"
#include "../c/resource.h"
#include "../c/fatal.h"

#define TEST_RESOURCE_COUNT 1U
#define TEST_RESOURCE_PAYLOAD_SIZE 4U
#define TEST_RESOURCE_DATA_OFFSET                                                                  \
	(RESOURCE_FILE_DIRECTORY_OFFSET + RESOURCE_FILE_IDENTIFIER_SIZE + RESOURCE_FILE_OFFSET_SIZE)
#define TEST_RESOURCE_SIZE (TEST_RESOURCE_DATA_OFFSET + TEST_RESOURCE_PAYLOAD_SIZE)
#define TEST_NAME_TERMINATOR_OFFSET 3U
#define TEST_RESOURCE_MARKER_BYTE_0 18U
#define TEST_RESOURCE_MARKER_BYTE_1 52U
#define TEST_RESOURCE_MARKER_BYTE_2 86U
#define TEST_RESOURCE_MARKER_BYTE_3 120U

static const legacy_s8 test_text_version[] = "Version 1.1 (Feb 12 1991)";
static const legacy_s8 test_text_other_version[] = "Version 1.1 (Feb 25 1991)";
static const legacy_s8 test_text_identifier[] = "gver";
static const legacy_s8 test_text_missing_identifier[] = "none";
static const legacy_s8 test_text_short_identifier[] = "gve";
static const legacy_s8 test_text_long_identifier[] = "gverx";
static const legacy_s8 test_text_prefix[] = "Version";
static const legacy_s8 test_text_empty[] = "";
#define TEST_TEXT_TITLE "Stunts"
#define TEST_TEXT_COUNT 3U
#define TEST_TEXT_VERSION_INDEX 0U
#define TEST_TEXT_TITLE_INDEX 1U
#define TEST_TEXT_UNRELATED_INDEX 2U
#define TEST_TEXT_DATA_OFFSET                                                                      \
	(RESOURCE_FILE_DIRECTORY_OFFSET +                                                              \
	 TEST_TEXT_COUNT * (RESOURCE_FILE_IDENTIFIER_SIZE + RESOURCE_FILE_OFFSET_SIZE))
#define TEST_TEXT_TITLE_OFFSET sizeof(test_text_version)
#define TEST_TEXT_UNRELATED_OFFSET (TEST_TEXT_TITLE_OFFSET + sizeof(TEST_TEXT_TITLE))
#define TEST_TEXT_SIZE                                                                             \
	(TEST_TEXT_DATA_OFFSET + TEST_TEXT_UNRELATED_OFFSET + TEST_RESOURCE_PAYLOAD_SIZE)
#define TEST_TEXT_MODIFIED_BYTE 165U
#define TEST_TEXT_MAX_COUNT 65535U
#define TEST_TEXT_INVALID_OFFSET 0xffffffffUL
#define TEST_TEXT_LARGE_COUNT 8192U
#define TEST_TEXT_LARGE_DATA_OFFSET                                                                \
	(RESOURCE_FILE_DIRECTORY_OFFSET +                                                              \
	 TEST_TEXT_LARGE_COUNT * (RESOURCE_FILE_IDENTIFIER_SIZE + RESOURCE_FILE_OFFSET_SIZE))
#define TEST_TEXT_LARGE_SIZE (TEST_TEXT_LARGE_DATA_OFFSET + sizeof(test_text_version))

const legacy_s8 missing_shape_error_format[] = "shape";

void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	assert(0);
}

static void make_text_resource(legacy_u8 *resource)
{
	memset(resource, 0, TEST_TEXT_SIZE);
	resource_file_set_size(resource, TEST_TEXT_SIZE);
	LEGACY_WRITE_U16_LE(resource + RESOURCE_FILE_COUNT_OFFSET, TEST_TEXT_COUNT);
	memcpy(resource + RESOURCE_FILE_DIRECTORY_OFFSET, "gvergstudata",
		   TEST_TEXT_COUNT * RESOURCE_FILE_IDENTIFIER_SIZE);
	resource_file_set_offset(resource, TEST_TEXT_COUNT, TEST_TEXT_VERSION_INDEX, 0);
	resource_file_set_offset(resource, TEST_TEXT_COUNT, TEST_TEXT_TITLE_INDEX,
							 TEST_TEXT_TITLE_OFFSET);
	resource_file_set_offset(resource, TEST_TEXT_COUNT, TEST_TEXT_UNRELATED_INDEX,
							 TEST_TEXT_UNRELATED_OFFSET);
	memcpy(resource + TEST_TEXT_DATA_OFFSET, test_text_version, sizeof(test_text_version));
	memcpy(resource + TEST_TEXT_DATA_OFFSET + TEST_TEXT_TITLE_OFFSET, TEST_TEXT_TITLE,
		   sizeof(TEST_TEXT_TITLE));
}

static void test_resource_text_matches(void)
{
	legacy_u8 resource[TEST_TEXT_SIZE];
	make_text_resource(resource);
	assert(
		resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));
	assert(!resource_text_equals(resource, sizeof(resource), test_text_identifier,
								 test_text_other_version));
	assert(!resource_text_equals(resource, sizeof(resource), test_text_missing_identifier,
								 test_text_version));
	assert(
		!resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_prefix));
	assert(
		!resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_empty));
	assert(!resource_text_equals(resource, sizeof(resource), test_text_short_identifier,
								 test_text_version));
	assert(!resource_text_equals(resource, sizeof(resource), test_text_long_identifier,
								 test_text_version));
	assert(!resource_text_equals(0, sizeof(resource), test_text_identifier, test_text_version));
	assert(!resource_text_equals(resource, sizeof(resource), 0, test_text_version));
	assert(!resource_text_equals(resource, sizeof(resource), test_text_identifier, 0));

	memcpy(resource + TEST_TEXT_DATA_OFFSET + TEST_TEXT_TITLE_OFFSET, "Custom",
		   sizeof(TEST_TEXT_TITLE));
	memset(resource + TEST_TEXT_DATA_OFFSET + TEST_TEXT_UNRELATED_OFFSET, TEST_TEXT_MODIFIED_BYTE,
		   TEST_RESOURCE_PAYLOAD_SIZE);
	assert(
		resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));

	/* Both the directory and the data may be reordered by a resource editor. */
	memcpy(resource + RESOURCE_FILE_DIRECTORY_OFFSET, "datagvergstu",
		   TEST_TEXT_COUNT * RESOURCE_FILE_IDENTIFIER_SIZE);
	resource_file_set_offset(resource, TEST_TEXT_COUNT, TEST_TEXT_VERSION_INDEX,
							 TEST_TEXT_UNRELATED_OFFSET);
	resource_file_set_offset(resource, TEST_TEXT_COUNT, TEST_TEXT_TITLE_INDEX, 0);
	resource_file_set_offset(resource, TEST_TEXT_COUNT, TEST_TEXT_UNRELATED_INDEX,
							 TEST_TEXT_TITLE_OFFSET);
	assert(
		resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));

	make_text_resource(resource);
	legacy_u32 version_offset = sizeof(TEST_TEXT_TITLE) + TEST_RESOURCE_PAYLOAD_SIZE;
	memset(resource + TEST_TEXT_DATA_OFFSET, TEST_TEXT_MODIFIED_BYTE,
		   TEST_TEXT_SIZE - TEST_TEXT_DATA_OFFSET);
	memcpy(resource + TEST_TEXT_DATA_OFFSET, TEST_TEXT_TITLE, sizeof(TEST_TEXT_TITLE));
	memcpy(resource + TEST_TEXT_DATA_OFFSET + version_offset, test_text_version,
		   sizeof(test_text_version));
	resource_file_set_offset(resource, TEST_TEXT_COUNT, TEST_TEXT_VERSION_INDEX, version_offset);
	resource_file_set_offset(resource, TEST_TEXT_COUNT, TEST_TEXT_TITLE_INDEX, 0);
	resource_file_set_offset(resource, TEST_TEXT_COUNT, TEST_TEXT_UNRELATED_INDEX,
							 sizeof(TEST_TEXT_TITLE));
	assert(
		resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));
}

static void test_resource_text_bounds(void)
{
	legacy_u8 resource[TEST_TEXT_SIZE];
	make_text_resource(resource);
	for (legacy_u32 length = 0; length < RESOURCE_FILE_DIRECTORY_OFFSET; ++length) {
		assert(!resource_text_equals(resource, length, test_text_identifier, test_text_version));
	}
	assert(!resource_text_equals(resource, sizeof(resource) - 1U, test_text_identifier,
								 test_text_version));
	resource_file_set_size(resource, RESOURCE_FILE_DIRECTORY_OFFSET - 1U);
	assert(
		!resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));
	resource_file_set_size(resource, TEST_TEXT_DATA_OFFSET - 1U);
	assert(
		!resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));

	make_text_resource(resource);
	LEGACY_WRITE_U16_LE(resource + RESOURCE_FILE_COUNT_OFFSET, TEST_TEXT_MAX_COUNT);
	assert(
		!resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));
	LEGACY_WRITE_U16_LE(resource + RESOURCE_FILE_COUNT_OFFSET, 0);
	assert(
		!resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));

	make_text_resource(resource);
	resource_file_set_offset(resource, TEST_TEXT_COUNT, TEST_TEXT_UNRELATED_INDEX,
							 TEST_TEXT_INVALID_OFFSET);
	assert(
		!resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));
	resource_file_set_offset(resource, TEST_TEXT_COUNT, TEST_TEXT_UNRELATED_INDEX,
							 TEST_TEXT_SIZE - TEST_TEXT_DATA_OFFSET + 1U);
	assert(
		!resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));

	make_text_resource(resource);
	resource_file_set_offset(resource, TEST_TEXT_COUNT, TEST_TEXT_VERSION_INDEX,
							 TEST_TEXT_SIZE - TEST_TEXT_DATA_OFFSET);
	assert(
		!resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));

	make_text_resource(resource);
	resource[TEST_TEXT_DATA_OFFSET + sizeof(test_text_version) - 1U] = TEST_TEXT_MODIFIED_BYTE;
	assert(
		!resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));
	make_text_resource(resource);
	resource_file_set_offset(resource, TEST_TEXT_COUNT, TEST_TEXT_TITLE_INDEX,
							 sizeof(test_text_version) - 1U);
	assert(
		!resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));

	make_text_resource(resource);
	memcpy(resource + RESOURCE_FILE_DIRECTORY_OFFSET + RESOURCE_FILE_IDENTIFIER_SIZE, "gver",
		   RESOURCE_FILE_IDENTIFIER_SIZE);
	assert(
		!resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));
}

static void test_resource_text_large_directory(void)
{
	static legacy_u8 resource[TEST_TEXT_LARGE_SIZE];
	resource_file_set_size(resource, sizeof(resource));
	LEGACY_WRITE_U16_LE(resource + RESOURCE_FILE_COUNT_OFFSET, TEST_TEXT_LARGE_COUNT);
	memcpy(resource + RESOURCE_FILE_DIRECTORY_OFFSET, "gver", RESOURCE_FILE_IDENTIFIER_SIZE);
	memcpy(resource + TEST_TEXT_LARGE_DATA_OFFSET, test_text_version, sizeof(test_text_version));
	assert(
		resource_text_equals(resource, sizeof(resource), test_text_identifier, test_text_version));
}

legacy_s32 main(void)
{
	legacy_u8 resource[TEST_RESOURCE_SIZE] = {0,
											  0,
											  0,
											  0,
											  TEST_RESOURCE_COUNT,
											  0,
											  'a',
											  'b',
											  'c',
											  0,
											  0,
											  0,
											  0,
											  0,
											  TEST_RESOURCE_MARKER_BYTE_0,
											  TEST_RESOURCE_MARKER_BYTE_1,
											  TEST_RESOURCE_MARKER_BYTE_2,
											  TEST_RESOURCE_MARKER_BYTE_3};

	const legacy_s8 literal_name[] = "abc";
	legacy_s8 far *result = locate_resource((legacy_s8 far *)resource, literal_name, 0U);
	assert(result == (legacy_s8 far *)&resource[TEST_RESOURCE_DATA_OFFSET]);
	assert(literal_name[TEST_NAME_TERMINATOR_OFFSET] == 0);

	legacy_s8 four_byte_name[RESOURCE_FILE_IDENTIFIER_SIZE] = {'a', 'b', 'c', 0};
	result = locate_shape_nofatal((legacy_s8 far *)resource, four_byte_name);
	assert(result == (legacy_s8 far *)&resource[TEST_RESOURCE_DATA_OFFSET]);
	assert(four_byte_name[TEST_NAME_TERMINATOR_OFFSET] == 0);

	test_resource_text_matches();
	test_resource_text_bounds();
	test_resource_text_large_directory();

	return 0;
}
