#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../c/hires/frame_adaptive.h"
#include "../c/math.h"

#define TEST_CAMERA_TILE 15
#define TEST_MASK_RADIUS 10
#define TEST_MASK_DIAMETER (TEST_MASK_RADIUS * 2 + 1)
#define TEST_DIAGONAL_TRIG 11585
#define TEST_SMALL_HEADING_SINE 100
#define TEST_OVERSIZED_NS (~(legacy_u64)0)
#define TEST_BURST_NS (FRAME_ADAPTIVE_WORK_BUDGET_NS * 3U)
#define TEST_FAST_NS (FRAME_ADAPTIVE_WORK_BUDGET_NS / 2U)
#define TEST_SLOW_NS (FRAME_ADAPTIVE_WORK_BUDGET_NS * 3U / 2U)
#define TEST_ALTERNATING_FAST_NS (FRAME_ADAPTIVE_WORK_BUDGET_NS * 3U / 4U)
#define TEST_STUTTER_COMPANION_NS (FRAME_ADAPTIVE_WORK_BUDGET_NS * 9U / 10U)
#define TEST_BACKOFF_ATTEMPTS 6U
#define TEST_OUTSIDE_TILE (-1)
#define TEST_BENCHMARK_FAST_FRAMES 10000000UL
#define TEST_BENCHMARK_REBUILD_FRAMES 5000UL
#define TEST_NS_PER_SECOND 1000000000.0

static const legacy_char *large_mask[] = {"..LLLLL..", ".LLLLLLL.", ".LLLLLLL.", "LLHHHHHLL",
										  "LLHHHHHLL", "LLHHHHHLL", "LLHHHHHLL", ".LLHHHLL.",
										  "..LHHHL..", "...HCH..."};
static const legacy_char *small_mask[] = {"LLLLL", "LLLLL", "LHHHL", "HHHHH", ".HCH."};

static void record_frames(struct FRAME_ADAPTIVE_STATE *adaptive, legacy_u64 elapsed_ns,
						  legacy_u32 count)
{
	for (legacy_u32 frame = 0; frame < count; frame++) {
		frame_adaptive_record(adaptive, elapsed_ns);
	}
}

static legacy_u8 expected_flags(legacy_char cell)
{
	if (cell == 'H' || cell == 'C') {
		return FRAME_ADAPTIVE_FULL;
	}
	return cell == 'L' ? FRAME_ADAPTIVE_LOW_GEOMETRY : FRAME_ADAPTIVE_HIDE;
}

static void test_cardinal_masks_and_stage_scales(void)
{
	static const legacy_s16 cosines[] = {TRIG_FIXED_ONE, 0, -TRIG_FIXED_ONE, 0};
	static const legacy_s16 sines[] = {0, TRIG_FIXED_ONE, 0, -TRIG_FIXED_ONE};
	static const legacy_s32 scales[] = {FRAME_ADAPTIVE_FULL_SCALE, FRAME_ADAPTIVE_FULL_SCALE,
										FRAME_ADAPTIVE_HALF_SCALE, FRAME_ADAPTIVE_MINIMUM_SCALE,
										FRAME_ADAPTIVE_MINIMUM_SCALE};
	struct FRAME_ADAPTIVE_STATE adaptive;
	frame_adaptive_reset(&adaptive);
	for (legacy_u16 stage = 0; stage <= FRAME_ADAPTIVE_MAX_QUALITY; stage++) {
		adaptive.quality = stage;
		assert(frame_adaptive_render_scale(&adaptive) == scales[stage]);
		const legacy_char *const *mask =
			stage == FRAME_ADAPTIVE_SMALL_VIEW ? small_mask : large_mask;
		legacy_s32 height = stage == FRAME_ADAPTIVE_SMALL_VIEW
								? sizeof(small_mask) / sizeof(small_mask[0])
								: sizeof(large_mask) / sizeof(large_mask[0]);
		legacy_s32 width = strlen(mask[0]);
		for (legacy_u16 heading = 0; heading < sizeof(cosines) / sizeof(cosines[0]); heading++) {
			frame_adaptive_prepare(&adaptive, TEST_CAMERA_TILE, TEST_CAMERA_TILE, cosines[heading],
								   sines[heading]);
			for (legacy_s32 forward = -TEST_MASK_RADIUS; forward <= TEST_MASK_RADIUS; forward++) {
				for (legacy_s32 right = -TEST_MASK_RADIUS; right <= TEST_MASK_RADIUS; right++) {
					legacy_s16 east =
						TEST_CAMERA_TILE +
						(right * cosines[heading] + forward * sines[heading]) / TRIG_FIXED_ONE;
					legacy_s16 south =
						TEST_CAMERA_TILE +
						(right * sines[heading] - forward * cosines[heading]) / TRIG_FIXED_ONE;
					legacy_u8 expected = FRAME_ADAPTIVE_HIDE;
					legacy_s32 column = right + width / 2;
					if (stage == FRAME_ADAPTIVE_FULL_VIEW) {
						expected = FRAME_ADAPTIVE_FULL;
					} else if (forward >= 0 && forward < height && column >= 0 && column < width) {
						expected = expected_flags(mask[height - 1 - forward][column]);
					}
					assert(frame_adaptive_flags(&adaptive, east, south) == expected);
				}
			}
		}
	}
}

