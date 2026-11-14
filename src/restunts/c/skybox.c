#include "frame/frame_internal.h"
#include "shape2d/shape2d.h"
#include "skybox.h"
#include "video/video_color.h"
#include "externs.h"
#ifdef RESTUNTS_SDL3
#include "hires/skybox_hires.h"
#include "hires/skybox_scene.h"
#include "shape2d/shape2d_internal.h"
#endif

#define SKYBOX_SCREEN_RIGHT 319

#define SKYBOX_ROLL_VECTOR_POSITIVE_X 18000U
#define SKYBOX_ROLL_VECTOR_NEGATIVE_X_BITS 47536U
#define SKYBOX_HORIZON_RADIUS 16000

#define SKYBOX_CHANGED_RECT_COUNT 15
#define SKYBOX_RECT_INDEX 5
#define SKYBOX_RECT_CHANGED 1
#define SKYBOX_RECT_FORCE_REDRAW 3
#define SKYBOX_MAX_LINEAR_HORIZON_DELTA 96
#define SKYBOX_MAX_HORIZON_STRIPS 32

#define SKYBOX_ROLL_VECTOR_COUNT 2
#define SKYBOX_ROLL_POINT_COUNT 6
#define SKYBOX_CORNER_COUNT 4
#define SKYBOX_POLYGON_POINT_COUNT 4
#define SKYBOX_LINE_DATA_WORD_COUNT 14
#define SKYBOX_FORWARD_DIRECTION 1

void skybox_copy_image(legacy_s16 image, legacy_s16 x, legacy_s16 y)
{
	sprite_copy_image_at(skyboxes[image], x, y);
#ifdef RESTUNTS_SDL3
	skybox_hires_draw(&drawing_sprite, loaded_skybox_index, image,
					  shape2d_get_width(skyboxes[image]), shape2d_get_height(skyboxes[image]), x,
					  y);
#endif
}

void skybox_render_level_rect(struct RECTANGLE *rect, legacy_s16 angle, legacy_s16 horizon)
{
	legacy_u16 top = (legacy_u16)rect->top;
	legacy_u16 bottom = (legacy_u16)rect->bottom;
	legacy_u16 left = (legacy_u16)rect->left;
	legacy_u16 right = (legacy_u16)rect->right;
	legacy_u16 horizon_bits = (legacy_u16)horizon;
	legacy_u16 sky_lines = LEGACY_U16_WRAP_SUB(horizon_bits, top);
	if (detail_level != SKYBOX_LOWEST_DETAIL_LEVEL) {
		sky_lines = LEGACY_U16_WRAP_SUB(sky_lines, skybox.minimum_height);
	}
	legacy_u16 rect_height = LEGACY_U16_WRAP_SUB(bottom, top);
	if (LEGACY_S16_FROM_BITS(rect_height) < LEGACY_S16_FROM_BITS(sky_lines)) {
		sky_lines = rect_height;
	}
	if (LEGACY_S16_FROM_BITS(sky_lines) > 0) {
		sprite_set_target_clip_bounds(left, right, top, LEGACY_U16_WRAP_ADD(top, sky_lines));
		VIDEO_MATERIAL_BEGIN(SKYBOX_MATERIAL_SKY);
		sprite_clear_target((legacy_u8)skybox.sky_color);
		VIDEO_DIRECT_END();
	}

	if (detail_level != SKYBOX_LOWEST_DETAIL_LEVEL &&
		LEGACY_S16_FROM_BITS(top) < LEGACY_S16_FROM_BITS(horizon_bits) &&
		LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_SUB(horizon_bits, skybox.maximum_height)) <=
			LEGACY_S16_FROM_BITS(bottom)) {
		sprite_set_target_clip_bounds(left, right, top, bottom);
		legacy_u16 image_x = LEGACY_U16_WRAP_SUB(
			LEGACY_U16_WRAP_ADD(angle, ANGLE_HALF_TURN) & ANGLE_MASK, SKYBOX_IMAGE_FULL_WRAP);
		skybox_copy_image(0, LEGACY_S16_FROM_BITS(image_x),
						  LEGACY_S16_WRAP_SUB(horizon_bits, skybox.heights[0]));
		skybox_copy_image(1, LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(image_x, SKYBOX_IMAGE_WIDTH)),
						  LEGACY_S16_WRAP_SUB(horizon_bits, skybox.heights[1]));
		skybox_copy_image(
			2, LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(image_x, SKYBOX_IMAGE_HALF_WRAP)),
			LEGACY_S16_WRAP_SUB(horizon_bits, skybox.heights[2]));
		skybox_copy_image(
			3, LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(image_x, SKYBOX_IMAGE_ONE_AND_HALF_WIDTH)),
			LEGACY_S16_WRAP_SUB(horizon_bits, skybox.heights[3]));
		skybox_copy_image(
			0, LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(image_x, SKYBOX_IMAGE_FULL_WRAP)),
			LEGACY_S16_WRAP_SUB(horizon_bits, skybox.heights[0]));
	}

	legacy_u16 ground_top = horizon_bits;
	if (LEGACY_S16_FROM_BITS(top) > LEGACY_S16_FROM_BITS(horizon_bits)) {
		ground_top = top;
	}
	legacy_u16 ground_lines = LEGACY_U16_WRAP_SUB(bottom, ground_top);
	if (LEGACY_S16_FROM_BITS(ground_lines) > 0) {
		sprite_set_target_clip_bounds(left, right, ground_top,
									  LEGACY_U16_WRAP_ADD(ground_top, ground_lines));
		VIDEO_MATERIAL_BEGIN(SKYBOX_MATERIAL_GROUND);
		sprite_clear_target((legacy_u8)skybox.ground_color);
		VIDEO_DIRECT_END();
	}
}

