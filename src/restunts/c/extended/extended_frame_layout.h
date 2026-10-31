#ifndef RESTUNTS_EXTENDED_FRAME_LAYOUT_H
#define RESTUNTS_EXTENDED_FRAME_LAYOUT_H

#include "../legacy.h"

struct FRAME_TILE_SELECTION;

void extended_frame_begin_tile_selection(struct FRAME_TILE_SELECTION *tiles);
void extended_frame_adaptive_policy(legacy_u8 *active);
legacy_s16 extended_frame_find_indexed_wheel(const struct FRAME_TILE_SELECTION *tiles,
											 legacy_s8 east, legacy_s8 south, legacy_s16 wheel,
											 legacy_s16 *best_tile_index, legacy_s16 *matched_wheel,
											 legacy_s8 *result_east, legacy_s8 *result_south);

#endif
