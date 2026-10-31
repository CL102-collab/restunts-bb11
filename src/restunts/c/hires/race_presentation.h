#ifndef RESTUNTS_HIRES_RACE_PRESENTATION_H
#define RESTUNTS_HIRES_RACE_PRESENTATION_H
#include "../legacy.h"
#ifdef RESTUNTS_SDL3
void race_presentation_sync(legacy_s16 rewinding);
void race_presentation_capture(void);
void race_draw_visual(void);
void race_present_interpolation(void);
void race_presentation_initialize(void);
void race_presentation_begin_frame(void);
legacy_s16 race_presentation_draw_snapshot(void);
legacy_s16 race_presentation_poll(legacy_s64 *last_processed_frame, legacy_s16 *wait_for_sample,
								  legacy_u16 *ready);
legacy_s16 race_presentation_draw(legacy_s16 rewind_active);
void race_presentation_frame_complete(void);
void race_presentation_finish(void);
#endif
#endif
