#include "fileio.h"
#include "legacy.h"
#include "memmgr.h"
#include "menu_internal.h"
#include "platform.h"
#include "shape2d.h"
#include "shape3d.h"
#include "ui_text.h"
#include "timing.h"
#include "game_input.h"
#include "ui_input.h"
#include "ui_dialog.h"
#include "car_speed.h"
#include "video_frame.h"
#include "car_model.h"
#include "scene_resources.h"
#include "car_resources.h"
#include "menu_common.h"
#include "externs.h"
#include "keyboard.h"
#include "frame_internal.h"
#ifdef RESTUNTS_SDL3
#include "presentation.h"
#include "shape3d_hires.h"
#include "opponent_portrait.h"
#include "menu_background.h"
#include "shape2d_internal.h"
#endif

#define GAME_RESOURCE_FILE_INDEX 2
#define CAR_MENU_PLAYER_MODE 0U
#define CAR_MENU_MAXIMUM_CARS 120U
#define CAR_ID_LENGTH 4U
#define CAR_ID_BUFFER_SIZE (CAR_ID_LENGTH + 1U)
#define CAR_RESOURCE_ID_OFFSET 3U
#define CAR_MENU_BUTTON_COUNT 5U
#define CAR_MENU_NO_SELECTION 255U
#define CAR_MENU_WAIT_TICKS 90
#define CAR_MENU_IDLE_LIMIT_TICKS 12000
#define CAR_MENU_BUTTON_WIDTH 86
#define CAR_MENU_BUTTON_HEIGHT 16
#define CAR_MENU_SCREEN_WIDTH 320U
#define CAR_MENU_SCREEN_HEIGHT 200U
#define CAR_MENU_OPPONENT_PANEL_X 240
#define CAR_MENU_OPPONENT_PANEL_RIGHT 240
#define CAR_MENU_TRANSPARENT_COLOR 15U
#define CAR_MENU_PROJECTION_X_SCALE 36
#define CAR_MENU_PROJECTION_Y_SCALE 17
#define CAR_MENU_PROJECTION_HEIGHT 100
#define CAR_MENU_MODEL_SCALE 20
/* Low light from the left casts toward the rear-right of the painted podium. */
#define CAR_MENU_SHADOW_HEADING ANGLE_EIGHTH_TURN
/* Cotangent of the showroom light's 30-degree elevation. */
#define CAR_MENU_SHADOW_COTANGENT 1.732051
#define CAR_MENU_TRANSFORM_DISTANCE 30000U
#define CAR_MENU_CLIPPED_TRANSFORM_FLAG 8U
#define CAR_MENU_BACKGROUND_Y 103
#define CAR_MENU_BACKGROUND_HEIGHT 97
#define CAR_MENU_LEFT_PANEL_X 5
#define CAR_MENU_RIGHT_PANEL_X 82
#define CAR_MENU_PANEL_Y 109
#define CAR_MENU_LEFT_PANEL_WIDTH 70
#define CAR_MENU_RIGHT_PANEL_WIDTH 140
#define CAR_MENU_PANEL_HEIGHT 85
#define CAR_MENU_GRAPH_LABEL_X 9
#define CAR_MENU_GRAPH_LABEL_150_Y 115
#define CAR_MENU_GRAPH_LABEL_100_Y 135
#define CAR_MENU_GRAPH_LABEL_50_Y 155
#define CAR_MENU_GRAPH_LABEL_0_Y 175
#define CAR_MENU_GRAPH_AXIS_X 26
#define CAR_MENU_GRAPH_AXIS_Y 185
#define CAR_MENU_GRAPH_BASELINE_Y 181U
#define CAR_MENU_GRAPH_MINIMUM_Y 117U
#define CAR_MENU_GRAPH_SPEED_SCALE 64UL
#define CAR_MENU_GRAPH_SPEED_DIVISOR 150UL
#define CAR_MENU_GRAPH_WIDTH 38UL
#define CAR_MENU_GRAPH_STEPS 800U
#define CAR_MENU_GRAPH_FIRST_X 28U
#define CAR_MENU_DESCRIPTION_X 88
#define CAR_MENU_DESCRIPTION_FIRST_Y 116
#define CAR_MENU_FULL_CLIP_BOTTOM 200
#define CAR_MENU_CAR_CLIP_BOTTOM 95
#define TRANSMISSION_MODE_MASK 1U

enum CAR_MENU_BUTTON {
	CAR_MENU_DONE_BUTTON = 0,
	CAR_MENU_NEXT_BUTTON = 1,
	CAR_MENU_PREVIOUS_BUTTON = 2,
	CAR_MENU_TRANSMISSION_BUTTON = 3,
	CAR_MENU_COLOR_BUTTON = 4
};

