#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../c/externs.h"
#include "../c/extended/extended_track.h"
#include "../c/opponent/unit_shortcut.h"
#include "../c/track/track_objects.h"
#include "../c/track/trackdata_layout.h"

#undef memcpy

#define TEST_MAP_SIZE 30U
#define TEST_MAP_CELLS (TEST_MAP_SIZE * TEST_MAP_SIZE)
#define TEST_VERTEX_SIDE (TEST_MAP_SIZE + 1U)
#define TEST_VERTEX_COUNT (TEST_VERTEX_SIDE * TEST_VERTEX_SIDE)
#define TEST_ROUTE_COUNT 3U
#define TEST_POINT_COUNT 5U
#define TEST_ROUTE_PIECE 1U
#define TEST_ROAD_ID 7U
#define TEST_COLUMN 5U
#define TEST_ROW 5U
#define TEST_MIDDLE_POINT 2U
#define TEST_NO_SIGN LEGACY_U8_MAX
#define TEST_CAR_HALF_WIDTH 56.0
#define TEST_CAR_HALF_LENGTH 62.0
#define TEST_CAR_MAX_SPEED 246.7
#define TEST_ROAD_HALF_WIDTH 120.0

struct TRACKOBJECT trkObjectList[215];
legacy_s16 track_pieces_counter;
legacy_u16 framespersec;
legacy_s8 *track_route_element_ids;
legacy_s8 *track_route_columns;
legacy_s8 *track_route_rows;
legacy_s16 *track_alternate_route_links;
legacy_s16 trackrows[TEST_MAP_SIZE];
legacy_s16 terrainrows[TEST_MAP_SIZE];
legacy_u8 *track_terrain_map;
legacy_u8 *roadside_sign_indices_by_tile;

static legacy_s8 elements[TEST_ROUTE_COUNT], columns[TEST_ROUTE_COUNT], rows[TEST_ROUTE_COUNT];
static legacy_s16 alternate[TEST_ROUTE_COUNT];
static legacy_u8 terrain[TEST_MAP_CELLS], signs[TEST_MAP_CELLS];
static struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
static struct UNIT_CAR_LIMITS limits;
static struct CSTRK_TRACK model;
static struct CSTRK_OBJECT object, other_object;
static struct CSTRK_CELL cells[TEST_MAP_CELLS];
static legacy_u8 heights[TEST_VERTEX_COUNT];
static legacy_u8 extended_active, missing_owner, wrong_owner, extra_object;

legacy_int extended_track_active(void)
{
	return extended_active;
}

legacy_int extended_track_width(void)
{
	return TEST_MAP_SIZE;
}

legacy_int extended_track_height(void)
{
	return TEST_MAP_SIZE;
}

const struct CSTRK_TRACK *extended_track_model(void)
{
	return extended_active ? &model : NULL;
}

const struct CSTRK_OBJECT *extended_track_object_for_id(legacy_int column, legacy_int row,
														legacy_uint id)
{
	return column == object.x && row == object.y && id == TEST_ROAD_ID ? &object : NULL;
}

const struct CSTRK_OBJECT *extended_track_object_at(legacy_int column, legacy_int row)
{
	if (column < object.x || row < object.y || column >= object.x + object.width ||
		row >= object.y + object.height) {
		return NULL;
	}
	legacy_uint bit = (legacy_uint)(row - object.y) * object.width + column - object.x;
	if (!(object.occupied_mask & (1U << bit))) {
		return NULL;
	}
	if (missing_owner && column == object.x + object.width - 1 &&
		row == object.y + object.height - 1) {
		return NULL;
	}
	return wrong_owner ? &other_object : &object;
}

legacy_uint extended_track_object_id(const struct CSTRK_OBJECT *placed)
{
	return placed == &object ? TEST_ROAD_ID : TEST_ROAD_ID + 1U;
}

legacy_u16 extended_track_objects_in_cell(legacy_s16 column, legacy_s16 row,
										  const struct CSTRK_OBJECT **objects, legacy_u16 capacity)
{
	assert(column >= object.x && column < object.x + object.width);
	assert(row >= object.y && row < object.y + object.height);
	assert(capacity > 0);
	objects[0] = &object;
	if (extra_object && capacity > 1U) {
		objects[1] = &other_object;
		return 2;
	}
	return 1;
}

