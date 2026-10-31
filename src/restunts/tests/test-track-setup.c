#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/memmgr.h"
#include "../c/track/track_objects.h"
#include "../c/track/trackdata_layout.h"

#undef memcpy

#define TEST_TILE_COUNT 901
#define TEST_CAMERA_COUNT 64

struct SETUP_OUTPUT {
	legacy_u8 elements[TEST_TILE_COUNT];
	legacy_u8 terrain[TEST_TILE_COUNT];
	legacy_s16 primary[TEST_TILE_COUNT];
	legacy_s16 alternate[TEST_TILE_COUNT];
	legacy_s8 columns[TEST_TILE_COUNT];
	legacy_s8 rows[TEST_TILE_COUNT];
	legacy_s8 element_ids[TEST_TILE_COUNT];
	legacy_s8 traversal[TEST_TILE_COUNT];
	legacy_u8 signs_by_tile[TEST_TILE_COUNT];
	legacy_s16 camera_height[TEST_CAMERA_COUNT];
	legacy_s16 camera_reserved[TEST_CAMERA_COUNT];
	legacy_s16 sign_headings[TEST_CAMERA_COUNT];
	struct VECTOR cameras[TEST_CAMERA_COUNT];
	struct VECTOR signs[TEST_CAMERA_COUNT];
	legacy_u8 sign_shapes[TEST_CAMERA_COUNT];
	legacy_s16 status;
	legacy_s16 pieces;
	legacy_s16 angle;
	legacy_s16 start_column;
	legacy_s16 start_row;
	legacy_s16 validation_column;
	legacy_s16 validation_row;
	legacy_s16 sign_count;
	legacy_s16 camera_count;
	legacy_s16 hill;
	legacy_s32 allocations;
	legacy_s32 releases;
};

static struct SETUP_OUTPUT output;
static legacy_s32 allocation_fails;

void far *mmgr_alloc_resbytes(const legacy_s8 *name, legacy_s32 size)
{
	assert(size == 64 * 14);
	assert(name[0] == 't');
	output.allocations++;
	return allocation_fails ? NULL : malloc((size_t)size);
}

void mmgr_release(void far *pointer)
{
	assert(pointer != NULL);
	output.releases++;
	free(pointer);
}

static void initialize(void)
{
	memset(&output, 0, sizeof(output));
	track_element_map = output.elements;
	track_terrain_map = output.terrain;
	track_primary_route_links = output.primary;
	track_alternate_route_links = output.alternate;
	track_route_columns = output.columns;
	track_route_rows = output.rows;
	track_route_element_ids = output.element_ids;
	track_route_traversal_flags = output.traversal;
	roadside_sign_indices_by_tile = output.signs_by_tile;
	roadside_sign_headings = output.sign_headings;
	roadside_sign_positions = output.signs;
	roadside_sign_shape_indices = output.sign_shapes;
	trackside_camera_positions = output.cameras;
	trackside_camera_ground_heights = output.camera_height;
	reserved_trackside_camera_words = output.camera_reserved;
	track_pieces_counter = 0;
	track_angle = 0;
	start_finish_column = 0;
	start_finish_row = 0;
	track_validation_column = 0;
	track_validation_row = 0;
	roadside_sign_count = 0;
	trackside_camera_count = 0;
	hillFlag = 0;
	for (legacy_s32 index = 0; index < 30; index++) {
		trackrows[index] = index * 30;
		terrainrows[index] = (29 - index) * 30;
		track_column_positions[index] = index * 1024;
		track_column_centers[index] = index * 1024 + 512;
		track_row_positions[index] = (30 - index) * 1024;
		track_row_centers[index] = (29 - index) * 1024 + 512;
	}
}

static void capture_globals(legacy_s16 status)
{
	output.status = status;
	output.pieces = track_pieces_counter;
	output.angle = track_angle;
	output.start_column = start_finish_column;
	output.start_row = start_finish_row;
	output.validation_column = track_validation_column;
	output.validation_row = track_validation_row;
	output.sign_count = roadside_sign_count;
	output.camera_count = trackside_camera_count;
	output.hill = hillFlag;
}

#ifdef TRACK_SETUP_DIFFERENTIAL
legacy_s16 reference_track_setup(void);
#endif

static legacy_s16 run_setup(void)
{
#ifdef TRACK_SETUP_DIFFERENTIAL
	legacy_u8 input_elements[TEST_TILE_COUNT];
	memcpy(input_elements, output.elements, sizeof(input_elements));
	legacy_u8 input_terrain[TEST_TILE_COUNT];
	memcpy(input_terrain, output.terrain, sizeof(input_terrain));
	capture_globals(reference_track_setup());
	struct SETUP_OUTPUT reference = output;
	initialize();
	memcpy(output.elements, input_elements, sizeof(input_elements));
	memcpy(output.terrain, input_terrain, sizeof(input_terrain));
#endif
	capture_globals(track_setup());
#ifdef TRACK_SETUP_DIFFERENTIAL
	assert(memcmp(&reference, &output, sizeof(output)) == 0);
#endif
	assert(output.allocations == 1);
	assert(output.releases == !allocation_fails);
	return output.status;
}

static void put_tile(legacy_s32 column, legacy_s32 row, legacy_u8 tile)
{
	output.elements[trackrows[row] + column] = tile;
}

