#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Keep the real Amiga arithmetic and discard unrelated platform code at link. */
#include "../c/ghost.c"

#undef printf
#undef memcpy
#undef memset

static void test_scalar_reference(void)
{
	const ghost_line_scalar scale = GHOST_LINE_ONE;
	const ghost_line_scalar factors[] = {
		-scale * 131070 - scale / 3, -scale - 1, -scale / 3, 0, scale / 3, scale + 1,
		scale * 131070 + scale / 3,
	};
	for (legacy_uint i = 0; i < sizeof(factors) / sizeof(*factors); i++) {
		for (legacy_uint j = 0; j < sizeof(factors) / sizeof(*factors); j++) {
			legacy_fext expected = truncl((legacy_fext)factors[i] * factors[j] / scale);
			assert(fabsl(ghost_line_multiply(factors[i], factors[j]) - expected) <= 1);
		}
	}
	/* Every production divisor is a positive length. Large values exercise
	 * normalization of cross products from extreme signed 16-bit wheel data. */
	const ghost_line_scalar divisors[] = {1, 2, scale - 1, scale, scale + 1, (1LL << 59) - 1};
	for (legacy_uint i = 0; i < sizeof(divisors) / sizeof(*divisors); i++) {
		ghost_line_scalar denominator = divisors[i];
		ghost_line_scalar numerators[] = {0, denominator - 1, denominator, denominator + 1,
										  2 * denominator + 1};
		for (legacy_uint j = 0; j < sizeof(numerators) / sizeof(*numerators); j++) {
			for (legacy_int sign = -1; sign <= 1; sign += 2) {
				ghost_line_scalar numerator = sign * numerators[j];
				legacy_fext expected = truncl((legacy_fext)numerator * scale / denominator);
				assert(fabsl(ghost_line_divide(numerator, denominator) - expected) <= 1);
			}
		}
	}
}

static legacy_fext vector_length(struct GHOST_LINE_VECTOR vector)
{
	return sqrtl((legacy_fext)vector.x * vector.x + (legacy_fext)vector.y * vector.y +
				 (legacy_fext)vector.z * vector.z);
}

static void test_length_reference(void)
{
	const ghost_line_scalar large = GHOST_LINE_VALUE(2LL * 131070 * 131070);
	const struct GHOST_LINE_VECTOR vectors[] = {
		{0, 0, 0},
		{GHOST_LINE_ONE, 0, 0},
		{GHOST_LINE_ONE, GHOST_LINE_ONE, GHOST_LINE_ONE},
		{large, -large, large},
		{-large, 1, GHOST_LINE_ONE},
	};
	for (legacy_uint i = 0; i < sizeof(vectors) / sizeof(*vectors); i++) {
		legacy_fext expected = vector_length(vectors[i]);
		legacy_fext tolerance = expected * 0.0000002L + 1;
		assert(fabsl(ghost_line_length(vectors[i]) - expected) <= tolerance);
	}
	struct GHOST_LINE_VECTOR zero = {0};
	assert(!ghost_line_normalize(&zero));
}

static void test_extreme_wheel_contacts(void)
{
	/* All signs of all twelve wheel coordinates, including degenerate poses. */
	for (legacy_uint signs = 0; signs < (1U << 12); signs++) {
		struct CARSTATE car = {0};
		for (legacy_uint wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
			struct VECTOR *contact = &car.car_wheel_contact_positions[wheel];
			contact->x = signs & (1U << (wheel * 3)) ? 32767 : -32768;
			contact->y = signs & (2U << (wheel * 3)) ? 32767 : -32768;
			contact->z = signs & (4U << (wheel * 3)) ? 32767 : -32768;
		}
		struct GHOST_LINE_POSE pose = ghost_line_pose(&car);
		assert(fabsl(vector_length(pose.right) / GHOST_LINE_ONE - 1) < 0.000001L);
		assert(fabsl(vector_length(pose.up) / GHOST_LINE_ONE - 1) < 0.000001L);
		legacy_fext dot = (legacy_fext)pose.right.x * pose.up.x +
						  (legacy_fext)pose.right.y * pose.up.y +
						  (legacy_fext)pose.right.z * pose.up.z;
		assert(fabsl(dot / GHOST_LINE_ONE / GHOST_LINE_ONE) < 0.000001L);
		struct VECTORLONG edge = ghost_line_edge(&pose, GHOST_LINE_HALF_WIDTH);
		assert(fabsl(edge.lx - (legacy_fext)pose.center.x / GHOST_LINE_ONE) <= 257);
		assert(fabsl(edge.ly - (legacy_fext)pose.center.y / GHOST_LINE_ONE) <= 257);
		assert(fabsl(edge.lz - (legacy_fext)pose.center.z / GHOST_LINE_ONE) <= 257);
	}
}

static void test_coordinate_units_and_rounding(void)
{
	const struct VECTOR wheels[] = {{-20, 0, 40}, {20, 0, 40}, {20, 0, -40}, {-20, 0, -40}};
	/* The first shifted pose lies near the far side of the 30-tile map. */
	const struct VECTOR translations[] = {{30000, 1000, 20000}, {-30000, -1000, -20000}};
	for (legacy_uint index = 0; index < 2; index++) {
		struct CARSTATE car = {0};
		for (legacy_uint wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
			car.car_wheel_contact_positions[wheel].x = wheels[wheel].x + translations[index].x;
			car.car_wheel_contact_positions[wheel].y = wheels[wheel].y + translations[index].y;
			car.car_wheel_contact_positions[wheel].z = wheels[wheel].z + translations[index].z;
		}
		struct GHOST_LINE_POSE pose = ghost_line_pose(&car);
		struct VECTORLONG left = ghost_line_edge(&pose, -GHOST_LINE_HALF_WIDTH);
		struct VECTORLONG right = ghost_line_edge(&pose, GHOST_LINE_HALF_WIDTH);
		assert(left.lx == translations[index].x * 64L - 128);
		assert(right.lx == translations[index].x * 64L + 128);
		assert(left.ly == translations[index].y * 64L + 128 && right.ly == left.ly);
		assert(left.lz == translations[index].z * 64L && right.lz == left.lz);
	}
	assert(ghost_line_round(GHOST_LINE_ONE / 2 - 1) == 0);
	assert(ghost_line_round(GHOST_LINE_ONE / 2) == 1);
	assert(ghost_line_round(-GHOST_LINE_ONE / 2 + 1) == 0);
	assert(ghost_line_round(-GHOST_LINE_ONE / 2) == -1);
	assert(ghost_line_round(-5 * GHOST_LINE_ONE / 2) == -3);
}

legacy_int main(void)
{
	test_scalar_reference();
	test_length_reference();
	test_extreme_wheel_contacts();
	test_coordinate_units_and_rounding();
	puts("Amiga ghost racing line arithmetic passed.");
	return 0;
}
