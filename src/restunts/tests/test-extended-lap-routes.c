#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../c/extended_track.h"
#include "../c/externs.h"
#include "../c/memmgr.h"
#include "../c/track_objects.h"

#define TEST_CAMERA_CAPACITY 64U
#define TEST_BRANCH_MAP_SIZE 20U
#define TEST_BRANCH_LEFT 5U
#define TEST_BRANCH_RIGHT 12U
#define TEST_BRANCH_TOP 5U
#define TEST_BRANCH_BOTTOM 12U
#define TEST_BRANCH_ROW 8U
#define TEST_RECTANGLE_TILE_COUNT 28U

static struct VECTOR cameras[TEST_CAMERA_CAPACITY], signs[TEST_CAMERA_CAPACITY];
static legacy_s16 camera_height[TEST_CAMERA_CAPACITY], camera_words[TEST_CAMERA_CAPACITY];
static legacy_s16 sign_headings[TEST_CAMERA_CAPACITY];
static legacy_u8 sign_shapes[TEST_CAMERA_CAPACITY];

void far *mmgr_alloc_resbytes(const legacy_s8 *name, legacy_s32 size)
{
	(void)name;
	assert(size > 0);
	return malloc((size_t)size);
}

void mmgr_release(void far *pointer)
{
	free(pointer);
}

static void put(struct CSTRK_TRACK *track, legacy_u16 id, legacy_u8 column, legacy_u8 row)
{
	for (legacy_u32 index = 0; index < track->object_count; index++) {
		if (track->objects[index].x == column && track->objects[index].y == row) {
			track->objects[index].legacy_id = id;
			return;
		}
	}
	struct CSTRK_OBJECT object = {0};
	object.legacy_id = id;
	object.x = column;
	object.y = row;
	object.width = object.height = object.occupied_mask = 1;
	assert(cstrk_add_object(track, &object, NULL, 0));
}

static void rectangle(struct CSTRK_TRACK *track)
{
	cstrk_init(track);
	assert(cstrk_create(track, TEST_BRANCH_MAP_SIZE, TEST_BRANCH_MAP_SIZE, NULL, 0));
	for (legacy_u8 column = TEST_BRANCH_LEFT + 1U; column < TEST_BRANCH_RIGHT; column++) {
		put(track, 5, column, TEST_BRANCH_TOP);
		put(track, 5, column, TEST_BRANCH_BOTTOM);
	}
	for (legacy_u8 row = TEST_BRANCH_TOP + 1U; row < TEST_BRANCH_BOTTOM; row++) {
		put(track, 4, TEST_BRANCH_LEFT, row);
		put(track, 4, TEST_BRANCH_RIGHT, row);
	}
	put(track, 6, TEST_BRANCH_LEFT, TEST_BRANCH_TOP);
	put(track, 7, TEST_BRANCH_RIGHT, TEST_BRANCH_TOP);
	put(track, 8, TEST_BRANCH_LEFT, TEST_BRANCH_BOTTOM);
	put(track, 9, TEST_BRANCH_RIGHT, TEST_BRANCH_BOTTOM);
	put(track, 1, TEST_BRANCH_LEFT, TEST_BRANCH_TOP + 1U);
}

static legacy_u16 selected_count(void)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	const legacy_u8 *tiles = extended_track_lap_tiles();
	assert(track != NULL && tiles != NULL);
	legacy_u16 count = 0;
	for (legacy_u16 cell = 0; cell < track->width * track->height; cell++) {
		count += tiles[cell] != 0;
	}
	return count;
}

static void activate(const struct CSTRK_TRACK *track)
{
	assert(extended_track_activate(track, NULL, 0));
	assert(selected_count() == 0);
}

static void assert_rectangle(void)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	const legacy_u8 *tiles = extended_track_lap_tiles();
	for (legacy_u16 row = 0; row < track->height; row++) {
		for (legacy_u16 column = 0; column < track->width; column++) {
			legacy_s16 on_rectangle =
				((column == TEST_BRANCH_LEFT || column == TEST_BRANCH_RIGHT) &&
				 row >= TEST_BRANCH_TOP && row <= TEST_BRANCH_BOTTOM) ||
				((row == TEST_BRANCH_TOP || row == TEST_BRANCH_BOTTOM) &&
				 column >= TEST_BRANCH_LEFT && column <= TEST_BRANCH_RIGHT);
			assert((tiles[row * track->width + column] != 0) == on_rectangle);
		}
	}
}

static void test_closed_lap_and_disconnected_roads(void)
{
	struct CSTRK_TRACK track;
	rectangle(&track);
	put(&track, 4, 18, 1);
	put(&track, 4, 18, 2);
	activate(&track);
	assert(track_setup() == TRACK_SETUP_OK);
	assert(selected_count() == TEST_RECTANGLE_TILE_COUNT);
	assert_rectangle();
	const legacy_u8 *cached = extended_track_lap_tiles();
	assert(extended_track_lap_tiles() == cached);
	cstrk_free(&track);
}

