#ifndef RESTUNTS_HIRES_FRAME_RENDER_H
#define RESTUNTS_HIRES_FRAME_RENDER_H

#include "../frame/frame_render_internal.h"
#include "settings.h"
#include "frame_adaptive.h"
#include "shape3d_hires.h"

struct CARSTATE;
struct SIMD;
struct FRAME_CAR_RENDER;

#define FRAME_NON_GRASS_HEIGHT_OFFSET 2
#define FRAME_GRASS_HEIGHT_HASH_SHIFT 8U
#define FRAME_GRASS_HEIGHT_VARIATION_MASK 1U

extern legacy_u8 frame_hires_adaptive_active;

legacy_s32 frame_hires_adaptive_world_tile(legacy_s32 position, legacy_s16 offset);

legacy_u8 frame_hires_adaptive_position_flags(const struct VECTOR *relative,
											  const struct VECTOR *camera);

legacy_u8 frame_hires_adaptive_car_flags(const struct CARSTATE *carstate, const struct SIMD *simd);

legacy_u8 frame_hires_adaptive_tile_flags(const struct FRAME_TILE *tile);

void frame_hires_offset_car_shape(const struct VECTOR *offset);
struct VECTOR frame_hires_car_render_offset(const struct CARSTATE *carstate,
											const struct SHAPE3D *wheel_shape);

void frame_hires_add_car_shadow(const struct CARSTATE *carstate, const struct SIMD *simd,
								const struct VECTOR *camera_position, const struct SHAPE3D *shape);

legacy_s16 frame_hires_draw_adaptive_particles(const struct FRAME_CAMERA *camera,
											   struct FRAME_CAR_RENDER *cars,
											   legacy_s8 redraw_transform_flags);

void frame_hires_extend_lookahead(struct FRAME_TILE_SELECTION *tiles,
								  const struct FRAME_CAMERA *camera);

void frame_hires_reset(void);

void frame_hires_draw_selected(struct FRAME_TILE_SELECTION *tiles, struct FRAME_CAMERA *camera,
							   struct FRAME_CAR_RENDER *cars, legacy_s8 buffer_index,
							   legacy_s8 redraw_transform_flags, legacy_s8 animated_material);

legacy_s16 frame_hires_draw_water(const struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
								  legacy_s8 redraw_transform_flags);

legacy_s16 frame_hires_enabled(void);

legacy_s16 frame_hires_uses_screen_depth(void);

legacy_s16 frame_hires_adaptive_enabled(void);

legacy_s16 frame_hires_cell_hidden(legacy_s16 east, legacy_s16 south);

legacy_s16 frame_hires_tile_detail(const struct FRAME_TILE_SELECTION *tiles, legacy_s16 tile_index,
								   legacy_s8 *detail);
legacy_s16 frame_hires_prepare_tile(struct FRAME_TILE *tile);

legacy_s16 frame_hires_prepare_car(const struct CARSTATE *carstate, legacy_s8 debris_owner,
								   const struct SHAPE3D *wheel_shape, legacy_s8 *tile_detail,
								   legacy_s8 *flags, struct VECTOR *render_offset);

void frame_hires_car_flags(legacy_s8 flags);

void frame_hires_prepare_tiles(struct FRAME_TILE_SELECTION *tiles,
							   const struct FRAME_CAMERA *camera);

legacy_s16 frame_hires_draw_covered_water(const struct FRAME_TILE *tile,
										  const struct FRAME_CAMERA *camera,
										  legacy_s8 redraw_transform_flags);

void frame_hires_begin(void);

void frame_hires_prepare_ground(const struct FRAME_CAMERA *camera);

legacy_s16 frame_hires_draw_scene(struct FRAME_TILE_SELECTION *tiles, struct FRAME_CAMERA *camera,
								  struct FRAME_CAR_RENDER *cars, legacy_s8 buffer_index,
								  legacy_s8 redraw_transform_flags, legacy_s8 animated_material);

legacy_s16 frame_hires_dashboard_top(const struct RECTANGLE *cliprect);

void frame_hires_override_car_detail(const struct TRACKOBJECT *track_object, legacy_s8 tile_detail,
									 legacy_s16 *low_detail);
void frame_hires_racing_line_flags(legacy_u8 *flags);
void frame_hires_override_track_shape(const struct TRACKOBJECT *object, struct SHAPE3D **shape);
void frame_hires_override_terrain_shape(const struct TRACKOBJECT *object, legacy_s8 detail,
										struct SHAPE3D **shape);
legacy_s16 frame_hires_dynamic_shape(const struct TRACKOBJECT *track_object,
									 const struct VECTOR *camera_position, struct SHAPE3D **shape);
legacy_s16 frame_hires_wheel_tile(const struct CARSTATE *carstate, const struct VECTOR *rotated,
								  legacy_s8 *tile_east, legacy_s8 *tile_south, legacy_s16 *visible);

#endif
