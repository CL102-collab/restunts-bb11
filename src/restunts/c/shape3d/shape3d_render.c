#include <stddef.h>
#include <limits.h>
#if defined(RESTUNTS_SDL3)
#include <stdlib.h>
#include <string.h>
#endif
#include "../externs.h"
#include "../fileio.h"
#include "../legacy.h"
#include "../memmgr.h"
#include "shape3d.h"
#include "../shape2d/shape2d_internal.h"
#include "shape3d_internal.h"
#include "../shape2d/shape2d.h"
#include "../math/math_internal.h"
#include "../car/car_model.h"
#include "../math/projection.h"
#include "../residue.h"
#if defined(RESTUNTS_SDL3)
#undef memcpy
#include "../fatal.h"
#include "../hires/hires.h"
#include "../hires/shape3d_hires.h"
#endif

#include "shape3d_pipeline_internal.h"

static legacy_s16 *legacy_render_wheel_headings;
static legacy_u16 legacy_render_polygon_frame_pointer;
static legacy_u16 legacy_render_polygon_code_segment;
static struct SHAPE3D_LEGACY_OPPONENT_RENDER_CONTEXT legacy_opponent_render_context;

legacy_s8 is_facing_camera(struct POINT2D far *pts);
legacy_u16 projection_scale_x_wrapped(legacy_u16 numerator, legacy_s16 divisor);
legacy_u16 divide_u32_to_u16(legacy_u32 dividend, legacy_u16 divisor);
legacy_u32 projection_multiply_x(legacy_u16 value);
legacy_u32 projection_multiply_y(legacy_u16 value);
legacy_u16 projection_scale_y(legacy_u16 value, legacy_u16 divisor);
static legacy_u16 projection_angle_from_extent(legacy_s16 extent);
static legacy_u16 projection_scale_for_angle(legacy_u16 angle, legacy_u16 extent);
static void projection_update_derived(void);
void set_projection(legacy_s16 horizontal_fov_degrees, legacy_s16 vertical_fov_degrees,
					legacy_s16 width, legacy_s16 height);
void projection_set_origin(legacy_u16 x, legacy_u16 y);
void projection_set_half_fov(legacy_u16 horizontal_half_fov, legacy_u16 vertical_half_fov,
							 legacy_u16 width, legacy_u16 height);
legacy_u16 select_cliprect_rotate(legacy_s16 angZ, legacy_s16 angX, legacy_s16 angY,
								  struct RECTANGLE *cliprect, legacy_s16 half_scale);
void shape3d_set_legacy_render_stack(legacy_s16 *wheel_headings, legacy_u16 polygon_frame_pointer,
									 legacy_u16 polygon_code_segment,
									 const struct SHAPE3D_LEGACY_OPPONENT_RENDER_CONTEXT *opponent);
static void shape3d_retain_legacy_view_rotation(legacy_s16 angZ, legacy_s16 angX, legacy_s16 angY);
void shape3d_retain_legacy_skybox_horizon(legacy_s16 horizon);
void shape3d_retain_legacy_skybox_rect(const struct RECTANGLE *rect);
void shape3d_retain_legacy_skybox_points(const struct POINT2D *points);
void shape3d_retain_legacy_crack_line(const legacy_u16 *line, legacy_u16 end_y,
									  legacy_s16 dirty_rects, legacy_s16 drawn);
void shape3d_retain_legacy_crack_bounds(legacy_s16 line_index, legacy_u16 crack_offset);
static void shape3d_retain_opponent_pattern(legacy_u16 value);
static void shape3d_retain_opponent_polygon_pointer(const legacy_u8 far *record,
													legacy_u16 vertex_count);
static void shape3d_retain_render_local_pair(legacy_u16 first, legacy_u16 second,
											 legacy_u16 return_ip);
