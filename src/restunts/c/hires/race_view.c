#include "race_view.h"
#include "settings.h"
#include "../externs.h"
void race_hires_clip_dashboard(void)
{
	if (supersight_enabled != 0 && dashbmp_y_copy > height_above_replaybar) {
		dashbmp_y_copy = height_above_replaybar;
	}
}
legacy_s16 race_hires_viewport_changed(const struct RACE_HIRES_VIEWPORT *cache)
{
	return cache->supersight != supersight_enabled;
}
void race_hires_cache_viewport(struct RACE_HIRES_VIEWPORT *cache)
{
	cache->supersight = supersight_enabled;
}
