#include "frame_internal.h"
#include "frame_fps.h"
#include "../game_input.h"
#include "../ui/ui_text.h"
#include "../shape2d/shape2d.h"
#include "../shape2d/shape2d_internal.h"
#include "../race/race_stats.h"
#include "../camera.h"
#include "../video/video_frame.h"
#include "../skybox.h"
#include "../video/video_color.h"
#include "../race/race_graphics.h"
#include "../externs.h"
#include "../memmgr.h"
#include "../resource.h"
#include "../crash_state.h"
#include "../platform.h"
#include "../dashboard.h"

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
#define INGAME_TEXT_SHADOW_HEIGHT 1
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
	VIDEO_MATERIAL_BEGIN(SKYBOX_MATERIAL_WATER);
	sprite_clear_target((legacy_u8)skybox.water_color);
	VIDEO_DIRECT_END();
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
							 LEGACY_S16_WRAP_ADD(scaled_end_y, top),
							 VIDEO_UI_COLOR(dialog_fnt_colour));

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
	rect_union(&rect_ingame_text,
			   intro_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1), y,
							   VIDEO_UI_COLOR(dialog_fnt_colour), 0),
			   &rect_ingame_text);
}

static struct SHAPE2D far *route_direction_shape(legacy_s8 indicator)
{
	switch (indicator) {
		case ROUTE_INDICATOR_LEFT:
			return sdgame2shapes[DIRECTION_ICON_LEFT_SHAPE];
		case ROUTE_INDICATOR_RIGHT:
			return sdgame2shapes[DIRECTION_ICON_RIGHT_SHAPE];
		default:
			return 0;
	}
}

static void include_route_extent(legacy_s32 y, legacy_s32 height, legacy_s32 *top,
								 legacy_s32 *bottom)
{
	if (y < *top) {
		*top = y;
	}
	if (y + height > *bottom) {
		*bottom = y + height;
	}
}

static void draw_route_direction(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	struct RECTANGLE bounds;
	bounds.left = x;
	bounds.right = (legacy_s16)(x + shape2d_get_width(shape));
	bounds.top = y;
	bounds.bottom = (legacy_s16)(y + shape2d_get_height(shape));
	sprite_putimage_transparent(shape, x, y);
	rect_union(&rect_ingame_text, &bounds, &rect_ingame_text);
}

static void draw_ingame_route_information(void)
{
	struct SHAPE2D far *player_arrow = route_direction_shape(state.game_player_route_indicator);
	struct SHAPE2D far *opponent_arrow = route_direction_shape(state.game_opponent_route_indicator);
	legacy_s32 text_height = (legacy_s32)font_glyph_height + INGAME_TEXT_SHADOW_HEIGHT;
	legacy_s32 top = OVERLAY_REFERENCE_HEIGHT;
	legacy_s32 bottom = 0;
	if (player_arrow != 0) {
		include_route_extent(DIRECTION_ICON_CENTER_Y, shape2d_get_height(player_arrow), &top,
							 &bottom);
	} else if (state.game_player_route_indicator == ROUTE_INDICATOR_WRONG_WAY) {
		include_route_extent(WRONG_WAY_TEXT_Y, text_height, &top, &bottom);
	}
	if (opponent_arrow != 0) {
		include_route_extent(OPPONENT_ICON_Y, shape2d_get_height(opponent_arrow), &top, &bottom);
		include_route_extent(OPPONENT_TEXT_Y, text_height, &top, &bottom);
	}
	if (show_penalty_counter != 0) {
		include_route_extent(PENALTY_TEXT_Y, text_height, &top, &bottom);
	}

	legacy_s32 viewport_top = drawing_sprite.sprite_top;
	legacy_s32 viewport_bottom = drawing_sprite.sprite_bottom;
	legacy_s32 group_height = bottom - top;
	legacy_s32 viewport_height = viewport_bottom - viewport_top;
	if (group_height <= 0 || group_height > viewport_height) {
		return;
	}
	/* Keep every active route notice, including text shadows, in the refreshed
	 * windshield. Bitmaps clip at its edge, but font pixels can otherwise spill
	 * into the cached cockpit and survive after the notice disappears. Move the
	 * group together to preserve the original spacing between its messages. */
	legacy_s32 offset = bottom > viewport_bottom ? viewport_bottom - bottom : 0;
	if (top + offset < viewport_top) {
		offset = viewport_top - top;
	}

	if (player_arrow != 0) {
		draw_route_direction(player_arrow, DIRECTION_ICON_CENTER_X,
							 (legacy_s16)(DIRECTION_ICON_CENTER_Y + offset));
	} else if (state.game_player_route_indicator == ROUTE_INDICATOR_WRONG_WAY) {
		draw_centered_ingame_resource("www", (legacy_s16)(WRONG_WAY_TEXT_Y + offset));
	}
	if (opponent_arrow != 0) {
		legacy_s16 x = state.game_opponent_route_indicator == ROUTE_INDICATOR_LEFT
						   ? OPPONENT_LEFT_ICON_X
						   : OPPONENT_RIGHT_ICON_X;
		draw_route_direction(opponent_arrow, x, (legacy_s16)(OPPONENT_ICON_Y + offset));
		draw_centered_ingame_resource("opp", (legacy_s16)(OPPONENT_TEXT_Y + offset));
	}
	if (show_penalty_counter != 0) {
		copy_string(&resID_byte1, locate_text_res(gameresptr, "pen"));
		format_frame_as_string(&resID_byte1 + strlen(&resID_byte1), penalty_time, 0);
		rect_union(&rect_ingame_text,
				   intro_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1),
								   (legacy_s16)(PENALTY_TEXT_Y + offset),
								   VIDEO_UI_COLOR(dialog_fnt_colour), 0),
				   &rect_ingame_text);
	}
}

static void draw_replay_text(legacy_s8 *text, legacy_s16 y)
{
	legacy_s16 x = LEGACY_S16_WRAP_SUB(
		REPLAY_TEXT_RIGHT_X, LEGACY_U16_WRAP_MUL(strlen(text), REPLAY_TEXT_CHARACTER_WIDTH));
	rect_union(&rect_ingame_text, intro_draw_text(text, x, y, VIDEO_UI_COLOR(dialog_fnt_colour), 0),
			   &rect_ingame_text);
}

static legacy_s16 draw_replay_filename(legacy_u16 reserved_characters)
{
	const legacy_s8 *filename = replay_filename;
	legacy_s16 y = REPLAY_FILENAME_Y;

	while (*filename != 0) {
		legacy_u16 reserved = y == REPLAY_FILENAME_Y ? reserved_characters : 0;
		reserved = frame_fps_filename_reserved(y, reserved);
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
	legacy_u16 fps_characters = frame_fps_prepare_text();
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
