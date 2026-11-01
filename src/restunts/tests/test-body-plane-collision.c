#include <assert.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/physics/physics_internal.h"
#include "../c/track/track_collision.h"
#include "../c/track/track_objects.h"
#include "../c/track/trackdata_layout.h"

#define TEST_TILE_FIRST 1U
#define TEST_TILE_SECOND 2U
#define TEST_GRID_POSITION 10
#define TEST_ORIGIN (TEST_GRID_POSITION * TRACK_TILE_SIZE + TRACK_TILE_HALF_SIZE)
#define TEST_ROTATION_COUNT 4U
#define TEST_DECK_HEIGHT 450
#define TEST_PAVED_HEIGHT_OFFSET 2
#define TEST_RAMP_PLANE_BASE 12
#define TEST_DECK_PLANE_BASE 8
#define TEST_PLANE_NORMAL_SCALE 8192
#define TEST_QUERY_VALUE_COUNT 15U
#define TEST_NO_WALL (-1)
#define TEST_SAVED_WALL_INDEX 31
#define TEST_SAVED_WALL_HEIGHT 123
#define TEST_SAVED_WALL_LOWER_BOUND 234
#define TEST_SAVED_WALL_X 345
#define TEST_SAVED_WALL_Z 456
#define TEST_SAVED_WALL_ORIENTATION 567
#define TEST_SAVED_PLANE_COPY 45
#define TEST_SAVED_NORMAL_PRODUCT 678

static legacy_u8 elements[TRACKDATA_MAP_SIZE];
static legacy_u8 terrain[TRACKDATA_MAP_SIZE];
static struct PLANE planes[TRACK_PLAN_RESOURCE_COUNT];
static struct TRACK_WALL walls[TRACK_WALL_RESOURCE_COUNT];
static const legacy_s16 plane_offsets[TEST_ROTATION_COUNT] = {0, 3, 2, 1};
static const legacy_u8 hill_tiles[TEST_ROTATION_COUNT] = {7, 10, 9, 8};

extern legacy_s8 corkFlag;

static void configure_option(const legacy_s8 *option)
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

static struct VECTOR world_point(struct VECTOR point, legacy_u32 rotation, legacy_s16 elevation)
{
	point = rotate_from_local(point, rotation);
	point.x += TEST_ORIGIN;
	point.y += elevation;
	point.z += TEST_ORIGIN;
	return point;
}

static void place_tile(legacy_s16 x, legacy_s16 z, legacy_u8 element, legacy_u8 ground,
					   legacy_u32 rotation)
{
	struct VECTOR center = {x, 0, z};
	center = world_point(center, rotation, 0);
	legacy_s16 column = center.x >> TRACK_TILE_POSITION_SHIFT;
	legacy_s16 row = center.z >> TRACK_TILE_POSITION_SHIFT;
	elements[terrainrows[row] + column] = element;
	terrain[trackrows[row] + column] = ground;
}

static void initialize_track(legacy_u32 rotation, legacy_s8 model)
{
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
		track_column_centers[index] = index * TRACK_TILE_SIZE + TRACK_TILE_HALF_SIZE;
		terraincenterpos[index] = index * TRACK_TILE_SIZE + TRACK_TILE_HALF_SIZE;
		track_column_positions[index] = index * TRACK_TILE_SIZE;
		terrainpos[index] = index * TRACK_TILE_SIZE;
	}
	for (legacy_u32 tile = TEST_TILE_FIRST; tile <= TEST_TILE_SECOND; tile++) {
		trkObjectList[tile].ss_physicalModel = model;
		trkObjectList[tile].ss_rotY = (legacy_s16)(rotation * ANGLE_QUARTER_TURN);
		trkObjectList[tile].ss_multiTileFlag = 0;
		trkObjectList[tile].ss_surfaceType = 0;
	}

	/* The PLAN ramp origin and normal, transformed as build_track_object does,
	 * plus equivalent horizontal elevated-deck planes. */
	const struct VECTOR ramp_origin = {ROAD_HALF_WIDTH, 0, -TRACK_TILE_HALF_SIZE};
	const struct VECTOR ramp_normal = {0, 7499, -3295};
	const struct VECTOR deck_origin = {0, TEST_DECK_HEIGHT, 0};
	const struct VECTOR deck_normal = {0, TEST_PLANE_NORMAL_SCALE, 0};
	for (legacy_u32 facing = 0; facing < TEST_ROTATION_COUNT; facing++) {
		legacy_s16 ramp = TEST_RAMP_PLANE_BASE + plane_offsets[facing];
		legacy_s16 deck = TEST_DECK_PLANE_BASE + plane_offsets[facing];
		planes[ramp].plane_origin = rotate_from_local(ramp_origin, facing);
		planes[ramp].plane_normal = rotate_from_local(ramp_normal, facing);
		planes[deck].plane_origin = deck_origin;
		planes[deck].plane_normal = deck_normal;
	}
}