static void shape3d_retain_render_local(legacy_u16 first, legacy_u16 return_ip);
static void shape3d_retain_render_argument(legacy_u16 return_ip, legacy_u16 argument);
static void shape3d_retain_sphere_stack(legacy_u16 size);
legacy_u16 shape3d_material_value(const legacy_s16 *table, legacy_u16 index);
static void shape3d_render_ghost(const legacy_u8 far *record, struct POINT2D *points);
static legacy_u16 shape3d_legacy_record_index(polyinfo_index record_index);
#if (defined(RESTUNTS_SDL3))
legacy_u32 shape3d_queued_primitive_count(void);
#endif
void shape3d_render_queued_primitives(void);

legacy_s8 is_facing_camera(struct POINT2D far *pts)
{
	legacy_s32 dx0 = (legacy_s32)pts[0].px - pts[1].px;
	legacy_s32 dx1 = (legacy_s32)pts[2].px - pts[1].px;

	if (dx0 == 0 && dx1 == 0) {
		return 0;
	}

	legacy_s32 dy0 = (legacy_s32)pts[0].py - pts[1].py;
	legacy_s32 dy1 = (legacy_s32)pts[2].py - pts[1].py;

	if (dy0 == 0 && dy1 == 0) {
		return 0;
	}
	legacy_s32 signed_area = (dx1 * dy0) - (dx0 * dy1);
	return signed_area <= 0 ? 0 : 1;
}

legacy_u16 projection_scale_x_wrapped(legacy_u16 numerator, legacy_s16 divisor)
{
	return LEGACY_U16_DIV_OR_ZERO(LEGACY_U16_WRAP_MUL(projection_focal_length_x, numerator),
								  (legacy_u16)divisor);
}

legacy_u16 divide_u32_to_u16(legacy_u32 dividend, legacy_u16 divisor)
{
	return (legacy_u16)LEGACY_U32_DIV_OR_ZERO(dividend, divisor);
}

legacy_u32 projection_multiply_x(legacy_u16 value)
{
	return (legacy_u32)projection_focal_length_x * value;
}

legacy_u32 projection_multiply_y(legacy_u16 value)
{
	return (legacy_u32)projection_focal_length_y * value;
}

legacy_u16 projection_scale_y(legacy_u16 value, legacy_u16 divisor)
{
	return (legacy_u16)LEGACY_U32_DIV_OR_ZERO(LEGACY_U32_WRAP_MUL(projection_focal_length_y, value),
											  divisor);
}

static legacy_u16 projection_angle_from_extent(legacy_s16 extent)
{
	legacy_s32 scaled = LEGACY_S32_WRAP_MUL((legacy_s32)extent, PROJECTION_EXTENT_SCALE);
	legacy_s32 quotient = LEGACY_S32_DIV_OR_ZERO(scaled, PROJECTION_EXTENT_DIVISOR);
	return (legacy_u16)LEGACY_S32_SAR(quotient, 1U);
}

static legacy_u16 projection_scale_for_angle(legacy_u16 angle, legacy_u16 extent)
{
	legacy_s32 product = LEGACY_S32_WRAP_MUL((legacy_s32)cos_fast(angle), (legacy_s32)extent);
	legacy_s32 quotient = LEGACY_S32_DIV_OR_ZERO(product, (legacy_s32)sin_fast(angle));
	return (legacy_u16)quotient;
}

static void projection_update_derived(void)
{
	projection_center_x = LEGACY_U16_WRAP_ADD(projection_half_width, projection_origin_x);
	projection_center_y = LEGACY_U16_WRAP_ADD(projection_half_height, projection_origin_y);
	projection_focal_length_x =
		projection_scale_for_angle(projection_horizontal_half_fov, projection_half_width);
	if (projection_vertical_half_fov != 0) {
		projection_focal_length_y =
			projection_scale_for_angle(projection_vertical_half_fov, projection_half_height);
	} else {
		projection_focal_length_y = LEGACY_U16_WRAP_SUB(
			LEGACY_U16_WRAP_SUB(projection_focal_length_x, projection_focal_length_x >> 3),
			projection_focal_length_x >> 4);
		projection_vertical_half_fov =
			polarAngle(projection_focal_length_y, projection_half_height);
	}
}