static void test_diagonal_masks(void)
{
	/* Independent raster fixtures for northeast; rotations also check the
	 * other three diagonal headings without quantizing them to cardinals. */
	static const legacy_char diagonal_large[TEST_MASK_DIAMETER][TEST_MASK_DIAMETER + 1] = {
		".....................", ".....................", "..............LL.....",
		"...........LLLLLL....", "..........LLLHLLLL...", ".........LLLHHHLLLL..",
		"..........LHHHHHLLL..", "..........LHHHHHHL...", ".........LHHHHHHLL...",
		".........HHHHHHLLL...", "..........HHHLLLL....", "...........HL..L.....",
		".....................", ".....................", ".....................",
		".....................", ".....................", ".....................",
		".....................", ".....................", "....................."};
	static const legacy_char diagonal_small[TEST_MASK_DIAMETER][TEST_MASK_DIAMETER + 1] = {
		".....................", ".....................", ".....................",
		".....................", ".....................", ".....................",
		"...........LL........", "..........LLLL.......", ".........HHHLLL......",
		".........HHHHLL......", "..........HHHL.......", "...........HH........",
		".....................", ".....................", ".....................",
		".....................", ".....................", ".....................",
		".....................", ".....................", "....................."};
	static const legacy_s16 cosines[] = {TEST_DIAGONAL_TRIG, -TEST_DIAGONAL_TRIG,
										 -TEST_DIAGONAL_TRIG, TEST_DIAGONAL_TRIG};
	static const legacy_s16 sines[] = {TEST_DIAGONAL_TRIG, TEST_DIAGONAL_TRIG, -TEST_DIAGONAL_TRIG,
									   -TEST_DIAGONAL_TRIG};
	for (legacy_u16 small = 0; small <= 1; small++) {
		struct FRAME_ADAPTIVE_STATE adaptive;
		frame_adaptive_reset(&adaptive);
		adaptive.quality = small ? FRAME_ADAPTIVE_SMALL_VIEW : FRAME_ADAPTIVE_LARGE_VIEW;
		for (legacy_u16 heading = 0; heading < sizeof(cosines) / sizeof(cosines[0]); heading++) {
			assert(frame_adaptive_prepare(&adaptive, TEST_CAMERA_TILE, TEST_CAMERA_TILE,
										  cosines[heading], sines[heading]));
			for (legacy_s32 row = 0; row < TEST_MASK_DIAMETER; row++) {
				for (legacy_s32 column = 0; column < TEST_MASK_DIAMETER; column++) {
					legacy_s32 east = column - TEST_MASK_RADIUS;
					legacy_s32 south = row - TEST_MASK_RADIUS;
					for (legacy_u16 turn = 0; turn < heading; turn++) {
						legacy_s32 saved_east = east;
						east = -south;
						south = saved_east;
					}
					legacy_char cell =
						small ? diagonal_small[row][column] : diagonal_large[row][column];
					assert(frame_adaptive_flags(&adaptive, TEST_CAMERA_TILE + east,
												TEST_CAMERA_TILE + south) == expected_flags(cell));
				}
			}
		}
	}
}

