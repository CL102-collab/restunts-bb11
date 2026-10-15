#include "externs.h"
#include "fileio.h"
#include "legacy.h"
#include "memmgr.h"
#include "menu_internal.h"
#include "platform.h"
#ifdef RESTUNTS_AMIGA
#include "../platform/amiga/amiga.h"
#endif
#include "shape2d.h"
#include "shape3d.h"
#include "trackdata_layout.h"
#include "ui_text.h"
#include "timing.h"
#include "ui_dialog.h"
#include "game_input.h"
#include "ui_input.h"
#include "track_objects.h"
#include "highscore.h"
#include "menu_common.h"
#include "keyboard.h"

#define TRACK_EDITOR_CACHE_INVALID 255U
#define TRACK_EDITOR_POSITION_UNSET 255U
#define TRACK_EDITOR_PAGE_UNSET 255U
#define TRACK_EDITOR_HOVER_UNSET 255U
#define TRACK_EDITOR_MOUSE_NO_HIT 255U
#define TRACK_EDITOR_SAVE_CANCELLED 255U
#define TRACK_EDITOR_DIALOG_CANCELLED 255U
#define TRACK_EDITOR_SCREEN_WIDTH 320U
#define TRACK_EDITOR_SCREEN_HEIGHT 200U
#define TRACK_EDITOR_MAP_LEFT 8
#define TRACK_EDITOR_MAP_TOP 4
#define TRACK_EDITOR_MAP_CLIP_RIGHT 200
#define TRACK_EDITOR_MAP_CLIP_BOTTOM 179
#define TRACK_EDITOR_TILE_SIZE 16U
#define TRACK_EDITOR_PALETTE_LEFT 220U
#define TRACK_EDITOR_PALETTE_TOP 36U
#define TRACK_EDITOR_PALETTE_WIDTH 96U
#define TRACK_EDITOR_PALETTE_ACTION_TOP 28
#define TRACK_EDITOR_PALETTE_ACTION_WIDTH 48U
#define TRACK_EDITOR_MULTITILE_CURSOR_SIZE 32U
#define TRACK_EDITOR_LABEL_Y 192
#define TRACK_EDITOR_HORIZONTAL_SCROLLBAR_X 9
#define TRACK_EDITOR_HORIZONTAL_SCROLLBAR_LENGTH 181
#define TRACK_EDITOR_VERTICAL_SCROLLBAR_X 202
#define TRACK_EDITOR_VERTICAL_SCROLLBAR_LENGTH 176
#define TRACK_EDITOR_PAGE_BAR_X 221
#define TRACK_EDITOR_PAGE_BAR_WIDTH 95
#define TRACK_EDITOR_PAGE_BAR_Y 133
#define TRACK_EDITOR_TITLE_X 217
#define TRACK_EDITOR_TITLE_Y 3
#define TRACK_EDITOR_TITLE_WIDTH 102
#define TRACK_EDITOR_TITLE_HEIGHT 22
#define TRACK_EDITOR_LEFT_FRAME_X 5
#define TRACK_EDITOR_LEFT_FRAME_Y 0
#define TRACK_EDITOR_LEFT_FRAME_WIDTH 206
#define TRACK_EDITOR_LEFT_FRAME_HEIGHT 190
#define TRACK_EDITOR_RIGHT_FRAME_X 217
#define TRACK_EDITOR_RIGHT_FRAME_Y 32
#define TRACK_EDITOR_RIGHT_FRAME_WIDTH 102
#define TRACK_EDITOR_RIGHT_FRAME_HEIGHT 158
#define TRACK_EDITOR_WIDE_BUTTON_X 221
#define TRACK_EDITOR_WIDE_BUTTON_Y 140
#define TRACK_EDITOR_WIDE_BUTTON_WIDTH 94
#define TRACK_EDITOR_BUTTON_LEFT_X 221
#define TRACK_EDITOR_BUTTON_RIGHT_X 269
#define TRACK_EDITOR_BUTTON_UPPER_Y 156
#define TRACK_EDITOR_BUTTON_LOWER_Y 172
#define TRACK_EDITOR_BUTTON_WIDTH 46
#define TRACK_EDITOR_BUTTON_HEIGHT 14
#define TRACK_EDITOR_WATER_RAISED_ROAD_FIRST 34U
#define TRACK_EDITOR_WATER_RAISED_ROAD_LAST 35U
#define TRACK_EDITOR_WATER_BRIDGE_FIRST 103U
#define TRACK_EDITOR_WATER_BRIDGE_LAST 108U
#define TRACK_EDITOR_WATER_SHIP_FIRST 171U
#define TRACK_EDITOR_WATER_SHIP_LAST 174U
#define TRACK_EDITOR_TRACK_FILE_SIZE 1802UL
#define TRACK_EDITOR_TRANSPARENT_COLOR 15U
#define TRACK_EDITOR_TILE_PREVIEW_RIGHT 15
#define TRACK_EDITOR_TILE_PREVIEW_BOTTOM 14
#define TRACK_EDITOR_BLINK_INITIAL_COUNT 99U
#define TRACK_EDITOR_BLINK_INTERVAL 15U
#define TRACK_EDITOR_SKYBOX_MAP_INDEX 900U
#define TRACK_EDITOR_DIALOG_NO_CHANGE 5U

enum TRACK_EDITOR_VALIDATION_RESULT {
	TRACK_EDITOR_VALIDATION_OK = 0,
	TRACK_EDITOR_ERROR_INVALID_WATER_TILE = 12,
	TRACK_EDITOR_ERROR_INVALID_HILL_TILE = 13,
	TRACK_EDITOR_ERROR_INVALID_TERRAIN_TILE = 14
};

static legacy_u8 far *progress_box_shape;

void preRender_icons(legacy_u8 page)
{
	for (legacy_u16 row = 0; row < 6U; row++) {
		for (legacy_u16 column = 0; column < 6U; column++) {
			legacy_u8 tile = progress_box_shape[(legacy_u16)page * 36U + row * 6U + column];
			legacy_u16 x = LEGACY_U16_WRAP_ADD(TRACK_EDITOR_PALETTE_LEFT,
											   LEGACY_U16_WRAP_MUL(column, TRACK_EDITOR_TILE_SIZE));
			legacy_u16 y = LEGACY_U16_WRAP_ADD(TRACK_EDITOR_PALETTE_TOP,
											   LEGACY_U16_WRAP_MUL(row, TRACK_EDITOR_TILE_SIZE));
			if (page == 0) {
				sprite_shape_to_1(track_editor_terrain_shapes[tile], x, y);
				continue;
			}
			if (tile >= TRACK_TILE_CONTINUATION_SOUTHEAST) {
				continue;
			}

			sprite_shape_to_1(track_editor_terrain_shapes[0], x, y);
			legacy_u8 multi_tile = trkObjectList[tile].ss_multiTileFlag;
			if (multi_tile == 1U || multi_tile == 3U) {
				sprite_shape_to_1(track_editor_terrain_shapes[0], x, LEGACY_U16_WRAP_ADD(y, 16U));
			}
			if (multi_tile == 2U || multi_tile == 3U) {
				sprite_shape_to_1(track_editor_terrain_shapes[0], LEGACY_U16_WRAP_ADD(x, 16U), y);
			}
			if (multi_tile == 3U) {
				sprite_shape_to_1(track_editor_terrain_shapes[0], LEGACY_U16_WRAP_ADD(x, 16U),
								  LEGACY_U16_WRAP_ADD(y, 16U));
			}
			putpixel_iconMask(track_editor_tile_masks[tile], x, y);
			putpixel_iconFillings(track_editor_tile_shapes[tile], x, y);
		}
	}
}

static legacy_u16 track_menu_previous_row(legacy_u16 row);
static legacy_u8 track_editor_map_tile(legacy_u8 column, legacy_u8 row);

struct TRACK_EDITOR_MAP_CELL {
	legacy_u16 source_row;
	legacy_u16 source_column;
	legacy_u16 source_index;
	legacy_u16 cache_index;
	legacy_s16 x;
	legacy_s16 y;
	legacy_u8 tile;
	legacy_u8 terrain;
};

