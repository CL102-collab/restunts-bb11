#ifndef RESTUNTS_SHAPE3D_H
#define RESTUNTS_SHAPE3D_H

#include "math.h"

#define SHAPE3D_HEADER_SIZE 4U
#define SHAPE3D_VERTEX_COUNT_OFFSET 0U
#define SHAPE3D_PRIMITIVE_COUNT_OFFSET 1U
#define SHAPE3D_PAINT_COUNT_OFFSET 2U
#define SHAPE3D_VERTEX_SIZE 6U
#define SHAPE3D_VERTEX_X_OFFSET 0U
#define SHAPE3D_VERTEX_Y_OFFSET 2U
#define SHAPE3D_VERTEX_Z_OFFSET 4U

#define SHAPE3D_PRIMITIVE_HEADER_SIZE 2U
#define SHAPE3D_VISIBILITY_MASK_SIZE 4U
#define SHAPE3D_POLYGON_MAX_VERTICES 10U
#define SHAPE3D_WHEEL_RIM_VERTEX_COUNT 3U
#define SHAPE3D_WHEEL_FIRST_AXIS 1U
#define SHAPE3D_WHEEL_SECOND_AXIS 2U

/* Resource primitive IDs differ from the queued RENDER_PRIMITIVE_* IDs. */
enum SHAPE3D_PRIMITIVE_TYPE {
	SHAPE3D_PRIMITIVE_EMPTY = 0,
	SHAPE3D_PRIMITIVE_POINT = 1,
	SHAPE3D_PRIMITIVE_LINE = 2,
	SHAPE3D_PRIMITIVE_POLYGON_FIRST = 3,
	SHAPE3D_PRIMITIVE_POLYGON_LAST = 10,
	SHAPE3D_PRIMITIVE_SPHERE = 11,
	SHAPE3D_PRIMITIVE_WHEEL = 12,
	SHAPE3D_PRIMITIVE_TYPE_COUNT = 16
};

/* Per-instance visual override; resource paint and geometry remain unchanged. */
#define SHAPE3D_GHOST_FLAG 16U
/* Camera-relative scenery is painted behind all world geometry in SuperSight. */
#define SHAPE3D_BACKGROUND_FLAG 32U
/* Cars and their wheels never receive the simple ground shadow. */
#define SHAPE3D_NO_SHADOW_RECEIVE_FLAG 64U
/* Generated grille instances retain the stock mask while painting it black. */
#define SHAPE3D_BLACK_GRILLE_FLAG 128U
#define SHAPE3D_STOCK_GRILLE_MATERIAL 22U

enum BACKLIGHT_PAINT {
	BACKLIGHT_PAINT_DEFAULT = 45,
	BACKLIGHT_PAINT_NORMAL = 46,
	BACKLIGHT_PAINT_BRAKING = 47
};

enum SHAPE3D_CAR_SHAPE_INDEX {
	PLAYER_CAR_LOW_SHAPE = 124,
	OPPONENT_CAR_LOW_SHAPE = 125,
	PLAYER_CAR_WHEEL_SHAPE = 126,
	OPPONENT_CAR_WHEEL_SHAPE = 127,
	PLAYER_CAR_HIGH_SHAPE = 128,
	OPPONENT_CAR_HIGH_SHAPE = 129
};

#pragma pack(push, 1)

struct SHAPE3D {
	legacy_u16 shape3d_numverts;
	legacy_u8 far *shape3d_vertex_bytes;
	legacy_u16 shape3d_numprimitives;
	legacy_u16 shape3d_numpaints;
	legacy_u8 far *shape3d_primitives;
	legacy_u8 far *shape3d_visibility_masks;
	legacy_u8 far *shape3d_front_facing_masks;
};

struct TRANSFORMEDSHAPE3D {
	struct VECTOR pos;
	struct SHAPE3D *shapeptr;
	struct RECTANGLE *rectptr;
	struct VECTOR rotvec;
	legacy_u16 culling_distance;
	legacy_u8 ts_flags;
	legacy_u8 material;
};

#pragma pack(pop)

/* These records mix 16-bit near and far pointers, so their original layout is
 * a DOS-compiler property rather than a portable resource representation. */
#if defined(RESTUNTS_DOS16)
typedef char legacy_shape3d_must_be_22_bytes[(sizeof(struct SHAPE3D) == 22) ? 1 : -1];
typedef char
	legacy_transformedshape3d_must_be_20_bytes[(sizeof(struct TRANSFORMEDSHAPE3D) == 20) ? 1 : -1];
#endif

legacy_s16 shape3d_load_all(void);
void shape3d_free_all(void);
void shape3d_vertex_read(const struct SHAPE3D *shape, legacy_u16 index, struct VECTOR *destination);
void shape3d_vertex_write(struct SHAPE3D *shape, legacy_u16 index, const struct VECTOR *source);
void shape3d_load_car_shapes(legacy_s8 *carid, legacy_s8 *opponent_carid);
void shape3d_free_car_shapes(void);
#if defined(RESTUNTS_SDL3)
/* Visible local underside at the current projection, clamped to signed renderer units. */
legacy_s16 shape3d_car_ground_height(const struct SHAPE3D *shape);
/* Cached local downward correction for a loaded car model; physics stays unchanged. */
legacy_s16 shape3d_car_ground_offset(const struct SHAPE3D *shape);
#endif
void shape3d_init_shape(legacy_s8 far *shapeptr, struct SHAPE3D *gameshape);
legacy_u16 shape3d_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance);
void set_projection(legacy_s16 horizontal_fov_degrees, legacy_s16 vertical_fov_degrees,
					legacy_s16 width, legacy_s16 height);
