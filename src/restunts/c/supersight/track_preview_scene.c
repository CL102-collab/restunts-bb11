#include "track_preview_scene.h"
#include "../track/trackdata_layout.h"
#include "../track/track_types.h"
#include "../track/track_objects.h"
#include "../externs.h"
#include "settings.h"
#define TRACK_PREVIEW_BRIDGE_FIRST 105U
#define TRACK_PREVIEW_BRIDGE_LAST 108U
#define TRACK_PREVIEW_GRID_SIZE 30U
#define TRACK_PREVIEW_WATER_TERRAIN_FIRST 1U
#define TRACK_PREVIEW_WATER_TERRAIN_LAST 5U

/* Queue covered water with its owner so later continuation cells cannot paint
 * over the road after the owner's primitives have been flushed. */
void supersight_track_preview_draw_covered_water(legacy_u8 track, legacy_u8 terrain,
												 legacy_u8 column, legacy_u8 row,
												 const struct VECTOR *camera,
												 struct TRANSFORMEDSHAPE3D *transformed,
												 SUPERSIGHT_TRACK_PREVIEW_TERRAIN draw_terrain)
{
	if (supersight_enabled == 0) {
		return;
	}
	if (track == 0 || (track >= TRACK_PREVIEW_BRIDGE_FIRST && track <= TRACK_PREVIEW_BRIDGE_LAST &&
					   terrain != TERRAIN_RAISED_TILE)) {
		/* Elevated corners already draw every cell's terrain. */
		return;
	}
	legacy_u8 footprint = trkObjectList[track].ss_multiTileFlag;
	if (footprint < MULTI_TILE_ROW_EDGE_FLAG ||
		footprint > (MULTI_TILE_ROW_EDGE_FLAG | MULTI_TILE_COLUMN_EDGE_FLAG)) {
		return;
	}
	legacy_u8 last_column = column + ((footprint & MULTI_TILE_COLUMN_EDGE_FLAG) != 0);
	legacy_u8 last_row = row + ((footprint & MULTI_TILE_ROW_EDGE_FLAG) != 0);
	for (legacy_u8 adjacent_row = row;
		 adjacent_row <= last_row && adjacent_row < TRACK_PREVIEW_GRID_SIZE; adjacent_row++) {
		for (legacy_u8 adjacent_column = column;
			 adjacent_column <= last_column && adjacent_column < TRACK_PREVIEW_GRID_SIZE;
			 adjacent_column++) {
			if (adjacent_column == column && adjacent_row == row) {
				continue;
			}
			legacy_u8 covered_terrain =
				track_terrain_map[LEGACY_U16_WRAP_ADD(terrainrows[adjacent_row], adjacent_column)];
			if (covered_terrain >= TRACK_PREVIEW_WATER_TERRAIN_FIRST &&
				covered_terrain <= TRACK_PREVIEW_WATER_TERRAIN_LAST) {
				draw_terrain(covered_terrain, adjacent_column, adjacent_row, 0, camera->x,
							 camera->y, camera->z, 0, transformed);
			}
		}
	}
}

legacy_s16 supersight_track_preview_batch_scene(void)
{
	return 0;
}

void supersight_track_preview_flush_scene(legacy_s16 batch_scene)
{
	(void)batch_scene;
}
