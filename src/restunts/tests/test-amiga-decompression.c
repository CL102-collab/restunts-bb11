#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/platform.h"

legacy_u32 file_decomp_rle(legacy_u8 huge *source, legacy_u8 huge *destination,
						 legacy_u16 paragraphs);

static legacy_u8 memory[8192];

void far *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	legacy_u32 address = (legacy_u32)segment * 16U + offset;
	assert(address < sizeof(memory));
	return memory + address;
}

legacy_u16 dos_memory_pointer_segment(const void far *pointer)
{
	assert((const legacy_u8 *)pointer >= memory &&
		   (const legacy_u8 *)pointer < memory + sizeof(memory));
	return (legacy_u16)(((const legacy_u8 *)pointer - memory) >> 4);
}

legacy_u16 dos_memory_pointer_offset(const void far *pointer)
{
	return (legacy_u16)(((const legacy_u8 *)pointer - memory) & 15U);
}

void copy_paras_reverse(legacy_u16 source, legacy_u16 destination, legacy_s16 paragraphs)
{
	assert(paragraphs >= 0);
	memmove(memory + (legacy_u32)destination * 16U,
			memory + (legacy_u32)source * 16U, (legacy_u32)paragraphs * 16U);
}

static void test_escape_tables(void)
{
	static const unsigned offsets[] = { 0, 1, 15, 16, 127, 256, 513, 1023, 2048 };
	unsigned slot, count, index;
	legacy_u8 *destination = memory + 4096;
	for (slot = 0; slot < sizeof(offsets) / sizeof(offsets[0]); ++slot) {
		for (count = 1; count <= 16; ++count) {
			legacy_u8 *source = memory + offsets[slot];
			memset(source, 0, 128);
			source[0] = 1;
			source[1] = 37;
			source[4] = 37;
			source[8] = 128U | count;
			for (index = 0; index < count; ++index) source[9 + index] = 224U + index;
			for (index = 0; index < 37; ++index) source[9 + count + index] = index;
			memset(destination, 0xa5, 38);
			assert(file_decomp_rle(source, destination, 128) == 37);
			for (index = 0; index < 37; ++index) assert(destination[index] == index);
			assert(destination[37] == 0xa5);
		}
	}
}

static void test_run_counts(void)
{
	static const legacy_u8 packed[] = {
		1, 37, 0, 0, 17, 0, 0, 0, 133, 224, 225, 226, 227, 228,
		17, 224, 5, 34, 226, 19, 0, 51, 227, 68, 228, 85,
		102, 103, 104, 105, 106
	};
	legacy_u8 *destination = memory + 4096;
	unsigned index;
	memcpy(memory + 1, packed, sizeof(packed));
	memset(destination, 0xa5, 38);
	assert(file_decomp_rle(memory + 1, destination, 128) == 37);
	assert(destination[0] == 17);
	for (index = 1; index < 6; ++index) assert(destination[index] == 34);
	for (; index < 25; ++index) assert(destination[index] == 51);
	for (; index < 28; ++index) assert(destination[index] == 68);
	for (; index < 32; ++index) assert(destination[index] == 85);
	for (; index < 37; ++index) assert(destination[index] == 102U + index - 32U);
	assert(destination[37] == 0xa5);
}

int main(void)
{
	test_escape_tables();
	test_run_counts();
	puts("Amiga decompression tests passed.");
	return 0;
}
