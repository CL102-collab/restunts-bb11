#ifndef RESTUNTS_GHOST_H
#define RESTUNTS_GHOST_H

#include "gamestate.h"

struct SIMD;

/* Camera state follows the source player at the cached replay frame. */
struct GHOST_CAMERA_STATE {
	struct VECTOR follow_position;
	struct VECTOR previous_position;
	legacy_s16 trackside_index;
	legacy_s16 frame;
	legacy_s16 crash_frame;
};

/* Selection never installs the replay as the player's current recording. */
legacy_s16 ghost_select_replay(const legacy_s8 *directory, const legacy_s8 *name);
legacy_s16 ghost_is_selected(void);
void ghost_clear(void);
void ghost_check_track(void);

/* Prepare before loading live race resources or installing the frame callback. */
legacy_s16 ghost_prepare_race(void);
void ghost_end_race(void);
legacy_s16 ghost_is_active(void);
void ghost_update(legacy_u32 frame, legacy_u16 live_frame_rate);
struct CARSTATE *ghost_car_state(void);
const struct GHOST_CAMERA_STATE *ghost_camera_state(void);
#ifdef RESTUNTS_SDL3
enum GHOST_RACING_LINE_COLOR {
	GHOST_RACING_LINE_RED = 0,
	GHOST_RACING_LINE_YELLOW = 1,
	GHOST_RACING_LINE_GREEN = 2
};

/* Immutable, two-metre ribbon pieces in CARSTATE position units (64 per
 * renderer unit). Wheel contacts provide banking and a small surface lift.
 * `speed` is MPH * 256; `next` is private to the tile-bucket iterator. */
struct GHOST_RACING_LINE_SEGMENT {
	struct VECTORLONG start_left;
	struct VECTORLONG start_right;
	struct VECTORLONG end_left;
	struct VECTORLONG end_right;
	legacy_u16 speed;
	legacy_u32 next;
};

const struct GHOST_RACING_LINE_SEGMENT *ghost_racing_line_first(legacy_s16 east, legacy_s16 south);
const struct GHOST_RACING_LINE_SEGMENT *
ghost_racing_line_next(const struct GHOST_RACING_LINE_SEGMENT *segment);
legacy_u8 ghost_racing_line_color(legacy_u16 current_speed, legacy_u16 recorded_speed);

/* Sample cached recorded poses at (live_frame - lag / 65536) / live_frame_rate.
 * This presentation-only read never advances physics or changes the live ghost.
 * Failure leaves both outputs untouched. Start/end times clamp to the replay. */
legacy_s16 ghost_sample_render_pose(legacy_u32 live_frame, legacy_u16 live_frame_rate,
									legacy_u32 lag, struct CARSTATE *car,
									struct GHOST_CAMERA_STATE *camera);
#endif
/* Convert recorded listener movement to one live audio tick, without file I/O. */
void ghost_adjust_camera_motion(struct VECTOR *previous, const struct VECTOR *current);
const struct SIMD *ghost_car_simd(void);
const legacy_s8 *ghost_car_id(void);
legacy_u8 ghost_car_material(void);

#endif
