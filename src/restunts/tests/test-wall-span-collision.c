#include <assert.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/physics_internal.h"
#include "../c/track/track_collision.h"
#include "../c/track/track_objects.h"
#include "../c/track/trackdata_layout.h"

#define TEST_ROW 10
#define TEST_COLUMN 10
#define TEST_ORIGIN 10752
#define QUERY_VALUE_COUNT 15

extern legacy_u8 corkFlag;

static legacy_u8 elements[TRACKDATA_MAP_SIZE];
static legacy_u8 terrain[TRACKDATA_MAP_SIZE];
static struct PLANE planes[TRACK_PLAN_RESOURCE_COUNT];
static struct TRACK_WALL walls[TRACK_WALL_RESOURCE_COUNT];

static void configure_option(const char *option)
{
	legacy_s8 *argv[] = {(legacy_s8 *)"restunts", (legacy_s8 *)option};
	configure_legacy_collision(option == NULL ? 1 : 2, argv);
}

static struct VECTOR rotate_from_local(struct VECTOR point, legacy_u32 rotation)
{
	legacy_s16 old_x = point.x;
	switch (rotation) {
		case 1:
			point.x = point.z;
			point.z = -old_x;
			break;
		case 2:
			point.x = -point.x;
			point.z = -point.z;
			break;
		case 3:
			point.x = -point.z;
			point.z = old_x;
			break;
	}
	return point;
}

static struct VECTOR world_point(struct VECTOR point, legacy_u32 rotation, legacy_s16 elevation,
								 legacy_s32 side)
{
	point.x *= side;
	point = rotate_from_local(point, rotation);
	point.x += TEST_ORIGIN;
	point.y += elevation;
	point.z += TEST_ORIGIN;
	return point;
}

static void initialize_ramp(legacy_u32 rotation, legacy_s16 elevation)
{
	static const legacy_s16 plane_offsets[] = {0, 3, 2, 1};
	memset(elements, 0, sizeof(elements));
	memset(terrain, 0, sizeof(terrain));
	memset(planes, 0, sizeof(planes));
	memset(walls, 0, sizeof(walls));
	track_element_map = elements;
	track_terrain_map = terrain;
	planptr = planes;
	wallptr = walls;
	for (legacy_s32 index = 0; index < TRACK_GRID_SIZE; index++) {
		trackrows[index] = index * TRACK_GRID_SIZE;
		terrainrows[index] = (TRACK_GRID_LAST_INDEX - index) * TRACK_GRID_SIZE;
		track_column_centers[index] = index * 1024 + 512;
		terraincenterpos[index] = index * 1024 + 512;
		track_column_positions[index] = index * 1024;
		terrainpos[index] = index * 1024;
	}
	elements[terrainrows[TEST_ROW] + TEST_COLUMN] = 1;
	terrain[trackrows[TEST_ROW] + TEST_COLUMN] = elevation == 0 ? 0 : 6;
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_RAMP;
	trkObjectList[1].ss_rotY = (legacy_s16)(rotation * 256);
	trkObjectList[1].ss_multiTileFlag = 0;
	trkObjectList[1].ss_surfaceType = 0;

	/* Real ramp plane and sidewalls from PLAN and WALL, rotated as the game does. */
	struct VECTOR origin = {120, 0, -512};
	struct VECTOR normal = {0, 7499, -3295};
	legacy_s16 selected_plane = 12 + plane_offsets[rotation];
	planes[selected_plane].plane_origin = rotate_from_local(origin, rotation);
	planes[selected_plane].plane_normal = rotate_from_local(normal, rotation);
	walls[100].orientation = 0;
	walls[100].x = -120;
	walls[101].orientation = 512;
	walls[101].x = 120;

	struct VECTOR point = {0, 300, 128};
	point = world_point(point, rotation, elevation, 1);
	build_track_object(&point, &point);
	assert(planindex == selected_plane);
	assert(terrainHeight == elevation + 2);

	/* Deliberately retain unrelated query outputs to catch leaked scratch state. */
	wallindex = 31;
	wallHeight = 123;
	elRdWallRelated = 234;
	corkFlag = 1;
	wallStartX = 345;
	wallStartZ = 456;
	wallOrientation = 567;
	planindex_copy = 45;
	nextPosAndNormalIP = 678;
}

