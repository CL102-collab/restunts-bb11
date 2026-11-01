#ifndef RESTUNTS_HIRES_RACE_PRESENTATION_H
#define RESTUNTS_HIRES_RACE_PRESENTATION_H
#include "../legacy.h"
#ifdef RESTUNTS_SDL3
void race_hires_presentation_sync(legacy_s16 rewinding);
void race_hires_presentation_capture(void);
void race_hires_draw_visual(void);
void race_hires_present_interpolation(void);
void race_hires_presentation_idle(legacy_s16 rewind_active, legacy_s64 last_processed_frame);
void race_hires_presentation_initialize(void);
void race_hires_presentation_begin_frame(void);
legacy_s16 race_hires_presentation_draw_snapshot(void);
legacy_s16 race_hires_presentation_poll(legacy_s64 *last_processed_frame,
										legacy_s16 *wait_for_sample, legacy_u16 *ready);
void race_hires_presentation_draw(legacy_s16 rewind_active);
void race_hires_presentation_finish(void);
#endif
#endif
