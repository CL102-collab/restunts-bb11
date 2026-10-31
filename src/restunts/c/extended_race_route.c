#include "extended_race_route.h"
#include "externs.h"
#include "track_objects.h"
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include "extended_replay_runtime.h"
#endif
#define EXTENDED_ROUTE_SENTINEL (-1)
#define EXTENDED_ROUTE_INVALID_COORDINATE LEGACY_U8_MAX
legacy_s16 extended_race_route_alternate(legacy_s16 legacy_track)
{
	return extended_track_active() ? EXTENDED_ROUTE_SENTINEL : legacy_track;
}
legacy_s16 extended_race_route_sentinel_bounds(legacy_s16 piece, legacy_u8 *minimum_row,
											   legacy_u8 *maximum_row, legacy_u8 *minimum_column,
											   legacy_u8 *maximum_column)
{
	if (extended_track_active() && piece == EXTENDED_ROUTE_SENTINEL) {
		*minimum_row = *maximum_row = EXTENDED_ROUTE_INVALID_COORDINATE;
		*minimum_column = *maximum_column = EXTENDED_ROUTE_INVALID_COORDINATE;
		return 1;
	}
	return 0;
}
#ifdef RESTUNTS_SDL3
static legacy_s16 extended_route_owner_matches(legacy_s16 column, legacy_s16 row, legacy_s16 piece,
											   legacy_u8 owner_column, legacy_u8 owner_row)
{
	if (piece < 0 || piece >= track_pieces_counter) {
		return 0;
	}
	const struct CSTRK_OBJECT *object = extended_track_object_at(column, row);
	return object && object->x == owner_column && object->y == owner_row &&
		   extended_track_object_id(object) == (legacy_u8)track_route_element_ids[piece];
}
#endif
legacy_s16 extended_race_route_contains(legacy_s16 column, legacy_s16 row, legacy_s16 piece,
										legacy_u8 owner_column, legacy_u8 owner_row,
										legacy_s16 *contains)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		*contains = extended_route_owner_matches(column, row, piece, owner_column, owner_row);
		return 1;
	}
#else
	(void)column;
	(void)row;
	(void)piece;
	(void)owner_column;
	(void)owner_row;
	(void)contains;
#endif
	return 0;
}
legacy_s16 extended_race_route_current_matches(legacy_s16 piece, legacy_s16 column, legacy_s16 row)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		if (piece < 0 || piece >= track_pieces_counter) {
			return 0;
		}
		return extended_route_owner_matches(column, row, piece,
											(legacy_u8)track_route_columns[piece],
											(legacy_u8)track_route_rows[piece]);
	}
#else
	(void)piece;
	(void)column;
	(void)row;
#endif
	return 1;
}
void extended_race_route_add_penalty(legacy_s16 skipped, legacy_u16 rate, legacy_u16 seconds)
{
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
	if (extended_replay_is_active()) {
		legacy_u32 added = (legacy_u32)skipped * rate * seconds;
		if (UINT32_MAX - extended_replay_clocks.penalty < added) {
			extended_replay_clocks.penalty = UINT32_MAX;
		} else {
			extended_replay_clocks.penalty += added;
		}
	}
#else
	(void)skipped;
	(void)rate;
	(void)seconds;
#endif
}
