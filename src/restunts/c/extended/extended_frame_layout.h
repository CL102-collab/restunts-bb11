#ifndef RESTUNTS_EXTENDED_FRAME_LAYOUT_H
#define RESTUNTS_EXTENDED_FRAME_LAYOUT_H

#include "../legacy.h"
#if defined(RESTUNTS_SDL3)
#include "extended_frame_limits.h"

/* Indexed lookup storage follows the supported extended track area. */
struct EXTENDED_FRAME_TILE_LOOKUP {
	legacy_s16 index_by_world_tile[EXTENDED_FRAME_MAXIMUM_TILE_COUNT];
	legacy_s32 complete_tile_lookup;
};
#endif

struct FRAME_TILE_SELECTION;

void hires_extended_frame_begin_tile_selection(struct FRAME_TILE_SELECTION *tiles);
void hires_extended_frame_adaptive_policy(legacy_u8 *active);
legacy_s16 hires_extended_frame_find_indexed_wheel(const struct FRAME_TILE_SELECTION *tiles,
												   legacy_s8 east, legacy_s8 south,
												   legacy_s16 wheel, legacy_s16 *best_tile_index,
												   legacy_s16 *matched_wheel,
												   legacy_s8 *result_east, legacy_s8 *result_south);

#endif