void set_projection(legacy_s16 horizontal_fov_degrees, legacy_s16 vertical_fov_degrees,
					legacy_s16 width, legacy_s16 height)
{
	projection_horizontal_half_fov = projection_angle_from_extent(horizontal_fov_degrees);
	projection_vertical_half_fov = projection_angle_from_extent(vertical_fov_degrees);
	projection_half_width = (legacy_u16)LEGACY_S16_SAR(width, 1U);
	projection_half_height = (legacy_u16)LEGACY_S16_SAR(height, 1U);
	projection_update_derived();
}

void projection_set_origin(legacy_u16 x, legacy_u16 y)
{
	projection_origin_x = x;
	projection_center_x = LEGACY_U16_WRAP_ADD(projection_half_width, x);
	projection_origin_y = y;
	projection_center_y = LEGACY_U16_WRAP_ADD(projection_half_height, y);
}

void projection_set_half_fov(legacy_u16 horizontal_half_fov, legacy_u16 vertical_half_fov,
							 legacy_u16 width, legacy_u16 height)
{
	projection_horizontal_half_fov = horizontal_half_fov;
	projection_vertical_half_fov = vertical_half_fov;
	projection_half_width = width >> 1;
	projection_half_height = height >> 1;
	projection_update_derived();
}

legacy_u16 select_cliprect_rotate(legacy_s16 angZ, legacy_s16 angX, legacy_s16 angY,
								  struct RECTANGLE *cliprect, legacy_s16 half_scale)
{
	//return ported_select_cliprect_rotate_(angX, angY, angZ, cliprect, half_scale);

	mat_temp = *mat_rot_zxy(angZ, angX, angY, MATRIX_ROTATION_ORDER_YXZ);
	polyinfo_reset();
	select_rect_rc = *cliprect;
	shape_half_scale = half_scale;
#if defined(RESTUNTS_SDL3)
	shape3d_hires_set_half_scale(half_scale);
#endif
	struct MATRIX *inverse_view_rotation =
		mat_rot_zxy(-angZ, -angX, -angY, MATRIX_ROTATION_ORDER_ZXY);
	shape3d_retain_legacy_view_rotation(angZ, angX, angY);
	struct VECTOR forward_axis;
	forward_axis.z = PROJECTION_VIEW_VECTOR_LENGTH;
	forward_axis.y = 0;
	forward_axis.x = 0;
	struct VECTOR view_direction;
	mat_mul_vector(&forward_axis, inverse_view_rotation, &view_direction);
	return polarAngle(view_direction.x, view_direction.z) & PROJECTION_YAW_MASK;
}

void shape3d_set_legacy_render_stack(legacy_s16 *wheel_headings, legacy_u16 polygon_frame_pointer,
									 legacy_u16 polygon_code_segment,
									 const struct SHAPE3D_LEGACY_OPPONENT_RENDER_CONTEXT *opponent)
{
	legacy_render_wheel_headings = wheel_headings;
	legacy_render_player_headings_active =
		wheel_headings == legacy_execution_residue.wheel_plane_angles;
	legacy_render_polygon_frame_pointer = polygon_frame_pointer;
	legacy_render_polygon_code_segment = polygon_code_segment;
	if (opponent != 0) {
		legacy_opponent_render_context = *opponent;
	} else {
		legacy_opponent_render_context.wheel_headings = 0;
	}
}

