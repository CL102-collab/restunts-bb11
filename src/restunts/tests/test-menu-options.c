#include <string.h>
#define main car_snapshot_main
#define locate_text_res car_fixture_locate_text_res
#include "test-car-menu.c"
#undef main
#undef locate_text_res
#include "../c/audio_control.h"
#include "../c/fatal.h"
#include "../c/ui_dialog_internal.h"
#include "../c/replay_viewer_internal.h"
#ifdef RESTUNTS_SDL3
#include "../platform/sdl3/controller_game.h"
#define TEST_DRIVING_MODE_COUNT 4U
#define TEST_INPUT_CHOICE_COUNT 3U
static enum SDL3_DRIVING_INPUT driving_mode;
static legacy_u16 modal_depth;
static legacy_u16 input_menu_count;
static legacy_s16 input_menu_initial;
static legacy_s8 input_text[] = "Input devices}[ Keyboard][ Joystick][ Mouse]";
static legacy_s8 *input_resource = input_text;

void sdl3_input_set_driving_mode(enum SDL3_DRIVING_INPUT mode)
{
	driving_mode = mode;
}

enum SDL3_DRIVING_INPUT sdl3_input_driving_mode(void)
{
	return driving_mode;
}

void sdl3_input_push_modal(void)
{
	modal_depth++;
}

void sdl3_input_pop_modal(void)
{
	assert(modal_depth != 0);
	modal_depth--;
}
#endif

static legacy_s16 dialog_answers[24];
static legacy_u32 dialog_index, joystick_index, joystick_limit;
static legacy_u8 joystick_enabled;
static legacy_s16 keyboard_stop;
static legacy_s8 graphics_text[] = "[ ]0[ ]1[ ]2[ ]3[ ]4[ ]5[ ]6[ ]7[ ]8";

legacy_s8 *locate_text_res(legacy_s8 *data, const legacy_s8 *name)
{
	trace_word(2000);
	trace_pointer(data);
	trace_text(name);
#ifdef RESTUNTS_SDL3
	if (_strcmp(name, "mid") == 0) {
		return input_resource;
	}
#endif
	return _strcmp(name, graphics_options_dialog_id) == 0 ? graphics_text : (legacy_s8 *)name;
}
void input_push_status(void)
{
	trace_word(2001);
}
void input_pop_status(void)
{
	trace_word(2002);
}
void dos_timer_set_callbacks_suspended(legacy_s16 flag)
{
	trace_word(2003);
	trace_word(flag);
}
void audio_suspend(void)
{
	trace_word(2004);
}
void audio_resume(void)
{
	trace_word(2005);
}
legacy_s16 audio_toggle_music(void)
{
	trace_word(2006);
	return scenario % 2U;
}
legacy_s16 audio_toggle_effects(void)
{
	trace_word(2007);
	return scenario % 2U;
}
legacy_s16 kb_check(void)
{
	trace_word(2008);
	return 0;
}
legacy_s16 kb_read_char(void)
{
	trace_word(2009);
	return keyboard_stop != 0 && joystick_index == joystick_limit ? KEY_ESCAPE : 0;
}
void dos_joystick_set_enabled(legacy_u8 enabled)
{
	trace_word(2010);
	trace_word(enabled);
	joystick_enabled = enabled;
}
legacy_u8 dos_joystick_is_enabled(void)
{
	trace_word(2011);
	return joystick_enabled;
}
void joystick_reset_calibration(void)
{
	trace_word(2012);
}
legacy_s16 dos_get_joy_flags(void)
{
	trace_word(2013);
	if (joystick_index == joystick_limit) {
		return 16;
	}
	return (legacy_s16)(joystick_index++ / 2U % 9U);
}
legacy_s16 input_direction_from_flags(legacy_s16 flags)
{
	trace_word(2014);
	trace_word(flags);
	return flags;
}
void sprite_pop_background(void)
{
	trace_word(2015);
}
void sprite_fill_rect(legacy_s16 x, legacy_s16 y, legacy_s16 w, legacy_s16 h, legacy_s16 color)
{
	trace_word(2016);
	trace_word(x);
	trace_word(y);
	trace_word(w);
	trace_word(h);
	trace_word(color);
}
legacy_u16 show_dialog(legacy_s16 type, legacy_s16 save, void *text, legacy_u16 x, legacy_u16 y,
					   legacy_s16 border, legacy_s16 *positions, legacy_s16 selected)
{
	trace_word(2017);
	trace_word(type);
	trace_word(save);
	trace_text(text);
	trace_word(x);
	trace_word(y);
	trace_word(border);
	trace_word(selected);
#ifdef RESTUNTS_SDL3
	if (type == DIALOG_TYPE_MENU && text == input_resource) {
		assert(modal_depth != 0);
		assert(selected >= 0 && selected < (legacy_s16)TEST_INPUT_CHOICE_COUNT);
		input_menu_initial = selected;
		input_menu_count++;
	}
#endif
	if (positions != 0) {
		for (legacy_u32 i = 0; i < 15U; i++) {
			positions[i] = LEGACY_S16_FROM_BITS(32750U + i * 43U);
		}
	}
	assert(dialog_index < 24U);
	return (legacy_u16)dialog_answers[dialog_index++];
}
void call_exitlist2(void)
{
	trace_word(2018);
}
void copy_string(legacy_s8 *dst, legacy_s8 *src)
{
	_strcpy(dst, src);
}
legacy_s8 *locate_shape_alt(legacy_s8 *data, const legacy_s8 *name)
{
	trace_word(2019);
	trace_pointer(data);
	trace_text(name);
	return (legacy_s8 *)name;
}
struct RECTANGLE *intro_draw_text(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 color,
								  legacy_s16 mode)
{
	assert(y == 6 || y == 16);
	assert(_strcmp(text, y == 6 ? "Chocolate Stunts" : "Version 0123456 (Feb 12 1991)") == 0);
	trace_word(2020);
	trace_text(text);
	trace_word(x);
	trace_word(y);
	trace_word(color);
	trace_word(mode);
	return 0;
}
legacy_s16 font_centered_text_x(const legacy_s8 *text)
{
	trace_word(2021);
	trace_text(text);
	return 100;
}
legacy_s8 do_fileselect_dialog(legacy_s8 *dir, legacy_s8 *name, legacy_s8 *ext, legacy_s8 *prompt)
{
	trace_word(2022);
	trace_text(dir);
	trace_text(name);
	trace_text(ext);
	trace_text(prompt);
	return scenario % 2U;
}
void show_waiting(void)
{
	trace_word(2023);
	trace_word(waitflag);
}
legacy_s16 file_load_replay(const legacy_s8 *dir, const legacy_s8 *name)
{
	trace_word(2024);
	trace_text(dir);
	trace_text(name);
	return 0;
}

