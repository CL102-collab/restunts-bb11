#include "../platform/amiga/amiga.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Report failures to the redirected Amiga shell output instead of opening
 * libnix's interactive assertion requester during unattended emulator runs. */
static void display_test_failure(legacy_uint line)
{
	printf("AMIGA DISPLAY TEST FAIL at line %u\n", line);
	fflush(stdout);
	exit(1);
}
#undef assert
#define assert(condition) ((condition) ? (void)0 : display_test_failure(__LINE__))

#define TEST_PITCH 47U
#define TEST_GUARD 16U
static legacy_u8 pixels[AMIGA_SCREEN_BYTES];
static legacy_u8 storage[AMIGA_DEPTH][TEST_GUARD + TEST_PITCH * AMIGA_HEIGHT + TEST_GUARD];

static legacy_u32 random_state = 0x12345678UL;
static legacy_u8 next_byte(void)
{
	random_state = random_state * 1664525UL + 1013904223UL;
	return (legacy_u8)(random_state >> 24);
}

static void check_conversion(legacy_u16 pitch, legacy_u16 pattern)
{
	legacy_u8 map[256], *planes[AMIGA_DEPTH];
	legacy_u32 i;
	legacy_u16 plane, y, x;
	for (i = 0; i < 256U; ++i) {
		map[i] = next_byte() & 31U;
	}
	for (i = 0; i < AMIGA_SCREEN_BYTES; ++i) {
		legacy_u16 fill = pattern == 3 ? (legacy_u16)((i / AMIGA_WIDTH) & 3U) : pattern;
		pixels[i] = fill == 0 ? 0 : fill == 1 ? 255 : fill == 2 ? (legacy_u8)i : next_byte();
	}
	memset(storage, 0xa5, sizeof(storage));
	for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
		planes[plane] = storage[plane] + TEST_GUARD;
	}
	amiga_chunky_to_planar(pixels, map, planes, pitch);
	/* Reconstruct each pixel independently from the five bitplanes. This
	 * checks plane order, pixel order, palette lookup, row pitch and padding. */
	for (y = 0; y < AMIGA_HEIGHT; ++y) {
		for (x = 0; x < AMIGA_WIDTH; ++x) {
			legacy_u8 result = 0;
			for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
				legacy_u8 byte = planes[plane][(legacy_u32)y * pitch + (x >> 3)];
				result |= ((byte >> (7U - (x & 7U))) & 1U) << plane;
			}
			assert(result == map[pixels[(legacy_u32)y * AMIGA_WIDTH + x]]);
		}
		for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
			for (x = AMIGA_ROW_BYTES; x < pitch; ++x) {
				assert(planes[plane][(legacy_u32)y * pitch + x] == 0xa5);
			}
		}
	}
	for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
		for (i = 0; i < TEST_GUARD; ++i) {
			assert(storage[plane][i] == 0xa5);
		}
		for (i = TEST_GUARD + (legacy_u32)pitch * AMIGA_HEIGHT; i < sizeof(storage[plane]); ++i) {
			assert(storage[plane][i] == 0xa5);
		}
	}
}

