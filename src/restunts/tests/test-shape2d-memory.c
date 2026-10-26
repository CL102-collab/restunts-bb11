#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/platform.h"
#include "../c/resource.h"
#include "../c/shape2d.h"

#define TEST_PARAGRAPH_BYTES 16U
#define TEST_ARENA_START 4096U
#define TEST_ARENA_END 32768U
#define TEST_SHAPE_COUNT 5U
#define TEST_SHAPE_WIDTH 160U
#define TEST_SHAPE_HEIGHT 96U
#define TEST_SHAPE_BYTES (SHAPE2D_HEADER_SIZE + TEST_SHAPE_WIDTH * TEST_SHAPE_HEIGHT)
#define TEST_RESOURCE_BYTES (6U + TEST_SHAPE_COUNT * (8U + TEST_SHAPE_BYTES))
#define TEST_RESOURCE_PARAGRAPHS (TEST_RESOURCE_BYTES / TEST_PARAGRAPH_BYTES + 1U)

legacy_u8 palmap[16];
legacy_s8 *shapeexts[] = {""};

static union {
	legacy_u32 alignment;
	legacy_u8 bytes[TEST_ARENA_END * TEST_PARAGRAPH_BYTES];
} test_memory;
static legacy_u8 resource_fixture[TEST_RESOURCE_BYTES];
static legacy_u8 expected_resource[TEST_RESOURCE_BYTES];
static legacy_u32 load_count;

void fatal_error(const legacy_s8 *format, ...)
{
	fprintf(stderr, "Unexpected shape loader error: %s\n", format);
	abort();
}

void *_memcpy(void *destination, const void *source, legacy_u16 length)
{
	return memmove(destination, source, length);
}

void far *__fmemcpy(void far *destination, const void far *source, legacy_u16 length)
{
	return memmove(destination, source, length);
}

void far *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	size_t address = (size_t)segment * TEST_PARAGRAPH_BYTES + offset;
	assert(address < sizeof(test_memory.bytes));
	return test_memory.bytes + address;
}

legacy_u16 dos_memory_pointer_segment(const void far *pointer)
{
	ptrdiff_t address = (const legacy_u8 *)pointer - test_memory.bytes;
	assert(address >= 0 && (size_t)address < sizeof(test_memory.bytes));
	return (legacy_u16)((size_t)address / TEST_PARAGRAPH_BYTES);
}

legacy_u16 dos_memory_pointer_offset(const void far *pointer)
{
	ptrdiff_t address = (const legacy_u8 *)pointer - test_memory.bytes;
	assert(address >= 0 && (size_t)address < sizeof(test_memory.bytes));
	return (legacy_u16)((size_t)address % TEST_PARAGRAPH_BYTES);
}

void far *dos_memory_get_psp(void)
{
	return dos_memory_make_pointer(TEST_ARENA_START - 16U, 0U);
}

legacy_u16 dos_memory_allocate(legacy_u16 paragraphs)
{
	assert(paragraphs <= TEST_ARENA_END - TEST_ARENA_START);
	return TEST_ARENA_START;
}

legacy_u16 dos_memory_resize(legacy_u16 segment, legacy_u16 paragraphs)
{
	assert(segment == TEST_ARENA_START);
	assert(paragraphs <= TEST_ARENA_END - TEST_ARENA_START);
	return paragraphs;
}

const legacy_s8 *file_find(const legacy_s8 *name)
{
	(void)name;
	abort();
}

void far *file_decomp(const legacy_s8 *name, legacy_s16 fatal)
{
	(void)name;
	(void)fatal;
	abort();
}

void far *file_load_binary(const legacy_s8 *name, legacy_s16 fatal)
{
	assert(strcmp((const legacy_char *)name, "TEST.VSH") == 0);
	assert(fatal != 0);
	load_count++;
	legacy_u8 *resource = mmgr_alloc_resbytes(name, TEST_RESOURCE_BYTES);
	memmove(resource, resource_fixture, TEST_RESOURCE_BYTES);
	return resource;
}

static void make_fixture(void)
{
	resource_file_set_size(resource_fixture, TEST_RESOURCE_BYTES);
	LEGACY_WRITE_U16_LE(resource_fixture + 4U, TEST_SHAPE_COUNT);
	for (legacy_u32 shape_index = 0; shape_index < TEST_SHAPE_COUNT; shape_index++) {
		legacy_u8 *identifier = resource_fixture + 6U + shape_index * 4U;
		memcpy(identifier, "SH00", 4U);
		identifier[3] += shape_index;
		resource_file_set_offset(resource_fixture, TEST_SHAPE_COUNT, shape_index,
								 shape_index * TEST_SHAPE_BYTES);
		struct SHAPE2D *shape = (struct SHAPE2D *)resource_file_data(resource_fixture, shape_index);
		shape->width = TEST_SHAPE_WIDTH;
		shape->height = TEST_SHAPE_HEIGHT;
		legacy_u8 *pixels = (legacy_u8 *)shape + SHAPE2D_HEADER_SIZE;
		for (legacy_u32 index = 0; index < TEST_SHAPE_WIDTH * TEST_SHAPE_HEIGHT; index++) {
			pixels[index] = (legacy_u8)(index % 128U < 24U ? index : shape_index + 16U);
		}
	}
}

