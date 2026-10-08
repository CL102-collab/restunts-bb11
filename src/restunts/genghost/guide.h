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
	unsigned char input;
};

void genghost_guide_capture(struct GENGHOST_GUIDE_POINT *point, unsigned char input);
/* Re-time recorded controls by pose after a sector edit, and optionally correct
 * steering toward the demonstrated line. The engine state is only read: every
 * accepted lap still consists solely of ordinary input bytes and is revalidated. */
unsigned char genghost_guide_step(const struct GENGHOST_GUIDE_POINT *points, unsigned int count,
								  unsigned int *cursor, double correction);

#endif
