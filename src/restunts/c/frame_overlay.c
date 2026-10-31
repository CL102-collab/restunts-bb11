#include "frame_internal.h"
#include "hires/frame_render.h"
#include "game_input.h"
#include "ui/ui_text.h"
#include "shape2d/shape2d.h"
#include "shape2d/shape2d_internal.h"
#include "race/race_stats.h"
#include "camera.h"
#include "video_frame.h"
#include "skybox.h"
#include "race/race_graphics.h"
#include "externs.h"
#include "memmgr.h"
#include "resource.h"
#include "crash_state.h"
#include "platform.h"
#include "dashboard.h"

#define OVERLAY_SCREEN_WIDTH 320
#define OVERLAY_REFERENCE_HEIGHT 200L
#define SINK_DURATION_FRAME_SHIFT 2U
#define CRACK_FRAME_RATE_DIVISOR 7U
#define CRACK_INFO_HEADER_WORDS 1U
#define CRACK_INFO_INDEX_SHIFT 1U
#define CRACK_LINE_RECORD_SHIFT 3U
#define CRACK_START_Y_OFFSET 2U
#define CRACK_END_X_OFFSET 4U
#define CRACK_END_Y_OFFSET 6U
#define DEMO_TEXT_FIRST_Y 170
#define DEMO_TEXT_SECOND_Y 182
#define REPLAY_TEXT_LEFT_X 8
#define REPLAY_TEXT_RIGHT_X 312
#define REPLAY_TEXT_CHARACTER_WIDTH 8U
#define REPLAY_TEXT_Y 15
#define REPLAY_FILENAME_Y 3
#define REPLAY_TEXT_LINE_HEIGHT (REPLAY_TEXT_Y - REPLAY_FILENAME_Y)
#define REPLAY_TEXT_MAX_CHARACTERS                                                                 \
	((REPLAY_TEXT_RIGHT_X - REPLAY_TEXT_LEFT_X) / REPLAY_TEXT_CHARACTER_WIDTH)
#define PREPARE_TEXT_Y 90
#define SECURITY_TEXT_FIRST_Y 93
#define SECURITY_TEXT_SECOND_Y 105
#define WRONG_WAY_TEXT_Y 93
#define DIRECTION_ICON_CENTER_X 148
#define DIRECTION_ICON_CENTER_Y 93
#define OPPONENT_LEFT_ICON_X 68
#define OPPONENT_RIGHT_ICON_X 228
#define OPPONENT_ICON_Y 113
#define OPPONENT_TEXT_Y 116
#define OPPONENT_TEXT_SHADOW_HEIGHT 1
#define PENALTY_TEXT_Y 102

enum DIRECTION_ICON_SHAPE_INDEX { DIRECTION_ICON_LEFT_SHAPE = 3, DIRECTION_ICON_RIGHT_SHAPE = 4 };

struct RECTANGLE *do_sinking(legacy_s16 frame, legacy_s16 top, legacy_s16 height)
{
	legacy_s16 duration = LEGACY_S16_SHL(framespersec, SINK_DURATION_FRAME_SHIFT);
	legacy_s16 clipped_frame = (legacy_s16)frame;
	if (clipped_frame > duration) {
		clipped_frame = duration;
	}
	legacy_s16 sink_height = LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_DIV_OR_ZERO(
		LEGACY_S32_WRAP_MUL((legacy_s32)height, (legacy_s32)clipped_frame), (legacy_s32)duration));
	legacy_s16 bottom = LEGACY_S16_WRAP_ADD(top, height);
	rect_ingame_text.left = 0;
	rect_ingame_text.right = OVERLAY_SCREEN_WIDTH;
	rect_ingame_text.top = LEGACY_S16_WRAP_SUB(bottom, sink_height);
	rect_ingame_text.bottom = bottom;
	sprite_set_target_clip_bounds(0, OVERLAY_SCREEN_WIDTH, rect_ingame_text.top,
								  rect_ingame_text.bottom);
	sprite_clear_target((legacy_u8)skybox.water_color);
	return &rect_ingame_text;
}

