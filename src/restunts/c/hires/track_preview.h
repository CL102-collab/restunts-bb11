#ifndef RESTUNTS_HIRES_TRACK_PREVIEW_H
#define RESTUNTS_HIRES_TRACK_PREVIEW_H

#include "../shape3d/shape3d.h"

typedef void (*HIRES_TRACK_PREVIEW_TERRAIN)(legacy_u8 terrain, legacy_u8 column, legacy_u8 row,
											legacy_s16 height, legacy_s16 camera_x,
											legacy_s16 camera_y, legacy_s16 camera_z,
											legacy_s16 use_high_detail,
											struct TRANSFORMEDSHAPE3D *transformed);
void hires_track_preview_draw_covered_water(legacy_u8 track, legacy_u8 terrain, legacy_u8 column,
											legacy_u8 row, const struct VECTOR *camera,
											struct TRANSFORMEDSHAPE3D *transformed,
											HIRES_TRACK_PREVIEW_TERRAIN draw_terrain);
void hires_track_preview_flush_tile(void);
void hires_track_preview_flush_scene(void);

#endif
