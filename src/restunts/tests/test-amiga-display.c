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
static legacy_u8 expected[AMIGA_DEPTH][AMIGA_PLANE_BYTES];
static legacy_u8 storage[AMIGA_DEPTH][TEST_GUARD + TEST_PITCH * AMIGA_PAL_HEIGHT + TEST_GUARD];

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

static void reference_scaled_band(const legacy_u8 *map, legacy_u16 first, legacy_u16 rows)
{
	legacy_u16 plane, y, x;
	for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
		memset(expected[plane] + (legacy_u32)first * AMIGA_ROW_BYTES, 0,
			   (legacy_u32)rows * AMIGA_ROW_BYTES);
	}
	for (y = first; y < first + rows; ++y) {
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
}

static void check_scaled_storage(legacy_u16 pitch, legacy_u16 height, legacy_u16 first,
								 legacy_u16 last)
{
	legacy_u16 plane, y, x;
	legacy_u32 i;
	for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
		legacy_u8 *data = storage[plane] + TEST_GUARD;
		for (y = 0; y < height; ++y) {
			legacy_u16 source = (legacy_u32)y * AMIGA_HEIGHT / height;
			if (source >= first && source < last) {
				assert(memcmp(data + (legacy_u32)y * pitch,
							  expected[plane] + (legacy_u32)source * AMIGA_ROW_BYTES,
							  AMIGA_ROW_BYTES) == 0);
			} else {
				for (x = 0; x < AMIGA_ROW_BYTES; ++x) {
					assert(data[(legacy_u32)y * pitch + x] == 0xa5);
				}
			}
			for (x = AMIGA_ROW_BYTES; x < pitch; ++x) {
				assert(data[(legacy_u32)y * pitch + x] == 0xa5);
			}
		}
		for (i = 0; i < TEST_GUARD; ++i) {
			assert(storage[plane][i] == 0xa5);
		}
		for (i = TEST_GUARD + (legacy_u32)height * pitch; i < sizeof(storage[plane]); ++i) {
			assert(storage[plane][i] == 0xa5);
		}
	}
}

static void check_display_scaling(void)
{
	static const legacy_u16 heights[] = {AMIGA_NTSC_HEIGHT, AMIGA_PAL_HEIGHT};
	static const legacy_u16 pitches[] = {AMIGA_ROW_BYTES, TEST_PITCH};
	static const legacy_u16 ranges[][2] = {{0, 200}, {0, 1},	{1, 1},
										   {150, 2}, {151, 49}, {199, 65535U}};
	static const legacy_u16 bands[][2] = {{0, 9}, {9, 121}, {130, 21}, {151, 49}};
	static const legacy_u16 invalid_heights[] = {0, AMIGA_HEIGHT - 1U, AMIGA_PAL_HEIGHT + 1U,
												 65535U};
	legacy_u8 map[AMIGA_DOS_COLORS], *planes[AMIGA_DEPTH];
	legacy_u16 height_index, pitch_index, plane, row, test, band;
	legacy_u32 i;

	for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
		planes[plane] = storage[plane] + TEST_GUARD;
	}
	for (i = 0; i < AMIGA_DOS_COLORS; ++i) {
		map[i] = next_byte() & 31U;
	}
	for (i = 0; i < AMIGA_SCREEN_BYTES; ++i) {
		pixels[i] = next_byte();
	}
	reference_scaled_band(map, 0, AMIGA_HEIGHT);
	assert(amiga_display_row(0, AMIGA_PAL_HEIGHT) == 0);
	assert(amiga_display_row(1, AMIGA_PAL_HEIGHT) == 2);
	assert(amiga_display_row(AMIGA_REPLAY_TOP, AMIGA_PAL_HEIGHT) == 194);
	assert(amiga_display_row(AMIGA_HEIGHT - 1U, AMIGA_PAL_HEIGHT) == 255);
	assert(amiga_display_row(AMIGA_HEIGHT, AMIGA_PAL_HEIGHT) == AMIGA_PAL_HEIGHT);
	for (height_index = 0; height_index < sizeof(heights) / sizeof(heights[0]); ++height_index) {
		legacy_u16 height = heights[height_index];
		/* The palette switch must occur at the first physical scanline whose
		 * source row reaches the logical boundary, including end-of-image. */
		for (row = 0; row <= AMIGA_HEIGHT; ++row) {
			legacy_u16 boundary = amiga_display_row(row, height);
			assert(boundary <= height);
			assert((legacy_u32)boundary * AMIGA_HEIGHT / height >= row);
			assert(boundary == 0 || (legacy_u32)(boundary - 1U) * AMIGA_HEIGHT / height < row);
		}
		for (pitch_index = 0; pitch_index < sizeof(pitches) / sizeof(pitches[0]); ++pitch_index) {
			legacy_u16 pitch = pitches[pitch_index];
			for (test = 0; test < sizeof(ranges) / sizeof(ranges[0]); ++test) {
				legacy_u16 first = ranges[test][0], rows = ranges[test][1];
				legacy_u16 last = rows > AMIGA_HEIGHT - first ? AMIGA_HEIGHT : first + rows;
				memset(storage, 0xa5, sizeof(storage));
				amiga_chunky_to_planar_scaled_rows(pixels + (legacy_u32)first * AMIGA_WIDTH, map,
												   planes, pitch, first, rows, height);
				check_scaled_storage(pitch, height, first, last);
			}
		}
	}
	/* Adjacent scene bands use independent palette maps. This catches source
	 * offsets or rounding that would shift the dashboard/replay boundary. */
	for (height_index = 0; height_index < sizeof(heights) / sizeof(heights[0]); ++height_index) {
		legacy_u16 height = heights[height_index];
		for (pitch_index = 0; pitch_index < sizeof(pitches) / sizeof(pitches[0]); ++pitch_index) {
			legacy_u16 pitch = pitches[pitch_index];
			memset(storage, 0xa5, sizeof(storage));
			for (band = 0; band < sizeof(bands) / sizeof(bands[0]); ++band) {
				legacy_u16 first = bands[band][0], rows = bands[band][1];
				for (i = 0; i < AMIGA_DOS_COLORS; ++i) {
					map[i] = (i + band * 7U) & 31U;
				}
				reference_scaled_band(map, first, rows);
				amiga_chunky_to_planar_scaled_rows(pixels + (legacy_u32)first * AMIGA_WIDTH, map,
												   planes, pitch, first, rows, height);
			}
			check_scaled_storage(pitch, height, 0, AMIGA_HEIGHT);
		}
	}
	for (test = 0; test < sizeof(invalid_heights) / sizeof(invalid_heights[0]); ++test) {
		amiga_chunky_to_planar_scaled_rows(NULL, NULL, NULL, TEST_PITCH, 0, AMIGA_HEIGHT,
										   invalid_heights[test]);
	}
	amiga_chunky_to_planar_scaled_rows(NULL, NULL, NULL, TEST_PITCH, 0, 0, AMIGA_PAL_HEIGHT);
	amiga_chunky_to_planar_scaled_rows(NULL, NULL, NULL, TEST_PITCH, AMIGA_HEIGHT, 1,
									   AMIGA_PAL_HEIGHT);
	amiga_chunky_to_planar_scaled_rows(NULL, NULL, NULL, TEST_PITCH, 65535U, 1, AMIGA_PAL_HEIGHT);
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
	check_display_scaling();
	puts("Amiga aspect scaling passed");
	puts("AMIGA DISPLAY TEST PASS");
	return 0;
}