static void test_cache_full_quality_and_bounds(void)
{
	struct FRAME_ADAPTIVE_STATE adaptive;
	frame_adaptive_reset(&adaptive);
	adaptive.tile_flags[0] = FRAME_ADAPTIVE_HIDE;
	struct FRAME_ADAPTIVE_STATE saved = adaptive;
	assert(
		!frame_adaptive_prepare(&adaptive, TEST_CAMERA_TILE, TEST_CAMERA_TILE, TRIG_FIXED_ONE, 0));
	assert(memcmp(&saved, &adaptive, sizeof(adaptive)) == 0);
	assert(frame_adaptive_flags(&adaptive, TEST_OUTSIDE_TILE, 0) == FRAME_ADAPTIVE_FULL);
	adaptive.quality = FRAME_ADAPTIVE_LARGE_VIEW;
	assert(
		frame_adaptive_prepare(&adaptive, TEST_CAMERA_TILE, TEST_CAMERA_TILE, TRIG_FIXED_ONE, 0));
	saved = adaptive;
	assert(
		!frame_adaptive_prepare(&adaptive, TEST_CAMERA_TILE, TEST_CAMERA_TILE, TRIG_FIXED_ONE, 0));
	assert(memcmp(&saved, &adaptive, sizeof(adaptive)) == 0);
	/* Resolution-only transitions reuse the entire mask cache. */
	for (legacy_u16 stage = FRAME_ADAPTIVE_HALF_RESOLUTION;
		 stage <= FRAME_ADAPTIVE_MINIMUM_RESOLUTION; stage++) {
		adaptive.quality = stage;
		saved = adaptive;
		assert(!frame_adaptive_prepare(&adaptive, TEST_CAMERA_TILE, TEST_CAMERA_TILE,
									   TRIG_FIXED_ONE, 0));
		assert(memcmp(&saved, &adaptive, sizeof(adaptive)) == 0);
		assert(frame_adaptive_flags(&adaptive, 0, 0) == FRAME_ADAPTIVE_HIDE);
	}
	adaptive.quality = FRAME_ADAPTIVE_SMALL_VIEW;
	assert(frame_adaptive_flags(&adaptive, 0, 0) == FRAME_ADAPTIVE_FULL);
	assert(
		frame_adaptive_prepare(&adaptive, TEST_CAMERA_TILE, TEST_CAMERA_TILE, TRIG_FIXED_ONE, 0));
	assert(frame_adaptive_prepare(&adaptive, TEST_CAMERA_TILE + 1, TEST_CAMERA_TILE, TRIG_FIXED_ONE,
								  0));
	assert(frame_adaptive_prepare(&adaptive, TEST_CAMERA_TILE + 1, TEST_CAMERA_TILE + 1,
								  TRIG_FIXED_ONE, 0));
	/* Even a one-angle-step-sized heading change must invalidate the cache. */
	assert(frame_adaptive_prepare(&adaptive, TEST_CAMERA_TILE + 1, TEST_CAMERA_TILE + 1,
								  TRIG_FIXED_ONE, TEST_SMALL_HEADING_SINE));
	frame_adaptive_invalidate(&adaptive);
	assert(frame_adaptive_flags(&adaptive, 0, 0) == FRAME_ADAPTIVE_FULL);
	assert(
		frame_adaptive_prepare(&adaptive, TEST_CAMERA_TILE, TEST_CAMERA_TILE, TRIG_FIXED_ONE, 0));
	assert(frame_adaptive_flags(&adaptive, TEST_OUTSIDE_TILE, 0) == FRAME_ADAPTIVE_HIDE);
	assert(frame_adaptive_flags(&adaptive, TRACK_GRID_SIZE, TRACK_GRID_SIZE) ==
		   FRAME_ADAPTIVE_HIDE);
	adaptive.quality = LEGACY_U16_MAX;
	assert(frame_adaptive_render_scale(&adaptive) == FRAME_ADAPTIVE_MINIMUM_SCALE);
	assert(frame_adaptive_prepare(&adaptive, LEGACY_S16_MAX,
								  LEGACY_S16_FROM_BITS(LEGACY_U16_SIGN_BIT), TEST_DIAGONAL_TRIG,
								  TEST_DIAGONAL_TRIG));
	assert(frame_adaptive_flags(&adaptive, 0, 0) == FRAME_ADAPTIVE_HIDE);
	adaptive.quality = FRAME_ADAPTIVE_FULL_VIEW;
	saved = adaptive;
	assert(!frame_adaptive_prepare(&adaptive, 0, 0, 0, 0));
	assert(memcmp(&saved, &adaptive, sizeof(adaptive)) == 0);
	assert(frame_adaptive_flags(&adaptive, 0, 0) == FRAME_ADAPTIVE_FULL);
}

