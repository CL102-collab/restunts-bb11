#include "fileio.h"
#ifdef RESTUNTS_AMIGA
#include "../platform/amiga/amiga.h"
#endif
#include "legacy.h"
#include "memmgr.h"
#include "menu_internal.h"
#include "resource.h"
#include "shape2d.h"
#include "timing.h"
#include "ui_text.h"
#include "game_input.h"
#include "ui_input.h"
#include "audio_control.h"
#include "externs.h"
#ifdef RESTUNTS_SDL3
#include "../platform/sdl3/touch_game.h"
#endif

#define INTRO_SCREEN_WIDTH 320
#define INTRO_SCREEN_HEIGHT 200
#define INTRO_SCREEN_COLOR 15
#define INTRO_PRODUCTION_RAISED_WAIT 160
#define INTRO_DEFAULT_PAGE_WAIT 180
#define INTRO_PAGE_INPUT_DELAY 400

#define CREDITS_RESOURCE_COUNT 11
#define CREDITS_BACKGROUND_INDEX 0
#define CREDITS_ARROW_INDEX 1
#define CREDITS_FIRST_ANIMATION_INDEX 2U
#define CREDITS_ANIMATION_END_INDEX 10U
#define CREDITS_CLOSING_INDEX 10
#define CREDITS_INITIAL_WAIT 150
#define CREDITS_LEFT_COLUMN_X 20
#define CREDITS_RIGHT_COLUMN_X 172
#define CREDITS_TITLE_X 120
#define CREDITS_FIRST_LOGO_X 60
#define CREDITS_SECOND_LOGO_X 104
#define CREDITS_TITLE_Y 0
#define CREDITS_FIRST_LOGO_Y 12
#define CREDITS_SECOND_LOGO_Y 20
#define CREDITS_DESIGN_HEADING_Y 32
#define CREDITS_FIRST_DESIGNER_Y 44
#define CREDITS_SECOND_DESIGNER_Y 52
#define CREDITS_THIRD_DESIGNER_Y 60
#define CREDITS_FOURTH_DESIGNER_Y 68
#define CREDITS_FIFTH_DESIGNER_Y 76
#define CREDITS_MUSIC_HEADING_Y 92
#define CREDITS_FIRST_MUSICIAN_Y 104
#define CREDITS_SECOND_MUSICIAN_Y 112
#define CREDITS_THIRD_MUSICIAN_Y 120
#define CREDITS_PRODUCTION_HEADING_Y 32
#define CREDITS_PRODUCER_Y 44
#define CREDITS_OPPONENT_HEADING_Y 56
#define CREDITS_FIRST_OPPONENT_Y 64
#define CREDITS_SECOND_OPPONENT_Y 72
#define CREDITS_ART_HEADING_Y 84
#define CREDITS_FIRST_ARTIST_Y 96
#define CREDITS_SECOND_ARTIST_Y 104
#define CREDITS_THIRD_ARTIST_Y 112
#define CREDITS_FOURTH_ARTIST_Y 120
#define CREDITS_ARROW_START_X 330
#define CREDITS_ARROW_SPEED 2
#define CREDITS_ARROW_ERASE_WIDTH 32
#define CREDITS_ANIMATION_INTERVAL 5
#define CREDITS_END_INPUT_DELAY 500

enum CREDITS_LINE_TYPE { CREDITS_LINE_SHAPE = 0, CREDITS_LINE_TEXT = 1 };

static void far *ui_temp_resource;

#if defined(RESTUNTS_SDL3) && !defined(__DJGPP__)
#define INTRO_AUDIO_NOTICE_LINE_CAPACITY 32
#define INTRO_AUDIO_NOTICE_TOP 132