enum CAR_RENDER_PHASE {
	CAR_RENDER_IDLE_PHASE = 0,
	CAR_RENDER_DRAW_PHASE = 1,
	CAR_RENDER_START_PHASE = 3
};

static void car_menu_draw_standard_button(legacy_s8 far *text, legacy_u16 button_index)
{
	draw_button(text, LEGACY_S16_WRAP_ADD(carmenu_buttons[0].x1, 1),
				LEGACY_S16_WRAP_ADD(carmenu_buttons[button_index].y1, 1), CAR_MENU_BUTTON_WIDTH,
				CAR_MENU_BUTTON_HEIGHT, button_top_color, button_bottom_color, button_fill_color,
				0);
}

struct CAR_MENU_STATE {
	legacy_s8 car_ids[CAR_MENU_MAXIMUM_CARS][CAR_ID_BUFFER_SIZE];
	legacy_s8 far *car_resource;
	void far *selector_resource;
	struct SPRITE far *opponent_sprite;
	struct TRANSFORMEDSHAPE3D transformed;
	struct RECTANGLE current_rect;
	struct RECTANGLE previous_rect;
	struct RECTANGLE union_rect;
	legacy_u8 car_count, car_index, previous_car_index;
	legacy_u8 selected, previous_selected, blit_mode;
	legacy_u8 render_phase, car_ready, render_deferred;
	legacy_s16 rotation, rotation_delta;
	legacy_s8 *material;
	legacy_s8 *transmission;
	legacy_u16 opponent_type;
#ifdef RESTUNTS_SDL3
	struct VECTOR shadow_position;
	struct PRESENTATION_CLOCK presentation_clock;
	legacy_u64 rotation_time;
	legacy_u64 prepared_render_ns;
	legacy_u64 input_time;
	legacy_s16 input_delta;
	legacy_u8 portrait_dirty, background_dirty;
#endif
};

static legacy_s16 car_menu_find_cars(struct CAR_MENU_STATE *menu, legacy_s8 *car_id)
{
	ensure_file_exists(GAME_RESOURCE_FILE_INDEX);
	const legacy_s8 *found_path =
		file_combine_and_find(0, car_resource_wildcard, car_resource_extension);
	if (found_path == 0) {
		return 0;
	}
	menu->car_count = 0;
	do {
		for (legacy_u16 i = 0; i < CAR_ID_LENGTH; i++) {
			menu->car_ids[menu->car_count][i] = found_path[i + CAR_RESOURCE_ID_OFFSET];
		}
		menu->car_ids[menu->car_count][CAR_ID_LENGTH] = 0;
		menu->car_count++;
		if (menu->car_count >= CAR_MENU_MAXIMUM_CARS) {
			break;
		}
		found_path = file_find_next_alt();
	} while (found_path != 0);

	legacy_u16 j;
	legacy_s8 swap_id[CAR_ID_BUFFER_SIZE];
	for (legacy_u16 i = 0; i + 1U < menu->car_count; i++) {
		for (j = i + 1U; j < menu->car_count; j++) {
			if (strcmp(menu->car_ids[i], menu->car_ids[j]) > 0) {
				strcpy(swap_id, menu->car_ids[i]);
				strcpy(menu->car_ids[i], menu->car_ids[j]);
				strcpy(menu->car_ids[j], swap_id);
			}
		}
	}

	menu->car_index = 0;
	for (legacy_u16 i = 0; i < menu->car_count; i++) {
		for (j = 0; j < CAR_ID_LENGTH; j++) {
			if (menu->car_ids[i][j] != car_id[j]) {
				break;
			}
		}
		if (j == CAR_ID_LENGTH) {
			menu->car_index = (legacy_u8)i;
		}
	}

	return 1;
}

