#ifndef RESTUNTS_SHAPE3D_PIPELINE_INTERNAL_H
#define RESTUNTS_SHAPE3D_PIPELINE_INTERNAL_H

#include <stddef.h>
#include "shape3d_internal.h"
#include "../hires/shape3d_pipeline.h"
#define WHEEL_INNER_RADIUS_SCALE 9472U
#define SHAPE3D_VERTEX_CAPACITY 255U
#define SHAPE3D_VERTEX_FLAG_CAPACITY 256U
#define SHAPE3D_VERTEX_UNTRANSFORMED LEGACY_U8_MAX
#define SHAPE3D_FORWARD_VECTOR_SCALE 4096
#define SHAPE3D_NEAR_CLIP_Z 12
#define SHAPE3D_USE_BOUNDING_RECT_FLAG 8U
#define SHAPE3D_PRETRANSFORMED_FLAG 2U
#define SHAPE3D_NO_DEPTH_SORT_FLAG 1U
#define SHAPE3D_PRIMITIVE_ALWAYS_VISIBLE_FLAG 1U
#define POLYINFO_LIST_SENTINEL (-1)
#define POLYINFO_MATERIAL_OFFSET 2U
#define POLYINFO_VERTEX_COUNT_OFFSET 3U
#define POLYINFO_TYPE_OFFSET 4U
#define POLYINFO_HEADER_WORD_COUNT 3U
#define POLYINFO_POINT_WORD_COUNT 2U
#define POLYINFO_START_X_WORD POLYINFO_HEADER_WORD_COUNT
#define POLYINFO_START_Y_WORD (POLYINFO_START_X_WORD + 1U)
#define POLYINFO_END_X_WORD (POLYINFO_START_X_WORD + POLYINFO_POINT_WORD_COUNT)
#define POLYINFO_END_Y_WORD (POLYINFO_END_X_WORD + 1U)
#define POLYINFO_SPHERE_SIZE_WORD (POLYINFO_HEADER_WORD_COUNT + POLYINFO_POINT_WORD_COUNT)
#define POLYINFO_LEGACY_LAST_VALID_OFFSET 10354U
#define POLYINFO_MAX_RENDER_POINTS 15U
#define POLYINFO_MAX_RECORD_SIZE (6U + 4U * POLYINFO_MAX_RENDER_POINTS)
#define PROJECTION_EXTENT_SCALE 2048L
#define PROJECTION_EXTENT_DIVISOR 360L
#define PROJECTION_VIEW_VECTOR_LENGTH 10000
#define PROJECTION_YAW_MASK 1023U
#define PROJECTION_SINCOS_ANGLE 128

#if !((defined(RESTUNTS_TEMPLEOS)))
#define shape3d_project_point vector_to_point
#define shape3d_geometry_clip_flags rect_compare_point
#define shape3d_adjust_geometry_bounds rect_adjust_from_point
#endif

#define SHAPE3D_LEGACY_SOLID_POLYGON_RETURN_IP 4895U
#define SHAPE3D_LEGACY_PATTERNED_POLYGON_RETURN_IP 4936U
#define SHAPE3D_LEGACY_TWO_COLOR_POLYGON_RETURN_IP 5089U
#define SHAPE3D_LEGACY_LINE_RETURN_IP 5119U
#define SHAPE3D_LEGACY_WHEEL_RETURN_IP 5213U
#define SHAPE3D_LEGACY_SPHERE_RETURN_IP 5239U
#define SHAPE3D_LEGACY_POINT_RETURN_IP 5261U
#define SHAPE3D_LEGACY_POLYGON_POINTS_STACK_OFFSET 50U
#define SHAPE3D_LEGACY_POLYGON_HEADER_SIZE 6U
#define SHAPE3D_LEGACY_POLYGON_POINT_SIZE 4U
#define SHAPE3D_LEGACY_ROTATE_Z_RETURN_IP 5306U
#define SHAPE3D_LEGACY_ROTATE_X_RETURN_IP 5331U
#define SHAPE3D_LEGACY_ROTATE_Y_RETURN_IP 5395U
#define SHAPE3D_LEGACY_ROTATE_PAIR_RETURN_IP 5482U
#define SHAPE3D_LEGACY_ROTATE_TRIPLE_RETURN_IP 5626U
#define SHAPE3D_LEGACY_VIEW_ROTATION_STACK_OFFSET 42U
#define SHAPE3D_LEGACY_ROTATE_Y_FLAG 1U
#define SHAPE3D_LEGACY_ROTATE_X_FLAG 2U
#define SHAPE3D_LEGACY_ROTATE_Z_FLAG 4U
#define SHAPE3D_LEGACY_ROTATE_ALL_FLAGS                                                            \
	(SHAPE3D_LEGACY_ROTATE_Z_FLAG | SHAPE3D_LEGACY_ROTATE_X_FLAG | SHAPE3D_LEGACY_ROTATE_Y_FLAG)
#define SHAPE3D_LEGACY_CRACK_LINE_STACK_OFFSET 80U
#define SHAPE3D_LEGACY_LINE_CODE_PARAGRAPH_DELTA 2508U

