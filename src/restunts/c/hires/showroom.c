#ifdef RESTUNTS_SDL3
#include "showroom.h"
#include "settings.h"
#include "../platform.h"
#include "../menu/menu_internal.h"
#include "shape3d_hires.h"
#include "../opponent/opponent_portrait.h"
#include "../menu/menu_background.h"
#include "../shape2d/shape2d_internal.h"
#include "../shape3d/shape3d.h"
#include "../externs.h"
#include "../keyboard.h"
#include "../timing.h"
#include "../car/car_model.h"
#include "../video_frame.h"

#define SHOWROOM_MODEL_SCALE 20
/* Low light from the left casts toward the rear-right of the painted podium. */
#define SHOWROOM_SHADOW_HEADING ANGLE_EIGHTH_TURN
/* Cotangent of the showroom light's 30-degree elevation. */
#define SHOWROOM_SHADOW_COTANGENT 1.732051
#define SHOWROOM_SCREEN_WIDTH 320U
#define SHOWROOM_BACKGROUND_HEIGHT 103
#define SHOWROOM_OPPONENT_PANEL_X 240

void hires_showroom_initialize(struct HIRES_SHOWROOM *showroom)
{
	showroom->rotation_time = presentation_now();
	showroom->prepared_render_ns = 0;
	showroom->input_time = showroom->rotation_time;
	showroom->input_delta = 0;
	showroom->portrait_dirty = 0;
	showroom->background_dirty = 0;
	presentation_reset(&showroom->presentation_clock, showroom->rotation_time);
}

void hires_showroom_load_car(struct HIRES_SHOWROOM *showroom,
							 const struct TRANSFORMEDSHAPE3D *transformed)
{
	showroom->shadow_position = transformed->pos;
	showroom->shadow_position.y = LEGACY_S16_WRAP_ADD(
		showroom->shadow_position.y, shape3d_car_ground_height(transformed->shapeptr));
}

void hires_showroom_reset_clock(struct HIRES_SHOWROOM *showroom)
{
	showroom->rotation_time = presentation_now();
	presentation_reset(&showroom->presentation_clock, showroom->rotation_time);
}

legacy_u8 hires_showroom_visual_rotation(struct HIRES_SHOWROOM *showroom, legacy_s16 rotation,
										 legacy_u8 start, legacy_s16 *visual_rotation)
{
	if (supersight_enabled != 0) {
		legacy_u64 now = presentation_now();
		if (!presentation_due(&showroom->presentation_clock, now) && start == 0) {
			return 0;
		}
		/* The preview turns at one angle unit per 100 Hz timer tick. Continue
		 * that known motion without reading another input or changing its timer. */
		*visual_rotation = LEGACY_S16_WRAP_ADD(
			rotation, (legacy_u16)((now - showroom->rotation_time) *
								   DOS_TIMER_REALTIME_TICKS_PER_SECOND / PRESENTATION_SECOND_NS));
	}
	return 1;
}

void hires_showroom_prepare_begin(struct HIRES_SHOWROOM *showroom)
{
	showroom->prepare_started = presentation_now();
}

void hires_showroom_shadows(struct HIRES_SHOWROOM *showroom, struct TRANSFORMEDSHAPE3D *transformed,
							legacy_s16 visual_rotation)
{
	/* The car0 showroom model is authored at twenty times the racing scale. */
	shape3d_hires_set_model_scale(SHOWROOM_MODEL_SCALE);
	transformed->ts_flags &= ~SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
	if (supersight_enabled != 0) {
		const struct VECTOR camera = {LEGACY_S16_WRAP_NEGATE(showroom->shadow_position.x),
									  LEGACY_S16_WRAP_NEGATE(showroom->shadow_position.y),
									  LEGACY_S16_WRAP_NEGATE(showroom->shadow_position.z)};
		shape3d_hires_shadows_begin(&camera);
		shape3d_hires_set_shadow_light(SHOWROOM_SHADOW_HEADING, SHOWROOM_SHADOW_COTANGENT);
		shape3d_hires_shadow_car(
			&showroom->shadow_position, visual_rotation,
			LEGACY_S16_WRAP_MUL(simd_player.collide_points[0].px, SHOWROOM_MODEL_SCALE),
			LEGACY_S16_WRAP_MUL(simd_player.collide_points[1].px, SHOWROOM_MODEL_SCALE));
		shape3d_hires_shadow_projected_model(
			transformed->shapeptr,
			LEGACY_S16_WRAP_SUB(showroom->shadow_position.y, transformed->pos.y));
		transformed->ts_flags |= SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
	}
}

