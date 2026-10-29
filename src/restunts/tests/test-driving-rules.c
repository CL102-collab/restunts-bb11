#include <assert.h>
#include <stdio.h>
#include "../c/driving_rules.h"
#include "../c/car_speed.h"
#include "../c/physics_internal.h"
#include "../c/race_route.h"
#include "../c/owoot.h"

#define TEST_SWITCH_RULE_COUNT 5U
#define TEST_ARGUMENT_CAPACITY 16U
#define TEST_DEFAULT_RULES                                                                         \
	(DRIVING_RULE_POWER_GEAR | DRIVING_RULE_LEGACY_COLLISION | DRIVING_RULE_LEFT_CORNER_BIAS |     \
	 DRIVING_RULE_DUAL_WAY_SWITCHING | DRIVING_RULE_CHICANE_FAST_GRASS)

static void configure_rules(legacy_s16 argc, legacy_s8 *argv[])
{
	configure_powergear_bug(argc, argv);
	configure_legacy_collision(argc, argv);
	configure_left_corner_bias(argc, argv);
	configure_dual_way_switching(argc, argv);
	configure_chicane_fast_grass(argc, argv);
	configure_amiga_grass(argc, argv);
	configure_owoot(argc, argv);
}

static void test_all_recorded_combinations(void)
{
	static const legacy_s8 *const off_options[TEST_SWITCH_RULE_COUNT] = {
		"--pg:off", "--lc:off", "--lcb:off", "--dws:off", "--cfg:off"};
	static const legacy_s8 *const on_options[TEST_SWITCH_RULE_COUNT] = {
		"--pg:on", "--lc:on", "--lcb:on", "--dws:on", "--cfg:on"};
	static const legacy_u16 bits[TEST_SWITCH_RULE_COUNT] = {
		DRIVING_RULE_POWER_GEAR, DRIVING_RULE_LEGACY_COLLISION, DRIVING_RULE_LEFT_CORNER_BIAS,
		DRIVING_RULE_DUAL_WAY_SWITCHING, DRIVING_RULE_CHICANE_FAST_GRASS};
	legacy_s8 *defaults[] = {(legacy_s8 *)"restunts"};
	configure_rules(1, defaults);
	assert(driving_rules_current() == TEST_DEFAULT_RULES);
	for (legacy_u16 mask = 0; mask <= DRIVING_RULE_KNOWN_MASK; mask++) {
		legacy_s8 *arguments[TEST_ARGUMENT_CAPACITY];
		legacy_s16 argc = 0;
		arguments[argc++] = (legacy_s8 *)"restunts";
		for (legacy_u16 rule = 0; rule < TEST_SWITCH_RULE_COUNT; rule++) {
			arguments[argc++] =
				(legacy_s8 *)((mask & bits[rule]) ? on_options[rule] : off_options[rule]);
		}
		if (mask & DRIVING_RULE_AMIGA_GRASS) {
			arguments[argc++] = (legacy_s8 *)"--amigagrass";
		}
		if (mask & DRIVING_RULE_OWOOT) {
			arguments[argc++] = (legacy_s8 *)"--owoot";
		}
		configure_rules(argc, arguments);
		assert(driving_rules_current() == mask);
		/* Reading the snapshot does not reset or alter any configured rule. */
		assert(driving_rules_current() == mask);
		configure_rules(1, defaults);
		assert(driving_rules_current() == TEST_DEFAULT_RULES);
	}
}

static void test_startup_argument_semantics(void)
{
	legacy_s8 *arguments[] = {
		(legacy_s8 *)"restunts",  (legacy_s8 *)"--PG:off", (legacy_s8 *)"--PG:on",
		(legacy_s8 *)"--LC:on",	  (legacy_s8 *)"--LC:off", (legacy_s8 *)"--LCB:off",
		(legacy_s8 *)"--LCB:on",  (legacy_s8 *)"--DWS:on", (legacy_s8 *)"--DWS:off",
		(legacy_s8 *)"--CFG:off", (legacy_s8 *)"--CFG:on", (legacy_s8 *)"--AMIGAGRASS",
		(legacy_s8 *)"--OWOOT"};
	configure_rules((legacy_s16)(sizeof(arguments) / sizeof(arguments[0])), arguments);
	assert(driving_rules_current() ==
		   (DRIVING_RULE_POWER_GEAR | DRIVING_RULE_LEFT_CORNER_BIAS |
			DRIVING_RULE_CHICANE_FAST_GRASS | DRIVING_RULE_AMIGA_GRASS | DRIVING_RULE_OWOOT));
	legacy_s8 *visual_options[] = {(legacy_s8 *)"restunts", (legacy_s8 *)"--fullscreen:on",
								   (legacy_s8 *)"--fps:off", (legacy_s8 *)"--ogg:on",
								   (legacy_s8 *)"--nointro"};
	configure_rules((legacy_s16)(sizeof(visual_options) / sizeof(visual_options[0])),
					visual_options);
	assert(driving_rules_current() == TEST_DEFAULT_RULES);
}

int main(void)
{
	test_all_recorded_combinations();
	test_startup_argument_semantics();
	puts("Driving-rule startup snapshots passed for all 128 combinations");
	return 0;
}
