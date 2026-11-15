#include "extended_barrel_search.h"
#include <float.h>
#include <math.h>
#include <string.h>
#include "../game_input.h"
#define BARREL_REPAIR_AIR_PEDALS 4U
#define BARREL_REPAIR_MAX_CHANGES 3U
#define BARREL_REPAIR_LATERAL_LIMIT 0.8
#define BARREL_REPAIR_SPEED_MINIMUM 0.85
#define BARREL_REPAIR_SPEED_MAXIMUM 1.5
#define BARREL_REPAIR_SHIFT_MINIMUM 0.65
#define BARREL_REPAIR_SHIFT_MAXIMUM 1.05

void extended_barrel_search_init(struct EXTENDED_BARREL_SEARCH *search, legacy_u32 seed)
{
	memset(search, 0, sizeof(*search));
	search->random = seed ? seed : 1U;
}
void extended_barrel_search_clear(struct EXTENDED_BARREL_SEARCH *search)
{
	search->count = 0U;
	search->attempts = 0U;
}
void extended_barrel_search_invalidate(struct EXTENDED_BARREL_SEARCH *search)
{
	for (legacy_uint index = 0U; index < search->count; index++) {
		search->fitness[index] = -DBL_MAX;
	}
}
static legacy_u32 barrel_random(struct EXTENDED_BARREL_SEARCH *repair)
{
	legacy_u32 value = repair->random;
	value ^= value << 13U;
	value ^= value >> 17U;
	value ^= value << 5U;
	return repair->random = value;
}