static void car_menu_initialize(struct CAR_MENU_STATE *menu)
{
	waitflag = CAR_MENU_WAIT_TICKS;
	menu->blit_mode = MENU_BLIT_MODE_INITIAL;
	backlights_paint_override = BACKLIGHT_PAINT_DEFAULT;
	menu->selector_resource = file_load_shape2d_fatal(car_menu_shapes_name);
	menu->opponent_sprite = 0;
	if (menu->opponent_type == CAR_MENU_PLAYER_MODE) {
		miscptr = file_load_resfile(car_misc_resource_name);
	}

	struct SHAPE2D far *opponent_shape;
	if (menu->opponent_type != CAR_MENU_PLAYER_MODE) {
		car_menu_redraw_cliprect.right = CAR_MENU_OPPONENT_PANEL_RIGHT;
		if (video_uses_page_flipping != 0) {
			opponent_shape = (struct SHAPE2D far *)oppresources[(legacy_u16)menu->opponent_type];
			/* Preserve resource access order independently of argument evaluation. */
			legacy_u16 height = shape2d_get_height(opponent_shape);
			legacy_u16 width = shape2d_get_width(opponent_shape);
			menu->opponent_sprite = sprite_make_wnd(width, height, CAR_MENU_TRANSPARENT_COLOR);
			sprite_select_mcga_backbuffer();
			sprite_clear_target(0);
			shape2d_select_ega_palette(opp_res, (legacy_u8)menu->opponent_type);
			sprite_putimage_transparent(opponent_shape, 0, 0);
			sprite_clear_shape_alt(menu->opponent_sprite->sprite_bitmapptr, 0, 0);
		}
	} else {
		car_menu_redraw_cliprect.right = CAR_MENU_SCREEN_WIDTH;
	}

	menu->previous_car_index = CAR_MENU_NO_SELECTION;
	menu->rotation = 0;
#ifdef RESTUNTS_SDL3
	menu->rotation_time = presentation_now();
	menu->prepared_render_ns = 0;
	menu->input_time = menu->rotation_time;
	menu->input_delta = 0;
	menu->portrait_dirty = 0;
	menu->background_dirty = 0;
	presentation_reset(&menu->presentation_clock, menu->rotation_time);
#endif
	menu->selected = CAR_MENU_DONE_BUTTON;
	menu_reset_animation_timers();
	menu->rotation_delta = 0;
	menu->previous_selected = CAR_MENU_NO_SELECTION;
	set_projection(CAR_MENU_PROJECTION_X_SCALE, CAR_MENU_PROJECTION_Y_SCALE, CAR_MENU_SCREEN_WIDTH,
				   CAR_MENU_PROJECTION_HEIGHT);
	(void)timer_get_delta_alt();
	render_window_sprite =
		sprite_make_wnd(CAR_MENU_SCREEN_WIDTH, CAR_MENU_SCREEN_HEIGHT, CAR_MENU_TRANSPARENT_COLOR);
}

static void car_menu_draw_graph(void)
{
	legacy_u16 old_frame_rate = (legacy_u16)framespersec;
	framespersec = GAME_FRAME_RATE_NORMAL;
	init_game_state(GAMESTATE_INIT_SKIP_ROUTE_SETUP);
	state.playerstate.car_transmission = TRANSMISSION_AUTOMATIC;
	legacy_u16 graph_step = 0;
	for (;;) {
		update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &state.playerstate, &simd_player);
		legacy_u16 speed = (legacy_u16)state.playerstate.car_rev_speed >> 8;
		legacy_u16 graph_y = LEGACY_U16_WRAP_SUB(
			CAR_MENU_GRAPH_BASELINE_Y, (legacy_u16)LEGACY_U32_DIV_OR_ZERO(
										   LEGACY_U32_WRAP_MUL(speed, CAR_MENU_GRAPH_SPEED_SCALE),
										   CAR_MENU_GRAPH_SPEED_DIVISOR));
		if (graph_y < CAR_MENU_GRAPH_MINIMUM_Y) {
			break;
		}
		legacy_u16 graph_x = LEGACY_U16_WRAP_ADD(
			(legacy_u16)LEGACY_U32_DIV_OR_ZERO(
				LEGACY_U32_WRAP_MUL(CAR_MENU_GRAPH_WIDTH, graph_step), CAR_MENU_GRAPH_STEPS),
			CAR_MENU_GRAPH_FIRST_X);
		sprite_putpixel_clipped(graph_x, graph_y, performGraphColor);
		graph_step++;
		if (graph_step >= CAR_MENU_GRAPH_STEPS) {
			break;
		}
	}
	framespersec = (legacy_s16)old_frame_rate;
}

static void car_menu_draw_description(struct CAR_MENU_STATE *menu)
{
	font_set_fontdef2(fontnptr);
	legacy_s8 far *description = locate_text_res(menu->car_resource, car_description_id);
	legacy_s16 text_y = CAR_MENU_DESCRIPTION_FIRST_Y;
	legacy_u16 line_length = 0;
	do {
		legacy_u8 character = (legacy_u8)*description++;
		if (character == ']') {
			if (line_length != 0) {
				(&resID_byte1)[line_length] = 0;
				font_draw_text(&resID_byte1, CAR_MENU_DESCRIPTION_X, text_y);
			}
			line_length = 0;
			text_y = LEGACY_S16_WRAP_ADD(text_y, font_glyph_height);
		} else {
			(&resID_byte1)[line_length++] = (legacy_s8)character;
		}
	} while (*description != 0);
	font_set_fontdef();
}

