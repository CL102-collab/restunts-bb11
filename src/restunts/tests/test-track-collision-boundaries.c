#include <assert.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/physics_internal.h"
#include "../c/residue.h"
#include "../c/track/track_objects.h"
#include "../c/track/trackdata_layout.h"

static legacy_u8 elements[900], terrain[900];

static void reset_collision_track(void)
{
	memset(elements, 0, sizeof(elements));
	memset(terrain, 0, sizeof(terrain));
	memset(trkObjectList, 0, sizeof(trkObjectList));
	track_element_map = elements;
	track_terrain_map = terrain;
	legacy_closed_hihat_offset = 0;
	elapsed_time2 = 0;
	for (legacy_u32 index = 0; index < 30; index++) {
		trackrows[index] = terrainrows[index] = index * 30;
		track_row_positions[index] = (30 - index) * 1024;
		track_row_centers[index] = (30 - index) * 1024 - 512;
		track_column_positions[index] = index * 1024;
		track_column_centers[index] = index * 1024 + 512;
	}
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_HIGHWAY;
}

static void test_coordinate_aliases(void)
{
	reset_collision_track();
	for (legacy_u32 index = 0; index < 30; index++) {
		assert(track_row_position(index) == (legacy_s16)((30 - index) * 1024));
		assert(track_column_position(index) == (legacy_s16)(index * 1024));
	}
	static const legacy_s16 signed_words[] = {0, 1234, -32768, -292, -1};
	static const legacy_u16 words[] = {0, 1234, 0x8000, 0xfedc, 0xffff};
	for (legacy_u32 index = 0; index < sizeof(words) / sizeof(words[0]); index++) {
		legacy_closed_hihat_offset = words[index];
		elapsed_time2 = words[4 - index];
		assert(track_row_position(30) == signed_words[index]);
		assert(track_column_position(30) == signed_words[4 - index]);
		assert(track_row_position(29) == 1024);
		assert(track_column_position(29) == 29696);
	}
}

struct BOUNDARY_CASE {
	legacy_u8 tile, row, column, anchor_row, anchor_column, flags;
	legacy_u16 hihat_offset, elapsed;
	legacy_s16 x, z;
};

static void test_collision_continuations(void)
{
	static const struct BOUNDARY_CASE cases[] = {
		/* South continuations reach the word after row 29. */
		{254, 29, 14, 28, 14, 1, 0, 777, 14848, 0},
		{254, 29, 14, 28, 14, 1, 1234, 777, 14848, 1234},
		{254, 29, 14, 28, 14, 1, 0x8000, 777, 14848, -32768},
		/* A southeast continuation also reaches row 30, but keeps its column. */
		{253, 29, 14, 28, 13, 3, 0xfedc, 777, 14336, -292},
		{253, 29, 29, 28, 28, 3, 321, 456, 29696, 321},
		/* South continuations at the corner use both adjacent legacy words. */
		{254, 29, 29, 28, 29, 3, 1234, 5678, 5678, 1234},
		{254, 29, 29, 28, 29, 3, 0x8000, 0xfedc, -292, -32768},
		/* A base element on the east edge reaches column 30 directly. */
		{1, 12, 29, 12, 29, 2, 123, 0, 0, 17920},
		{1, 12, 29, 12, 29, 2, 123, 5678, 5678, 17920},
		{1, 12, 29, 12, 29, 2, 123, 0xfedc, -292, 17920},
		/* Without the matching multi-tile flag, the tile center remains valid. */
		{254, 29, 29, 28, 29, 0, 123, 456, 30208, 512},
		{253, 29, 29, 28, 28, 0, 123, 456, 30208, 512},
		/* East continuations and interior tiles use ordinary table entries. */
		{255, 29, 29, 29, 28, 3, 123, 456, 29696, 1024},
		{254, 12, 7, 11, 7, 3, 123, 456, 8192, 17408},
		{253, 12, 7, 11, 6, 3, 123, 456, 7168, 17408},
		{255, 12, 7, 12, 6, 3, 123, 456, 7168, 18432},
		{1, 12, 7, 12, 7, 3, 123, 456, 8192, 18432},
		{1, 12, 7, 12, 7, 0, 123, 456, 7680, 17920}};

	const struct BOUNDARY_CASE *sample;
	struct VECTOR result[8];
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		reset_collision_track();
		sample = &cases[index];
		elements[sample->anchor_row * 30 + sample->anchor_column] = 1;
		elements[sample->row * 30 + sample->column] = sample->tile;
		trkObjectList[1].ss_multiTileFlag = sample->flags;
		legacy_closed_hihat_offset = sample->hihat_offset;
		elapsed_time2 = sample->elapsed;
		memset(result, 0x5a, sizeof(result));
		assert(get_track_collision_points(sample->column, sample->row, result) == 1);
		assert(result[0].x == sample->x);
		assert(result[0].y == 0);
		assert(result[0].z == sample->z);
		assert(result[1].x == 0x5a5a);
	}
}

static void test_rotated_corkscrew_at_boundary(void)
{
	reset_collision_track();
	elements[28 * 30 + 14] = 1;
	elements[29 * 30 + 14] = TRACK_TILE_CONTINUATION_SOUTH;
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT;
	trkObjectList[1].ss_multiTileFlag = 1;
	trkObjectList[1].ss_rotY = ANGLE_QUARTER_TURN;
	terrain[29 * 30 + 14] = TERRAIN_RAISED_TILE;
	struct VECTOR result[8];
	assert(get_track_collision_points(14, 29, result) == 2);
	assert(result[0].x == 14336);
	assert(result[1].x == 15360);
	assert(result[0].y == 450);
	assert(result[1].y == 450);
	assert(result[0].z == 60);
	assert(result[1].z == -60);

	/* Changing the pointer word must move the same geometry on the next call. */
	legacy_closed_hihat_offset = 0x8000;
	assert(get_track_collision_points(14, 29, result) == 2);
	assert(result[0].z == -32708);
	assert(result[1].z == 32708);
}

int main(void)
{
	test_coordinate_aliases();
	test_collision_continuations();
	test_rotated_corkscrew_at_boundary();
	return 0;
}