static void track_editor_draw_map_element(struct TRACK_EDITOR_MAP_CELL *cell)
{
	legacy_u8 multi_tile = (legacy_u8)trkObjectList[cell->tile].ss_multiTileFlag;
	switch (multi_tile) {
		case 0:
			putpixel_iconMask(track_editor_tile_masks[cell->tile], cell->x, cell->y);
			putpixel_iconFillings(track_editor_tile_shapes[cell->tile], cell->x, cell->y);
			break;

		case 1:
			cell->terrain = track_terrain_map[LEGACY_U16_WRAP_ADD(
				(legacy_u16)terrainrows[cell->source_row + 1U], cell->source_column)];
			sprite_copy_image_at(track_editor_terrain_shapes[cell->terrain], cell->x,
								 LEGACY_S16_WRAP_ADD(cell->y, 16));
			sprite_putimage_and(track_editor_tile_masks[cell->tile], cell->x, cell->y);
			sprite_putimage_or(track_editor_tile_shapes[cell->tile], cell->x, cell->y);
			break;

		case 2:
			cell->terrain = track_terrain_map[LEGACY_U16_WRAP_ADD(
				(legacy_u16)terrainrows[cell->source_row], cell->source_column + 1U)];
			sprite_copy_image_at(track_editor_terrain_shapes[cell->terrain],
								 LEGACY_S16_WRAP_ADD(cell->x, 16), cell->y);
			sprite_putimage_and(track_editor_tile_masks[cell->tile], cell->x, cell->y);
			sprite_putimage_or(track_editor_tile_shapes[cell->tile], cell->x, cell->y);
			break;

		case 3:
			cell->terrain = track_terrain_map[LEGACY_U16_WRAP_ADD(
				(legacy_u16)terrainrows[cell->source_row], cell->source_column + 1U)];
			sprite_copy_image_at(track_editor_terrain_shapes[cell->terrain],
								 LEGACY_S16_WRAP_ADD(cell->x, 16), cell->y);
			cell->terrain = track_terrain_map[LEGACY_U16_WRAP_ADD(
				(legacy_u16)terrainrows[cell->source_row + 1U], cell->source_column)];
			sprite_copy_image_at(track_editor_terrain_shapes[cell->terrain], cell->x,
								 LEGACY_S16_WRAP_ADD(cell->y, 16));
			cell->terrain = track_terrain_map[LEGACY_U16_WRAP_ADD(
				(legacy_u16)terrainrows[cell->source_row + 1U], cell->source_column + 1U)];
			sprite_copy_image_at(track_editor_terrain_shapes[cell->terrain],
								 LEGACY_S16_WRAP_ADD(cell->x, 16),
								 LEGACY_S16_WRAP_ADD(cell->y, 16));
			sprite_putimage_and(track_editor_tile_masks[cell->tile], cell->x, cell->y);
			sprite_putimage_or(track_editor_tile_shapes[cell->tile], cell->x, cell->y);
			break;
	}
}

static void track_editor_draw_map_cell(struct TRACK_EDITOR_MAP_CELL *cell, legacy_u8 *cached_track,
									   legacy_u8 *cached_terrain)
{
	if (cell->tile == 0) {
		if (cached_track[cell->cache_index] == 0 &&
			cached_terrain[cell->cache_index] == cell->terrain) {
			return;
		}
		sprite_shape_to_1(track_editor_terrain_shapes[cell->terrain], cell->x, cell->y);
		cached_track[cell->cache_index] = 0;
		cached_terrain[cell->cache_index] = cell->terrain;
		return;
	}

	if (cached_track[cell->cache_index] == cell->tile &&
		cached_terrain[cell->cache_index] == cell->terrain) {
		return;
	}
	cached_track[cell->cache_index] = cell->tile;
	cached_terrain[cell->cache_index] = cell->terrain;
	sprite_shape_to_1(track_editor_terrain_shapes[cell->terrain], cell->x, cell->y);
	track_editor_draw_map_element(cell);
	return;
}

static void track_editor_draw_map_continuation(struct TRACK_EDITOR_MAP_CELL *cell,
											   legacy_u16 map_row, legacy_u16 map_column,
											   legacy_u8 *cached_track, legacy_u8 *cached_terrain)
{
	if (map_row != 0 && map_column != 0) {
		cached_track[cell->cache_index] = TRACK_EDITOR_CACHE_INVALID;
		cached_terrain[cell->cache_index] = TRACK_EDITOR_CACHE_INVALID;
		return;
	}
	cached_track[cell->cache_index] = TRACK_EDITOR_CACHE_INVALID;

	legacy_u8 neighbor_tile;
	if (cell->tile == TRACK_TILE_CONTINUATION_EAST && map_column == 0) {
		sprite_copy_image_at(track_editor_terrain_shapes[cell->terrain], cell->x, cell->y);
		cell->terrain = track_terrain_map[LEGACY_U16_WRAP_ADD(
			(legacy_u16)terrainrows[cell->source_row + 1U], cell->source_column)];
		sprite_copy_image_at(track_editor_terrain_shapes[cell->terrain], cell->x,
							 LEGACY_S16_WRAP_ADD(cell->y, 16));
		neighbor_tile = track_element_map[LEGACY_U16_WRAP_SUB(cell->source_index, 1U)];
		sprite_putimage_and(track_editor_tile_masks[neighbor_tile],
							LEGACY_S16_WRAP_SUB(cell->x, 16), cell->y);
		sprite_putimage_or(track_editor_tile_shapes[neighbor_tile],
						   LEGACY_S16_WRAP_SUB(cell->x, 16), cell->y);
	} else if (cell->tile == TRACK_TILE_CONTINUATION_SOUTH && map_row == 0) {
		sprite_copy_image_at(track_editor_terrain_shapes[cell->terrain], cell->x, cell->y);
		cell->terrain = track_terrain_map[LEGACY_U16_WRAP_ADD(
			(legacy_u16)terrainrows[cell->source_row], cell->source_column + 1U)];
		sprite_copy_image_at(track_editor_terrain_shapes[cell->terrain],
							 LEGACY_S16_WRAP_ADD(cell->x, 16), cell->y);
		neighbor_tile = track_element_map[LEGACY_U16_WRAP_ADD(
			track_menu_previous_row(cell->source_row), cell->source_column)];
		sprite_putimage_and(track_editor_tile_masks[neighbor_tile], cell->x,
							LEGACY_S16_WRAP_SUB(cell->y, 16));
		sprite_putimage_or(track_editor_tile_shapes[neighbor_tile], cell->x,
						   LEGACY_S16_WRAP_SUB(cell->y, 16));
	} else if (cell->tile == TRACK_TILE_CONTINUATION_SOUTHEAST && map_row == 0 && map_column == 0) {
		sprite_copy_image_at(track_editor_terrain_shapes[cell->terrain], cell->x, cell->y);
		neighbor_tile = track_element_map[LEGACY_U16_WRAP_SUB(
			LEGACY_U16_WRAP_ADD(track_menu_previous_row(cell->source_row), cell->source_column),
			1U)];
		sprite_putimage_and(track_editor_tile_masks[neighbor_tile],
							LEGACY_S16_WRAP_SUB(cell->x, 16), LEGACY_S16_WRAP_SUB(cell->y, 16));
		sprite_putimage_or(track_editor_tile_shapes[neighbor_tile],
						   LEGACY_S16_WRAP_SUB(cell->x, 16), LEGACY_S16_WRAP_SUB(cell->y, 16));
	}
}

void draw_2DtrackMap(legacy_u8 column_offset, legacy_u8 row_offset, legacy_u8 *cached_track,
					 legacy_u8 *cached_terrain)
{
	struct TRACK_EDITOR_MAP_CELL cell;

	for (legacy_u16 map_row = 0; map_row < 11U; map_row++) {
		for (legacy_u16 map_column = 0; map_column < 12U; map_column++) {
			cell.source_row = LEGACY_U16_WRAP_ADD(row_offset, map_row);
			cell.source_column = LEGACY_U16_WRAP_ADD(column_offset, map_column);
			cell.source_index =
				LEGACY_U16_WRAP_ADD((legacy_u16)trackrows[cell.source_row], cell.source_column);
			cell.tile = track_element_map[cell.source_index];
			cell.terrain = track_terrain_map[LEGACY_U16_WRAP_ADD(
				(legacy_u16)terrainrows[cell.source_row], cell.source_column)];
			cell.cache_index = LEGACY_U16_WRAP_ADD(LEGACY_U16_WRAP_MUL(map_row, 12U), map_column);
			cell.x = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_MUL((legacy_s16)map_column, 16), 8);
			cell.y = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_MUL((legacy_s16)map_row, 16), 4);

			if (cell.tile < TRACK_TILE_CONTINUATION_SOUTHEAST) {
				track_editor_draw_map_cell(&cell, cached_track, cached_terrain);
				continue;
			}

			track_editor_draw_map_continuation(&cell, map_row, map_column, cached_track,
											   cached_terrain);
		}
	}
}

static legacy_u16 track_menu_next_row(legacy_u16 row)
{
	if (row == 29U) {
		return dos_mouse_get_button_count();
	}
	return (legacy_u16)trackrows[row + 1U];
}

static legacy_u16 track_menu_previous_row(legacy_u16 row)
{
	if (row == 0) {
		return (legacy_u16)replay_overflow_acknowledged_word;
	}
	return (legacy_u16)trackrows[row - 1U];
}

static void track_editor_validate_square_links(legacy_u8 *used, legacy_u16 current_index,
											   legacy_u16 east_index, legacy_u16 next_index)
{
	if (used[east_index] != 0 || used[next_index] != 0 ||
		used[LEGACY_U16_WRAP_ADD(next_index, 1U)] != 0 ||
		track_element_map[east_index] != TRACK_TILE_CONTINUATION_EAST ||
		track_element_map[next_index] != TRACK_TILE_CONTINUATION_SOUTH ||
		track_element_map[LEGACY_U16_WRAP_ADD(next_index, 1U)] !=
			TRACK_TILE_CONTINUATION_SOUTHEAST) {
		track_element_map[current_index] = 0;
	} else {
		used[east_index] = 1;
		used[next_index] = 1;
		used[LEGACY_U16_WRAP_ADD(next_index, 1U)] = 1;
	}
}

