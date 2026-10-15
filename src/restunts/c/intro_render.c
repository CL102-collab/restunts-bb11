#include "frame_internal.h"
#include "platform.h"
#ifdef RESTUNTS_AMIGA
#include "../platform/amiga/amiga.h"
#endif
#include "shape2d.h"
#include "shape3d.h"
#include "game_input.h"
#include "track_objects.h"
#include "video_frame.h"
#include "opponent.h"
#include "projection.h"
#include "externs.h"
#include "fileio.h"
#include "memmgr.h"
#include "keyboard.h"
#ifdef RESTUNTS_SDL3
#include "presentation.h"
#endif

#define TRACK_OBJECT_COUNT 215U
#define INTRO_SCREEN_WIDTH 320U
#define INTRO_SCREEN_HEIGHT 200U
#define INTRO_SCREEN_MAX_X 320
#define INTRO_SCREEN_MAX_Y 200
#define INTRO_SCREEN_COLOR 15U
#define INTRO_STAR_COUNT 100U
#define INTRO_POINT_BUFFER_COUNT 2
#define INTRO_POINT_BUFFER_MASK 1U
#define INTRO_TITLE_SHAPE_COUNT 3

#define TRANSFORMED_SHAPE_BASE_FLAG 4U
#define TRANSFORMED_SHAPE_RECT_FLAG 8U
#define INTRO_TRANSFORMED_SHAPE_SCALE 1024
#define INTRO_LOGO_WORLD_CENTER 1024
#define INTRO_STAR_MIN_DEPTH 200

#define INTRO_STAR_RANDOM_SCALE 128U
#define INTRO_STAR_XZ_OFFSET 16384
#define INTRO_STAR_Y_OFFSET 5000
#define INTRO_PROJECTION_SCALE_X 40
#define INTRO_PROJECTION_SCALE_Y 40

#define INTRO_INITIAL_CAMERA_Y 300
#define INTRO_LOGO_CAMERA_Y 90
#define INTRO_CAMERA_CAR_HEIGHT 20
#define INTRO_CAMERA_RISE_STEP 20
#define INTRO_CAMERA_RETREAT_STEP 5
#define INTRO_CAMERA_CENTER_STEP 10
#define INTRO_CAMERA_CENTER_SNAP_DISTANCE 10

#define INTRO_CAR_PHASE_SECONDS 6
#define INTRO_LOGO_PHASE_SECONDS 11
#define INTRO_TOTAL_SECONDS 23
#ifdef RESTUNTS_SDL3
#define INTRO_TIMER_TICK_NS (PRESENTATION_SECOND_NS / DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#endif

/*
 * In the original dseg, terrain_scene_objects immediately follows trkObjectList.
 * Some track objects store overlay indices into that combined legacy table,
 * so indices beyond trkObjectList intentionally address terrain_scene_objects.
 */
static legacy_s16 intro_shift_position(legacy_s32 position, legacy_s16 camera)
{
	return LEGACY_S16_WRAP_SUB(position_to_word(position), camera);
}

static void intro_draw_transformed_shape(struct TRANSFORMEDSHAPE3D *transformed,
										 struct RECTANGLE *shape_rect, legacy_s16 rotation_z)
{
	if (slow_video_mgmt_copy != 0) {
		transformed->rectptr = shape_rect;
		transformed->ts_flags = TRANSFORMED_SHAPE_BASE_FLAG | TRANSFORMED_SHAPE_RECT_FLAG;
	} else {
		transformed->ts_flags = TRANSFORMED_SHAPE_BASE_FLAG;
	}
	transformed->rotvec.x = 0;
	transformed->rotvec.y = 0;
	transformed->rotvec.z = rotation_z;
	transformed->culling_distance = INTRO_TRANSFORMED_SHAPE_SCALE;
	transformed->material = 0;
	shape3d_transform_and_queue(transformed);
}