static void reset_options(legacy_u32 index)
{
#ifdef RESTUNTS_SDL3
	assert(modal_depth == 0);
	driving_mode = index % 3U != 0	 ? SDL3_DRIVING_MOUSE
				   : index % 2U != 0 ? SDL3_DRIVING_JOYSTICK
									 : SDL3_DRIVING_KEYBOARD;
	input_menu_count = 0;
	input_resource = input_text;
#endif
	scenario = index;
	allocation_index = 0;
	dialog_index = 0;
	joystick_index = 0;
	joystick_limit = 18;
	keyboard_stop = 0;
	joystick_enabled = index % 2U;
	mouse_driving_enabled = index % 3U;
	detail_level = index % 5U;
	slow_video_mgmt = index % 2U;
	configured_frame_rate = index % 2U ? GAME_FRAME_RATE_LOW : GAME_FRAME_RATE_NORMAL;
	mainresptr = resource_bytes[50];
	dialog_border_color = 3;
	dialog_fnt_colour = 7;
	dialog_background_color = 9;
	performGraphColor = 10;
	graphics_menu_background_color = 11;
	waitflag = 65530;
	replay_directory[0] = 0;
	replay_filename_input[0] = 0;
	for (legacy_u32 i = 0; i < 24U; i++) {
		dialog_answers[i] = -1;
	}
	trace_word(index);
}
static void record_options(void)
{
	trace_word(joystick_enabled);
	trace_word(mouse_driving_enabled);
	trace_word(detail_level);
	trace_word(slow_video_mgmt);
	trace_word(configured_frame_rate);
	trace_word(waitflag);
}
static void test_calibration(void)
{
	for (legacy_u32 i = 0; i < 6U; i++) {
		reset_options(i);
		dialog_answers[0] = i < 2U ? (legacy_s16)i - 1 : 1;
		joystick_limit = i == 2U ? 0 : i == 3U ? 16 : 18;
		keyboard_stop = i % 2U;
		calibrate_joystick_driving();
		record_options();
	}
}
static void test_graphics(void)
{
	for (legacy_u32 i = 0; i < 10U; i++) {
		reset_options(i + 10U);
		for (legacy_u32 j = 0; j < 9U; j++) {
			dialog_answers[j] = (legacy_s16)((i + j) % 9U);
		}
		dialog_answers[9] = i % 2U ? 9 : -1;
		show_graphic_levels_menu();
		record_options();
	}
}
static void test_options(void)
{
	for (legacy_u32 i = 0; i < 16U; i++) {
		reset_options(i + 20U);
		dialog_answers[0] = (legacy_s16)(i / 2U) - 1;
		if (dialog_answers[0] == 0) {
			dialog_answers[1] = i % 2U ? 2 : 0;
		} else if (dialog_answers[0] == 5) {
			dialog_answers[1] = i % 2U;
		}
		trace_word(run_option_menu());
		record_options();
	}
	reset_options(40);
	dialog_answers[0] = 0;
	dialog_answers[1] = 1;
	dialog_answers[2] = 1;
	trace_word(run_option_menu());
	record_options();
}
#ifdef RESTUNTS_SDL3
static void test_input_menu(void)
{
	for (legacy_u16 mode = 0; mode < TEST_DRIVING_MODE_COUNT; mode++) {
		reset_options(mode);
		driving_mode = (enum SDL3_DRIVING_INPUT)mode;
		legacy_u8 original_mouse = mouse_driving_enabled;
		legacy_u8 original_joystick = joystick_enabled;
		dialog_answers[0] = 0;
		dialog_answers[1] = -1;
		run_option_menu();
		legacy_s16 expected_initial =
			mode < TEST_INPUT_CHOICE_COUNT ? (legacy_s16)mode : (legacy_s16)SDL3_DRIVING_KEYBOARD;
		assert(input_menu_count == 1 && input_menu_initial == expected_initial);
		assert(driving_mode == (enum SDL3_DRIVING_INPUT)mode && modal_depth == 0);
		assert(mouse_driving_enabled == original_mouse && joystick_enabled == original_joystick);
		assert(_strcmp(input_text, "Input devices}[ Keyboard][ Joystick][ Mouse]") == 0);
	}

	static const enum SDL3_DRIVING_INPUT manual_modes[] = {
		SDL3_DRIVING_KEYBOARD, SDL3_DRIVING_JOYSTICK, SDL3_DRIVING_MOUSE};
	for (legacy_u16 choice = 0; choice < TEST_INPUT_CHOICE_COUNT; choice++) {
		reset_options(0);
		driving_mode = SDL3_DRIVING_CONTROLLER;
		dialog_answers[0] = 0;
		dialog_answers[1] = (legacy_s16)choice;
		dialog_answers[2] = 1;
		run_option_menu();
		assert(input_menu_count == 1 && input_menu_initial == (legacy_s16)SDL3_DRIVING_KEYBOARD);
		assert(driving_mode == manual_modes[choice] && modal_depth == 0);
	}

	reset_options(0);
	static legacy_s8 localized_text[] = "Geraet]}[Tastatur][Joystick][Maus]";
	input_resource = localized_text;
	driving_mode = SDL3_DRIVING_CONTROLLER;
	dialog_answers[0] = 0;
	dialog_answers[1] = -1;
	run_option_menu();
	assert(input_menu_count == 1 && input_menu_initial == (legacy_s16)SDL3_DRIVING_KEYBOARD);
	assert(driving_mode == SDL3_DRIVING_CONTROLLER && modal_depth == 0);
	assert(_strcmp(localized_text, "Geraet]}[Tastatur][Joystick][Maus]") == 0);
}
#endif

int main(void)
{
	test_calibration();
	test_graphics();
	test_options();
#ifdef RESTUNTS_SDL3
	test_input_menu();
#elif defined(OPTIONS_RECORD_BASELINE)
	fprintf(stdout, "test-menu-options.c=0x%016" LEGACY_PRIx64 "\n", trace_hash);
#else
	assert(trace_hash == UINT64_C(0xaf8245c3425601de));
#endif
	printf("test-menu-options: passed\n");
	return 0;
}
