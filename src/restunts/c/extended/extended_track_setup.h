#ifndef RESTUNTS_EXTENDED_TRACK_SETUP_H
#define RESTUNTS_EXTENDED_TRACK_SETUP_H
#include "../legacy.h"
#include "extended_track.h"
#define TRACK_SETUP_LEGACY_BRANCH_COUNT 64U
#ifdef RESTUNTS_SDL3
#define TRACK_SETUP_BRANCH_COUNT EXTENDED_TRACK_ROUTE_CAPACITY
#define TRACK_SETUP_VISITED_TILE_CAPACITY (EXTENDED_TRACK_MAX_CELLS + 4U)
#define TRACK_SETUP_PIECE_CAPACITY (EXTENDED_TRACK_ROUTE_CAPACITY + 1U)
#else
#define TRACK_SETUP_BRANCH_COUNT TRACK_SETUP_LEGACY_BRANCH_COUNT
#define TRACK_SETUP_VISITED_TILE_CAPACITY 904U
#define TRACK_SETUP_PIECE_CAPACITY 902U
#endif
struct TRACK_SETUP_WALK;
struct TRKOBJINFO;
struct VECTOR;
legacy_s16 extended_track_setup_terrain_validated(void);
legacy_s16 extended_track_setup_legacy_elements(void);
void extended_track_setup_start_element(struct TRACK_SETUP_WALK *walk);
legacy_s16 extended_track_setup_piece_matches(legacy_u8 existing, legacy_u8 current);
legacy_u16 extended_track_setup_branch_limit(void);
legacy_u16 extended_track_setup_cell_count(void);
const struct TRKOBJINFO *extended_track_setup_info(legacy_s16 column, legacy_s16 row,
												   legacy_u16 id);
legacy_u8 extended_track_setup_jump_limit(const struct TRACK_SETUP_WALK *walk,
										  legacy_u8 legacy_limit);
legacy_u8 extended_track_setup_connection_code(legacy_u8 code);
legacy_s16 extended_track_setup_reversible(const struct TRACK_SETUP_WALK *walk);
legacy_s16 extended_track_setup_resolve_piece(struct TRACK_SETUP_WALK *walk, legacy_s16 *status);
void extended_track_setup_jump_source(const struct TRACK_SETUP_WALK *walk, legacy_s8 *column,
									  legacy_s8 *row);
legacy_s16 extended_track_setup_roadside_height(const struct TRACK_SETUP_WALK *walk,
												struct VECTOR *vector);
legacy_s16 extended_track_setup_camera_height(const struct TRACK_SETUP_WALK *walk,
											  const struct VECTOR *vector, legacy_s16 far *height);
legacy_s16 extended_track_setup_jump_connection(legacy_u8 code);
#endif