struct RECTANGLE *init_crak(legacy_s16 frame, legacy_s16 top, legacy_s16 height)
{
	legacy_u8 far *crack_lines = (legacy_u8 far *)locate_shape_alt(gameresptr, "crak");
	legacy_u8 far *crack_info = (legacy_u8 far *)locate_shape_alt(gameresptr, "cinf");
	legacy_s16 frame_divisor =
		LEGACY_S16_FROM_BITS(LEGACY_U16_DIV_OR_ZERO(framespersec, CRACK_FRAME_RATE_DIVISOR));
	legacy_s16 frame_index = LEGACY_S16_DIV_OR_ZERO(frame, frame_divisor);
	legacy_s16 frame_count = LEGACY_READ_S16_LE(crack_info);
	if (frame_index >= frame_count) {
		frame_index = LEGACY_S16_WRAP_SUB(frame_count, 1);
	}
	legacy_s16 line_count = LEGACY_READ_S16_LE(
		crack_info + ((legacy_u16)LEGACY_S16_WRAP_ADD(frame_index, CRACK_INFO_HEADER_WORDS)
					  << CRACK_INFO_INDEX_SHIFT));
	rect_ingame_text = empty_rect;

	struct POINT2D point;
	for (legacy_s16 i = 0; i < line_count; i++) {
		legacy_u16 line_offset = (legacy_u16)i << CRACK_LINE_RECORD_SHIFT;

		legacy_s16 start_x = LEGACY_READ_S16_LE(crack_lines + line_offset);
		legacy_s16 start_y = LEGACY_READ_S16_LE(crack_lines + line_offset + CRACK_START_Y_OFFSET);
		legacy_s16 end_x = LEGACY_READ_S16_LE(crack_lines + line_offset + CRACK_END_X_OFFSET);
		legacy_s16 end_y = LEGACY_READ_S16_LE(crack_lines + line_offset + CRACK_END_Y_OFFSET);
		legacy_s32 scaled_coordinate = LEGACY_S32_WRAP_MUL((legacy_s32)start_y, (legacy_s32)height);
		legacy_s16 scaled_start_y = LEGACY_S16_FROM_BITS(
			(legacy_u16)LEGACY_S32_DIV_OR_ZERO(scaled_coordinate, OVERLAY_REFERENCE_HEIGHT));
		scaled_coordinate = LEGACY_S32_WRAP_MUL((legacy_s32)end_y, (legacy_s32)height);
		legacy_s16 scaled_end_y = LEGACY_S16_FROM_BITS(
			(legacy_u16)LEGACY_S32_DIV_OR_ZERO(scaled_coordinate, OVERLAY_REFERENCE_HEIGHT));

		preRender_crack_line(
			start_x, LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(scaled_start_y, top), 1), end_x,
			LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(scaled_end_y, top), 1), 0);
		preRender_crack_line(
			start_x, LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(scaled_start_y, top), 1), end_x,
			LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(scaled_end_y, top), 1), 0);
		preRender_crack_line(start_x, LEGACY_S16_WRAP_ADD(scaled_start_y, top), end_x,
							 LEGACY_S16_WRAP_ADD(scaled_end_y, top), dialog_fnt_colour);

		if (slow_video_mgmt_copy != 0) {
			point.px = start_x;
			point.py = LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(scaled_start_y, top), 1);
			rect_adjust_from_point(&point, &rect_ingame_text);
			point.px = end_x;
			point.py = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(scaled_end_y, top), 1);
			rect_adjust_from_point(&point, &rect_ingame_text);
			point.px = start_x;
			point.py = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(scaled_start_y, top), 1);
			rect_adjust_from_point(&point, &rect_ingame_text);
			point.px = end_x;
			point.py = LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(scaled_end_y, top), 1);
			rect_adjust_from_point(&point, &rect_ingame_text);
			shape3d_retain_legacy_crack_bounds(i, dos_memory_pointer_offset(crack_lines));
		}
	}

	return &rect_ingame_text;
}