legacy_int cstrk_is_checkpoint(legacy_u16 id)
{
	return id == CSTRK_BLUE_CHECKPOINT_ID || id == CSTRK_RED_CHECKPOINT_ID ||
		   id == CSTRK_COMBINED_CHECKPOINT_ID;
}

static void reset_fixture(void)
{
	static const legacy_s16 sharp_centers[TEST_POINT_COUNT][2] = {
		{-17, -382}, {-68, -257}, {-150, -150}, {-255, -70}, {-379, -18}};
	memset(trkObjectList, 0, sizeof(trkObjectList));
	memset(terrain, 0, sizeof(terrain));
	memset(signs, TEST_NO_SIGN, sizeof(signs));
	memset(points, 0, sizeof(points));
	memset(&limits, 0, sizeof(limits));
	memset(&model, 0, sizeof(model));
	memset(&object, 0, sizeof(object));
	memset(&other_object, 0, sizeof(other_object));
	memset(cells, 0, sizeof(cells));
	memset(heights, 0, sizeof(heights));
	for (legacy_u16 index = 0; index < TEST_MAP_SIZE; index++) {
		trackrows[index] = index * TEST_MAP_SIZE;
		terrainrows[index] = (TEST_MAP_SIZE - 1U - index) * TEST_MAP_SIZE;
	}
	for (legacy_u16 index = 0; index < TEST_ROUTE_COUNT; index++) {
		elements[index] = TEST_ROAD_ID;
		columns[index] = TEST_COLUMN;
		rows[index] = TEST_ROW;
		alternate[index] = TRACK_ROUTE_LINK_NONE;
	}
	framespersec = GAME_FRAME_RATE_NORMAL;
	track_pieces_counter = TEST_ROUTE_COUNT;
	track_route_element_ids = elements;
	track_route_columns = columns;
	track_route_rows = rows;
	track_alternate_route_links = alternate;
	track_terrain_map = terrain;
	roadside_sign_indices_by_tile = signs;
	trkObjectList[TEST_ROAD_ID].ss_physicalModel = PHYSICAL_MODEL_SHARP_CORNER;
	limits.half_width = TEST_CAR_HALF_WIDTH;
	limits.half_length = TEST_CAR_HALF_LENGTH;
	limits.max_speed = TEST_CAR_MAX_SPEED;
	for (legacy_u16 index = 0; index < TEST_POINT_COUNT; index++) {
		points[index].x =
			TEST_COLUMN * TRACK_TILE_SIZE + TRACK_TILE_HALF_SIZE + sharp_centers[index][0];
		points[index].z = (TEST_MAP_SIZE - TEST_ROW - 1U) * TRACK_TILE_SIZE + TRACK_TILE_HALF_SIZE +
						  sharp_centers[index][1];
		points[index].half_width = TEST_ROAD_HALF_WIDTH;
		points[index].route_index = TEST_ROUTE_PIECE;
		points[index].route_point = index;
		points[index].surface = CAR_SURFACE_PAVED;
		points[index].shortcut_safe = 1;
	}
	model.width = TEST_MAP_SIZE;
	model.height = TEST_MAP_SIZE;
	model.corner_heights = heights;
	model.cells = cells;
	model.objects = &object;
	model.object_count = 1;
	object.legacy_id = TEST_ROAD_ID;
	object.x = TEST_COLUMN;
	object.y = TEST_ROW;
	object.width = object.height = object.occupied_mask = 1;
	other_object = object;
	other_object.legacy_id = TEST_ROAD_ID + 1U;
	extended_active = missing_owner = wrong_owner = extra_object = 0;
}

static void assert_no_shortcut(void)
{
	unit_shortcut_mark_route(points, TEST_POINT_COUNT, &limits, 0);
	for (legacy_u16 index = 0; index < TEST_POINT_COUNT; index++) {
		assert(points[index].shortcut_safe == 0);
	}
}

