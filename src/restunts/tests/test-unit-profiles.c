#include <assert.h>
#include <math.h>
#include <string.h>
#include "../c/opponent/unit_profiles.h"
#include "../c/opponent/unit_opponent.h"
#include "../c/opponent/unit_opponent_internal.h"
#include "../c/opponent/unit_launch.h"

#define TEST_PIECES 4U
#define TEST_STORAGE (TEST_PIECES + 2U)
#define TEST_PRIMARY 1
#define TEST_SECONDARY 2
#define TEST_OTHER 3
#define TEST_SPEED_CAP 140.0
#define TEST_EXCESS_SPEED 200.0
#define TEST_PRIMARY_SPEED 83.0
#define TEST_SECONDARY_SPEED 97.0
#define TEST_EXIT_SPEED 110.0
#define TEST_GUARD_SPEED 73.0
#define TEST_GUARD_EXIT 89.0
#define TEST_GUARD_FRACTION 0.625
#define TEST_GUARD_TRIM 0.375

static struct UNIT_ROUTE route;
static legacy_f64 speeds[TEST_STORAGE], offsets[TEST_STORAGE];
static legacy_u8 ramps[TEST_STORAGE];
static struct UNIT_EXIT_PROFILE exits[TEST_STORAGE];

struct UNIT_PROFILE_SNAPSHOT {
	legacy_f64 speeds[TEST_STORAGE], offsets[TEST_STORAGE];
	legacy_u8 ramps[TEST_STORAGE];
	struct UNIT_EXIT_PROFILE exits[TEST_STORAGE];
};

struct UNIT_ROUTE *unit_opponent_pilot_route_mutable(void)
{
	return &route;
}