static void draw_centered_ingame_resource(legacy_s8 *resource_id, legacy_s16 y)
{
	copy_string(&resID_byte1, locate_text_res(gameresptr, resource_id));
	rect_union(
		&rect_ingame_text,
		intro_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1), y, dialog_fnt_colour, 0),
		&rect_ingame_text);
}

static void draw_opponent_route_information(void)
{
	legacy_s16 shape_index;
	legacy_s16 x;
	switch (state.game_opponent_route_indicator) {
		case ROUTE_INDICATOR_LEFT:
			shape_index = DIRECTION_ICON_LEFT_SHAPE;
			x = OPPONENT_LEFT_ICON_X;
			break;
		case ROUTE_INDICATOR_RIGHT:
			shape_index = DIRECTION_ICON_RIGHT_SHAPE;
			x = OPPONENT_RIGHT_ICON_X;
			break;
		default:
			return;
	}

	struct SHAPE2D far *shape = sdgame2shapes[shape_index];
	legacy_s32 icon_bottom = OPPONENT_ICON_Y + (legacy_s32)shape2d_get_height(shape);
	legacy_s32 text_bottom =
		OPPONENT_TEXT_Y + (legacy_s32)font_glyph_height + OPPONENT_TEXT_SHADOW_HEIGHT;
	legacy_s32 bottom = icon_bottom > text_bottom ? icon_bottom : text_bottom;
	legacy_s32 top = OPPONENT_ICON_Y < OPPONENT_TEXT_Y ? OPPONENT_ICON_Y : OPPONENT_TEXT_Y;
	legacy_s32 viewport_top = drawing_sprite.sprite_top;
	legacy_s32 viewport_bottom = drawing_sprite.sprite_bottom;
	if (bottom - top > viewport_bottom - viewport_top) {
		return;
	}
	/* Keep the arrow and label together above every car's dashboard boundary.
	 * The bitmap is clipped by this viewport, while font pixels are not. */
	legacy_s32 offset = bottom > viewport_bottom ? viewport_bottom - bottom : 0;
	if (top + offset < viewport_top) {
		offset = viewport_top - top;
	}
	legacy_s16 icon_y = (legacy_s16)(OPPONENT_ICON_Y + offset);
	struct RECTANGLE icon_bounds;
	icon_bounds.left = x;
	icon_bounds.right = (legacy_s16)(x + shape2d_get_width(shape));
	icon_bounds.top = icon_y;
	icon_bounds.bottom = (legacy_s16)(icon_bottom + offset);
	sprite_putimage_transparent(shape, x, icon_y);
	rect_union(&rect_ingame_text, &icon_bounds, &rect_ingame_text);
	draw_centered_ingame_resource("opp", (legacy_s16)(OPPONENT_TEXT_Y + offset));
}

static void draw_ingame_route_information(void)
{
	switch (state.game_player_route_indicator) {
		case ROUTE_INDICATOR_LEFT:
			sprite_putimage_transparent(sdgame2shapes[DIRECTION_ICON_LEFT_SHAPE],
										DIRECTION_ICON_CENTER_X, DIRECTION_ICON_CENTER_Y);
			rect_union(&rect_ingame_text, &rect_ingame_text2, &rect_ingame_text);
			break;
		case ROUTE_INDICATOR_RIGHT:
			sprite_putimage_transparent(sdgame2shapes[DIRECTION_ICON_RIGHT_SHAPE],
										DIRECTION_ICON_CENTER_X, DIRECTION_ICON_CENTER_Y);
			rect_union(&rect_ingame_text, &rect_ingame_text2, &rect_ingame_text);
			break;
		case ROUTE_INDICATOR_WRONG_WAY:
			draw_centered_ingame_resource("www", WRONG_WAY_TEXT_Y);
			break;
	}

	draw_opponent_route_information();

	if (show_penalty_counter != 0) {
		copy_string(&resID_byte1, locate_text_res(gameresptr, "pen"));
		format_frame_as_string(&resID_byte1 + strlen(&resID_byte1), penalty_time, 0);
		rect_union(&rect_ingame_text,
				   intro_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1), PENALTY_TEXT_Y,
								   dialog_fnt_colour, 0),
				   &rect_ingame_text);
	}
}