static void capture_query(legacy_s16 values[QUERY_VALUE_COUNT])
{
	values[0] = planindex;
	values[1] = wallindex;
	values[2] = wallHeight;
	values[3] = elRdWallRelated;
	values[4] = corkFlag;
	values[5] = current_surf_type;
	values[6] = track_wall_collision_enabled;
	values[7] = terrainHeight;
	values[8] = elem_xCenter;
	values[9] = elem_zCenter;
	values[10] = wallStartX;
	values[11] = wallStartZ;
	values[12] = wallOrientation;
	values[13] = planindex_copy;
	values[14] = nextPosAndNormalIP;
}

static struct VECTORLONG fixed_point(struct VECTOR point)
{
	struct VECTORLONG result = {(legacy_s32)point.x * 64, (legacy_s32)point.y * 64,
								(legacy_s32)point.z * 64};
	return result;
}

static legacy_s16 assert_fixed_span(struct VECTORLONG points[4], legacy_s16 expected_hit,
									legacy_s16 expected_fraction, legacy_s16 tolerance)
{
	struct VECTORLONG saved_points[] = {points[0], points[1], points[2], points[3]};
	struct PLANE *saved_plane = current_planptr;
	legacy_s16 before[QUERY_VALUE_COUNT];
	legacy_s16 after[QUERY_VALUE_COUNT];
	capture_query(before);
	legacy_s16 fraction = -1;
	assert(sweep_track_wall_span(&points[0], &points[1], &points[2], &points[3], &fraction) ==
		   expected_hit);
	if (expected_hit != 0) {
		assert(fraction >= 0 && fraction < TRIG_FIXED_ONE);
		/* The last clear fraction can differ by one quantized world coordinate. */
		assert(fraction >= expected_fraction - tolerance);
		assert(fraction <= expected_fraction + tolerance);
	}
	capture_query(after);
	assert(memcmp(before, after, sizeof(before)) == 0);
	assert(current_planptr == saved_plane);
	assert(memcmp(points, saved_points, sizeof(saved_points)) == 0);
	return fraction;
}

static void assert_span(struct VECTOR points[4], legacy_s16 expected_hit,
						legacy_s16 expected_fraction, legacy_s16 tolerance)
{
	struct VECTORLONG fixed_points[4];
	for (legacy_u32 index = 0; index < 4; index++) {
		fixed_points[index] = fixed_point(points[index]);
	}
	assert_fixed_span(fixed_points, expected_hit, expected_fraction, tolerance);
}

static struct VECTOR retained_position(struct VECTORLONG *previous, struct VECTORLONG *current,
									   legacy_s16 fraction)
{
	/* Reproduce the actual wheel stop, retaining sub-world-coordinate motion until
	 * the final conversion. Rounding endpoints before interpolation is different. */
	struct VECTOR result;
	result.x = position_to_word(
		previous->lx + scale_position_delta(current->lx, previous->lx, fraction, TRIG_FIXED_ONE));
	result.y = position_to_word(
		previous->ly + scale_position_delta(current->ly, previous->ly, fraction, TRIG_FIXED_ONE));
	result.z = position_to_word(
		previous->lz + scale_position_delta(current->lz, previous->lz, fraction, TRIG_FIXED_ONE));
	return result;
}

static void test_fractional_motion(legacy_u32 rotation, legacy_s16 elevation, legacy_s32 side)
{
	initialize_ramp(rotation, elevation);
	configure_option("--lc:off");
	struct VECTOR points[] = {{100, 220, -32}, {140, 220, -32}, {100, 248, 32}, {140, 248, 32}};
	struct VECTOR offsets[] = {{55, 11, 63}, {55, 11, 63}, {7, 57, 3}, {7, 57, 3}};
	struct VECTORLONG fixed_points[4];
	for (legacy_u32 index = 0; index < 4; index++) {
		fixed_points[index] = fixed_point(world_point(points[index], rotation, elevation, side));
		offsets[index].x *= side;
		struct VECTOR offset = rotate_from_local(offsets[index], rotation);
		fixed_points[index].lx += offset.x;
		fixed_points[index].ly += offset.y;
		fixed_points[index].lz += offset.z;
	}
	legacy_s16 fraction =
		assert_fixed_span(fixed_points, 1, TRIG_FIXED_ONE / 2, TRIG_FIXED_ONE / 32);
	struct VECTOR first = retained_position(&fixed_points[0], &fixed_points[2], fraction);
	struct VECTOR second = retained_position(&fixed_points[1], &fixed_points[3], fraction);
	assert(!track_wall_intersects_segment(&first, &second));
	first = retained_position(&fixed_points[0], &fixed_points[2], fraction + 1);
	second = retained_position(&fixed_points[1], &fixed_points[3], fraction + 1);
	assert(track_wall_intersects_segment(&first, &second));
}