static void track_editor_validate_tile_links(legacy_u8 *used, legacy_u16 row, legacy_u16 column,
											 legacy_u16 current_index, legacy_u8 tile)
{
	legacy_u8 multi_tile = trkObjectList[tile].ss_multiTileFlag;
	legacy_u16 east_index;
	legacy_u16 next_index;
	switch (multi_tile) {
		case 1:
			next_index = LEGACY_U16_WRAP_ADD(track_menu_next_row(row), column);
			if (used[next_index] != 0 ||
				track_element_map[next_index] != TRACK_TILE_CONTINUATION_SOUTH) {
				track_element_map[current_index] = 0;
			} else {
				used[next_index] = 1;
			}
			break;

		case 2:
			east_index = LEGACY_U16_WRAP_ADD(current_index, 1U);
			if (used[east_index] != 0 ||
				track_element_map[east_index] != TRACK_TILE_CONTINUATION_EAST) {
				track_element_map[current_index] = 0;
			} else {
				used[east_index] = 1;
			}
			break;

		case 3:
			east_index = LEGACY_U16_WRAP_ADD(current_index, 1U);
			next_index = LEGACY_U16_WRAP_ADD(track_menu_next_row(row), column);
			track_editor_validate_square_links(used, current_index, east_index, next_index);
			break;
	}
}

void track_editor_remove_invalid_multitile_links(void)
{
	legacy_u16 current_index;
	legacy_u8 used[900];
	for (current_index = 0; current_index < 900U; current_index++) {
		used[current_index] = 0;
	}

	for (legacy_u16 row = 0; row < 30U; row++) {
		for (legacy_u16 column = 0; column < 30U; column++) {
			current_index = LEGACY_U16_WRAP_ADD(trackrows[row], column);
			legacy_u8 tile = track_element_map[current_index];
			if (tile == 0) {
				continue;
			}
			if (tile >= TRACK_TILE_CONTINUATION_SOUTHEAST) {
				if (used[current_index] == 0) {
					track_element_map[current_index] = 0;
				}
				continue;
			}

			track_editor_validate_tile_links(used, row, column, current_index, tile);
		}
	}
}

static legacy_u8 track_editor_water_tile_allowed(legacy_u8 tile)
{
	return (tile >= TRACK_EDITOR_WATER_RAISED_ROAD_FIRST &&
			tile <= TRACK_EDITOR_WATER_RAISED_ROAD_LAST) ||
		   (tile >= TRACK_EDITOR_WATER_BRIDGE_FIRST && tile <= TRACK_EDITOR_WATER_BRIDGE_LAST) ||
		   (tile >= TRACK_EDITOR_WATER_SHIP_FIRST && tile <= TRACK_EDITOR_WATER_SHIP_LAST);
}

static legacy_u8 track_editor_terrain_error(legacy_u16 row, legacy_u16 column, legacy_u8 terrain,
											legacy_u8 tile)
{
	if (terrain >= 1U && terrain <= 5U) {
		tile = track_editor_map_tile((legacy_u8)column, (legacy_u8)row);

		if (track_editor_water_tile_allowed(tile) == 0) {
			return TRACK_EDITOR_ERROR_INVALID_WATER_TILE;
		}
	} else if (terrain >= 7U && terrain <= 10U) {
		if (subst_hillroad_track(terrain, tile) == 0) {
			return TRACK_EDITOR_ERROR_INVALID_HILL_TILE;
		}
	} else {
		return TRACK_EDITOR_ERROR_INVALID_TERRAIN_TILE;
	}
	return TRACK_EDITOR_VALIDATION_OK;
}

legacy_s16 track_editor_remove_invalid_terrain_tiles(void)
{
	track_editor_remove_invalid_multitile_links();
	legacy_u8 error = TRACK_EDITOR_VALIDATION_OK;
	for (legacy_u16 row = 0; row < 30U; row++) {
		for (legacy_u16 column = 0; column < 30U; column++) {
			legacy_u8 terrain = track_terrain_map[LEGACY_U16_WRAP_ADD(terrainrows[row], column)];
			legacy_u16 current_index = LEGACY_U16_WRAP_ADD(trackrows[row], column);
			legacy_u8 tile = track_element_map[current_index];
			if (tile == 0 || terrain == 0 || terrain == 6U) {
				continue;
			}

			legacy_u8 tile_error = track_editor_terrain_error(row, column, terrain, tile);
			if (tile_error != TRACK_EDITOR_VALIDATION_OK) {
				track_element_map[current_index] = 0;
				error = tile_error;
			}
		}
	}
	if (error != TRACK_EDITOR_VALIDATION_OK) {
		track_editor_remove_invalid_multitile_links();
	}
	return error;
}

static legacy_u8 track_editor_palette_tile(legacy_u8 page, legacy_u8 row, legacy_u8 column)
{
	return progress_box_shape[(legacy_u16)page * 36U + (legacy_u16)row * 6U + column];
}

static void track_editor_skip_previous_placeholders(legacy_u8 page, legacy_u8 *row,
													legacy_u8 *column)
{
	while (track_editor_palette_tile(page, *row, *column) >= TRACK_TILE_CONTINUATION_SOUTH) {
		legacy_u8 tile = track_editor_palette_tile(page, *row, *column);
		if (tile == TRACK_TILE_CONTINUATION_EAST) {
			(*column)--;
		} else {
			(*row)--;
		}
	}
}

static legacy_u8 track_editor_map_tile(legacy_u8 column, legacy_u8 row)
{
	legacy_u16 source_index = LEGACY_U16_WRAP_ADD((legacy_u16)trackrows[row], column);
	legacy_u8 tile = track_element_map[source_index];
	if (tile == TRACK_TILE_CONTINUATION_SOUTHEAST) {
		source_index =
			LEGACY_U16_WRAP_SUB(LEGACY_U16_WRAP_ADD(track_menu_previous_row(row), column), 1U);
		tile = track_element_map[source_index];
	} else if (tile == TRACK_TILE_CONTINUATION_SOUTH) {
		source_index = LEGACY_U16_WRAP_ADD(track_menu_previous_row(row), column);
		tile = track_element_map[source_index];
	} else if (tile == TRACK_TILE_CONTINUATION_EAST) {
		tile = track_element_map[LEGACY_U16_WRAP_SUB(source_index, 1U)];
	}
	return tile;
}

static void track_editor_toggle_highlight(legacy_s16 x, legacy_s16 y, legacy_u8 width,
										  legacy_u8 height)
{
	sprite_xor_rect_outline(x, LEGACY_S16_WRAP_SUB(y, 1), LEGACY_S16_WRAP_ADD(x, width),
							LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(y, height), 1),
							track_editor_highlight_color);
}

static void track_editor_show_message(legacy_s8 far *text_resource, const legacy_s8 *resource_id)
{
	show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
				locate_text_res(text_resource, (legacy_s8 *)resource_id), DIALOG_AUTO_POSITION,
				DIALOG_AUTO_POSITION, performGraphColor, 0, 0);
}

static void track_editor_save_track(legacy_u8 *track_changed, legacy_u8 *map_dirty)
{
	g_is_busy = 1;
	legacy_u8 save_status = 0;
	while (save_status == 0) {
		sprite_select_screen_compat();
		*map_dirty = 1;
		legacy_s8 far *text = locate_text_res((legacy_s8 far *)mainresptr, "trk");
		if (do_savefile_dialog(track_directory, gameconfig.game_trackname, text) == 0) {
			save_status = TRACK_EDITOR_SAVE_CANCELLED;
			break;
		}
		file_build_path(track_directory, gameconfig.game_trackname, ".trk", g_path_buf);
		save_status = 1;
		if (file_find(g_path_buf) != 0) {
			legacy_s16 result = LEGACY_S16_FROM_BITS(
				show_dialog(DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND,
							locate_text_res((legacy_s8 far *)mainresptr, "fex"),
							DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, performGraphColor, 0, 0));
			if (result == -1) {
				save_status = TRACK_EDITOR_SAVE_CANCELLED;
				break;
			}
			if (result == 0) {
				save_status = 0;
				continue;
			}
		}
		legacy_s16 write_result =
			file_write_fatal(g_path_buf, track_element_map, TRACK_EDITOR_TRACK_FILE_SIZE);
		if (write_result == 0) {
			highscore_load_or_create(1);
		}
		if (write_result != 0) {
			track_editor_show_message((legacy_s8 far *)mainresptr, "ser");
			save_status = 0;
		} else {
			*track_changed = 0;
		}
	}
	g_is_busy = 0;
}

static void track_editor_swap_tiles(legacy_u8 *selected_tile, legacy_u8 *saved_tile,
									legacy_u8 *palette_dirty)
{
	legacy_u8 value = *selected_tile;
	*selected_tile = *saved_tile;
	*saved_tile = value;
	*palette_dirty = 1;
}