static void test_footprint_flags_and_floor(void)
{
	struct FRAME_ADAPTIVE_STATE adaptive;
	frame_adaptive_reset(&adaptive);
	legacy_u8 previous[FRAME_ADAPTIVE_TILE_COUNT] = {0};
	for (legacy_u16 stage = FRAME_ADAPTIVE_LARGE_VIEW; stage <= FRAME_ADAPTIVE_MAX_QUALITY;
		 stage++) {
		adaptive.quality = stage;
		frame_adaptive_prepare(&adaptive, TEST_CAMERA_TILE, TEST_CAMERA_TILE, TRIG_FIXED_ONE, 0);
		for (legacy_s16 south = 0; south < TRACK_GRID_SIZE; south++) {
			for (legacy_s16 east = 0; east < TRACK_GRID_SIZE; east++) {
				legacy_u16 index = south * TRACK_GRID_SIZE + east;
				legacy_u8 flags = frame_adaptive_flags(&adaptive, east, south);
				/* The small mask keeps full geometry across its near five-tile
				 * row, including two tiles marked low in the large mask. Only
				 * visibility is strictly monotonic between the authored masks. */
				assert(previous[index] != FRAME_ADAPTIVE_HIDE || flags == FRAME_ADAPTIVE_HIDE);
				previous[index] = flags;
			}
		}
		assert(frame_adaptive_flags(&adaptive, TEST_CAMERA_TILE, TEST_CAMERA_TILE) ==
			   FRAME_ADAPTIVE_FULL);
	}
	assert((FRAME_ADAPTIVE_HIDE & FRAME_ADAPTIVE_HIDE) == FRAME_ADAPTIVE_HIDE);
	assert((FRAME_ADAPTIVE_HIDE & FRAME_ADAPTIVE_LOW_GEOMETRY) == FRAME_ADAPTIVE_LOW_GEOMETRY);
	assert((FRAME_ADAPTIVE_HIDE & FRAME_ADAPTIVE_LOW_GEOMETRY & FRAME_ADAPTIVE_FULL) ==
		   FRAME_ADAPTIVE_FULL);
}

static void test_sustained_load_and_hysteresis(void)
{
	struct FRAME_ADAPTIVE_STATE adaptive;
	frame_adaptive_reset(&adaptive);
	struct FRAME_ADAPTIVE_STATE saved = adaptive;
	frame_adaptive_record(&adaptive, 0);
	assert(memcmp(&saved, &adaptive, sizeof(adaptive)) == 0);
	/* Isolated stalls cannot start the reduction ladder, even across windows. */
	for (legacy_u16 window = 0; window < FRAME_ADAPTIVE_RECOVERY_WINDOWS; window++) {
		frame_adaptive_record(&adaptive, TEST_OVERSIZED_NS);
		record_frames(&adaptive, TEST_STUTTER_COMPANION_NS, FRAME_ADAPTIVE_WINDOW_FRAMES - 1U);
		assert(adaptive.quality == FRAME_ADAPTIVE_FULL_VIEW);
	}
	record_frames(&adaptive, TEST_SLOW_NS, FRAME_ADAPTIVE_WINDOW_FRAMES - 1U);
	assert(adaptive.quality == FRAME_ADAPTIVE_FULL_VIEW);
	frame_adaptive_record(&adaptive, TEST_SLOW_NS);
	assert(adaptive.quality == FRAME_ADAPTIVE_LARGE_VIEW);
	/* Alternating expensive/cheap frames reduce quality when the mean is too slow. */
	for (legacy_u16 frame = 0; frame < FRAME_ADAPTIVE_WINDOW_FRAMES; frame++) {
		frame_adaptive_record(&adaptive, frame & 1U ? TEST_SLOW_NS : TEST_ALTERNATING_FAST_NS);
	}
	assert(adaptive.quality == FRAME_ADAPTIVE_HALF_RESOLUTION);
	record_frames(&adaptive, FRAME_ADAPTIVE_WORK_BUDGET_NS,
				  FRAME_ADAPTIVE_RECOVERY_WINDOWS * FRAME_ADAPTIVE_WINDOW_FRAMES);
	assert(adaptive.quality == FRAME_ADAPTIVE_HALF_RESOLUTION);
	/* Recovery requires sustained reserve and restores exactly one stage. */
	record_frames(&adaptive, TEST_FAST_NS,
				  FRAME_ADAPTIVE_RECOVERY_WINDOWS * FRAME_ADAPTIVE_WINDOW_FRAMES - 1U);
	assert(adaptive.quality == FRAME_ADAPTIVE_HALF_RESOLUTION);
	frame_adaptive_record(&adaptive, TEST_FAST_NS);
	assert(adaptive.quality == FRAME_ADAPTIVE_LARGE_VIEW && adaptive.recovery_probe != 0);
	record_frames(&adaptive, TEST_FAST_NS,
				  FRAME_ADAPTIVE_RECOVERY_WINDOWS * FRAME_ADAPTIVE_WINDOW_FRAMES);
	assert(adaptive.quality == FRAME_ADAPTIVE_FULL_VIEW);
}

