/* Run with each generated AppImage SDK's pinned C++ headers/static archive. */
#include <cstdio>
#include <limits>
#include <new>
#include <vector>
#include "../../src/restunts/c/legacy.h"

#define RUNTIME_TEST_SUCCESS 0
#define RUNTIME_TEST_FAILURE 1
#define RUNTIME_VECTOR_LENGTH 4U
#define RUNTIME_VECTOR_VALUE 7U

using ArraySize = decltype(sizeof(legacy_u32));

#if defined(__i386__)
extern "C" legacy_s64 __divmoddi4(legacy_s64 dividend, legacy_s64 divisor, legacy_s64 *remainder);

static legacy_int check_signed_division()
{
	struct DivisionCase {
		legacy_s64 dividend;
		legacy_s64 divisor;
		legacy_s64 quotient;
		legacy_s64 remainder;
	};
	const DivisionCase cases[] = {
		{17, 5, 3, 2},
		{-17, 5, -3, -2},
		{17, -5, -3, 2},
		{-17, -5, 3, -2},
		{0x123456789LL, 0x100000000LL, 1, 0x23456789LL},
		{-0x123456789LL, 0x100000000LL, -1, -0x23456789LL},
	};
	for (const DivisionCase &item : cases) {
		legacy_s64 remainder;
		if (__divmoddi4(item.dividend, item.divisor, &remainder) != item.quotient ||
			remainder != item.remainder ||
			__divmoddi4(item.dividend, item.divisor, nullptr) != item.quotient) {
			return RUNTIME_TEST_FAILURE;
		}
	}
	return RUNTIME_TEST_SUCCESS;
}
#endif

legacy_int main()
{
#if defined(__i386__)
	if (check_signed_division() != RUNTIME_TEST_SUCCESS) {
		std::fputs("Signed quotient/remainder regression failed\n", stderr);
		return RUNTIME_TEST_FAILURE;
	}
	std::puts("Signed quotient/remainder sign, wide operand and null output cases passed");
#endif
	std::vector<legacy_u32> values(RUNTIME_VECTOR_LENGTH, RUNTIME_VECTOR_VALUE);
	values.push_back(RUNTIME_VECTOR_VALUE);
	for (legacy_u32 value : values) {
		if (value != RUNTIME_VECTOR_VALUE) {
			return RUNTIME_TEST_FAILURE;
		}
	}
	legacy_u32 *normal = new legacy_u32[RUNTIME_VECTOR_LENGTH];
	*normal = RUNTIME_VECTOR_VALUE;
	delete[] normal;
	volatile ArraySize invalid_count = std::numeric_limits<ArraySize>::max();
	try {
		legacy_u32 *volatile invalid = new legacy_u32[invalid_count];
		delete[] invalid;
	} catch (const std::bad_array_new_length &error) {
		const std::bad_alloc &base = error;
		if (base.what() == nullptr || *base.what() == '\0') {
			return RUNTIME_TEST_FAILURE;
		}
		std::puts("C++11 invalid array count and normal vector/allocation passed");
		return RUNTIME_TEST_SUCCESS;
	} catch (...) {
		std::fputs("Invalid array count threw the wrong exception type\n", stderr);
		return RUNTIME_TEST_FAILURE;
	}
	std::fputs("Invalid array count did not throw\n", stderr);
	return RUNTIME_TEST_FAILURE;
}