static legacy_s8 track_editor_terrain_shape_names[] =
	"flatlakelak1lak2lak3lak4highgoungouwgousgouegou1gou2gou3gou4gou5gou6gou7gou8";
static legacy_s8 track_editor_cursor_shape_names[] = "crs0crs1crs2crs3";
static legacy_s8 track_editor_under_cursor_shape_names[] = "ucr0ucr1ucr2ucr3";
static const legacy_s8 track_editor_error_resource_ids[] =
	"eokenseieemseedewwefuenpestejsejdeteewaefteat";
static const struct BUTTON_AREA track_editor_buttons[5] = {{9, 199, 181, 187},
														   {202, 206, 4, 179},
														   {220, 315, 132, 139},
														   {8, 199, 4, 179},
														   {220, 315, 36, 187}};
static const legacy_u16 track_editor_page_keys[10] = {KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
													  KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10};
static const legacy_u8 track_editor_maximum_columns[2] = {30, 6};
static const legacy_u8 track_editor_maximum_rows[2] = {29, 9};

/* Resources and interaction state belong to one editor invocation. */
struct TRACK_EDITOR_SESSION {
	legacy_u8 cached_track[132];
	legacy_u8 cached_terrain[132];
	struct SPRITE far *cursor_sprites[4];
	legacy_s8 far *shape_resource;
	legacy_s8 far *text_resource;
	legacy_s8 far *text_name_resource;
	legacy_u8 selection_column[2];
	legacy_u8 selection_row[2];
	legacy_u8 map_column_offset;
	legacy_u8 map_row_offset;
	legacy_u8 previous_column_offset;
	legacy_u8 previous_row_offset;
	legacy_u8 page;
	legacy_u8 previous_page;
	legacy_u8 selected_tile;
	legacy_u8 saved_tile;
	legacy_u8 last_column;
	legacy_u8 last_row;
	legacy_u8 hovered_tile;
	legacy_u8 previous_hovered_tile;
	legacy_u8 multi_tile;
	legacy_u8 tile_width;
	legacy_u8 tile_height;
	legacy_u8 cursor_width;
	legacy_u8 cursor_height;
	legacy_u8 map_dirty;
	legacy_u8 palette_dirty;
	legacy_u8 scrollbars_dirty;
	legacy_u8 validate_track;
	legacy_u8 validation_error;
	legacy_u8 track_changed;
	legacy_u8 menu_active;
	legacy_u8 blit_mode;
	legacy_u8 cursor_drawn;
	legacy_u8 focus;
	legacy_u8 blink_focus;
	legacy_u8 animation_saved_tile;
	legacy_u16 path_animation_index;
	legacy_u16 blink_timer;
	legacy_u16 key;
	legacy_s16 previous_label_width;
	legacy_s16 cursor_x;
	legacy_s16 cursor_y;
};

static void track_editor_load_resources(struct TRACK_EDITOR_SESSION *editor)
{
	editor->shape_resource = (legacy_s8 far *)file_load_shape2d_fatal("sdtedit");
	locate_many_resources(editor->shape_resource, track_editor_terrain_shape_names,
						  (legacy_s8 far **)track_editor_terrain_shapes);
	locate_many_resources(editor->shape_resource, track_editor_cursor_shape_names,
						  (legacy_s8 far **)track_editor_cursor_shapes);
	locate_many_resources(editor->shape_resource, track_editor_under_cursor_shape_names,
						  (legacy_s8 far **)track_editor_under_cursor_shapes);
	for (legacy_u16 index = 0; index < 4U; index++) {
		editor->cursor_sprites[index] = sprite_make_wnd(
			LEGACY_U16_WRAP_MUL(shape2d_get_width(track_editor_cursor_shapes[index]),
								(legacy_u16)video_shape_width_scale),
			shape2d_get_height(track_editor_cursor_shapes[index]), TRACK_EDITOR_TRANSPARENT_COLOR);
	}

	editor->text_resource = (legacy_s8 far *)file_load_resfile("tedit");
	render_window_sprite = sprite_make_wnd(TRACK_EDITOR_SCREEN_WIDTH, TRACK_EDITOR_SCREEN_HEIGHT,
										   TRACK_EDITOR_TRANSPARENT_COLOR);
	progress_box_shape = (legacy_u8 far *)locate_shape_alt(editor->text_resource, "pbox");
	legacy_s8 far *shape_name_resource = locate_shape_alt(editor->text_resource, "snam");
	legacy_s8 far *mask_name_resource = locate_shape_alt(editor->text_resource, "mnam");
	editor->text_name_resource = locate_shape_alt(editor->text_resource, "tnam");

	for (legacy_u16 index = 0; index < 132U; index++) {
		editor->cached_track[index] = TRACK_EDITOR_CACHE_INVALID;
		editor->cached_terrain[index] = TRACK_EDITOR_CACHE_INVALID;
	}
	for (legacy_u16 index = 0; index < 186U; index++) {
		__fmemcpy(&resID_byte1, shape_name_resource + index * 4U, 4U);
		track_editor_tile_shapes[index] =
			(struct SHAPE2D far *)locate_shape_fatal(editor->shape_resource, &resID_byte1);
		__fmemcpy(&resID_byte1, mask_name_resource + index * 4U, 4U);
		track_editor_tile_masks[index] =
			(struct SHAPE2D far *)locate_shape_fatal(editor->shape_resource, &resID_byte1);
	}
}

static void track_editor_initialize_session(struct TRACK_EDITOR_SESSION *editor)
{
	editor->last_column = TRACK_EDITOR_POSITION_UNSET;
	editor->last_row = TRACK_EDITOR_POSITION_UNSET;
	editor->saved_tile = 0;
	editor->selected_tile = 0;
	editor->previous_page = TRACK_EDITOR_PAGE_UNSET;
	editor->map_dirty = 1;
	editor->palette_dirty = 1;
	editor->scrollbars_dirty = 1;
	editor->validate_track = 1;
	editor->validation_error = TRACK_EDITOR_VALIDATION_OK;
	editor->track_changed = 0;
	editor->menu_active = 1;
	editor->blit_mode = MENU_BLIT_MODE_INITIAL;
	editor->page = 1;
	editor->selection_column[0] = track_validation_column;
	editor->selection_row[0] = track_validation_row;
	editor->selection_column[1] = 0;
	editor->selection_row[1] = 7;
	editor->map_column_offset = 0;
	editor->map_row_offset = 0;
	editor->previous_column_offset = TRACK_EDITOR_POSITION_UNSET;
	editor->previous_row_offset = TRACK_EDITOR_POSITION_UNSET;
	editor->previous_hovered_tile = TRACK_EDITOR_HOVER_UNSET;
	editor->previous_label_width = 0;
	editor->focus = 0;
	editor->path_animation_index = 0;
}