static void test_native_corner(void)
{
	reset_fixture();
	struct UNIT_ROUTE_POINT original[TEST_POINT_COUNT];
	memcpy(original, points, sizeof(original));
	unit_shortcut_mark_route(points, TEST_POINT_COUNT, &limits, 0);
	for (legacy_u16 index = 0; index < TEST_POINT_COUNT; index++) {
		assert(points[index].shortcut_safe == (index == TEST_MIDDLE_POINT));
		original[index].shortcut_safe = points[index].shortcut_safe;
	}
	assert(memcmp(original, points, sizeof(original)) == 0);
	unit_shortcut_mark_route(points, TEST_POINT_COUNT, &limits, 1);
	for (legacy_u16 index = 0; index < TEST_POINT_COUNT; index++) {
		assert(points[index].shortcut_safe == 0);
	}
}

static void test_physics_frame_clearance(void)
{
	reset_fixture();
	unit_shortcut_mark_route(points, TEST_POINT_COUNT, &limits, 0);
	assert(points[TEST_MIDDLE_POINT].shortcut_safe != 0);
	/* The same narrow corner fits one 20 Hz tick of travel, but cannot safely
	 * permit the longer step at 10 Hz. Returning to 20 Hz restores eligibility. */
	framespersec = GAME_FRAME_RATE_LOW;
	assert_no_shortcut();
	framespersec = GAME_FRAME_RATE_NORMAL;
	unit_shortcut_mark_route(points, TEST_POINT_COUNT, &limits, 0);
	assert(points[TEST_MIDDLE_POINT].shortcut_safe != 0);
	/* An unset rate uses the conservative supported physics period. */
	framespersec = 0;
	assert_no_shortcut();
}

static void test_native_rejections(void)
{
	reset_fixture();
	for (legacy_u16 index = 0; index < TEST_POINT_COUNT; index++) {
		points[index].route_index = 0;
	}
	assert_no_shortcut();
	reset_fixture();
	for (legacy_u16 index = 0; index < TEST_POINT_COUNT; index++) {
		points[index].route_index = TEST_ROUTE_COUNT;
	}
	assert_no_shortcut();
	reset_fixture();
	points[TEST_MIDDLE_POINT].route_index = TEST_ROUTE_PIECE + 1U;
	assert_no_shortcut();
	reset_fixture();
	points[TEST_MIDDLE_POINT].x = TEST_COLUMN * TRACK_TILE_SIZE;
	assert_no_shortcut();
	reset_fixture();
	limits.half_length = TRACK_TILE_SIZE;
	assert_no_shortcut();
	reset_fixture();
	limits.half_width = NAN;
	assert_no_shortcut();
	reset_fixture();
	limits.max_speed = INFINITY;
	assert_no_shortcut();
	reset_fixture();
	points[TEST_MIDDLE_POINT].z = NAN;
	assert_no_shortcut();
	reset_fixture();
	alternate[TEST_ROUTE_PIECE] = TEST_ROUTE_PIECE + 1U;
	assert_no_shortcut();
	reset_fixture();
	trkObjectList[TEST_ROAD_ID].ss_ssOvelay = 1;
	assert_no_shortcut();
	static const legacy_s8 rejected_models[] = {
		PHYSICAL_MODEL_HIGHWAY, PHYSICAL_MODEL_HIGHWAY_ENTRANCE, PHYSICAL_MODEL_LOOP,
		PHYSICAL_MODEL_TUNNEL,	PHYSICAL_MODEL_ELEVATED_CORNER,	 PHYSICAL_MODEL_BANKED_CORNER};
	for (legacy_u16 index = 0; index < sizeof(rejected_models) / sizeof(rejected_models[0]);
		 index++) {
		reset_fixture();
		trkObjectList[TEST_ROAD_ID].ss_physicalModel = rejected_models[index];
		assert_no_shortcut();
	}
	reset_fixture();
	signs[trackrows[TEST_ROW] + TEST_COLUMN] = 0;
	assert_no_shortcut();
	reset_fixture();
	terrain[terrainrows[TEST_ROW] + TEST_COLUMN] = HILL_TERRAIN_FIRST;
	assert_no_shortcut();
}

