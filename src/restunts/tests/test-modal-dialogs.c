#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../c/externs.h"
#include "../c/game_input.h"
#include "../c/keyboard.h"
#include "../c/platform.h"
#include "../c/shape2d.h"
#include "../c/ui_text.h"
#include "../c/ui_dialog_internal.h"
#include "../c/ui_dialog.h"

#undef printf
#undef strcmp
#undef strlen

void far *mainresptr;
legacy_s8 pause_dialog_id[4] = "pau";
legacy_s8 disk_retry_dialog_id[4] = "dea";
legacy_s8 disk_error_dialog_id[4] = "der";
legacy_s8 missing_disk1_message_id[4] = "id1";
legacy_s8 missing_disk2_message_id[4] = "id2";
legacy_s8 missing_disk3_message_id[4] = "id3";
legacy_s8 missing_disk4_message_id[4] = "id4";
legacy_s8 g_is_busy;
legacy_s16 font_glyph_height = 8;
legacy_s16 dialog_fnt_colour = 7;
legacy_s16 dialog_background_color;
legacy_s16 performGraphColor;
legacy_u16 dialog_border_color = 4;

static legacy_u32 background_depth, input_depth, polls, releases, restored;
static legacy_s16 timer_suspended, audio_suspended, save_succeeds, dismiss_key, is_pause;

void input_push_status(void)
{
	assert(input_depth++ == 0);
}

void input_pop_status(void)
{
	assert(input_depth == 1 && background_depth == 0);
	assert(timer_suspended == 0 && audio_suspended == 0);
	input_depth--;
}

void dos_timer_set_callbacks_suspended(legacy_s16 suspended)
{
	assert(input_depth == 1);
	if (suspended == 0) {
		assert(background_depth == 0);
	}
	timer_suspended = suspended;
}

void audio_suspend(void)
{
	assert(timer_suspended == 1 && audio_suspended == 0);
	audio_suspended = 1;
}

void audio_resume(void)
{
	assert(timer_suspended == 0 && background_depth == 0 && audio_suspended == 1);
	audio_suspended = 0;
}

legacy_s8 far *locate_text_res(legacy_s8 far *resource, const legacy_s8 *name)
{
	assert((void far *)resource == mainresptr);
	assert(strcmp((const legacy_char *)name, is_pause != 0 ? "pau" : "der") == 0);
	return (legacy_s8 far *)(is_pause != 0 ? "Game paused]Press any key"
										   : "Disk error]Press any key");
}

legacy_s16 sprite_push_background(legacy_s16 left, legacy_s16 right, legacy_s16 top,
								  legacy_s16 bottom)
{
	assert(left < right && top < bottom);
	assert(timer_suspended == is_pause && audio_suspended == is_pause && background_depth == 0);
	if (save_succeeds != 0) {
		background_depth++;
	}
	return save_succeeds;
}

void sprite_pop_background(void)
{
	assert(background_depth == 1 && timer_suspended == is_pause && audio_suspended == is_pause);
	assert(polls == 3 && releases == 1);
	background_depth--;
	restored++;
}

legacy_s16 input_checking(legacy_s16 elapsed)
{
	assert(elapsed == 1 && background_depth == 1);
	assert(timer_suspended == is_pause && audio_suspended == is_pause && input_depth == 1);
	assert(polls < 3);
	return ++polls == 3 ? dismiss_key : 0;
}

void check_input(void)
{
	assert(polls == 3 && background_depth == 1);
	releases++;
}

legacy_u32 timer_get_delta_alt(void)
{
	return 1;
}

legacy_u32 slow_timer_wait_ticks(legacy_u32 ticks)
{
	assert(0);
	return ticks;
}

legacy_s16 font_text_width(const legacy_s8 *text)
{
	return (legacy_s16)strlen((const legacy_char *)text) * 8;
}

void font_set_colors(legacy_s16 color, legacy_s16 background)
{
	assert(color == dialog_fnt_colour && background == 0);
}

void font_draw_text_opaque(const legacy_s8 *text, legacy_s16 x, legacy_s16 y)
{
	(void)text;
	(void)x;
	(void)y;
	assert(background_depth == 1);
}

void sprite_select_screen(void)
{
}

void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	assert(left < right && top < bottom);
}

void sprite_clear_target(legacy_u8 color)
{
	assert(color == 0 && background_depth == 1);
}

void sprite_draw_rect_outline(legacy_s16 left, legacy_s16 top, legacy_s16 right, legacy_s16 bottom,
							  legacy_s16 color)
{
	assert(left < right && top < bottom && color == dialog_border_color);
}

void mouse_draw_opaque_check(void)
{
}

void mouse_draw_transparent_check(void)
{
}

legacy_s16 mouse_multi_hittest(legacy_s16 count, const struct BUTTON_AREA *buttons)
{
	(void)count;
	(void)buttons;
	assert(0);
	return -1;
}

int main(void)
{
	static const legacy_s16 keys[] = {KEY_SPACE, KEY_ENTER, KEY_ESCAPE};
	for (is_pause = 0; is_pause <= 1; is_pause++) {
		for (legacy_u32 pass = 0; pass < 6; pass++) {
			dismiss_key = keys[pass % 3];
			save_succeeds = pass < 3;
			polls = releases = restored = 0;
			if (is_pause != 0) {
				show_pause_dialog();
			} else {
				assert(show_disk_error_dialog() == 1);
			}
			assert(background_depth == 0 && input_depth == 0);
			assert(timer_suspended == 0 && audio_suspended == 0);
			assert(polls == (save_succeeds != 0 ? 3U : 0U));
			assert(releases == (legacy_u32)save_succeeds && restored == (legacy_u32)save_succeeds);
		}
	}
	puts("Modal dialogs wait and restore saved backgrounds (12 scenarios).");
	return 0;
}