static void intro_draw_native_audio_notice(void)
{
	static legacy_s8 lines[][INTRO_AUDIO_NOTICE_LINE_CAPACITY] = {
		"Nuked OPL2 Lite", "Copyright (C) 2026 Nuke.YKT.", "LGPL-2.1-or-later",
		"See share/licenses/restunts/", "Nuked-OPL2-LICENSE"};
	const legacy_s16 notice_x = 8;
	const legacy_s16 title_text_color = 77;
	legacy_s16 notice_y = INTRO_AUDIO_NOTICE_TOP;
	const legacy_s16 line_height = 8;
	legacy_u8 far *saved_font = active_font_definition;
	legacy_u8 far *notice_font = (legacy_u8 far *)fontnptr;
	legacy_s16 saved_color = LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(notice_font));
	legacy_s16 saved_background =
		LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(notice_font + LEGACY_WORD_BYTES));
	font_set_fontdef2(notice_font);
	/* The narrow font fits beside the title's car and above its copyright. */
	for (legacy_u16 line = 0; line < sizeof(lines) / sizeof(lines[0]); ++line) {
		intro_draw_text(lines[line], notice_x, notice_y, title_text_color, 0);
		notice_y += line_height;
	}
	font_set_colors(saved_color, saved_background);
	font_set_fontdef2(saved_font);
}
#endif

static legacy_s16 run_intro(void far *audio_resource)
{
#ifdef RESTUNTS_AMIGA
	amiga_video_palette_scene("SDTITL", "!pa0");
#endif
	mouse_draw_opaque_check();
	sprite_select_screen_and_clear();
	mouse_draw_transparent_check();
	sprite_select_render_window_and_clear();

	struct SHAPE2D far *shape =
		(struct SHAPE2D far *)locate_shape_fatal((legacy_s8 far *)ui_temp_resource, "prod");
	waitflag =
		shape2d_get_pos_y(shape) != 0 ? INTRO_PRODUCTION_RAISED_WAIT : INTRO_DEFAULT_PAGE_WAIT;

	shape = (struct SHAPE2D far *)locate_shape_fatal((legacy_s8 far *)ui_temp_resource, "prod");
	sprite_shape_to_1_alt(shape);
	legacy_s16 result = sprite_blit_to_video(render_window_sprite, -1);
	/* Finish title loading and drawing before music starts: DOS sends timer-driven
	 * notes directly to OPL, so a delayed first timer pump shortens the opening. */
	load_audio_finalize(audio_resource);
	if (result == 0) {
		result = input_repeat_check(INTRO_PAGE_INPUT_DELAY);
	}

	if (result == 0) {
#ifdef RESTUNTS_AMIGA
		amiga_video_palette_scene("SDTITL", "!pa1");
#endif
		sprite_select_render_window_and_clear();
		waitflag = INTRO_DEFAULT_PAGE_WAIT;
		shape = (struct SHAPE2D far *)locate_shape_fatal((legacy_s8 far *)ui_temp_resource, "titl");
		sprite_shape_to_1_alt(shape);
#if defined(RESTUNTS_SDL3) && !defined(__DJGPP__)
		intro_draw_native_audio_notice();
#endif
		result = sprite_blit_to_video(render_window_sprite, -1);
		if (result == 0) {
			result = input_repeat_check(INTRO_PAGE_INPUT_DELAY);
		}
	}

	return result;
}

legacy_s16 run_intro_looped(void)
{
#ifdef RESTUNTS_SDL3
	sdl3_touch_set_intro_active(1);
#endif
	void far *audio_resource = file_prepare_audiores("skidtitl", "skidms", "TITL");
	ui_temp_resource = file_load_resource(FILE_RESOURCE_SHAPE2D, "sdtitl");
	render_window_sprite =
		sprite_make_wnd(INTRO_SCREEN_WIDTH, INTRO_SCREEN_HEIGHT, INTRO_SCREEN_COLOR);
	legacy_s16 result = run_intro(audio_resource);
	sprite_free_wnd(render_window_sprite);
	mmgr_free((legacy_s8 far *)ui_temp_resource);

	if (result == 0) {
		result = setup_intro();
		if (result == 0) {
#ifdef RESTUNTS_AMIGA
			amiga_video_palette_scene("SDCRED", "!pal");
#endif
			ui_temp_resource = file_load_resource(FILE_RESOURCE_SHAPE2D, "sdcred");
			render_window_sprite =
				sprite_make_wnd(INTRO_SCREEN_WIDTH, INTRO_SCREEN_HEIGHT, INTRO_SCREEN_COLOR);
			sprite_select_render_window_and_clear();
			sprite_blit_to_video(render_window_sprite, 0);
			result = load_intro_resources();
			sprite_free_wnd(render_window_sprite);
			mmgr_free((legacy_s8 far *)ui_temp_resource);
		}
	}

	audio_unload();
#ifdef RESTUNTS_SDL3
	sdl3_touch_set_intro_active(0);
#endif
	return result;
}