static void check_conversion_rows(void)
{
	static const legacy_u16 row_counts[] = {0, 1, 9, 70, 200, 65535U};
	static const legacy_u16 pitches[] = {AMIGA_ROW_BYTES, TEST_PITCH};
	static legacy_u8 expected[AMIGA_DEPTH][AMIGA_PLANE_BYTES];
	legacy_u8 map[256], *planes[AMIGA_DEPTH];
	legacy_u32 i;
	legacy_u16 plane, y, x, test, pitch_index;

	/* Build an independent bit-at-a-time reference once. Keeping it static
	 * avoids consuming the native task's stack or repeating slow checks. */
	for (i = 0; i < 256U; ++i) {
		map[i] = next_byte() & 31U;
	}
	memset(expected, 0, sizeof(expected));
	for (i = 0; i < AMIGA_SCREEN_BYTES; ++i) {
		pixels[i] = next_byte();
	}
	for (y = 0; y < AMIGA_HEIGHT; ++y) {
		for (x = 0; x < AMIGA_WIDTH; ++x) {
			legacy_u8 color = map[pixels[(legacy_u32)y * AMIGA_WIDTH + x]];
			for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
				if (color & (1U << plane)) {
					expected[plane][(legacy_u32)y * AMIGA_ROW_BYTES + (x >> 3)] |=
						(legacy_u8)(1U << (7U - (x & 7U)));
				}
			}
		}
	}
	/* Empty ranges must return before dereferencing any buffers. */
	amiga_chunky_to_planar_rows(NULL, NULL, NULL, TEST_PITCH, 0);
	for (pitch_index = 0; pitch_index < sizeof(pitches) / sizeof(pitches[0]); ++pitch_index) {
		legacy_u16 pitch = pitches[pitch_index];
		for (test = 0; test < sizeof(row_counts) / sizeof(row_counts[0]); ++test) {
			legacy_u16 rows = row_counts[test];
			legacy_u16 converted = rows > AMIGA_HEIGHT ? AMIGA_HEIGHT : rows;
			memset(storage, 0xa5, sizeof(storage));
			for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
				planes[plane] = storage[plane] + TEST_GUARD;
			}
			amiga_chunky_to_planar_rows(pixels, map, planes, pitch, rows);
			for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
				for (y = 0; y < converted; ++y) {
					assert(memcmp(planes[plane] + (legacy_u32)y * pitch,
								  expected[plane] + (legacy_u32)y * AMIGA_ROW_BYTES,
								  AMIGA_ROW_BYTES) == 0);
					for (x = AMIGA_ROW_BYTES; x < pitch; ++x) {
						assert(planes[plane][(legacy_u32)y * pitch + x] == 0xa5);
					}
				}
				for (i = 0; i < TEST_GUARD; ++i) {
					assert(storage[plane][i] == 0xa5);
				}
				/* This includes every unconverted row and the trailing guard. */
				for (i = TEST_GUARD + (legacy_u32)converted * pitch; i < sizeof(storage[plane]);
					 ++i) {
					assert(storage[plane][i] == 0xa5);
				}
			}
		}
	}
}

