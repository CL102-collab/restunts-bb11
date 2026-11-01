#include <assert.h>
#include <stddef.h>

#ifndef far
#define far
#endif

#include "../c/math/math.h"

static legacy_s16 reference_matrix_product(legacy_s16 left, legacy_s16 right)
{
	legacy_s32 product = (legacy_s32)left * (legacy_s32)right;
	legacy_u32 bits = (legacy_u32)product << MATH_PRODUCT_SCALE_SHIFT;
	return LEGACY_S16_FROM_BITS((legacy_u16)(bits >> LEGACY_WORD_BITS));
}

static legacy_s16 reference_multiply_and_scale(legacy_s16 left, legacy_s16 right)
{
	legacy_s32 product = (legacy_s32)left * (legacy_s32)right;
	legacy_u32 bits = (legacy_u32)product << MATH_PRODUCT_SCALE_SHIFT;
	legacy_u16 result = (legacy_u16)(bits >> LEGACY_WORD_BITS);
	result = (legacy_u16)(result + ((bits & LEGACY_U16_SIGN_BIT) >> (LEGACY_WORD_BITS - 1U)));
	return LEGACY_S16_FROM_BITS(result);
}

static void clear_matrix(struct MATRIX *matrix)
{
	for (legacy_u16 index = 0U; index < MATRIX_ELEMENT_COUNT; index++) {
		matrix->vals[index] = 0;
	}
}

static void test_matrix_products(void)
{
	struct VECTOR output;
	static const legacy_s16 values[] = {-32768, -32767, -20000, -16384, -1,
										0,		1,		16384,	20000,	32767};
	struct MATRIX matrix;
	struct VECTOR input;
	for (size_t left_index = 0U; left_index < sizeof(values) / sizeof(values[0]); left_index++) {
		for (size_t right_index = 0U; right_index < sizeof(values) / sizeof(values[0]);
			 right_index++) {
			clear_matrix(&matrix);
			matrix.m._11 = values[left_index];
			input.x = values[right_index];
			input.y = 0;
			input.z = 0;
			mat_mul_vector(&input, &matrix, &output);
			legacy_s16 expected = reference_matrix_product(values[left_index], values[right_index]);
			assert(output.x == expected);
			assert(output.y == 0);
			assert(output.z == 0);
			assert(multiply_and_scale(values[left_index], values[right_index]) ==
				   reference_multiply_and_scale(values[left_index], values[right_index]));
		}
	}
}

static void test_matrix_sum_wrap(void)
{
	struct MATRIX matrix;
	clear_matrix(&matrix);
	matrix.m._11 = 32767;
	matrix.m._12 = 32767;
	matrix.m._13 = 32767;
	struct VECTOR input;
	input.x = 32767;
	input.y = 32767;
	input.z = 32767;
	struct VECTOR output;
	mat_mul_vector(&input, &matrix, &output);

	legacy_s16 expected = reference_matrix_product(matrix.m._11, input.x);
	expected = LEGACY_S16_WRAP_ADD(expected, reference_matrix_product(matrix.m._12, input.y));
	expected = LEGACY_S16_WRAP_ADD(expected, reference_matrix_product(matrix.m._13, input.z));
	assert(output.x == expected);
}

int main(void)
{
	test_matrix_products();
	test_matrix_sum_wrap();
	return 0;
}
