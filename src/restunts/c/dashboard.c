#include "dashboard.h"
#include "fileio.h"
#include "memmgr.h"
#include "platform.h"
#include "shape2d.h"
#include "game_input.h"
#include "externs.h"
#include "shape3d.h"

#define DASHBOARD_STEERING_SCALE_SHIFT 3U
#define DASHBOARD_VIEWPORT_WIDTH 320
#define DASHBOARD_SPRITE_WINDOW_LEGACY_ARGUMENT 15U
#define DASHBOARD_ANALOG_SPEED_DIVISOR 640U
#define DASHBOARD_CAR_ID_LENGTH 4U
#define DASHBOARD_CAR_ID_RESOURCE_OFFSET 4U

enum DASHBOARD_PRIMARY_SHAPE_INDEX {
	DASHBOARD_WHEEL_LEFT_SHAPE = 0,
	DASHBOARD_WHEEL_CENTER_SHAPE = 1,
	DASHBOARD_WHEEL_RIGHT_SHAPE = 2,
	DASHBOARD_INSTRUMENT_PANEL_SHAPE = 3,
	DASHBOARD_GEARBOX_SHAPE = 4,
	DASHBOARD_LEFT_INSTRUMENT_SOURCE_SHAPE = 5,
	DASHBOARD_RIGHT_INSTRUMENT_SOURCE_SHAPE = 6,
	DASHBOARD_LEFT_INSTRUMENT_MASK_SHAPE = 7,
	DASHBOARD_RIGHT_INSTRUMENT_MASK_SHAPE = 8
};

enum DASHBOARD_SUBRESOURCE_SHAPE_INDEX {
	DASHBOARD_GEAR_KNOB_SHAPE = 0,
	DASHBOARD_GEAR_KNOB_BACKGROUND_SHAPE = 1,
	DASHBOARD_STEERING_DOT_SHAPE = 2,
	DASHBOARD_STEERING_DOT_BACKGROUND_SHAPE = 3,
	DASHBOARD_STEERING_DOT_BUFFER_FIRST_SHAPE = 4
};

#define DASHBOARD_CACHE_VALUE_INVALID (-1)
#define DASHBOARD_STEERING_LEFT_LIMIT (-10)
#define DASHBOARD_STEERING_RIGHT_LIMIT 10

enum DASHBOARD_WHEEL_STATE {
	DASHBOARD_WHEEL_STATE_LEFT = DASHBOARD_WHEEL_LEFT_SHAPE,
	DASHBOARD_WHEEL_STATE_CENTER = DASHBOARD_WHEEL_CENTER_SHAPE,
	DASHBOARD_WHEEL_STATE_RIGHT = DASHBOARD_WHEEL_RIGHT_SHAPE
};

enum DASHBOARD_GAUGE_MODE {
	DASHBOARD_GAUGE_ANALOG = 0,
	DASHBOARD_GAUGE_DIGITAL = 1,
	DASHBOARD_GAUGE_HIDDEN = 2
};

#define DASHBOARD_GAUGE_HIDDEN_CENTER_Y (-1)
#define DASHBOARD_GAUGE_DIGITAL_CENTER_Y 0
#define DASHBOARD_DIGITAL_SPEED_SHIFT 8U
#define DASHBOARD_RPM_INDEX_SHIFT 7U
#define DASHBOARD_POINT_COORDINATE_STRIDE 2U
#define DASHBOARD_POINT_Y_OFFSET 1U
#define DASHBOARD_STEERING_MIRROR_SHIFT 1U

#define DASHBOARD_DECIMAL_BASE 10U
#define DASHBOARD_ONE_HUNDRED 100U
#define DASHBOARD_TWO_HUNDRED 200U
#define DASHBOARD_ONE_HUNDRED_DIGIT 1U
#define DASHBOARD_TWO_HUNDRED_DIGIT 2U

enum DASHBOARD_SPEED_COORDINATE_INDEX {
	DASHBOARD_SPEED_HUNDREDS_X = 0,
	DASHBOARD_SPEED_HUNDREDS_Y = 1,
	DASHBOARD_SPEED_TENS_X = 2,
	DASHBOARD_SPEED_TENS_Y = 3,
	DASHBOARD_SPEED_UNITS_X = 4,
	DASHBOARD_SPEED_UNITS_Y = 5
};

#define DASHBOARD_TRANSFORM_DESCRIPTOR_WIDTH 1U
#define DASHBOARD_TRANSFORM_DESCRIPTOR_HEIGHT 1U
#define DASHBOARD_WHEEL_STATE_COUNT 3U