static void track_editor_draw_window(struct TRACK_EDITOR_SESSION *editor)
{
	sprite_select_render_window_and_clear();
	draw_button(locate_text_res(editor->text_resource, "bti"), TRACK_EDITOR_TITLE_X,
				TRACK_EDITOR_TITLE_Y, TRACK_EDITOR_TITLE_WIDTH, TRACK_EDITOR_TITLE_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
	draw_three_color_beveled_border(TRACK_EDITOR_LEFT_FRAME_X, TRACK_EDITOR_LEFT_FRAME_Y,
									TRACK_EDITOR_LEFT_FRAME_WIDTH, TRACK_EDITOR_LEFT_FRAME_HEIGHT,
									11, 9, 1);
	draw_three_color_beveled_border(TRACK_EDITOR_RIGHT_FRAME_X, TRACK_EDITOR_RIGHT_FRAME_Y,
									TRACK_EDITOR_RIGHT_FRAME_WIDTH, TRACK_EDITOR_RIGHT_FRAME_HEIGHT,
									11, 9, 1);
	draw_button(locate_text_res(editor->text_resource, "bsc"), TRACK_EDITOR_WIDE_BUTTON_X,
				TRACK_EDITOR_WIDE_BUTTON_Y, TRACK_EDITOR_WIDE_BUTTON_WIDTH,
				TRACK_EDITOR_BUTTON_HEIGHT, button_top_color, button_bottom_color,
				button_fill_color, 0);
	draw_button(locate_text_res(editor->text_resource, "blo"), TRACK_EDITOR_BUTTON_LEFT_X,
				TRACK_EDITOR_BUTTON_UPPER_Y, TRACK_EDITOR_BUTTON_WIDTH, TRACK_EDITOR_BUTTON_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
	draw_button(locate_text_res(editor->text_resource, "bsa"), TRACK_EDITOR_BUTTON_LEFT_X,
				TRACK_EDITOR_BUTTON_LOWER_Y, TRACK_EDITOR_BUTTON_WIDTH, TRACK_EDITOR_BUTTON_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
	draw_button(locate_text_res(editor->text_resource, "bcl"), TRACK_EDITOR_BUTTON_RIGHT_X,
				TRACK_EDITOR_BUTTON_UPPER_Y, TRACK_EDITOR_BUTTON_WIDTH, TRACK_EDITOR_BUTTON_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
	draw_button(locate_text_res(editor->text_resource, "bex"), TRACK_EDITOR_BUTTON_RIGHT_X,
				TRACK_EDITOR_BUTTON_LOWER_Y, TRACK_EDITOR_BUTTON_WIDTH, TRACK_EDITOR_BUTTON_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
}

static void track_editor_keep_selection_visible(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->selection_column[0] == 29U && editor->tile_width == 2U) {
		editor->selection_column[0]--;
	}
	if (editor->selection_row[0] == 29U && editor->tile_height == 2U) {
		editor->selection_row[0]--;
	}
	while ((legacy_s16)(editor->selection_column[0] - editor->map_column_offset +
						editor->tile_width) > 12) {
		editor->map_column_offset++;
	}
	while (editor->selection_column[0] < editor->map_column_offset) {
		editor->map_column_offset--;
	}
	while ((legacy_s16)(editor->selection_row[0] - editor->map_row_offset + editor->tile_height) >
		   11) {
		editor->map_row_offset++;
	}
	while (editor->selection_row[0] < editor->map_row_offset) {
		editor->map_row_offset--;
	}
	if (editor->map_column_offset != editor->previous_column_offset ||
		editor->map_row_offset != editor->previous_row_offset) {
		editor->previous_column_offset = editor->map_column_offset;
		editor->previous_row_offset = editor->map_row_offset;
		editor->map_dirty = 1;
		editor->scrollbars_dirty = 1;
	}
}

static void track_editor_draw_palette_page(struct TRACK_EDITOR_SESSION *editor)
{
	editor->palette_dirty = 1;
	editor->previous_page = editor->page;
	while (editor->selection_row[1] < 6U &&
		   track_editor_palette_tile(editor->page, editor->selection_row[1],
									 editor->selection_column[1]) >=
			   TRACK_TILE_CONTINUATION_SOUTH) {
		legacy_u8 value = track_editor_palette_tile(editor->page, editor->selection_row[1],
													editor->selection_column[1]);
		if (value == TRACK_TILE_CONTINUATION_EAST) {
			editor->selection_column[1]--;
		} else {
			editor->selection_row[1]--;
		}
	}
	sprite_select_render_window();
	preRender_icons(editor->page);
	if (editor->page == 0) {
		scrollbar_update(0, TRACK_EDITOR_PAGE_BAR_X, TRACK_EDITOR_PAGE_BAR_WIDTH,
						 TRACK_EDITOR_PAGE_BAR_Y, 5, 0, 1, 1);
	} else {
		scrollbar_update(0, TRACK_EDITOR_PAGE_BAR_X, TRACK_EDITOR_PAGE_BAR_WIDTH,
						 TRACK_EDITOR_PAGE_BAR_Y, 5, editor->page - 1U, 1, 10);
	}
}

static void track_editor_update_palette(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->palette_dirty != 0 || editor->page != editor->previous_page) {
		editor->tile_width = 1;
		editor->tile_height = 1;
		editor->multi_tile = 0;
		if (editor->page != 0) {
			editor->multi_tile = (legacy_u8)trkObjectList[editor->selected_tile].ss_multiTileFlag;
			if (editor->multi_tile == 1U) {
				editor->tile_height = 2;
				editor->multi_tile = 1;
			} else if (editor->multi_tile == 2U) {
				editor->tile_width = 2;
				editor->multi_tile = 2;
			} else if (editor->multi_tile == 3U) {
				editor->tile_width = 2;
				editor->tile_height = 2;
				editor->multi_tile = 3;
			}
		}

		if (editor->focus == 0) {
			track_editor_keep_selection_visible(editor);
		}

		if (editor->page != editor->previous_page) {
			track_editor_draw_palette_page(editor);
		}
	}
}

static void track_editor_redraw(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->map_dirty != 0 || editor->palette_dirty != 0) {
		sprite_select_render_window();
		if (editor->map_dirty != 0) {
			editor->map_dirty = 0;
			if (editor->scrollbars_dirty != 0) {
				editor->scrollbars_dirty = 0;
				scrollbar_update(0, TRACK_EDITOR_HORIZONTAL_SCROLLBAR_X, TRACK_EDITOR_LABEL_Y,
								 TRACK_EDITOR_HORIZONTAL_SCROLLBAR_LENGTH, 5,
								 editor->map_column_offset, 12, 30);
				scrollbar_update(0, TRACK_EDITOR_VERTICAL_SCROLLBAR_X, 5, 4,
								 TRACK_EDITOR_VERTICAL_SCROLLBAR_LENGTH, editor->map_row_offset, 11,
								 30);
			}
			sprite_set_target_clip_bounds(TRACK_EDITOR_MAP_LEFT, TRACK_EDITOR_MAP_CLIP_RIGHT,
										  TRACK_EDITOR_MAP_TOP, TRACK_EDITOR_MAP_CLIP_BOTTOM);
			draw_2DtrackMap(editor->map_column_offset, editor->map_row_offset, editor->cached_track,
							editor->cached_terrain);
			sprite_set_target_clip_bounds(0, TRACK_EDITOR_SCREEN_WIDTH, 0,
										  TRACK_EDITOR_SCREEN_HEIGHT);
		}

		if (editor->palette_dirty != 0) {
			editor->palette_dirty = 0;
			sprite_select_target(editor->cursor_sprites[editor->multi_tile]);
			if (editor->page == 0) {
				sprite_shape_to_1(track_editor_terrain_shapes[editor->selected_tile], 0, 0);
				preRender_line(1, 0, TRACK_EDITOR_TILE_PREVIEW_RIGHT, 0, performGraphColor);
				preRender_line(1, TRACK_EDITOR_TILE_PREVIEW_BOTTOM, TRACK_EDITOR_TILE_PREVIEW_RIGHT,
							   TRACK_EDITOR_TILE_PREVIEW_BOTTOM, performGraphColor);
				preRender_line(1, 0, 1, TRACK_EDITOR_TILE_PREVIEW_BOTTOM, performGraphColor);
				preRender_line(TRACK_EDITOR_TILE_PREVIEW_RIGHT, 0, TRACK_EDITOR_TILE_PREVIEW_RIGHT,
							   TRACK_EDITOR_TILE_PREVIEW_BOTTOM, performGraphColor);
			} else {
				sprite_shape_to_1(track_editor_cursor_shapes[editor->multi_tile], 0, 0);
				if (editor->selected_tile != 0) {
					putpixel_iconMask(track_editor_tile_masks[editor->selected_tile], 0, 0);
					putpixel_iconFillings(track_editor_tile_shapes[editor->selected_tile], 0, 0);
				}
			}
		}

		sprite_blit_to_video(render_window_sprite, LEGACY_S8_FROM_BITS(editor->blit_mode));
		editor->blit_mode = MENU_BLIT_MODE_REFRESH;
		editor->previous_hovered_tile = TRACK_EDITOR_HOVER_UNSET;
	}
}

static void track_editor_position_cursor(struct TRACK_EDITOR_SESSION *editor)
{
	sprite_select_screen_compat();
	if (editor->focus == 0) {
		editor->cursor_width = (legacy_u8)(editor->tile_width << 4);
		editor->cursor_height = (legacy_u8)(editor->tile_height << 4);
		editor->cursor_x =
			(legacy_s16)((editor->selection_column[0] - editor->map_column_offset) * 16U + 8U);
		editor->cursor_y =
			(legacy_s16)((editor->selection_row[0] - editor->map_row_offset) * 16U + 4U);
		editor->hovered_tile =
			track_editor_map_tile(editor->selection_column[0], editor->selection_row[0]);
	} else {
		editor->cursor_width = TRACK_EDITOR_TILE_SIZE;
		editor->cursor_height = TRACK_EDITOR_TILE_SIZE;
		editor->cursor_y = (legacy_s16)(editor->selection_row[1] * TRACK_EDITOR_TILE_SIZE +
										TRACK_EDITOR_PALETTE_TOP);
		if (editor->selection_row[1] == 6U) {
			editor->cursor_x = TRACK_EDITOR_PALETTE_LEFT;
			editor->cursor_width = TRACK_EDITOR_PALETTE_WIDTH;
			editor->cursor_height = 8U;
		} else if (editor->selection_row[1] == 7U) {
			editor->selection_column[1] = 0;
			editor->cursor_x = TRACK_EDITOR_PALETTE_LEFT;
			editor->cursor_width = TRACK_EDITOR_PALETTE_WIDTH;
			editor->cursor_y = LEGACY_S16_WRAP_SUB(editor->cursor_y, 8);
		} else if (editor->selection_row[1] > 7U) {
			editor->cursor_y = LEGACY_S16_WRAP_SUB(editor->cursor_y, 8);
			editor->selection_column[1] = editor->selection_column[1] < 3U ? 0U : 3U;
			editor->cursor_x = (legacy_s16)(editor->selection_column[1] * TRACK_EDITOR_TILE_SIZE +
											TRACK_EDITOR_PALETTE_LEFT);
			editor->cursor_width = TRACK_EDITOR_PALETTE_ACTION_WIDTH;
		} else {
			editor->cursor_x = (legacy_s16)(editor->selection_column[1] * TRACK_EDITOR_TILE_SIZE +
											TRACK_EDITOR_PALETTE_LEFT);
			if (track_editor_palette_tile(editor->page, editor->selection_row[1],
										  editor->selection_column[1] + 6U) ==
				TRACK_TILE_CONTINUATION_SOUTH) {
				editor->cursor_height = TRACK_EDITOR_MULTITILE_CURSOR_SIZE;
			}
			if (editor->selection_column[1] < 5U &&
				track_editor_palette_tile(editor->page, editor->selection_row[1],
										  editor->selection_column[1] + 1U) ==
					TRACK_TILE_CONTINUATION_EAST) {
				editor->cursor_width = TRACK_EDITOR_MULTITILE_CURSOR_SIZE;
			}
		}
		editor->hovered_tile =
			editor->selection_row[1] < 6U
				? track_editor_palette_tile(editor->page, editor->selection_row[1],
											editor->selection_column[1])
				: 0;
		if (editor->hovered_tile >= TRACK_TILE_CONTINUATION_SOUTHEAST || editor->page == 0) {
			editor->hovered_tile = 0;
		}
	}
}

static void track_editor_draw_tile_label(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->hovered_tile != editor->previous_hovered_tile) {
		mouse_draw_opaque_check();
		font_set_colors(dialog_fnt_colour, 0);
		legacy_s8 *resource_id = &resID_byte1;
		__fmemcpy(resource_id, editor->text_name_resource + (legacy_u16)editor->hovered_tile * 3U,
				  3U);
		resource_id[3] = 0;
		legacy_s8 far *text = locate_text_res(editor->text_resource, resource_id);
		copy_string(resource_id, text);
		legacy_s16 label_width = (legacy_s16)font_text_width(resource_id);
		font_draw_text_opaque(resource_id, TRACK_EDITOR_MAP_LEFT, TRACK_EDITOR_LABEL_Y);
		if (editor->previous_label_width > label_width) {
			sprite_fill_rect(LEGACY_S16_WRAP_ADD(label_width, TRACK_EDITOR_MAP_LEFT),
							 TRACK_EDITOR_LABEL_Y,
							 LEGACY_S16_WRAP_SUB(editor->previous_label_width, label_width), 8, 0);
		}
		mouse_draw_transparent_check();
		editor->previous_label_width = label_width;
		editor->previous_hovered_tile = editor->hovered_tile;
	}
}

static void track_editor_report_validation(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->validation_error != TRACK_EDITOR_VALIDATION_OK) {
		legacy_s8 *resource_id = (legacy_s8 *)track_editor_error_resource_ids +
								 (legacy_u16)editor->validation_error * 3U;
		__fmemcpy(&resID_byte1, resource_id, 3U);
		*(&resID_byte1 + 3) = 0;
		track_editor_show_message(editor->text_resource, &resID_byte1);
		editor->validation_error = TRACK_EDITOR_VALIDATION_OK;
	}
}

