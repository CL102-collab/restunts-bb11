#include <assert.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/physics/physics_internal.h"
#include "../c/track/track_collision.h"
#include "../c/track/track_objects.h"
#include "../c/track/trackdata_layout.h"

#define TEST_ROW 10
#define TEST_COLUMN 10
#define TEST_ORIGIN 10752
#define QUERY_VALUE_COUNT 14

extern legacy_s8 corkFlag;

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

static struct VECTOR world_point(legacy_s16 x, legacy_s16 y, legacy_s16 z, legacy_u32 rotation,
								 legacy_s16 elevation, legacy_u32 rear)
{
	struct VECTOR point = {x, y, z};
	if (rear != 0) {
		point.x = -point.x;
		point.z = -point.z;
	}
	point = rotate_from_local(point, rotation);
	point.x += TEST_ORIGIN;
	point.y += elevation;
	point.z += TEST_ORIGIN;
	return point;
}

static void initialize_loop(legacy_u32 rotation, legacy_s16 elevation, legacy_u32 rear)
{
	static const legacy_s16 plane_offsets[] = {0, 3, 2, 1};
	memset(elements, 0, sizeof(elements));
	memset(terrain, 0, sizeof(terrain));
	memset(planes, 0, sizeof(planes));
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
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_LOOP;
	trkObjectList[1].ss_rotY = (legacy_s16)(rotation * 256);
	trkObjectList[1].ss_multiTileFlag = 0;
	trkObjectList[1].ss_surfaceType = 0;

	/* The loop consists of six finite inclined facets on each mirrored half. */
	static const struct VECTOR origins[] = {{-367, 135, 224}, {-334, 299, 389}, {-300, 524, 449},
											{-266, 749, 389}, {-233, 914, 225}, {-200, 975, 0}};
	static const struct VECTOR normals[] = {{0, 7016, -4228},  {0, 5810, -5774},
											{0, 2110, -7915},  {0, -2110, -7915},
											{0, -5774, -5810}, {0, -7906, -2143}};
	for (legacy_u32 half = 0; half < 2; half++) {
		for (legacy_u32 index = 0; index < 6; index++) {
			struct VECTOR origin = origins[index];
			struct VECTOR normal = normals[index];
			if (half != 0) {
				origin.x = -origin.x;
				origin.z = -origin.z;
				normal.z = -normal.z;
			}
			legacy_s16 plane = (half == 0 ? 180 : 204) + index * 4 + plane_offsets[rotation];
			planes[plane].plane_origin = rotate_from_local(origin, rotation);
			planes[plane].plane_normal = rotate_from_local(normal, rotation);
		}
	}
	legacy_s16 selected_plane = (rear == 0 ? 180 : 204) + plane_offsets[rotation];

	struct VECTOR point = world_point(-200, 60, 100, rotation, elevation, rear);
	build_track_object(&point, &point);
	assert(planindex == selected_plane);
	assert(terrainHeight == elevation + 2);
	assert(track_wall_collision_enabled == 0);

	/* Nondefault output values expose any footprint-query state left behind. */
	wallindex = 31;
	wallHeight = 123;
	elRdWallRelated = 234;
	corkFlag = 1;
	wallStartX = 345;
	wallStartZ = 456;
	wallOrientation = 567;
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
}

static void assert_sweep(struct VECTOR previous, struct VECTOR current, legacy_s16 expected_hit,
						 legacy_s16 expected_fraction)
{
	struct VECTOR saved_previous = previous;
	struct VECTOR saved_current = current;
	struct PLANE *saved_plane = current_planptr;
	legacy_s16 before[QUERY_VALUE_COUNT];
	legacy_s16 after[QUERY_VALUE_COUNT];
	capture_query(before);
	legacy_s16 fraction = -1;
	assert(sweep_track_underside(&previous, &current, &fraction) == expected_hit);
	if (expected_hit != 0) {
		assert(fraction == expected_fraction);
	}
	capture_query(after);
	assert(memcmp(before, after, sizeof(before)) == 0);
	assert(current_planptr == saved_plane);
	assert(memcmp(&previous, &saved_previous, sizeof(previous)) == 0);
	assert(memcmp(&current, &saved_current, sizeof(current)) == 0);
}

