#ifndef RESTUNTS_DJGPP_MATH_H
#define RESTUNTS_DJGPP_MATH_H

#include <math.h>
#include "../../c/legacy.h"

/* Handle NaNs and select the appropriate sign for mixed signed zeros. */
static inline legacy_f64 restunts_djgpp_fmin(legacy_f64 left, legacy_f64 right)
{
	if (__builtin_isnan(left)) {
		return right;
	}
	return __builtin_isnan(right) || left < right || (left == right && __builtin_signbit(left))
			   ? left
			   : right;
}

static inline legacy_f64 restunts_djgpp_fmax(legacy_f64 left, legacy_f64 right)
{
	if (__builtin_isnan(left)) {
		return right;
	}
	return __builtin_isnan(right) || left > right || (left == right && !__builtin_signbit(left))
			   ? left
			   : right;
}

#define fmin restunts_djgpp_fmin
#define fmax restunts_djgpp_fmax

#endif
