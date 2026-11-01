#include <string.h>
#include "frame_adaptive.h"
#include "../math/math.h"

#define FRAME_HIRES_ADAPTIVE_BACKOFF_FACTOR 2U
#define FRAME_HIRES_ADAPTIVE_HALF_CELL (TRIG_FIXED_ONE / 2)

struct FRAME_HIRES_ADAPTIVE_STATE frame_hires_adaptive;

void frame_hires_adaptive_reset(struct FRAME_HIRES_ADAPTIVE_STATE *adaptive)
{
	memset(adaptive, 0, sizeof(*adaptive));
	adaptive->recovery_windows = FRAME_HIRES_ADAPTIVE_RECOVERY_WINDOWS;
}

void frame_hires_adaptive_set_preset(struct FRAME_HIRES_ADAPTIVE_STATE *adaptive,
									 enum FRAME_HIRES_ADAPTIVE_PRESET preset)
{
	if (preset < FRAME_HIRES_ADAPTIVE_PRESET_AUTO || preset > FRAME_HIRES_ADAPTIVE_PRESET_LOW) {
		preset = FRAME_HIRES_ADAPTIVE_PRESET_AUTO;
	}
	frame_hires_adaptive_reset(adaptive);
	adaptive->preset = preset;
	if (preset != FRAME_HIRES_ADAPTIVE_PRESET_AUTO) {
		adaptive->quality = preset - FRAME_HIRES_ADAPTIVE_PRESET_FULL;
	}
}

void frame_hires_adaptive_restart(struct FRAME_HIRES_ADAPTIVE_STATE *adaptive)
{
	frame_hires_adaptive_set_preset(adaptive, (enum FRAME_HIRES_ADAPTIVE_PRESET)adaptive->preset);
}

static void hires_adaptive_finish_window(struct FRAME_HIRES_ADAPTIVE_STATE *adaptive)
{
	legacy_u64 budget = FRAME_HIRES_ADAPTIVE_WORK_BUDGET_NS * FRAME_HIRES_ADAPTIVE_WINDOW_FRAMES;
	if (adaptive->elapsed_ns > budget &&
		adaptive->slow_samples >= FRAME_HIRES_ADAPTIVE_SLOW_FRAMES) {
		if (adaptive->quality < FRAME_HIRES_ADAPTIVE_MAX_QUALITY) {
			adaptive->quality++;
		}
		adaptive->headroom_windows = 0;
		if (adaptive->recovery_probe != 0) {
			adaptive->recovery_windows *= FRAME_HIRES_ADAPTIVE_BACKOFF_FACTOR;
			if (adaptive->recovery_windows > FRAME_HIRES_ADAPTIVE_MAX_RECOVERY_WINDOWS) {
				adaptive->recovery_windows = FRAME_HIRES_ADAPTIVE_MAX_RECOVERY_WINDOWS;
			}
		}
		adaptive->recovery_probe = 0;
		adaptive->probe_windows = 0;
	} else if (adaptive->slow_samples == 0 &&
			   adaptive->elapsed_ns <= budget * FRAME_HIRES_ADAPTIVE_HEADROOM_PERCENT /
										   FRAME_HIRES_ADAPTIVE_PERCENT_ONE) {
		if (adaptive->recovery_probe != 0) {
			adaptive->probe_windows++;
			if (adaptive->probe_windows >= FRAME_HIRES_ADAPTIVE_PROBE_WINDOWS) {
				adaptive->recovery_probe = 0;
				adaptive->probe_windows = 0;
				adaptive->recovery_windows /= FRAME_HIRES_ADAPTIVE_BACKOFF_FACTOR;
				if (adaptive->recovery_windows < FRAME_HIRES_ADAPTIVE_RECOVERY_WINDOWS) {
					adaptive->recovery_windows = FRAME_HIRES_ADAPTIVE_RECOVERY_WINDOWS;
				}
			}
		}
		if (adaptive->quality != 0) {
			adaptive->headroom_windows++;
			if (adaptive->headroom_windows >= adaptive->recovery_windows) {
				adaptive->quality--;
				adaptive->headroom_windows = 0;
				adaptive->recovery_probe = 1;
				adaptive->probe_windows = 0;
			}
		}
	} else {
		adaptive->headroom_windows = 0;
		adaptive->probe_windows = 0;
	}
	adaptive->elapsed_ns = 0;
	adaptive->samples = 0;
	adaptive->slow_samples = 0;
}