void hires_showroom_car_bounds(struct RECTANGLE *rectangle)
{
	if (supersight_enabled != 0) {
		/* Restore and copy the floor beyond the car's own dirty bounds, including
		 * the previous shadow when the preview rotates or F12 turns it off. */
		*rectangle = carmenu_cliprect;
	}
}

void hires_showroom_background_bounds(struct HIRES_SHOWROOM *showroom, struct RECTANGLE *rectangle)
{
	if (showroom->background_dirty != 0) {
		/* Mode/scale changes discard companion pixels. Restore the complete
		 * showroom, including the wall outside the rotating car's bounds. */
		struct RECTANGLE background = {0, SHOWROOM_SCREEN_WIDTH, 0, SHOWROOM_BACKGROUND_HEIGHT};
		rect_union(rectangle, &background, rectangle);
	}
}

void hires_showroom_defer_render(legacy_u8 *deferred)
{
	if (supersight_enabled != 0) {
		*deferred = 0;
	}
}

void hires_showroom_prepare_end(struct HIRES_SHOWROOM *showroom)
{
	/* Classic previews can defer rasterization until another iteration.
	 * Carry preparation work, but exclude input and waiting between them. */
	showroom->prepared_render_ns = presentation_now() - showroom->prepare_started;
}

void hires_showroom_render_begin(struct HIRES_SHOWROOM *showroom, legacy_u8 complete_frame)
{
	showroom->complete_frame = complete_frame;
	if (complete_frame != 0) {
		/* Geometry is already queued at the current scale. Keep that scale and
		 * leave the initial dissolve free to present each of its phases. */
		sdl3_video_begin_track_frame(0);
		sdl3_video_add_render_work(showroom->prepared_render_ns);
	}
	showroom->prepared_render_ns = 0;
}

void hires_showroom_background(struct HIRES_SHOWROOM *showroom, const struct SHAPE2D *background)
{
	menu_background_draw(&drawing_sprite, background, MENU_BACKGROUND_SHOWROOM);
	showroom->background_dirty = 0;
}

legacy_u8 hires_showroom_redraw_portrait(const struct HIRES_SHOWROOM *showroom, legacy_u8 redraw)
{
	return redraw | showroom->portrait_dirty;
}

void hires_showroom_portrait(struct HIRES_SHOWROOM *showroom, legacy_u16 opponent,
							 struct RECTANGLE *rectangle)
{
	struct SHAPE2D *shape = (struct SHAPE2D *)oppresources[opponent];
	opponent_portrait_draw_at(&drawing_sprite, shape, (legacy_u8)opponent,
							  SHOWROOM_OPPONENT_PANEL_X, 0);
	if (showroom->portrait_dirty != 0) {
		/* Normal preview copies stop before the opponent panel. */
		struct RECTANGLE portrait = {SHOWROOM_OPPONENT_PANEL_X,
									 SHOWROOM_OPPONENT_PANEL_X + shape2d_get_width(shape), 0,
									 shape2d_get_height(shape)};
		rect_union(rectangle, &portrait, rectangle);
		showroom->portrait_dirty = 0;
	}
}

void hires_showroom_render_end(struct HIRES_SHOWROOM *showroom)
{
	if (showroom->complete_frame != 0) {
		sdl3_video_end_frame();
	}
}

void hires_showroom_record_rotation(struct HIRES_SHOWROOM *showroom)
{
	showroom->rotation_time = presentation_now();
}

legacy_u8 hires_showroom_input(struct HIRES_SHOWROOM *showroom, legacy_s16 rotation_delta,
							   legacy_s16 *input_delta)
{
	if (supersight_enabled != 0) {
		showroom->input_delta = LEGACY_S16_WRAP_ADD(showroom->input_delta, rotation_delta);
		/* Render deadlines do not create additional device samples. */
		if (showroom->rotation_time - showroom->input_time <
			PRESENTATION_SECOND_NS / DOS_TIMER_REALTIME_TICKS_PER_SECOND) {
			return 0;
		}
		*input_delta = showroom->input_delta;
	}
	showroom->input_delta = 0;
	showroom->input_time = showroom->rotation_time;
	return 1;
}

void hires_showroom_shortcut(struct HIRES_SHOWROOM *showroom, legacy_u16 input, legacy_u8 opponent)
{
	if (input != (legacy_u16)KEY_F11) {
		showroom->background_dirty = 1;
		if (opponent != 0) {
			showroom->portrait_dirty = 1;
		}
	}
	presentation_reset(&showroom->presentation_clock, presentation_now());
}

void hires_showroom_release(void)
{
	menu_background_unload();
}

#endif
