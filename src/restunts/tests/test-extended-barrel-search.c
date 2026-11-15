#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "../c/extended/extended_barrel_search.h"
#include "../c/game_input.h"

#define TEST_SEED UINT32_C(0x12345678)
#define TEST_GOLDEN_ATTEMPTS 1024U
#define TEST_MEASURED_SPAN 61U
#define TEST_MEASURED_BANK 31U
#define TEST_HASH_INITIAL UINT64_C(1469598103934665603)
#define TEST_HASH_MULTIPLIER UINT64_C(1099511628211)
#define TEST_GOLDEN_HASH UINT64_C(10910418344216323971)
#define TEST_GOLDEN_RANDOM UINT32_C(1419965855)
#define TEST_HASH_BYTES 8U
#define TEST_BYTE_BITS 8U
#define TEST_SCORE_SPEED_WEIGHT 1000.0
#define TEST_SCORE_LATERAL_WEIGHT 30.0
#define TEST_SCORE_PEDAL_WEIGHT 2.0
#define TEST_SCORE_VARIATION 13U
#define TEST_DUPLICATE_INDEX 7U
#define TEST_IMPROVED_FITNESS 100.0
#define TEST_REVALIDATED_FITNESS (-100.0)
#define TEST_NEW_FITNESS (-200.0)
#define TEST_REPLACEMENT_KEY EXTENDED_BARREL_PATTERN_UNITS
#define TEST_SPEED 1.125
#define TEST_LATERAL (-0.375)
#define TEST_SHIFT 0.875
#define TEST_CHECKPOINT 3U
#define TEST_AIR_PEDAL 2U

struct TEST_WINDOW {
	legacy_uint tick;
	legacy_u8 mask, value;
};

static legacy_u64 hash_word(legacy_u64 hash, legacy_u64 word)
{
	for (legacy_u32 byte = 0; byte < TEST_HASH_BYTES; byte++) {
		hash = (hash ^ (word & LEGACY_U8_MAX)) * TEST_HASH_MULTIPLIER;
		word >>= TEST_BYTE_BITS;
	}
	return hash;
}

static legacy_u64 hash_parameters(legacy_u64 hash, const struct EXTENDED_BARREL_PARAMETERS *p)
{
	const legacy_f64 values[] = {p->speed, p->lateral, p->shift};
	for (legacy_u32 index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
		legacy_u64 bits;
		assert(sizeof(bits) == sizeof(values[index]));
		memcpy(&bits, &values[index], sizeof(bits));
		hash = hash_word(hash, bits);
	}
	const legacy_u64 fields[] = {p->checkpoint, p->begin,		p->duration,	   p->gap,
								 p->counter,	p->pedal_begin, p->pedal_duration, p->side,
								 p->pedal,		p->air_brake};
	for (legacy_u32 index = 0; index < sizeof(fields) / sizeof(fields[0]); index++) {
		hash = hash_word(hash, fields[index]);
	}
	return hash;
}

static legacy_f64 score_parameters(const struct EXTENDED_BARREL_PARAMETERS *p, legacy_uint index)
{
	return p->speed * TEST_SCORE_SPEED_WEIGHT + p->lateral * TEST_SCORE_LATERAL_WEIGHT - p->begin -
		   p->duration + p->pedal_duration * TEST_SCORE_PEDAL_WEIGHT + index % TEST_SCORE_VARIATION;
}

static struct EXTENDED_BARREL_PARAMETERS window_parameters(void)
{
	/* On a64-tick span, steer[8,20), countersteer[24,34), pedal[16,28). */
	const struct EXTENDED_BARREL_PARAMETERS p = {1.0, 0.0, 1.0, 0U, 8U, 12U, 4U,
												 10U, 16U, 12U, 0U, 1U, 0U};
	return p;
}

static void assert_input(const struct EXTENDED_BARREL_PARAMETERS *p, legacy_uint span,
						 legacy_uint tick, legacy_u8 mask, legacy_u8 value)
{
	struct EXTENDED_BARREL_INPUT input = extended_barrel_search_input(p, span, tick);
	assert(input.mask == mask && input.value == value);
	assert((input.value & (legacy_u8)~input.mask) == 0);
}