static void capture_query(legacy_s16 values[TEST_QUERY_VALUE_COUNT])
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

static void assert_existing_trigger(struct VECTOR *previous, struct VECTOR *current)
{
	build_track_object(previous, current);
	legacy_s16 previous_plane = planindex;
	legacy_s16 previous_distance =
		plane_signed_distance(planindex, previous->x, previous->y, previous->z);
	build_track_object(current, previous);
	legacy_s16 current_distance =
		plane_signed_distance(planindex, current->x, current->y, current->z);
	assert(planindex == previous_plane);
	assert(planindex >= PHYSICS_HEIGHT_ONLY_PLANE_COUNT);
	assert((previous_distance < 0 && current_distance > 0) ||
		   (previous_distance > 0 && current_distance < 0));
}

static void assert_validation(struct VECTOR previous, struct VECTOR current,
							  legacy_s16 expected_hit)
{
	assert_existing_trigger(&previous, &current);

	/* The caller's collision selection can belong to an unrelated query. */
	wallindex = TEST_SAVED_WALL_INDEX;
	wallHeight = TEST_SAVED_WALL_HEIGHT;
	elRdWallRelated = TEST_SAVED_WALL_LOWER_BOUND;
	corkFlag = 1;
	wallStartX = TEST_SAVED_WALL_X;
	wallStartZ = TEST_SAVED_WALL_Z;
	wallOrientation = TEST_SAVED_WALL_ORIENTATION;
	planindex_copy = TEST_SAVED_PLANE_COPY;
	nextPosAndNormalIP = TEST_SAVED_NORMAL_PRODUCT;
	struct VECTOR saved_previous = previous;
	struct VECTOR saved_current = current;
	struct PLANE *saved_plane = current_planptr;
	legacy_s16 before[TEST_QUERY_VALUE_COUNT];
	legacy_s16 after[TEST_QUERY_VALUE_COUNT];
	capture_query(before);
	assert(body_plane_crossing_is_collision(&previous, &current) == expected_hit);
	capture_query(after);
	assert(memcmp(before, after, sizeof(before)) == 0);
	assert(current_planptr == saved_plane);
	assert(memcmp(&previous, &saved_previous, sizeof(previous)) == 0);
	assert(memcmp(&current, &saved_current, sizeof(current)) == 0);
}

static void assert_both_directions(struct VECTOR previous, struct VECTOR current,
								   legacy_s16 expected_hit)
{
	assert_validation(previous, current, expected_hit);
	assert_validation(current, previous, expected_hit);
}

static void test_ramp_hill_alias(legacy_u32 rotation)
{
	initialize_track(rotation, PHYSICAL_MODEL_RAMP);
	place_tile(0, 0, TEST_TILE_FIRST, 0, rotation);
	place_tile(0, TRACK_TILE_SIZE, 0, hill_tiles[rotation], rotation);
	/* crwhy.rpl's body corner, expressed relative to the open ramp. */
	const struct VECTOR hill_point = {48, 34, TRACK_TILE_HALF_SIZE + 1};
	const struct VECTOR ramp_point = {47, 31, TRACK_TILE_HALF_SIZE - 7};
	struct VECTOR previous = world_point(hill_point, rotation, 0);
	struct VECTOR current = world_point(ramp_point, rotation, 0);
	configure_option((const legacy_s8 *)"--lc:off");
	assert_both_directions(previous, current, 0);

	/* A sub-unit positive distance to the ramp must not round away. The true
	 * contact lies just inside its high end even though the old endpoint is
	 * on the hill. Whole-coordinate intersection rounding keeps it inside. */
	const struct VECTOR shallow_impact[] = {{47, 453, TRACK_TILE_HALF_SIZE + 1},
											{47, 449, TRACK_TILE_HALF_SIZE - 4}};
	assert_both_directions(world_point(shallow_impact[0], rotation, 0),
						   world_point(shallow_impact[1], rotation, 0), 1);

	/* This genuine contact is less than one unit inside the side edge. */
	const struct VECTOR side_edge_impact[] = {{-119, 453, TRACK_TILE_HALF_SIZE + 1},
											  {-121, 449, TRACK_TILE_HALF_SIZE - 4}};
	assert_both_directions(world_point(side_edge_impact[0], rotation, 0),
						   world_point(side_edge_impact[1], rotation, 0), 1);

	/* Moving two units farther sideways shifts that contact outside the ramp. */
	const struct VECTOR side_edge_miss[] = {{-119, 453, TRACK_TILE_HALF_SIZE + 1},
											{-123, 449, TRACK_TILE_HALF_SIZE - 4}};
	assert_both_directions(world_point(side_edge_miss[0], rotation, 0),
						   world_point(side_edge_miss[1], rotation, 0), 0);
}