static struct SHAPE2D far *dashboard_instrument_transforms[DASHBOARD_WHEEL_STATE_COUNT];
static legacy_u8 dashboard_instruments_follow_wheel;

static void dashboard_load_instrument_transforms(void)
{
	static const legacy_s8 *const transform_ids[DASHBOARD_WHEEL_STATE_COUNT] = {"imt1", 0, "imt3"};
	dashboard_instruments_follow_wheel = 0;
	for (legacy_u16 index = 0; index < DASHBOARD_WHEEL_STATE_COUNT; index++) {
		dashboard_instrument_transforms[index] = 0;
		if (transform_ids[index] == 0) {
			continue;
		}
		struct SHAPE2D far *shape =
			(struct SHAPE2D far *)locate_shape_nofatal(stdaresptr, transform_ids[index]);
		if (shape == 0 || shape2d_get_width(shape) != DASHBOARD_TRANSFORM_DESCRIPTOR_WIDTH ||
			shape2d_get_height(shape) != DASHBOARD_TRANSFORM_DESCRIPTOR_HEIGHT) {
			continue;
		}
		legacy_s16 xx = LEGACY_S16_FROM_BITS(shape2d_get_anchor_x(shape));
		legacy_s16 xy = LEGACY_S16_FROM_BITS(shape2d_get_anchor_y(shape));
		legacy_s16 yx = LEGACY_S16_FROM_BITS(shape2d_get_pos_x(shape));
		legacy_s16 yy = LEGACY_S16_FROM_BITS(shape2d_get_pos_y(shape));
		/* Compare the products without subtracting: even malformed coefficients
		 * cannot overflow while checking for a singular inverse matrix. */
		if ((legacy_s32)xx * yy == (legacy_s32)xy * yx) {
			continue;
		}
		dashboard_instrument_transforms[index] = shape;
		dashboard_instruments_follow_wheel = 1;
	}
}

static legacy_s16 dashboard_steering_position(legacy_s16 angle)
{
	legacy_s16 magnitude = angle < 0 ? LEGACY_S16_WRAP_NEGATE(angle) : angle;
	legacy_u16 bits = (legacy_u16)magnitude;
	bits = LEGACY_U16_SAR(bits, DASHBOARD_STEERING_SCALE_SHIFT);
	magnitude = LEGACY_S16_FROM_BITS(bits);
	return angle < 0 ? LEGACY_S16_WRAP_NEGATE(magnitude) : magnitude;
}

static legacy_u8 dashboard_clear_steering_dot(legacy_u16 buffer_index)
{
	if (dashboard_steering_dot_y_cache[buffer_index] == 0) {
		return 0;
	}
	sprite_copy_image_at(
		gnobshapes[DASHBOARD_STEERING_DOT_BUFFER_FIRST_SHAPE + (legacy_u8)frame_buffer_index],
		dashboard_steering_dot_x_cache[buffer_index], dashboard_steering_dot_y_cache[buffer_index]);
	dashboard_steering_dot_y_cache[buffer_index] = 0;
	return 1;
}

static void dashboard_set_viewport(void)
{
	sprite_set_target_clip_bounds(0, DASHBOARD_VIEWPORT_WIDTH, 0, height_above_replaybar);
}

static void dashboard_load_resources(void)
{
	for (legacy_u16 index = 0; index < DASHBOARD_CAR_ID_LENGTH; index++) {
		dashboard_primary_resource_name[index + DASHBOARD_CAR_ID_RESOURCE_OFFSET] =
			gameconfig.game_playercarid[index];
		dashboard_secondary_resource_name[index + DASHBOARD_CAR_ID_RESOURCE_OFFSET] =
			gameconfig.game_playercarid[index];
	}
	stdaresptr = (legacy_s8 far *)file_load_resource(FILE_RESOURCE_SHAPE2D_COLLECTION,
													 dashboard_primary_resource_name);
	stdbresptr = (legacy_s8 far *)file_load_resource(FILE_RESOURCE_SHAPE2D,
													 dashboard_secondary_resource_name);
	locate_many_resources(stdaresptr, dashboard_wheel_and_instrument_ids,
						  (legacy_s8 far **)whlshapes);
	locate_many_resources(stdbresptr, dashboard_gear_and_dot_shape_ids,
						  (legacy_s8 far **)gnobshapes);
	if (simd_player.spdcenter.py == 0) {
		locate_many_resources(stdbresptr, dashboard_digit_shape_ids, (legacy_s8 far **)digshapes);
	}
}

