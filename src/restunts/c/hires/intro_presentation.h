#ifndef RESTUNTS_HIRES_INTRO_PRESENTATION_H
#define RESTUNTS_HIRES_INTRO_PRESENTATION_H

#ifdef RESTUNTS_SDL3
#include "presentation.h"
#include "../intro_view.h"

struct INTRO_SESSION;
struct HIRES_INTRO_PRESENTATION {
	struct PRESENTATION_CLOCK presentation_clock;
	struct INTRO_VIEW previous_view;
	struct INTRO_VIEW current_view;
	legacy_u64 view_time;
	legacy_u64 view_interval;
	legacy_s16 view_frame;
	legacy_u8 view_valid;
	legacy_u64 input_time;
	legacy_s16 input_delta;
};

void hires_intro_reset(struct INTRO_SESSION *intro);
void hires_intro_begin(struct INTRO_SESSION *intro);
void hires_intro_present(struct INTRO_SESSION *intro);
legacy_s16 hires_intro_input(struct INTRO_SESSION *intro, legacy_s16 delta);
#endif

#endif
