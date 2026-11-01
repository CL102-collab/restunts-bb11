#include "legacy.h"
#include "math/math.h"

#define HEAPSORT_GAP_DIVISOR 2

void heapsort_by_order(legacy_s16 count, legacy_s16 *values, legacy_s16 *order)
{
	legacy_s16 gap = LEGACY_S16_DIV_OR_ZERO(count, HEAPSORT_GAP_DIVISOR);
	while (gap > 0) {
		legacy_s16 counter = gap;
		while (counter < count) {
			legacy_s16 index = counter - gap;
			while (index >= 0 && values[index + gap] > values[index]) {
				legacy_s16 temp = values[index];
				values[index] = values[index + gap];
				values[index + gap] = temp;

				temp = order[index];
				order[index] = order[index + gap];
				order[index + gap] = temp;
				index -= gap;
			}
			counter++;
		}
		gap = LEGACY_S16_DIV_OR_ZERO(gap, HEAPSORT_GAP_DIVISOR);
	}
}
