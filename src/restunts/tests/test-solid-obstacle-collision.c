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
								 legacy_s32 stone)
{
	point.x *= stone;
	point.z *= stone;
	point = rotate_from_local(point, rotation);
	point.x += TEST_ORIGIN;
	point.y += elevation;
	point.z += TEST_ORIGIN;
	return point;
}

static struct VECTORLONG fixed_point(struct VECTOR point)
{
	struct VECTORLONG result = {(legacy_s32)point.x * 64, (legacy_s32)point.y * 64,
								(legacy_s32)point.z * 64};
	return result;
}

static struct VECTOR retained_position(struct VECTORLONG *previous, struct VECTORLONG *current,
									   legacy_s16 fraction)
{
	/* Match clipping of the actual wheels before converting to world coordinates. */
	struct VECTOR result;
	result.x = position_to_word(
		previous->lx + scale_position_delta(current->lx, previous->lx, fraction, TRIG_FIXED_ONE));
	result.y = position_to_word(
		previous->ly + scale_position_delta(current->ly, previous->ly, fraction, TRIG_FIXED_ONE));
	result.z = position_to_word(
		previous->lz + scale_position_delta(current->lz, previous->lz, fraction, TRIG_FIXED_ONE));
	return result;
}

static void initialize_slalom(legacy_u32 rotation, legacy_s16 elevation)
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
		track_column_centers[index] = index * 1024 + 512;
		terraincenterpos[index] = index * 1024 + 512;
		track_column_positions[index] = index * 1024;
		terrainpos[index] = index * 1024;
	}
	elements[terrainrows[TEST_ROW] + TEST_COLUMN] = 1;
	terrain[trackrows[TEST_ROW] + TEST_COLUMN] = elevation == 0 ? 0 : TERRAIN_RAISED_TILE;
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_SLALOM;
	trkObjectList[1].ss_rotY = (legacy_s16)(rotation * 256);
	trkObjectList[1].ss_multiTileFlag = 0;
	trkObjectList[1].ss_surfaceType = 0;
	static const struct TRACK_WALL fixture[] = {{256, -97, 271}, {768, -23, 241}, {0, -23, 271},
												{512, -97, 241}, {768, 97, -271}, {256, 23, -241},
												{0, 97, -241},	 {512, 23, -271}};
	for (legacy_u32 index = 0; index < sizeof(fixture) / sizeof(fixture[0]); index++) {
		walls[141 + index] = fixture[index];
	}

	/* Unrelated live query state must survive every speculative collision query. */
	planindex = 12;
	current_planptr = &planes[12];
	wallindex = 31;
	wallHeight = 123;
	elRdWallRelated = 234;
	corkFlag = 1;
	current_surf_type = 3;
	track_wall_collision_enabled = 0;
	terrainHeight = 47;
	elem_xCenter = 1234;
	elem_zCenter = 2345;
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

static void assert_contact(struct VECTOR first, struct VECTOR second, legacy_s16 expected_hit,
						   legacy_s16 expected_fraction)
{
	struct VECTOR saved_first = first;
	struct VECTOR saved_second = second;
	struct PLANE *saved_plane = current_planptr;
	legacy_s16 before[QUERY_VALUE_COUNT];
	legacy_s16 after[QUERY_VALUE_COUNT];
	capture_query(before);
	legacy_s16 fraction = -1;
	assert(track_solid_obstacle_contact(&first, &second, &fraction) == expected_hit);
	if (expected_hit != 0) {
		assert(fraction >= 0 && fraction <= TRIG_FIXED_ONE);
		if (expected_fraction >= 0) {
			assert(fraction >= expected_fraction - 1 && fraction <= expected_fraction + 1);
		}
	}
	capture_query(after);
	assert(memcmp(before, after, sizeof(before)) == 0);
	assert(current_planptr == saved_plane);
	assert(memcmp(&first, &saved_first, sizeof(first)) == 0);
	assert(memcmp(&second, &saved_second, sizeof(second)) == 0);
}

