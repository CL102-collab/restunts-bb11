#ifndef RESTUNTS_FRAME_INTERNAL_H
#define RESTUNTS_FRAME_INTERNAL_H

#include "math.h"
#include "shape3d/shape3d.h"
#include "track/track_types.h"

extern struct RECTANGLE frame_rects_page0[];
extern struct RECTANGLE frame_rects_page1[];
extern struct RECTANGLE merged_redraw_rects[];
extern legacy_s8 frame_rect_change_flags[];
extern legacy_s16 redraw_rect_sort_indices[];
extern legacy_s8 redraw_rect_count;
extern struct RECTANGLE frame_layer_rects[];
/* These legacy labels were views into consecutive elements of frame_layer_rects. */
#define frame_unsorted_shapes_rect frame_layer_rects[1]
#define frame_sorted_shapes_rect frame_layer_rects[2]
#define frame_player_car_rect frame_layer_rects[3]
#define frame_opponent_car_rect frame_layer_rects[4]
#define rect_skybox frame_layer_rects[5]
#define frame_elapsed_time_rect frame_layer_rects[6]
#define frame_cloud_rect frame_layer_rects[7]
extern struct RECTANGLE intro_redraw_cliprect;
extern struct RECTANGLE rect_ingame_text2;
extern struct RECTANGLE rect_ingame_text3;
extern struct RECTANGLE rect_ingame_text4;
extern legacy_s8 detail_threshold_by_level[];
extern legacy_s8 track_material_animation[];
extern legacy_u16 frame_callback_count;
extern legacy_s16 cloud_heading_offsets[];
extern legacy_s8 *lookahead_tiles_tables[];
extern struct SHAPE3D *cloud_shapes[];
extern struct TRANSFORMEDSHAPE3D currenttransshape[29];
//extern struct TRANSFORMEDSHAPE3D transshapeunk;
extern struct TRANSFORMEDSHAPE3D *curtransshape_ptr;
extern legacy_u8 fence_TrkObjCodes[];

extern legacy_s8 fence_tile_offsets_single[];
extern legacy_s8 fence_tile_offsets_row[];
extern legacy_s8 fence_tile_offsets_both[];
extern legacy_s8 fence_tile_offsets_column[];
extern legacy_s16 fence_rotations[];
extern legacy_s16 hill_fill_offsets_single[];
extern legacy_s16 hill_fill_offsets_row[];
extern legacy_s16 hill_fill_offsets_column[];
extern legacy_s16 hill_fill_offsets_both[];
extern struct TRACKOBJECT terrain_scene_objects[];
extern struct TRACKOBJECT particle_scene_objects[];
extern legacy_s16 frame_buffer_camera_headings[];
extern legacy_s16 last_rendered_camera_heading;
extern legacy_s16 transformedshape_zarray[];
extern legacy_s16 transformedshape_indices[];
extern legacy_s8 transformed_shape_sort_types[];
extern legacy_s8 transformedshape_counter;
extern struct RECTANGLE rect_ingame_text;
extern struct RECTANGLE intro_cliprect;
extern legacy_s16 intro_colorvalue;
extern legacy_s16 intro_palette_color_count;
extern struct SHAPE3D logoshape;
extern struct SHAPE3D logo2shape;
extern struct SHAPE3D bravshape;
extern legacy_s16 intro_elapsed_ticks;
extern legacy_s16 track_preview_camera_x;
extern legacy_s16 track_preview_camera_y;
extern legacy_s16 track_preview_camera_z;
extern legacy_s16 track_preview_target_x;
extern legacy_s16 track_preview_target_y;
extern legacy_s16 track_preview_target_z;
extern struct VECTOR track_preview_horizon_vector;
extern struct RECTANGLE trackpreview_cliprect;

struct TRACKOBJECT *frame_track_object_from_legacy_index(legacy_u8 index);
void transformed_shape_add_for_sort(legacy_s16 z_adjust, legacy_s16 type);

struct GAMESTATE;
struct CARSTATE;
struct GHOST_CAMERA_STATE;

#include "hires/frame_snapshot.h"
#include "hires/frame_overlay.h"

/* Prepare diagnostic redraw bounds before dashboard composition; paint it with
 * frame_fps_draw_text after the dashboard is complete. */
struct RECTANGLE *draw_ingame_text(void);
struct RECTANGLE *init_crak(legacy_s16 frame, legacy_s16 top, legacy_s16 height);
struct RECTANGLE *do_sinking(legacy_s16 frame, legacy_s16 top, legacy_s16 height);

void intro_render_scene(legacy_s16 camera_x, legacy_s16 camera_y, legacy_s16 camera_z,
						legacy_s16 rotate_y, legacy_s16 rotate_x, legacy_s16 draw_car,
						legacy_s16 primary_logo, struct VECTOR *stars,
						struct POINT2D *previous_points, legacy_s16 *previous_point_count,
						struct RECTANGLE previous_rect, struct RECTANGLE *shape_rect,
						struct RECTANGLE *combined_rect);

#endif