static void intro_draw_stars(legacy_s16 camera_x, legacy_s16 camera_y, legacy_s16 camera_z,
							 struct VECTOR *stars, struct POINT2D *previous_points,
							 legacy_s16 *previous_point_count, struct RECTANGLE *point_rect)
{
	struct POINT2D point;
	legacy_u16 new_point_count = 0;
	struct VECTOR translated;
	struct VECTOR projected;
	for (legacy_u16 i = 0; i < INTRO_STAR_COUNT; i++) {
		translated.x = LEGACY_S16_WRAP_SUB(stars[i].x, camera_x);
		translated.y = LEGACY_S16_WRAP_SUB(stars[i].y, camera_y);
		translated.z = LEGACY_S16_WRAP_SUB(stars[i].z, camera_z);
		mat_mul_vector(&translated, &mat_temp, &projected);
		if (projected.z <= INTRO_STAR_MIN_DEPTH) {
			continue;
		}
		vector_to_point(&projected, &point);
		sprite_putpixel_clipped(point.px, point.py, intro_colorvalue);
		if (slow_video_mgmt_copy != 0) {
			previous_points[new_point_count] = point;
			new_point_count++;
			rect_adjust_from_point(&point, point_rect);
		}
		intro_colorvalue = LEGACY_S16_WRAP_ADD(intro_colorvalue, 1);
		if (intro_colorvalue == intro_palette_color_count) {
			intro_colorvalue = 1;
		}
	}
	if (slow_video_mgmt_copy != 0) {
		*previous_point_count = new_point_count;
	}
}

static void intro_render_scene_impl(legacy_s16 camera_x, legacy_s16 camera_y, legacy_s16 camera_z,
									legacy_s16 rotate_y, legacy_s16 rotate_x, legacy_s16 draw_car,
									legacy_s16 primary_logo, const struct VECTOR *car_position,
									legacy_s16 car_rotation, struct VECTOR *stars,
									struct POINT2D *previous_points,
									legacy_s16 *previous_point_count,
									struct RECTANGLE *previous_rect, struct RECTANGLE *shape_rect,
									struct RECTANGLE *combined_rect)
{
	struct RECTANGLE current_shape_rect = empty_rect;
	select_cliprect_rotate(0, rotate_x, rotate_y, &intro_cliprect, 0);
	struct TRANSFORMEDSHAPE3D transformed;
	transformed.shapeptr = primary_logo != 0 ? &logoshape : &logo2shape;
	transformed.pos.x = LEGACY_S16_WRAP_SUB(INTRO_LOGO_WORLD_CENTER, camera_x);
	transformed.pos.y = LEGACY_S16_WRAP_NEGATE(camera_y);
	transformed.pos.z = LEGACY_S16_WRAP_SUB(INTRO_LOGO_WORLD_CENTER, camera_z);
	intro_draw_transformed_shape(&transformed, &current_shape_rect, 0);

	if (draw_car != 0) {
		transformed.pos.x = LEGACY_S16_WRAP_SUB(car_position->x, camera_x);
		transformed.pos.y = LEGACY_S16_WRAP_SUB(car_position->y, camera_y);
		transformed.pos.z = LEGACY_S16_WRAP_SUB(car_position->z, camera_z);
		transformed.shapeptr = &bravshape;
		intro_draw_transformed_shape(&transformed, &current_shape_rect,
									 LEGACY_S16_WRAP_NEGATE(car_rotation));
	}

	struct RECTANGLE redraw_rect;
	struct RECTANGLE point_rect;
	if (slow_video_mgmt_copy != 0) {
		legacy_u16 old_point_count = (legacy_u16)*previous_point_count;
		for (legacy_u16 i = 0; i < old_point_count; i++) {
			sprite_putpixel_clipped(previous_points[i].px, previous_points[i].py, 0);
		}
		rect_union(shape_rect, previous_rect, &redraw_rect);
		if (rect_intersect(&redraw_rect, &intro_redraw_cliprect) == 0) {
			sprite_set_target_clip_bounds(redraw_rect.left, redraw_rect.right, redraw_rect.top,
										  redraw_rect.bottom);
			sprite_clear_target(0);
		}
		point_rect = current_shape_rect;
	} else {
		sprite_set_target_clip_bounds(intro_cliprect.left, intro_cliprect.right, intro_cliprect.top,
									  intro_cliprect.bottom);
		sprite_clear_target(0);
	}

	sprite_set_target_clip_bounds(intro_cliprect.left, intro_cliprect.right, intro_cliprect.top,
								  intro_cliprect.bottom);
	intro_draw_stars(camera_x, camera_y, camera_z, stars, previous_points, previous_point_count,
					 &point_rect);

	shape3d_render_queued_primitives();
	if (slow_video_mgmt_copy != 0) {
		*shape_rect = current_shape_rect;
		*combined_rect = point_rect;
	}
}

