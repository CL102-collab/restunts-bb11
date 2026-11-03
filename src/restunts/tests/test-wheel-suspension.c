#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/physics/physics_internal.h"
#include "../c/gamestate.h"

static void test_suspension_boundaries(void)
{
	struct CARSTATE car;

	memset(&car, 0, sizeof(car));
	car.car_suspension_target[0] = 3;
	car.car_suspension_deflection[0] = 129;
	assert(update_wheel_suspension(&car, 0, 0) == 257);
	assert(car.car_suspension_target[0] == 0);
	assert(car.car_suspension_deflection[0] == 1);
	car.car_suspension_deflection[0] = -129;
	car.car_suspension_target[0] = -3;
	assert(update_wheel_suspension(&car, 0, 0) == -129);
	assert(car.car_suspension_target[0] == 0);
	assert(car.car_suspension_deflection[0] == -1);
	car.car_suspension_deflection[0] = 300;
	car.car_reserved_contact_state[0] = 99;
	assert(update_wheel_suspension(&car, 193, 0) == 300);
	assert(car.car_suspension_deflection[0] == 384);
	assert(car.car_reserved_contact_state[0] == 0);
	car.car_suspension_deflection[0] = -100;
	assert(update_wheel_suspension(&car, -188, 0) == -147);
	assert(car.car_suspension_deflection[0] == -241);
	car.car_suspension_deflection[0] = -100;
	assert(update_wheel_suspension(&car, -187, 0) == -100);
	assert(car.car_suspension_deflection[0] == -287);
}

static legacy_u32 suspension_fingerprint(void)
{
	legacy_u32 hash = 2166136261UL;
	static const legacy_s16 values[] = {-32768, -385, -384, -289, -288, -193, -192, -129, -128, -5,
										-4,		-3,	  -1,	0,	  1,	3,	  4,	5,	  127,	128,
										129,	191,  192,	193,  383,	384,  385,	32767};
	struct CARSTATE car;
	for (legacy_u32 deflection = 0; deflection < sizeof(values) / sizeof(values[0]); deflection++) {
		for (legacy_u32 target = 0; target < sizeof(values) / sizeof(values[0]); target++) {
			for (legacy_u32 delta = 0; delta < sizeof(values) / sizeof(values[0]); delta++) {
				memset(&car, 0x5a, sizeof(car));
				legacy_u32 wheel = (deflection + target + delta) % 4;
				car.car_suspension_deflection[wheel] = values[deflection];
				car.car_suspension_target[wheel] = values[target];
				legacy_s16 result = update_wheel_suspension(&car, values[delta], wheel);
				hash = (hash ^ (legacy_u16)result) * 16777619UL;
				const legacy_u8 *bytes = (const legacy_u8 *)&car;
				for (legacy_u32 i = 0; i < sizeof(car); i++) {
					hash = (hash ^ bytes[i]) * 16777619UL;
				}
			}
		}
	}
	return hash;
}

int main(void)
{
	test_suspension_boundaries();
	legacy_u32 hash = suspension_fingerprint();
#ifdef PHYSICS_RECORD_BASELINE
	printf("%08" LEGACY_PRIx32 "\n", hash);
#else
	/* Fingerprints captured from the pre-refactor implementation. */
	assert(hash == 0x509385dfUL);
#endif
	return 0;
}