static void test_swept_geometry(legacy_u32 rotation, legacy_s16 elevation, legacy_u32 rear)
{
	static const struct {
		legacy_s16 previous_height;
		legacy_s16 current_height;
		legacy_s16 previous_distance;
		legacy_s16 current_distance;
		legacy_s16 hit;
		legacy_s16 fraction;
	} cases[] = {
		/* High speed can skip the whole underside band or cross the entire plane. */
		{9, 60, -45, -1, 1, 21 * TRIG_FIXED_ONE / 44},
		{9, 100, -45, 32, 1, 21 * TRIG_FIXED_ONE / 77},
		/* A wheel already in the body-clearance band must stop immediately. */
		{36, 60, -22, -1, 1, 0},
		{47, 60, -13, -1, 1, 0},
		/* Keep the existing contact tolerance and the open clearance boundary. */
		{48, 60, -12, -1, 0, 0},
		{9, 34, -45, -24, 0, 0},
		{9, 35, -45, -23, 1, 21 * TRIG_FIXED_ONE / 22},
		/* Ordinary surface travel and front-side landing are not underside hits. */
		{62, 64, 0, 1, 0, 0},
		{100, 60, 32, -1, 0, 0},
		/* Passing deep underneath, moving away, and staying still remain safe. */
		{9, 20, -45, -36, 0, 0},
		{36, 9, -22, -45, 0, 0},
		{36, 36, -22, -22, 0, 0},
	};

	initialize_loop(rotation, elevation, rear);
	configure_option("--lc:off");
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		struct VECTOR previous =
			world_point(-200, cases[index].previous_height, 100, rotation, elevation, rear);
		struct VECTOR current =
			world_point(-200, cases[index].current_height, 100, rotation, elevation, rear);
		assert(plane_signed_distance(planindex, previous.x, previous.y, previous.z) ==
			   cases[index].previous_distance);
		assert(plane_signed_distance(planindex, current.x, current.y, current.z) ==
			   cases[index].current_distance);
		assert_sweep(previous, current, cases[index].hit, cases[index].fraction);
	}

	struct VECTOR previous = world_point(-250, 36, 100, rotation, elevation, rear);
	struct VECTOR current = world_point(-150, 36, 100, rotation, elevation, rear);
	assert_sweep(previous, current, 0, 0);

	/* The infinite plane crosses the path, but its projected contact is outside
	 * the loop strip. The underlying road is traversable at that point. */
	previous = world_point(-100, 9, 100, rotation, elevation, rear);
	current = world_point(500, 60, 100, rotation, elevation, rear);
	assert_sweep(previous, current, 0, 0);
}

static void test_opt_in_and_surface_eligibility(void)
{
	initialize_loop(0, 0, 0);
	struct VECTOR previous = world_point(-200, 9, 100, 0, 0, 0);
	struct VECTOR current = world_point(-200, 60, 100, 0, 0, 0);
	/* Static startup state and both explicit/default legacy modes stay unchanged. */
	assert_sweep(previous, current, 0, 0);
	configure_option("--lc:on");
	assert_sweep(previous, current, 0, 0);
	configure_option("--lc:off");
	assert_sweep(previous, current, 1, 21 * TRIG_FIXED_ONE / 44);
	configure_option(NULL);
	assert_sweep(previous, current, 0, 0);

	/* A genuinely clear path beside the loop must remain traversable. */
	configure_option("--lc:off");
	previous = world_point(-450, 9, 100, 0, 0, 0);
	current = world_point(-450, 60, 100, 0, 0, 0);
	build_track_object(&current, &previous);
	assert(planindex == 0);
	assert_sweep(previous, current, 0, 0);
}

static void test_surface_transitions(legacy_u32 rotation, legacy_s16 elevation, legacy_u32 rear)
{
	static const legacy_s16 plane_offsets[] = {0, 3, 2, 1};
	static const struct {
		struct VECTOR previous;
		struct VECTOR current;
		legacy_s16 previous_facet;
		legacy_s16 current_facet;
		legacy_s16 hit;
		legacy_s16 fraction;
	} cases[] = {
		/* The endpoint has left the facet, but the wheel crossed its underside. */
		{{-200, 60, 200}, {-200, 60, -100}, 0, -1, 1, 3085},
		{{-200, 150, 300}, {-200, 150, 150}, 1, 0, 1, 3120},
		{{-200, 140, 250}, {-200, 140, 210}, 1, 0, 1, 0},
		/* Travel staying on one facet still uses its original swept contact. */
		{{-200, 60, 200}, {-200, 60, 100}, 0, 0, 1, 9137},
		/* Ordinary front-side travel over a facet seam remains safe. */
		{{-200, 136, 223}, {-200, 138, 225}, 0, 1, 0, 0},
		{{-200, 138, 225}, {-200, 136, 223}, 1, 0, 0, 0},
		/* The same direction of travel beside or well underneath is clear. */
		{{-450, 60, 200}, {-450, 60, -100}, -1, -1, 0, 0},
		{{-200, 2, 400}, {-200, 2, 300}, -1, 1, 0, 0},
	};
	initialize_loop(rotation, elevation, rear);
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		struct VECTOR previous = world_point(cases[index].previous.x, cases[index].previous.y,
											 cases[index].previous.z, rotation, elevation, rear);
		struct VECTOR current = world_point(cases[index].current.x, cases[index].current.y,
											cases[index].current.z, rotation, elevation, rear);
		legacy_s16 base = (rear == 0 ? 180 : 204) + plane_offsets[rotation];
		build_track_object(&previous, &current);
		assert(planindex ==
			   (cases[index].previous_facet < 0 ? 0 : base + cases[index].previous_facet * 4));
		/* Match runtime: the collision query initially belongs to the endpoint. */
		build_track_object(&current, &previous);
		assert(planindex ==
			   (cases[index].current_facet < 0 ? 0 : base + cases[index].current_facet * 4));
		configure_option(NULL);
		assert_sweep(previous, current, 0, 0);
		configure_option("--lc:on");
		assert_sweep(previous, current, 0, 0);
		configure_option("--lc:off");
		assert_sweep(previous, current, cases[index].hit, cases[index].fraction);
	}
}

int main(void)
{
	test_opt_in_and_surface_eligibility();
	for (legacy_u32 rear = 0; rear < 2; rear++) {
		for (legacy_u32 rotation = 0; rotation < 4; rotation++) {
			test_swept_geometry(rotation, 0, rear);
			test_swept_geometry(rotation, 450, rear);
			test_surface_transitions(rotation, 0, rear);
			test_surface_transitions(rotation, 450, rear);
		}
	}
	return 0;
}