void intro_render_scene(legacy_s16 camera_x, legacy_s16 camera_y, legacy_s16 camera_z,
						legacy_s16 rotate_y, legacy_s16 rotate_x, legacy_s16 draw_car,
						legacy_s16 primary_logo, struct VECTOR *stars,
						struct POINT2D *previous_points, legacy_s16 *previous_point_count,
						struct RECTANGLE previous_rect, struct RECTANGLE *shape_rect,
						struct RECTANGLE *combined_rect)
{
	struct VECTOR car_position;
	car_position.x = intro_shift_position(state.opponentstate.car_position.lx, 0);
	car_position.y = intro_shift_position(state.opponentstate.car_position.ly, 0);
	car_position.z = intro_shift_position(state.opponentstate.car_position.lz, 0);
	intro_render_scene_impl(camera_x, camera_y, camera_z, rotate_y, rotate_x, draw_car,
							primary_logo, &car_position, state.opponentstate.car_rotate.x, stars,
							previous_points, previous_point_count, &previous_rect, shape_rect,
							combined_rect);
}

/* Creep one unit towards the wanted value, and stop once it is reached. */
static legacy_s16 intro_step_towards(legacy_s16 value, legacy_s16 target)
{
	if (value > target) {
		return LEGACY_S16_WRAP_SUB(value, 1);
	}
	if (value < target) {
		return LEGACY_S16_WRAP_ADD(value, 1);
	}
	return value;
}

struct INTRO_VIEW {
	struct VECTOR camera;
	struct VECTOR car_position;
	legacy_s16 car_rotation;
	legacy_s16 horizontal_angle;
	legacy_s16 vertical_angle;
	legacy_s16 draw_car;
	legacy_s16 primary_logo;
};

struct INTRO_SESSION {
	legacy_s8 far *title_resource;
	struct VECTOR stars[INTRO_STAR_COUNT];
	struct POINT2D point_buffers[INTRO_POINT_BUFFER_COUNT][INTRO_STAR_COUNT];
	legacy_s16 point_counts[INTRO_POINT_BUFFER_COUNT];
	struct RECTANGLE shape_rect;
	struct RECTANGLE combined_rect;
	legacy_s16 camera_x;
	legacy_s16 camera_y;
	legacy_s16 camera_z;
	legacy_s16 target_x;
	legacy_s16 target_y;
	legacy_s16 target_z;
	legacy_s16 frame_count;
	legacy_s16 logo_changed;
	legacy_s16 needs_render;
	legacy_u16 rect_index;
#ifdef RESTUNTS_SDL3
	struct PRESENTATION_CLOCK presentation_clock;
	struct INTRO_VIEW previous_view;
	struct INTRO_VIEW current_view;
	legacy_u64 view_time;
	legacy_u64 view_interval;
	legacy_s16 view_frame;
	legacy_u8 view_valid;
#endif
};

static void intro_request_full_redraw(struct INTRO_SESSION *intro)
{
	/* Refresh both pages after display changes, including an otherwise idle frame. */
	for (legacy_u16 i = 0; i < INTRO_POINT_BUFFER_COUNT; i++) {
		frame_layer_rects[i] = intro_redraw_cliprect;
	}
	frame_sorted_shapes_rect = intro_redraw_cliprect;
	intro->shape_rect = intro_redraw_cliprect;
	intro->combined_rect = intro_redraw_cliprect;
	intro->needs_render = 1;
#ifdef RESTUNTS_SDL3
	intro->view_valid = 0;
	presentation_reset(&intro->presentation_clock, presentation_now());
#endif
}

static void intro_load_title(struct INTRO_SESSION *intro)
{
	intro->title_resource = (legacy_s8 far *)file_load_3dres("title");
	legacy_s8 far *title_shapes[INTRO_TITLE_SHAPE_COUNT];
	locate_many_resources(intro->title_resource, "logolog2brav", title_shapes);
	shape3d_init_shape(title_shapes[0], &logoshape);
	shape3d_init_shape(title_shapes[1], &logo2shape);
	shape3d_init_shape(title_shapes[2], &bravshape);
	if (video_uses_page_flipping == 0) {
		render_window_sprite =
			sprite_make_wnd(INTRO_SCREEN_WIDTH, INTRO_SCREEN_HEIGHT, INTRO_SCREEN_COLOR);
	}
}

