#ifndef RESTUNTS_GENGHOST_GUIDE_H
#define RESTUNTS_GENGHOST_GUIDE_H

#include "../c/gamestate.h"

/* A demonstration is measured with the selected car and physics, not inferred
 * from replay length or track geometry. Points describe the state BEFORE each
 * input. This also works with a completed lap discovered by the autonomous driver. */
struct GENGHOST_GUIDE_POINT {
	struct VECTORLONG position;
	struct VECTOR rotation;
	legacy_s16 steering;
	legacy_u16 speed;
	legacy_s8 gear, contact;
	legacy_u8 input;
};

void genghost_guide_capture(struct GENGHOST_GUIDE_POINT *point, legacy_u8 input);
/* Re-time recorded controls by pose after a sector edit, and optionally correct
 * steering toward the demonstrated line. The engine state is only read: every
 * accepted lap still consists solely of ordinary input bytes and is revalidated. */
legacy_u8 genghost_guide_step(const struct GENGHOST_GUIDE_POINT *points, legacy_uint count,
							  legacy_uint *cursor, legacy_f64 correction);

#endif