static legacy_u16 dashboard_panel_width(struct SHAPE2D far *shape)
{
	return LEGACY_U16_WRAP_MUL(shape2d_get_width(shape), (legacy_u16)video_shape_width_scale);
}

static void dashboard_create_sprites(void)
{
	struct SHAPE2D far *instrument_shape = whlshapes[DASHBOARD_INSTRUMENT_PANEL_SHAPE];
	struct SHAPE2D far *gearbox_shape = whlshapes[DASHBOARD_GEARBOX_SHAPE];
	legacy_u16 instrument_width = dashboard_panel_width(instrument_shape);
	legacy_u16 instrument_height = shape2d_get_height(instrument_shape);
	legacy_u16 gearbox_width = dashboard_panel_width(gearbox_shape);
	legacy_u16 gearbox_height = shape2d_get_height(gearbox_shape);
	legacy_u16 scratch_width = instrument_width;
	legacy_u16 scratch_height = instrument_height;
	if (gearbox_width >= instrument_width && gearbox_height >= instrument_height) {
		scratch_width = gearbox_width;
		scratch_height = gearbox_height;
	}
	dashboard_instrument_sprite =
		sprite_make_wnd(scratch_width, scratch_height, DASHBOARD_SPRITE_WINDOW_LEGACY_ARGUMENT);
	/* Both panels are rebuilt before use. Share scratch storage when one fits
	 * inside the other, without increasing its pitch or row-table allocation. */
	if (scratch_width >= gearbox_width && scratch_height >= gearbox_height) {
		dashboard_gearbox_sprite = dashboard_instrument_sprite;
	} else {
		dashboard_gearbox_sprite =
			sprite_make_wnd(gearbox_width, gearbox_height, DASHBOARD_SPRITE_WINDOW_LEGACY_ARGUMENT);
	}

	struct SHAPE2D far *dashboard_shape =
		(struct SHAPE2D far *)locate_shape_fatal(stdaresptr, dashboard_background_shape_id);
	sprite_select_screen();
	dashbmp_y = shape2d_get_pos_y(dashboard_shape);
}

static void dashboard_select_panel_target(struct SPRITE far *sprite, struct SHAPE2D far *shape)
{
	sprite_select_target(sprite);
	sprite_set_target_clip_bounds(0, dashboard_panel_width(shape), 0, shape2d_get_height(shape));
}

static void dashboard_copy_panel(struct SPRITE far *sprite, struct SHAPE2D far *shape)
{
	legacy_s16 x = (legacy_s16)shape2d_get_pos_x(shape);
	legacy_s16 y = (legacy_s16)shape2d_get_pos_y(shape);
	legacy_s16 left = x < 0 ? 0 : x;
	legacy_s16 top = y < 0 ? 0 : y;
	legacy_s32 right = (legacy_s32)x + dashboard_panel_width(shape);
	legacy_s32 bottom = (legacy_s32)y + shape2d_get_height(shape);
	if (right > DASHBOARD_VIEWPORT_WIDTH) {
		right = DASHBOARD_VIEWPORT_WIDTH;
	}
	if (bottom > height_above_replaybar) {
		bottom = height_above_replaybar;
	}
	if (left < right && top < bottom) {
		/* Keep the shared bitmap's physical stride, but copy only this panel. */
		sprite_set_target_clip_bounds(left, (legacy_u16)right, top, (legacy_u16)bottom);
		sprite_copy_image_at(sprite->sprite_bitmapptr, x, y);
	}
	dashboard_set_viewport();
}

static void dashboard_load_optional_shapes(void)
{
	struct SHAPE2D far *shape =
		(struct SHAPE2D far *)locate_shape_nofatal(stdaresptr, dashboard_roof_shape_id);
	if (shape != 0) {
		shape = (struct SHAPE2D far *)locate_shape_fatal(stdaresptr, dashboard_roof_shape_id);
		roofbmpheight = shape2d_get_height(shape);
	} else {
		roofbmpheight = 0;
	}

	shape = (struct SHAPE2D far *)locate_shape_nofatal(stdaresptr, dashboard_top_shape_id);
	if (shape != 0) {
		dastbmp_y = shape2d_get_pos_y(shape);
		dastbmp_y2 = dos_memory_pointer_offset(shape);
		dastseg = dos_memory_pointer_segment(shape);
		dasmshapeptr = locate_shape_fatal(stdaresptr, dashboard_mask_shape_id);
	} else {
		dastbmp_y = 0;
	}
	return;
}

