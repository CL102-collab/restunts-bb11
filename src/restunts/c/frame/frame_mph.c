#include "frame_mph.h"
#include "frame_fps.h"
#include "../game_input.h"
#include "../ui/ui_text.h"
#include "../shape2d/shape2d.h"
#include "../shape2d/shape2d_internal.h"
#include "../externs.h"
#include "../platform.h"
#include "../video/video_frame.h"
#include "../memmgr.h"

enum {
	MPH_TEXT_SCREEN_WIDTH = 320,
	MPH_TEXT_SCREEN_HEIGHT = 200,
	MPH_TEXT_BOTTOM_MARGIN = 3,
	MPH_TEXT_SHADOW_SIZE = 1,
	MPH_TEXT_MAX_DIGITS = 3,
	MPH_TEXT_NUMBER_BASE = 10,
	MPH_TEXT_SPEED_SHIFT = LEGACY_BYTE_BITS,
	MPH_TEXT_SHADOW_COLOR = 0,
	MPH_TEXT_BUFFER_SIZE = MPH_TEXT_MAX_DIGITS + sizeof(" MPH"),
	MPH_TEXT_PAGE_COUNT = 2,
	MPH_TEXT_SPRITE_FLAGS = 15,
	MPH_TEXT_PARAGRAPH_BYTES = 16
};

static struct SPRITE far *mph_background[MPH_TEXT_PAGE_COUNT];
static struct SPRITE mph_target[MPH_TEXT_PAGE_COUNT];
static legacy_u8 mph_background_valid[MPH_TEXT_PAGE_COUNT];
static legacy_u16 mph_background_width;
static legacy_u16 mph_background_height;
static legacy_u16 mph_page_count;

static legacy_u16 frame_mph_format_number(legacy_s8 *text, legacy_u16 value)
{
	legacy_s8 digits[MPH_TEXT_MAX_DIGITS];
	legacy_u16 count = 0;
	do {
		digits[count++] = (legacy_s8)('0' + value % MPH_TEXT_NUMBER_BASE);
		value /= MPH_TEXT_NUMBER_BASE;
	} while (value != 0);
	for (legacy_u16 i = 0; i < count; i++) {
		text[i] = digits[count - i - 1U];
	}
	text[count] = 0;
	return count;
}

legacy_s16 frame_mph_initialize(void)
{
	/* Match the font selected for race HUD text, rather than assuming a fixed
	 * character advance. Custom fonts can give every digit a different width. */
	font_set_fontdef();
	legacy_s16 maximum_width = 0;
	legacy_s8 text[MPH_TEXT_BUFFER_SIZE];
	for (legacy_u16 speed = 0; speed <= (LEGACY_U16_MAX >> MPH_TEXT_SPEED_SHIFT); speed++) {
		legacy_u16 count = frame_mph_format_number(text, speed);
		copy_string(text + count, " MPH");
		legacy_s16 width = font_text_width(text);
		if (width > maximum_width) {
			maximum_width = width;
		}
	}
	mph_background_width = maximum_width + MPH_TEXT_SHADOW_SIZE;
	mph_background_height = font_glyph_height + MPH_TEXT_SHADOW_SIZE;
	if (mph_background_width > MPH_TEXT_SCREEN_WIDTH ||
		mph_background_height > MPH_TEXT_SCREEN_HEIGHT || maximum_width <= 0) {
		return 1;
	}
	legacy_u16 pages = video_uses_page_flipping != 0 ? MPH_TEXT_PAGE_COUNT : 1;
	/* sprite_make_wnd rounds down to paragraphs, then reserves one extra. Check
	 * both pages before allocating so race setup can report insufficient memory. */
	legacy_u32 paragraphs =
		((legacy_u32)mph_background_width * mph_background_height + SHAPE2D_HEADER_SIZE) /
			MPH_TEXT_PARAGRAPH_BYTES +
		1U;
	if (mmgr_get_res_ofs_diff_scaled() < paragraphs * MPH_TEXT_PARAGRAPH_BYTES * pages) {
		return 1;
	}
	mph_page_count = pages;
	for (legacy_u16 page = 0; page < mph_page_count; page++) {
		mph_background[page] =
			sprite_make_wnd(mph_background_width, mph_background_height, MPH_TEXT_SPRITE_FLAGS);
		mph_background_valid[page] = 0;
	}
	return 0;
}