static void test_inclined_intersection_rounding(legacy_u32 rotation, legacy_s16 elevation,
												legacy_s32 side)
{
	initialize_ramp(rotation, elevation);
	static const struct {
		struct VECTOR points[2];
		legacy_s16 hit;
	} cases[] = {
		/* Rounding the intersection coordinates before measuring its inclined
		 * plane height moves this valid contact onto the excluded lower bound. */
		{{{127, 225, 31}, {96, 238, 28}}, 1},
		/* A nearby span remains below the wall at the actual intersection. */
		{{{127, 223, 31}, {96, 236, 28}}, 0},
		/* The open wall band excludes the exact signed height boundaries. */
		{{{100, 270, 128}, {140, 270, 128}}, 0},
		{{{100, 330, 128}, {140, 330, 128}}, 0},
		/* Rational intersection heights just inside each bound still collide:
		 * (-12 + -11) / 2 and (41 + 42) / 2, respectively. */
		{{{100, 270, 128}, {140, 271, 128}}, 1},
		{{{100, 328, 128}, {140, 330, 128}}, 1},
		/* Just outside either bound stays clear: -12.5 and 42.5. */
		{{{100, 268, 128}, {140, 270, 128}}, 0},
		{{{100, 330, 128}, {140, 331, 128}}, 0},
	};
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		struct VECTOR first = world_point(cases[index].points[0], rotation, elevation, side);
		struct VECTOR second = world_point(cases[index].points[1], rotation, elevation, side);
		struct VECTOR saved_first = first;
		struct VECTOR saved_second = second;
		struct PLANE *saved_plane = current_planptr;
		legacy_s16 before[QUERY_VALUE_COUNT];
		legacy_s16 after[QUERY_VALUE_COUNT];
		capture_query(before);
		assert(track_wall_intersects_segment(&first, &second) == cases[index].hit);
		assert(track_wall_intersects_segment(&second, &first) == cases[index].hit);
		capture_query(after);
		assert(memcmp(before, after, sizeof(before)) == 0);
		assert(current_planptr == saved_plane);
		assert(memcmp(&first, &saved_first, sizeof(first)) == 0);
		assert(memcmp(&second, &saved_second, sizeof(second)) == 0);
	}
}

static void test_mode_gate(void)
{
	initialize_ramp(0, 0);
	struct VECTOR points[] = {{100, 220, -32}, {140, 220, -32}, {100, 248, 32}, {140, 248, 32}};
	for (legacy_u32 index = 0; index < 4; index++) {
		points[index] = world_point(points[index], 0, 0, 1);
	}
	/* Startup, explicit legacy mode, and resetting to defaults retain old physics. */
	assert_span(points, 0, 0, 0);
	configure_option("--lc:on");
	assert_span(points, 0, 0, 0);
	configure_option("--lc:off");
	assert_span(points, 1, TRIG_FIXED_ONE / 2, TRIG_FIXED_ONE / 64);
	configure_option(NULL);
	assert_span(points, 0, 0, 0);
}