static void test_uneven_load_and_interrupted_recovery(void)
{
	struct FRAME_ADAPTIVE_STATE adaptive;
	frame_adaptive_reset(&adaptive);
	for (legacy_u16 frame = 0; frame < FRAME_ADAPTIVE_WINDOW_FRAMES; frame++) {
		frame_adaptive_record(&adaptive,
							  frame < FRAME_ADAPTIVE_SLOW_FRAMES ? TEST_BURST_NS : TEST_FAST_NS);
	}
	assert(adaptive.quality == FRAME_ADAPTIVE_LARGE_VIEW);
	record_frames(&adaptive, TEST_FAST_NS,
				  (FRAME_ADAPTIVE_RECOVERY_WINDOWS - 1U) * FRAME_ADAPTIVE_WINDOW_FRAMES);
	record_frames(&adaptive, FRAME_ADAPTIVE_WORK_BUDGET_NS, FRAME_ADAPTIVE_WINDOW_FRAMES);
	record_frames(&adaptive, TEST_FAST_NS,
				  (FRAME_ADAPTIVE_RECOVERY_WINDOWS - 1U) * FRAME_ADAPTIVE_WINDOW_FRAMES);
	assert(adaptive.quality == FRAME_ADAPTIVE_LARGE_VIEW);
	record_frames(&adaptive, TEST_FAST_NS, FRAME_ADAPTIVE_WINDOW_FRAMES);
	assert(adaptive.quality == FRAME_ADAPTIVE_FULL_VIEW);
}

static void test_recovery_backoff_and_bounds(void)
{
	struct FRAME_ADAPTIVE_STATE adaptive;
	frame_adaptive_reset(&adaptive);
	adaptive.quality = FRAME_ADAPTIVE_SMALL_VIEW;
	legacy_u16 previous_wait = adaptive.recovery_windows;
	for (legacy_u16 attempt = 0; attempt < TEST_BACKOFF_ATTEMPTS; attempt++) {
		record_frames(&adaptive, TEST_FAST_NS,
					  adaptive.recovery_windows * FRAME_ADAPTIVE_WINDOW_FRAMES);
		assert(adaptive.quality == FRAME_ADAPTIVE_MINIMUM_RESOLUTION);
		record_frames(&adaptive, TEST_SLOW_NS, FRAME_ADAPTIVE_WINDOW_FRAMES);
		assert(adaptive.quality == FRAME_ADAPTIVE_SMALL_VIEW);
		assert(adaptive.recovery_windows >= previous_wait);
		assert(adaptive.recovery_windows <= FRAME_ADAPTIVE_MAX_RECOVERY_WINDOWS);
		previous_wait = adaptive.recovery_windows;
	}
	assert(adaptive.recovery_windows == FRAME_ADAPTIVE_MAX_RECOVERY_WINDOWS);
	record_frames(&adaptive, TEST_SLOW_NS,
				  FRAME_ADAPTIVE_MAX_QUALITY * FRAME_ADAPTIVE_WINDOW_FRAMES);
	assert(adaptive.quality == FRAME_ADAPTIVE_MAX_QUALITY);
	record_frames(&adaptive, TEST_OVERSIZED_NS,
				  FRAME_ADAPTIVE_MAX_QUALITY * FRAME_ADAPTIVE_WINDOW_FRAMES);
	assert(adaptive.quality == FRAME_ADAPTIVE_MAX_QUALITY);
	assert(adaptive.elapsed_ns == 0 && adaptive.samples == 0 && adaptive.slow_samples == 0);
	frame_adaptive_record(&adaptive, TEST_OVERSIZED_NS);
	assert(adaptive.elapsed_ns == FRAME_ADAPTIVE_MAX_SAMPLE_NS);
	frame_adaptive_reset(&adaptive);
	assert(adaptive.quality == FRAME_ADAPTIVE_FULL_VIEW && adaptive.cache_valid == 0);
	assert(adaptive.recovery_windows == FRAME_ADAPTIVE_RECOVERY_WINDOWS);
}