static void rectangle(legacy_s32 width, legacy_s32 height, legacy_s32 start_side)
{
	legacy_s32 right = width + 4;
	legacy_s32 bottom = height + 4;

	initialize();
	for (legacy_s32 index = 6; index < right; index++) {
		put_tile(index, 5, 5);
		put_tile(index, bottom, 5);
	}
	for (legacy_s32 index = 6; index < bottom; index++) {
		put_tile(5, index, 4);
		put_tile(right, index, 4);
	}
	put_tile(5, 5, 6);
	put_tile(right, 5, 7);
	put_tile(right, bottom, 9);
	put_tile(5, bottom, 8);
	switch (start_side) {
		case 0:
			put_tile(5, 6, 1);
			break;
		case 1:
			put_tile(6, 5, 136);
			break;
		case 2:
			put_tile(right, 6, 135);
			break;
		case 3:
			put_tile(6, bottom, 137);
			break;
	}
}

static void test_closed_routes(void)
{
	for (legacy_s32 width = 3; width <= 20; width++) {
		for (legacy_s32 height = 3; height <= 20; height++) {
			for (legacy_s32 side = 0; side < 4; side++) {
				for (legacy_s32 raised = 0; raised <= 1; raised++) {
					rectangle(width, height, side);
					memset(output.terrain, raised * 6, sizeof(output.terrain));
					assert(run_setup() == 0);
					legacy_s32 expected_pieces = 2 * (width + height) - 4;
					assert(track_pieces_counter == expected_pieces);
					assert(trackside_camera_count == expected_pieces / 3);
					assert(track_angle == side * 256);
					for (legacy_s32 index = 0; index < expected_pieces; index++) {
						assert(output.primary[index] == (index + 1) % expected_pieces);
						assert(output.alternate[index] == -1);
					}
					for (legacy_s32 index = 0; index < trackside_camera_count; index++) {
						assert(output.camera_height[index] == raised * 450);
						assert(output.camera_reserved[index] == 0);
					}
				}
			}
		}
	}
}

/* The straight path closes first; the deferred spur then backtracks to the
 * same owner tile with a distinct subtype, preserving both predecessor links. */
static void test_deferred_branch(void)
{
	rectangle(8, 8, 0);
	struct TRACKOBJECT saved_object = trkObjectList[2];
	struct TRKOBJINFO split[2];
	split[0] = trkObjectList[4].ss_trkObjInfoPtr[0];
	split[1] = split[0];
	split[0].si_noOfBlocks = 2;
	split[1].si_noOfBlocks = 0;
	split[1].si_exitPoint = 4;
	trkObjectList[2].ss_trkObjInfoPtr = split;
	put_tile(5, 7, 2);
	assert(run_setup() == 0);
	assert(track_pieces_counter == 29);
	legacy_s32 second_split = -1;
	legacy_s32 first_split = -1;
	for (legacy_s32 index = 0; index < track_pieces_counter; index++) {
		if (output.element_ids[index] == 2) {
			if (output.traversal[index] == 0) {
				first_split = index;
			} else {
				second_split = index;
			}
		}
	}
	assert(first_split > 0);
	assert(second_split == 28);
	assert(output.primary[first_split - 1] == first_split);
	assert(output.alternate[first_split - 1] == second_split);
	trkObjectList[2] = saved_object;
}

/* The initial branch is saved before any predecessor has been appended. */
static void test_start_finish_branch(void)
{
	rectangle(8, 8, 0);
	struct TRACKOBJECT saved_object = trkObjectList[1];
	struct TRKOBJINFO split[2];
	split[0] = trkObjectList[1].ss_trkObjInfoPtr[0];
	split[1] = split[0];
	split[0].si_noOfBlocks = 2;
	split[1].si_noOfBlocks = 0;
	split[1].si_exitPoint = 4;
	trkObjectList[1].ss_trkObjInfoPtr = split;

	assert(run_setup() == 0);
	assert(track_pieces_counter == 30);
	for (legacy_s16 index = 0; index < 28; index++) {
		assert(output.primary[index] == (index + 1) % 28);
	}
	assert(output.alternate[27] == 28);
	for (legacy_s16 index = 28; index < track_pieces_counter; index++) {
		assert(output.element_ids[index] == 1);
		assert(output.traversal[index] == 1);
		assert(output.primary[index] == -1);
		assert(output.alternate[index] == -1);
	}
	trkObjectList[1] = saved_object;
}

static void test_failures(void)
{
	initialize();
	allocation_fails = 1;
	assert(run_setup() == 2);
	allocation_fails = 0;
	initialize();
	assert(run_setup() == 1);
	assert(track_validation_column == 29);
	assert(track_validation_row == 29);
	rectangle(8, 8, 0);
	put_tile(7, 7, 1);
	assert(run_setup() == 3);
	assert(track_validation_column == 7);
	assert(track_validation_row == 7);
	rectangle(8, 8, 0);
	put_tile(7, 5, 0);
	assert(run_setup() == 7);
	rectangle(8, 8, 0);
	put_tile(7, 5, 35);
	assert(run_setup() == 4);
	rectangle(8, 8, 0);
	output.terrain[terrainrows[8] + 8] = 6;
	assert(run_setup() == 11);
}

/* Optional replay inputs reuse the same output-by-output differential harness
 * for real multi-tile pieces, split routes, jumps and terrain substitutions. */
static void test_replay(const char *path)
{
	initialize();
	FILE *file = fopen(path, "rb");
	assert(file != NULL);
	assert(fseek(file, REPLAY_GAMEINFO_SIZE, SEEK_SET) == 0);
	assert(fread(output.elements, 1, TEST_TILE_COUNT, file) == TEST_TILE_COUNT);
	assert(fread(output.terrain, 1, TEST_TILE_COUNT, file) == TEST_TILE_COUNT);
	fclose(file);
	run_setup();
}

int main(int argc, char **argv)
{
	test_closed_routes();
	test_deferred_branch();
	test_start_finish_branch();
	test_failures();
	for (legacy_s32 index = 1; index < argc; index++) {
		test_replay(argv[index]);
	}
	return 0;
}