static void track_editor_hover_map(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_u8 clicked_column =
		(legacy_u8)LEGACY_S16_DIV_OR_ZERO(LEGACY_S16_WRAP_SUB(mouse_xpos, 8), 16);
	legacy_u8 clicked_row =
		(legacy_u8)LEGACY_S16_DIV_OR_ZERO(LEGACY_S16_WRAP_SUB(mouse_ypos, 4), 16);
	if (editor->page != 0) {
		if (clicked_row == 10U &&
			((legacy_u8)trkObjectList[editor->selected_tile].ss_multiTileFlag & 1U) != 0) {
			clicked_row--;
		}
		if (clicked_column == 11U &&
			((legacy_u8)trkObjectList[editor->selected_tile].ss_multiTileFlag & 2U) != 0) {
			clicked_column--;
		}
	}
	clicked_column = (legacy_u8)(clicked_column + editor->map_column_offset);
	clicked_row = (legacy_u8)(clicked_row + editor->map_row_offset);
	if (editor->focus != 0 || editor->selection_column[0] != clicked_column ||
		editor->selection_row[0] != clicked_row) {
		editor->focus = 0;
		editor->selection_column[0] = clicked_column;
		editor->selection_row[0] = clicked_row;
		editor->key = 1;
	}
	if (editor->key == KEY_SPACE) {
		editor->key = KEY_ENTER;
	}
}

static void track_editor_hover_palette(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_u8 clicked_column = (legacy_u8)LEGACY_S16_DIV_OR_ZERO(
		LEGACY_S16_WRAP_SUB(mouse_xpos, TRACK_EDITOR_PALETTE_LEFT), TRACK_EDITOR_TILE_SIZE);
	legacy_u8 clicked_row = (legacy_u8)LEGACY_S16_DIV_OR_ZERO(
		LEGACY_S16_WRAP_SUB(mouse_ypos, TRACK_EDITOR_PALETTE_TOP), TRACK_EDITOR_TILE_SIZE);
	if (clicked_row < 6U) {
		if (track_editor_palette_tile(editor->page, clicked_row, clicked_column) ==
			TRACK_TILE_CONTINUATION_SOUTH) {
			clicked_row--;
		}
		if (track_editor_palette_tile(editor->page, clicked_row, clicked_column) ==
			TRACK_TILE_CONTINUATION_EAST) {
			clicked_column--;
		}
	} else {
		clicked_row = (legacy_u8)LEGACY_S16_DIV_OR_ZERO(
			LEGACY_S16_WRAP_SUB(mouse_ypos, TRACK_EDITOR_PALETTE_ACTION_TOP),
			TRACK_EDITOR_TILE_SIZE);
		if (clicked_row == 7U) {
			clicked_column = 0;
		} else if (clicked_column >= 3U) {
			clicked_column = 3;
		} else {
			clicked_column = 0;
		}
	}
	if (editor->focus == 0 || editor->selection_column[1] != clicked_column ||
		editor->selection_row[1] != clicked_row) {
		editor->selection_column[1] = clicked_column;
		editor->selection_row[1] = clicked_row;
		editor->focus = 1;
		editor->key = 1;
	}
	if (editor->key == KEY_SPACE) {
		editor->key = KEY_ENTER;
	}
}

static void track_editor_mouse_input(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_u8 hit = (legacy_u8)mouse_multi_hittest(5, track_editor_buttons);
	if (hit != TRACK_EDITOR_MOUSE_NO_HIT) {
		if (hit == 0U && (mouse_butstate & 3) != 0) {
			editor->focus = 0;
			legacy_u8 clicked_column = (legacy_u8)scrollbar_update(
				1, TRACK_EDITOR_HORIZONTAL_SCROLLBAR_X, TRACK_EDITOR_LABEL_Y,
				TRACK_EDITOR_HORIZONTAL_SCROLLBAR_LENGTH, 5, editor->map_column_offset, 12, 30);
			editor->selection_column[0] = (legacy_u8)(editor->selection_column[0] + clicked_column -
													  editor->map_column_offset);
			editor->map_column_offset = clicked_column;
			editor->key = 1;
		} else if (hit == 1U && (mouse_butstate & 3) != 0) {
			editor->focus = 0;
			legacy_u8 clicked_row = (legacy_u8)scrollbar_update(
				1, TRACK_EDITOR_VERTICAL_SCROLLBAR_X, 5, 4, TRACK_EDITOR_VERTICAL_SCROLLBAR_LENGTH,
				editor->map_row_offset, 11, 30);
			editor->selection_row[0] =
				(legacy_u8)(editor->selection_row[0] + clicked_row - editor->map_row_offset);
			editor->map_row_offset = clicked_row;
			editor->key = 1;
		} else if (hit == 2U) {
			if ((mouse_butstate & 3) != 0) {
				editor->focus = 1;
				editor->page = (legacy_u8)scrollbar_update(
								   1, TRACK_EDITOR_PAGE_BAR_X, TRACK_EDITOR_PAGE_BAR_WIDTH,
								   TRACK_EDITOR_PAGE_BAR_Y, 5, editor->page - 1U, 1, 10) +
							   1U;
				editor->key = 1;
			}
		} else if (hit == 3U) {
			track_editor_hover_map(editor);
		} else if (hit == 4U) {
			track_editor_hover_palette(editor);
		}
	}
}