static void intro_create_stars(struct VECTOR *stars)
{
	for (legacy_u16 i = 0; i < INTRO_STAR_COUNT; i++) {
		stars[i].x = LEGACY_S16_WRAP_SUB(
			LEGACY_U16_WRAP_MUL(get_kevinrandom(), INTRO_STAR_RANDOM_SCALE), INTRO_STAR_XZ_OFFSET);
		stars[i].y = LEGACY_S16_WRAP_NEGATE(LEGACY_S16_WRAP_SUB(
			LEGACY_U16_WRAP_MUL(get_kevinrandom(), INTRO_STAR_RANDOM_SCALE), INTRO_STAR_Y_OFFSET));
		stars[i].z = LEGACY_S16_WRAP_SUB(
			LEGACY_U16_WRAP_MUL(get_kevinrandom(), INTRO_STAR_RANDOM_SCALE), INTRO_STAR_XZ_OFFSET);
	}
}

static void intro_prepare_session(struct INTRO_SESSION *intro)
{
	set_projection(INTRO_PROJECTION_SCALE_X, INTRO_PROJECTION_SCALE_Y, INTRO_SCREEN_MAX_X,
				   INTRO_SCREEN_MAX_Y);
	intro->camera_x = INTRO_LOGO_WORLD_CENTER;
	intro->camera_y = INTRO_INITIAL_CAMERA_Y;
	intro->camera_z = INTRO_LOGO_WORLD_CENTER;
	intro->logo_changed = 0;
	intro->frame_count = 0;
	void far *opponent_resource = file_load_resfile("carcoun");
	setup_aero_trackdata(opponent_resource, 1);
	unload_resource(opponent_resource);
	init_plantrak();
	(void)timer_get_delta();
	intro->point_counts[0] = 0;
	intro->point_counts[1] = 0;
	slow_video_mgmt_copy = slow_video_mgmt;
	frame_layer_rects[0].left = 0;
	frame_layer_rects[0].right = INTRO_SCREEN_MAX_X;
	frame_layer_rects[0].top = 0;
	frame_layer_rects[0].bottom = INTRO_SCREEN_MAX_Y;
	frame_unsorted_shapes_rect = frame_layer_rects[0];
	intro_redraw_cliprect = frame_layer_rects[0];
	intro->rect_index = 0;
	intro->needs_render = 1;
}

static void intro_advance_session(struct INTRO_SESSION *intro, legacy_s16 delta)
{
	intro_elapsed_ticks = LEGACY_S16_WRAP_ADD(intro_elapsed_ticks, delta);

	while ((legacy_s16)intro_elapsed_ticks > (legacy_s16)timer_ticks_per_frame) {
		intro_elapsed_ticks = LEGACY_S16_WRAP_SUB(intro_elapsed_ticks, timer_ticks_per_frame);
		update_opponent();
		intro->needs_render = 1;
		intro->frame_count = LEGACY_S16_WRAP_ADD(intro->frame_count, 1);
		legacy_s16 elapsed_limit = LEGACY_S16_WRAP_MUL(framespersec, INTRO_LOGO_PHASE_SECONDS);
		if (intro->frame_count > elapsed_limit) {
			intro->logo_changed = 1;
			intro->camera_y = LEGACY_S16_WRAP_ADD(intro->camera_y, INTRO_CAMERA_RISE_STEP);
			intro->camera_z = LEGACY_S16_WRAP_SUB(intro->camera_z, INTRO_CAMERA_RETREAT_STEP);
			legacy_s16 difference = LEGACY_S16_WRAP_SUB(intro->camera_x, INTRO_LOGO_WORLD_CENTER);
			legacy_s16 absolute_difference = absolute_word(difference);
			if (absolute_difference < INTRO_CAMERA_CENTER_SNAP_DISTANCE) {
				intro->camera_x = INTRO_LOGO_WORLD_CENTER;
			} else if (difference > 0) {
				intro->camera_x = LEGACY_S16_WRAP_SUB(intro->camera_x, INTRO_CAMERA_CENTER_STEP);
			} else if (difference < 0) {
				intro->camera_x = LEGACY_S16_WRAP_ADD(intro->camera_x, INTRO_CAMERA_CENTER_STEP);
			}

			intro->target_x = intro_step_towards(intro->target_x, INTRO_LOGO_WORLD_CENTER);
			intro->target_z = intro_step_towards(intro->target_z, INTRO_LOGO_WORLD_CENTER);
		}
	}
}

