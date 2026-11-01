#ifndef RESTUNTS_RENDERER_FRAME_LIMITS_H
#define RESTUNTS_RENDERER_FRAME_LIMITS_H

#if defined(RESTUNTS_SDL3)
#include "../extended/extended_frame_layout.h"
#define FRAME_RENDERER_TILE_COUNT EXTENDED_FRAME_MAXIMUM_TILE_COUNT
#else
#include "../supersight/frame_limits.h"
#define FRAME_RENDERER_TILE_COUNT FRAME_SUPERSIGHT_TILE_COUNT
#endif

#endif