static void dashboard_load(void)
{
	dashboard_load_resources();
	dashboard_load_instrument_transforms();
	dashboard_create_sprites();
	dashboard_load_optional_shapes();
}

static void dashboard_redraw_static(void)
{
	mouse_draw_opaque_check();
	struct SHAPE2D far *shape =
		(struct SHAPE2D far *)locate_shape_nofatal(stdaresptr, dashboard_roof_shape_id);
	if (shape != 0) {
		shape2d_rle_copy_at_position(
			(struct SHAPE2D far *)locate_shape_fatal(stdaresptr, dashboard_roof_shape_id));
	}
	shape2d_rle_copy_position_clipped(
		(struct SHAPE2D far *)locate_shape_fatal(stdaresptr, dashboard_background_shape_id));
	shape2d_rle_copy_position_clipped(whlshapes[DASHBOARD_WHEEL_CENTER_SHAPE]);
	mouse_draw_transparent_check();

	legacy_u16 buffer_index = (legacy_u8)dashboard_buffer_index;
	replay_controls_drawn[buffer_index] = 0;
	dashboard_gear_knob_visible_cache[buffer_index] = 0;
	dashboard_steering_dot_y_cache[buffer_index] = 0;
	dashboard_wheel_shape_cache[buffer_index] = 0;
	dashboard_steering_position_cache[buffer_index] = DASHBOARD_CACHE_VALUE_INVALID;
	dashboard_speed_index_cache[buffer_index] = DASHBOARD_CACHE_VALUE_INVALID;
	dashboard_rpm_index_cache[buffer_index] = DASHBOARD_CACHE_VALUE_INVALID;
	return;
}

static void dashboard_unload(void)
{
	dashboard_instruments_follow_wheel = 0;
	for (legacy_u16 index = 0; index < DASHBOARD_WHEEL_STATE_COUNT; index++) {
		dashboard_instrument_transforms[index] = 0;
	}
	if (dashboard_gearbox_sprite != dashboard_instrument_sprite) {
		sprite_free_wnd(dashboard_gearbox_sprite);
	}
	sprite_free_wnd(dashboard_instrument_sprite);
	mmgr_free(stdbresptr);
	mmgr_free(stdaresptr);
	return;
}

struct DASHBOARD_UPDATE {
	legacy_u16 buffer_index;
	legacy_u16 speed_index;
	legacy_u16 rpm_index;
	legacy_s16 steering_position;
	legacy_u8 wheel_state;
	legacy_u8 wheel_redrawn;
	legacy_u8 steering_dot_cleared;
	legacy_u8 gauge_mode;
};

static void dashboard_rebuild_gearbox_background(void)
{
	struct SPRITE saved_context[SPRITE_STATE_COUNT];
	sprite_save_context(saved_context);
	dashboard_select_panel_target(dashboard_gearbox_sprite, whlshapes[DASHBOARD_GEARBOX_SHAPE]);
	/* A custom gearbox may extend beyond the dashboard image. Do not retain
	 * the previous knob in any part the clipped background does not cover. */
	sprite_clear_target(0);
	struct SHAPE2D far *dashboard_shape =
		(struct SHAPE2D far *)locate_shape_fatal(stdaresptr, dashboard_background_shape_id);
	struct SHAPE2D far *gearbox_shape = whlshapes[DASHBOARD_GEARBOX_SHAPE];
	shape2d_rle_copy_clipped(dashboard_shape,
							 LEGACY_S16_WRAP_SUB((legacy_s16)shape2d_get_pos_x(dashboard_shape),
												 (legacy_s16)shape2d_get_pos_x(gearbox_shape)),
							 LEGACY_S16_WRAP_SUB((legacy_s16)shape2d_get_pos_y(dashboard_shape),
												 (legacy_s16)shape2d_get_pos_y(gearbox_shape)));
	sprite_restore_context(saved_context);
}

