#include <assert.h>
#include <string.h>

#include "../c/menu/menu_track_editor.c"
#include "../c/menu/menu_track_editor_map.c"
#include "../c/menu/menu_track_editor_render.c"
#include "../c/menu/menu_track_editor_input.c"

static legacy_u8 element_tiles[TRACKDATA_MAP_SIZE];
static legacy_u8 terrain_tiles[TRACKDATA_MAP_SIZE];
static legacy_u8 palette[432];
static struct TRACK_EDITOR_SESSION editor;

static void reset_editor(void)
{
	memset(&editor, 0, sizeof(editor));
	memset(element_tiles, 0, sizeof(element_tiles));
	memset(terrain_tiles, 0, sizeof(terrain_tiles));
	memset(palette, 0, sizeof(palette));
	track_element_map = element_tiles;
	track_terrain_map = terrain_tiles;
	progress_box_shape = palette;
	for (legacy_u32 row = 0; row < 30; row++) {
		trackrows[row] = (legacy_s16)(row * 30);
		terrainrows[row] = (legacy_s16)((29 - row) * 30);
	}
	track_validation_column = 10;
	track_validation_row = 10;
	track_editor_initialize_session(&editor);
	editor.selected_tile = 1;
	editor.map_dirty = 0;
	editor.palette_dirty = 0;
	editor.validate_track = 0;
}

static void test_multitile_placement(void)
{
	struct TRACKOBJECT saved = trkObjectList[1];

	legacy_u32 index = 10 * 30 + 10;
	for (legacy_u32 flags = 0; flags < 4; flags++) {
		reset_editor();
		trkObjectList[1].ss_multiTileFlag = (legacy_s8)flags;
		track_editor_place_element(&editor);
		assert(element_tiles[index] == 1);
		assert(element_tiles[index + 1] == ((flags & 2) ? TRACK_TILE_CONTINUATION_EAST : 0));
		assert(element_tiles[index + 30] == ((flags & 1) ? TRACK_TILE_CONTINUATION_SOUTH : 0));
		assert(element_tiles[index + 31] == (flags == 3 ? TRACK_TILE_CONTINUATION_SOUTHEAST : 0));
		assert(editor.track_changed == 1);
		assert(editor.validate_track == 1);
	}
	trkObjectList[1] = saved;
}

static void test_invalid_edge_placement(void)
{
	struct TRACKOBJECT saved = trkObjectList[1];

	for (legacy_u32 flags = 1; flags < 4; flags++) {
		for (legacy_u32 row = 28; row <= 29; row++) {
			for (legacy_u32 column = 28; column <= 29; column++) {
				reset_editor();
				trkObjectList[1].ss_multiTileFlag = (legacy_s8)flags;
				editor.selection_row[0] = (legacy_u8)row;
				editor.selection_column[0] = (legacy_u8)column;
				track_editor_place_element(&editor);
				if (((flags & 1) && row == 29) || ((flags & 2) && column == 29)) {
					assert(editor.track_changed == 0);
					assert(editor.validate_track == 0);
					assert(element_tiles[row * 30 + column] == 0);
					assert(editor.last_column == TRACK_EDITOR_POSITION_UNSET);
				} else {
					assert(element_tiles[row * 30 + column] == 1);
				}
			}
		}
	}
	trkObjectList[1] = saved;
}

static void test_repeated_placement_and_terrain_rows(void)
{
	struct TRACKOBJECT saved = trkObjectList[1];

	reset_editor();
	trkObjectList[1].ss_multiTileFlag = 0;
	element_tiles[310] = 2;
	track_editor_place_element(&editor);
	assert(element_tiles[310] == 1);
	assert(editor.saved_tile == 2);
	track_editor_place_element(&editor);
	assert(element_tiles[310] == 2);
	assert(editor.saved_tile == 1);
	assert(editor.selected_tile == 2);
	trkObjectList[1] = saved;

	reset_editor();
	terrain_tiles[580] = 6;
	track_editor_place_terrain(&editor);
	assert(terrain_tiles[580] == 1);
	assert(terrain_tiles[310] == 0);
	track_editor_place_terrain(&editor);
	assert(terrain_tiles[580] == 6);
	assert(editor.saved_tile == 1);
}

static void test_visible_selection(void)
{
	reset_editor();
	editor.selection_column[0] = 29;
	editor.selection_row[0] = 29;
	editor.tile_width = 2;
	editor.tile_height = 2;
	track_editor_keep_selection_visible(&editor);
	assert(editor.selection_column[0] == 28);
	assert(editor.selection_row[0] == 28);
	assert(editor.map_column_offset == 18);
	assert(editor.map_row_offset == 19);
	assert(editor.scrollbars_dirty == 1);

	track_editor_move_home(&editor);
	assert(editor.selection_column[0] == 18);
	assert(editor.selection_row[0] == 19);
	track_editor_move_home(&editor);
	assert(editor.selection_column[0] == 0);
	assert(editor.selection_row[0] == 0);
	assert(editor.map_column_offset == 0);
	assert(editor.map_row_offset == 0);
}

static void test_palette_navigation(void)
{
	reset_editor();
	editor.focus = 1;
	editor.selection_row[1] = 1;
	editor.selection_column[1] = 1;
	palette[36 + 1 * 6 + 2] = TRACK_TILE_CONTINUATION_EAST;
	track_editor_move_right(&editor);
	assert(editor.selection_column[1] == 3);
	track_editor_move_left(&editor);
	assert(editor.selection_column[1] == 1);

	editor.selection_row[1] = 2;
	palette[36 + 1 * 6 + 1] = TRACK_TILE_CONTINUATION_EAST;
	track_editor_move_up(&editor);
	assert(editor.selection_row[1] == 1);
	assert(editor.selection_column[1] == 0);

	editor.selection_row[1] = 6;
	editor.page = 10;
	track_editor_move_right(&editor);
	assert(editor.page == 10);
	track_editor_move_left(&editor);
	assert(editor.page == 9);
	editor.page = 1;
	track_editor_move_left(&editor);
	assert(editor.page == 1);
}

int main(void)
{
	test_multitile_placement();
	test_invalid_edge_placement();
	test_repeated_placement_and_terrain_rows();
	test_visible_selection();
	test_palette_navigation();
	return 0;
}