static void intro_aim_camera(struct INTRO_SESSION *intro, legacy_s16 *horizontal_angle,
							 legacy_s16 *vertical_angle, legacy_s16 *draw_car)
{
	(*draw_car) = 1;
	(*horizontal_angle) = -1;
	legacy_s16 opponent_x =
		intro_shift_position((legacy_s32)state.opponentstate.car_position.lx, 0);
	legacy_s16 opponent_y =
		intro_shift_position((legacy_s32)state.opponentstate.car_position.ly, 0);
	legacy_s16 opponent_z =
		intro_shift_position((legacy_s32)state.opponentstate.car_position.lz, 0);

	legacy_s16 elapsed_limit = LEGACY_S16_WRAP_MUL(framespersec, INTRO_CAR_PHASE_SECONDS);
	if (intro->frame_count < elapsed_limit) {
		(*draw_car) = 0;
		(*horizontal_angle) =
			LEGACY_S16_FROM_BITS((legacy_u16)state.opponentstate.car_rotate.x & ANGLE_MASK);
		(*vertical_angle) = 0;
		intro->camera_x = opponent_x;
		intro->camera_y = LEGACY_S16_WRAP_ADD(opponent_y, INTRO_CAMERA_CAR_HEIGHT);
		intro->camera_z = opponent_z;
	} else {
		elapsed_limit = LEGACY_S16_WRAP_MUL(framespersec, INTRO_LOGO_PHASE_SECONDS);
		if (intro->frame_count < elapsed_limit) {
			intro->camera_x = INTRO_LOGO_WORLD_CENTER;
			intro->camera_y = INTRO_LOGO_CAMERA_Y;
			intro->camera_z = INTRO_LOGO_WORLD_CENTER;
			intro->target_x = opponent_x;
			intro->target_y = opponent_y;
			intro->target_z = opponent_z;
		}
	}

	if ((*horizontal_angle) == -1) {
		(*horizontal_angle) =
			LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S16_WRAP_NEGATE(polarAngle(
									 LEGACY_S16_WRAP_SUB(intro->target_x, intro->camera_x),
									 LEGACY_S16_WRAP_SUB(intro->target_z, intro->camera_z))) &
								 ANGLE_MASK);
		legacy_s16 target_distance =
			(legacy_s16)polarRadius2D(LEGACY_S16_WRAP_SUB(intro->target_x, intro->camera_x),
									  LEGACY_S16_WRAP_SUB(intro->target_z, intro->camera_z));
		(*vertical_angle) = LEGACY_S16_FROM_BITS(
			(legacy_u16)polarAngle(LEGACY_S16_WRAP_SUB(intro->target_y, intro->camera_y),
								   target_distance) &
			ANGLE_MASK);
	}
}

static void intro_capture_view(struct INTRO_SESSION *intro, struct INTRO_VIEW *view)
{
	intro_aim_camera(intro, &view->horizontal_angle, &view->vertical_angle, &view->draw_car);
	view->camera.x = intro->camera_x;
	view->camera.y = intro->camera_y;
	view->camera.z = intro->camera_z;
	view->car_position.x = intro_shift_position(state.opponentstate.car_position.lx, 0);
	view->car_position.y = intro_shift_position(state.opponentstate.car_position.ly, 0);
	view->car_position.z = intro_shift_position(state.opponentstate.car_position.lz, 0);
	view->car_rotation = state.opponentstate.car_rotate.x;
	view->primary_logo = intro->logo_changed;
}

#ifdef RESTUNTS_SDL3
static legacy_s16 intro_predict_coordinate(legacy_s16 previous, legacy_s16 current,
										   legacy_u64 elapsed, legacy_u64 interval, legacy_u8 angle)
{
	legacy_s16 change = LEGACY_S16_WRAP_SUB(current, previous);
	if (angle != 0) {
		change =
			(legacy_s16)(((legacy_u16)change + ANGLE_HALF_TURN) & ANGLE_MASK) - ANGLE_HALF_TURN;
	}
	legacy_s32 offset =
		(legacy_s32)((legacy_s64)change * (legacy_s64)elapsed / (legacy_s64)interval);
	return LEGACY_S16_WRAP_ADD(current, offset);
}