static void dashboard_update_gear(legacy_u16 buffer_index)
{
	if (state.playerstate.car_gear_change_delay == GEAR_CHANGE_DELAY_EXPIRED &&
		state.playerstate.car_changing_gear == CAR_GEAR_CHANGE_INACTIVE &&
		dashboard_gear_knob_visible_cache[buffer_index] != 0) {
		if (video_uses_page_flipping == 0) {
			mouse_draw_opaque_check();
		}
		dashboard_rebuild_gearbox_background();
		dashboard_copy_panel(dashboard_gearbox_sprite, whlshapes[DASHBOARD_GEARBOX_SHAPE]);
		dashboard_gear_knob_visible_cache[buffer_index] = 0;
	} else if (dashboard_gear_knob_visible_cache[buffer_index] !=
				   (legacy_u8)state.playerstate.car_changing_gear ||
			   dashboard_gear_knob_x_cache[buffer_index] != state.playerstate.car_knob_x ||
			   dashboard_gear_knob_y_cache[buffer_index] != state.playerstate.car_knob_y ||
			   (state.playerstate.car_gear_change_delay != GEAR_CHANGE_DELAY_EXPIRED &&
				dashboard_gear_knob_visible_cache[buffer_index] == 0)) {
		dashboard_select_panel_target(dashboard_gearbox_sprite, whlshapes[DASHBOARD_GEARBOX_SHAPE]);
		dashboard_gear_knob_visible_cache[buffer_index] = 1;
		shape2d_rle_copy_clipped(whlshapes[DASHBOARD_GEARBOX_SHAPE], 0, 0);
		dashboard_gear_knob_x_cache[buffer_index] = state.playerstate.car_knob_x;
		dashboard_gear_knob_y_cache[buffer_index] = state.playerstate.car_knob_y;
		sprite_and_image_at_anchor(gnobshapes[DASHBOARD_GEAR_KNOB_BACKGROUND_SHAPE],
								   state.playerstate.car_knob_x, state.playerstate.car_knob_y);
		sprite_or_image_at_anchor(gnobshapes[DASHBOARD_GEAR_KNOB_SHAPE],
								  state.playerstate.car_knob_x, state.playerstate.car_knob_y);
		if (video_uses_page_flipping != 0) {
			sprite_select_mcga_backbuffer();
		} else {
			sprite_select_screen_compat();
			mouse_draw_opaque_check();
		}
		dashboard_copy_panel(dashboard_gearbox_sprite, whlshapes[DASHBOARD_GEARBOX_SHAPE]);
	}
}

static void dashboard_update_wheel(struct DASHBOARD_UPDATE *update)
{
	update->steering_position = dashboard_steering_position(state.playerstate.car_steeringAngle);
	update->wheel_state = DASHBOARD_WHEEL_STATE_CENTER;
	if (update->steering_position < DASHBOARD_STEERING_LEFT_LIMIT) {
		update->wheel_state = DASHBOARD_WHEEL_STATE_LEFT;
	} else if (update->steering_position > DASHBOARD_STEERING_RIGHT_LIMIT) {
		update->wheel_state = DASHBOARD_WHEEL_STATE_RIGHT;
	}
	if (dashboard_wheel_shape_cache[update->buffer_index] != update->wheel_state ||
		full_redraw_frames_remaining != 0) {
		if (video_uses_page_flipping == 0) {
			mouse_draw_opaque_check();
		}
		update->steering_dot_cleared = dashboard_clear_steering_dot(update->buffer_index);
		shape2d_rle_copy_position_clipped(whlshapes[update->wheel_state]);
		dashboard_wheel_shape_cache[update->buffer_index] = update->wheel_state;
		update->wheel_redrawn = 1;
	} else {
		update->wheel_redrawn = 0;
	}
}

static void dashboard_select_gauges(struct DASHBOARD_UPDATE *update)
{
	if (simd_player.spdcenter.py == DASHBOARD_GAUGE_HIDDEN_CENTER_Y) {
		update->speed_index = 0;
		update->gauge_mode = DASHBOARD_GAUGE_HIDDEN;
	} else if (simd_player.spdcenter.py == DASHBOARD_GAUGE_DIGITAL_CENTER_Y) {
		update->speed_index =
			(legacy_u16)state.playerstate.car_rev_speed >> DASHBOARD_DIGITAL_SPEED_SHIFT;
		update->gauge_mode = DASHBOARD_GAUGE_DIGITAL;
	} else {
		update->speed_index =
			LEGACY_U16_DIV_OR_ZERO(state.playerstate.car_rev_speed, DASHBOARD_ANALOG_SPEED_DIVISOR);
		if ((legacy_s16)update->speed_index >= simd_player.spdnumpoints) {
			update->speed_index = (legacy_u16)(simd_player.spdnumpoints - 1);
		}
		update->gauge_mode = DASHBOARD_GAUGE_ANALOG;
	}
	update->rpm_index = (legacy_u16)state.playerstate.car_currpm >> DASHBOARD_RPM_INDEX_SHIFT;
	if ((legacy_s16)update->rpm_index >= simd_player.revnumpoints) {
		update->rpm_index = (legacy_u16)(simd_player.revnumpoints - 1);
	}
}