static void check_windows(void)
{
	static const struct TEST_WINDOW windows[] = {
		{7U, INPUT_NONE, INPUT_NONE},
		{8U, INPUT_STEERING_MASK, INPUT_STEER_LEFT_FLAG},
		{15U, INPUT_STEERING_MASK, INPUT_STEER_LEFT_FLAG},
		{16U, INPUT_DRIVING_MASK, INPUT_STEER_LEFT_FLAG | INPUT_BRAKE_FLAG},
		{19U, INPUT_DRIVING_MASK, INPUT_STEER_LEFT_FLAG | INPUT_BRAKE_FLAG},
		{20U, INPUT_PEDAL_MASK, INPUT_BRAKE_FLAG},
		{23U, INPUT_PEDAL_MASK, INPUT_BRAKE_FLAG},
		{24U, INPUT_DRIVING_MASK, INPUT_STEER_RIGHT_FLAG | INPUT_BRAKE_FLAG},
		{27U, INPUT_DRIVING_MASK, INPUT_STEER_RIGHT_FLAG | INPUT_BRAKE_FLAG},
		{28U, INPUT_STEERING_MASK, INPUT_STEER_RIGHT_FLAG},
		{33U, INPUT_STEERING_MASK, INPUT_STEER_RIGHT_FLAG},
		{34U, INPUT_NONE, INPUT_NONE}};
	struct EXTENDED_BARREL_PARAMETERS p = window_parameters();
	for (legacy_u32 index = 0; index < sizeof(windows) / sizeof(windows[0]); index++) {
		const struct TEST_WINDOW *expected = &windows[index];
		assert_input(&p, EXTENDED_BARREL_PATTERN_UNITS, expected->tick, expected->mask,
					 expected->value);
		/* The same fractions scale with measured traversal duration. */
		assert_input(&p, EXTENDED_BARREL_PATTERN_UNITS * 2U, expected->tick * 2U, expected->mask,
					 expected->value);
		assert_input(&p, EXTENDED_BARREL_PATTERN_UNITS * 2U, expected->tick * 2U + 1U,
					 expected->mask, expected->value);
		assert_input(&p, 0U, expected->tick, INPUT_NONE, INPUT_NONE);
	}
	assert_input(&p, 0U, 0U, INPUT_NONE, INPUT_NONE);
	p.side = 1U;
	assert_input(&p, EXTENDED_BARREL_PATTERN_UNITS, 8U, INPUT_STEERING_MASK,
				 INPUT_STEER_RIGHT_FLAG);
	assert_input(&p, EXTENDED_BARREL_PATTERN_UNITS, 24U, INPUT_DRIVING_MASK,
				 INPUT_STEER_LEFT_FLAG | INPUT_BRAKE_FLAG);
	p.pedal = 0U;
	assert_input(&p, EXTENDED_BARREL_PATTERN_UNITS, 20U, INPUT_PEDAL_MASK, INPUT_NONE);
	assert_input(&p, EXTENDED_BARREL_PATTERN_UNITS, 24U, INPUT_DRIVING_MASK, INPUT_STEER_LEFT_FLAG);
	p.pedal = 2U;
	assert_input(&p, EXTENDED_BARREL_PATTERN_UNITS, 20U, INPUT_PEDAL_MASK, INPUT_ACCELERATE_FLAG);
	/* The fractional window[8,20) rounds down separately on an odd span. */
	p = window_parameters();
	assert_input(&p, TEST_MEASURED_SPAN, 6U, INPUT_NONE, INPUT_NONE);
	assert_input(&p, TEST_MEASURED_SPAN, 7U, INPUT_STEERING_MASK, INPUT_STEER_LEFT_FLAG);
	assert_input(&p, TEST_MEASURED_SPAN, 17U, INPUT_DRIVING_MASK,
				 INPUT_STEER_LEFT_FLAG | INPUT_BRAKE_FLAG);
	assert_input(&p, TEST_MEASURED_SPAN, 18U, INPUT_PEDAL_MASK, INPUT_BRAKE_FLAG);
	p.begin = p.duration = EXTENDED_BARREL_PATTERN_UNITS;
	p.counter = p.pedal_duration = 0U;
	assert_input(&p, LEGACY_U32_MAX, LEGACY_U32_MAX - 1U, INPUT_NONE, INPUT_NONE);
	assert_input(&p, LEGACY_U32_MAX, LEGACY_U32_MAX, INPUT_STEERING_MASK, INPUT_STEER_LEFT_FLAG);
}