void frame_mph_shutdown(void)
{
	while (mph_page_count != 0) {
		legacy_u16 page = --mph_page_count;
		/* Capture uses the measured text size; freeing uses the allocated row table. */
		shape2d_put_word((legacy_u8 far *)mph_background[page]->sprite_bitmapptr +
							 offsetof(struct SHAPE2D, height),
						 mph_background_height);
		sprite_free_wnd(mph_background[page]);
		mph_background[page] = 0;
		mph_background_valid[page] = 0;
	}
}

static legacy_u16 frame_mph_page(void)
{
	return video_uses_page_flipping != 0 ? (legacy_u16)frame_buffer_index : 0;
}

void frame_mph_restore_text(void)
{
	legacy_u16 page = frame_mph_page();
	if (page >= mph_page_count || mph_background_valid[page] == 0) {
		return;
	}
	mph_background_valid[page] = 0;
	struct SPRITE saved_context[SPRITE_STATE_COUNT];
	sprite_save_context(saved_context);
	/* Without page flipping, frame entry targets the render window while the
	 * completed HUD lives on the display. Restore exactly the captured target. */
	sprite_select_target(&mph_target[page]);
	mouse_draw_opaque_check();
	sprite_shape_to_1_alt(mph_background[page]->sprite_bitmapptr);
	mouse_draw_transparent_check();
	sprite_restore_context(saved_context);
}

struct RECTANGLE *frame_mph_draw_text(legacy_s16 screen_bottom)
{
	legacy_u16 page = frame_mph_page();
	if (mph_display_enabled == FRAME_MPH_DISPLAY_OFF || idle_expired != 0 ||
		page >= mph_page_count) {
		return &empty_rect;
	}
	legacy_s8 text[MPH_TEXT_BUFFER_SIZE];
	/* Actual road speed is Q8 MPH; engine/rev speed also includes wheelspin. */
	legacy_u16 speed = state.playerstate.car_actual_speed >> MPH_TEXT_SPEED_SHIFT;
	legacy_u16 count = frame_mph_format_number(text, speed);
	copy_string(text + count, " MPH");
	legacy_s16 width = font_text_width(text);
	legacy_s16 x = REPLAY_TEXT_RIGHT_X - width;
	legacy_s16 bottom = screen_bottom - MPH_TEXT_BOTTOM_MARGIN;
	legacy_s16 y = bottom - font_glyph_height - MPH_TEXT_SHADOW_SIZE;
	legacy_u16 height = font_glyph_height + MPH_TEXT_SHADOW_SIZE;
	if (x < 0 || y < 0 || width + MPH_TEXT_SHADOW_SIZE > mph_background_width ||
		height > mph_background_height) {
		return &empty_rect;
	}
	struct SPRITE saved_context[SPRITE_STATE_COUNT];
	sprite_save_context(saved_context);
	sprite_set_target_clip_bounds(0, MPH_TEXT_SCREEN_WIDTH, 0, MPH_TEXT_SCREEN_HEIGHT);
	mph_target[page] = drawing_sprite;
	struct SHAPE2D far *background = mph_background[page]->sprite_bitmapptr;
	shape2d_put_word((legacy_u8 far *)background + offsetof(struct SHAPE2D, width),
					 width + MPH_TEXT_SHADOW_SIZE);
	shape2d_put_word((legacy_u8 far *)background + offsetof(struct SHAPE2D, height), height);
	/* Save the composed scene/dashboard, including shadow pixels. Restoring it
	 * before the next frame removes old digits without painting a background. */
	sprite_clear_shape_alt(background, x, y);
	mph_background_valid[page] = 1;
	struct RECTANGLE *bounds =
		intro_draw_text(text, x, y, dialog_fnt_colour, MPH_TEXT_SHADOW_COLOR);
	sprite_restore_context(saved_context);
	return bounds;
}
