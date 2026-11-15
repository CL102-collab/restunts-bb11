#include <assert.h>
#include <math.h>
#include "../c/opponent/unit_collision.h"
#include "../c/opponent/unit_planner.h"

#define TEST_PI 3.14159265358979323846
#define TEST_ROTATIONS 4U
#define TEST_FRAME_SECONDS 0.05
#define TEST_HALF_WIDTH 19.0
#define TEST_HALF_LENGTH 56.0
#define TEST_MARGIN 20.0
#define TEST_GRID_SPACING 72.0
#define TEST_ORIGIN_X 20000.0
#define TEST_ORIGIN_Z 50000.0

static struct UNIT_VEHICLE vehicle(legacy_f64 x, legacy_f64 z, legacy_f64 yaw)
{
	struct UNIT_VEHICLE car = {0};
	car.x = x;
	car.z = z;
	car.yaw = yaw;
	car.half_width = TEST_HALF_WIDTH;
	car.half_length = TEST_HALF_LENGTH;
	return car;
}

static struct UNIT_VEHICLE rotated(const struct UNIT_VEHICLE *car, legacy_u8 direction)
{
	struct UNIT_VEHICLE result = *car;
	legacy_f64 angle = direction * TEST_PI * 0.5;
	legacy_f64 sine = sin(angle), cosine = cos(angle);
	result.x = TEST_ORIGIN_X + car->x * cosine + car->z * sine;
	result.z = TEST_ORIGIN_Z - car->x * sine + car->z * cosine;
	result.vx = car->vx * cosine + car->vz * sine;
	result.vz = -car->vx * sine + car->vz * cosine;
	result.yaw += angle;
	return result;
}

static void assert_sweep(legacy_int expected, const struct UNIT_VEHICLE *first,
						 const struct UNIT_VEHICLE *last, const struct UNIT_VEHICLE *player,
						 legacy_f64 margin)
{
	for (legacy_u8 direction = 0U; direction < TEST_ROTATIONS; direction++) {
		struct UNIT_VEHICLE a = rotated(first, direction);
		struct UNIT_VEHICLE b = rotated(last, direction);
		struct UNIT_VEHICLE human = rotated(player, direction);
		assert(!!unit_collision_swept_boxes(&a, &b, &human, TEST_FRAME_SECONDS, margin) ==
			   expected);
	}
}

static void test_parallel_grid_and_close_contact(void)
{
	struct UNIT_VEHICLE first = vehicle(0.0, 0.0, 0.0);
	struct UNIT_VEHICLE last = vehicle(0.0, 40.0, 0.0);
	struct UNIT_VEHICLE player = vehicle(TEST_GRID_SPACING, 0.0, 0.0);
	player.vz = (last.z - first.z) / TEST_FRAME_SECONDS;
	/* Bounding circles overlap on the real grid; the body rectangles do not. */
	assert_sweep(0, &first, &last, &player, TEST_MARGIN);
	player.x = -TEST_GRID_SPACING;
	assert_sweep(0, &first, &last, &player, TEST_MARGIN);
	player.vz = 0.0;
	first.z = -200.0;
	last.z = 200.0;
	assert_sweep(0, &first, &last, &player, TEST_MARGIN);
	player.x = 30.0;
	assert_sweep(1, &first, &last, &player, 0.0);
	player.x = 50.0;
	assert_sweep(0, &first, &last, &player, 0.0);
	assert_sweep(1, &first, &last, &player, TEST_MARGIN);
}

static void test_crossing_between_samples(void)
{
	struct UNIT_VEHICLE first = vehicle(-200.0, 0.0, TEST_PI * 0.5);
	struct UNIT_VEHICLE last = vehicle(200.0, 0.0, TEST_PI * 0.5);
	struct UNIT_VEHICLE player = vehicle(0.0, -200.0, 0.0);
	player.vz = 400.0 / TEST_FRAME_SECONDS;
	/* Endpoints are separate; the rectangles meet halfway through the frame. */
	assert_sweep(1, &first, &last, &player, 0.0);
	player.vz = 0.0;
	assert_sweep(0, &first, &last, &player, TEST_MARGIN);
	/* Crossing the same point at disjoint times does not imply a collision. */
	first.x = -300.0;
	last.x = 300.0;
	player.z = -300.0;
	player.vz = 1200.0 / TEST_FRAME_SECONDS;
	assert_sweep(0, &first, &last, &player, 0.0);
}

static void test_longitudinal_approach_and_separation(void)
{
	struct UNIT_VEHICLE first = vehicle(0.0, -200.0, 0.0);
	struct UNIT_VEHICLE last = vehicle(0.0, -40.0, 0.0);
	struct UNIT_VEHICLE player = vehicle(0.0, 200.0, TEST_PI);
	player.vz = -160.0 / TEST_FRAME_SECONDS;
	assert_sweep(1, &first, &last, &player, 0.0);
	player.vz = 160.0 / TEST_FRAME_SECONDS;
	assert_sweep(0, &first, &last, &player, TEST_MARGIN);
	/* Initial front overlap remains a collision while separating. */
	first.z = 100.0;
	last.z = -100.0;
	player.vz = 0.0;
	assert_sweep(1, &first, &last, &player, 0.0);
}

static void test_rotating_body_sweep(void)
{
	struct UNIT_VEHICLE first = vehicle(0.0, 0.0, TEST_PI * 0.25);
	struct UNIT_VEHICLE last = vehicle(0.0, 0.0, TEST_PI * 0.75);
	struct UNIT_VEHICLE player = vehicle(74.0, 0.0, 0.0);
	/* Endpoint projections reach only 72.04 world units; mid-turn support
	 * reaches 75 and intersects the human despite separate endpoints. */
	assert_sweep(1, &first, &last, &player, 0.0);
	player.x = 1000.0;
	assert_sweep(0, &first, &last, &player, TEST_MARGIN);
	/* Crossing the signed-angle seam must use the short yaw arc. */
	first.yaw = TEST_PI - 0.01;
	last.yaw = -TEST_PI + 0.01;
	player.x = TEST_GRID_SPACING;
	assert_sweep(0, &first, &last, &player, TEST_MARGIN);
}

legacy_int main(void)
{
	test_parallel_grid_and_close_contact();
	test_crossing_between_samples();
	test_longitudinal_approach_and_separation();
	test_rotating_body_sweep();
	return 0;
}
