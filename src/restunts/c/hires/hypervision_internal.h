#ifndef RESTUNTS_HYPERVISION_INTERNAL_H
#define RESTUNTS_HYPERVISION_INTERNAL_H

#include "hypervision.h"

/* Private records for scanline crossings and the ordered visibility partition. */
struct HIRES_VISIBILITY_SPAN {
	legacy_s32 left, right;
	legacy_f64 origin, step;
	legacy_u32 command;
};
struct HIRES_VISIBILITY_CROSSING {
	legacy_f64 x, z;
};

#endif