static legacy_u8 *load_with_available_space(legacy_u16 available)
{
	memset(test_memory.bytes, 0xa5, sizeof(test_memory.bytes));
	mmgr_alloc_resmem(TEST_ARENA_END);
	mmgr_alloc_pages("PINNED", TEST_ARENA_END - TEST_ARENA_START - available);
	load_count = 0;
	legacy_u8 *resource = file_load_shape2d_res_fatal("TEST.VSH");
	assert(load_count == 1U);
	return resource;
}

/* Validate every encoded pixel and find the exact written size, excluding
 * allocation padding whose contents need not match between parsing paths. */
static size_t check_parsed_resource(legacy_u8 *resource)
{
	legacy_u8 *end = resource;
	assert(resource_file_count(resource) == TEST_SHAPE_COUNT);
	for (legacy_u32 shape_index = 0; shape_index < TEST_SHAPE_COUNT; shape_index++) {
		legacy_u8 *shape = resource_file_data(resource, shape_index);
		legacy_u8 *source = resource_file_data(resource_fixture, shape_index);
		assert(memcmp(shape, source, SHAPE2D_HEADER_SIZE) == 0);
		assert(memcmp(resource_file_identifier(resource, shape_index),
					  resource_file_identifier(resource_fixture, shape_index), 4U) == 0);
		legacy_u8 *encoded = shape + SHAPE2D_HEADER_SIZE;
		legacy_u8 *pixels = source + SHAPE2D_HEADER_SIZE;
		legacy_u32 decoded = 0;
		for (;;) {
			legacy_u8 control_byte = *encoded++;
			legacy_s8 control = LEGACY_S8_FROM_BITS(control_byte);
			if (control == 0) {
				break;
			}
			legacy_u32 count = control < 0 ? -control : control;
			assert(decoded + count <= TEST_SHAPE_WIDTH * TEST_SHAPE_HEIGHT);
			for (legacy_u32 index = 0; index < count; index++) {
				assert(pixels[decoded++] == (control < 0 ? *encoded++ : *encoded));
			}
			if (control > 0) {
				encoded++;
			}
		}
		assert(decoded == TEST_SHAPE_WIDTH * TEST_SHAPE_HEIGHT);
		end = encoded;
	}
	return (size_t)(end - resource);
}

int main(void)
{
	make_fixture();
	legacy_u8 *resource = load_with_available_space(TEST_RESOURCE_PARAGRAPHS * 5U / 2U);
	size_t parsed_bytes = check_parsed_resource(resource);
	legacy_u16 parsed_paragraphs = mmgr_get_chunk_size((legacy_s8 *)resource);
	assert(parsed_paragraphs == (parsed_bytes + TEST_PARAGRAPH_BYTES - 1U) / TEST_PARAGRAPH_BYTES);
	assert(parsed_paragraphs < TEST_RESOURCE_PARAGRAPHS / 2U);
	memcpy(expected_resource, resource, parsed_bytes);

	/* Both budgets force overlapping input/output; the tighter one also
 * exercises the reduced overlap margin selected from available space. */
	static const legacy_u16 available[] = {TEST_RESOURCE_PARAGRAPHS * 15U / 8U,
										   TEST_RESOURCE_PARAGRAPHS * 8U / 5U};
	for (legacy_u32 scenario = 0; scenario < sizeof(available) / sizeof(available[0]); scenario++) {
		resource = load_with_available_space(available[scenario]);
		assert(check_parsed_resource(resource) == parsed_bytes);
		assert(memcmp(resource, expected_resource, parsed_bytes) == 0);
		assert(mmgr_get_chunk_size((legacy_s8 *)resource) == parsed_paragraphs);
		assert(mmgr_get_ofs_diff() == available[scenario] - parsed_paragraphs);
		/* The next resource fits only after discarding the raw dashboard tail. */
		void *next = mmgr_alloc_pages("NEXT", TEST_RESOURCE_PARAGRAPHS);
		memset(next, 0x5a, TEST_RESOURCE_PARAGRAPHS * TEST_PARAGRAPH_BYTES);
		assert(memcmp(resource, expected_resource, parsed_bytes) == 0);
	}
	return 0;
}
