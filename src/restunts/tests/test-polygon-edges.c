#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "../c/externs.h"
#include "../c/shape2d/shape2d.h"
#include "../c/shape2d/shape2d_internal.h"
#include "../c/shape3d/shape3d_internal.h"

void generate_poly_edges(legacy_s16 *edges, const legacy_u16 *line, legacy_s16 clipping_mode);

void polygon_merge_second_edge(const legacy_u16 *line, legacy_u16 choose_edge_per_row,
							   legacy_u16 needs_clipping, legacy_s16 *edges);

#define EDGE_ROWS 480U

static legacy_u32 random_word(legacy_u32 *seed)
{
	*seed = *seed * 1664525UL + 1013904223UL;
	return *seed >> 16;
}

static void test_row_selection(void)
{
	legacy_s16 edges[EDGE_ROWS * 2U];
	for (legacy_u32 i = 0; i < EDGE_ROWS; i++) {
		edges[i] = 10;
		edges[EDGE_ROWS + i] = 20;
	}
	legacy_u16 line[DRAW_LINE_WORD_COUNT] = {0};
	line[DRAW_LINE_START_Y_INDEX] = 100;
	line[DRAW_LINE_START_X_INDEX] = 5;
	line[DRAW_LINE_PIXEL_COUNT_INDEX] = 3;
	line[DRAW_LINE_MODE_AND_CLIP_INDEX] = DRAW_LINE_MODE_DIAGONAL_RIGHT;
	edges[101] = 0;
	polygon_merge_second_edge(line, 1, 0, edges);
	assert(edges[100] == 5);
	assert(edges[101] == 0);
	assert(edges[102] == 7);
	assert(edges[EDGE_ROWS + 100] == 20);
	/* Fixed-side mode commits to the first selected edge even on later rows
	 * whose current span already contains the new position. */
	edges[100] = 10;
	polygon_merge_second_edge(line, 0, 0, edges);
	assert(edges[100] == 5);
	assert(edges[101] == 6);
	assert(edges[102] == 7);
}

static void test_final_x_major_carry(void)
{
	/* The final sample closes row 100. Its carry must not open row 101,
	 * which may already contain the endpoint of another polygon edge. */
	legacy_u16 line[DRAW_LINE_WORD_COUNT] = {0};
	line[DRAW_LINE_START_Y_INDEX] = 100;
	line[DRAW_LINE_START_X_INDEX] = 20;
	line[DRAW_LINE_PIXEL_COUNT_INDEX] = 2;
	line[DRAW_LINE_STEP_INDEX] = 16384;
	legacy_s16 edges[EDGE_ROWS * 2U];
	for (legacy_u16 mode = DRAW_LINE_MODE_X_MAJOR_LEFT; mode <= DRAW_LINE_MODE_X_MAJOR_RIGHT;
		 mode++) {
		for (legacy_u32 i = 0; i < EDGE_ROWS * 2U; i++) {
			edges[i] = 99;
		}
		line[DRAW_LINE_MODE_AND_CLIP_INDEX] = mode;
		generate_poly_edges(edges, line, 1);
		assert(edges[100] == (mode == DRAW_LINE_MODE_X_MAJOR_LEFT ? 19 : 20));
		assert(edges[EDGE_ROWS + 100] == (mode == DRAW_LINE_MODE_X_MAJOR_LEFT ? 20 : 21));
		assert(edges[101] == 99);
		assert(edges[EDGE_ROWS + 101] == 99);
	}
}

static void test_clip_padding(void)
{
	drawing_sprite.sprite_raster_left = 12;
	drawing_sprite.sprite_raster_right = 301;
	legacy_u16 line[DRAW_LINE_WORD_COUNT] = {0};
	line[DRAW_LINE_START_Y_INDEX] = 100;
	line[DRAW_LINE_START_Y_FRACTION_INDEX] = 32768U;
	line[DRAW_LINE_END_Y_INDEX] = 103;
	line[DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX] = 2;
	line[DRAW_LINE_START_RIGHT_CLIP_COUNT_INDEX] = 1;
	line[DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX] = 1;
	line[DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX] = 2;
	/* Padding is still emitted when the visible line has no samples. */
	legacy_s16 edges[EDGE_ROWS * 2U] = {0};
	polygon_merge_second_edge(line, 0, 1, edges);
	assert(edges[98] == 0);
	assert(edges[99] == 12);
	assert(edges[100] == 12);
	assert(edges[101] == 0);
	assert(edges[EDGE_ROWS + 99] == 0);
	assert(edges[EDGE_ROWS + 100] == 300);
	assert(edges[104] == 12);
	assert(edges[105] == 0);
	assert(edges[EDGE_ROWS + 104] == 300);
	assert(edges[EDGE_ROWS + 105] == 300);
}