static legacy_s16 skybox_scaled_constant(legacy_u16 value, legacy_s16 scale)
{
	return LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_MUL(value, (legacy_u16)scale));
}

static void skybox_clear_rect(const struct RECTANGLE *rect, legacy_s16 color)
{
	sprite_set_target_clip_bounds(rect->left, rect->right, rect->top, rect->bottom);
	sprite_clear_target((legacy_u8)color);
}

static void skybox_collect_changed_rects(struct RECTANGLE *clip)
{
	redraw_rect_count = 0;
	rectlist_add_rects(SKYBOX_CHANGED_RECT_COUNT, frame_rect_change_flags, active_frame_rects,
					   frame_layer_rects, clip, &redraw_rect_count, merged_redraw_rects);
}

static void skybox_clear_changed_rects(struct RECTANGLE *rect, struct RECTANGLE *clip,
									   legacy_s16 color)
{
	legacy_s16 outside = rect_intersect(rect, clip);
	shape3d_retain_legacy_skybox_rect(rect);
	if (outside != 0) {
		return;
	}
	skybox_collect_changed_rects(rect);
	for (legacy_s16 i = 0; i < (legacy_s8)redraw_rect_count; i++) {
		skybox_clear_rect(&merged_redraw_rects[i], color);
	}
}

static legacy_u8 skybox_project_rolled_horizon(legacy_s16 direction, struct MATRIX *rotation,
											   legacy_s16 camera_y, struct POINT2D *points)
{
	struct VECTOR source;
	source.x = skybox_scaled_constant(SKYBOX_ROLL_VECTOR_POSITIVE_X, (legacy_s16)direction);
	source.y = LEGACY_S16_WRAP_NEGATE((legacy_s16)camera_y);
	source.z = skybox_scaled_constant(SKYBOX_ROLL_VECTOR_Z, (legacy_s16)direction);
	struct VECTOR vectors[SKYBOX_ROLL_VECTOR_COUNT];
	mat_mul_vector(&source, rotation, &vectors[0]);
	source.x = skybox_scaled_constant(SKYBOX_ROLL_VECTOR_NEGATIVE_X_BITS, (legacy_s16)direction);
	mat_mul_vector(&source, rotation, &vectors[1]);

	if (vectors[0].z < 0 || vectors[1].z < 0) {
		VIDEO_MATERIAL_BEGIN(SKYBOX_MATERIAL_SKY);
		sprite_clear_target((legacy_u8)skybox.sky_color);
		VIDEO_DIRECT_END();
		return 1;
	}
	vector_to_point(&vectors[0], &points[0]);
	vector_to_point(&vectors[1], &points[1]);

	return 0;
}