static void track_editor_wait_for_input(struct TRACK_EDITOR_SESSION *editor)
{
	editor->blink_timer = TRACK_EDITOR_BLINK_INITIAL_COUNT;
	editor->cursor_drawn = 0;
	mouse_draw_opaque_check();
	editor->blink_focus = editor->focus;
	if (editor->blink_focus == 0) {
		sprite_clear_shape_alt(track_editor_under_cursor_shapes[editor->multi_tile],
							   editor->cursor_x, editor->cursor_y);
	}

	editor->key = 0;
	while (editor->key == 0) {
		if (editor->blink_timer > TRACK_EDITOR_BLINK_INTERVAL) {
			mouse_draw_opaque_check();
			if (editor->blink_focus == 0) {
				if (editor->cursor_drawn != 0) {
					sprite_shape_to_1(track_editor_under_cursor_shapes[editor->multi_tile],
									  editor->cursor_x, editor->cursor_y);
				} else {
					sprite_shape_to_1(editor->cursor_sprites[editor->multi_tile]->sprite_bitmapptr,
									  editor->cursor_x, editor->cursor_y);
				}
			} else {
				track_editor_toggle_highlight(editor->cursor_x, editor->cursor_y,
											  editor->cursor_width, editor->cursor_height);
			}
			mouse_draw_transparent_check();
			editor->cursor_drawn ^= 1U;
			editor->blink_timer = 0;
		}

		legacy_u16 delta = (legacy_u16)timer_get_delta_alt();
		editor->blink_timer = LEGACY_U16_WRAP_ADD(editor->blink_timer, delta);
		editor->key = (legacy_u16)input_checking(LEGACY_S16_FROM_BITS(delta));
		track_editor_mouse_input(editor);

		if (editor->key == 1U) {
			editor->last_column = TRACK_EDITOR_POSITION_UNSET;
		}
		if (editor->key == 0 && editor->path_animation_index != 0) {
			editor->key = 1;
		}
	}
	if (editor->path_animation_index != 0) {
		(void)timer_wait_ticks(10UL);
	}

	if (editor->cursor_drawn != 0) {
		mouse_draw_opaque_check();
		if (editor->blink_focus == 0) {
			sprite_shape_to_1(track_editor_under_cursor_shapes[editor->multi_tile],
							  editor->cursor_x, editor->cursor_y);
		} else {
			track_editor_toggle_highlight(editor->cursor_x, editor->cursor_y, editor->cursor_width,
										  editor->cursor_height);
		}
		mouse_draw_transparent_check();
	}
}

static void track_editor_advance_path_animation(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->key == 1U && editor->focus == 0) {
		editor->path_animation_index = (legacy_u16)(track_pieces_counter - 1);
	}
	editor->selection_column[0] = (legacy_u8)track_route_columns[editor->path_animation_index];
	editor->selection_row[0] = (legacy_u8)track_route_rows[editor->path_animation_index];
	editor->selected_tile =
		track_editor_map_tile(editor->selection_column[0], editor->selection_row[0]);
	editor->map_dirty = 1;
	editor->palette_dirty = 1;
	editor->path_animation_index++;
	if (editor->path_animation_index >= (legacy_u16)track_pieces_counter) {
		editor->selected_tile = editor->animation_saved_tile;
		editor->path_animation_index = 0;
	}
	editor->palette_dirty = 1;
	editor->map_dirty = 1;
}

static void track_editor_check_route(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_s16 result = (legacy_s8)track_setup();
	legacy_s8 *resource_id =
		(legacy_s8 *)track_editor_error_resource_ids + (legacy_u16)(legacy_u8)result * 3U;
	__fmemcpy(&resID_byte1, resource_id, 3U);
	*(&resID_byte1 + 3) = 0;
	track_editor_show_message(editor->text_resource, &resID_byte1);
	if (result > 1) {
		editor->focus = 0;
		if (track_pieces_counter == 0) {
			editor->selection_column[0] = track_validation_column;
			editor->selection_row[0] = track_validation_row;
		} else {
			editor->animation_saved_tile = editor->selected_tile;
			editor->selection_column[0] = (legacy_u8)track_route_columns[0];
			editor->selection_row[0] = (legacy_u8)track_route_rows[0];
			editor->selected_tile =
				track_editor_map_tile(editor->selection_column[0], editor->selection_row[0]);
			editor->path_animation_index = 1;
			editor->palette_dirty = 1;
		}
	}
	check_input();
}

static void track_editor_select_skybox(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_u8 dialog_result = LEGACY_S8_FROM_BITS(show_dialog(
		DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, locate_text_res(editor->text_resource, "mss"),
		DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, dialog_border_color, 0,
		track_element_map[TRACK_EDITOR_SKYBOX_MAP_INDEX]));
	if (dialog_result != TRACK_EDITOR_DIALOG_CANCELLED &&
		dialog_result != TRACK_EDITOR_DIALOG_NO_CHANGE) {
		track_element_map[TRACK_EDITOR_SKYBOX_MAP_INDEX] = dialog_result;
		editor->map_dirty = 1;
		editor->track_changed = 1;
	}
}

static void track_editor_select_terrain(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_u8 dialog_result = LEGACY_S8_FROM_BITS(show_dialog(
		DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, locate_text_res(editor->text_resource, "men"),
		DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, dialog_border_color, 0, 0));
	legacy_s8 terrain_id[5];
	struct SHAPE2D far *terrain_shape;
	if (dialog_result != TRACK_EDITOR_DIALOG_CANCELLED &&
		dialog_result != TRACK_EDITOR_DIALOG_NO_CHANGE) {
		for (legacy_u16 index = 0; index < 900U; index++) {
			track_element_map[index] = 0;
		}
		terrain_id[0] = 't';
		terrain_id[1] = 'e';
		terrain_id[2] = 'r';
		terrain_id[3] = (legacy_s8)('0' + dialog_result);
		terrain_id[4] = 0;
		terrain_shape = (struct SHAPE2D far *)locate_shape_alt(editor->text_resource, terrain_id);
		__fmemcpy(track_terrain_map, terrain_shape, 901U);
		gameconfig.game_trackname[0] = 0;
		editor->map_dirty = 1;
		editor->track_changed = 1;
	}
}

static void track_editor_load_track(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_s16 result = 1;
	if (editor->track_changed != 0) {
		result = (legacy_s16)show_dialog(
			DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, locate_text_res(editor->text_resource, "chl"),
			DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, performGraphColor, 0, 0);
	}
	if (result == 0) {
		track_editor_save_track(&editor->track_changed, &editor->map_dirty);
	} else {
		g_is_busy = 1;
		editor->map_dirty = 1;
		legacy_s8 far *text = locate_text_res((legacy_s8 far *)mainresptr, "trk");
		result = do_fileselect_dialog(track_directory, gameconfig.game_trackname, ".trk", text);
		file_build_path(track_directory, gameconfig.game_trackname, ".trk", g_path_buf);
		if (result > 0) {
			file_read_fatal(g_path_buf, track_element_map);
			track_setup();
			editor->focus = 0;
			editor->selection_column[0] = track_validation_column;
			editor->selection_row[0] = track_validation_row;
			editor->track_changed = 0;
			editor->map_dirty = 1;
		}
		g_is_busy = 0;
	}
}

static void track_editor_exit(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_s16 result = 1;
	if (editor->track_changed != 0) {
		result = (legacy_s16)show_dialog(
			DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, locate_text_res(editor->text_resource, "chx"),
			DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, performGraphColor, 0, 0);
	}
	if (result == 0) {
		track_editor_save_track(&editor->track_changed, &editor->map_dirty);
	} else {
		editor->menu_active = 0;
	}
}

static void track_editor_select_palette_tile(struct TRACK_EDITOR_SESSION *editor)
{
	editor->selected_tile = track_editor_palette_tile(editor->page, editor->selection_row[1],
													  editor->selection_column[1]);
	if (editor->page != 0) {
		editor->multi_tile = (legacy_u8)trkObjectList[editor->selected_tile].ss_multiTileFlag;
		if ((editor->multi_tile & 1U) != 0 &&
			editor->selection_row[0] - editor->map_row_offset == 10U) {
			editor->selection_row[0]--;
		}
		if ((editor->multi_tile & 2U) != 0 &&
			editor->selection_column[0] - editor->map_column_offset == 11U) {
			editor->selection_column[0]--;
		}
	}
	editor->palette_dirty = 1;
	editor->focus = 0;
}

static void track_editor_activate_palette(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->selection_row[1] < 6U) {
		track_editor_select_palette_tile(editor);
	} else if (editor->selection_row[1] == 6U) {
		editor->page++;
		if (editor->page > 10U) {
			editor->page = 1;
		}
	} else if (editor->selection_row[1] == 7U) {
		track_editor_select_skybox(editor);
	} else if (editor->selection_row[1] == 8U && editor->selection_column[1] != 0) {
		track_editor_select_terrain(editor);
	} else if (editor->selection_row[1] == 8U) {
		track_editor_load_track(editor);
	} else if (editor->selection_column[1] == 0) {
		track_editor_save_track(&editor->track_changed, &editor->map_dirty);
	} else {
		track_editor_exit(editor);
	}
}

static void track_editor_place_terrain(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_u16 source_index;

	if (editor->selection_column[0] == editor->last_column &&
		editor->selection_row[0] == editor->last_row) {
		track_editor_swap_tiles(&editor->selected_tile, &editor->saved_tile,
								&editor->palette_dirty);
	} else {
		source_index = LEGACY_U16_WRAP_ADD((legacy_u16)terrainrows[editor->selection_row[0]],
										   editor->selection_column[0]);
		editor->saved_tile = track_terrain_map[source_index];
		editor->last_column = editor->selection_column[0];
		editor->last_row = editor->selection_row[0];
	}
	source_index =
		LEGACY_U16_WRAP_ADD((legacy_u16)terrainrows[editor->last_row], editor->last_column);
	track_terrain_map[source_index] = editor->selected_tile;
	editor->track_changed = 1;
	editor->validate_track = 1;
	editor->map_dirty = 1;
}