static legacy_u32 edge_fingerprint(legacy_u16 choose_edge_per_row, legacy_u16 needs_clipping)
{
	drawing_sprite.sprite_raster_left = 7;
	drawing_sprite.sprite_raster_right = 313;
	static const legacy_u16 fractions[] = {0, 1, 32767, 32768, 65534, 65535};
	legacy_u16 line[DRAW_LINE_WORD_COUNT];
	legacy_s16 edges[EDGE_ROWS * 2U];
	legacy_u32 seed = 314159UL;
	legacy_u32 hash = 2166136261UL;
	for (legacy_u32 iteration = 0; iteration < 8192; iteration++) {
		memset(line, 0, sizeof(line));
		line[DRAW_LINE_MODE_AND_CLIP_INDEX] = iteration % 11;
		line[DRAW_LINE_START_X_INDEX] = random_word(&seed);
		line[DRAW_LINE_START_X_FRACTION_INDEX] = fractions[iteration % 6];
		line[DRAW_LINE_START_Y_FRACTION_INDEX] = fractions[(iteration / 6) % 6];
		line[DRAW_LINE_START_Y_INDEX] = 100 + random_word(&seed) % 100;
		line[DRAW_LINE_END_Y_INDEX] = 350;
		line[DRAW_LINE_PIXEL_COUNT_INDEX] = random_word(&seed) % 100;
		line[DRAW_LINE_STEP_INDEX] = random_word(&seed);
		for (legacy_u32 i = DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX; i < DRAW_LINE_WORD_COUNT; i++) {
			line[i] = random_word(&seed) % 8;
		}
		for (legacy_u32 i = 0; i < EDGE_ROWS * 2U; i++) {
			edges[i] = LEGACY_S16_FROM_BITS((legacy_u16)random_word(&seed));
		}
		polygon_merge_second_edge(line, choose_edge_per_row, needs_clipping, edges);
		for (legacy_u32 i = 0; i < EDGE_ROWS * 2U; i++) {
			hash = (hash ^ (legacy_u16)edges[i]) * 16777619UL;
		}
	}
	return hash;
}

static legacy_u32 first_edge_fingerprint(legacy_s16 clipping_mode)
{
	drawing_sprite.sprite_raster_left = 7;
	drawing_sprite.sprite_raster_right = 313;
	static const legacy_u16 fractions[] = {0, 1, 32767, 32768, 65534, 65535};
	legacy_u16 line[DRAW_LINE_WORD_COUNT];
	legacy_s16 edges[EDGE_ROWS * 2U];
	legacy_u32 seed = 161803UL;
	legacy_u32 hash = 2166136261UL;
	for (legacy_u32 iteration = 0; iteration < 8192; iteration++) {
		memset(line, 0, sizeof(line));
		line[DRAW_LINE_MODE_AND_CLIP_INDEX] = iteration % 11;
		line[DRAW_LINE_START_X_INDEX] = random_word(&seed);
		line[DRAW_LINE_START_X_FRACTION_INDEX] = fractions[iteration % 6];
		line[DRAW_LINE_START_Y_FRACTION_INDEX] = fractions[(iteration / 6) % 6];
		line[DRAW_LINE_START_Y_INDEX] = 100 + random_word(&seed) % 100;
		line[DRAW_LINE_END_Y_INDEX] = 350;
		line[DRAW_LINE_PIXEL_COUNT_INDEX] = random_word(&seed) % 100;
		line[DRAW_LINE_STEP_INDEX] = random_word(&seed);
		for (legacy_u32 i = DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX; i < DRAW_LINE_WORD_COUNT; i++) {
			line[i] = random_word(&seed) % 8;
		}
		for (legacy_u32 i = 0; i < EDGE_ROWS * 2U; i++) {
			edges[i] = LEGACY_S16_FROM_BITS((legacy_u16)random_word(&seed));
		}
		generate_poly_edges(edges, line, clipping_mode);
		for (legacy_u32 i = 0; i < EDGE_ROWS * 2U; i++) {
			hash = (hash ^ (legacy_u16)edges[i]) * 16777619UL;
		}
	}
	return hash;
}

int main(void)
{
	/* Baseline fingerprints cover all modes, fractional carry boundaries,
	 * wrapping x positions, initial spans, side selection, and clip padding.
	 * Each random input is evaluated once when converted to its signed value. */
	static const legacy_u32 expected[4] = {0x34eedc0bUL, 0x345f7697UL, 0x086138edUL, 0xded71e9cUL};

	test_row_selection();
	test_final_x_major_carry();
	test_clip_padding();
	for (legacy_u32 i = 0; i < 4; i++) {
#ifdef PRERENDER_RECORD_BASELINE
		fprintf(stdout, "edge%" LEGACY_PRIu32 "=%08" LEGACY_PRIx32 "\n", i,
				edge_fingerprint(i & 1U, i >> 1U));
#else
		assert(edge_fingerprint(i & 1U, i >> 1U) == expected[i]);
#endif
	}
#ifdef PRERENDER_RECORD_BASELINE
	fprintf(stdout, "%08" LEGACY_PRIx32 " %08" LEGACY_PRIx32 "\n", first_edge_fingerprint(0),
			first_edge_fingerprint(1));
#else
	/* First-edge baselines include untouched rows and clipping padding. */
	assert(first_edge_fingerprint(0) == 0xa0fcb7b7UL);
	assert(first_edge_fingerprint(1) == 0x169f0188UL);
#endif
	return 0;
}