static void intro_predict_view(const struct INTRO_SESSION *intro, legacy_u64 now,
							   struct INTRO_VIEW *view)
{
	*view = intro->current_view;
	if (intro->view_interval == 0) {
		return;
	}
	legacy_u64 elapsed = now - intro->view_time;
	legacy_u64 maximum = (legacy_u64)(legacy_u16)timer_ticks_per_frame * INTRO_TIMER_TICK_NS;
	if (elapsed > maximum) {
		elapsed = maximum;
	}
	const struct INTRO_VIEW *previous = &intro->previous_view;
	view->camera.x = intro_predict_coordinate(previous->camera.x, view->camera.x, elapsed,
											  intro->view_interval, 0);
	view->camera.y = intro_predict_coordinate(previous->camera.y, view->camera.y, elapsed,
											  intro->view_interval, 0);
	view->camera.z = intro_predict_coordinate(previous->camera.z, view->camera.z, elapsed,
											  intro->view_interval, 0);
	view->car_position.x = intro_predict_coordinate(previous->car_position.x, view->car_position.x,
													elapsed, intro->view_interval, 0);
	view->car_position.y = intro_predict_coordinate(previous->car_position.y, view->car_position.y,
													elapsed, intro->view_interval, 0);
	view->car_position.z = intro_predict_coordinate(previous->car_position.z, view->car_position.z,
													elapsed, intro->view_interval, 0);
	view->car_rotation = intro_predict_coordinate(previous->car_rotation, view->car_rotation,
												  elapsed, intro->view_interval, 1);
	view->horizontal_angle = intro_predict_coordinate(
		previous->horizontal_angle, view->horizontal_angle, elapsed, intro->view_interval, 1);
	view->vertical_angle = intro_predict_coordinate(previous->vertical_angle, view->vertical_angle,
													elapsed, intro->view_interval, 1);
}

static void intro_update_view(struct INTRO_SESSION *intro, legacy_u64 now)
{
	struct INTRO_VIEW current;
	intro_capture_view(intro, &current);
	/* A scene cut has no continuous motion to predict. The real animation and
	 * car state advance only in intro_advance_session at their original rate. */
	intro->view_interval = 0;
	if (intro->view_valid != 0 && current.draw_car == intro->current_view.draw_car &&
		current.primary_logo == intro->current_view.primary_logo &&
		intro->frame_count > intro->view_frame) {
		intro->previous_view = intro->current_view;
		intro->view_interval = (legacy_u64)(intro->frame_count - intro->view_frame) *
							   (legacy_u16)timer_ticks_per_frame * INTRO_TIMER_TICK_NS;
	}
	intro->current_view = current;
	intro->view_valid = 1;
	intro->view_frame = intro->frame_count;
	intro->view_time = now;
	intro->needs_render = 0;
}
#endif

static void intro_present_session(struct INTRO_SESSION *intro)
{
	struct RECTANGLE redraw_rect;

	if (video_uses_page_flipping != 0) {
		mouse_draw_opaque_check();
		sprite_present_mcga_backbuffer();
		mouse_draw_transparent_check();
		if (slow_video_mgmt_copy != 0) {
			frame_layer_rects[intro->rect_index] = intro->shape_rect;
		}
		intro->rect_index ^= INTRO_POINT_BUFFER_MASK;
	} else {
		sprite_select_screen_compat();
		if (slow_video_mgmt_copy != 0) {
			rect_union(&intro->combined_rect, &frame_sorted_shapes_rect, &redraw_rect);
			if (rect_intersect(&redraw_rect, &intro_redraw_cliprect) == 0) {
				sprite_set_target_clip_bounds(redraw_rect.left, redraw_rect.right, redraw_rect.top,
											  redraw_rect.bottom);
				mouse_draw_opaque_check();
				sprite_putimage(render_window_sprite->sprite_bitmapptr);
				mouse_draw_transparent_check();
				frame_layer_rects[0] = intro->shape_rect;
				frame_sorted_shapes_rect = intro->combined_rect;
			}
		} else {
			mouse_draw_opaque_check();
			sprite_putimage(render_window_sprite->sprite_bitmapptr);
			mouse_draw_transparent_check();
		}
	}
}