static void branch(struct CSTRK_TRACK *track, legacy_u16 first_spur_id)
{
	rectangle(track);
	put(track, 75, TEST_BRANCH_LEFT, TEST_BRANCH_ROW);
	for (legacy_u8 column = 2; column < TEST_BRANCH_LEFT; column++) {
		put(track, column == TEST_BRANCH_LEFT - 1U ? first_spur_id : 5, column, TEST_BRANCH_ROW);
	}
}

static void test_dead_branch_in_both_visit_orders(void)
{
	struct TRACKOBJECT original = trkObjectList[75];
	for (legacy_u16 reverse = 0; reverse < 2U; reverse++) {
		struct CSTRK_TRACK track;
		branch(&track, 5);
		activate(&track);
		struct TRKOBJINFO choices[2];
		choices[0] = original.ss_trkObjInfoPtr[reverse];
		choices[1] = original.ss_trkObjInfoPtr[1U - reverse];
		choices[0].si_noOfBlocks = 2;
		choices[1].si_noOfBlocks = 0;
		trkObjectList[75].ss_trkObjInfoPtr = choices;
		assert(track_setup() == TRACK_SETUP_OK);
		assert((legacy_u16)track_pieces_counter > TEST_RECTANGLE_TILE_COUNT);
		assert(selected_count() == TEST_RECTANGLE_TILE_COUNT);
		assert_rectangle();
		trkObjectList[75] = original;
		cstrk_free(&track);
	}
}

static void test_valid_fork(void)
{
	struct CSTRK_TRACK track;
	rectangle(&track);
	put(&track, 79, TEST_BRANCH_LEFT, TEST_BRANCH_ROW);
	put(&track, 75, TEST_BRANCH_RIGHT, TEST_BRANCH_ROW);
	for (legacy_u8 column = TEST_BRANCH_LEFT + 1U; column < TEST_BRANCH_RIGHT; column++) {
		put(&track, 5, column, TEST_BRANCH_ROW);
	}
	activate(&track);
	assert(track_setup() == TRACK_SETUP_OK);
	assert(selected_count() ==
		   TEST_RECTANGLE_TILE_COUNT + TEST_BRANCH_RIGHT - TEST_BRANCH_LEFT - 1U);
	for (legacy_u16 column = TEST_BRANCH_LEFT; column <= TEST_BRANCH_RIGHT; column++) {
		assert(extended_track_lap_tiles()[TEST_BRANCH_ROW * track.width + column] != 0);
	}
	cstrk_free(&track);
}

static void test_invalid_branch_clears_membership_in_both_visit_orders(void)
{
	struct TRACKOBJECT original_split = trkObjectList[75];
	struct TRACKOBJECT original_slalom = trkObjectList[116];
	for (legacy_u16 reverse = 0; reverse < 2U; reverse++) {
		struct CSTRK_TRACK track;
		branch(&track, 116);
		activate(&track);
		struct TRKOBJINFO choices[2];
		choices[0] = original_split.ss_trkObjInfoPtr[reverse];
		choices[1] = original_split.ss_trkObjInfoPtr[1U - reverse];
		choices[0].si_noOfBlocks = 2;
		choices[1].si_noOfBlocks = 0;
		trkObjectList[75].ss_trkObjInfoPtr = choices;
		assert(track_setup() == TRACK_SETUP_OK);
		assert(selected_count() == TEST_RECTANGLE_TILE_COUNT);
		struct TRKOBJINFO mismatch = original_slalom.ss_trkObjInfoPtr[0];
		mismatch.si_exitType = 1;
		trkObjectList[116].ss_trkObjInfoPtr = &mismatch;
		assert(track_setup() == TRACK_SETUP_ELEMENT_MISMATCH);
		/* The strict game compiler rejects this track in either order. Never
		 * expose whichever prefix happened to close before the invalid branch. */
		assert(selected_count() == 0);
		trkObjectList[75] = original_split;
		trkObjectList[116] = original_slalom;
		cstrk_free(&track);
	}
}

static void route_piece(legacy_u16 index, legacy_u16 id, legacy_u8 column, legacy_u8 row)
{
	track_route_columns[index] = (legacy_s8)column;
	track_route_rows[index] = (legacy_s8)row;
	track_route_element_ids[index] = (legacy_s8)id;
	track_primary_route_links[index] = TRACK_ROUTE_LINK_NONE;
	track_alternate_route_links[index] = TRACK_ROUTE_LINK_NONE;
}

