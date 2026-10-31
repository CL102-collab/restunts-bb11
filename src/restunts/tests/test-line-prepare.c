#include <assert.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/shape2d/shape2d.h"
#include "../c/shape2d/shape2d_internal.h"
#include "../c/shape3d/shape3d_internal.h"

static void reset_clip(void)
{
	drawing_sprite.sprite_top = 20;
	drawing_sprite.sprite_bottom = 180;
	drawing_sprite.sprite_raster_left = 10;
	drawing_sprite.sprite_raster_right = 310;
}

static void test_line_modes(void)
{
	static const struct {
		legacy_s16 dx, dy;
		legacy_u16 mode, count;
	} cases[] = {{-10, 0, DRAW_LINE_MODE_HORIZONTAL_REVERSED, 11},
				 {10, 0, DRAW_LINE_MODE_HORIZONTAL, 11},
				 {0, 0, DRAW_LINE_MODE_POINT, 1},
				 {0, 10, DRAW_LINE_MODE_VERTICAL, 11},
				 {-10, 10, DRAW_LINE_MODE_DIAGONAL_LEFT, 11},
				 {10, 10, DRAW_LINE_MODE_DIAGONAL_RIGHT, 11},
				 {-3, 10, DRAW_LINE_MODE_Y_MAJOR_LEFT, 11},
				 {3, 10, DRAW_LINE_MODE_Y_MAJOR_RIGHT, 11},
				 {-10, 3, DRAW_LINE_MODE_X_MAJOR_LEFT, 11},
				 {10, 3, DRAW_LINE_MODE_X_MAJOR_RIGHT, 11}};

	reset_clip();
	legacy_u16 line[DRAW_LINE_WORD_COUNT];
	for (legacy_u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memset(line, 0, sizeof(line));
		line[DRAW_LINE_COLOR_INDEX] = 37;
		assert(line_prepare_clipped(100, 100, 100 + cases[i].dx, 100 + cases[i].dy, line) == 0);
		assert(line[DRAW_LINE_MODE_AND_CLIP_INDEX] == cases[i].mode);
		assert(line[DRAW_LINE_PIXEL_COUNT_INDEX] == cases[i].count);
		assert(line[DRAW_LINE_COLOR_INDEX] == 37);
		if (cases[i].mode >= DRAW_LINE_MODE_Y_MAJOR_LEFT &&
			cases[i].mode <= DRAW_LINE_MODE_X_MAJOR_RIGHT) {
			assert(line[DRAW_LINE_STEP_INDEX] == 19660);
		}
	}
}

static void test_step_rounding(void)
{
	/* Values from the original table and its DIV/SHR/SUB/ADC fallback.
	 * Cover the cutoff, round-up/down, an exact half-pixel slope, and both
	 * axes/directions. In particular, 1/50 needs 1311, not truncated 1310:
	 * its middle sample crosses into the next row/column only with 1311. */
	static const struct {
		legacy_u16 major, minor, step;
	} cases[] = {{48, 1, 1365}, {49, 2, 2674},	 {50, 1, 1311}, {50, 2, 2621}, {50, 25, 32768},
				 {51, 1, 1285}, {51, 25, 32125}, {52, 2, 2521}, {64, 1, 1024}, {150, 1, 437}};

	reset_clip();
	legacy_u16 line[DRAW_LINE_WORD_COUNT];
	for (legacy_u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		for (legacy_u16 mode = DRAW_LINE_MODE_Y_MAJOR_LEFT; mode <= DRAW_LINE_MODE_X_MAJOR_RIGHT;
			 mode++) {
			legacy_s16 dx = mode < DRAW_LINE_MODE_X_MAJOR_LEFT ? cases[i].minor : cases[i].major;
			legacy_s16 dy = mode < DRAW_LINE_MODE_X_MAJOR_LEFT ? cases[i].major : cases[i].minor;
			if (mode == DRAW_LINE_MODE_Y_MAJOR_LEFT || mode == DRAW_LINE_MODE_X_MAJOR_LEFT) {
				dx = -dx;
			}
			for (legacy_u32 reverse = 0; reverse < 2; reverse++) {
				memset(line, 0, sizeof(line));
				assert(line_prepare_unclipped(reverse ? 160 + dx : 160, reverse ? 25 + dy : 25,
											  reverse ? 160 : 160 + dx, reverse ? 25 : 25 + dy,
											  line) == 0);
				assert(line[DRAW_LINE_STEP_INDEX] == cases[i].step);
				assert(line[DRAW_LINE_MODE_AND_CLIP_INDEX] == mode);
				assert(line[DRAW_LINE_PIXEL_COUNT_INDEX] == cases[i].major + 1);
				assert(line_prepare_clipped(reverse ? 160 + dx : 160, reverse ? 25 + dy : 25,
											reverse ? 160 : 160 + dx, reverse ? 25 : 25 + dy,
											line) == 0);
				assert(line[DRAW_LINE_STEP_INDEX] == cases[i].step);
				assert(line[DRAW_LINE_MODE_AND_CLIP_INDEX] == mode);
			}
		}
	}
}