static void intro_render_session(struct INTRO_SESSION *intro, const struct INTRO_VIEW *view)
{
#ifdef RESTUNTS_SDL3
	sdl3_video_begin_frame();
#endif
	intro->needs_render = 0;
	if (video_uses_page_flipping != 0) {
		sprite_select_mcga_backbuffer();
	} else {
		sprite_select_render_window();
	}
	struct POINT2D *active_points = intro->point_buffers[intro->rect_index];
	legacy_s16 *active_point_count = &intro->point_counts[intro->rect_index];
	intro_render_scene_impl(view->camera.x, view->camera.y, view->camera.z, view->horizontal_angle,
							view->vertical_angle, view->draw_car, view->primary_logo,
							&view->car_position, view->car_rotation, intro->stars, active_points,
							active_point_count, &frame_layer_rects[intro->rect_index],
							&intro->shape_rect, &intro->combined_rect);

	if (frame_display_overlay_active() != 0) {
		struct RECTANGLE *fps_rect = frame_fps_draw_text();
		if (slow_video_mgmt_copy != 0) {
			/* Erase the previous digits and include the counter in the screen copy. */
			rect_union(&intro->shape_rect, fps_rect, &intro->shape_rect);
			rect_union(&intro->combined_rect, fps_rect, &intro->combined_rect);
		}
	}
	intro_present_session(intro);
	frame_fps_record_presented();
#ifdef RESTUNTS_SDL3
	sdl3_video_end_frame();
#endif
}

static void intro_finish_session(struct INTRO_SESSION *intro)
{
	if (video_uses_page_flipping != 0) {
		if (video_backbuffer_copy_required() != 0) {
			sprite_select_mcga_backbuffer();
			sprite_copy_rect_shifted(0, 0, INTRO_SCREEN_MAX_X, INTRO_SCREEN_MAX_Y, 0);
			mouse_draw_opaque_check();
			sprite_present_mcga_backbuffer();
			mouse_draw_transparent_check();
		}
	} else {
		sprite_free_wnd(render_window_sprite);
	}
	mmgr_free(intro->title_resource);
}

legacy_s8 setup_intro(void)
{
#ifdef RESTUNTS_AMIGA
	amiga_video_palette_scene("SDTITL", "!def");
#endif
	struct INTRO_SESSION intro;
	intro_load_title(&intro);
	intro_create_stars(intro.stars);
	intro_prepare_session(&intro);
	frame_fps_reset();
	intro_request_full_redraw(&intro);
#ifdef RESTUNTS_SDL3
	legacy_u64 input_time = presentation_now();
	legacy_s16 input_delta = 0;
#endif
	legacy_s8 interrupted = 0;
	for (;;) {
		legacy_s16 delta = LEGACY_S16_FROM_BITS((legacy_u16)timer_get_delta());
		intro_advance_session(&intro, delta);
#ifdef RESTUNTS_SDL3
		legacy_u64 now = presentation_now();
		legacy_s16 keyframe = intro.needs_render;
		if (keyframe != 0) {
			intro_update_view(&intro, now);
		}
		if (supersight_enabled != 0) {
			if (presentation_due(&intro.presentation_clock, now)) {
				struct INTRO_VIEW view;
				intro_predict_view(&intro, now, &view);
				intro_render_session(&intro, &view);
			}
		} else if (keyframe != 0) {
			intro_render_session(&intro, &intro.current_view);
		}
#else
		if (intro.needs_render != 0) {
			struct INTRO_VIEW view;
			intro_capture_view(&intro, &view);
			intro_render_session(&intro, &view);
		}
#endif
		legacy_s16 key = 0;
#ifdef RESTUNTS_SDL3
		legacy_u64 input_now = presentation_now();
		if (supersight_enabled != 0) {
			input_delta = LEGACY_S16_WRAP_ADD(input_delta, delta);
			/* Presentation-only loops cannot sample devices between timer ticks. */
			if (input_now - input_time >= INTRO_TIMER_TICK_NS) {
				key = input_do_checking(input_delta);
				input_delta = 0;
				input_time = input_now;
			}
		} else {
			key = input_do_checking(delta);
			input_delta = 0;
			input_time = input_now;
		}
#else
		key = input_do_checking(delta);
#endif
		if (key == KEY_F11 || key == KEY_F12
#ifdef RESTUNTS_SDL3
			|| key == KEY_SHIFT_F12
#endif
		) {
			handle_ingame_kb_shortcuts(key);
			intro_request_full_redraw(&intro);
			key = 0;
		}
		if (key != 0) {
			interrupted = 1;
			break;
		}
		legacy_s16 elapsed_limit = LEGACY_S16_WRAP_MUL(INTRO_TOTAL_SECONDS, framespersec);
		if (intro.frame_count >= elapsed_limit) {
			break;
		}
	}
	intro_finish_session(&intro);
	frame_fps_reset();
	return interrupted;
}
