/* Modern 32-bit GCC can combine signed division and remainder into this
 * helper. Keep the pinned compiler runtime's existing arithmetic semantics. */
#include <stddef.h>
#include "../../src/restunts/c/legacy.h"

extern legacy_s64 __divdi3(legacy_s64 dividend, legacy_s64 divisor);
extern legacy_s64 __moddi3(legacy_s64 dividend, legacy_s64 divisor);

legacy_s64 __divmoddi4(legacy_s64 dividend, legacy_s64 divisor, legacy_s64 *remainder)
{
	legacy_s64 quotient = __divdi3(dividend, divisor);
	if (remainder != NULL) {
		*remainder = __moddi3(dividend, divisor);
	}
	return quotient;
}