void frame_hires_adaptive_record(struct FRAME_HIRES_ADAPTIVE_STATE *adaptive, legacy_u64 elapsed_ns)
{
	if (adaptive->preset != FRAME_HIRES_ADAPTIVE_PRESET_AUTO || elapsed_ns == 0) {
		return;
	}
	if (adaptive->quality > FRAME_HIRES_ADAPTIVE_MAX_QUALITY) {
		adaptive->quality = FRAME_HIRES_ADAPTIVE_MAX_QUALITY;
	}
	if (adaptive->recovery_windows < FRAME_HIRES_ADAPTIVE_RECOVERY_WINDOWS) {
		adaptive->recovery_windows = FRAME_HIRES_ADAPTIVE_RECOVERY_WINDOWS;
	}
	if (elapsed_ns > FRAME_HIRES_ADAPTIVE_MAX_SAMPLE_NS) {
		elapsed_ns = FRAME_HIRES_ADAPTIVE_MAX_SAMPLE_NS;
	}
	adaptive->elapsed_ns += elapsed_ns;
	adaptive->samples++;
	if (elapsed_ns > FRAME_HIRES_ADAPTIVE_WORK_BUDGET_NS) {
		adaptive->slow_samples++;
	}
	if (adaptive->samples == FRAME_HIRES_ADAPTIVE_WINDOW_FRAMES) {
		hires_adaptive_finish_window(adaptive);
	}
}

legacy_s32 frame_hires_adaptive_render_scale(const struct FRAME_HIRES_ADAPTIVE_STATE *adaptive)
{
	if (adaptive->quality < FRAME_HIRES_ADAPTIVE_HALF_RESOLUTION) {
		return FRAME_HIRES_ADAPTIVE_FULL_SCALE;
	}
	if (adaptive->quality == FRAME_HIRES_ADAPTIVE_HALF_RESOLUTION) {
		return FRAME_HIRES_ADAPTIVE_HALF_SCALE;
	}
	return FRAME_HIRES_ADAPTIVE_MINIMUM_SCALE;
}

enum {
	ADAPTIVE_LARGE_MASK = 1,
	ADAPTIVE_SMALL_MASK = 2,
	ADAPTIVE_LARGE_WIDTH = 9,
	ADAPTIVE_LARGE_HEIGHT = 10,
	ADAPTIVE_SMALL_WIDTH = 5,
	ADAPTIVE_SMALL_HEIGHT = 5
};

static const legacy_char hires_large_mask[ADAPTIVE_LARGE_HEIGHT][ADAPTIVE_LARGE_WIDTH + 1] = {
	"..LLLLL..", ".LLLLLLL.", ".LLLLLLL.", "LLHHHHHLL", "LLHHHHHLL",
	"LLHHHHHLL", "LLHHHHHLL", ".LLHHHLL.", "..LHHHL..", "...HCH..."};
static const legacy_char hires_small_mask[ADAPTIVE_SMALL_HEIGHT][ADAPTIVE_SMALL_WIDTH + 1] = {
	"LLLLL", "LLLLL", "LHHHL", "HHHHH", ".HCH."};

static legacy_u16 hires_adaptive_mask_kind(legacy_u16 quality)
{
	return quality < FRAME_HIRES_ADAPTIVE_SMALL_VIEW ? ADAPTIVE_LARGE_MASK : ADAPTIVE_SMALL_MASK;
}

static legacy_s32 hires_adaptive_tile_index(legacy_s32 east, legacy_s32 south)
{
	if (east < 0 || east >= TRACK_GRID_SIZE || south < 0 || south >= TRACK_GRID_SIZE) {
		return -1;
	}
	return south * TRACK_GRID_SIZE + east;
}

static legacy_s32 hires_adaptive_nearest_cell(legacy_s64 coordinate)
{
	/* Symmetric rounding keeps opposite headings exact reflections, including
	 * half-cell boundaries. Each world tile is queried once, so rotating the
	 * mask cannot duplicate cells or leave forward-mapped holes. */
	if (coordinate < 0) {
		return -((-coordinate + FRAME_HIRES_ADAPTIVE_HALF_CELL) / TRIG_FIXED_ONE);
	}
	return (coordinate + FRAME_HIRES_ADAPTIVE_HALF_CELL) / TRIG_FIXED_ONE;
}