legacy_u16 select_cliprect_rotate(legacy_s16 angZ, legacy_s16 angX, legacy_s16 angY,
								  struct RECTANGLE *cliprect, legacy_s16 half_scale);
void init_polyinfo(void);
void polyinfo_reset(void);
/* Changing capacity discards queued primitives; select the mode before drawing a frame. */
void polyinfo_set_supersight(legacy_u8 enabled);
void shape3d_render_queued_primitives(void);
#if defined(RESTUNTS_SDL3)
/* Submitted scene primitives before rasterization, for either active backend. */
legacy_u32 shape3d_queued_primitive_count(void);
#endif
/* Logical original addresses are independent of the host's allocation layout. */
struct SHAPE3D_LEGACY_OPPONENT_RENDER_CONTEXT {
	legacy_s16 *wheel_headings;
	legacy_u16 polyinfo_offset;
	legacy_u16 polyinfo_segment;
	legacy_u16 material_color_offset;
};

/* Optional compatibility for original renderer/physics stack reuse.
 * Null word/context pointers disable the corresponding car's compatibility.
 * The opponent context is copied; its word buffer retains values between renders. */
void shape3d_set_legacy_render_stack(legacy_s16 *wheel_headings, legacy_u16 polygon_frame_pointer,
									 legacy_u16 polygon_code_segment,
									 const struct SHAPE3D_LEGACY_OPPONENT_RENDER_CONTEXT *opponent);

/* Retain only skybox locals that the original path actually assigns. */
void shape3d_retain_legacy_skybox_horizon(legacy_s16 horizon);
void shape3d_retain_legacy_skybox_rect(const struct RECTANGLE *rect);
void shape3d_retain_legacy_skybox_points(const struct POINT2D *points);
void shape3d_retain_legacy_crack_line(const legacy_u16 *line, legacy_u16 end_y,
									  legacy_s16 dirty_rects, legacy_s16 drawn);
void shape3d_retain_legacy_crack_bounds(legacy_s16 line_index, legacy_u16 crack_offset);

void preRender_default(legacy_u16 color, legacy_u16 vertex_count, const struct POINT2D *vertices);
void preRender_default_alt(legacy_u16 color, legacy_u16 vertex_count,
						   const struct POINT2D *vertices);
void preRender_patterned(legacy_u16 pattern, legacy_u16 color, legacy_u16 vertex_count,
						 const struct POINT2D *vertices);
void preRender_two_color(legacy_u16 pattern, legacy_u16 color, legacy_u16 alternate_color,
						 legacy_u16 vertex_count, const struct POINT2D *vertices);
void preRender_line(legacy_u16 start_x, legacy_u16 start_y, legacy_u16 end_x, legacy_u16 end_y,
					legacy_u16 color);
void preRender_crack_line(legacy_u16 start_x, legacy_u16 start_y, legacy_u16 end_x,
						  legacy_u16 end_y, legacy_u16 color);
legacy_u16 line_prepare_clipped(legacy_u16 start_x, legacy_u16 start_y, legacy_u16 end_x,
								legacy_u16 end_y, legacy_u16 *line_data);
legacy_u16 line_prepare_unclipped(legacy_u16 start_x, legacy_u16 start_y, legacy_u16 end_x,
								  legacy_u16 end_y, legacy_u16 *line_data);
void skybox_fill_polygon(legacy_u16 color, legacy_u16 vertex_count, struct POINT2D vertices[]);
void sphere_build_perimeter(legacy_u16 *source, legacy_u16 *destination);
void sphere_draw_polygon(legacy_u16 *source, legacy_u16 color);
void wheel_build_perimeter(legacy_u16 *source, legacy_u16 *destination);
void wheel_build_rim_perimeters(legacy_u16 *source, legacy_u16 *destination, legacy_u16 scale);
void wheel_build_vertices(legacy_u16 *source, legacy_u16 *destination, legacy_u16 scale);
void preRender_wheel(const struct POINT2D *source, legacy_u16 scale, legacy_u16 outer_color,
					 legacy_u16 side_color, legacy_u16 inner_color);
void preRender_sphere(legacy_s16 x, legacy_s16 y, legacy_u16 size, legacy_u16 color);
void draw_beveled_border(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
						 legacy_s16 top_outer_color, legacy_s16 top_inner_color,
						 legacy_s16 bottom_outer_color, legacy_s16 bottom_inner_color);
void draw_three_color_beveled_border(legacy_s16 x, legacy_s16 y, legacy_s16 width,
									 legacy_s16 height, legacy_s16 outer_color,
									 legacy_s16 inner_color, legacy_s16 opposite_color);
void shape3d_update_car_wheel_vertices(struct SHAPE3D *shape, legacy_u16 first_vertex,
									   legacy_s16 steering_angle,
									   const legacy_s16 *suspension_offsets,
									   legacy_s16 *cached_wheel_state, struct VECTOR *base_vertices,
									   struct VECTOR *front_wheel_centers);

#endif
