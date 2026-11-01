#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../c/externs.h"
#include "../c/math/math.h"
#include "../c/fatal.h"

#undef printf

extern void rectlist_add_rect(legacy_s8 *rectangle_count, struct RECTANGLE *rectangles,
							  struct RECTANGLE *rect);

static legacy_u64 trace_hash = UINT64_C(1469598103934665603);

void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	assert(0);
}

static void trace_word(legacy_u16 value)
{
	trace_hash = (trace_hash ^ (value & 255U)) * UINT64_C(1099511628211);
	trace_hash = (trace_hash ^ (value >> 8)) * UINT64_C(1099511628211);
}

static void test_polar_boundaries(void)
{
	assert(polarAngle(0, 0) == 0);
	assert(polarAngle(1, 0) == ANGLE_QUARTER_TURN);
	assert(polarAngle(0, -1) == ANGLE_HALF_TURN);
	assert(polarAngle(-1, 0) == -ANGLE_QUARTER_TURN);
	assert(polarAngle(-32768, -32768) == -384);
	static const legacy_s16 coordinates[] = {-32767, -1025, -1024, -129, -128, -1,	 0,
											 1,		 128,	129,   1024, 1025, 32767};
	for (legacy_u32 x = 0; x < sizeof(coordinates) / sizeof(coordinates[0]); x++) {
		for (legacy_u32 y = 0; y < sizeof(coordinates) / sizeof(coordinates[0]); y++) {
			trace_word(polarAngle(coordinates[x], coordinates[y]));
		}
	}
}

static void test_polar_minimum_components(void)
{
	const legacy_s16 minimum = LEGACY_S16_FROM_BITS(LEGACY_U16_SIGN_BIT);
	const legacy_s16 maximum = (legacy_s16)LEGACY_S16_MAX;
	assert(polarAngle(minimum, 0) == -ANGLE_QUARTER_TURN);
	assert(polarAngle(0, minimum) == ANGLE_HALF_TURN);
	assert(polarAngle(minimum, minimum) == -(ANGLE_QUARTER_TURN + ANGLE_EIGHTH_TURN));
	assert(polarAngle(minimum, LEGACY_S16_MAX) == -ANGLE_EIGHTH_TURN);
	assert(polarAngle(LEGACY_S16_MAX, minimum) == ANGLE_QUARTER_TURN + ANGLE_EIGHTH_TURN);
	legacy_s16 previous_first = -(ANGLE_QUARTER_TURN + ANGLE_EIGHTH_TURN);
	legacy_u16 previous_second = ANGLE_HALF_TURN + ANGLE_EIGHTH_TURN;
	/* Every signed word paired with the minimum must stay in its geometric
	 * quadrant and turn monotonically. Even pairs also have an exact, safe
	 * half-scale direction that never contains the signed minimum. */
	for (legacy_s32 coordinate = minimum; coordinate <= maximum; coordinate++) {
		legacy_s16 first = polarAngle(minimum, (legacy_s16)coordinate);
		legacy_u16 second = (legacy_u16)polarAngle((legacy_s16)coordinate, minimum) & ANGLE_MASK;
		assert(first >= -(ANGLE_QUARTER_TURN + ANGLE_EIGHTH_TURN));
		assert(first <= -ANGLE_EIGHTH_TURN && first >= previous_first);
		assert(second >= ANGLE_QUARTER_TURN + ANGLE_EIGHTH_TURN);
		assert(second <= ANGLE_HALF_TURN + ANGLE_EIGHTH_TURN && second <= previous_second);
		if (coordinate % 2 == 0) {
			assert(first == polarAngle(minimum / 2, (legacy_s16)(coordinate / 2)));
			assert(second == ((legacy_u16)polarAngle((legacy_s16)(coordinate / 2), minimum / 2) &
							  ANGLE_MASK));
		}
		previous_first = first;
		previous_second = second;
	}
}

static void test_rectangle_splits(void)
{
	video_x_alignment = 1;
	legacy_s8 count;
	static const struct RECTANGLE inputs[] = {{0, 10, 0, 10},  {2, 8, 2, 8},	{-2, 12, -2, 12},
											  {2, 8, -4, 14},  {-4, 14, 2, 8},	{5, 15, -5, 5},
											  {5, 15, 5, 15},  {0, 10, 10, 20}, {10, 20, 0, 10},
											  {-10, 0, 0, 10}, {0, 10, -10, 0}, {30, 40, 30, 40}};
	struct RECTANGLE rectangles[64];
	struct RECTANGLE next;
	for (legacy_u32 first = 0; first < 12U; first++) {
		for (legacy_u32 second = 0; second < 12U; second++) {
			for (legacy_u32 third = 0; third < 12U; third++) {
				count = 0;
				next = inputs[first];
				rectlist_add_rect(&count, rectangles, &next);
				next = inputs[second];
				rectlist_add_rect(&count, rectangles, &next);
				next = inputs[third];
				rectlist_add_rect(&count, rectangles, &next);
				trace_word(count);
				for (legacy_u32 index = 0; index < (legacy_u32)count; index++) {
					trace_word(rectangles[index].left);
					trace_word(rectangles[index].right);
					trace_word(rectangles[index].top);
					trace_word(rectangles[index].bottom);
				}
			}
		}
	}
}

int main(void)
{
	test_polar_boundaries();
	test_polar_minimum_components();
	test_rectangle_splits();
	/* Captured before extraction: quadrant boundaries and 1,728 ordered rectangle triples. */
	assert(trace_hash == UINT64_C(0x998e7d0d29ad404a));
	puts("Polar boundaries and rectangle splitting snapshots passed.");
	return 0;
}
