#include <assert.h>
#include <stddef.h>

#include "../c/physics/physics_internal.h"

static void configure_collision_option(const char *option)
{
	legacy_s8 *argv[] = {(legacy_s8 *)"restunts", (legacy_s8 *)option};
	configure_legacy_collision(2, argv);
}

static void assert_vector(const struct VECTOR *actual, const struct VECTOR *expected)
{
	assert(actual->x == expected->x);
	assert(actual->y == expected->y);
	assert(actual->z == expected->z);
}

static void assert_crossing(legacy_s16 expected_x)
{
	struct VECTOR first = {100, 200, -100};
	struct VECTOR second = {-100, -200, 100};
	struct VECTOR result;
	interpolate_collision_at_z(&first, &second, &result, 0);
	assert(result.x == expected_x);
	assert(result.y == expected_x * 2);
	assert(result.z == 0);
}

static void test_interpolation_geometry(void)
{
	static const struct {
		struct VECTOR first;
		struct VECTOR second;
		legacy_s16 depth;
		struct VECTOR corrected;
	} cases[] = {
		/* Reversing travel across the plane must preserve an even-span midpoint. */
		{{100, 200, -100}, {-100, -200, 100}, 0, {0, 0, 0}},
		{{-100, -200, 100}, {100, 200, -100}, 0, {0, 0, 0}},
		/* The earlier signed algorithm halves negative spans, rounding down. */
		{{120, 240, -3}, {0, 0, 2}, 0, {40, 80, 0}},
		{{120, 240, -2}, {0, 0, 3}, 0, {80, 160, 0}},
		/* Axis differences must not wrap at the signed 16-bit boundary. */
		{{30000, -30000, 100}, {-30000, 30000, -100}, 0, {0, 0, 0}},
		{{-30000, 30000, 100}, {30000, -30000, -100}, 0, {0, 0, 0}},
		/* Depth differences must also retain their full signed range. */
		{{100, 200, -30000}, {-100, -200, 30000}, 0, {0, 0, 0}},
		{{100, 200, 30000}, {-100, -200, -30000}, 0, {0, 0, 0}},
		/* Signed division truncates fractional coordinates toward zero. */
		{{3, -3, 3}, {0, 0, -3}, -2, {0, 0, -2}},
		/* Coplanar points retain the second point's coordinates. */
		{{100, 200, 7}, {-100, -200, 7}, 0, {-100, -200, 0}},
		{{100, 200, -100}, {-100, -200, 100}, -100, {100, 200, -100}},
		{{100, 200, -100}, {-100, -200, 100}, 100, {-100, -200, 100}},
	};

	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		struct VECTOR first = cases[index].first;
		struct VECTOR second = cases[index].second;
		struct VECTOR original;
		struct VECTOR result;
		vector_interpolate_at_z(&first, &second, &original, cases[index].depth);
		configure_collision_option("--lc:on");
		interpolate_collision_at_z(&first, &second, &result, cases[index].depth);
		assert_vector(&result, &original);

		configure_collision_option("--lc:off");
		interpolate_collision_at_z(&first, &second, &result, cases[index].depth);
		assert_vector(&result, &cases[index].corrected);

		/* Renderer interpolation must retain its original arithmetic in either mode. */
		vector_interpolate_at_z(&first, &second, &result, cases[index].depth);
		assert_vector(&result, &original);
		assert_vector(&first, &cases[index].first);
		assert_vector(&second, &cases[index].second);
	}
}

static void test_collision_options(void)
{
	static const struct {
		const legacy_char *first;
		const legacy_char *second;
		legacy_s16 expected_x;
	} cases[] = {
		/* Removed spellings are ignored, including after a recognized option. */
		{"/lc:off", NULL, 100},		  {"/LC:OFF", NULL, 100},	  {"--lc:off", "/lc:on", 0},
		{"--lc:off", "/LC:ON", 0},	  {NULL, NULL, 100},		  {"--lc:on", NULL, 100},
		{"--lc:off", NULL, 0},		  {"--LC:OFF", NULL, 0},	  {"--Lc:OfF", NULL, 0},
		{"--lc", NULL, 100},		  {"lc:off", NULL, 100},	  {"-lc:off", NULL, 100},
		{"--lc:offx", NULL, 100},	  {"--lc:off ", NULL, 100},	  {"--lc:", NULL, 100},
		{"--lc:off", "--lC:On", 100}, {"--LC:ON", "--lc:off", 0}, {"--lc:off", "--lc:onx", 0},
		{"--lc:off", "--nointro", 0}, {"/ns", "--lc:off", 0},	  {"--lc:off", "--lc:off", 0},
		{"--pg:off", NULL, 100},	  {"--lc:off", "--pg:on", 0},
	};

	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		/* A new invocation must reset the preceding invocation's opt-in. */
		configure_collision_option("--lc:off");
		legacy_s8 *argv[] = {(legacy_s8 *)"restunts", (legacy_s8 *)cases[index].first,
							 (legacy_s8 *)cases[index].second};
		legacy_s16 argc = cases[index].second ? 3 : cases[index].first ? 2 : 1;
		configure_legacy_collision(argc, argv);
		assert_crossing(cases[index].expected_x);
	}

	legacy_s8 *argv[] = {(legacy_s8 *)"--lc:off"};
	configure_legacy_collision(1, argv);
	assert_crossing(100);
}

int main(void)
{
	/* The static initial state must preserve replay-compatible collision behavior. */
	assert_crossing(100);
	test_interpolation_geometry();
	test_collision_options();
	return 0;
}
