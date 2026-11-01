#ifndef RESTUNTS_SUPERSIGHT_RACE_VIEW_H
#define RESTUNTS_SUPERSIGHT_RACE_VIEW_H
#include "../legacy.h"
struct RACE_SUPERSIGHT_VIEWPORT {
	legacy_u8 supersight_enabled;
};
void race_supersight_clip_dashboard(void);
legacy_s16 race_supersight_viewport_changed(const struct RACE_SUPERSIGHT_VIEWPORT *cache);
void race_supersight_cache_viewport(struct RACE_SUPERSIGHT_VIEWPORT *cache);
#endif