static void dashboard_draw_digital_speed(legacy_u16 speed_index)
{
	legacy_u16 digit_group = 0;
	if (speed_index >= DASHBOARD_TWO_HUNDRED) {
		digit_group = DASHBOARD_TWO_HUNDRED_DIGIT;
		speed_index -= DASHBOARD_TWO_HUNDRED;
	} else if (speed_index >= DASHBOARD_ONE_HUNDRED) {
		digit_group = DASHBOARD_ONE_HUNDRED_DIGIT;
		speed_index -= DASHBOARD_ONE_HUNDRED;
	}
	legacy_u8 digit_started = 0;
	if (digit_group != 0) {
		sprite_putimage_or(digshapes[digit_group],
						   (legacy_u8)simd_player.spdpoints[DASHBOARD_SPEED_HUNDREDS_X],
						   (legacy_u8)simd_player.spdpoints[DASHBOARD_SPEED_HUNDREDS_Y]);
		digit_started = 1;
	}
	legacy_u16 digit = LEGACY_U16_DIV_OR_ZERO(speed_index, DASHBOARD_DECIMAL_BASE);
	if (digit != 0 || digit_started != 0) {
		sprite_putimage_or(digshapes[digit],
						   (legacy_u8)simd_player.spdpoints[DASHBOARD_SPEED_TENS_X],
						   (legacy_u8)simd_player.spdpoints[DASHBOARD_SPEED_TENS_Y]);
		speed_index -= digit * DASHBOARD_DECIMAL_BASE;
	}
	sprite_putimage_or(digshapes[speed_index],
					   (legacy_u8)simd_player.spdpoints[DASHBOARD_SPEED_UNITS_X],
					   (legacy_u8)simd_player.spdpoints[DASHBOARD_SPEED_UNITS_Y]);
}

static void dashboard_draw_wheel_mask(legacy_u8 wheel_state)
{
	if (wheel_state == DASHBOARD_WHEEL_STATE_LEFT) {
		shape2d_render_bmp_as_mask(whlshapes[DASHBOARD_LEFT_INSTRUMENT_MASK_SHAPE]);
		shape2d_rle_or_far_pointer(
			dos_memory_pointer_offset(whlshapes[DASHBOARD_LEFT_INSTRUMENT_SOURCE_SHAPE]),
			dos_memory_pointer_segment(whlshapes[DASHBOARD_LEFT_INSTRUMENT_SOURCE_SHAPE]));
	} else if (wheel_state == DASHBOARD_WHEEL_STATE_RIGHT) {
		shape2d_render_bmp_as_mask(whlshapes[DASHBOARD_RIGHT_INSTRUMENT_MASK_SHAPE]);
		shape2d_rle_or_far_pointer(
			dos_memory_pointer_offset(whlshapes[DASHBOARD_RIGHT_INSTRUMENT_SOURCE_SHAPE]),
			dos_memory_pointer_segment(whlshapes[DASHBOARD_RIGHT_INSTRUMENT_SOURCE_SHAPE]));
	}
}

