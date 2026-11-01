#include "extended_trkutil.h"
#include "../externs.h"
#include "extended_track.h"
#include "../track/trackdata_layout.h"
legacy_s16 extended_trkutil_row_position(legacy_u16 row, legacy_s16 *position)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		*position = track_row_positions[row];
		return 1;
	}
#else
	(void)row;
	(void)position;
#endif
	return 0;
}
legacy_s16 extended_trkutil_column_position(legacy_u16 column, legacy_s16 *position)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		*position = track_column_positions[column];
		return 1;
	}
#else
	(void)column;
	(void)position;
#endif
	return 0;
}
static legacy_s16 extended_trkutil_row_table_count(legacy_s16 width, legacy_s16 height)
{
	legacy_s16 count = width > height ? width : height;
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		count++;
	}
#endif
	return count;
}
void extended_trkutil_init_row_tables(void)
{
	legacy_s16 width = (legacy_s16)extended_track_width();
	legacy_s16 height = (legacy_s16)extended_track_height();
	legacy_s16 count = extended_trkutil_row_table_count(width, height);
	for (legacy_s16 i = 0; i < count; i++) {
		legacy_s16 inverse_row = LEGACY_S16_WRAP_SUB(height - 1, i);
		legacy_s16 track_position = LEGACY_S16_SHL(inverse_row, TRACK_TILE_POSITION_SHIFT);
		legacy_s16 terrain_position = LEGACY_S16_SHL(i, TRACK_TILE_POSITION_SHIFT);
		trackrows[i] = LEGACY_S16_WRAP_MUL(width, inverse_row);
		terrainrows[i] = LEGACY_S16_WRAP_MUL(width, i);
		track_row_positions[i] = track_position;
		track_column_positions[i] = terrain_position;
		track_row_centers[i] = LEGACY_S16_WRAP_ADD(track_position, TRACK_TILE_HALF_SIZE);
		terrainpos[i] = terrain_position;
		terraincenterpos[i] = LEGACY_S16_WRAP_ADD(terrain_position, TRACK_TILE_HALF_SIZE);
		track_column_centers[i] = LEGACY_S16_WRAP_ADD(terrain_position, TRACK_TILE_HALF_SIZE);
	}
}
legacy_u8 extended_trkutil_horizon(legacy_u8 legacy_horizon)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		return extended_track_model()->horizon;
	}
#endif
	return legacy_horizon;
}