static void test_wrong_way_branch_is_invalid_in_both_visit_orders(void)
{
	struct TRACKOBJECT original = trkObjectList[75];
	for (legacy_u16 reverse = 0; reverse < 2U; reverse++) {
		struct CSTRK_TRACK track;
		rectangle(&track);
		put(&track, 75, TEST_BRANCH_LEFT, TEST_BRANCH_ROW);
		struct CSTRK_OBJECT ramp = {0};
		ramp.legacy_id = CSTRK_ROLL_RAMP_LAST_ID;
		ramp.x = TEST_BRANCH_LEFT - CSTRK_ROLL_RAMP_TILE_COUNT;
		ramp.y = TEST_BRANCH_ROW;
		ramp.width = CSTRK_ROLL_RAMP_TILE_COUNT;
		ramp.height = 1;
		ramp.occupied_mask = CSTRK_ROLL_RAMP_OCCUPIED_MASK;
		ramp.rotation = 2;
		ramp.roll_ramp = 1;
		assert(cstrk_add_object(&track, &ramp, NULL, 0));
		activate(&track);
		struct TRKOBJINFO choices[2];
		choices[0] = original.ss_trkObjInfoPtr[reverse];
		choices[1] = original.ss_trkObjInfoPtr[1U - reverse];
		choices[0].si_noOfBlocks = 2;
		choices[1].si_noOfBlocks = 0;
		trkObjectList[75].ss_trkObjInfoPtr = choices;
		assert(track_setup() == TRACK_SETUP_WRONG_WAY);
		assert(selected_count() == 0);
		trkObjectList[75] = original;
		cstrk_free(&track);
	}
}

static void test_graph_cycles_and_sparse_footprints(void)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 12, 10, NULL, 0));
	put(&track, 1, 1, 1);
	put(&track, 4, 10, 8);
	put(&track, 4, 8, 7);
	put(&track, 2, 6, 3);
	struct CSTRK_OBJECT corner = {0};
	corner.legacy_id = 11;
	corner.x = 3;
	corner.y = 2;
	corner.width = corner.height = 2;
	corner.occupied_mask = 9;
	assert(cstrk_add_object(&track, &corner, NULL, 0));
	activate(&track);
	route_piece(0, 1, 1, 1);
	route_piece(1, 11, 3, 2);
	route_piece(2, 4, 10, 8);
	route_piece(3, 2, 6, 3);
	route_piece(4, 4, 8, 7);
	route_piece(5, 1, 1, 1);
	track_primary_route_links[0] = 1;
	track_alternate_route_links[0] = 3;
	track_primary_route_links[1] = 5;
	track_alternate_route_links[1] = 4;
	track_primary_route_links[2] = 0;
	track_primary_route_links[3] = 0;
	track_primary_route_links[4] = 4;
	track_primary_route_links[5] = 0;
	extended_track_cache_lap_routes(6);
	assert(selected_count() == 3);
	assert(extended_track_lap_tiles()[1U * track.width + 1U]);
	assert(extended_track_lap_tiles()[2U * track.width + 3U]);
	assert(extended_track_lap_tiles()[3U * track.width + 4U]);
	assert(!extended_track_lap_tiles()[2U * track.width + 4U]);
	assert(!extended_track_lap_tiles()[3U * track.width + 3U]);
	/* The detached road points to finish, the reachable dead loop never does,
	 * scenery has no route, and repeated states still count each tile once. */
	assert(!extended_track_lap_tiles()[8U * track.width + 10U]);
	assert(!extended_track_lap_tiles()[7U * track.width + 8U]);
	assert(!extended_track_lap_tiles()[3U * track.width + 6U]);
	track_primary_route_links[5] = TRACK_ROUTE_LINK_NONE;
	track_alternate_route_links[0] = TRACK_ROUTE_LINK_NONE;
	extended_track_cache_lap_routes(6);
	assert(selected_count() == 0);
	for (legacy_u16 index = 0; index < EXTENDED_TRACK_ROUTE_CAPACITY; index++) {
		route_piece(index, 1, 1, 1);
		track_primary_route_links[index] = (index + 1U) % EXTENDED_TRACK_ROUTE_CAPACITY;
		track_alternate_route_links[index] = track_primary_route_links[index];
	}
	extended_track_cache_lap_routes(EXTENDED_TRACK_ROUTE_CAPACITY);
	assert(selected_count() == 1);
	extended_track_cache_lap_routes(0);
	assert(selected_count() == 0);
	cstrk_free(&track);
}

legacy_int main(void)
{
	trackside_camera_positions = cameras;
	trackside_camera_ground_heights = camera_height;
	reserved_trackside_camera_words = camera_words;
	roadside_sign_positions = signs;
	roadside_sign_headings = sign_headings;
	roadside_sign_shape_indices = sign_shapes;
	assert(extended_track_lap_tiles() == NULL);
	test_closed_lap_and_disconnected_roads();
	test_dead_branch_in_both_visit_orders();
	test_valid_fork();
	test_invalid_branch_clears_membership_in_both_visit_orders();
	test_wrong_way_branch_is_invalid_in_both_visit_orders();
	test_graph_cycles_and_sparse_footprints();
	extended_track_unload();
	assert(extended_track_lap_tiles() == NULL);
	puts("Complete-lap tiles retain valid forks and exclude dead routes, detached roads and "
		 "scenery.");
	return 0;
}
