#ifndef RESTUNTS_HIRES_SHAPE3D_QUEUE_H
#define RESTUNTS_HIRES_SHAPE3D_QUEUE_H
#include "../legacy.h"
#define POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY 592U
#define POLYINFO_SUPERSIGHT_DATA_SIZE 13312U
#define POLYINFO_SUPERSIGHT_LAST_VALID_OFFSET                                                      \
	(POLYINFO_SUPERSIGHT_DATA_SIZE - POLYINFO_MAX_RECORD_SIZE)
#ifdef RESTUNTS_SDL3
typedef legacy_u32 polyinfo_index;
typedef legacy_s32 polyinfo_link;
typedef legacy_u32 polyinfo_offset;
extern polyinfo_link *polygon_next_index;
extern polyinfo_offset *polygon_record_offsets;
struct HYPERVISION_SCENE_PRIMITIVE {
	legacy_u16 material;
	legacy_u8 type;
};
extern struct HYPERVISION_SCENE_PRIMITIVE *hypervision_scene;
extern legacy_u32 hypervision_scene_count;
void hypervision_scene_reserve(void);
legacy_u16 shape3d_hires_sort_policy(legacy_u16 sort_by_depth);
legacy_u8 shape3d_hires_queue_is_unlimited(void);
void shape3d_hires_scene_reset(void);
#endif
legacy_u16 shape3d_hires_polyinfo_size(void);
void shape3d_hires_set_queue_mode(legacy_u8 enabled);
#endif
