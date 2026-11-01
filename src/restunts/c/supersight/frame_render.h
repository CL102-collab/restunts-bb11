#ifndef RESTUNTS_SUPERSIGHT_FRAME_RENDER_H
#define RESTUNTS_SUPERSIGHT_FRAME_RENDER_H

#include "../frame/frame_render_internal.h"
#include "settings.h"

struct CARSTATE;
struct SIMD;
struct FRAME_CAR_RENDER;

#define FRAME_SUPERSIGHT_DISCARD_BATCH 20
#define FRAME_SUPERSIGHT_MINIMUM_TILES 4
#define FRAME_SUPERSIGHT_PROBE_INTERVAL 16
#define FRAME_SUPERSIGHT_VIEW_KEY_COUNT 20

struct FRAME_SUPERSIGHT_TILE {
	legacy_s8 width, depth, priority;
};

extern const struct FRAME_SUPERSIGHT_TILE supersight_tiles[FRAME_SUPERSIGHT_TILE_COUNT];

extern const legacy_s8 supersight_detail_thresholds[];

extern legacy_s16 supersight_attempt_hint;
extern legacy_u8 supersight_probe_frames;
extern legacy_u8 supersight_view_valid;
extern legacy_s16 supersight_previous_view[FRAME_SUPERSIGHT_VIEW_KEY_COUNT];

void frame_supersight_extend_lookahead(struct FRAME_TILE_SELECTION *tiles,
									   const struct FRAME_CAMERA *camera);

void frame_supersight_reset(void);

void frame_supersight_draw_selected(struct FRAME_TILE_SELECTION *tiles, struct FRAME_CAMERA *camera,
									struct FRAME_CAR_RENDER *cars, legacy_s8 buffer_index,
									legacy_s8 redraw_transform_flags, legacy_s8 animated_material);

legacy_s16 frame_supersight_first_attempt(const struct FRAME_TILE_SELECTION *tiles,
										  const struct FRAME_CAMERA *camera);

legacy_s16 frame_supersight_draw_water(const struct FRAME_TILE *tile,
									   const struct FRAME_CAMERA *camera,
									   legacy_s8 redraw_transform_flags);

legacy_s16 frame_supersight_enabled(void);

legacy_s16 frame_supersight_uses_screen_depth(void);

legacy_s16 frame_supersight_adaptive_enabled(void);

legacy_s16 frame_supersight_tile_detail(const struct FRAME_TILE_SELECTION *tiles,
										legacy_s16 tile_index, legacy_s8 *detail);
legacy_s16 frame_supersight_prepare_tile(struct FRAME_TILE *tile);

void frame_supersight_prepare_tiles(struct FRAME_TILE_SELECTION *tiles,
									const struct FRAME_CAMERA *camera);

legacy_s16 frame_supersight_draw_covered_water(const struct FRAME_TILE *tile,
											   const struct FRAME_CAMERA *camera,
											   legacy_s8 redraw_transform_flags);

void frame_supersight_begin(void);

void frame_supersight_prepare_ground(const struct FRAME_CAMERA *camera);

legacy_s16 frame_supersight_draw_scene(struct FRAME_TILE_SELECTION *tiles,
									   struct FRAME_CAMERA *camera, struct FRAME_CAR_RENDER *cars,
									   legacy_s8 buffer_index, legacy_s8 redraw_transform_flags,
									   legacy_s8 animated_material);

legacy_s16 frame_supersight_dashboard_top(const struct RECTANGLE *cliprect);

void frame_supersight_override_car_detail(const struct TRACKOBJECT *track_object,
										  legacy_s8 tile_detail, legacy_s16 *low_detail);
void frame_supersight_racing_line_flags(legacy_u8 *flags);

#endif