static legacy_u8 hires_adaptive_mask_flags(legacy_u16 mask, legacy_s32 right, legacy_s32 forward)
{
	legacy_s32 width = mask == ADAPTIVE_LARGE_MASK ? ADAPTIVE_LARGE_WIDTH : ADAPTIVE_SMALL_WIDTH;
	legacy_s32 height = mask == ADAPTIVE_LARGE_MASK ? ADAPTIVE_LARGE_HEIGHT : ADAPTIVE_SMALL_HEIGHT;
	legacy_s32 column = right + width / 2;
	if (column < 0 || column >= width || forward < 0 || forward >= height) {
		return FRAME_HIRES_ADAPTIVE_HIDE;
	}
	legacy_s32 row = height - 1 - forward;
	legacy_char cell =
		mask == ADAPTIVE_LARGE_MASK ? hires_large_mask[row][column] : hires_small_mask[row][column];
	if (cell == 'H' || cell == 'C') {
		return FRAME_HIRES_ADAPTIVE_FULL;
	}
	return cell == 'L' ? FRAME_HIRES_ADAPTIVE_LOW_GEOMETRY : FRAME_HIRES_ADAPTIVE_HIDE;
}

legacy_s32 frame_hires_adaptive_prepare(struct FRAME_HIRES_ADAPTIVE_STATE *adaptive,
										legacy_s16 camera_east, legacy_s16 camera_south,
										legacy_s16 heading_cos, legacy_s16 heading_sin)
{
	if (adaptive->quality == FRAME_HIRES_ADAPTIVE_FULL_VIEW) {
		return 0;
	}
	legacy_u16 mask = hires_adaptive_mask_kind(adaptive->quality);
	if (adaptive->cache_valid != 0 && adaptive->cached_mask == mask &&
		adaptive->cached_east == camera_east && adaptive->cached_south == camera_south &&
		adaptive->cached_cos == heading_cos && adaptive->cached_sin == heading_sin) {
		return 0;
	}
	for (legacy_s32 south = 0; south < TRACK_GRID_SIZE; south++) {
		for (legacy_s32 east = 0; east < TRACK_GRID_SIZE; east++) {
			legacy_s32 delta_east = east - (legacy_s32)camera_east;
			legacy_s32 delta_south = south - (legacy_s32)camera_south;
			legacy_s32 right = hires_adaptive_nearest_cell((legacy_s64)delta_east * heading_cos +
														   (legacy_s64)delta_south * heading_sin);
			legacy_s32 forward = hires_adaptive_nearest_cell((legacy_s64)delta_east * heading_sin -
															 (legacy_s64)delta_south * heading_cos);
			adaptive->tile_flags[south * TRACK_GRID_SIZE + east] =
				hires_adaptive_mask_flags(mask, right, forward);
		}
	}
	adaptive->cached_mask = mask;
	adaptive->cached_east = camera_east;
	adaptive->cached_south = camera_south;
	adaptive->cached_cos = heading_cos;
	adaptive->cached_sin = heading_sin;
	adaptive->cache_valid = 1;
	return 1;
}

void frame_hires_adaptive_invalidate(struct FRAME_HIRES_ADAPTIVE_STATE *adaptive)
{
	adaptive->cache_valid = 0;
}

legacy_u8 frame_hires_adaptive_flags(const struct FRAME_HIRES_ADAPTIVE_STATE *adaptive,
									 legacy_s16 east, legacy_s16 south)
{
	if (adaptive->quality == FRAME_HIRES_ADAPTIVE_FULL_VIEW) {
		return FRAME_HIRES_ADAPTIVE_FULL;
	}
	legacy_s32 index = hires_adaptive_tile_index(east, south);
	if (index < 0) {
		return FRAME_HIRES_ADAPTIVE_HIDE;
	}
	if (adaptive->cache_valid == 0 ||
		adaptive->cached_mask != hires_adaptive_mask_kind(adaptive->quality)) {
		return FRAME_HIRES_ADAPTIVE_FULL;
	}
	return adaptive->tile_flags[index];
}