static void shape3d_retain_legacy_view_rotation(legacy_s16 angZ, legacy_s16 angX, legacy_s16 angY)
{
	legacy_s16 rotate_z;
	legacy_s16 rotate_x;
	legacy_s16 rotate_y;
	legacy_s16 angle;
	legacy_s16 *headings;
	legacy_u16 frame_pointer;
	legacy_u16 return_ip;
	legacy_u16 axes;

	if (legacy_opponent_render_context.wheel_headings == 0) {
		return;
	}
	rotate_z = (angZ & ANGLE_MASK) != 0;
	rotate_x = (angX & ANGLE_MASK) != 0;
	rotate_y = (angY & ANGLE_MASK) != 0;
	headings = legacy_opponent_render_context.wheel_headings;
	frame_pointer = (legacy_u16)(legacy_render_polygon_frame_pointer -
								 SHAPE3D_LEGACY_VIEW_ROTATION_STACK_OFFSET);
	if ((rotate_z && rotate_x) || (rotate_z && rotate_y) || (rotate_x && rotate_y)) {
		axes = (legacy_u16)((rotate_z ? SHAPE3D_LEGACY_ROTATE_Z_FLAG : 0) |
							(rotate_x ? SHAPE3D_LEGACY_ROTATE_X_FLAG : 0) |
							(rotate_y ? SHAPE3D_LEGACY_ROTATE_Y_FLAG : 0));
		headings[0] = LEGACY_S16_FROM_BITS(axes);
		headings[1] = LEGACY_S16_FROM_BITS(frame_pointer);
		headings[2] = LEGACY_S16_FROM_BITS(axes == SHAPE3D_LEGACY_ROTATE_ALL_FLAGS
											   ? SHAPE3D_LEGACY_ROTATE_TRIPLE_RETURN_IP
											   : SHAPE3D_LEGACY_ROTATE_PAIR_RETURN_IP);
		headings[3] = LEGACY_S16_FROM_BITS(legacy_render_polygon_code_segment);
		return;
	} else if (rotate_z) {
		angle = LEGACY_S16_WRAP_NEGATE(angZ);
		return_ip = SHAPE3D_LEGACY_ROTATE_Z_RETURN_IP;
	} else if (rotate_x) {
		angle = LEGACY_S16_WRAP_NEGATE(angX);
		return_ip = SHAPE3D_LEGACY_ROTATE_X_RETURN_IP;
	} else if ((angY & ANGLE_QUARTER_MASK) != 0) {
		angle = LEGACY_S16_WRAP_NEGATE(angY);
		return_ip = SHAPE3D_LEGACY_ROTATE_Y_RETURN_IP;
	} else {
		return;
	}
	headings[0] = sin_fast(angle);
	headings[1] = cos_fast(angle);
	headings[2] = LEGACY_S16_FROM_BITS(frame_pointer);
	headings[3] = LEGACY_S16_FROM_BITS(return_ip);
}

void shape3d_retain_legacy_skybox_horizon(legacy_s16 horizon)
{
	if (legacy_opponent_render_context.wheel_headings != 0) {
		legacy_opponent_render_context.wheel_headings[0] = horizon;
	}
}

void shape3d_retain_legacy_skybox_rect(const struct RECTANGLE *rect)
{
	if (legacy_opponent_render_context.wheel_headings != 0) {
		legacy_opponent_render_context.wheel_headings[1] = rect->left;
		legacy_opponent_render_context.wheel_headings[2] = rect->right;
		legacy_opponent_render_context.wheel_headings[3] = rect->top;
	}
}

void shape3d_retain_legacy_skybox_points(const struct POINT2D *points)
{
	if (legacy_render_wheel_headings != 0) {
		legacy_render_wheel_headings[0] = points[0].px;
		legacy_render_wheel_headings[1] = points[0].py;
		legacy_render_wheel_headings[2] = points[1].px;
		legacy_render_wheel_headings[3] = points[1].py;
	}
}

void shape3d_retain_legacy_crack_line(const legacy_u16 *line, legacy_u16 end_y,
									  legacy_s16 dirty_rects, legacy_s16 drawn)
{
	legacy_u16 i;
	legacy_u16 opponent_index;

	if (legacy_render_wheel_headings != 0) {
		if (dirty_rects != 0) {
			for (i = 0; i < LEGACY_RESIDUE_WORD_COUNT; i++) {
				legacy_render_wheel_headings[i] = LEGACY_S16_FROM_BITS(line[i]);
			}
		} else {
			/* Drawing replaces the setup call's end-Y argument with its return CS. */
			legacy_render_wheel_headings[0] = LEGACY_S16_FROM_BITS(
				drawn != 0 ? LEGACY_U16_WRAP_ADD(legacy_render_polygon_code_segment,
												 SHAPE3D_LEGACY_LINE_CODE_PARAGRAPH_DELTA)
						   : end_y);
			legacy_render_wheel_headings[1] = LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_SUB(
				legacy_render_polygon_frame_pointer, SHAPE3D_LEGACY_CRACK_LINE_STACK_OFFSET));
			legacy_render_wheel_headings[2] =
				LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_FRACTION_INDEX]);
			legacy_render_wheel_headings[3] = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]);
		}
	}
	if (legacy_opponent_render_context.wheel_headings != 0) {
		opponent_index =
			dirty_rects != 0 ? DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX : DRAW_LINE_COLOR_INDEX;
		for (i = 0; i < LEGACY_RESIDUE_WORD_COUNT; i++) {
			legacy_opponent_render_context.wheel_headings[i] =
				LEGACY_S16_FROM_BITS(line[opponent_index + i]);
		}
	}
}

