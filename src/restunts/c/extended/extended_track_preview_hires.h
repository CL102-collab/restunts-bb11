#ifndef RESTUNTS_HIRES_TRACK_PREVIEW_EXTENDED_H
#define RESTUNTS_HIRES_TRACK_PREVIEW_EXTENDED_H

#include "../legacy.h"
#ifdef RESTUNTS_SDL3
legacy_s16 extended_track_preview_hires_draw(void);
/* Number of scene primitives submitted by the last HyperVision preview. */
legacy_u32 extended_track_preview_hires_primitive_count(void);
#endif

#endif