static void test_repeated_ramps(legacy_u32 rotation, legacy_s16 elevation)
{
	initialize_track(rotation, PHYSICAL_MODEL_RAMP);
	legacy_u8 ground = elevation == 0 ? 0 : TERRAIN_RAISED_TILE;
	place_tile(0, 0, TEST_TILE_FIRST, ground, rotation);
	place_tile(0, TRACK_TILE_SIZE, TEST_TILE_SECOND, ground, rotation);
	const struct VECTOR clear_points[] = {{0, 34, TRACK_TILE_HALF_SIZE + 4},
										  {0, 31, TRACK_TILE_HALF_SIZE - 7}};
	configure_option((const legacy_s8 *)"--lc:off");
	assert_both_directions(world_point(clear_points[0], rotation, elevation),
						   world_point(clear_points[1], rotation, elevation), 0);

	/* This steeper movement really cuts through the first ramp's high end.
	 * A change of tile origin must not discard that finite intersection. */
	const struct VECTOR impact_points[] = {{0, 470, 500}, {0, -20, 524}};
	assert_both_directions(world_point(impact_points[0], rotation, elevation),
						   world_point(impact_points[1], rotation, elevation), 1);
}

static void test_deck_height_alias(legacy_u32 rotation)
{
	initialize_track(rotation, PHYSICAL_MODEL_ELEVATED_ROAD);
	place_tile(0, 0, TEST_TILE_FIRST, 0, rotation);
	place_tile(0, TRACK_TILE_SIZE, TEST_TILE_SECOND, TERRAIN_RAISED_TILE, rotation);
	const struct VECTOR points[] = {{0, 860, TRACK_TILE_HALF_SIZE - 4},
									{0, 860, TRACK_TILE_HALF_SIZE + 4}};
	struct VECTOR previous = world_point(points[0], rotation, 0);
	struct VECTOR current = world_point(points[1], rotation, 0);
	build_track_object(&previous, &current);
	assert(plane_signed_distance(planindex, previous.x, previous.y, previous.z) == 408);
	assert(wallindex == TEST_NO_WALL);
	build_track_object(&current, &previous);
	assert(plane_signed_distance(planindex, current.x, current.y, current.z) == -42);
	assert(wallindex == TEST_NO_WALL);
	configure_option((const legacy_s8 *)"--lc:off");
	assert_both_directions(previous, current, 0);
}

static void test_real_deck_crossings(legacy_u32 rotation, legacy_s16 elevation)
{
	initialize_track(rotation, PHYSICAL_MODEL_ELEVATED_ROAD);
	legacy_u8 ground = elevation == 0 ? 0 : TERRAIN_RAISED_TILE;
	place_tile(0, 0, TEST_TILE_FIRST, ground, rotation);
	place_tile(0, TRACK_TILE_SIZE, TEST_TILE_SECOND, ground, rotation);
	const struct VECTOR same_tile[] = {{0, 470, 0}, {0, 420, 0}};
	const struct VECTOR coplanar_seam[] = {
		{0, TEST_DECK_HEIGHT + TEST_PAVED_HEIGHT_OFFSET + 8, TRACK_TILE_HALF_SIZE - 4},
		{0, TEST_DECK_HEIGHT + TEST_PAVED_HEIGHT_OFFSET - 8, TRACK_TILE_HALF_SIZE + 4}};
	configure_option((const legacy_s8 *)"--lc:off");
	assert_both_directions(world_point(same_tile[0], rotation, elevation),
						   world_point(same_tile[1], rotation, elevation), 1);
	/* Distinct element origins can still describe the same world plane. */
	assert_both_directions(world_point(coplanar_seam[0], rotation, elevation),
						   world_point(coplanar_seam[1], rotation, elevation), 1);
}