struct SHAPE3D_TRANSFORM_CONTEXT {
	legacy_u8 vertex_clip_flags[SHAPE3D_VERTEX_FLAG_CAPACITY];
	struct MATRIX object_to_view_rotation;
	struct VECTOR view_translation;
	struct VECTOR view_vertices[SHAPE3D_VERTEX_CAPACITY];
	struct POINT2D projected_vertices[SHAPE3D_VERTEX_CAPACITY];
#if defined(RESTUNTS_SDL3)
	struct SHAPE3D_HIRES_TRANSFORM_CONTEXT hires;
#endif
	legacy_s32 visibility_mask;
	legacy_s32 front_facing_mask;
};

enum SHAPE3D_PATTERN_TYPE {
	SHAPE3D_PATTERN_SOLID = 0,
	SHAPE3D_PATTERN_MASKED = 1,
	SHAPE3D_PATTERN_TWO_COLOR = 2
};

extern legacy_s8 is_facing_camera(struct POINT2D far *);

extern legacy_u16 polygon_insert_newest(legacy_u16, legacy_u16);

extern legacy_u16 projection_scale_x_wrapped(legacy_u16, legacy_s16);

extern legacy_u16 polygon_buffer_full;

extern legacy_u16 transshapenumverts;

extern legacy_u8 far *transshapeprimitives;

extern legacy_u16 transshapenumpaints;

extern legacy_u8 transshapematerial;

extern legacy_u8 transshapeflags;

extern struct RECTANGLE *transshaperectptr;

extern legacy_s32 invpow2tbl[32];

extern legacy_u8 shape_view_direction_sector;

extern polyinfo_index shape_polygon_predecessor;

extern polyinfo_index polygon_list_tail;

extern polyinfo_index shape_polygon_count;

extern polyinfo_index polygon_insertion_cursor;

extern legacy_u8 transshapenumvertscopy;

extern struct POINT2D *polyvertpointptrtab[];

extern legacy_u16 shape_half_scale;

extern legacy_u8 primidxcounttab[];

extern legacy_u8 primtypetab[];

extern legacy_u8 far *transshapeprimptr;

extern legacy_u8 far *transshapepolyinfo;

extern legacy_s8 transprimitivepaintjob;

extern legacy_u8 far *transshapeprimindexptr;

#if (defined(RESTUNTS_TEMPLEOS))
extern void TempleRasterNative(legacy_u8 enabled);
#endif

extern legacy_u16 projection_horizontal_half_fov;

extern legacy_u16 projection_vertical_half_fov;

extern legacy_u16 projection_half_width;

extern legacy_u16 projection_origin_x;

extern legacy_u16 projection_half_height;

extern legacy_u16 projection_origin_y;

extern struct MATRIX mat_y0, mat_y100, mat_y200, mat_y300;

extern legacy_s32 direction_sector_sine_copy;

extern legacy_s32 direction_sector_cosine_copy;

extern polyinfo_index queued_ghost_primitives;
extern polyinfo_index polyinfo_primitive_capacity;

#if (defined(RESTUNTS_SDL3))
/* Reserve shared primitive and record storage for either rasterizer. */
void polyinfo_reserve(void);
#endif
legacy_u16 polyinfo_read_word(const legacy_u8 far *record, legacy_u16 word_index);
void polyinfo_write_word(legacy_u8 far *record, legacy_u16 word_index, legacy_u16 value);
void polyinfo_read_point(const legacy_u8 far *record, legacy_u16 point_index,
						 struct POINT2D *point);
void polyinfo_read_points(const legacy_u8 far *record, struct POINT2D *points,
						  legacy_u16 point_count);
void polyinfo_write_point(legacy_u8 far *record, legacy_u16 point_index,
						  const struct POINT2D *point);
void shape3d_cache_vertex(const struct SHAPE3D *shape, struct SHAPE3D_TRANSFORM_CONTEXT *context,
						  legacy_u16 index);
legacy_u8 shape3d_vertex_rect_flags(struct SHAPE3D_TRANSFORM_CONTEXT *context, legacy_u16 index);
legacy_u16 shape3d_prepare_primitive_vertices(const struct SHAPE3D *shape,
											  struct SHAPE3D_TRANSFORM_CONTEXT *context,
											  legacy_u16 *any_vertex_behind);
legacy_u16 shape3d_prepare_primitive(struct SHAPE3D_TRANSFORM_CONTEXT *context,
									 legacy_u8 primitive_type, legacy_u16 any_vertex_behind,
									 legacy_u16 primitive_flags,
									 const legacy_u8 far *front_facing_masks,
									 legacy_s32 *depth_sum);
legacy_u16 shape3d_insert_primitive(legacy_u8 primitive_type, legacy_u16 primitive_flags,
									legacy_s32 depth_sum);
void shape3d_prepare_instance(struct TRANSFORMEDSHAPE3D *instance,
							  struct SHAPE3D_TRANSFORM_CONTEXT *context);
void shape3d_prepare_visibility(struct TRANSFORMEDSHAPE3D *instance,
								struct SHAPE3D_TRANSFORM_CONTEXT *context);
extern polyinfo_offset polyinfo_last_valid_offset;
legacy_u16 shape3d_material_value(const legacy_s16 *table, legacy_u16 index);

#endif
