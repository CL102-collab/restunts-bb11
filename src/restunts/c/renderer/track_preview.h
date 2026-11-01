#ifndef RESTUNTS_RENDERER_TRACK_PREVIEW_H
#define RESTUNTS_RENDERER_TRACK_PREVIEW_H

/* Compile-time routing only; each renderer owns its preview policy. */
#if defined(RESTUNTS_SDL3)
#include "../hires/track_preview.h"
#define renderer_track_preview_draw_covered_water hires_track_preview_draw_covered_water
#define renderer_track_preview_batch_scene hires_track_preview_batch_scene
#define renderer_track_preview_flush_scene hires_track_preview_flush_scene
#else
#include "../supersight/track_preview_scene.h"
#define renderer_track_preview_draw_covered_water supersight_track_preview_draw_covered_water
#define renderer_track_preview_batch_scene supersight_track_preview_batch_scene
#define renderer_track_preview_flush_scene supersight_track_preview_flush_scene
#endif

#endif
