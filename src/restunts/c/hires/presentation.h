#ifndef RESTUNTS_PRESENTATION_H
#define RESTUNTS_PRESENTATION_H

#include "../legacy.h"

#define PRESENTATION_RATE 60U
#define PRESENTATION_SECOND_NS 1000000000ULL

/* This clock never polls devices or dispatches game timer callbacks. */
legacy_u64 presentation_now(void);

struct PRESENTATION_CLOCK {
	legacy_u64 origin;
	legacy_u64 next_frame;
};

static inline void presentation_reset(struct PRESENTATION_CLOCK *clock, legacy_u64 now)
{
	clock->origin = now;
	clock->next_frame = 0;
}

static inline legacy_s16 presentation_due(struct PRESENTATION_CLOCK *clock, legacy_u64 now)
{
	legacy_u64 elapsed = now - clock->origin;
	if (elapsed * PRESENTATION_RATE < clock->next_frame * PRESENTATION_SECOND_NS) {
		return 0;
	}
	/* Skip missed deadlines instead of drawing a burst after a slow frame. */
	clock->next_frame = elapsed * PRESENTATION_RATE / PRESENTATION_SECOND_NS + 1U;
	return 1;
}

#endif