legacy_f64 unit_bounded(legacy_f64 value, legacy_f64 minimum, legacy_f64 maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

static void reset_fixture(void)
{
	route = (struct UNIT_ROUTE){0};
	route.verified_count = TEST_PIECES;
	route.verified_speeds = speeds + 1;
	route.verified_barrel_offsets = offsets + 1;
	route.verified_ramp_approaches = ramps + 1;
	route.verified_exits = exits + 1;
	route.limits.max_speed = TEST_SPEED_CAP;
	for (legacy_u32 index = 0U; index < TEST_STORAGE; index++) {
		speeds[index] = TEST_GUARD_SPEED;
		offsets[index] = TEST_GUARD_TRIM;
		ramps[index] = UNIT_RAMP_APPROACH_MINIMUM;
		exits[index] = (struct UNIT_EXIT_PROFILE){TEST_GUARD_FRACTION, TEST_GUARD_EXIT};
	}
	unit_profiles_reset();
}

static struct UNIT_PROFILE_SNAPSHOT snapshot(void)
{
	struct UNIT_PROFILE_SNAPSHOT saved;
	memcpy(saved.speeds, speeds, sizeof(speeds));
	memcpy(saved.offsets, offsets, sizeof(offsets));
	memcpy(saved.ramps, ramps, sizeof(ramps));
	memcpy(saved.exits, exits, sizeof(exits));
	return saved;
}

static void assert_unchanged(const struct UNIT_PROFILE_SNAPSHOT *saved)
{
	for (legacy_u32 index = 0U; index < TEST_STORAGE; index++) {
		assert(speeds[index] == saved->speeds[index]);
		assert(offsets[index] == saved->offsets[index]);
		assert(ramps[index] == saved->ramps[index]);
		assert(exits[index].fraction == saved->exits[index].fraction);
		assert(exits[index].speed == saved->exits[index].speed);
	}
}

static legacy_u8 ramp_for(const struct UNIT_LAUNCH_POLICY *policy, legacy_s16 piece)
{
	legacy_u8 value = 0U;
	for (legacy_u32 slot = 0U; slot < UNIT_RAMP_APPROACH_OVERRIDES; slot++) {
		if (policy->ramps[slot].piece == piece && policy->ramps[slot].tiles) {
			assert(!value);
			value = policy->ramps[slot].tiles;
		}
	}
	return value;
}

static void assert_private_clear(const struct UNIT_LAUNCH_POLICY *policy)
{
	assert(policy->secondary_piece == ROUTE_INDEX_NONE);
	assert(policy->secondary_speed == 0.0);
	assert(!policy->exit && !policy->barrel_offset);
	assert(!policy->preserve_approach);
	for (legacy_u32 slot = 0U; slot < UNIT_RAMP_APPROACH_OVERRIDES; slot++) {
		assert(policy->ramps[slot].tiles == 0U);
	}
}

static void test_private_policy_cannot_replace_accepted_live_maps(void)
{
	reset_fixture();
	unit_opponent_pilot_accept(TEST_PRIMARY, TEST_PRIMARY_SPEED);
	unit_opponent_pilot_accept(TEST_SECONDARY, TEST_SECONDARY_SPEED);
	unit_opponent_pilot_accept_exit(TEST_PRIMARY, 0.5, TEST_EXIT_SPEED);
	unit_opponent_pilot_accept_barrel(TEST_PRIMARY, 0.25);
	unit_opponent_pilot_accept_ramp(TEST_PRIMARY, 3U);
	unit_opponent_pilot_accept_ramp(TEST_SECONDARY, 4U);
	struct UNIT_PROFILE_SNAPSHOT accepted = snapshot();
	unit_opponent_pilot_secondary_override(TEST_SECONDARY, TEST_EXCESS_SPEED);
	unit_opponent_pilot_exit_override(TEST_PRIMARY, 0.75, TEST_EXCESS_SPEED);
	unit_opponent_pilot_barrel_override(TEST_PRIMARY, -0.75);
	unit_opponent_pilot_ramp_override(TEST_PRIMARY, 2U);
	unit_opponent_pilot_ramp_override(TEST_SECONDARY, 2U);
	struct UNIT_LAUNCH_POLICY preview, live;
	unit_profiles_policy(1, TEST_PRIMARY, TEST_PRIMARY_SPEED, &preview);
	assert(preview.primary_piece == TEST_PRIMARY && preview.primary_speed == TEST_PRIMARY_SPEED);
	assert(preview.secondary_piece == TEST_SECONDARY && preview.secondary_speed == TEST_SPEED_CAP);
	assert(preview.exit && preview.exit->fraction == 0.75 && preview.exit->speed == TEST_SPEED_CAP);
	assert(preview.barrel_offset && *preview.barrel_offset == -0.75);
	assert(ramp_for(&preview, TEST_PRIMARY) == 2U && ramp_for(&preview, TEST_SECONDARY) == 2U);
	unit_profiles_policy(0, TEST_PRIMARY, TEST_PRIMARY_SPEED, &live);
	assert(live.primary_piece == TEST_PRIMARY && live.primary_speed == TEST_PRIMARY_SPEED);
	assert_private_clear(&live);
	unit_profiles_policy(1, TEST_OTHER, 0.0, &preview);
	assert(!preview.exit && !preview.barrel_offset);
	assert_unchanged(&accepted);
	unit_opponent_pilot_exit_override(ROUTE_INDEX_NONE, 0.0, 0.0);
	unit_opponent_pilot_barrel_override(ROUTE_INDEX_NONE, 0.0);
	unit_opponent_pilot_secondary_override(ROUTE_INDEX_NONE, 0.0);
	unit_opponent_pilot_ramp_override(ROUTE_INDEX_NONE, 0U);
	unit_profiles_policy(1, TEST_PRIMARY, 0.0, &preview);
	assert_private_clear(&preview);
	assert_unchanged(&accepted);
	unit_opponent_pilot_ramp_override(TEST_PRIMARY, 4U);
	unit_profiles_reset();
	unit_profiles_policy(1, TEST_PRIMARY, 0.0, &preview);
	assert_private_clear(&preview);
	assert_unchanged(&accepted);
}

static void test_two_ramp_slots_update_and_reset(void)
{
	reset_fixture();
	struct UNIT_PROFILE_SNAPSHOT accepted = snapshot();
	unit_opponent_pilot_ramp_override(TEST_PRIMARY, 2U);
	unit_opponent_pilot_ramp_override(TEST_SECONDARY, 4U);
	unit_opponent_pilot_ramp_override(TEST_OTHER, 3U);
	struct UNIT_LAUNCH_POLICY policy;
	unit_profiles_policy(1, TEST_PRIMARY, 0.0, &policy);
	assert(ramp_for(&policy, TEST_PRIMARY) == 2U);
	assert(ramp_for(&policy, TEST_SECONDARY) == 4U);
	assert(ramp_for(&policy, TEST_OTHER) == 0U);
	unit_opponent_pilot_ramp_override(TEST_SECONDARY, 3U);
	unit_profiles_policy(1, TEST_PRIMARY, 0.0, &policy);
	assert(ramp_for(&policy, TEST_PRIMARY) == 2U);
	assert(ramp_for(&policy, TEST_SECONDARY) == 3U);
	unit_opponent_pilot_ramp_override(TEST_PRIMARY, 0U);
	unit_opponent_pilot_ramp_override(TEST_SECONDARY, 4U);
	unit_opponent_pilot_ramp_override(TEST_OTHER, 3U);
	unit_profiles_policy(1, TEST_PRIMARY, 0.0, &policy);
	assert(ramp_for(&policy, TEST_PRIMARY) == 0U);
	assert(ramp_for(&policy, TEST_SECONDARY) == 4U);
	assert(ramp_for(&policy, TEST_OTHER) == 3U);
	unit_profiles_reset();
	unit_profiles_policy(1, TEST_PRIMARY, 0.0, &policy);
	assert_private_clear(&policy);
	unit_opponent_pilot_ramp_override(TEST_SECONDARY, 3U);
	unit_profiles_policy(1, TEST_PRIMARY, 0.0, &policy);
	assert(ramp_for(&policy, TEST_PRIMARY) == 0U);
	assert(ramp_for(&policy, TEST_SECONDARY) == 3U);
	assert_unchanged(&accepted);
}

static void test_accepted_bounds_and_car_limits(void)
{
	reset_fixture();
	struct UNIT_PROFILE_SNAPSHOT before = snapshot();
	static const legacy_s16 invalid[] = {ROUTE_INDEX_NONE, TEST_PIECES, TEST_PIECES + 1U,
										 LEGACY_S16_MAX};
	for (legacy_u32 index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); index++) {
		unit_opponent_pilot_accept(invalid[index], TEST_EXCESS_SPEED);
		unit_opponent_pilot_accept_exit(invalid[index], 2.0, TEST_EXCESS_SPEED);
		unit_opponent_pilot_accept_barrel(invalid[index], 2.0);
		unit_opponent_pilot_accept_ramp(invalid[index], UNIT_RAMP_APPROACH_MAXIMUM);
	}
	assert_unchanged(&before);
	unit_opponent_pilot_accept(TEST_PRIMARY, TEST_EXCESS_SPEED);
	assert(route.verified_speeds[TEST_PRIMARY] == TEST_SPEED_CAP);
	unit_opponent_pilot_accept(TEST_PRIMARY, 0.0);
	unit_opponent_pilot_accept(TEST_PRIMARY, -1.0);
	unit_opponent_pilot_accept(TEST_PRIMARY, NAN);
	unit_opponent_pilot_accept(TEST_PRIMARY, INFINITY);
	assert(route.verified_speeds[TEST_PRIMARY] == TEST_SPEED_CAP);
	unit_opponent_pilot_accept_exit(TEST_PRIMARY, 2.0, TEST_EXCESS_SPEED);
	assert(route.verified_exits[TEST_PRIMARY].fraction == 1.0);
	assert(route.verified_exits[TEST_PRIMARY].speed == TEST_SPEED_CAP);
	unit_opponent_pilot_accept_exit(TEST_PRIMARY, -1.0, -1.0);
	assert(route.verified_exits[TEST_PRIMARY].fraction == 0.0);
	assert(route.verified_exits[TEST_PRIMARY].speed == 0.0);
	unit_opponent_pilot_accept_exit(TEST_PRIMARY, NAN, INFINITY);
	assert(route.verified_exits[TEST_PRIMARY].fraction == 0.0);
	assert(route.verified_exits[TEST_PRIMARY].speed == 0.0);
	unit_opponent_pilot_accept_barrel(TEST_PRIMARY, 2.0);
	assert(route.verified_barrel_offsets[TEST_PRIMARY] == 1.0);
	unit_opponent_pilot_accept_barrel(TEST_PRIMARY, -2.0);
	assert(route.verified_barrel_offsets[TEST_PRIMARY] == -1.0);
	unit_opponent_pilot_accept_barrel(TEST_PRIMARY, NAN);
	assert(route.verified_barrel_offsets[TEST_PRIMARY] == 0.0);
	unit_opponent_pilot_accept_ramp(TEST_PRIMARY, UNIT_RAMP_APPROACH_MAXIMUM);
	assert(route.verified_ramp_approaches[TEST_PRIMARY] == UNIT_RAMP_APPROACH_MAXIMUM);
	unit_opponent_pilot_accept_ramp(TEST_PRIMARY, UNIT_RAMP_APPROACH_MINIMUM - 1U);
	unit_opponent_pilot_accept_ramp(TEST_PRIMARY, UNIT_RAMP_APPROACH_MAXIMUM + 1U);
	unit_opponent_pilot_accept_ramp(TEST_PRIMARY, 0U);
	assert(route.verified_ramp_approaches[TEST_PRIMARY] == UNIT_RAMP_APPROACH_MAXIMUM);
	unit_opponent_pilot_accept_ramp(TEST_PRIMARY, UNIT_RAMP_APPROACH_MINIMUM);
	assert(route.verified_ramp_approaches[TEST_PRIMARY] == UNIT_RAMP_APPROACH_MINIMUM);
	before = snapshot();
	route.verified_exits = NULL;
	route.verified_barrel_offsets = NULL;
	route.verified_ramp_approaches = NULL;
	unit_opponent_pilot_accept_exit(TEST_PRIMARY, 1.0, TEST_EXCESS_SPEED);
	unit_opponent_pilot_accept_barrel(TEST_PRIMARY, 1.0);
	unit_opponent_pilot_accept_ramp(TEST_PRIMARY, UNIT_RAMP_APPROACH_MAXIMUM);
	assert_unchanged(&before);
}