static void test_extended_corner(void)
{
	reset_fixture();
	extended_active = 1;
	unit_shortcut_mark_route(points, TEST_POINT_COUNT, &limits, 0);
	assert(points[TEST_MIDDLE_POINT].shortcut_safe != 0);
	wrong_owner = 1;
	assert_no_shortcut();
	wrong_owner = 0;
	missing_owner = 1;
	assert_no_shortcut();
	missing_owner = 0;
	extra_object = 1;
	assert_no_shortcut();
	extra_object = 0;
	object.occupied_mask = 0;
	assert_no_shortcut();
	object.occupied_mask = 1;
	object.legacy_id = CSTRK_COMBINED_CHECKPOINT_ID;
	assert_no_shortcut();
	object.legacy_id = TEST_ROAD_ID;
	object.slope_x = 1;
	assert_no_shortcut();
	object.slope_x = 0;
	object.roll_ramp = 1;
	assert_no_shortcut();
	object.roll_ramp = 0;
	object.tunnel_slalom = 1;
	assert_no_shortcut();
	object.tunnel_slalom = 0;
	object.pine_count = 1;
	assert_no_shortcut();
	object.pine_count = 0;
	object.lower_surface = CSTRK_ROAD_ICE;
	assert_no_shortcut();
	object.lower_surface = CSTRK_ROAD_PAVED;
	object.base_height = 1;
	assert_no_shortcut();
	object.base_height = 0;
	heights[TEST_ROW * TEST_VERTEX_SIDE + TEST_COLUMN] = 1;
	assert_no_shortcut();
	heights[TEST_ROW * TEST_VERTEX_SIDE + TEST_COLUMN] = 0;
	struct CSTRK_CELL *cell = &cells[TEST_ROW * TEST_MAP_SIZE + TEST_COLUMN];
	cell->half_water_corner = CSTRK_WATER_NORTHWEST;
	assert_no_shortcut();
	cell->half_water_corner = CSTRK_WATER_NONE;
	for (legacy_u8 surface = CSTRK_TERRAIN_ICE; surface <= CSTRK_TERRAIN_WATER; surface++) {
		cell->surface = surface;
		assert_no_shortcut();
	}
}

static void test_extended_raised_plateau(void)
{
	reset_fixture();
	extended_active = 1;
	object.base_height = 1;
	for (legacy_u16 row = TEST_ROW; row <= TEST_ROW + object.height; row++) {
		for (legacy_u16 column = TEST_COLUMN; column <= TEST_COLUMN + object.width; column++) {
			heights[row * TEST_VERTEX_SIDE + column] = object.base_height;
		}
	}
	for (legacy_u16 index = 0; index < TEST_POINT_COUNT; index++) {
		points[index].y = object.base_height * CSTRK_LEVEL_WORLD_HEIGHT;
	}
	unit_shortcut_mark_route(points, TEST_POINT_COUNT, &limits, 0);
	assert(points[TEST_MIDDLE_POINT].shortcut_safe != 0);
	points[TEST_MIDDLE_POINT].y = 0;
	assert_no_shortcut();
}

static void test_extended_full_footprint(void)
{
	reset_fixture();
	extended_active = 1;
	object.width = object.height = 2;
	object.occupied_mask = (1U << (object.width * object.height)) - 1U;
	trkObjectList[TEST_ROAD_ID].ss_multiTileFlag =
		MULTI_TILE_ROW_EDGE_FLAG | MULTI_TILE_COLUMN_EDGE_FLAG;
	trkObjectList[TEST_ROAD_ID].ss_physicalModel = PHYSICAL_MODEL_LARGE_CORNER;
	unit_shortcut_mark_route(points, TEST_POINT_COUNT, &limits, 0);
	assert(points[TEST_MIDDLE_POINT].shortcut_safe != 0);
	/* The lower-right cell does not contain these samples. Eligibility still
	 * requires every cell of the claimed convex rectangle to have its owner. */
	missing_owner = 1;
	assert_no_shortcut();
	missing_owner = 0;
	object.occupied_mask &= ~(1U << (object.width * object.height - 1U));
	assert_no_shortcut();
	object.occupied_mask = (1U << (object.width * object.height)) - 1U;
	heights[(TEST_ROW + object.height) * TEST_VERTEX_SIDE + TEST_COLUMN + object.width] = 1;
	assert_no_shortcut();
}

legacy_int main(void)
{
	test_native_corner();
	test_physics_frame_clearance();
	test_native_rejections();
	test_extended_corner();
	test_extended_raised_plateau();
	test_extended_full_footprint();
	puts("UNIT shortcut eligibility tests passed");
	return 0;
}
