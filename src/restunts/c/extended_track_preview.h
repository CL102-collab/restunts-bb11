#ifndef RESTUNTS_EXTENDED_TRACK_PREVIEW_H
#define RESTUNTS_EXTENDED_TRACK_PREVIEW_H

#include "legacy.h"

#ifdef RESTUNTS_SDL3
/* Draw the active CSTRK preview. Return zero to use the classic track preview. */
legacy_s16 extended_track_preview_draw(void);
#endif

#endif