static void test_private_clamps_leave_accepted_maps_unchanged(void)
{
	reset_fixture();
	struct UNIT_PROFILE_SNAPSHOT accepted = snapshot();
	unit_opponent_pilot_secondary_override(TEST_SECONDARY, TEST_EXCESS_SPEED);
	unit_opponent_pilot_exit_override(TEST_PRIMARY, 2.0, TEST_EXCESS_SPEED);
	unit_opponent_pilot_barrel_override(TEST_PRIMARY, 2.0);
	struct UNIT_LAUNCH_POLICY policy;
	unit_profiles_policy(1, TEST_PRIMARY, TEST_PRIMARY_SPEED, &policy);
	assert(policy.secondary_speed == TEST_SPEED_CAP);
	assert(policy.exit && policy.exit->fraction == 1.0 && policy.exit->speed == TEST_SPEED_CAP);
	assert(policy.barrel_offset && *policy.barrel_offset == 1.0);
	unit_opponent_pilot_secondary_override(TEST_SECONDARY, -1.0);
	unit_opponent_pilot_exit_override(TEST_PRIMARY, -1.0, -1.0);
	unit_opponent_pilot_barrel_override(TEST_PRIMARY, -2.0);
	unit_profiles_policy(1, TEST_PRIMARY, 0.0, &policy);
	assert(policy.secondary_speed == 0.0);
	assert(policy.exit->fraction == 0.0 && policy.exit->speed == 0.0);
	assert(*policy.barrel_offset == -1.0);
	unit_opponent_pilot_secondary_override(TEST_SECONDARY, NAN);
	unit_opponent_pilot_exit_override(TEST_PRIMARY, INFINITY, NAN);
	unit_opponent_pilot_barrel_override(TEST_PRIMARY, NAN);
	unit_profiles_policy(1, TEST_PRIMARY, 0.0, &policy);
	assert(policy.secondary_speed == 0.0);
	assert(policy.exit->fraction == 0.0 && policy.exit->speed == 0.0);
	assert(*policy.barrel_offset == 0.0);
	unit_profiles_reset();
	assert_unchanged(&accepted);
}

legacy_int main(void)
{
	test_private_policy_cannot_replace_accepted_live_maps();
	test_two_ramp_slots_update_and_reset();
	test_accepted_bounds_and_car_limits();
	test_private_clamps_leave_accepted_maps_unchanged();
	return 0;
}
