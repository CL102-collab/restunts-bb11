#ifndef RESTUNTS_EXTENDED_CHECKPOINT_H
#define RESTUNTS_EXTENDED_CHECKPOINT_H

#include "cstrk.h"
#include "../math/math.h"

#define EXTENDED_CHECKPOINT_CAR_COUNT 2U
#define EXTENDED_CHECKPOINT_COLOR_COUNT 2U
#define EXTENDED_CHECKPOINT_VISITED_BYTES ((CSTRK_MAX_OBJECTS + 7U) / 8U)
#define EXTENDED_CHECKPOINT_POSITION_SCALE 64L

/* Runtime-only state. The replay clocks carry this through rewind, nested ghost
 * playback, and the generator's physics snapshots; it is never serialized. */
struct EXTENDED_CHECKPOINT_PROGRESS {
	legacy_u8 visited[EXTENDED_CHECKPOINT_CAR_COUNT][EXTENDED_CHECKPOINT_VISITED_BYTES];
	legacy_u16 remaining[EXTENDED_CHECKPOINT_CAR_COUNT][EXTENDED_CHECKPOINT_COLOR_COUNT];
	struct VECTORLONG previous[EXTENDED_CHECKPOINT_CAR_COUNT];
	legacy_u8 initialized;
};

/* Positions use CARSTATE's 1/64-world-unit coordinates. A sweep must cross the
 * gate plane inside its open aperture; either travel direction is accepted. */
legacy_s16 extended_checkpoint_crossed(const struct CSTRK_TRACK *track,
									   const struct CSTRK_OBJECT *object,
									   const struct VECTORLONG *previous,
									   const struct VECTORLONG *current);
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
void extended_checkpoint_begin_step(void);
void extended_checkpoint_end_step(void);
legacy_s16 extended_checkpoint_passed(const struct CSTRK_OBJECT *object, legacy_s16 car_index);
/* Legacy tracks and extended tracks without colored gates retain native rules.
 * Colored tracks additionally require a finite, forward finish-gate crossing
 * after completing every checkpoint of at least one color present on the map. */
legacy_s16 extended_checkpoint_finish_allowed(legacy_s16 car_index);
#endif

#endif