static void test_real_ramp_crossing(legacy_u32 rotation, legacy_s16 elevation, legacy_s8 model)
{
	initialize_track(rotation, model);
	place_tile(0, 0, TEST_TILE_FIRST, elevation == 0 ? 0 : TERRAIN_RAISED_TILE, rotation);
	const struct VECTOR points[] = {{0, 260, 0}, {0, 190, 0}};
	configure_option((const legacy_s8 *)"--lc:off");
	assert_both_directions(world_point(points[0], rotation, elevation),
						   world_point(points[1], rotation, elevation), 1);
}

static void test_finite_ramp_footprint(legacy_u32 rotation)
{
	initialize_track(rotation, PHYSICAL_MODEL_RAMP);
	place_tile(0, 0, TEST_TILE_FIRST, 0, rotation);
	place_tile(TRACK_TILE_SIZE, 0, TEST_TILE_SECOND, TERRAIN_RAISED_TILE, rotation);
	/* The high ramp's infinite plane crosses this diagonal movement in the
	 * empty gap between the two road strips. Both actual decks stay clear. */
	const struct VECTOR points[] = {{0, 1000, 0}, {TRACK_TILE_SIZE, 500, 0}};
	configure_option((const legacy_s8 *)"--lc:off");
	assert_both_directions(world_point(points[0], rotation, 0), world_point(points[1], rotation, 0),
						   0);
}

static void test_grass_height_variation(void)
{
	initialize_track(0, PHYSICAL_MODEL_RAMP);
	place_tile(0, 0, 0, HILL_TERRAIN_FIRST, 0);
	const struct VECTOR points[] = {{-1, 228, 0}, {0, 223, 0}};
	struct VECTOR previous = world_point(points[0], 0, 0);
	struct VECTOR current = world_point(points[1], 0, 0);
	build_track_object(&previous, &current);
	legacy_s16 previous_height = terrainHeight;
	build_track_object(&current, &previous);
	assert(terrainHeight != previous_height);
	configure_option((const legacy_s8 *)"--lc:off");
	assert_both_directions(previous, current, 1);
}

static void test_mode_gate(void)
{
	initialize_track(0, PHYSICAL_MODEL_ELEVATED_ROAD);
	place_tile(0, 0, TEST_TILE_FIRST, 0, 0);
	place_tile(0, TRACK_TILE_SIZE, TEST_TILE_SECOND, TERRAIN_RAISED_TILE, 0);
	const struct VECTOR points[] = {{0, 860, TRACK_TILE_HALF_SIZE - 4},
									{0, 860, TRACK_TILE_HALF_SIZE + 4}};
	struct VECTOR previous = world_point(points[0], 0, 0);
	struct VECTOR current = world_point(points[1], 0, 0);
	/* Run first to exercise static startup before any explicit configuration. */
	assert_both_directions(previous, current, 1);
	configure_option((const legacy_s8 *)"--lc:off");
	assert_both_directions(previous, current, 0);
	configure_option((const legacy_s8 *)"--lc:on");
	assert_both_directions(previous, current, 1);
	configure_option(NULL);
	assert_both_directions(previous, current, 1);
}

legacy_int main(void)
{
	test_mode_gate();
	test_grass_height_variation();
	for (legacy_u32 rotation = 0; rotation < TEST_ROTATION_COUNT; rotation++) {
		test_ramp_hill_alias(rotation);
		test_deck_height_alias(rotation);
		test_finite_ramp_footprint(rotation);
		for (legacy_s16 elevation = 0; elevation <= TEST_DECK_HEIGHT;
			 elevation += TEST_DECK_HEIGHT) {
			test_repeated_ramps(rotation, elevation);
			test_real_deck_crossings(rotation, elevation);
			test_real_ramp_crossing(rotation, elevation, PHYSICAL_MODEL_RAMP);
			test_real_ramp_crossing(rotation, elevation, PHYSICAL_MODEL_SOLID_RAMP);
		}
	}
	return 0;
}
