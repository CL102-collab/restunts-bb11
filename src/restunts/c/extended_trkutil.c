#include "extended_trkutil.h"
#include "externs.h"
#include "extended_track.h"
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
legacy_s16 extended_trkutil_row_table_count(legacy_s16 width, legacy_s16 height)
{
	legacy_s16 count = width > height ? width : height;
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		count++;
	}
#endif
	return count;
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
