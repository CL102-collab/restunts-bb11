#ifndef RESTUNTS_HIRES_SHAPE3D_QUEUE_H
#define RESTUNTS_HIRES_SHAPE3D_QUEUE_H
#include "../legacy.h"
#define POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY POLYINFO_STORAGE_PRIMITIVE_CAPACITY
#define POLYINFO_SUPERSIGHT_DATA_SIZE POLYINFO_STORAGE_DATA_SIZE
#ifdef RESTUNTS_SDL3
struct HYPERVISION_SCENE_PRIMITIVE {
	legacy_u16 material;
	legacy_u8 type;
};
extern struct HYPERVISION_SCENE_PRIMITIVE *hypervision_scene;
extern legacy_u32 hypervision_scene_count;
void hypervision_scene_reserve(void);
void shape3d_hires_adjust_sort(legacy_u16 *sort_by_depth);
void shape3d_hires_scene_reset(void);
#endif

#endif
