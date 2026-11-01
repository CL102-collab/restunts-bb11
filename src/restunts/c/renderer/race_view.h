#ifndef RESTUNTS_RENDERER_RACE_VIEW_H
#define RESTUNTS_RENDERER_RACE_VIEW_H

/* Compile-time routing only; implementations belong to each renderer. */
#if defined(RESTUNTS_SDL3)
#include "../hires/race_view.h"
#define RACE_RENDERER_VIEWPORT RACE_HIRES_VIEWPORT
#define race_renderer_clip_dashboard race_hires_clip_dashboard
#define race_renderer_viewport_changed race_hires_viewport_changed
#define race_renderer_cache_viewport race_hires_cache_viewport
#else
#include "../supersight/race_view.h"
#define RACE_RENDERER_VIEWPORT RACE_SUPERSIGHT_VIEWPORT
#define race_renderer_clip_dashboard race_supersight_clip_dashboard
#define race_renderer_viewport_changed race_supersight_viewport_changed
#define race_renderer_cache_viewport race_supersight_cache_viewport
#endif

#endif