static legacy_u8 skybox_clear_outside_horizon(struct RECTANGLE *clip, const struct POINT2D *points)
{
	legacy_s16 fill_color;

	if (points[0].px > SKYBOX_SCREEN_WIDTH && points[1].px > SKYBOX_SCREEN_WIDTH) {
		fill_color = points[0].py < points[1].py ? skybox.sky_color : skybox.ground_color;
		VIDEO_MATERIAL_BEGIN(points[0].py < points[1].py ? SKYBOX_MATERIAL_SKY
														 : SKYBOX_MATERIAL_GROUND);
		sprite_clear_target((legacy_u8)fill_color);
		VIDEO_DIRECT_END();
		return 1;
	}
	if (points[0].px < 0 && points[1].px < 0) {
		fill_color = points[0].py <= points[1].py ? skybox.ground_color : skybox.sky_color;
		VIDEO_MATERIAL_BEGIN(points[0].py <= points[1].py ? SKYBOX_MATERIAL_GROUND
														  : SKYBOX_MATERIAL_SKY);
		sprite_clear_target((legacy_u8)fill_color);
		VIDEO_DIRECT_END();
		return 1;
	}
	if (clip->bottom < points[0].py && clip->bottom < points[1].py) {
		fill_color = points[0].px <= points[1].px ? skybox.ground_color : skybox.sky_color;
		VIDEO_MATERIAL_BEGIN(points[0].px <= points[1].px ? SKYBOX_MATERIAL_GROUND
														  : SKYBOX_MATERIAL_SKY);
		sprite_clear_target((legacy_u8)fill_color);
		VIDEO_DIRECT_END();
		return 1;
	}
	if (clip->top > points[0].py && clip->top > points[1].py) {
		fill_color = points[0].px >= points[1].px ? skybox.ground_color : skybox.sky_color;
		VIDEO_MATERIAL_BEGIN(points[0].px >= points[1].px ? SKYBOX_MATERIAL_GROUND
														  : SKYBOX_MATERIAL_SKY);
		sprite_clear_target((legacy_u8)fill_color);
		VIDEO_DIRECT_END();
		return 1;
	}
	return 0;
}

static legacy_u8 skybox_find_linear_horizon(const struct POINT2D *points, legacy_s16 *base,
											legacy_s16 *delta)
{
	legacy_u16 line_data[SKYBOX_LINE_DATA_WORD_COUNT];
	legacy_s16 base_horizon = 0;
	legacy_s16 horizon_delta = 0;
	legacy_u8 has_linear_horizon = 0;
	if (detail_level != SKYBOX_LOWEST_DETAIL_LEVEL && points[1].px < 0 &&
		points[0].px > SKYBOX_SCREEN_WIDTH &&
		line_prepare_clipped(points[1].px, points[1].py, points[0].px, points[0].py, line_data) ==
			0) {
		legacy_s16 absolute_delta = LEGACY_S16_WRAP_SUB(line_data[3], line_data[5]);
		if (absolute_delta < 0) {
			absolute_delta = LEGACY_S16_WRAP_NEGATE(absolute_delta);
		}
		if (absolute_delta < SKYBOX_MAX_LINEAR_HORIZON_DELTA) {
			if (line_data[1] == 0) {
				base_horizon = LEGACY_S16_FROM_BITS(line_data[3]);
				horizon_delta = LEGACY_S16_WRAP_SUB(line_data[5], base_horizon);
				has_linear_horizon = 1;
			} else if (line_data[1] == SKYBOX_SCREEN_RIGHT) {
				base_horizon = LEGACY_S16_FROM_BITS(line_data[5]);
				horizon_delta = LEGACY_S16_WRAP_SUB(line_data[3], base_horizon);
				has_linear_horizon = 1;
			}
		}
	}
	if (has_linear_horizon != 0) {
		shape3d_retain_legacy_skybox_horizon(base_horizon);
	}
	*base = base_horizon;
	*delta = horizon_delta;
	return has_linear_horizon;
}