static void intro_draw_resource_line(legacy_s8 far *resource, legacy_s8 *resource_id,
									 legacy_s16 is_text, legacy_s16 x, legacy_s16 y,
									 legacy_s16 color, legacy_s16 shadow_color)
{
	legacy_s8 far *text;

	if (is_text != 0) {
		text = locate_text_res(resource, resource_id);
	} else {
		text = locate_shape_alt(resource, resource_id);
	}
	copy_string(&resID_byte1, text);
	intro_draw_text(&resID_byte1, x, y, color, shadow_color);
}

static void credits_draw_title(legacy_s8 far *credit_resource)
{
	intro_draw_resource_line(credit_resource, credits_title_id, CREDITS_LINE_TEXT, CREDITS_TITLE_X,
							 CREDITS_TITLE_Y, credits_title_color, credits_title_shadow_color);
	intro_draw_resource_line(credit_resource, credits_first_logo_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_FIRST_LOGO_X, CREDITS_FIRST_LOGO_Y, credits_text_color,
							 credits_text_shadow_color);
	intro_draw_resource_line(credit_resource, credits_second_logo_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_SECOND_LOGO_X, CREDITS_SECOND_LOGO_Y, credits_text_color,
							 credits_text_shadow_color);
}

static void credits_draw_designers(legacy_s8 far *credit_resource)
{
	intro_draw_resource_line(credit_resource, credits_design_heading_id, CREDITS_LINE_TEXT,
							 CREDITS_LEFT_COLUMN_X, CREDITS_DESIGN_HEADING_Y,
							 credits_design_heading_color, credits_design_heading_shadow_color);
	intro_draw_resource_line(credit_resource, credits_first_designer_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_LEFT_COLUMN_X, CREDITS_FIRST_DESIGNER_Y, credits_text_color,
							 credits_text_shadow_color);
	intro_draw_resource_line(credit_resource, credits_second_designer_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_LEFT_COLUMN_X, CREDITS_SECOND_DESIGNER_Y, credits_text_color,
							 credits_text_shadow_color);
	intro_draw_resource_line(credit_resource, credits_third_designer_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_LEFT_COLUMN_X, CREDITS_THIRD_DESIGNER_Y, credits_text_color,
							 credits_text_shadow_color);
	intro_draw_resource_line(credit_resource, credits_fourth_designer_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_LEFT_COLUMN_X, CREDITS_FOURTH_DESIGNER_Y, credits_text_color,
							 credits_text_shadow_color);
	intro_draw_resource_line(credit_resource, credits_fifth_designer_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_LEFT_COLUMN_X, CREDITS_FIFTH_DESIGNER_Y, credits_text_color,
							 credits_text_shadow_color);
}

static void credits_draw_musicians(legacy_s8 far *credit_resource)
{
	intro_draw_resource_line(credit_resource, credits_music_heading_id, CREDITS_LINE_TEXT,
							 CREDITS_LEFT_COLUMN_X, CREDITS_MUSIC_HEADING_Y,
							 credits_music_heading_color, credits_music_heading_shadow_color);
	intro_draw_resource_line(credit_resource, credits_first_musician_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_LEFT_COLUMN_X, CREDITS_FIRST_MUSICIAN_Y, credits_text_color,
							 credits_text_shadow_color);
	intro_draw_resource_line(credit_resource, credits_second_musician_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_LEFT_COLUMN_X, CREDITS_SECOND_MUSICIAN_Y, credits_text_color,
							 credits_text_shadow_color);
	intro_draw_resource_line(credit_resource, credits_third_musician_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_LEFT_COLUMN_X, CREDITS_THIRD_MUSICIAN_Y, credits_text_color,
							 credits_text_shadow_color);
}