void shape3d_retain_legacy_crack_bounds(legacy_s16 line_index, legacy_u16 crack_offset)
{
	if (legacy_opponent_render_context.wheel_headings != 0) {
		legacy_opponent_render_context.wheel_headings[2] = line_index;
		legacy_opponent_render_context.wheel_headings[3] = LEGACY_S16_FROM_BITS(crack_offset);
	}
}

static void shape3d_retain_opponent_pattern(legacy_u16 value)
{
	if (legacy_opponent_render_context.wheel_headings != 0) {
		legacy_opponent_render_context.wheel_headings[0] = LEGACY_S16_FROM_BITS(value);
	}
}

static void shape3d_retain_opponent_polygon_pointer(const legacy_u8 far *record,
													legacy_u16 vertex_count)
{
	if (legacy_opponent_render_context.wheel_headings == 0) {
		return;
	}
	legacy_u16 offset = LEGACY_U16_WRAP_ADD(legacy_opponent_render_context.polyinfo_offset,
											(legacy_u16)(record - polyinfoptr));
	offset = LEGACY_U16_WRAP_ADD(offset, SHAPE3D_LEGACY_POLYGON_HEADER_SIZE);
	offset = LEGACY_U16_WRAP_ADD(
		offset, LEGACY_U16_WRAP_MUL(vertex_count, SHAPE3D_LEGACY_POLYGON_POINT_SIZE));
	legacy_opponent_render_context.wheel_headings[1] = LEGACY_S16_FROM_BITS(offset);
	/* The original increments only the pointer's offset, without normalizing it. */
	legacy_opponent_render_context.wheel_headings[2] =
		LEGACY_S16_FROM_BITS(legacy_opponent_render_context.polyinfo_segment);
}

static void shape3d_retain_render_local_pair(legacy_u16 first, legacy_u16 second,
											 legacy_u16 return_ip)
{
	if (legacy_render_wheel_headings == 0) {
		return;
	}
	legacy_render_wheel_headings[0] = LEGACY_S16_FROM_BITS(first);
	legacy_render_wheel_headings[1] = LEGACY_S16_FROM_BITS(second);
	legacy_render_wheel_headings[2] = LEGACY_S16_FROM_BITS(legacy_render_polygon_frame_pointer);
	legacy_render_wheel_headings[3] = LEGACY_S16_FROM_BITS(return_ip);
}

static void shape3d_retain_render_local(legacy_u16 first, legacy_u16 return_ip)
{
	if (legacy_render_wheel_headings == 0) {
		return;
	}
	legacy_render_wheel_headings[0] = LEGACY_S16_FROM_BITS(first);
	legacy_render_wheel_headings[1] = LEGACY_S16_FROM_BITS(legacy_render_polygon_frame_pointer);
	legacy_render_wheel_headings[2] = LEGACY_S16_FROM_BITS(return_ip);
	legacy_render_wheel_headings[3] = LEGACY_S16_FROM_BITS(legacy_render_polygon_code_segment);
}