static void skybox_prepare_horizon_rect(struct RECTANGLE *clip, legacy_s16 base_horizon,
										legacy_s16 horizon_delta, struct RECTANGLE *work_rect)
{
	if (slow_video_mgmt_copy != 0) {
		work_rect->left = 0;
		work_rect->right = SKYBOX_SCREEN_WIDTH;
		rect_skybox.left = 0;
		rect_skybox.right = SKYBOX_SCREEN_WIDTH;
		if (full_redraw_frames_remaining != 0) {
			rect_skybox.top = clip->top;
			rect_skybox.bottom = clip->bottom;
		} else {
			legacy_s16 horizon = LEGACY_S16_WRAP_ADD(base_horizon, horizon_delta);
			rect_skybox.top = horizon < base_horizon ? horizon : base_horizon;
			rect_skybox.top = LEGACY_S16_WRAP_SUB(rect_skybox.top, skybox.maximum_height);
			if (clip->top > rect_skybox.top) {
				rect_skybox.top = clip->top;
			}
			rect_skybox.bottom = horizon > base_horizon ? horizon : base_horizon;
			for (legacy_s16 i = 0; i < SKYBOX_CHANGED_RECT_COUNT; i++) {
				frame_rect_change_flags[i] = SKYBOX_RECT_CHANGED;
			}
			frame_rect_change_flags[SKYBOX_RECT_INDEX] = SKYBOX_RECT_FORCE_REDRAW;

			work_rect->top = 0;
			work_rect->bottom = rect_skybox.top;
			VIDEO_MATERIAL_BEGIN(SKYBOX_MATERIAL_SKY);
			skybox_clear_changed_rects(work_rect, clip, skybox.sky_color);
			VIDEO_DIRECT_END();

			work_rect->top = rect_skybox.bottom;
			work_rect->bottom = SKYBOX_SCREEN_BOTTOM;
			VIDEO_MATERIAL_BEGIN(SKYBOX_MATERIAL_GROUND);
			skybox_clear_changed_rects(work_rect, clip, skybox.ground_color);
			VIDEO_DIRECT_END();
		}
		work_rect->top = rect_skybox.top;
		work_rect->bottom = rect_skybox.bottom;
	} else {
		work_rect->left = 0;
		work_rect->right = SKYBOX_SCREEN_WIDTH;
		work_rect->top = clip->top;
		work_rect->bottom = clip->bottom;
	}
	shape3d_retain_legacy_skybox_rect(work_rect);
}

static void skybox_render_horizon_strips(struct RECTANGLE *clip, struct RECTANGLE *work_rect,
										 legacy_s16 base_horizon, legacy_s16 horizon_delta,
										 legacy_s16 angle)
{
	legacy_s16 outside = rect_intersect(work_rect, clip);
	shape3d_retain_legacy_skybox_rect(work_rect);
	if (outside != 0) {
		return;
	}
	legacy_s16 absolute_delta = horizon_delta;
	if (absolute_delta < 0) {
		absolute_delta = LEGACY_S16_WRAP_NEGATE(absolute_delta);
	}
	legacy_s16 strip_count = LEGACY_S16_WRAP_ADD(absolute_delta, 1);
	if (strip_count > SKYBOX_MAX_HORIZON_STRIPS) {
		strip_count = SKYBOX_MAX_HORIZON_STRIPS;
	}
	legacy_s16 previous_x = 0;
	for (legacy_s16 strip = 0; strip < strip_count; strip++) {
		work_rect->left = previous_x;
		work_rect->right =
			(legacy_s16)(((legacy_s32)SKYBOX_SCREEN_WIDTH * strip + SKYBOX_SCREEN_WIDTH) /
						 strip_count) &
			video_x_alignment_mask;
		if (work_rect->left == work_rect->right) {
			continue;
		}
		legacy_s16 horizon = LEGACY_S16_WRAP_ADD(
			base_horizon, (legacy_s16)((legacy_s32)horizon_delta * strip / strip_count));
		skybox_render_level_rect(work_rect, angle, horizon);
		previous_x = work_rect->right;
	}
	shape3d_retain_legacy_skybox_rect(work_rect);
	return;
}

static void skybox_render_horizon_polygons(struct POINT2D *points)
{
	static const legacy_u16 corner_angles[SKYBOX_CORNER_COUNT] = {
		ANGLE_EIGHTH_TURN, ANGLE_EIGHTH_TURN + ANGLE_QUARTER_TURN,
		ANGLE_EIGHTH_TURN + ANGLE_HALF_TURN, ANGLE_EIGHTH_TURN + ANGLE_THREE_QUARTER_TURN};

	legacy_s16 angle_offset =
		(legacy_s16)polarAngle(LEGACY_S16_WRAP_SUB(points[0].px, points[1].px),
							   LEGACY_S16_WRAP_SUB(points[0].py, points[1].py)) &
		ANGLE_MASK;
	for (legacy_s16 point_index = 0; point_index < SKYBOX_CORNER_COUNT; point_index++) {
		legacy_s16 base_index = point_index < 2 ? 0 : 1;
		points[point_index + 2].px = LEGACY_S16_WRAP_ADD(
			points[base_index].px,
			multiply_and_scale(
				sin_fast(LEGACY_S16_WRAP_ADD(corner_angles[point_index], angle_offset)),
				SKYBOX_HORIZON_RADIUS));
		points[point_index + 2].py = LEGACY_S16_WRAP_ADD(
			points[base_index].py,
			multiply_and_scale(
				cos_fast(LEGACY_S16_WRAP_ADD(corner_angles[point_index], angle_offset)),
				SKYBOX_HORIZON_RADIUS));
	}
	/* Retain the original local points before arranging C call arguments or
	 * reusing their slots for the ground polygon. */
	shape3d_retain_legacy_skybox_points(points + 2);
	/* The original inline stack arguments order these vertices 0,1,3,2. */
	struct POINT2D point_swap = points[2];
	points[2] = points[3];
	points[3] = point_swap;
	VIDEO_MATERIAL_BEGIN(SKYBOX_MATERIAL_SKY);
	skybox_fill_polygon(skybox.sky_color, SKYBOX_POLYGON_POINT_COUNT, points);
	VIDEO_DIRECT_END();
	points[2] = points[4];
	points[3] = points[5];
	VIDEO_MATERIAL_BEGIN(SKYBOX_MATERIAL_GROUND);
	skybox_fill_polygon(skybox.ground_color, SKYBOX_POLYGON_POINT_COUNT, points);
	VIDEO_DIRECT_END();
}

