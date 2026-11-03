#ifndef RESTUNTS_FRAME_ADAPTIVE_H
#define RESTUNTS_FRAME_ADAPTIVE_H

#include "../legacy.h"
#include "../track/trackdata_layout.h"

#define FRAME_HIRES_ADAPTIVE_TILE_COUNT (TRACK_GRID_SIZE * TRACK_GRID_SIZE)
#define FRAME_HIRES_ADAPTIVE_MAX_QUALITY 4U
#define FRAME_HIRES_ADAPTIVE_FULL_SCALE 4U
#define FRAME_HIRES_ADAPTIVE_HALF_SCALE 2U
#define FRAME_HIRES_ADAPTIVE_MINIMUM_SCALE 1U
#define FRAME_HIRES_ADAPTIVE_FRAME_NS (1000000000ULL / 60U)
#define FRAME_HIRES_ADAPTIVE_RESERVE_NS 1000000ULL
#define FRAME_HIRES_ADAPTIVE_WORK_BUDGET_NS                                                        \
	(FRAME_HIRES_ADAPTIVE_FRAME_NS - FRAME_HIRES_ADAPTIVE_RESERVE_NS)
#define FRAME_HIRES_ADAPTIVE_HEADROOM_PERCENT 85U
#define FRAME_HIRES_ADAPTIVE_PERCENT_ONE 100U
#define FRAME_HIRES_ADAPTIVE_WINDOW_FRAMES 30U
#define FRAME_HIRES_ADAPTIVE_SLOW_FRAMES (FRAME_HIRES_ADAPTIVE_WINDOW_FRAMES / 4U)
#define FRAME_HIRES_ADAPTIVE_RECOVERY_WINDOWS 6U
#define FRAME_HIRES_ADAPTIVE_MAX_RECOVERY_WINDOWS 48U
#define FRAME_HIRES_ADAPTIVE_PROBE_WINDOWS 2U
#define FRAME_HIRES_ADAPTIVE_MAX_SAMPLE_NS (FRAME_HIRES_ADAPTIVE_FRAME_NS * 6U)

enum FRAME_HIRES_ADAPTIVE_FLAGS {
	FRAME_HIRES_ADAPTIVE_FULL = 0U,
	FRAME_HIRES_ADAPTIVE_LOW_GEOMETRY = 1U,
	FRAME_HIRES_ADAPTIVE_HIDE = FRAME_HIRES_ADAPTIVE_LOW_GEOMETRY | 2U
};

enum FRAME_HIRES_ADAPTIVE_QUALITY {
	FRAME_HIRES_ADAPTIVE_FULL_VIEW = 0U,
	FRAME_HIRES_ADAPTIVE_LARGE_VIEW = 1U,
	FRAME_HIRES_ADAPTIVE_HALF_RESOLUTION = 2U,
	FRAME_HIRES_ADAPTIVE_MINIMUM_RESOLUTION = 3U,
	FRAME_HIRES_ADAPTIVE_SMALL_VIEW = FRAME_HIRES_ADAPTIVE_MAX_QUALITY
};

enum FRAME_HIRES_ADAPTIVE_PRESET {
	FRAME_HIRES_ADAPTIVE_PRESET_AUTO = 0U,
	FRAME_HIRES_ADAPTIVE_PRESET_FULL = 1U,
	FRAME_HIRES_ADAPTIVE_PRESET_HIGH = 2U,
	FRAME_HIRES_ADAPTIVE_PRESET_MEDIUM = 3U,
	FRAME_HIRES_ADAPTIVE_PRESET_LOW = 4U
};

struct FRAME_HIRES_ADAPTIVE_STATE {
	/* Increasing stages reduce the view, then resolution, then the view again. */
	legacy_u16 quality, preset;
	legacy_u64 elapsed_ns;
	legacy_u16 samples, slow_samples;
	legacy_u16 headroom_windows, recovery_windows, probe_windows;
	legacy_u8 recovery_probe;
	legacy_u8 cache_valid;
	legacy_u16 cached_mask;
	legacy_s16 cached_east, cached_south, cached_cos, cached_sin;
	legacy_u8 tile_flags[FRAME_HIRES_ADAPTIVE_TILE_COUNT];
};

/* Shared render-only state. Tests may instead use independent state objects. */
extern struct FRAME_HIRES_ADAPTIVE_STATE frame_hires_adaptive;

/* A fresh independent state defaults to automatic quality. */
void frame_hires_adaptive_reset(struct FRAME_HIRES_ADAPTIVE_STATE *adaptive);
/* Clear accumulated timing and cached masks, keeping the selected preset. */
void frame_hires_adaptive_restart(struct FRAME_HIRES_ADAPTIVE_STATE *adaptive);
/* Locked presets select stages zero through three; AUTO restarts at stage zero. */
void frame_hires_adaptive_set_preset(struct FRAME_HIRES_ADAPTIVE_STATE *adaptive,
									 enum FRAME_HIRES_ADAPTIVE_PRESET preset);
/* Processing time only: exclude pacing/presentation waits. Zero samples are ignored.
 * A window spans about half a second at the target rate; recovery needs three
 * seconds at the target rate with spare processing time. Failed probes wait longer.
 * Locked presets ignore timing samples entirely. */
void frame_hires_adaptive_record(struct FRAME_HIRES_ADAPTIVE_STATE *adaptive,
								 legacy_u64 elapsed_ns);
legacy_s32 frame_hires_adaptive_render_scale(const struct FRAME_HIRES_ADAPTIVE_STATE *adaptive);
/* Pass the active camera heading's sine/cosine scaled by TRIG_FIXED_ONE.
 * Cosine=one, sine=zero faces north; positive sine turns toward east.
 * Inverse mapping assigns each world tile to one nearest camera-grid cell.
 * Returns one only when masks were rebuilt; full quality performs no policy work. */
legacy_s32 frame_hires_adaptive_prepare(struct FRAME_HIRES_ADAPTIVE_STATE *adaptive,
										legacy_s16 camera_east, legacy_s16 camera_south,
										legacy_s16 heading_cos, legacy_s16 heading_sin);
/* AND footprint flags together: any full tile keeps full geometry, any low
 * tile keeps low geometry, and only an entirely hidden footprint disappears. */
legacy_u8 frame_hires_adaptive_flags(const struct FRAME_HIRES_ADAPTIVE_STATE *adaptive,
									 legacy_s16 east, legacy_s16 south);

#endif