static void check_golden_sequence(void)
{
	struct EXTENDED_BARREL_SEARCH search;
	extended_barrel_search_init(&search, TEST_SEED);
	legacy_u64 hash = TEST_HASH_INITIAL;
	/* Independent golden generated from the original ghost search before extraction. */
	for (legacy_uint index = 0; index < TEST_GOLDEN_ATTEMPTS; index++) {
		struct EXTENDED_BARREL_PARAMETERS p = extended_barrel_search_next(
			&search, TEST_MEASURED_SPAN, TEST_MEASURED_BANK, EXTENDED_BARREL_FIELDS_ALL, NULL);
		hash = hash_parameters(hash, &p);
		extended_barrel_search_observe(&search, &p, score_parameters(&p, index));
	}
	assert(hash == TEST_GOLDEN_HASH && search.random == TEST_GOLDEN_RANDOM);
	assert(search.count == EXTENDED_BARREL_POPULATION && search.attempts == TEST_GOLDEN_ATTEMPTS);
}

static void check_restricted_determinism(void)
{
	struct EXTENDED_BARREL_SEARCH first, second;
	struct EXTENDED_BARREL_PARAMETERS baseline = window_parameters();
	baseline.speed = TEST_SPEED;
	baseline.lateral = TEST_LATERAL;
	baseline.shift = TEST_SHIFT;
	baseline.checkpoint = TEST_CHECKPOINT;
	baseline.air_brake = TEST_AIR_PEDAL;
	extended_barrel_search_init(&first, TEST_SEED);
	extended_barrel_search_init(&second, TEST_SEED);
	for (legacy_uint index = 0; index < TEST_GOLDEN_ATTEMPTS; index++) {
		struct EXTENDED_BARREL_PARAMETERS a =
			extended_barrel_search_next(&first, TEST_MEASURED_SPAN, TEST_MEASURED_BANK,
										EXTENDED_BARREL_FIELDS_PULSES, &baseline);
		struct EXTENDED_BARREL_PARAMETERS b =
			extended_barrel_search_next(&second, TEST_MEASURED_SPAN, TEST_MEASURED_BANK,
										EXTENDED_BARREL_FIELDS_PULSES, &baseline);
		assert(hash_parameters(TEST_HASH_INITIAL, &a) == hash_parameters(TEST_HASH_INITIAL, &b));
		assert(a.speed == baseline.speed && a.lateral == baseline.lateral &&
			   a.shift == baseline.shift);
		assert(a.checkpoint == baseline.checkpoint && a.air_brake == baseline.air_brake);
		extended_barrel_search_observe(&first, &a, score_parameters(&a, index));
		extended_barrel_search_observe(&second, &b, score_parameters(&b, index));
		assert(first.count == second.count && first.random == second.random);
	}
	legacy_u32 random = first.random;
	legacy_u64 attempts = first.attempts;
	struct EXTENDED_BARREL_PARAMETERS fixed = extended_barrel_search_next(
		&first, 0U, 0U, (legacy_u16)~EXTENDED_BARREL_FIELDS_ALL, &baseline);
	assert(hash_parameters(TEST_HASH_INITIAL, &fixed) ==
		   hash_parameters(TEST_HASH_INITIAL, &baseline));
	assert(first.random == random && first.attempts == attempts + 1U);
	extended_barrel_search_init(&first, TEST_SEED);
	extended_barrel_search_init(&second, TEST_SEED);
	struct EXTENDED_BARREL_PARAMETERS a = extended_barrel_search_next(
		&first, TEST_MEASURED_SPAN, TEST_MEASURED_BANK, EXTENDED_BARREL_FIELDS_ALL, NULL);
	struct EXTENDED_BARREL_PARAMETERS b =
		extended_barrel_search_next(&second, TEST_MEASURED_SPAN * 2U, TEST_MEASURED_BANK * 2U,
									EXTENDED_BARREL_FIELDS_ALL, NULL);
	assert(hash_parameters(TEST_HASH_INITIAL, &a) == hash_parameters(TEST_HASH_INITIAL, &b));
	extended_barrel_search_init(&first, 0U);
	assert(first.random != 0U);
	a = extended_barrel_search_next(&first, 0U, 0U, EXTENDED_BARREL_FIELDS_ALL, NULL);
	assert(a.begin == 0U && a.pedal_begin == 0U);
}

