#ifndef RESTUNTS_HIRES_RACE_VIEW_H
#define RESTUNTS_HIRES_RACE_VIEW_H
#include "../legacy.h"
struct RACE_HIRES_VIEWPORT {
	legacy_u8 supersight;
};
void race_hires_clip_dashboard(void);
legacy_s16 race_hires_viewport_changed(const struct RACE_HIRES_VIEWPORT *cache);
void race_hires_cache_viewport(struct RACE_HIRES_VIEWPORT *cache);
#endif