static void test_locked_presets_and_restart(void)
{
	static const legacy_u16 qualities[] = {
		FRAME_ADAPTIVE_FULL_VIEW, FRAME_ADAPTIVE_FULL_VIEW, FRAME_ADAPTIVE_LARGE_VIEW,
		FRAME_ADAPTIVE_HALF_RESOLUTION, FRAME_ADAPTIVE_MINIMUM_RESOLUTION};
	static const legacy_s32 scales[] = {FRAME_ADAPTIVE_FULL_SCALE, FRAME_ADAPTIVE_FULL_SCALE,
										FRAME_ADAPTIVE_FULL_SCALE, FRAME_ADAPTIVE_HALF_SCALE,
										FRAME_ADAPTIVE_MINIMUM_SCALE};
	struct FRAME_ADAPTIVE_STATE adaptive;
	frame_adaptive_reset(&adaptive);
	assert(adaptive.preset == FRAME_ADAPTIVE_PRESET_AUTO);
	for (legacy_u16 preset = FRAME_ADAPTIVE_PRESET_FULL; preset <= FRAME_ADAPTIVE_PRESET_LOW;
		 preset++) {
		/* Selecting a preset discards both a previous view and unfinished timing. */
		frame_adaptive_set_preset(&adaptive, FRAME_ADAPTIVE_PRESET_AUTO);
		record_frames(&adaptive, TEST_SLOW_NS, FRAME_ADAPTIVE_WINDOW_FRAMES + 1U);
		assert(frame_adaptive_prepare(&adaptive, TEST_CAMERA_TILE, TEST_CAMERA_TILE, TRIG_FIXED_ONE,
									  0));
		frame_adaptive_set_preset(&adaptive, (enum FRAME_ADAPTIVE_PRESET)preset);
		struct FRAME_ADAPTIVE_STATE expected;
		frame_adaptive_reset(&expected);
		expected.preset = preset;
		expected.quality = qualities[preset];
		assert(memcmp(&adaptive, &expected, sizeof(adaptive)) == 0);
		assert(frame_adaptive_render_scale(&adaptive) == scales[preset]);
		frame_adaptive_prepare(&adaptive, TEST_CAMERA_TILE, TEST_CAMERA_TILE, TRIG_FIXED_ONE, 0);
		struct FRAME_ADAPTIVE_STATE saved = adaptive;
		record_frames(&adaptive, TEST_SLOW_NS,
					  FRAME_ADAPTIVE_MAX_RECOVERY_WINDOWS * FRAME_ADAPTIVE_WINDOW_FRAMES);
		record_frames(&adaptive, TEST_FAST_NS,
					  FRAME_ADAPTIVE_MAX_RECOVERY_WINDOWS * FRAME_ADAPTIVE_WINDOW_FRAMES);
		frame_adaptive_record(&adaptive, TEST_OVERSIZED_NS);
		frame_adaptive_record(&adaptive, 0);
		assert(memcmp(&adaptive, &saved, sizeof(adaptive)) == 0);
		/* New races and F12 toggles keep the selection and restart its stage. */
		frame_adaptive_restart(&adaptive);
		assert(memcmp(&adaptive, &expected, sizeof(adaptive)) == 0);
		assert(adaptive.preset == preset && adaptive.quality == qualities[preset]);
		assert(frame_adaptive_render_scale(&adaptive) == scales[preset]);
	}
	/* Returning to automatic discards the lock and restores the full ladder. */
	frame_adaptive_set_preset(&adaptive, FRAME_ADAPTIVE_PRESET_AUTO);
	assert(adaptive.preset == FRAME_ADAPTIVE_PRESET_AUTO);
	assert(adaptive.quality == FRAME_ADAPTIVE_FULL_VIEW && adaptive.cache_valid == 0);
	record_frames(&adaptive, TEST_SLOW_NS,
				  FRAME_ADAPTIVE_MAX_QUALITY * FRAME_ADAPTIVE_WINDOW_FRAMES);
	assert(adaptive.quality == FRAME_ADAPTIVE_SMALL_VIEW);
	record_frames(&adaptive, TEST_FAST_NS,
				  FRAME_ADAPTIVE_RECOVERY_WINDOWS * FRAME_ADAPTIVE_WINDOW_FRAMES);
	assert(adaptive.quality == FRAME_ADAPTIVE_MINIMUM_RESOLUTION);
	frame_adaptive_restart(&adaptive);
	assert(adaptive.preset == FRAME_ADAPTIVE_PRESET_AUTO);
	assert(adaptive.quality == FRAME_ADAPTIVE_FULL_VIEW && adaptive.samples == 0);
	/* The reset API deliberately clears a saved selection for fresh test states. */
	frame_adaptive_set_preset(&adaptive, FRAME_ADAPTIVE_PRESET_LOW);
	frame_adaptive_reset(&adaptive);
	assert(adaptive.preset == FRAME_ADAPTIVE_PRESET_AUTO);
	assert(adaptive.quality == FRAME_ADAPTIVE_FULL_VIEW);
}