static void track_editor_place_element(struct TRACK_EDITOR_SESSION *editor)
{
	editor->multi_tile = (legacy_u8)trkObjectList[editor->selected_tile].ss_multiTileFlag;
	if (((editor->multi_tile & 1U) != 0 && editor->selection_row[0] > 28U) ||
		((editor->multi_tile & 2U) != 0 && editor->selection_column[0] > 28U)) {
		editor->palette_dirty = 1;
		editor->map_dirty = 1;
		return;
	}
	legacy_u16 source_index;
	if (editor->selection_column[0] == editor->last_column &&
		editor->selection_row[0] == editor->last_row) {
		track_editor_swap_tiles(&editor->selected_tile, &editor->saved_tile,
								&editor->palette_dirty);
	} else {
		source_index = LEGACY_U16_WRAP_ADD((legacy_u16)trackrows[editor->selection_row[0]],
										   editor->selection_column[0]);
		editor->saved_tile = track_element_map[source_index];
		if (editor->saved_tile >= TRACK_TILE_CONTINUATION_SOUTHEAST) {
			editor->saved_tile = 0;
		}
		editor->last_column = editor->selection_column[0];
		editor->last_row = editor->selection_row[0];
	}
	source_index =
		LEGACY_U16_WRAP_ADD((legacy_u16)trackrows[editor->last_row], editor->last_column);
	track_element_map[source_index] = editor->selected_tile;
	editor->track_changed = 1;
	editor->validate_track = 1;
	editor->map_dirty = 1;
	if (editor->multi_tile == 1U) {
		track_element_map[LEGACY_U16_WRAP_ADD((legacy_u16)trackrows[editor->last_row + 1U],
											  editor->last_column)] = TRACK_TILE_CONTINUATION_SOUTH;
	} else if (editor->multi_tile == 2U) {
		track_element_map[LEGACY_U16_WRAP_ADD(source_index, 1U)] = TRACK_TILE_CONTINUATION_EAST;
	} else if (editor->multi_tile == 3U) {
		track_element_map[LEGACY_U16_WRAP_ADD(source_index, 1U)] = TRACK_TILE_CONTINUATION_EAST;
		source_index =
			LEGACY_U16_WRAP_ADD((legacy_u16)trackrows[editor->last_row + 1U], editor->last_column);
		track_element_map[source_index] = TRACK_TILE_CONTINUATION_SOUTH;
		track_element_map[LEGACY_U16_WRAP_ADD(source_index, 1U)] =
			TRACK_TILE_CONTINUATION_SOUTHEAST;
	}
}

static void track_editor_move_home(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->focus != 0) {
		editor->selection_row[1] = 0;
	} else {
		if (editor->selection_column[0] == editor->map_column_offset &&
			editor->selection_row[0] == editor->map_row_offset) {
			editor->map_column_offset = 0;
			editor->map_row_offset = 0;
		}
		editor->selection_column[0] = editor->map_column_offset;
		editor->selection_row[0] = editor->map_row_offset;
	}
}

static void track_editor_move_up(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->selection_row[editor->focus] != 0) {
		editor->last_column = TRACK_EDITOR_POSITION_UNSET;
		editor->selection_row[editor->focus]--;
		if (editor->focus != 0 && editor->selection_row[1] < 6U) {
			track_editor_skip_previous_placeholders(editor->page, &editor->selection_row[1],
													&editor->selection_column[1]);
		}
	}
}

static void track_editor_move_down(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->selection_row[editor->focus] < track_editor_maximum_rows[editor->focus]) {
		editor->last_column = TRACK_EDITOR_POSITION_UNSET;
		editor->selection_row[editor->focus]++;
		if (editor->focus != 0 && editor->selection_row[1] < 6U) {
			legacy_u8 value = track_editor_palette_tile(editor->page, editor->selection_row[1],
														editor->selection_column[1]);
			if (value == TRACK_TILE_CONTINUATION_EAST) {
				editor->selection_column[1]--;
			} else if (value == TRACK_TILE_CONTINUATION_SOUTH) {
				editor->selection_row[1]++;
			}
		}
	}
}

static void track_editor_move_left(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->focus != 0 && editor->selection_row[1] == 6U) {
		if (editor->page > 1U) {
			editor->page--;
		}
	} else if (editor->selection_column[editor->focus] != 0) {
		editor->last_column = TRACK_EDITOR_POSITION_UNSET;
		editor->selection_column[editor->focus]--;
		if (editor->focus != 0) {
			if (editor->selection_row[1] > 5U) {
				editor->selection_column[1] = 0;
			} else {
				track_editor_skip_previous_placeholders(editor->page, &editor->selection_row[1],
														&editor->selection_column[1]);
			}
		}
	}
}

static void track_editor_move_right(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->focus != 0 && editor->selection_row[1] == 6U) {
		if (editor->page < 10U) {
			editor->page++;
		}
	} else {
		legacy_u8 step = editor->focus != 0 && editor->selection_row[1] > 5U ? 3U : 1U;
		while ((legacy_u16)editor->selection_column[editor->focus] + step <
			   track_editor_maximum_columns[editor->focus]) {
			if (editor->focus == 0 || editor->selection_row[1] > 5U) {
				break;
			}
			legacy_u8 value =
				track_editor_palette_tile(editor->page, editor->selection_row[1],
										  (legacy_u8)(editor->selection_column[1] + step));
			if (value < TRACK_TILE_CONTINUATION_SOUTH) {
				break;
			}
			if (value == TRACK_TILE_CONTINUATION_EAST) {
				step++;
			} else {
				editor->selection_row[1]--;
			}
		}
		if ((legacy_u16)editor->selection_column[editor->focus] + step <
			track_editor_maximum_columns[editor->focus]) {
			editor->last_column = TRACK_EDITOR_POSITION_UNSET;
			editor->selection_column[editor->focus] =
				(legacy_u8)(editor->selection_column[editor->focus] + step);
		}
	}
}

static void track_editor_handle_navigation_key(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->key == (legacy_u16)'+') {
		if (editor->page < 10U) {
			editor->page++;
		}
	} else if (editor->key == (legacy_u16)'-') {
		if (editor->page > 1U) {
			editor->page--;
		}
	} else if (editor->key == KEY_SHIFT_F1) {
		editor->page = 0;
		editor->selected_tile = 0;
	} else if (editor->key == KEY_HOME) {
		track_editor_move_home(editor);
	} else if (editor->key == KEY_UP) {
		track_editor_move_up(editor);
	} else if (editor->key == KEY_DOWN) {
		track_editor_move_down(editor);
	} else if (editor->key == KEY_LEFT) {
		track_editor_move_left(editor);
	} else if (editor->key == KEY_RIGHT) {
		track_editor_move_right(editor);
	}
}

static void track_editor_handle_key(struct TRACK_EDITOR_SESSION *editor)
{
	for (legacy_u16 key_index = 0; key_index < 10U; key_index++) {
		if (editor->key == track_editor_page_keys[key_index]) {
			editor->page = (legacy_u8)(key_index + 1U);
			editor->key = 0;
			break;
		}
	}

	if (editor->key == 'c' || editor->key == 'C') {
		track_editor_check_route(editor);
	} else if (editor->key == KEY_ENTER) {
		if (editor->focus != 0) {
			track_editor_activate_palette(editor);
		} else if (editor->page == 0) {
			track_editor_place_terrain(editor);
		} else {
			track_editor_place_element(editor);
		}
	} else if (editor->key == KEY_SPACE || editor->key == KEY_INSERT) {
		editor->focus ^= 1U;
	} else {
		track_editor_handle_navigation_key(editor);
	}
}

static void track_editor_release_resources(struct TRACK_EDITOR_SESSION *editor)
{
	sprite_free_wnd(render_window_sprite);
	for (legacy_u16 index = 4U; index != 0; index--) {
		sprite_free_wnd(editor->cursor_sprites[index - 1U]);
	}
	unload_resource(editor->text_resource);
	mmgr_free(editor->shape_resource);
}

void load_tracks_menu_shapes(void)
{
#ifdef RESTUNTS_AMIGA
	amiga_video_palette_scene("SDTITL", "!def");
#endif
	struct TRACK_EDITOR_SESSION session;
	struct TRACK_EDITOR_SESSION *editor = &session;

	track_editor_load_resources(editor);
	track_editor_initialize_session(editor);
	track_editor_draw_window(editor);
	while (editor->menu_active != 0) {
		track_editor_update_palette(editor);

		if (editor->validate_track != 0) {
			editor->validate_track = 0;
			editor->validation_error = (legacy_u8)track_editor_remove_invalid_terrain_tiles();
		}

		track_editor_redraw(editor);
		track_editor_position_cursor(editor);
		track_editor_draw_tile_label(editor);
		track_editor_report_validation(editor);
		track_editor_wait_for_input(editor);

		if (editor->path_animation_index != 0) {
			track_editor_advance_path_animation(editor);
			continue;
		}

		track_editor_handle_key(editor);

		if (editor->menu_active != 0) {
			editor->palette_dirty = 1;
			editor->map_dirty = 1;
		}
	}

	track_editor_release_resources(editor);
}
