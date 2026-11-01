#ifndef RESTUNTS_PRESENTATION_H
#define RESTUNTS_PRESENTATION_H

#include "../legacy.h"

#define HIRES_PRESENTATION_RATE 60U
#define HIRES_PRESENTATION_SECOND_NS 1000000000ULL

/* This clock never polls devices or dispatches game timer callbacks. */
legacy_u64 hires_presentation_now(void);

struct HIRES_PRESENTATION_CLOCK {
	legacy_u64 origin;
	legacy_u64 next_frame;
};

static inline void hires_presentation_reset(struct HIRES_PRESENTATION_CLOCK *clock, legacy_u64 now)
{
	clock->origin = now;
	clock->next_frame = 0;
}

static inline legacy_s16 hires_presentation_due(struct HIRES_PRESENTATION_CLOCK *clock,
												legacy_u64 now)
{
	legacy_u64 elapsed = now - clock->origin;
	if (elapsed * HIRES_PRESENTATION_RATE < clock->next_frame * HIRES_PRESENTATION_SECOND_NS) {
		return 0;
	}
	/* Skip missed deadlines instead of drawing a burst after a slow frame. */
	clock->next_frame = elapsed * HIRES_PRESENTATION_RATE / HIRES_PRESENTATION_SECOND_NS + 1U;
	return 1;
}

#endif
