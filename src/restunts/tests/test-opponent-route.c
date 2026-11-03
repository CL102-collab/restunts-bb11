#include <assert.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/resource.h"
#include "../c/fatal.h"

#undef memcpy

#define TEST_RESOURCE_COUNT 3U
#define TEST_RESOURCE_DATA_SIZE 48U
#define TEST_SPEED_OFFSET 16U
#define TEST_TRAILER_COST_INDEX 20U
#define TEST_OUTSIDE_COST_INDEX 200U
#define TEST_ROUTE_PIECES 5U
#define TEST_ROUTE_WORDS 8U
#define TEST_TAIL_SIZE 256U

struct GAMEINFO gameconfig;
legacy_s8 opponent_resource_name[] = "opp1";
legacy_s8 opponent_name_text_id[] = "nam";
legacy_s8 opponent_path_resource_id[] = "path";
legacy_s8 opponent_speed_resource_id[] = "sped";
legacy_s8 opponent_highscore_name[3];
legacy_u8 oppnentSped[OPPONENT_SPEED_COUNT];
legacy_s16 *track_primary_route_links;
legacy_s16 *track_alternate_route_links;
legacy_s8 *track_route_element_ids;
legacy_s8 *opponent_route_track_indices;
const legacy_s8 missing_shape_error_format[] = "shape";

static legacy_u8 arena[512];
static legacy_u8 decompression_tail[TEST_TAIL_SIZE];
static legacy_s16 primary[TEST_ROUTE_PIECES] = {1, 2, 0, 4, 0};
static legacy_s16 alternate[TEST_ROUTE_PIECES] = {3, -1, -1, -1, -1};
static legacy_s8 elements[TEST_ROUTE_PIECES];
static legacy_u8 selected_route[TEST_ROUTE_WORDS * LEGACY_WORD_BYTES];
static legacy_u16 resource_loads;
static legacy_u16 resource_unloads;

void far *file_load_resfile_with_tail(const legacy_s8 *name, legacy_u16 tail_bytes)
{
	assert(name[0] == 'o' && name[3] == '6');
	assert(tail_bytes == TEST_TAIL_SIZE);
	legacy_u16 end = resource_file_data_start(TEST_RESOURCE_COUNT) + TEST_RESOURCE_DATA_SIZE;
	memcpy(arena + end, decompression_tail, tail_bytes);
	resource_loads++;
	return arena;
}

void mmgr_release(void far *resource)
{
	assert(resource == arena);
	resource_unloads++;
}

legacy_s8 far *locate_text_res(legacy_s8 far *resource, const legacy_s8 *name)
{
	assert(resource == (legacy_s8 *)arena && name[0] == 'n');
	return (legacy_s8 *)"SK";
}

void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	assert(0);
}

static void initialize(legacy_u8 poison)
{
	memset(arena, poison, sizeof(arena));
	memset(decompression_tail, 0, sizeof(decompression_tail));
	legacy_u16 data_start = resource_file_data_start(TEST_RESOURCE_COUNT);
	memset(arena, 0, data_start + TEST_RESOURCE_DATA_SIZE);
	resource_file_set_size(arena, data_start + TEST_RESOURCE_DATA_SIZE);
	LEGACY_WRITE_U16_LE(arena + RESOURCE_FILE_COUNT_OFFSET, TEST_RESOURCE_COUNT);
	memcpy(arena + RESOURCE_FILE_DIRECTORY_OFFSET, "pathspedwinn", 12);
	resource_file_set_offset(arena, TEST_RESOURCE_COUNT, 0, 0);
	resource_file_set_offset(arena, TEST_RESOURCE_COUNT, 1, TEST_SPEED_OFFSET);
	resource_file_set_offset(arena, TEST_RESOURCE_COUNT, 2,
							 TEST_SPEED_OFFSET + OPPONENT_SPEED_COUNT);
	arena[data_start + TEST_SPEED_OFFSET + 1] = 10;
	gameconfig.game_opponenttype = 6;
	track_primary_route_links = primary;
	track_alternate_route_links = alternate;
	track_route_element_ids = elements;
	opponent_route_track_indices = (legacy_s8 *)selected_route;
	elements[0] = 0;
	elements[1] = LEGACY_S8_FROM_BITS(TEST_OUTSIDE_COST_INDEX);
	elements[2] = 0;
	elements[3] = 1;
	elements[4] = 0;
	memset(selected_route, 0, sizeof(selected_route));
}

static void assert_selected_route(legacy_u16 first_branch, legacy_u16 second_branch)
{
	assert(LEGACY_READ_U16_LE(selected_route) == 0);
	assert(LEGACY_READ_U16_LE(selected_route + 2) == first_branch);
	assert(LEGACY_READ_U16_LE(selected_route + 4) == second_branch);
	assert(LEGACY_READ_U16_LE(selected_route + 6) == 0);
	assert(oppnentSped[1] == 10);
	assert(resource_loads == resource_unloads);
}

static void test_allocator_contents_do_not_change_route(void)
{
	static const legacy_u8 poisons[] = {0, 1, 127, 255};
	for (legacy_u16 index = 0; index < sizeof(poisons); index++) {
		initialize(poisons[index]);
		load_opponent_data();
		assert_selected_route(1, 2);
		/* Reusing the same resource after adjacent allocations changed must
		 * make the same choice as its first load. */
		legacy_u16 end = resource_file_data_start(TEST_RESOURCE_COUNT) + TEST_RESOURCE_DATA_SIZE;
		memset(arena + end, (legacy_u8)~poisons[index], sizeof(arena) - end);
		load_opponent_data();
		assert_selected_route(1, 2);
	}
}

static void test_valid_following_resource_costs_are_preserved(void)
{
	initialize(255);
	elements[1] = TEST_TRAILER_COST_INDEX;
	legacy_u16 speed_offset = resource_file_data_start(TEST_RESOURCE_COUNT) + TEST_SPEED_OFFSET;
	arena[speed_offset + TEST_TRAILER_COST_INDEX] = 30;
	load_opponent_data();
	assert_selected_route(3, 4);
	/* Unwritten bytes past the resource remain zero, independent of
	 * the surrounding arena contents. */
	elements[1] = TEST_RESOURCE_DATA_SIZE - TEST_SPEED_OFFSET;
	load_opponent_data();
	assert_selected_route(1, 2);
}

static void test_decompression_tail_costs_choose_route(void)
{
	static const legacy_u8 poisons[] = {0, 1, 127, 255};
	for (legacy_u16 index = 0; index < sizeof(poisons); index++) {
		initialize(poisons[index]);
		/* A tile indexes a byte left by decompression beyond the resource's
		 * declared size. It still participates in the original route score. */
		legacy_u16 tail_index =
			TEST_SPEED_OFFSET + TEST_OUTSIDE_COST_INDEX - TEST_RESOURCE_DATA_SIZE;
		decompression_tail[tail_index] = 30;
		load_opponent_data();
		assert_selected_route(3, 4);
		legacy_u16 end = resource_file_data_start(TEST_RESOURCE_COUNT) + TEST_RESOURCE_DATA_SIZE;
		memset(arena + end, (legacy_u8)~poisons[index], sizeof(arena) - end);
		load_opponent_data();
		assert_selected_route(3, 4);
	}
}

int main(void)
{
	test_allocator_contents_do_not_change_route();
	test_valid_following_resource_costs_are_preserved();
	test_decompression_tail_costs_choose_route();
	return 0;
}
