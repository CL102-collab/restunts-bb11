#ifndef RESTUNTS_RENDERER_TRACK_GEOMETRY_H
#define RESTUNTS_RENDERER_TRACK_GEOMETRY_H

/* Compile-time routing only; implementations belong to each renderer. */
#if defined(RESTUNTS_SDL3)
#include "../hires/track_geometry.h"
#define renderer_geometry_repaint_enabled hires_geometry_repaint_enabled
#else
#include "../supersight/track_geometry.h"
#define renderer_geometry_repaint_enabled supersight_geometry_repaint_enabled
#endif

#endif
