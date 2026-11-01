#include "extended_frame_layout.h"

#if defined(RESTUNTS_SDL3)
#include "extended_frame.h"
#include "../frame/frame_render_private.h"

void hires_extended_frame_begin_tile_selection(struct FRAME_TILE_SELECTION *tiles)
{
	tiles->extended_lookup.complete_tile_lookup = 0;
}

void hires_extended_frame_adaptive_policy(legacy_u8 *active)
{
	if (*active != 0 && !extended_frame_uses_legacy_tiles()) {
		*active = 0;
	}
}

legacy_s16 hires_extended_frame_find_indexed_wheel(const struct FRAME_TILE_SELECTION *tiles,
												   legacy_s8 east, legacy_s8 south,
												   legacy_s16 wheel, legacy_s16 *best_tile_index,
												   legacy_s16 *matched_wheel,
												   legacy_s8 *result_east, legacy_s8 *result_south)
{
	if (!hires_extended_frame_has_complete_tile_lookup(tiles)) {
		return 0;
	}
	legacy_s16 tile_index = hires_extended_frame_lookup_world_tile(tiles, east, south);
	if (tile_index > *best_tile_index &&
		tiles->markers[tile_index] != FRAME_TILE_UNAVAILABLE_MARKER) {
		*result_east = east;
		*result_south = south;
		*best_tile_index = tile_index;
		*matched_wheel = wheel;
	}
	return 1;
}
#endif