static void shape3d_retain_render_argument(legacy_u16 return_ip, legacy_u16 argument)
{
	if (legacy_render_wheel_headings == 0) {
		return;
	}
	legacy_render_wheel_headings[0] = LEGACY_S16_FROM_BITS(legacy_render_polygon_frame_pointer);
	legacy_render_wheel_headings[1] = LEGACY_S16_FROM_BITS(return_ip);
	legacy_render_wheel_headings[2] = LEGACY_S16_FROM_BITS(legacy_render_polygon_code_segment);
	legacy_render_wheel_headings[3] = LEGACY_S16_FROM_BITS(argument);
}

static void shape3d_retain_sphere_stack(legacy_u16 size)
{
	if (legacy_render_wheel_headings == 0) {
		return;
	}
	legacy_u16 effective_height =
		LEGACY_U16_WRAP_ADD(LEGACY_U16_WRAP_SUB(size, size >> 2), size >> 4);
	/* preRender_sphere writes BP-2 only after rejecting nonpositive heights
	 * and the one-pixel shortcut; its saved BP/return words always change. */
	legacy_u16 first = LEGACY_S16_FROM_BITS(effective_height) >= 2
						   ? LEGACY_U16_WRAP_SUB(drawing_sprite.sprite_raster_right, 1U)
						   : (legacy_u16)legacy_render_wheel_headings[0];
	shape3d_retain_render_local(first, SHAPE3D_LEGACY_SPHERE_RETURN_IP);
}

legacy_u16 shape3d_material_value(const legacy_s16 *table, legacy_u16 index)
{
	/* Resource materials use a byte, and wheel colors read two entries further.
	 * The original maps are adjacent: color, pattern type, then pattern bits.
	 * Model those reads without relying on the host's global-variable layout.
	 * Extended pattern types never select a secondary-pattern lookup. */
	if (index >= SHAPE3D_MATERIAL_COUNT) {
		if (table == material_color_list) {
			table = material_pattern_list;
			index -= SHAPE3D_MATERIAL_COUNT;
		} else if (table == material_pattern_list) {
			table = material_pattern2_list;
			index -= SHAPE3D_MATERIAL_COUNT;
		}
	}
	return (legacy_u16)table[index];
}

static void shape3d_render_ghost(const legacy_u8 far *record, struct POINT2D *points)
{
	legacy_u16 primitive_type = record[POLYINFO_TYPE_OFFSET] & ~RENDER_PRIMITIVE_VISUAL_FLAGS;
	if (primitive_type == RENDER_PRIMITIVE_POLYGON) {
		legacy_u16 material = record[POLYINFO_MATERIAL_OFFSET];
		legacy_u16 pattern_type = shape3d_material_value(material_patlist_ptr_cpy, material);
		if (pattern_type > SHAPE3D_PATTERN_TWO_COLOR ||
			(pattern_type == SHAPE3D_PATTERN_MASKED &&
			 shape3d_material_value(material_patlist2_ptr_cpy, material) == 0U)) {
			return;
		}
		polyinfo_read_points(record, points, record[POLYINFO_VERTEX_COUNT_OFFSET]);
		preRender_default(PRERENDER_GHOST_COLOR, record[POLYINFO_VERTEX_COUNT_OFFSET], points);
	} else if (primitive_type == RENDER_PRIMITIVE_WHEEL) {
		polyinfo_read_points(record, points, WHEEL_SOURCE_POINT_COUNT);
		preRender_wheel(points, WHEEL_INNER_RADIUS_SCALE, PRERENDER_GHOST_COLOR,
						PRERENDER_GHOST_COLOR, PRERENDER_GHOST_COLOR);
	} else if (primitive_type == RENDER_PRIMITIVE_SPHERE) {
		preRender_sphere(LEGACY_S16_FROM_BITS(polyinfo_read_word(record, POLYINFO_START_X_WORD)),
						 LEGACY_S16_FROM_BITS(polyinfo_read_word(record, POLYINFO_START_Y_WORD)),
						 polyinfo_read_word(record, POLYINFO_SPHERE_SIZE_WORD),
						 PRERENDER_GHOST_COLOR);
	} else if (primitive_type == RENDER_PRIMITIVE_LINE ||
			   primitive_type == RENDER_PRIMITIVE_POINT) {
		legacy_u16 x = polyinfo_read_word(record, POLYINFO_START_X_WORD);
		legacy_u16 y = polyinfo_read_word(record, POLYINFO_START_Y_WORD);
		preRender_line(x, y,
					   primitive_type == RENDER_PRIMITIVE_POINT
						   ? x
						   : polyinfo_read_word(record, POLYINFO_END_X_WORD),
					   primitive_type == RENDER_PRIMITIVE_POINT
						   ? y
						   : polyinfo_read_word(record, POLYINFO_END_Y_WORD),
					   PRERENDER_GHOST_COLOR);
	}
}

