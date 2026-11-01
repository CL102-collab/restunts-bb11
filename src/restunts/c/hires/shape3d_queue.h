#ifndef RESTUNTS_HIRES_SHAPE3D_QUEUE_H
#define RESTUNTS_HIRES_SHAPE3D_QUEUE_H
#include "../legacy.h"
#ifdef RESTUNTS_SDL3
struct HIRES_BACKEND_SCENE_PRIMITIVE {
	legacy_u16 material;
	legacy_u8 type;
};
extern struct HIRES_BACKEND_SCENE_PRIMITIVE *hires_backend_scene;
extern legacy_u32 hires_backend_scene_count;
void hires_backend_scene_reserve(void);
void shape3d_hires_adjust_sort(legacy_u16 *sort_by_depth);
void shape3d_hires_scene_reset(void);
#endif

#endif