static legacy_f64 barrel_clamp(legacy_f64 value, legacy_f64 minimum, legacy_f64 maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

static legacy_f64 barrel_signed(struct EXTENDED_BARREL_SEARCH *repair)
{
	return (legacy_f64)(barrel_random(repair) & 0xffffU) / 32767.5 - 1.0;
}

static legacy_uint barrel_field(legacy_u32 random, legacy_u16 enabled)
{
	legacy_uint count = 0U;
	for (legacy_uint field = 0U; field < EXTENDED_BARREL_FIELD_COUNT; field++) {
		count += (enabled >> field) & 1U;
	}
	legacy_uint selected = random % count;
	for (legacy_uint field = 0U; field < EXTENDED_BARREL_FIELD_COUNT; field++) {
		if (enabled & (1U << field)) {
			if (!selected) {
				return field;
			}
			selected--;
		}
	}
	return EXTENDED_BARREL_SIDE_PEDAL;
}
static void barrel_fixed_fields(struct EXTENDED_BARREL_PARAMETERS *result, legacy_u16 enabled,
								const struct EXTENDED_BARREL_PARAMETERS *baseline)
{
	const struct EXTENDED_BARREL_PARAMETERS zero = {0};
	if (!baseline) {
		baseline = &zero;
	}
	if (!(enabled & (1U << EXTENDED_BARREL_SPEED))) {
		result->speed = baseline->speed;
	}
	if (!(enabled & (1U << EXTENDED_BARREL_LATERAL))) {
		result->lateral = baseline->lateral;
	}
	if (!(enabled & (1U << EXTENDED_BARREL_SHIFT))) {
		result->shift = baseline->shift;
	}
	if (!(enabled & (1U << EXTENDED_BARREL_BEGIN))) {
		result->begin = baseline->begin;
	}
	if (!(enabled & (1U << EXTENDED_BARREL_DURATION))) {
		result->duration = baseline->duration;
	}
	if (!(enabled & (1U << EXTENDED_BARREL_GAP))) {
		result->gap = baseline->gap;
	}
	if (!(enabled & (1U << EXTENDED_BARREL_COUNTER))) {
		result->counter = baseline->counter;
	}
	if (!(enabled & (1U << EXTENDED_BARREL_PEDAL_BEGIN))) {
		result->pedal_begin = baseline->pedal_begin;
	}
	if (!(enabled & (1U << EXTENDED_BARREL_PEDAL_DURATION))) {
		result->pedal_duration = baseline->pedal_duration;
	}
	if (!(enabled & (1U << EXTENDED_BARREL_CHECKPOINT))) {
		result->checkpoint = baseline->checkpoint;
	}
	if (!(enabled & (1U << EXTENDED_BARREL_AIR_PEDAL))) {
		result->air_brake = baseline->air_brake;
	}
	if (!(enabled & (1U << EXTENDED_BARREL_SIDE_PEDAL))) {
		result->side = baseline->side;
	}
	if (!(enabled & (1U << EXTENDED_BARREL_SIDE_PEDAL))) {
		result->pedal = baseline->pedal;
	}
}
struct EXTENDED_BARREL_PARAMETERS
extended_barrel_search_next(struct EXTENDED_BARREL_SEARCH *repair, legacy_uint span,
							legacy_uint bank, legacy_u16 enabled_fields,
							const struct EXTENDED_BARREL_PARAMETERS *baseline)
{
	struct EXTENDED_BARREL_PARAMETERS result = {0};
	enabled_fields &= EXTENDED_BARREL_FIELDS_ALL;
	if (!enabled_fields) {
		barrel_fixed_fields(&result, enabled_fields, baseline);
		repair->attempts++;
		return result;
	}
	if (!span) {
		span = 1U;
	}
	/* Fund mirrored ordinary steering/countersteering patterns across launch
	 * speeds before local refinement. Timings are fractions of the measured
	 * native entry-to-lip traversal, rather than track or replay frame numbers. */
	if (!repair->count || repair->attempts % 8U == 0U) {
		legacy_u64 ordinal = repair->attempts / 8U;
		result.checkpoint = (legacy_uint)(ordinal % EXTENDED_BARREL_CHECKPOINTS);
		result.speed = 1.0 + (legacy_f64)((ordinal / EXTENDED_BARREL_CHECKPOINTS) % 5U) * 0.1;
		result.side = (legacy_u8)((ordinal / 2U) % 2U);
		result.lateral = result.side ? 0.125 : -0.125;
		result.shift = ordinal % 2U ? 0.9 : 1.0;
		result.duration = EXTENDED_BARREL_PATTERN_UNITS / 3U;
		legacy_uint bank_units =
			(legacy_uint)((legacy_u64)bank * EXTENDED_BARREL_PATTERN_UNITS / span);
		result.begin = bank_units > result.duration / 2U ? bank_units - result.duration / 2U : 0U;
		result.gap = EXTENDED_BARREL_PATTERN_UNITS / 16U;
		result.counter = EXTENDED_BARREL_PATTERN_UNITS / 4U;
		result.pedal_duration = EXTENDED_BARREL_PATTERN_UNITS / 32U;
		result.pedal_begin =
			bank_units > result.pedal_duration ? bank_units - result.pedal_duration : 0U;
		result.pedal = 1U;
		result.air_brake = (legacy_u8)((ordinal / 5U) % BARREL_REPAIR_AIR_PEDALS);
		if (ordinal % 8U == 0U && ordinal) {
			result.begin = barrel_random(repair) % EXTENDED_BARREL_PATTERN_UNITS;
			result.duration = 4U + barrel_random(repair) % (EXTENDED_BARREL_PATTERN_UNITS / 2U);
			result.counter = 4U + barrel_random(repair) % (EXTENDED_BARREL_PATTERN_UNITS / 2U);
			result.pedal_begin = barrel_random(repair) % EXTENDED_BARREL_PATTERN_UNITS;
		}
		goto ready;
	}
	legacy_uint selected = barrel_random(repair) % repair->count;
	legacy_uint other = barrel_random(repair) % repair->count;
	if (repair->fitness[other] > repair->fitness[selected]) {
		selected = other;
	}
	result = repair->population[selected];
	legacy_uint changes = 1U + barrel_random(repair) % BARREL_REPAIR_MAX_CHANGES;
	for (legacy_uint change = 0U; change < changes; ++change) {
		legacy_uint field = barrel_field(barrel_random(repair), enabled_fields);
		legacy_f64 delta = barrel_signed(repair);
		switch (field) {
			case 0U:
				result.speed =
					barrel_clamp(result.speed + delta * 0.04, BARREL_REPAIR_SPEED_MINIMUM,
								 BARREL_REPAIR_SPEED_MAXIMUM);
				break;
			case 1U:
				result.lateral =
					barrel_clamp(result.lateral + delta * 0.08, -BARREL_REPAIR_LATERAL_LIMIT,
								 BARREL_REPAIR_LATERAL_LIMIT);
				break;
			case 2U:
				result.shift =
					barrel_clamp(result.shift + delta * 0.04, BARREL_REPAIR_SHIFT_MINIMUM,
								 BARREL_REPAIR_SHIFT_MAXIMUM);
				break;
			case 3U:
				result.begin = (legacy_uint)barrel_clamp(result.begin + delta * 6.0, 0.0,
														 EXTENDED_BARREL_PATTERN_UNITS);
				break;
			case 4U:
				result.duration = (legacy_uint)barrel_clamp(result.duration + delta * 6.0, 1.0,
															EXTENDED_BARREL_PATTERN_UNITS / 2U);
				break;
			case 5U:
				result.gap = (legacy_uint)barrel_clamp(result.gap + delta * 4.0, 0.0,
													   EXTENDED_BARREL_PATTERN_UNITS / 3U);
				break;
			case 6U:
				result.counter = (legacy_uint)barrel_clamp(result.counter + delta * 6.0, 1.0,
														   EXTENDED_BARREL_PATTERN_UNITS / 2U);
				break;
			case 7U:
				result.pedal_begin = (legacy_uint)barrel_clamp(
					result.pedal_begin + delta * 6.0, 0.0, EXTENDED_BARREL_PATTERN_UNITS * 2U);
				break;
			case 8U:
				result.pedal_duration = (legacy_uint)barrel_clamp(
					result.pedal_duration + delta * 3.0, 0.0, EXTENDED_BARREL_PATTERN_UNITS / 2U);
				break;
			case 9U:
				result.checkpoint = barrel_random(repair) % EXTENDED_BARREL_CHECKPOINTS;
				break;
			case 10U:
				result.air_brake = (legacy_u8)(barrel_random(repair) % BARREL_REPAIR_AIR_PEDALS);
				break;
			default:
				if (barrel_random(repair) % 4U == 0U) {
					result.side = !result.side;
				} else {
					result.pedal = barrel_random(repair) % 3U;
				}
				break;
		}
	}
ready:
	barrel_fixed_fields(&result, enabled_fields, baseline);
	repair->attempts++;
	return result;
}

static legacy_int barrel_same_parameters(const struct EXTENDED_BARREL_PARAMETERS *first,
										 const struct EXTENDED_BARREL_PARAMETERS *second)
{
	return first->speed == second->speed && first->lateral == second->lateral &&
		   first->shift == second->shift && first->checkpoint == second->checkpoint &&
		   first->begin == second->begin && first->duration == second->duration &&
		   first->gap == second->gap && first->counter == second->counter &&
		   first->pedal_begin == second->pedal_begin &&
		   first->pedal_duration == second->pedal_duration && first->side == second->side &&
		   first->pedal == second->pedal && first->air_brake == second->air_brake;
}

void extended_barrel_search_observe(struct EXTENDED_BARREL_SEARCH *repair,
									const struct EXTENDED_BARREL_PARAMETERS *parameters,
									legacy_f64 fitness)
{
	if (!isfinite(fitness)) {
		return;
	}
	legacy_uint slot = repair->count;
	for (legacy_uint index = 0U; index < repair->count; ++index) {
		if (barrel_same_parameters(&repair->population[index], parameters)) {
			if (fitness > repair->fitness[index]) {
				repair->fitness[index] = fitness;
			}
			return;
		}
	}
	if (slot == EXTENDED_BARREL_POPULATION) {
		slot = 0U;
		for (legacy_uint index = 1U; index < repair->count; ++index) {
			if (repair->fitness[index] < repair->fitness[slot]) {
				slot = index;
			}
		}
		if (fitness <= repair->fitness[slot]) {
			return;
		}
	} else {
		++repair->count;
	}
	repair->population[slot] = *parameters;
	repair->fitness[slot] = fitness;
}

struct EXTENDED_BARREL_INPUT
extended_barrel_search_input(const struct EXTENDED_BARREL_PARAMETERS *parameters, legacy_uint span,
							 legacy_uint tick)
{
	struct EXTENDED_BARREL_INPUT result = {0};
	/* Wide arithmetic keeps even a very long measured span bounded. Zero span
	 * never creates a control window. No simulated or engine state is touched. */
	legacy_u64 begin = (legacy_u64)parameters->begin * span / EXTENDED_BARREL_PATTERN_UNITS;
	legacy_u64 duration = (legacy_u64)parameters->duration * span / EXTENDED_BARREL_PATTERN_UNITS;
	legacy_u64 counter_begin =
		begin + duration + (legacy_u64)parameters->gap * span / EXTENDED_BARREL_PATTERN_UNITS;
	legacy_u64 counter_end =
		counter_begin + (legacy_u64)parameters->counter * span / EXTENDED_BARREL_PATTERN_UNITS;
	legacy_u64 pedal_begin =
		(legacy_u64)parameters->pedal_begin * span / EXTENDED_BARREL_PATTERN_UNITS;
	legacy_u64 pedal_end =
		pedal_begin + (legacy_u64)parameters->pedal_duration * span / EXTENDED_BARREL_PATTERN_UNITS;
	legacy_u8 steering = parameters->side ? INPUT_STEER_RIGHT_FLAG : INPUT_STEER_LEFT_FLAG;
	if (tick >= begin && tick < begin + duration) {
		result.mask |= INPUT_STEERING_MASK;
		result.value |= steering;
	} else if (tick >= counter_begin && tick < counter_end) {
		result.mask |= INPUT_STEERING_MASK;
		result.value |= parameters->side ? INPUT_STEER_LEFT_FLAG : INPUT_STEER_RIGHT_FLAG;
	}
	if (tick >= pedal_begin && tick < pedal_end) {
		result.mask |= INPUT_PEDAL_MASK;
		if (parameters->pedal == 1U) {
			result.value |= INPUT_BRAKE_FLAG;
		} else if (parameters->pedal == 2U) {
			result.value |= INPUT_ACCELERATE_FLAG;
		}
	}
	return result;
}