static legacy_s16 skybox_render_rolled(struct RECTANGLE *clip, legacy_s16 direction,
									   struct MATRIX *rotation, legacy_s16 angle,
									   legacy_s16 camera_y)
{
	struct POINT2D points[SKYBOX_ROLL_POINT_COUNT];
	if (skybox_project_rolled_horizon(direction, rotation, camera_y, points) != 0 ||
		skybox_clear_outside_horizon(clip, points) != 0) {
		/* These paths fill the entire viewport. Retain that dirty region for
		 * presentation and the next frame, as in asmorig's loc_1CB72. */
		return 1;
	}
	legacy_s16 horizon_delta;
	struct RECTANGLE work_rect;
	legacy_s16 base_horizon;
	if (skybox_find_linear_horizon(points, &base_horizon, &horizon_delta) != 0) {
		skybox_prepare_horizon_rect(clip, base_horizon, horizon_delta, &work_rect);
		skybox_render_horizon_strips(clip, &work_rect, base_horizon, horizon_delta, angle);
		return 0;
	}
	skybox_render_horizon_polygons(points);
	return 1;
}

static legacy_s16 skybox_render_forward_horizon(legacy_s16 view_index, struct RECTANGLE *clip,
												legacy_s16 angle, legacy_s16 horizon)
{
	if (slow_video_mgmt_copy != 0) {
		rect_skybox.top = detail_level == SKYBOX_LOWEST_DETAIL_LEVEL
							  ? LEGACY_S16_WRAP_SUB(horizon, 1)
							  : LEGACY_S16_WRAP_SUB(horizon, skybox.maximum_height);
		rect_skybox.left = 0;
		rect_skybox.right = SKYBOX_SCREEN_WIDTH;
		rect_skybox.bottom = horizon;
		if (full_redraw_frames_remaining == 0) {
			for (legacy_s16 i = 0; i < SKYBOX_CHANGED_RECT_COUNT; i++) {
				frame_rect_change_flags[i] = SKYBOX_RECT_CHANGED;
			}
			legacy_s16 track_view_index = (legacy_s16)view_index;
			if (detail_level == SKYBOX_LOWEST_DETAIL_LEVEL) {
				frame_buffer_camera_headings[track_view_index] = last_rendered_camera_heading;
			}
			if (frame_buffer_camera_headings[track_view_index] == angle &&
				active_frame_rects[SKYBOX_RECT_INDEX].left == rect_skybox.left &&
				active_frame_rects[SKYBOX_RECT_INDEX].right == rect_skybox.right &&
				active_frame_rects[SKYBOX_RECT_INDEX].top == rect_skybox.top &&
				active_frame_rects[SKYBOX_RECT_INDEX].bottom == rect_skybox.bottom) {
				frame_rect_change_flags[SKYBOX_RECT_INDEX] = 0;
			} else {
				frame_rect_change_flags[SKYBOX_RECT_INDEX] = SKYBOX_RECT_FORCE_REDRAW;
			}
			skybox_collect_changed_rects(clip);
			for (legacy_s16 i = 0; i < (legacy_s8)redraw_rect_count; i++) {
				skybox_render_level_rect(&merged_redraw_rects[i], angle, horizon);
			}
			return 0;
		}
	}

	struct RECTANGLE work_rect;
	work_rect.left = 0;
	work_rect.right = SKYBOX_SCREEN_WIDTH;
	work_rect.top = clip->top;
	work_rect.bottom = clip->bottom;
	shape3d_retain_legacy_skybox_rect(&work_rect);
	skybox_render_level_rect(&work_rect, angle, horizon);
	return 0;
}