static legacy_uint population_slot(const struct EXTENDED_BARREL_SEARCH *search, legacy_uint key)
{
	for (legacy_uint index = 0; index < search->count; index++) {
		if (search->population[index].begin == key) {
			return index;
		}
	}
	return EXTENDED_BARREL_POPULATION;
}

static void check_population_lifecycle(void)
{
	struct EXTENDED_BARREL_SEARCH search;
	struct EXTENDED_BARREL_PARAMETERS p = window_parameters();
	extended_barrel_search_init(&search, TEST_SEED);
	for (legacy_uint index = 0; index < EXTENDED_BARREL_POPULATION; index++) {
		p.begin = index;
		extended_barrel_search_observe(&search, &p, index);
		assert(search.count == index + 1U);
	}
	p.begin = TEST_DUPLICATE_INDEX;
	extended_barrel_search_observe(&search, &p, -1.0);
	assert(search.fitness[TEST_DUPLICATE_INDEX] == TEST_DUPLICATE_INDEX);
	extended_barrel_search_observe(&search, &p, TEST_IMPROVED_FITNESS);
	assert(search.fitness[TEST_DUPLICATE_INDEX] == TEST_IMPROVED_FITNESS);
	p.begin = TEST_REPLACEMENT_KEY;
	extended_barrel_search_observe(&search, &p, NAN);
	extended_barrel_search_observe(&search, &p, INFINITY);
	extended_barrel_search_observe(&search, &p, -INFINITY);
	extended_barrel_search_observe(&search, &p, 0.0);
	assert(population_slot(&search, TEST_REPLACEMENT_KEY) == EXTENDED_BARREL_POPULATION);
	extended_barrel_search_observe(&search, &p, EXTENDED_BARREL_POPULATION);
	assert(search.count == EXTENDED_BARREL_POPULATION);
	assert(population_slot(&search, 0U) == EXTENDED_BARREL_POPULATION);
	assert(population_slot(&search, TEST_REPLACEMENT_KEY) < EXTENDED_BARREL_POPULATION);
	legacy_u64 ideas = TEST_HASH_INITIAL;
	for (legacy_uint index = 0; index < search.count; index++) {
		ideas = hash_parameters(ideas, &search.population[index]);
	}
	legacy_u32 random = search.random;
	legacy_u64 attempts = search.attempts;
	extended_barrel_search_invalidate(&search);
	legacy_u64 retained = TEST_HASH_INITIAL;
	for (legacy_uint index = 0; index < search.count; index++) {
		assert(search.fitness[index] == -DBL_MAX);
		retained = hash_parameters(retained, &search.population[index]);
	}
	assert(retained == ideas && search.random == random && search.attempts == attempts);
	p.begin = TEST_DUPLICATE_INDEX;
	extended_barrel_search_observe(&search, &p, TEST_REVALIDATED_FITNESS);
	assert(search.fitness[population_slot(&search, p.begin)] == TEST_REVALIDATED_FITNESS);
	p.begin = TEST_REPLACEMENT_KEY + 1U;
	extended_barrel_search_observe(&search, &p, TEST_NEW_FITNESS);
	assert(population_slot(&search, p.begin) < EXTENDED_BARREL_POPULATION);
	assert(search.count == EXTENDED_BARREL_POPULATION);
	(void)extended_barrel_search_next(&search, TEST_MEASURED_SPAN, TEST_MEASURED_BANK,
									  EXTENDED_BARREL_FIELDS_ALL, NULL);
	random = search.random;
	assert(search.attempts != 0U);
	extended_barrel_search_clear(&search);
	assert(search.count == 0U && search.attempts == 0U && search.random == random);
}

legacy_int main(void)
{
	check_windows();
	check_golden_sequence();
	check_restricted_determinism();
	check_population_lifecycle();
	return 0;
}