static void test_wall_geometry(legacy_u32 rotation, legacy_s16 elevation, legacy_s32 side)
{
	static const struct {
		struct VECTOR points[4];
		legacy_s16 hit;
		legacy_s16 fraction;
		legacy_s16 tolerance;
	} cases[] = {
		/* An axle straddles the side before it reaches the raised rail's front. */
		{{{100, 220, -32}, {140, 220, -32}, {100, 248, 32}, {140, 248, 32}},
		 1,
		 TRIG_FIXED_ONE / 2,
		 TRIG_FIXED_ONE / 64},
		/* Lateral entry and reversing into the finite upper end also make contact. */
		{{{40, 292, 128}, {80, 292, 128}, {100, 292, 128}, {140, 292, 128}},
		 1,
		 2 * TRIG_FIXED_ONE / 3,
		 TRIG_FIXED_ONE / 60},
		{{{100, 472, 540}, {140, 472, 540}, {100, 445, 480}, {140, 445, 480}},
		 1,
		 29 * TRIG_FIXED_ONE / 60,
		 TRIG_FIXED_ONE / 60},
		/* Segment height at the wall matters even when both endpoints miss the band. */
		{{{100, 182, -32}, {140, 262, -32}, {100, 210, 32}, {140, 290, 32}},
		 1,
		 TRIG_FIXED_ONE / 2,
		 TRIG_FIXED_ONE / 64},
		/* Wheel spans wholly inside or beside the track are clear. */
		{{{-80, 220, -32}, {80, 220, -32}, {-80, 248, 32}, {80, 248, 32}}, 0, 0, 0},
		{{{140, 220, -32}, {180, 220, -32}, {140, 248, 32}, {180, 248, 32}}, 0, 0, 0},
		/* Touching one wheel alone is handled by the existing wheel contact path. */
		{{{40, 292, 128}, {80, 292, 128}, {80, 292, 128}, {120, 292, 128}}, 0, 0, 0},
		{{{120, 220, -32}, {160, 220, -32}, {120, 248, 32}, {160, 248, 32}}, 0, 0, 0},
		/* Crossing below or above the finite wall must not hit its infinite plane. */
		{{{100, 152, -32}, {140, 152, -32}, {100, 180, 32}, {140, 180, 32}}, 0, 0, 0},
		{{{100, 302, -32}, {140, 302, -32}, {100, 330, 32}, {140, 330, 32}}, 0, 0, 0},
		/* The lower half has no raised rail, and the wall ends at the tile boundary. */
		{{{100, 192, -96}, {140, 192, -96}, {100, 220, -32}, {140, 220, -32}}, 0, 0, 0},
		{{{100, 472, 540}, {140, 472, 540}, {100, 486, 572}, {140, 486, 572}}, 0, 0, 0},
		/* A diagonal span can select a rail at an endpoint while its intersection
		 * lies outside that rail's finite length. */
		{{{100, 178, -128}, {140, 220, -32}, {100, 206, -64}, {140, 248, 32}}, 0, 0, 0},
		{{{140, 472, 540}, {100, 500, 604}, {140, 449, 488}, {100, 477, 552}}, 0, 0, 0},
		/* Existing penetration, stationary contact, and leaving are not new hits. */
		{{{100, 248, 32}, {140, 248, 32}, {100, 276, 96}, {140, 276, 96}}, 0, 0, 0},
		{{{100, 248, 32}, {140, 248, 32}, {100, 248, 32}, {140, 248, 32}}, 0, 0, 0},
		{{{100, 248, 32}, {140, 248, 32}, {100, 220, -32}, {140, 220, -32}}, 0, 0, 0},
	};
	initialize_ramp(rotation, elevation);
	configure_option("--lc:off");
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		struct VECTOR points[4];
		for (legacy_u32 point = 0; point < 4; point++) {
			points[point] = world_point(cases[index].points[point], rotation, elevation, side);
		}
		assert_span(points, cases[index].hit, cases[index].fraction, cases[index].tolerance);
		/* A perimeter edge has the same geometry in either endpoint order. */
		struct VECTOR reversed[] = {points[1], points[0], points[3], points[2]};
		assert_span(reversed, cases[index].hit, cases[index].fraction, cases[index].tolerance);
	}
}

int main(void)
{
	test_mode_gate();
	for (legacy_u32 rotation = 0; rotation < 4; rotation++) {
		for (legacy_s32 side = -1; side <= 1; side += 2) {
			test_wall_geometry(rotation, 0, side);
			test_wall_geometry(rotation, 450, side);
			test_fractional_motion(rotation, 0, side);
			test_fractional_motion(rotation, 450, side);
			test_inclined_intersection_rounding(rotation, 0, side);
			test_inclined_intersection_rounding(rotation, 450, side);
		}
	}
	return 0;
}
