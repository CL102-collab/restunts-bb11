#include "menu_track_editor_internal.h"

static legacy_u16 track_menu_previous_row(legacy_u16 row);

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

legacy_u8 track_editor_map_tile(legacy_u8 column, legacy_u8 row)
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
