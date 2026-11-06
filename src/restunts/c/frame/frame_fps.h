#ifndef RESTUNTS_FRAME_FPS_H
#define RESTUNTS_FRAME_FPS_H

#include "../math/math.h"

#define REPLAY_TEXT_LEFT_X 8
#define REPLAY_TEXT_RIGHT_X 312
#define REPLAY_TEXT_CHARACTER_WIDTH 8U
#define REPLAY_TEXT_Y 15
#define REPLAY_FILENAME_Y 3
#define REPLAY_TEXT_LINE_HEIGHT (REPLAY_TEXT_Y - REPLAY_FILENAME_Y)
#define REPLAY_TEXT_MAX_CHARACTERS                                                                 \
	((REPLAY_TEXT_RIGHT_X - REPLAY_TEXT_LEFT_X) / REPLAY_TEXT_CHARACTER_WIDTH)

#define FPS_TEXT_RED 4
#define FPS_TEXT_GREEN 2
#define FPS_TEXT_TARGET 20U
#define FPS_TEXT_NUMBER_BASE 10U
#define FPS_TEXT_MAX_DIGITS 5U
#define FPS_TEXT_SUFFIX_LENGTH 4U
#define FPS_TEXT_BUFFER_SIZE (FPS_TEXT_MAX_DIGITS + FPS_TEXT_SUFFIX_LENGTH + 1U)
#define FPS_TEXT_RIGHT_X                                                                           \
	(REPLAY_TEXT_LEFT_X + (FPS_TEXT_BUFFER_SIZE - 1U) * REPLAY_TEXT_CHARACTER_WIDTH + 1U)

#ifdef RESTUNTS_SDL3
#define RENDER_TIME_BUFFER_SIZE (FPS_TEXT_MAX_DIGITS + sizeof(".0ms"))
#define RENDER_TIME_SAMPLE_COUNT 100U
#define RENDER_TIME_NS_PER_TENTH_MS 100000ULL
#define RENDER_TIME_TENTHS_PER_MS 10U
#define RENDER_TIME_MAX_TENTHS ((legacy_u32)LEGACY_U16_MAX * RENDER_TIME_TENTHS_PER_MS)

/* Completed rendering only, excluding simulation, pacing, and display synchronization.
 * Shared by classic SDL and enhanced renderers; reset on renderer changes. */
void frame_render_timing_record(legacy_u64 elapsed_ns);
void frame_render_timing_reset(void);
#endif

/* Allocate last in race setup and release before other race resources.
 * Return nonzero if the backing cannot fit in the remaining resource arena. */
legacy_s16 frame_mph_initialize(void);
void frame_mph_shutdown(void);
/* Restore the previous page's HUD before drawing a new scene/dashboard. */
void frame_mph_restore_text(void);
/* Capture the composed background and draw shadowed text without a solid fill. */
struct RECTANGLE *frame_mph_draw_text(legacy_s16 screen_bottom);

void frame_fps_reset(void);
legacy_s16 frame_display_overlay_active(void);
legacy_s16 frame_fps_expire_idle(void);
void frame_fps_record_presented(void);
void frame_fps_present_roof(void);
struct RECTANGLE *frame_fps_draw_text(void);
/* Reserve diagnostics before dashboard composition, then paint them afterward. */
legacy_u16 frame_fps_prepare_text(void);
/* Leave room for enabled diagnostic rows when wrapping a replay filename. */
legacy_u16 frame_fps_filename_reserved(legacy_s16 y, legacy_u16 reserved);

#endif
