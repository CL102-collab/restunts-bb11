#ifndef RESTUNTS_HIRES_FRAME_OVERLAY_H
#define RESTUNTS_HIRES_FRAME_OVERLAY_H

#include "../math.h"

struct GAMESTATE;
struct CARSTATE;
struct GHOST_CAMERA_STATE;

void frame_supersight_reset(void);
void frame_fps_reset(void);
#ifdef RESTUNTS_SDL3
/* Completed rendering only, excluding simulation, pacing, and display synchronization.
 * Reset when changing renderer so the rolling history describes the selected backend. */
void frame_render_timing_record(legacy_u64 elapsed_ns);
void frame_render_timing_reset(void);
#endif
void frame_supersight_show_status(const legacy_s8 *name);
/* Static menus retain mode-change notices without drawing or sampling F11 diagnostics. */
legacy_s16 frame_status_overlay_active(void);
legacy_s16 frame_status_expire_idle(void);
struct RECTANGLE *frame_status_draw_text(void);
legacy_s16 frame_display_overlay_active(void);
legacy_s16 frame_fps_expire_idle(void);
void frame_fps_record_presented(void);
void frame_fps_present_roof(void);
struct RECTANGLE *frame_fps_draw_text(void);

#endif