static void benchmark_policy(void)
{
	static const legacy_char *names[] = {"full", "cache-hit", "cache-rebuild"};
	volatile legacy_u32 consumed = 0;
	for (legacy_u16 mode = 0; mode < sizeof(names) / sizeof(names[0]); mode++) {
		struct FRAME_ADAPTIVE_STATE adaptive;
		frame_adaptive_reset(&adaptive);
		adaptive.quality = mode == 0 ? FRAME_ADAPTIVE_FULL_VIEW : FRAME_ADAPTIVE_SMALL_VIEW;
		legacy_u32 frames = mode == 2 ? TEST_BENCHMARK_REBUILD_FRAMES : TEST_BENCHMARK_FAST_FRAMES;
		clock_t start = clock();
		for (legacy_u32 frame = 0; frame < frames; frame++) {
			frame_adaptive_record(&adaptive, FRAME_ADAPTIVE_WORK_BUDGET_NS);
			legacy_s16 east = TEST_CAMERA_TILE + (mode == 2 ? frame & 1U : 0);
			consumed +=
				frame_adaptive_prepare(&adaptive, east, TEST_CAMERA_TILE, TRIG_FIXED_ONE, 0);
			consumed += frame_adaptive_flags(&adaptive, 0, 0);
		}
		legacy_f64 cpu_ns = (legacy_f64)(clock() - start) * TEST_NS_PER_SECOND / CLOCKS_PER_SEC;
		printf("Adaptive %s: %" LEGACY_PRIu32 " frames, %.3f CPU ns/frame\n", names[mode],
			   (legacy_u32)frames, cpu_ns / frames);
	}
	printf("Adaptive benchmark checksum: %" LEGACY_PRIu32 "\n", (legacy_u32)consumed);
}

legacy_int main(legacy_int argc, legacy_char **argv)
{
	test_cardinal_masks_and_stage_scales();
	test_diagonal_masks();
	test_cache_full_quality_and_bounds();
	test_footprint_flags_and_floor();
	test_sustained_load_and_hysteresis();
	test_uneven_load_and_interrupted_recovery();
	test_recovery_backoff_and_bounds();
	test_locked_presets_and_restart();
	puts("Adaptive frame quality controller and rotated mask tests passed.");
	if (argc == 2 && strcmp(argv[1], "--benchmark") == 0) {
		benchmark_policy();
	}
	return 0;
}
