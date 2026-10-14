#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../c/shape2d_internal.h"

/* Include the parser to check the same wrapped copy used for its headers and
 * literal runs, including overlap that must not become a bulk memcpy. */
#include "../c/shape2d_resources.c"

#define SOURCE_SEGMENT 0x2000U
#define BITMAP_SEGMENT 0x4000U
#define SEGMENT_BYTES 65536UL

struct SPRITE drawing_sprite;
struct SPRITE screen_sprite;
static legacy_u8 source_storage[2][SEGMENT_BYTES];
static legacy_u8 bitmap[SEGMENT_BYTES];
static legacy_u8 line_offsets[4];
static legacy_u8 *source_base = source_storage[0];

void far *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	assert(segment == SOURCE_SEGMENT || segment == BITMAP_SEGMENT);
	return (segment == SOURCE_SEGMENT ? source_base : bitmap) + offset;
}

legacy_u16 dos_memory_pointer_segment(const void far *pointer)
{
	uintptr_t address = (uintptr_t)pointer;
	if (pointer == &drawing_sprite) {
		return 0x6000U;
	}
	if (address >= (uintptr_t)source_base &&
		address < (uintptr_t)source_base + SEGMENT_BYTES) {
		return SOURCE_SEGMENT;
	}
	assert(address >= (uintptr_t)bitmap && address < (uintptr_t)bitmap + SEGMENT_BYTES);
	return BITMAP_SEGMENT;
}

legacy_u16 dos_memory_pointer_offset(const void far *pointer)
{
	legacy_u16 segment = dos_memory_pointer_segment(pointer);
	return (legacy_u16)((const legacy_u8 *)pointer -
						  (segment == SOURCE_SEGMENT ? source_base : bitmap));
}

static void test_wrapped_scan_and_copy(void)
{
	source_base[65534] = source_base[65535] = source_base[0] = source_base[1] = 9;
	source_base[2] = 10;
	assert(shape2d_count_repeated_bytes(source_base + 65534) == 4);
	legacy_u16 source = 65534U;
	legacy_u16 destination = 65535U;
	shape2d_copy_wrapped(SOURCE_SEGMENT, &source, BITMAP_SEGMENT, &destination, 5);
	assert(source == 3 && destination == 4);
	assert(bitmap[65535] == 9 && bitmap[0] == 9 && bitmap[1] == 9);
	assert(bitmap[2] == 9 && bitmap[3] == 10);

	source_base[0] = 17;
	source = 0;
	destination = 1;
	shape2d_copy_wrapped(SOURCE_SEGMENT, &source, SOURCE_SEGMENT, &destination, 4);
	for (unsigned index = 0; index < 5; index++) {
		assert(source_base[index] == 17);
	}
}

static void test_wrapped_draw(legacy_u8 first)
{
	const legacy_u16 offset = 65518U;
	struct SHAPE2D *shape = (struct SHAPE2D *)(source_base + offset);
	memset(shape, 0, SHAPE2D_HEADER_SIZE);
	LEGACY_WRITE_U16_LE(source_base + offset, 7);
	LEGACY_WRITE_U16_LE(source_base + offset + 2, 1);
	source_base[65534] = 252;
	source_base[65535] = first;
	source_base[0] = 2;
	source_base[1] = 3;
	source_base[2] = 4;
	source_base[3] = 3;
	source_base[4] = 42;
	source_base[5] = 0;

	memset(&drawing_sprite, 0, sizeof(drawing_sprite));
	drawing_sprite.sprite_bitmapptr = (struct SHAPE2D *)bitmap;
	drawing_sprite.sprite_lineofs = line_offsets;
	drawing_sprite.sprite_pitch = 8;
	LEGACY_WRITE_U16_LE(line_offsets, 0);
	LEGACY_WRITE_U16_LE(line_offsets + 2, 8);
	sprite_set_target_clip_bounds(0, 8, 0, 1);
	memset(bitmap, 0, 8);
	shape2d_rle_copy(shape, 0, 0);
	assert(bitmap[0] == first && bitmap[1] == 2 && bitmap[2] == 3 && bitmap[3] == 4);
	assert(bitmap[4] == 42 && bitmap[5] == 42 && bitmap[6] == 42 && bitmap[7] == 0);

	sprite_set_target_clip_bounds(1, 6, 0, 1);
	memset(bitmap, 0, 8);
	shape2d_rle_copy_clipped(shape, 0, 0);
	assert(bitmap[0] == 0 && bitmap[1] == 2 && bitmap[2] == 3 && bitmap[3] == 4);
	assert(bitmap[4] == 42 && bitmap[5] == 42 && bitmap[6] == 0 && bitmap[7] == 0);
}

int main(void)
{
	test_wrapped_scan_and_copy();
	test_wrapped_draw(1);
	/* Reuse the same logical segment with a different native allocation. */
	source_base = source_storage[1];
	test_wrapped_scan_and_copy();
	test_wrapped_draw(77);
	puts("Amiga wrapped RLE tests passed.");
	return 0;
}