static legacy_s16 skybox_render_reversed_horizon(struct RECTANGLE *clip, legacy_s16 horizon)
{
	legacy_s16 fill_height = LEGACY_S16_WRAP_SUB(horizon, clip->top);
	if (LEGACY_S16_WRAP_SUB(clip->bottom, clip->top) < fill_height) {
		fill_height = LEGACY_S16_WRAP_SUB(clip->bottom, clip->top);
	}
	struct RECTANGLE work_rect;
	if (fill_height > 0) {
		work_rect.left = 0;
		work_rect.right = SKYBOX_SCREEN_WIDTH;
		work_rect.top = clip->top;
		work_rect.bottom = LEGACY_S16_WRAP_ADD(clip->top, fill_height);
		VIDEO_MATERIAL_BEGIN(SKYBOX_MATERIAL_GROUND);
		skybox_clear_rect(&work_rect, skybox.ground_color);
		VIDEO_DIRECT_END();
	}
	fill_height = LEGACY_S16_WRAP_SUB(clip->bottom, horizon);
	if (fill_height > 0) {
		work_rect.left = 0;
		work_rect.right = SKYBOX_SCREEN_WIDTH;
		work_rect.top = horizon;
		work_rect.bottom = LEGACY_S16_WRAP_ADD(horizon, fill_height);
		VIDEO_MATERIAL_BEGIN(SKYBOX_MATERIAL_SKY);
		skybox_clear_rect(&work_rect, skybox.sky_color);
		VIDEO_DIRECT_END();
	}
	return 1;
}

static legacy_s16 skybox_render_legacy(legacy_s16 view_index, struct RECTANGLE *clip,
									   legacy_s16 direction, struct MATRIX *rotation,
									   legacy_s16 roll, legacy_s16 angle, legacy_s16 camera_y)
{
	redraw_rect_count = 0;
	sprite_set_target_clip_bounds(0, SKYBOX_SCREEN_WIDTH, clip->top, clip->bottom);
	if (roll != 0) {
		return skybox_render_rolled(clip, direction, rotation, angle, camera_y);
	}
	struct VECTOR source;
	source.x = 0;
	source.y = LEGACY_S16_WRAP_NEGATE((legacy_s16)camera_y);
	source.z = skybox_scaled_constant(SKYBOX_ROLL_VECTOR_Z, (legacy_s16)direction);
	struct VECTOR vectors[SKYBOX_ROLL_VECTOR_COUNT];
	mat_mul_vector(&source, rotation, &vectors[0]);
	if (vectors[0].z < 0) {
		VIDEO_MATERIAL_BEGIN(SKYBOX_MATERIAL_SKY);
		sprite_clear_target((legacy_u8)skybox.sky_color);
		VIDEO_DIRECT_END();
		if (slow_video_mgmt_copy == 0) {
			return 0;
		}
		rect_skybox.left = 0;
		rect_skybox.right = SKYBOX_SCREEN_WIDTH;
		rect_skybox.top = clip->top;
		rect_skybox.bottom = clip->bottom;
		return 1;
	}

	struct POINT2D points[SKYBOX_ROLL_POINT_COUNT];
	vector_to_point(&vectors[0], &points[0]);
	legacy_s16 horizon = (legacy_s16)points[0].py;
	if (clip->top > horizon) {
		horizon = clip->top;
	}
	if (direction == SKYBOX_FORWARD_DIRECTION) {
		return skybox_render_forward_horizon(view_index, clip, angle, horizon);
	}
	return skybox_render_reversed_horizon(clip, horizon);
}

legacy_s16 skybox_render(legacy_s16 view_index, struct RECTANGLE *clip, legacy_s16 direction,
						 struct MATRIX *rotation, legacy_s16 roll, legacy_s16 angle,
						 legacy_s16 camera_y)
{
	legacy_s16 full_redraw =
		skybox_render_legacy(view_index, clip, direction, rotation, roll, angle, camera_y);
#ifdef RESTUNTS_SDL3
	if (skybox_hires_render_scene(clip, rotation, direction, angle, camera_y)) {
		return 1;
	}
#endif
	return full_redraw;
}