static void assert_sweep(struct VECTORLONG previous, struct VECTORLONG current,
						 legacy_s16 expected_hit)
{
	struct VECTORLONG saved_previous = previous;
	struct VECTORLONG saved_current = current;
	struct PLANE *saved_plane = current_planptr;
	legacy_s16 before[QUERY_VALUE_COUNT];
	legacy_s16 after[QUERY_VALUE_COUNT];
	capture_query(before);
	legacy_s16 fraction = -1;
	assert(sweep_track_solid_obstacle(&previous, &current, &fraction) == expected_hit);
	if (expected_hit != 0) {
		assert(fraction >= 0 && fraction < TRIG_FIXED_ONE);
		struct VECTOR start = retained_position(&previous, &current, 0);
		struct VECTOR stop = retained_position(&previous, &current, fraction);
		legacy_s16 contact_fraction;
		assert(!track_solid_obstacle_contact(&start, &stop, &contact_fraction));
		stop = retained_position(&previous, &current, fraction + 1);
		assert(track_solid_obstacle_contact(&start, &stop, &contact_fraction));
	}
	capture_query(after);
	assert(memcmp(before, after, sizeof(before)) == 0);
	assert(current_planptr == saved_plane);
	assert(memcmp(&previous, &saved_previous, sizeof(previous)) == 0);
	assert(memcmp(&current, &saved_current, sizeof(current)) == 0);
}

static void test_mode_gate(void)
{
	initialize_slalom(0, 0);
	struct VECTOR first = {60, 2, -400};
	struct VECTOR second = {60, 2, -240};
	struct VECTORLONG previous = fixed_point(world_point(first, 0, 0, 1));
	struct VECTORLONG current = fixed_point(world_point(second, 0, 0, 1));
	assert_sweep(previous, current, 0);
	configure_option("--lc:on");
	assert_sweep(previous, current, 0);
	configure_option("--lc:off");
	assert_sweep(previous, current, 1);
	configure_option(NULL);
	assert_sweep(previous, current, 0);
}