static void car_menu_load_car(struct CAR_MENU_STATE *menu)
{
	if (menu->previous_car_index != CAR_MENU_NO_SELECTION) {
		unload_resource(menu->car_resource);
		shape3d_free_car_shapes();
	}

	shape3d_load_car_shapes(menu->car_ids[menu->car_index], gameconfig.game_opponentcarid);
	for (legacy_u16 i = 0; i < CAR_ID_LENGTH; i++) {
		car_resource_name[i + CAR_RESOURCE_ID_OFFSET] = menu->car_ids[menu->car_index][i];
	}
	menu->car_resource = (legacy_s8 far *)file_load_resfile(car_resource_name);
	setup_aero_trackdata(menu->car_resource, 0);
#ifdef RESTUNTS_SDL3
	menu->shadow_position = menu->transformed.pos;
	menu->shadow_position.y = LEGACY_S16_WRAP_ADD(
		menu->shadow_position.y, shape3d_car_ground_height(menu->transformed.shapeptr));
#endif

	sprite_select_render_window_and_clear();
	draw_button(0, 0, CAR_MENU_BACKGROUND_Y, CAR_MENU_SCREEN_WIDTH, CAR_MENU_BACKGROUND_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
	draw_button(0, CAR_MENU_LEFT_PANEL_X, CAR_MENU_PANEL_Y, CAR_MENU_LEFT_PANEL_WIDTH,
				CAR_MENU_PANEL_HEIGHT, button_top_color, button_bottom_color, button_fill_color, 0);
	draw_button(0, CAR_MENU_RIGHT_PANEL_X, CAR_MENU_PANEL_Y, CAR_MENU_RIGHT_PANEL_WIDTH,
				CAR_MENU_PANEL_HEIGHT, button_top_color, button_bottom_color, button_fill_color, 0);
	struct SHAPE2D far *shape =
		(struct SHAPE2D far *)locate_shape_fatal(menu->selector_resource, car_graph_shape_id);
	sprite_shape_to_1_alt(shape);

	font_set_fontdef2(fontnptr);
	font_set_colors(0, dialog_fnt_colour);
	font_draw_text(car_graph_one_fifty_label, CAR_MENU_GRAPH_LABEL_X, CAR_MENU_GRAPH_LABEL_150_Y);
	font_draw_text(car_graph_hundred_label, CAR_MENU_GRAPH_LABEL_X, CAR_MENU_GRAPH_LABEL_100_Y);
	font_draw_text(car_graph_fifty_label, CAR_MENU_GRAPH_LABEL_X, CAR_MENU_GRAPH_LABEL_50_Y);
	font_draw_text(car_graph_zero_label, CAR_MENU_GRAPH_LABEL_X, CAR_MENU_GRAPH_LABEL_0_Y);
	font_draw_text(car_graph_time_labels, CAR_MENU_GRAPH_AXIS_X, CAR_MENU_GRAPH_AXIS_Y);
	font_set_fontdef();

	car_menu_draw_standard_button(locate_text_res(miscptr, car_done_button_id),
								  CAR_MENU_DONE_BUTTON);
	car_menu_draw_standard_button(locate_text_res(miscptr, car_next_button_id),
								  CAR_MENU_NEXT_BUTTON);
	car_menu_draw_standard_button(locate_text_res(miscptr, car_previous_button_id),
								  CAR_MENU_PREVIOUS_BUTTON);
	legacy_s8 far *transmission_text = locate_text_res(
		miscptr, *menu->transmission != TRANSMISSION_MANUAL ? car_automatic_button_id
															: car_manual_button_id);
	car_menu_draw_standard_button(transmission_text, CAR_MENU_TRANSMISSION_BUTTON);
	car_menu_draw_standard_button(locate_text_res(miscptr, car_color_button_id),
								  CAR_MENU_COLOR_BUTTON);

	car_menu_draw_graph();
	car_menu_draw_description(menu);

	(void)timer_get_delta_alt();
	frame_fps_reset();
#ifdef RESTUNTS_SDL3
	menu->rotation_time = presentation_now();
	presentation_reset(&menu->presentation_clock, menu->rotation_time);
#endif
	menu->previous_selected = CAR_MENU_NO_SELECTION;
	menu->previous_rect.left = 0;
	menu->previous_rect.right = CAR_MENU_SCREEN_WIDTH;
	menu->previous_rect.top = 0;
	menu->previous_rect.bottom = CAR_MENU_SCREEN_HEIGHT;
	menu->car_ready = 0;
	menu->render_phase = CAR_RENDER_START_PHASE;
}

static void car_menu_prepare_preview(struct CAR_MENU_STATE *menu)
{
	menu->rotation = LEGACY_S16_WRAP_ADD(menu->rotation, menu->rotation_delta);
	legacy_s16 visual_rotation = menu->rotation;
#ifdef RESTUNTS_SDL3
	if (supersight_enabled != 0) {
		legacy_u64 now = presentation_now();
		if (!presentation_due(&menu->presentation_clock, now) &&
			menu->render_phase != CAR_RENDER_START_PHASE) {
			return;
		}
		/* The preview turns at one angle unit per 100 Hz timer tick. Continue
		 * that known motion without reading another input or changing its timer. */
		visual_rotation =
			LEGACY_S16_WRAP_ADD(menu->rotation, (legacy_u16)((now - menu->rotation_time) *
															 DOS_TIMER_REALTIME_TICKS_PER_SECOND /
															 PRESENTATION_SECOND_NS));
	}
#endif
	if (menu->render_phase == CAR_RENDER_IDLE_PHASE ||
		menu->render_phase == CAR_RENDER_START_PHASE) {
#ifdef RESTUNTS_SDL3
		legacy_u64 prepare_started = presentation_now();
#endif
		legacy_s16 car_position_angle = (legacy_s16)polarAngle(carmenu_carpos.y, carmenu_carpos.z);
		menu->current_rect = slow_video_mgmt_copy != 0 ? empty_rect : carmenu_cliprect;
		select_cliprect_rotate(0, car_position_angle, 0, &carmenu_cliprect, 0);
#ifdef RESTUNTS_SDL3
		/* The car0 showroom model is authored at twenty times the racing scale. */
		shape3d_hires_set_model_scale(CAR_MENU_MODEL_SCALE);
		menu->transformed.ts_flags &= ~SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
		if (supersight_enabled != 0) {
			const struct VECTOR camera = {LEGACY_S16_WRAP_NEGATE(menu->shadow_position.x),
										  LEGACY_S16_WRAP_NEGATE(menu->shadow_position.y),
										  LEGACY_S16_WRAP_NEGATE(menu->shadow_position.z)};
			shape3d_hires_shadows_begin(&camera);
			shape3d_hires_set_shadow_light(CAR_MENU_SHADOW_HEADING, CAR_MENU_SHADOW_COTANGENT);
			shape3d_hires_shadow_car(
				&menu->shadow_position, visual_rotation,
				LEGACY_S16_WRAP_MUL(simd_player.collide_points[0].px, CAR_MENU_MODEL_SCALE),
				LEGACY_S16_WRAP_MUL(simd_player.collide_points[1].px, CAR_MENU_MODEL_SCALE));
			shape3d_hires_shadow_projected_model(
				menu->transformed.shapeptr,
				LEGACY_S16_WRAP_SUB(menu->shadow_position.y, menu->transformed.pos.y));
			menu->transformed.ts_flags |= SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
		}
#endif
		if ((legacy_s8)(legacy_u8)*menu->material >=
			(legacy_s8)(legacy_u8)game3dshapes[PLAYER_CAR_LOW_SHAPE].shape3d_numpaints) {
			*menu->material = 0;
		}
		menu->transformed.rotvec.z = visual_rotation;
		menu->transformed.material = (legacy_u8)*menu->material;
		shape3d_transform_and_queue(&menu->transformed);
#ifdef RESTUNTS_SDL3
		if (supersight_enabled != 0) {
			/* Restore and copy the floor beyond the car's own dirty bounds, including
			 * the previous shadow when the preview rotates or F12 turns it off. */
			menu->current_rect = carmenu_cliprect;
		}
#endif
		car_menu_redraw_cliprect.bottom = menu->previous_car_index == menu->car_index
											  ? CAR_MENU_CAR_CLIP_BOTTOM
											  : CAR_MENU_FULL_CLIP_BOTTOM;
		(void)rect_intersect(&menu->current_rect, &car_menu_redraw_cliprect);
		rect_union(&menu->current_rect, &menu->previous_rect, &menu->union_rect);
#ifdef RESTUNTS_SDL3
		if (menu->background_dirty != 0) {
			/* Mode/scale changes discard companion pixels. Restore the complete
			 * showroom, including the wall outside the rotating car's bounds. */
			struct RECTANGLE background_rect = {0, CAR_MENU_SCREEN_WIDTH, 0, CAR_MENU_BACKGROUND_Y};
			rect_union(&menu->union_rect, &background_rect, &menu->union_rect);
		}
#endif
		if (menu->render_phase != CAR_RENDER_START_PHASE) {
			menu->render_phase = CAR_RENDER_DRAW_PHASE;
			menu->render_deferred = 1;
#ifdef RESTUNTS_SDL3
			if (supersight_enabled != 0) {
				menu->render_deferred = 0;
			}
#endif
		}
#ifdef RESTUNTS_SDL3
		/* Classic previews can defer rasterization until another iteration.
		 * Carry preparation work, but exclude input and waiting between them. */
		menu->prepared_render_ns = presentation_now() - prepare_started;
#endif
	}
}

static void car_menu_render_preview(struct CAR_MENU_STATE *menu)
{
	if (menu->render_deferred == 0 && (menu->render_phase == CAR_RENDER_DRAW_PHASE ||
									   menu->render_phase == CAR_RENDER_START_PHASE)) {
		menu->render_phase = CAR_RENDER_IDLE_PHASE;
		menu->car_ready = 1;
#ifdef RESTUNTS_SDL3
		legacy_u8 complete_frame = menu->blit_mode == MENU_BLIT_MODE_REFRESH;
		if (complete_frame != 0) {
			/* Geometry is already queued at the current scale. Keep that scale and
			 * leave the initial dissolve free to present each of its phases. */
			sdl3_video_begin_track_frame(0);
			sdl3_video_add_render_work(menu->prepared_render_ns);
		}
		menu->prepared_render_ns = 0;
#endif
		sprite_select_render_window();
		sprite_set_target_clip_bounds(menu->union_rect.left, menu->union_rect.right,
									  menu->union_rect.top, menu->union_rect.bottom);
		struct SHAPE2D far *background = (struct SHAPE2D far *)locate_shape_fatal(
			menu->selector_resource, car_preview_top_shape_id);
		sprite_putimage(background);
#ifdef RESTUNTS_SDL3
		menu_background_draw(&drawing_sprite, background, MENU_BACKGROUND_SHOWROOM);
		menu->background_dirty = 0;
#endif
		shape3d_render_queued_primitives();
		sprite_select_render_window();
		if (frame_display_overlay_active() != 0) {
			sprite_set_target_clip_bounds(0, CAR_MENU_SCREEN_WIDTH, 0, CAR_MENU_CAR_CLIP_BOTTOM);
			struct RECTANGLE *fps_rect = frame_fps_draw_text();
			/* Include the counter in this copy and the next frame's background restore. */
			rect_union(&menu->current_rect, fps_rect, &menu->current_rect);
			rect_union(&menu->union_rect, fps_rect, &menu->union_rect);
		}
		sprite_set_target_clip_bounds(menu->union_rect.left, menu->union_rect.right,
									  menu->union_rect.top, menu->union_rect.bottom);
		menu->previous_rect = menu->current_rect;

		legacy_u8 redraw_portrait = menu->previous_car_index != menu->car_index;
#ifdef RESTUNTS_SDL3
		redraw_portrait |= menu->portrait_dirty;
#endif
		if (menu->opponent_type != CAR_MENU_PLAYER_MODE && redraw_portrait != 0) {
			sprite_select_render_window();
			if (video_uses_page_flipping == 0) {
				shape2d_select_ega_palette(opp_res, (legacy_u8)menu->opponent_type);
				sprite_putimage_transparent(
					(struct SHAPE2D far *)oppresources[(legacy_u16)menu->opponent_type],
					CAR_MENU_OPPONENT_PANEL_X, 0);
			} else {
				sprite_copy_image_at(menu->opponent_sprite->sprite_bitmapptr,
									 CAR_MENU_OPPONENT_PANEL_X, 0);
			}
#ifdef RESTUNTS_SDL3
			struct SHAPE2D *portrait_shape = (struct SHAPE2D *)oppresources[menu->opponent_type];
			opponent_portrait_draw_at(&drawing_sprite, portrait_shape,
									  (legacy_u8)menu->opponent_type, CAR_MENU_OPPONENT_PANEL_X, 0);
			if (menu->portrait_dirty != 0) {
				/* Normal preview copies stop before the opponent panel. */
				struct RECTANGLE portrait_rect = {CAR_MENU_OPPONENT_PANEL_X,
												  CAR_MENU_OPPONENT_PANEL_X +
													  shape2d_get_width(portrait_shape),
												  0, shape2d_get_height(portrait_shape)};
				rect_union(&menu->union_rect, &portrait_rect, &menu->union_rect);
				menu->portrait_dirty = 0;
			}
#endif
		}

		sprite_select_screen_compat();
		sprite_set_target_clip_bounds(menu->union_rect.left, menu->union_rect.right,
									  menu->union_rect.top, menu->union_rect.bottom);
		mouse_draw_opaque_check();
		if (menu->blit_mode != MENU_BLIT_MODE_REFRESH) {
			(void)sprite_blit_to_video(render_window_sprite, LEGACY_S8_FROM_BITS(menu->blit_mode));
			menu->blit_mode = MENU_BLIT_MODE_REFRESH;
		} else {
			sprite_putimage(render_window_sprite->sprite_bitmapptr);
		}
		mouse_draw_transparent_check();
		frame_fps_record_presented();
#ifdef RESTUNTS_SDL3
		if (complete_frame != 0) {
			sdl3_video_end_frame();
		}
#endif
		menu->previous_car_index = menu->car_index;
	}
}

static void car_menu_draw_selection(struct CAR_MENU_STATE *menu)
{
	if (menu->previous_selected != menu->selected) {
		if (menu->previous_selected != CAR_MENU_NO_SELECTION) {
			sprite_select_screen_compat();
			sprite_set_target_clip_bounds(
				carmenu_buttons[0].x1,
				LEGACY_S16_FROM_BITS(
					(legacy_u16)((LEGACY_U16_WRAP_ADD(carmenu_buttons[0].x2, video_x_alignment)) &
								 (legacy_u16)video_x_alignment_mask)),
				carmenu_buttons[0].y1,
				LEGACY_S16_WRAP_ADD(carmenu_buttons[CAR_MENU_COLOR_BUTTON].y2, 1));
			mouse_draw_opaque_check();
			sprite_putimage(render_window_sprite->sprite_bitmapptr);
			mouse_draw_transparent_check();
			sprite_select_screen_compat();
		}
		menu_reset_animation_timers();
		menu->previous_selected = menu->selected;
	}
}

static legacy_u16 car_menu_read_input(struct CAR_MENU_STATE *menu)
{
	sprite_select_screen_compat();
	menu->rotation_delta = (legacy_s16)menu_animate_button_highlight(
		menu->selected, carmenu_buttons, menu_highlight_second_color, menu_highlight_first_color);
#ifdef RESTUNTS_SDL3
	menu->rotation_time = presentation_now();
#endif
	menu_update_idle_counter((legacy_u16)menu->rotation_delta, CAR_MENU_IDLE_LIMIT_TICKS);
	legacy_s16 input_delta = menu->rotation_delta;
#ifdef RESTUNTS_SDL3
	if (supersight_enabled != 0) {
		menu->input_delta = LEGACY_S16_WRAP_ADD(menu->input_delta, menu->rotation_delta);
		/* Render deadlines do not create additional device samples. */
		if (menu->rotation_time - menu->input_time <
			PRESENTATION_SECOND_NS / DOS_TIMER_REALTIME_TICKS_PER_SECOND) {
			return 0;
		}
		input_delta = menu->input_delta;
	}
	menu->input_delta = 0;
	menu->input_time = menu->rotation_time;
#endif
	legacy_u16 input = (legacy_u16)input_checking(input_delta);
	legacy_s16 mouse_hit = (legacy_s16)mouse_multi_hittest(CAR_MENU_BUTTON_COUNT, carmenu_buttons);
	if (mouse_hit != -1) {
		menu->selected = (legacy_u8)mouse_hit;
	}
	if (idle_expired != 0) {
		menu->selected = CAR_MENU_DONE_BUTTON;
		input = KEY_ENTER;
	}

	return input;
}

static legacy_s16 car_menu_activate_selection(struct CAR_MENU_STATE *menu)
{
	if (menu->selected == CAR_MENU_DONE_BUTTON) {
		if (menu->car_ready == 0) {
			return 0;
		}
		return 1;
	} else if (menu->selected == CAR_MENU_NEXT_BUTTON) {
		menu->car_index++;
		if (menu->car_index == menu->car_count) {
			menu->car_index = 0;
		}
		return 0;
	} else if (menu->selected == CAR_MENU_PREVIOUS_BUTTON) {
		menu->car_index = menu->car_index == 0 ? (legacy_u8)(menu->car_count - 1U)
											   : (legacy_u8)(menu->car_index - 1U);
		return 0;
	} else if (menu->selected == CAR_MENU_TRANSMISSION_BUTTON) {
		*menu->transmission = (legacy_s8)((legacy_u8)*menu->transmission ^ TRANSMISSION_MODE_MASK);
		sprite_select_render_window();
		legacy_s8 far *transmission_text = locate_text_res(
			miscptr, *menu->transmission != TRANSMISSION_MANUAL ? car_automatic_toggle_id
																: car_manual_toggle_id);
		car_menu_draw_standard_button(transmission_text, CAR_MENU_TRANSMISSION_BUTTON);
		sprite_select_screen_compat();
		mouse_draw_opaque_check();
		car_menu_draw_standard_button(transmission_text, CAR_MENU_TRANSMISSION_BUTTON);
		mouse_draw_transparent_check();
		return 0;
	} else if (menu->selected == CAR_MENU_COLOR_BUTTON) {
		*menu->material = (legacy_s8)((legacy_u8)*menu->material + 1U);
		menu->render_phase = CAR_RENDER_START_PHASE;
		return 0;
	} else {
		return 0;
	}
}

static legacy_s16 car_menu_handle_input(struct CAR_MENU_STATE *menu, legacy_u16 input)
{
	if (input == (legacy_u16)KEY_F11 || input == (legacy_u16)KEY_F12
#ifdef RESTUNTS_SDL3
		|| input == (legacy_u16)KEY_SHIFT_F12
#endif
	) {
		handle_ingame_kb_shortcuts(LEGACY_S16_FROM_BITS(input));
#ifdef RESTUNTS_SDL3
		if (input != (legacy_u16)KEY_F11) {
			menu->background_dirty = 1;
			if (menu->opponent_type != CAR_MENU_PLAYER_MODE) {
				menu->portrait_dirty = 1;
			}
		}
		presentation_reset(&menu->presentation_clock, presentation_now());
#endif
		menu->render_phase = CAR_RENDER_START_PHASE;
		return 0;
	}
	if (input == 0) {
		return 0;
	}
	if (input == KEY_UP) {
		menu->selected = menu->selected == CAR_MENU_DONE_BUTTON ? CAR_MENU_COLOR_BUTTON
																: (legacy_u8)(menu->selected - 1U);
		return 0;
	}
	if (input == KEY_DOWN) {
		menu->selected = menu->selected >= CAR_MENU_COLOR_BUTTON ? CAR_MENU_DONE_BUTTON
																 : (legacy_u8)(menu->selected + 1U);
		return 0;
	}
	if (input == KEY_ESCAPE) {
		return 1;
	}
	if (input != KEY_ENTER && input != KEY_SPACE) {
		return 0;
	}

	return car_menu_activate_selection(menu);
}

static void car_menu_release(struct CAR_MENU_STATE *menu, legacy_s8 *car_id)
{
#ifdef RESTUNTS_SDL3
	menu_background_unload();
#endif
	sprite_free_wnd(render_window_sprite);
	unload_resource(menu->car_resource);
	shape3d_free_car_shapes();
	if (menu->opponent_type != CAR_MENU_PLAYER_MODE && video_uses_page_flipping != 0) {
		sprite_free_wnd(menu->opponent_sprite);
	}
	if (menu->opponent_type == CAR_MENU_PLAYER_MODE) {
		unload_resource(miscptr);
	}
	mmgr_free((legacy_s8 far *)menu->selector_resource);
	mouse_draw_opaque_check();
	for (legacy_u16 i = 0; i < CAR_ID_LENGTH; i++) {
		car_id[i] = menu->car_ids[menu->car_index][i];
	}
	idle_expired = 0;
}

void run_car_menu(legacy_s8 *car_id, legacy_s8 *material, legacy_s8 *transmission,
				  legacy_u16 opponent_type)
{
	struct CAR_MENU_STATE menu_state;
	struct CAR_MENU_STATE *menu = &menu_state;
	menu->material = material;
	menu->transmission = transmission;
	menu->opponent_type = opponent_type;
	menu->transformed.pos = carmenu_carpos;
	menu->transformed.shapeptr = &game3dshapes[PLAYER_CAR_LOW_SHAPE];
	menu->transformed.rotvec.x = 0;
	menu->transformed.rotvec.y = 0;
	menu->transformed.culling_distance = CAR_MENU_TRANSFORM_DISTANCE;
	slow_video_mgmt_copy = slow_video_mgmt;
	if (slow_video_mgmt_copy != 0) {
		menu->transformed.rectptr = &menu->current_rect;
		menu->transformed.ts_flags = CAR_MENU_CLIPPED_TRANSFORM_FLAG;
	} else {
		menu->transformed.rectptr = 0;
		menu->transformed.ts_flags = 0;
	}

	if (!car_menu_find_cars(menu, car_id)) {
		return;
	}
	car_menu_initialize(menu);
	for (;;) {
		menu->render_deferred = 0;
		if (menu->previous_car_index != menu->car_index) {
			car_menu_load_car(menu);
		}
		car_menu_prepare_preview(menu);
		car_menu_render_preview(menu);
		car_menu_draw_selection(menu);
		legacy_u16 input = car_menu_read_input(menu);
		if (car_menu_handle_input(menu, input)) {
			break;
		}
	}
	car_menu_release(menu, car_id);
	frame_fps_reset();
}
