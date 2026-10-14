#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/shape2d_resources.c"

#define MAX_PIXELS 64000U
#define RESOURCE_PREFIX (14U + SHAPE2D_HEADER_SIZE)
#define GUARD_BYTES 17U

static legacy_u8 storage[GUARD_BYTES + RESOURCE_PREFIX + MAX_PIXELS + GUARD_BYTES];
static legacy_u8 original[MAX_PIXELS];
static legacy_u8 expected[MAX_PIXELS];
static legacy_u8 workspace[1U + (MAX_PIXELS + 7U) / 8U + 1U];

void *_memcpy(void *destination, const void *source, legacy_u16 length)
{
	return memmove(destination, source, length);
}

/* Preserve the original out-of-place loops as an independent reference for
 * each DOS layout, including its unequal even/odd blocks for odd heights. */
static void reference_unflip(unsigned width, unsigned height, unsigned layout)
{
	if (layout == 1) {
		for (unsigned row = 0; row < height; row++) {
			for (unsigned column = 0; column < width; column++) {
				expected[column + row * width] = original[row + column * height];
			}
		}
	} else if (layout == 2) {
		for (unsigned row = 0; row < height; row += 2) {
			for (unsigned column = 0; column < width; column++) {
				expected[column + row * width] = original[row / 2 + column * height];
			}
		}
		for (unsigned row = 1; row < height; row += 2) {
			for (unsigned column = 0; column < width; column++) {
				expected[column + row * width] = original[(height + row) / 2 + column * height];
			}
		}
	} else {
		unsigned even = (height + 1) / 2;
		unsigned odd = height / 2;
		for (unsigned row = 0; row < height; row += 2) {
			for (unsigned column = 0; column < width; column++) {
				expected[column + row * width] = original[row / 2 + column * even];
			}
		}
		for (unsigned row = 1; row < height; row += 2) {
			for (unsigned column = 0; column < width; column++) {
				expected[column + row * width] = original[width * even + row / 2 + column * odd];
			}
		}
	}
}

static void test_layout(unsigned width, unsigned height, unsigned layout, unsigned disabled)
{
	legacy_u8 *resource = storage + GUARD_BYTES;
	legacy_u8 *header = resource + 14;
	legacy_u8 *pixels = header + SHAPE2D_HEADER_SIZE;
	unsigned count = width * height;
	unsigned visited_bytes = (count + 7U) / 8U;
	legacy_u8 saved_prefix[RESOURCE_PREFIX];
	memset(storage, 0xa5, sizeof(storage));
	memset(workspace, 0x6d, sizeof(workspace));
	LEGACY_WRITE_U32_LE(resource, RESOURCE_PREFIX + count);
	LEGACY_WRITE_U16_LE(resource + 4, 1);
	memcpy(resource + 6, "TEST", 4);
	LEGACY_WRITE_U32_LE(resource + 10, 0);
	LEGACY_WRITE_U16_LE(header, width);
	LEGACY_WRITE_U16_LE(header + 2, height);
	header[14] = (legacy_u8)((layout << 4) | 7U);
	header[15] = disabled ? 0x10U : 0;
	memcpy(saved_prefix, resource, sizeof(saved_prefix));
	for (unsigned index = 0; index < count; index++) {
		original[index] = pixels[index] = (legacy_u8)(index * 37U + (index >> 8) * 11U);
	}
	unsigned applies = !disabled && layout > 0 && layout <= 3;
	if (applies) {
		reference_unflip(width, height, layout);
	} else {
		memcpy(expected, original, count);
	}
	assert(shape2d_unflip_workspace_pages(resource) ==
		   (applies ? (visited_bytes + 15U) / 16U : 0));
	file_unflip_shape2d(resource, (legacy_s8 *)(workspace + 1));
	assert(memcmp(pixels, expected, count) == 0);
	assert(memcmp(resource, saved_prefix, sizeof(saved_prefix)) == 0);
	for (unsigned index = 0; index < GUARD_BYTES; index++) {
		assert(storage[index] == 0xa5);
		assert(pixels[count + index] == 0xa5);
	}
	assert(workspace[0] == 0x6d);
	for (unsigned index = 1 + (applies ? visited_bytes : 0); index < sizeof(workspace); index++) {
		assert(workspace[index] == 0x6d);
	}
}

int main(void)
{
	static const unsigned dimensions[][2] = {
		{0, 0}, {0, 5}, {5, 0}, {1, 1}, {1, 7}, {7, 1},
		{2, 3}, {3, 2}, {7, 5}, {8, 6}, {319, 199}, {320, 200}
	};
	for (unsigned layout = 1; layout <= 3; layout++) {
		for (unsigned index = 0; index < sizeof(dimensions) / sizeof(dimensions[0]); index++) {
			test_layout(dimensions[index][0], dimensions[index][1], layout, 0);
		}
	}
	test_layout(7, 5, 0, 0);
	test_layout(7, 5, 4, 0);
	test_layout(7, 5, 1, 1);
	puts("Amiga in-place bitmap unflip tests passed.");
	return 0;
}
