#ifndef RESTUNTS_SUPERSIGHT_TRACK_PREVIEW_SCENE_H
#define RESTUNTS_SUPERSIGHT_TRACK_PREVIEW_SCENE_H

#include "../shape3d/shape3d.h"

typedef void (*SUPERSIGHT_TRACK_PREVIEW_TERRAIN)(legacy_u8 terrain, legacy_u8 column, legacy_u8 row,
												 legacy_s16 height, legacy_s16 camera_x,
												 legacy_s16 camera_y, legacy_s16 camera_z,
												 legacy_s16 use_high_detail,
												 struct TRANSFORMEDSHAPE3D *transformed);
void supersight_track_preview_draw_covered_water(legacy_u8 track, legacy_u8 terrain,
												 legacy_u8 column, legacy_u8 row,
												 const struct VECTOR *camera,
												 struct TRANSFORMEDSHAPE3D *transformed,
												 SUPERSIGHT_TRACK_PREVIEW_TERRAIN draw_terrain);
legacy_s16 supersight_track_preview_batch_scene(void);
void supersight_track_preview_flush_scene(legacy_s16 batch_scene);

#endif