static void test_solid_geometry(legacy_u32 rotation, legacy_s16 elevation, legacy_s32 stone)
{
	static const struct {
		struct VECTOR first;
		struct VECTOR second;
		legacy_s16 hit;
	} cases[] = {
		/* Both endpoints and the midpoint are outside this brief contact interval. */
		{{60, 2, -400}, {60, 2, -240}, 1},
		{{60, 2, -240}, {60, 2, -400}, 1},
		/* Enter from either side, vertically, or across a track tile boundary. */
		{{0, 10, -256}, {120, 10, -256}, 1},
		{{120, 10, -256}, {0, 10, -256}, 1},
		{{60, -20, -256}, {60, 70, -256}, 1},
		{{60, 2, -600}, {60, 2, -200}, 1},
		/* SLALBUG frame 255's two front wheel paths in the element's coordinates. */
		{{56, 1, -292}, {46, -1, -237}, 1},
		{{87, 4, -288}, {78, 2, -232}, 1},
		/* Preserve clear passages beside, above and below the finite stone. */
		{{22, 2, -300}, {22, 2, -220}, 0},
		{{98, 2, -300}, {98, 2, -220}, 0},
		{{60, 45, -300}, {60, 45, -220}, 0},
		{{60, -1, -300}, {60, -1, -220}, 0},
		/* Pure face/corner grazes and endpoints on the entry boundary are clear. */
		{{23, 2, -300}, {23, 2, -220}, 0},
		{{97, 2, -300}, {97, 2, -220}, 0},
		{{60, 0, -300}, {60, 0, -220}, 0},
		{{60, 44, -300}, {60, 44, -220}, 0},
		{{0, 10, -271}, {120, 10, -271}, 0},
		{{0, 10, -248}, {46, 10, -294}, 0},
		{{60, 2, -300}, {60, 2, -271}, 0},
		/* Starting on a face and then entering is a new contact. */
		{{60, 2, -271}, {60, 2, -240}, 1},
	};
	initialize_slalom(rotation, elevation);
	configure_option("--lc:off");
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		struct VECTOR first = world_point(cases[index].first, rotation, elevation, stone);
		struct VECTOR second = world_point(cases[index].second, rotation, elevation, stone);
		assert_contact(first, second, cases[index].hit, -1);
		assert_sweep(fixed_point(first), fixed_point(second), cases[index].hit);
	}

	struct VECTOR inside = {60, 2, -256};
	struct VECTOR outside = {60, 2, -220};
	inside = world_point(inside, rotation, elevation, stone);
	outside = world_point(outside, rotation, elevation, stone);
	assert_contact(inside, inside, 1, 0);
	assert_contact(inside, outside, 1, 0);
	assert_sweep(fixed_point(inside), fixed_point(inside), 0);
	assert_sweep(fixed_point(inside), fixed_point(outside), 0);

	/* The segment helper must also find finite walls when neither end selects one. */
	struct VECTOR first = {60, 2, -300};
	struct VECTOR second = {60, 2, -220};
	first = world_point(first, rotation, elevation, stone);
	second = world_point(second, rotation, elevation, stone);
	struct VECTOR saved_first = first;
	struct VECTOR saved_second = second;
	struct PLANE *saved_plane = current_planptr;
	legacy_s16 before[QUERY_VALUE_COUNT];
	legacy_s16 after[QUERY_VALUE_COUNT];
	capture_query(before);
	assert(track_wall_intersects_segment(&first, &second));
	assert(track_wall_intersects_segment(&second, &first));
	capture_query(after);
	assert(memcmp(before, after, sizeof(before)) == 0);
	assert(current_planptr == saved_plane);
	assert(memcmp(&first, &saved_first, sizeof(first)) == 0);
	assert(memcmp(&second, &saved_second, sizeof(second)) == 0);

	/* Contact comes late in the path; a midpoint-only sweep would miss it. */
	first = world_point((struct VECTOR){60, 2, -400}, rotation, elevation, stone);
	second = world_point((struct VECTOR){60, 2, -240}, rotation, elevation, stone);
	assert_contact(first, second, 1, 129 * TRIG_FIXED_ONE / 160);
	struct VECTORLONG previous = fixed_point(first);
	struct VECTORLONG current = fixed_point(second);
	previous.lx += 13;
	previous.ly += 31;
	previous.lz += 59;
	current.lx += 51;
	current.ly += 7;
	current.lz += 3;
	assert_sweep(previous, current, 1);

	/* A long diagonal crosses both stones. Return the first, in either direction. */
	first = world_point((struct VECTOR){60, 10, -400}, rotation, elevation, stone);
	second = world_point((struct VECTOR){-60, 10, 400}, rotation, elevation, stone);
	assert_contact(first, second, 1, 129 * TRIG_FIXED_ONE / 800);
	assert_contact(second, first, 1, 129 * TRIG_FIXED_ONE / 800);

	/* The exact interior interval can be shorter than one Q14 fraction step.
	 * Entering X at 1/18978 precedes leaving Y at 1/18977; equal times only graze. */
	first = world_point((struct VECTOR){22, 1, -256}, rotation, elevation, stone);
	second = world_point((struct VECTOR){19000, -18976, -256}, rotation, elevation, stone);
	assert_contact(first, second, 1, 0);
	second = world_point((struct VECTOR){19000, -18977, -256}, rotation, elevation, stone);
	assert_contact(first, second, 0, -1);
}

int main(void)
{
	test_mode_gate();
	for (legacy_u32 rotation = 0; rotation < 4; rotation++) {
		for (legacy_s32 stone = -1; stone <= 1; stone += 2) {
			test_solid_geometry(rotation, 0, stone);
			test_solid_geometry(rotation, 450, stone);
		}
	}
	return 0;
}