static legacy_u16 shape3d_legacy_record_index(polyinfo_index record_index)
{
	polyinfo_index result = record_index;
	if (queued_ghost_primitives != 0U) {
		for (polyinfo_index index = 0; index < record_index; index++) {
			if ((polyinfoptr[polygon_record_offsets[index] + POLYINFO_TYPE_OFFSET] &
				 RENDER_PRIMITIVE_GHOST_FLAG) != 0U) {
				result--;
			}
		}
	}
	return result;
}

#if (defined(RESTUNTS_SDL3))
legacy_u32 shape3d_queued_primitive_count(void)
{
	if (hires_enabled()) {
		return shape3d_hires_scene_count();
	}
	return polyinfonumpolys;
}
#endif

void shape3d_render_queued_primitives(void)
{
#if defined(RESTUNTS_TEMPLEOS)
	TempleRasterNative(1);
#endif
#if defined(RESTUNTS_SDL3)
	if (shape3d_hires_render_scene()) {
		polyinfo_reset();
		return;
	}
#endif
	polyinfo_index record_index = polyinfo_primitive_capacity;
	polyinfo_index rendered_ghost_primitives = 0;
	struct POINT2D points[POLYINFO_MAX_RENDER_POINTS];
	for (polyinfo_index primitive_index = 0; primitive_index < polyinfonumpolys;
		 primitive_index++) {
		record_index = (polyinfo_index)polygon_next_index[record_index];
		legacy_u8 far *record = polyinfoptr + polygon_record_offsets[record_index];
		if ((record[POLYINFO_TYPE_OFFSET] & RENDER_PRIMITIVE_GHOST_FLAG) != 0U) {
			shape3d_render_ghost(record, points);
			rendered_ghost_primitives++;
			continue;
		}
		legacy_u16 material_type = record[POLYINFO_MATERIAL_OFFSET];
		legacy_u16 material_color = shape3d_material_value(material_clrlist_ptr_cpy, material_type);
		legacy_u16 primitive_type = record[POLYINFO_TYPE_OFFSET] & ~RENDER_PRIMITIVE_VISUAL_FLAGS;
		if ((record[POLYINFO_TYPE_OFFSET] & RENDER_PRIMITIVE_BLACK_GRILLE_FLAG) != 0U &&
			primitive_type == RENDER_PRIMITIVE_POLYGON) {
			material_color = 0;
		}

		if (primitive_type == RENDER_PRIMITIVE_POLYGON) {
			legacy_u16 vertex_count = record[POLYINFO_VERTEX_COUNT_OFFSET];
			polyinfo_read_points(record, points, vertex_count);
			shape3d_retain_opponent_polygon_pointer(record, vertex_count);
			legacy_u16 pattern_type =
				shape3d_material_value(material_patlist_ptr_cpy, material_type);
			if (pattern_type == SHAPE3D_PATTERN_SOLID) {
				shape3d_retain_render_local_pair(
					LEGACY_U16_WRAP_SUB(drawing_sprite.sprite_raster_right, 1U),
					drawing_sprite.sprite_raster_left, SHAPE3D_LEGACY_SOLID_POLYGON_RETURN_IP);
				preRender_default(material_color, vertex_count, points);
			} else if (pattern_type == SHAPE3D_PATTERN_MASKED) {
				pattern_type = shape3d_material_value(material_patlist2_ptr_cpy, material_type);
				shape3d_retain_opponent_pattern(pattern_type);
				if (pattern_type != 0U) {
					shape3d_retain_render_local(drawing_sprite.sprite_raster_left,
												SHAPE3D_LEGACY_PATTERNED_POLYGON_RETURN_IP);
					preRender_patterned(pattern_type, material_color, vertex_count, points);
				}
			} else if (pattern_type == SHAPE3D_PATTERN_TWO_COLOR) {
				shape3d_retain_opponent_pattern(LEGACY_U16_WRAP_MUL(material_type, 2U));
				/* preRender_unk replaces its first argument with the secondary color. */
				shape3d_retain_render_argument(
					SHAPE3D_LEGACY_TWO_COLOR_POLYGON_RETURN_IP,
					shape3d_material_value(material_clrlist2_ptr_cpy, material_type));
				preRender_two_color(
					shape3d_material_value(material_patlist2_ptr_cpy, material_type),
					shape3d_material_value(material_clrlist2_ptr_cpy, material_type),
					material_color, vertex_count, points);
			}
		} else if (primitive_type == RENDER_PRIMITIVE_LINE) {
			shape3d_retain_render_argument(SHAPE3D_LEGACY_LINE_RETURN_IP,
										   polyinfo_read_word(record, POLYINFO_START_X_WORD));
			preRender_line(polyinfo_read_word(record, POLYINFO_START_X_WORD),
						   polyinfo_read_word(record, POLYINFO_START_Y_WORD),
						   polyinfo_read_word(record, POLYINFO_END_X_WORD),
						   polyinfo_read_word(record, POLYINFO_END_Y_WORD), material_color);
		} else if (primitive_type == RENDER_PRIMITIVE_SPHERE) {
			shape3d_retain_sphere_stack(polyinfo_read_word(record, POLYINFO_SPHERE_SIZE_WORD));
			preRender_sphere(
				LEGACY_S16_FROM_BITS(polyinfo_read_word(record, POLYINFO_START_X_WORD)),
				LEGACY_S16_FROM_BITS(polyinfo_read_word(record, POLYINFO_START_Y_WORD)),
				polyinfo_read_word(record, POLYINFO_SPHERE_SIZE_WORD), material_color);
		} else if (primitive_type == RENDER_PRIMITIVE_WHEEL) {
			shape3d_retain_opponent_pattern(
				LEGACY_U16_WRAP_ADD(legacy_opponent_render_context.material_color_offset,
									LEGACY_U16_WRAP_MUL(material_type, 2U)));
			shape3d_retain_render_argument(
				SHAPE3D_LEGACY_WHEEL_RETURN_IP,
				LEGACY_U16_WRAP_SUB(legacy_render_polygon_frame_pointer,
									SHAPE3D_LEGACY_POLYGON_POINTS_STACK_OFFSET));
			polyinfo_read_points(record, points, WHEEL_SOURCE_POINT_COUNT);
			preRender_wheel(points, WHEEL_INNER_RADIUS_SCALE, material_color,
							shape3d_material_value(material_clrlist_ptr_cpy, material_type + 1U),
							shape3d_material_value(material_clrlist_ptr_cpy, material_type + 2U));
		} else if (primitive_type == RENDER_PRIMITIVE_POINT) {
			shape3d_retain_render_local_pair(shape3d_legacy_record_index(record_index),
											 primitive_index - rendered_ghost_primitives,
											 SHAPE3D_LEGACY_POINT_RETURN_IP);
#if defined(RESTUNTS_TEMPLEOS)
			legacy_u16 x = polyinfo_read_word(record, POLYINFO_START_X_WORD);
			legacy_u16 y = polyinfo_read_word(record, POLYINFO_START_Y_WORD);
			preRender_line(x, y, x, y, material_color);
#else
			sprite_putpixel_clipped(
				LEGACY_S16_FROM_BITS(polyinfo_read_word(record, POLYINFO_START_X_WORD)),
				LEGACY_S16_FROM_BITS(polyinfo_read_word(record, POLYINFO_START_Y_WORD)),
				material_color);
#endif
		}
	}
#if defined(RESTUNTS_TEMPLEOS)
	TempleRasterNative(0);
#endif
	polyinfo_reset();
}
