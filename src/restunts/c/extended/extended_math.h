#ifndef RESTUNTS_EXTENDED_MATH_H
#define RESTUNTS_EXTENDED_MATH_H

#include "../legacy.h"

#ifdef RESTUNTS_SDL3
struct PLANE;
/* Dynamic mesh-plane indices select the current precise collision plane. */
struct PLANE far *extended_math_selected_plane(legacy_s16 plane_index);
#endif

#endif
