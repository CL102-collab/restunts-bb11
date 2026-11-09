#ifndef RESTUNTS_FRAME_MPH_H
#define RESTUNTS_FRAME_MPH_H

#include "../math/math.h"

/* Allocate last in race setup and release before other race resources.
 * Return nonzero if the backing cannot fit in the remaining resource arena. */
legacy_s16 frame_mph_initialize(void);
void frame_mph_shutdown(void);
/* Restore the previous page's HUD before drawing a new scene/dashboard. */
void frame_mph_restore_text(void);
/* Capture the composed background and draw shadowed text without a solid fill. */
struct RECTANGLE *frame_mph_draw_text(legacy_s16 screen_bottom);

#endif
