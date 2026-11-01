#ifdef RESTUNTS_SDL3
#include "extended_frame_internal.h"
#include <stdlib.h>
#include <string.h>

void extended_frame_first_tile_pass(legacy_s16 *first_pass)
{
	if (supersight_enabled != 0 && extended_track_active()) {
		*first_pass = FRAME_TILE_GROUND_PASS;
	}
}

legacy_s32 extended_frame_has_complete_tile_lookup(const struct FRAME_TILE_SELECTION *tiles)
{
	return tiles->extended_lookup.complete_tile_lookup &&
		   tiles->lookahead == tiles->extended_lookahead;
}

legacy_s16 extended_frame_lookup_world_tile(const struct FRAME_TILE_SELECTION *tiles,
											legacy_s16 east, legacy_s16 south)
{
	if (east < 0 || east > FRAME_TRACK_LAST_X || south < 0 || south > TRACK_GRID_LAST_COORDINATE) {
		return -1;
	}
	return tiles->extended_lookup.index_by_world_tile[south * extended_track_width() + east];
}

void extended_frame_extend_lookahead(struct FRAME_TILE_SELECTION *tiles,
									 const struct FRAME_CAMERA *camera, legacy_u8 adaptive)
{
	(void)camera;
	/* HyperVision uses screen depth, so enumerate each world tile once in
	 * stable grid order. Continuations still resolve to their complete object;
	 * the lookup identifies the same submission tile for its cars and wheels. */
	legacy_s16 index = 0;
	memset(tiles->extended_lookup.index_by_world_tile, -1,
		   sizeof(tiles->extended_lookup.index_by_world_tile));
	for (legacy_s16 south = 0; south <= TRACK_GRID_LAST_COORDINATE; south++) {
		for (legacy_s16 east = 0; east <= FRAME_TRACK_LAST_X; east++) {
			if (extended_track_active() &&
				(abs(east - tiles->camera_east) > FRAME_EXTENDED_LOOKAHEAD_RADIUS ||
				 abs(south - tiles->camera_south) > FRAME_EXTENDED_LOOKAHEAD_RADIUS)) {
				continue;
			}
			if (adaptive != 0 &&
				frame_adaptive_flags(&frame_adaptive, east, south) == FRAME_ADAPTIVE_HIDE) {
				continue;
			}
			struct FRAME_LOOKAHEAD_TILE *tile = &tiles->extended_lookahead[index++];
			tile->east = east - tiles->camera_east;
			tile->south = south - tiles->camera_south;
			tile->detail = FRAME_TILE_DETAIL_FULL;
		}
	}
	tiles->count = index;
	tiles->extended_lookup.complete_tile_lookup = 1;
	for (index = 0; index < tiles->count; index++) {
		legacy_s16 east = tiles->extended_lookahead[index].east + tiles->camera_east;
		legacy_s16 south = tiles->extended_lookahead[index].south + tiles->camera_south;
		/* Wrapped signed-byte offsets need the original comparison semantics. */
		if (east < 0 || east > FRAME_TRACK_LAST_X || south < 0 ||
			south > TRACK_GRID_LAST_COORDINATE) {
			tiles->extended_lookup.complete_tile_lookup = 0;
		} else {
			tiles->extended_lookup.index_by_world_tile[south * extended_track_width() + east] =
				index;
		}
	}
	tiles->lookahead = tiles->extended_lookahead;
}

legacy_s16 hires_track_mark_covered_tiles(struct FRAME_TILE_SELECTION *tiles,
										  const struct FRAME_TILE *tile, legacy_s16 tile_index)
{
	if (tile->element != 0) {
		legacy_s16 multitile_flag = trkObjectList[tile->element].ss_multiTileFlag;
		if (multitile_flag != FRAME_MULTITILE_NONE) {
			if (extended_frame_has_complete_tile_lookup(tiles)) {
				if (multitile_flag < FRAME_MULTITILE_ROW || multitile_flag > FRAME_MULTITILE_BOTH) {
					return 1;
				}
				legacy_s16 last_east = tile->east + (multitile_flag != FRAME_MULTITILE_ROW);
				legacy_s16 last_south = tile->south + (multitile_flag != FRAME_MULTITILE_COLUMN);
				for (legacy_s16 south = tile->south; south <= last_south; south++) {
					for (legacy_s16 east = tile->east; east <= last_east; east++) {
						legacy_s16 covered_index =
							extended_frame_lookup_world_tile(tiles, east, south);
						if (covered_index >= 0 && covered_index < tile_index) {
							tiles->markers[covered_index] = FRAME_TILE_MULTITILE_COVERED_MARKER;
						}
					}
				}
				return 1;
			}
		}
	}
	return 0;
}

void hires_track_position_tiles(struct FRAME_TILE_SELECTION *tiles,
								const struct FRAME_CAMERA *camera, legacy_u8 adaptive)
{
	if (supersight_enabled != 0) {
		extended_frame_extend_lookahead(tiles, camera, adaptive);
	}
}

#endif
