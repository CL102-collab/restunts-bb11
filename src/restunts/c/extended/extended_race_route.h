#ifndef RESTUNTS_EXTENDED_RACE_ROUTE_H
#define RESTUNTS_EXTENDED_RACE_ROUTE_H
#include "../legacy.h"
#include "extended_track.h"
#ifdef RESTUNTS_SDL3
#define PENALTY_ROUTE_VISITED_CAPACITY (EXTENDED_TRACK_ROUTE_CAPACITY + 3U)
#define PENALTY_ROUTE_PENDING_CAPACITY EXTENDED_TRACK_ROUTE_CAPACITY
#else
#define PENALTY_ROUTE_VISITED_CAPACITY 904U
#define PENALTY_ROUTE_PENDING_CAPACITY 128U
#endif
legacy_s16 extended_race_route_alternate(legacy_s16 legacy_track);
legacy_s16 extended_race_route_player_row(legacy_s16 fallback);
legacy_s16 extended_race_route_outside(legacy_s16 column, legacy_s16 row, legacy_s16 fallback);
legacy_s16 extended_race_route_sentinel_bounds(legacy_s16 piece, legacy_u8 *minimum_row,
											   legacy_u8 *maximum_row, legacy_u8 *minimum_column,
											   legacy_u8 *maximum_column);
legacy_s16 extended_race_route_contains(legacy_s16 column, legacy_s16 row, legacy_s16 piece,
										legacy_u8 owner_column, legacy_u8 owner_row,
										legacy_s16 *contains);
legacy_s16 extended_race_route_current_matches(legacy_s16 piece, legacy_s16 column, legacy_s16 row);
void extended_race_route_add_penalty(legacy_s16 skipped, legacy_u16 rate, legacy_u16 seconds);
#endif
