#if !defined(RESTUNTS_SDL3)
#include "race_view.h"
#include "settings.h"
#include "../externs.h"
void race_supersight_clip_dashboard(void)
{
	if (supersight_enabled != 0 && dashbmp_y_copy > height_above_replaybar) {
		dashbmp_y_copy = height_above_replaybar;
	}
}
legacy_s16 race_supersight_viewport_changed(const struct RACE_SUPERSIGHT_VIEWPORT *cache)
{
	return cache->supersight_enabled != supersight_enabled;
}
void race_supersight_cache_viewport(struct RACE_SUPERSIGHT_VIEWPORT *cache)
{
	cache->supersight_enabled = supersight_enabled;
}
#endif