static void draw_replay_text(legacy_s8 *text, legacy_s16 y)
{
	legacy_s16 x = LEGACY_S16_WRAP_SUB(
		REPLAY_TEXT_RIGHT_X, LEGACY_U16_WRAP_MUL(strlen(text), REPLAY_TEXT_CHARACTER_WIDTH));
	rect_union(&rect_ingame_text, intro_draw_text(text, x, y, dialog_fnt_colour, 0),
			   &rect_ingame_text);
}

static legacy_s16 draw_replay_filename(legacy_u16 reserved_characters)
{
	const legacy_s8 *filename = replay_filename;
	legacy_s16 y = REPLAY_FILENAME_Y;

	while (*filename != 0) {
		legacy_u16 reserved = y == REPLAY_FILENAME_Y ? reserved_characters : 0;
		reserved = frame_hires_filename_reserved(y, reserved);
		legacy_u16 line_limit = REPLAY_TEXT_MAX_CHARACTERS - reserved;
		legacy_s8 line[REPLAY_TEXT_MAX_CHARACTERS + 1U];
		legacy_u16 length = 0;
		while (length < line_limit && *filename != 0) {
			line[length++] = *filename++;
		}
		line[length] = 0;
		draw_replay_text(line, y);
		y = LEGACY_S16_WRAP_ADD(y, REPLAY_TEXT_LINE_HEIGHT);
	}
	return y;
}

struct RECTANGLE *draw_ingame_text(void)
{
	rect_ingame_text = empty_rect;
	legacy_u16 fps_characters = frame_hires_prepare_text();
	if (idle_expired != 0) {
		draw_centered_ingame_resource("dm1", DEMO_TEXT_FIRST_Y);
		draw_centered_ingame_resource("dm2", DEMO_TEXT_SECOND_Y);
		return &rect_ingame_text;
	}

	if (game_replay_mode != REPLAY_MODE_LIVE) {
		legacy_s16 replay_y = REPLAY_TEXT_Y;
		if (replay_filename[0] != 0) {
			replay_y = draw_replay_filename(fps_characters);
		}
		if (game_replay_mode != REPLAY_MODE_PLAYBACK) {
			return &rect_ingame_text;
		}
		legacy_u16 replay_frame = (legacy_u16)state.game_frame % framespersec;
		if (replay_frame >= (legacy_u16)LEGACY_S16_SAR(framespersec, 1U)) {
			return &rect_ingame_text;
		}
		copy_string(&resID_byte1, locate_text_res(gameresptr, "rpl"));
		draw_replay_text(&resID_byte1, replay_y);
		return &rect_ingame_text;
	}

	if (state.game_inputmode == GAME_INPUT_MODE_WAITING) {
		draw_centered_ingame_resource("pre", PREPARE_TEXT_Y);
		return &rect_ingame_text;
	}
	if (passed_security == 0) {
		draw_centered_ingame_resource("se1", SECURITY_TEXT_FIRST_Y);
		draw_centered_ingame_resource("se2", SECURITY_TEXT_SECOND_Y);
		return &rect_ingame_text;
	}
	if (followOpponentFlag != 0 || cameramode != CAMERA_MODE_COCKPIT ||
		state.playerstate.car_crashBmpFlag != CRASH_EVENT_NONE) {
		return &rect_ingame_text;
	}

	draw_ingame_route_information();
	return &rect_ingame_text;
}