static void test_clipped_endpoints(void)
{
	reset_clip();
	legacy_u16 line[DRAW_LINE_WORD_COUNT] = {0};
	assert(line_prepare_clipped(100, 190, 100, 10, line) == 0);
	assert(line[DRAW_LINE_START_Y_INDEX] == 20);
	assert(line[DRAW_LINE_END_Y_INDEX] == 179);
	assert(line[DRAW_LINE_PIXEL_COUNT_INDEX] == 160);
	assert(line_prepare_clipped(0, 100, 319, 100, line) == 0);
	assert(line[DRAW_LINE_START_X_INDEX] == 10);
	assert(line[DRAW_LINE_END_X_INDEX] == 309);
	assert(line[DRAW_LINE_PIXEL_COUNT_INDEX] == 300);
	assert(line_prepare_clipped(400, 30, 500, 170, line) == DRAW_LINE_CLIP_RIGHT);
	assert(line[DRAW_LINE_PIXEL_COUNT_INDEX] == 0);
	assert(line_prepare_unclipped(100, 190, 100, 10, line) == 0);
	assert(line[DRAW_LINE_START_Y_INDEX] == 10);
	assert(line[DRAW_LINE_END_Y_INDEX] == 190);
	assert(line[DRAW_LINE_PIXEL_COUNT_INDEX] == 181);
}

static legacy_u16 random_word(legacy_u32 *seed)
{
	*seed = *seed * 1664525UL + 1013904223UL;
	return (legacy_u16)(*seed >> 16);
}

static legacy_u32 line_fingerprint(legacy_u16 unclipped, legacy_u16 wide_coordinates)
{
	reset_clip();
	static const legacy_u16 boundaries[] = {0,	   1,	  9,	 10,	19,	  20,	 179,
											180,   309,	  310,	 319,	320,  15999, 16000,
											32767, 32768, 49536, 49537, 65535};
	legacy_u16 line[DRAW_LINE_WORD_COUNT];
	legacy_u16 coordinates[4];
	legacy_u32 seed = 271828UL;
	legacy_u32 hash = 2166136261UL;
	for (legacy_u32 iteration = 0; iteration < 50000; iteration++) {
		for (legacy_u32 i = 0; i < 4; i++) {
			coordinates[i] = random_word(&seed);
			if (wide_coordinates == 0) {
				coordinates[i] = (legacy_u16)(coordinates[i] % 1000 - 300);
			} else if (iteration % 2 == 0) {
				coordinates[i] =
					boundaries[coordinates[i] % (sizeof(boundaries) / sizeof(boundaries[0]))];
			}
		}
		/* Unclipped subdivision assumes the x range intersects the viewport. */
		if (unclipped != 0 && wide_coordinates != 0) {
			coordinates[0] %= 320;
			coordinates[2] %= 320;
		}
		for (legacy_u32 i = 0; i < DRAW_LINE_WORD_COUNT; i++) {
			line[i] = random_word(&seed);
		}
		legacy_u16 result;
		if (unclipped != 0) {
			result = line_prepare_unclipped(coordinates[0], coordinates[1], coordinates[2],
											coordinates[3], line);
		} else {
			result = line_prepare_clipped(coordinates[0], coordinates[1], coordinates[2],
										  coordinates[3], line);
		}
		hash = (hash ^ result) * 16777619UL;
		for (legacy_u32 i = 0; i < DRAW_LINE_WORD_COUNT; i++) {
			hash = (hash ^ line[i]) * 16777619UL;
		}
	}
	return hash;
}

int main(void)
{
	/* All setup words, including words retained on rejection, contribute to
	 * these baselines, updated for the original long-line slope rounding. */
	static const legacy_u32 expected[] = {2774831423UL, 1964948289UL, 3864549147UL, 4141859671UL};

	test_line_modes();
	test_step_rounding();
	test_clipped_endpoints();
	for (legacy_u32 i = 0; i < 4; i++) {
		assert(line_fingerprint(i & 1U, i >> 1U) == expected[i]);
	}
	return 0;
}
