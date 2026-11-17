#if defined(RESTUNTS_SDL3)
#include "race_view.h"
#include "settings.h"
#include "widescreen.h"
#include "../externs.h"
void race_hires_clip_dashboard(void)
{
	if (hires_active != 0 && dashbmp_y_copy > height_above_replaybar) {
		dashbmp_y_copy = height_above_replaybar;
	}
}
legacy_s16 race_hires_viewport_changed(const struct RACE_HIRES_VIEWPORT *cache)
{
	return cache->hires_active != hires_active || cache->widescreen_active != widescreen_active() ||
		   cache->widescreen_width != widescreen_render_width() ||
		   cache->widescreen_height != widescreen_render_height() ||
		   cache->widescreen_generation != widescreen_generation();
}
void race_hires_cache_viewport(struct RACE_HIRES_VIEWPORT *cache)
{
	cache->hires_active = hires_active;
	cache->widescreen_active = widescreen_active();
	cache->widescreen_width = widescreen_render_width();
	cache->widescreen_height = widescreen_render_height();
	cache->widescreen_generation = widescreen_generation();
}
#endif
