#ifndef RESTUNTS_RACE_INTERNAL_H
#define RESTUNTS_RACE_INTERNAL_H

#include "../legacy.h"
#include "../gamestate.h"
#include "../ghost.h"
#ifdef RESTUNTS_SDL3
#include "../presentation.h"
#endif

#define RACE_SCREEN_WIDTH 320
#define RACE_SCREEN_HEIGHT 200
#define RACE_REPLAY_BAR_TOP 151
#define RACE_START_CAMERA_HEIGHT_OFFSET 1408L
#define RACE_REPLAY_RESTORE_WAIT_TICKS 500
#define RACE_PROJECTION_HORIZONTAL_SCALE 35
#define RACE_PROJECTION_VERTICAL_DIVISOR 6
#define RACE_OPPONENT_PROGRESS_DIALOG_Y 80
#define RACE_FRAME_LIMIT_MULTIPLIER 1500
#define RACE_FINAL_WAIT_TICKS 100
#define MOUSE_BUTTON_MASK 3
#define RACE_REPLAY_MODE_UNINITIALIZED (-1)
#define RACE_START_POSITION_DISTANCE (-240)
#define RACE_START_POSITION_SCALE_SHIFT 6U
#define RACE_RANDOM_VALUE_SHIFT 3U
#define RACE_REWIND_SCAN_CODE 0x10
#define RACE_CONTROL_SCAN_CODE 0x1D
/* Match the replay scrub units; the 100 Hz timer doubles speed after ten seconds. */
#define RACE_REWIND_UNITS_PER_FRAME 20UL
#define RACE_REWIND_BASE_SPEED 3UL
#define RACE_REWIND_DOUBLE_SPEED 6UL
#define RACE_REWIND_DOUBLE_AFTER_TICKS 1000UL

struct RACE_VIEWPORT_CACHE {
	legacy_s16 roof_height;
	legacy_s16 dashboard_bottom;
	legacy_u8 supersight;
};

struct RACE_REWIND_STATE {
	legacy_s16 active;
	legacy_u32 origin_frame;
	legacy_u32 held_ticks;
	legacy_u64 accumulated;
};

legacy_u32 race_current_frame(void);
legacy_u32 race_input_frame(void);
legacy_u32 race_recorded_frames(void);
void race_set_input_frame(legacy_u32 frame);
void race_rewind_resume(struct RACE_REWIND_STATE *rewind);
void race_update_rewind(struct RACE_REWIND_STATE *rewind);
legacy_u32 race_ghost_frame(void);
void race_draw_frame(void);
void race_update_dashboard_layout(legacy_s16 rewind_active);
void race_update_viewport(struct RACE_VIEWPORT_CACHE *cache, legacy_s16 rewind_active);
#ifdef RESTUNTS_SDL3
#define RACE_PRESENTATION_VIEW_COUNT 13U
#define RACE_PRESENTATION_SLOW_INTERVAL_SCALE 2U
#define RACE_PRESENTATION_FAST_FRAME_SPAN 2
#define RACE_CONTROL_POLL_DELAY_MS 1U

struct RACE_PRESENTATION {
	struct PRESENTATION_CLOCK clock;
	struct GAMESTATE previous;
	struct GAMESTATE current;
	struct GAMESTATE interpolated;
	struct CARSTATE ghost_interpolated;
	struct GHOST_CAMERA_STATE ghost_camera_interpolated;
	legacy_u64 sample_time;
	legacy_u64 control_time;
	legacy_u16 sample_span;
	legacy_s16 sample_frame;
	legacy_s16 view[RACE_PRESENTATION_VIEW_COUNT];
	legacy_u8 history_valid;
	legacy_u8 ghost_valid;
	legacy_u8 started;
	legacy_u8 interpolating;
};

extern struct RACE_PRESENTATION race_presentation;
void race_presentation_sync(legacy_s16 rewinding);
void race_presentation_capture(void);
void race_draw_visual(void);
void race_present_interpolation(void);
#endif

#endif