static void credits_draw_production(legacy_s8 far *credit_resource)
{
	intro_draw_resource_line(credit_resource, credits_production_heading_id, CREDITS_LINE_TEXT,
							 CREDITS_RIGHT_COLUMN_X, CREDITS_PRODUCTION_HEADING_Y,
							 credits_production_heading_color,
							 credits_production_heading_shadow_color);
	intro_draw_resource_line(credit_resource, credits_producer_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_RIGHT_COLUMN_X, CREDITS_PRODUCER_Y, credits_text_color,
							 credits_text_shadow_color);
	intro_draw_resource_line(credit_resource, credits_opponent_heading_id, CREDITS_LINE_TEXT,
							 CREDITS_RIGHT_COLUMN_X, CREDITS_OPPONENT_HEADING_Y,
							 credits_production_heading_color,
							 credits_production_heading_shadow_color);
	intro_draw_resource_line(credit_resource, credits_first_opponent_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_RIGHT_COLUMN_X, CREDITS_FIRST_OPPONENT_Y, credits_text_color,
							 credits_text_shadow_color);
	intro_draw_resource_line(credit_resource, credits_second_opponent_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_RIGHT_COLUMN_X, CREDITS_SECOND_OPPONENT_Y, credits_text_color,
							 credits_text_shadow_color);
}

static void credits_draw_artists(legacy_s8 far *credit_resource)
{
	intro_draw_resource_line(credit_resource, credits_art_heading_id, CREDITS_LINE_TEXT,
							 CREDITS_RIGHT_COLUMN_X, CREDITS_ART_HEADING_Y,
							 credits_art_heading_color, credits_art_heading_shadow_color);
	intro_draw_resource_line(credit_resource, credits_first_artist_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_RIGHT_COLUMN_X, CREDITS_FIRST_ARTIST_Y, credits_text_color,
							 credits_text_shadow_color);
	intro_draw_resource_line(credit_resource, credits_second_artist_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_RIGHT_COLUMN_X, CREDITS_SECOND_ARTIST_Y, credits_text_color,
							 credits_text_shadow_color);
	intro_draw_resource_line(credit_resource, credits_third_artist_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_RIGHT_COLUMN_X, CREDITS_THIRD_ARTIST_Y, credits_text_color,
							 credits_text_shadow_color);
	intro_draw_resource_line(credit_resource, credits_fourth_artist_shape_id, CREDITS_LINE_SHAPE,
							 CREDITS_RIGHT_COLUMN_X, CREDITS_FOURTH_ARTIST_Y, credits_text_color,
							 credits_text_shadow_color);
}

static legacy_s16 credits_slide_arrow(struct SHAPE2D far *arrow_shape, legacy_s16 target_x,
									  legacy_s16 arrow_y, legacy_s16 arrow_width,
									  legacy_s16 arrow_height)
{
	legacy_s16 arrow_x = CREDITS_ARROW_START_X;
	legacy_s16 input = 0;
	for (;;) {
		legacy_s16 frame_elapsed = (legacy_s16)timer_get_delta_alt();
		arrow_x =
			LEGACY_S16_WRAP_SUB(arrow_x, LEGACY_S16_WRAP_MUL(frame_elapsed, CREDITS_ARROW_SPEED));
		if (target_x > arrow_x) {
			break;
		}
		mouse_draw_opaque_check();
		sprite_copy_image_at(arrow_shape, arrow_x, arrow_y);
		sprite_fill_rect_clipped(LEGACY_S16_WRAP_ADD(arrow_width, arrow_x), arrow_y,
								 CREDITS_ARROW_ERASE_WIDTH, arrow_height, 0);
		mouse_draw_transparent_check();
		input = (legacy_s16)input_do_checking(frame_elapsed);
		if (input != 0) {
			break;
		}
	}

	return input;
}