static void check_palette(void)
{
	static const legacy_u16 main_colors[32] = {
		0x000, 0x00a, 0x470, 0x555, 0xa00, 0xa0a, 0x5bf, 0x580, 0x494, 0xe00, 0xcc0,
		0xccc, 0x00f, 0x980, 0xff5, 0x777, 0x00e, 0xf92, 0x00b, 0xccc, 0xddd, 0xfff,
		0x690, 0xf00, 0xc96, 0x7cf, 0x44f, 0x666, 0x009, 0xbbb, 0xdef, 0xfff};
	static const legacy_u16 ega[16] = {0x000, 0x00a, 0x0a0, 0x0aa, 0xa00, 0xa0a, 0xa50, 0xaaa,
									   0x555, 0x55f, 0x5f5, 0x5ff, 0xf55, 0xf5f, 0xff5, 0xfff};
	struct AMIGA_PALETTE palette;
	legacy_u8 colors[256 * 3], faded[256 * 3], saved_map[256];
	legacy_u16 i, slot;
	memset(&palette, 0, sizeof(palette));
	memset(colors, 0, sizeof(colors));
	amiga_palette_update(&palette, 0, 256, colors);
	for (i = 0; i < 32; ++i) {
		assert(palette.colors[i] == 0);
	}
	for (i = 0; i < sizeof(colors); ++i) {
		colors[i] = next_byte() & 63U;
	}
	colors[0] = colors[1] = colors[2] = 0;
	colors[3] = colors[4] = colors[5] = 63;
	amiga_palette_update(&palette, 0, 256, colors);
	assert(palette.initialized);
	amiga_palette_select(&palette, "sdmsel", "!pal", 32, 0);
	assert(memcmp(palette.colors, main_colors, sizeof(main_colors)) == 0);
	for (i = 0; i < 256; ++i) {
		assert(palette.map[i] < 32);
	}
	assert(palette.map[108] == 2 && palette.map[116] == 25 && palette.map[200] == 13);
	memcpy(saved_map, palette.map, sizeof(saved_map));
	for (i = 0; i < sizeof(colors); ++i) {
		faded[i] = colors[i] / 2;
	}
	amiga_palette_update(&palette, 0, 256, faded);
	assert(memcmp(saved_map, palette.map, sizeof(saved_map)) == 0);
	for (slot = 0; slot < 32; ++slot) {
		assert(((palette.colors[slot] >> 8) & 15) == ((main_colors[slot] >> 8) & 15) * 31 / 63);
		assert(((palette.colors[slot] >> 4) & 15) == ((main_colors[slot] >> 4) & 15) * 31 / 63);
		assert((palette.colors[slot] & 15) == (main_colors[slot] & 15) * 31 / 63);
	}
	memset(faded, 0, sizeof(faded));
	amiga_palette_update(&palette, 0, 256, faded);
	assert(memcmp(saved_map, palette.map, sizeof(saved_map)) == 0);
	for (slot = 0; slot < 32; ++slot) {
		assert(palette.colors[slot] == 0);
	}
	amiga_palette_update(&palette, 0, 256, colors);
	assert(memcmp(palette.colors, main_colors, sizeof(main_colors)) == 0);
	amiga_palette_select(&palette, "SDTITL", "!def", 16, 0);
	assert(memcmp(palette.colors, ega, sizeof(ega)) == 0);
	for (i = 0; i < 16; ++i) {
		assert(palette.map[i] == i);
	}
	for (i = 0; i < 256; ++i) {
		assert(palette.map[i] < 16);
	}
	amiga_palette_select(&palette, "STDBCOUN", "!pal", 16, 16);
	assert(palette.colors[3] == 0xfa3 && palette.colors[9] == 0xfc8);
	for (i = 0; i < 256; ++i) {
		assert(palette.map[i] >= 16 && palette.map[i] < 32);
	}
	assert(palette.map[37] == 29 && palette.map[116] == 22);
	amiga_palette_select(&palette, "STDBFGTO", "!pal", 16, 16);
	assert(palette.colors[3] == 0x666 && palette.colors[11] == 0xf22);
	assert(palette.map[37] == 27 && palette.map[116] == 29);
	amiga_palette_select(&palette, "SDGAME", "!pal", 16, 16);
	assert(palette.colors[3] == 0x55f && palette.colors[12] == 0x7c6);
	assert(amiga_palette_original("MISSING", "!pal") == NULL);
	/* Unknown assets leave the last valid palette intact. */
	amiga_palette_select(&palette, "MISSING", "!pal", 32, 0);
	assert(palette.colors[12] == 0x7c6 && palette.first_register == 16);
}

legacy_int main(void)
{
	legacy_u16 pattern;
	check_palette();
	puts("Amiga palette checks passed");
	fflush(stdout);
#ifdef __mc68000__
	/* Every four rows cover all fill patterns. Two complete screens keep the
	 * independent bit-by-bit verification practical on a 7 MHz processor. */
	pattern = 3;
	check_conversion(AMIGA_ROW_BYTES, pattern);
	puts("Amiga standard-pitch conversion passed");
	fflush(stdout);
	check_conversion(TEST_PITCH, pattern);
#else
	for (pattern = 0; pattern < 4; ++pattern) {
		check_conversion(AMIGA_ROW_BYTES, pattern);
		check_conversion(TEST_PITCH, pattern);
	}
#endif
	check_conversion_rows();
	puts("Amiga row-range conversion passed");
	puts("AMIGA DISPLAY TEST PASS");
	return 0;
}