static legacy_u8 dashboard_copy_transformed_instruments(legacy_u8 wheel_state)
{
	if (dashboard_instruments_follow_wheel == 0) {
		return 0;
	}
	struct SHAPE2D far *descriptor = dashboard_instrument_transforms[wheel_state];
	if (descriptor == 0 && wheel_state != DASHBOARD_WHEEL_STATE_CENTER) {
		return 0;
	}
	struct SHAPE2D far *panel = whlshapes[DASHBOARD_INSTRUMENT_PANEL_SHAPE];
	struct SHAPE2D far *wheel = whlshapes[wheel_state];
	struct SHAPE2D_TRANSFORM transform;
	transform.inverse_xx = (legacy_s16)SHAPE2D_TRANSFORM_ONE;
	transform.inverse_xy = 0;
	transform.inverse_yx = 0;
	transform.inverse_yy = (legacy_s16)SHAPE2D_TRANSFORM_ONE;
	if (descriptor != 0) {
		transform.inverse_xx = LEGACY_S16_FROM_BITS(shape2d_get_anchor_x(descriptor));
		transform.inverse_xy = LEGACY_S16_FROM_BITS(shape2d_get_anchor_y(descriptor));
		transform.inverse_yx = LEGACY_S16_FROM_BITS(shape2d_get_pos_x(descriptor));
		transform.inverse_yy = LEGACY_S16_FROM_BITS(shape2d_get_pos_y(descriptor));
	}
	transform.source_width = dashboard_panel_width(panel);
	transform.source_height = shape2d_get_height(panel);
	transform.source_pivot_x = LEGACY_S16_FROM_BITS(shape2d_get_anchor_x(panel));
	transform.source_pivot_y = LEGACY_S16_FROM_BITS(shape2d_get_anchor_y(panel));
	transform.target_pivot_x = LEGACY_S16_WRAP_ADD(LEGACY_S16_FROM_BITS(shape2d_get_pos_x(panel)),
												   transform.source_pivot_x);
	transform.target_pivot_y = LEGACY_S16_WRAP_ADD(LEGACY_S16_FROM_BITS(shape2d_get_pos_y(panel)),
												   transform.source_pivot_y);
	legacy_s16 x = LEGACY_S16_FROM_BITS(shape2d_get_pos_x(wheel));
	legacy_s16 y = LEGACY_S16_FROM_BITS(shape2d_get_pos_y(wheel));
	legacy_s32 right = (legacy_s32)x + dashboard_panel_width(wheel);
	legacy_s32 bottom = (legacy_s32)y + shape2d_get_height(wheel);
	transform.left = x < 0 ? 0 : x;
	transform.top = y < 0 ? 0 : y;
	transform.right =
		right > DASHBOARD_VIEWPORT_WIDTH ? DASHBOARD_VIEWPORT_WIDTH : (legacy_s16)right;
	transform.bottom =
		bottom > height_above_replaybar ? height_above_replaybar : (legacy_s16)bottom;
	dashboard_set_viewport();
	sprite_copy_image_transformed(dashboard_instrument_sprite, &transform);
	return 1;
}

static void dashboard_update_instruments(struct DASHBOARD_UPDATE *update)
{
	legacy_u16 dot_index;

	if (update->wheel_redrawn != 0 || full_redraw_frames_remaining != 0 ||
		dashboard_speed_index_cache[update->buffer_index] != (legacy_s16)update->speed_index ||
		dashboard_rpm_index_cache[update->buffer_index] != (legacy_s16)update->rpm_index) {
		if (video_uses_page_flipping == 0) {
			mouse_draw_opaque_check();
		}
		if (dashboard_clear_steering_dot(update->buffer_index) != 0) {
			update->steering_dot_cleared = 1;
		}
		if (dashboard_instruments_follow_wheel != 0 && update->wheel_redrawn == 0) {
			/* Gauge-only changes must also clear the old transformed pixels,
			 * including positions outside the upright instrument rectangle. */
			shape2d_rle_copy_position_clipped(whlshapes[update->wheel_state]);
			update->wheel_redrawn = 1;
		}
		dashboard_select_panel_target(dashboard_instrument_sprite,
									  whlshapes[DASHBOARD_INSTRUMENT_PANEL_SHAPE]);
		shape2d_rle_copy(whlshapes[DASHBOARD_INSTRUMENT_PANEL_SHAPE], 0, 0);
		dashboard_speed_index_cache[update->buffer_index] = (legacy_s16)update->speed_index;
		dashboard_rpm_index_cache[update->buffer_index] = (legacy_s16)update->rpm_index;

		legacy_u16 needle_colors =
			(legacy_u16)simd_player.reserved_handling_words[SIMD_NEEDLE_COLORS_INDEX];
		legacy_u8 speed_color = (legacy_u8)needle_colors;
		legacy_u8 rpm_color = (legacy_u8)(needle_colors >> LEGACY_BYTE_BITS);
		if (rpm_color == 0) {
			rpm_color = speed_color;
		}

		if (update->gauge_mode == DASHBOARD_GAUGE_DIGITAL) {
			dashboard_draw_digital_speed(update->speed_index);
		} else if (update->gauge_mode == DASHBOARD_GAUGE_ANALOG) {
			dot_index = update->speed_index * DASHBOARD_POINT_COORDINATE_STRIDE;
			preRender_line(simd_player.spdcenter.px, simd_player.spdcenter.py,
						   (legacy_u8)simd_player.spdpoints[dot_index],
						   (legacy_u8)simd_player.spdpoints[dot_index + DASHBOARD_POINT_Y_OFFSET],
						   speed_color);
		}

		dot_index = update->rpm_index * DASHBOARD_POINT_COORDINATE_STRIDE;
		preRender_line(simd_player.revcenter.px, simd_player.revcenter.py,
					   (legacy_u8)simd_player.revpoints[dot_index],
					   (legacy_u8)simd_player.revpoints[dot_index + DASHBOARD_POINT_Y_OFFSET],
					   rpm_color);
		legacy_u8 transformed = dashboard_instruments_follow_wheel != 0 &&
								(dashboard_instrument_transforms[update->wheel_state] != 0 ||
								 update->wheel_state == DASHBOARD_WHEEL_STATE_CENTER);
		if (transformed == 0) {
			dashboard_draw_wheel_mask(update->wheel_state);
		}
		if (video_uses_page_flipping != 0) {
			sprite_select_mcga_backbuffer();
		} else {
			sprite_select_screen_compat();
		}
		if (dashboard_copy_transformed_instruments(update->wheel_state) == 0) {
			dashboard_copy_panel(dashboard_instrument_sprite,
								 whlshapes[DASHBOARD_INSTRUMENT_PANEL_SHAPE]);
		}
	}
}