static void credits_animate_shapes(struct SHAPE2D far **credit_shapes, legacy_s16 arrow_y,
								   legacy_s16 input)
{
	legacy_s16 animation_elapsed = 0;
	legacy_s16 animation_target = 0;
	for (legacy_u16 animation_index = CREDITS_FIRST_ANIMATION_INDEX;
		 animation_index < CREDITS_ANIMATION_END_INDEX && input == 0; animation_index++) {
		sprite_select_render_window();
		sprite_set_target_clip_bounds(0, INTRO_SCREEN_WIDTH, arrow_y, INTRO_SCREEN_HEIGHT);
		sprite_clear_target(0);
		sprite_shape_to_1_alt(credit_shapes[animation_index]);
		sprite_select_screen_compat();
		sprite_set_target_clip_bounds(0, INTRO_SCREEN_WIDTH, arrow_y, INTRO_SCREEN_HEIGHT);
		mouse_draw_opaque_check();
		sprite_putimage(render_window_sprite->sprite_bitmapptr);
		mouse_draw_transparent_check();
		animation_target = LEGACY_S16_WRAP_ADD(animation_target, CREDITS_ANIMATION_INTERVAL);
		while (animation_target > animation_elapsed) {
			legacy_s16 frame_elapsed = (legacy_s16)timer_get_delta_alt();
			input = (legacy_s16)input_do_checking(frame_elapsed);
			animation_elapsed = LEGACY_S16_WRAP_ADD(animation_elapsed, frame_elapsed);
		}
	}
}

static legacy_s8 credits_present_closing(struct SHAPE2D far **credit_shapes, legacy_s16 arrow_y)
{
	sprite_set_target_clip_bounds(0, INTRO_SCREEN_WIDTH, 0, INTRO_SCREEN_HEIGHT);
	mouse_draw_opaque_check();
	sprite_clear_shape(render_window_sprite->sprite_bitmapptr);
	sprite_select_render_window();
	sprite_set_target_clip_bounds(0, INTRO_SCREEN_WIDTH, arrow_y, INTRO_SCREEN_HEIGHT);
	sprite_clear_target(0);
	sprite_shape_to_1_alt(credit_shapes[CREDITS_BACKGROUND_INDEX]);
	sprite_shape_to_1_alt(credit_shapes[CREDITS_CLOSING_INDEX]);
	if (sprite_blit_to_video(render_window_sprite, 0) != 0) {
		return 1;
	}
	return input_repeat_check(CREDITS_END_INPUT_DELAY) != 0;
}

legacy_s8 load_intro_resources(void)
{
	legacy_s8 far *credit_resource = (legacy_s8 far *)file_load_resfile(credits_resource_name);
	struct SHAPE2D far *credit_shapes[CREDITS_RESOURCE_COUNT];
	locate_many_resources((legacy_s8 far *)ui_temp_resource, credits_shape_ids,
						  (legacy_s8 far **)credit_shapes);
	waitflag = CREDITS_INITIAL_WAIT;
	sprite_select_render_window_and_clear();
	struct SHAPE2D far *arrow_shape = credit_shapes[CREDITS_ARROW_INDEX];
	legacy_s16 target_x = (legacy_s16)shape2d_get_pos_x(arrow_shape);
	legacy_s16 arrow_y = (legacy_s16)shape2d_get_pos_y(arrow_shape);
	legacy_s16 arrow_width =
		LEGACY_S16_WRAP_MUL((legacy_s16)shape2d_get_width(arrow_shape), video_shape_width_scale);
	legacy_s16 arrow_height = (legacy_s16)shape2d_get_height(arrow_shape);

	credits_draw_title(credit_resource);
	credits_draw_designers(credit_resource);
	credits_draw_musicians(credit_resource);
	credits_draw_production(credit_resource);
	credits_draw_artists(credit_resource);
	unload_resource(credit_resource);

	(void)sprite_blit_to_video(render_window_sprite, -1);
	sprite_select_screen_compat();
	(void)timer_get_delta_alt();
	legacy_s16 input =
		credits_slide_arrow(arrow_shape, target_x, arrow_y, arrow_width, arrow_height);
	arrow_y = (legacy_s16)shape2d_get_pos_y(credit_shapes[CREDITS_BACKGROUND_INDEX]);
	credits_animate_shapes(credit_shapes, arrow_y, input);
	return credits_present_closing(credit_shapes, arrow_y);
}