static void dashboard_update_steering_dot(struct DASHBOARD_UPDATE *update)
{
	if (dashboard_steering_position_cache[update->buffer_index] != update->steering_position ||
		full_redraw_frames_remaining != 0 || update->steering_dot_cleared != 0) {
		if (video_uses_page_flipping == 0) {
			mouse_draw_opaque_check();
		}
		dashboard_set_viewport();
		(void)dashboard_clear_steering_dot(update->buffer_index);
		legacy_u8 *steering_dots = (legacy_u8 *)simd_player.steeringdots;
		legacy_u16 dot_index = (legacy_u16)(update->steering_position < 0
												? LEGACY_S16_WRAP_NEGATE(update->steering_position)
												: update->steering_position) *
							   DASHBOARD_POINT_COORDINATE_STRIDE;
		legacy_s16 dot_x = steering_dots[dot_index];
		legacy_s16 dot_y = steering_dots[dot_index + DASHBOARD_POINT_Y_OFFSET];
		if (update->steering_position < 0) {
			dot_x = (legacy_u8)(dot_x - (legacy_u8)((legacy_u8)(dot_x - steering_dots[0])
													<< DASHBOARD_STEERING_MIRROR_SHIFT));
		}
		dashboard_steering_dot_x_cache[update->buffer_index] = LEGACY_S16_FROM_BITS(
			((legacy_u16)((legacy_u8)dot_x -
						  shape2d_get_anchor_x(gnobshapes[DASHBOARD_STEERING_DOT_SHAPE]))) &
			(legacy_u16)video_x_alignment_mask);
		dashboard_steering_dot_y_cache[update->buffer_index] =
			LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_SUB(
				(legacy_u8)dot_y, shape2d_get_anchor_y(gnobshapes[DASHBOARD_STEERING_DOT_SHAPE])));
		sprite_clear_shape_alt(
			gnobshapes[DASHBOARD_STEERING_DOT_BUFFER_FIRST_SHAPE + (legacy_u8)frame_buffer_index],
			dashboard_steering_dot_x_cache[update->buffer_index],
			dashboard_steering_dot_y_cache[update->buffer_index]);
		sprite_and_image_at_anchor(gnobshapes[DASHBOARD_STEERING_DOT_BACKGROUND_SHAPE], dot_x,
								   dot_y);
		sprite_or_image_at_anchor(gnobshapes[DASHBOARD_STEERING_DOT_SHAPE], dot_x, dot_y);
		dashboard_steering_position_cache[update->buffer_index] = update->steering_position;
	}
}

static void dashboard_update(void)
{
	struct DASHBOARD_UPDATE update;

	update.buffer_index = (legacy_u8)dashboard_buffer_index;
	update.steering_dot_cleared = 0;
	dashboard_update_gear(update.buffer_index);
	dashboard_update_wheel(&update);
	dashboard_select_gauges(&update);
	dashboard_update_instruments(&update);
	dashboard_update_steering_dot(&update);
	mouse_draw_transparent_check();
}

void setup_car_shapes(legacy_s16 operation)
{
	switch (operation) {
		case DASHBOARD_OPERATION_LOAD:
			dashboard_load();
			break;
		case DASHBOARD_OPERATION_REDRAW_STATIC:
			dashboard_redraw_static();
			break;
		case DASHBOARD_OPERATION_UPDATE:
			dashboard_update();
			break;
		case DASHBOARD_OPERATION_UNLOAD:
			dashboard_unload();
			break;
	}
}
