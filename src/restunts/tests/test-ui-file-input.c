#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/ui_input.h"
#include "../c/externs.h"
#include "../c/fileio.h"
#include "../c/ui_dialog.h"
#include "../c/ui_dialog_internal.h"
#include "../c/ui_text.h"
#include "../c/shape2d.h"
#include "../c/timing.h"
#include "../c/game_input.h"
#include "../c/keyboard.h"
#ifdef __EMSCRIPTEN__
#include "../platform/sdl3/touch_game.h"
#endif
#undef printf

legacy_s16 font_glyph_height;
legacy_s16 dialog_fnt_colour;
legacy_s16 dialog_background_color;
legacy_s16 performGraphColor;

static legacy_u32 pop_calls;
static legacy_u64 trace_hash = UINT64_C(1469598103934665603);
static legacy_u16 scripted_keys[512];
static legacy_s16 scripted_hits[512];
static legacy_s16 scripted_buttons[512];
static legacy_u32 input_index;
static legacy_u32 timer_calls;
static legacy_s16 save_succeeds;
#ifdef __EMSCRIPTEN__
static legacy_u8 test_soft_keyboard_enabled;
static legacy_u8 test_soft_keyboard_active;
static legacy_u32 test_soft_keyboard_begins;
static legacy_u32 test_soft_keyboard_ends;
static legacy_u32 empty_list_messages;
static const legacy_s8 *matching_directory;

legacy_u8 sdl3_soft_keyboard_enabled(void)
{
	return test_soft_keyboard_enabled;
}

void sdl3_soft_keyboard_begin(const legacy_s8 *text, legacy_u16 cursor, const legacy_char *label)
{
	(void)text;
	(void)cursor;
	(void)label;
	if (test_soft_keyboard_enabled) {
		assert(!test_soft_keyboard_active);
		test_soft_keyboard_active = 1;
		test_soft_keyboard_begins++;
	}
}

void sdl3_soft_keyboard_update(const legacy_s8 *text, legacy_u16 cursor)
{
	(void)text;
	(void)cursor;
}

void sdl3_soft_keyboard_end(void)
{
	if (test_soft_keyboard_enabled) {
		assert(test_soft_keyboard_active);
		test_soft_keyboard_active = 0;
		test_soft_keyboard_ends++;
	}
}
#endif

static void trace_word(legacy_u16 value)
{
	trace_hash = (trace_hash ^ (value & 255U)) * UINT64_C(1099511628211);
	trace_hash = (trace_hash ^ (value >> 8)) * UINT64_C(1099511628211);
}

static void trace_rectangle(legacy_s16 left, legacy_s16 right, legacy_s16 top, legacy_s16 bottom)
{
	trace_word((legacy_u16)left);
	trace_word((legacy_u16)right);
	trace_word((legacy_u16)top);
	trace_word((legacy_u16)bottom);
}

void mouse_draw_opaque_check(void)
{
	trace_word(1);
}
void mouse_draw_transparent_check(void)
{
	trace_word(2);
}
void sprite_pop_background(void)
{
	trace_word(3);
	pop_calls++;
}
void sprite_select_screen(void)
{
	trace_word(4);
}
void check_input(void)
{
	trace_word(5);
}
void sprite_clear_target(legacy_u8 color)
{
	trace_word(6);
	trace_word(color);
}

legacy_s16 sprite_push_background(legacy_s16 left, legacy_s16 right, legacy_s16 top,
								  legacy_s16 bottom)
{
	trace_word(7);
	trace_rectangle(left, right, top, bottom);
	return save_succeeds;
}

void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	trace_word(8);
	trace_rectangle(left, right, top, bottom);
}

void sprite_draw_rect_outline(legacy_s16 left, legacy_s16 top, legacy_s16 right, legacy_s16 bottom,
							  legacy_s16 color)
{
	trace_word(9);
	trace_rectangle(left, right, top, bottom);
	trace_word(color);
}

void font_set_colors(legacy_s16 color, legacy_s16 background_color)
{
	trace_word(10);
	trace_word(color);
	trace_word(background_color);
}

legacy_s16 font_text_width(const legacy_s8 *text)
{
	trace_word(11);
	legacy_s16 width = 0;
	while (*text != 0) {
		trace_word((legacy_u8)*text);
		width += 2 + ((legacy_u8)*text++ % 5);
	}
	trace_word(0);
	return width;
}

void font_draw_text_opaque(const legacy_s8 *text, legacy_s16 x, legacy_s16 y)
{
#ifdef __EMSCRIPTEN__
	if (strcmp(text, "No files found") == 0) {
		empty_list_messages++;
	}
#endif
	trace_word(12);
	trace_word(x);
	trace_word(y);
	while (*text != 0) {
		trace_word((legacy_u8)*text++);
	}
	trace_word(0);
}

legacy_u32 timer_get_delta_alt(void)
{
	trace_word(13);
	timer_calls++;
	return 3U * timer_calls;
}

legacy_u32 slow_timer_wait_ticks(legacy_u32 ticks)
{
	trace_word(14);
	trace_word((legacy_u16)ticks);
	return ticks;
}

legacy_s16 input_checking(legacy_s16 delta)
{
#ifdef __EMSCRIPTEN__
	assert(!test_soft_keyboard_active);
#endif
	trace_word(15);
	trace_word(delta);
	assert(input_index < 512U);
	return (legacy_s16)scripted_keys[input_index++];
}

legacy_s16 mouse_multi_hittest(legacy_s16 count, const struct BUTTON_AREA *buttons)
{
	trace_word(16);
	trace_word(count);
	for (legacy_s16 i = 0; i < count; i++) {
		trace_rectangle(buttons[i].x1, buttons[i].x2, buttons[i].y1, buttons[i].y2);
	}
	mouse_butstate = scripted_buttons[input_index - 1U];
	return scripted_hits[input_index - 1U];
}

legacy_s8 missing_disk1_message_id[4];
legacy_s8 missing_disk2_message_id[4];
legacy_s8 missing_disk3_message_id[4];
legacy_s8 missing_disk4_message_id[4];
legacy_u8 far *active_font_definition;
legacy_s16 mouse_butstate;
void far *mainresptr;
legacy_s8 resID_buffer[RESID_BUFFER_SIZE];
legacy_s8 g_is_busy;
legacy_u16 dialog_border_color;
legacy_s8 file_load_dialog_id[4] = "loa";
legacy_s8 file_save_dialog_id[4] = "sav";
legacy_s8 file_scroll_up_label_id[4] = "lsu";
legacy_s8 file_scroll_down_label_id[4] = "lsd";
static legacy_u16 keyboard_keys[512];
static legacy_u32 keyboard_index;
static legacy_u32 keyboard_count;
static legacy_u32 timeout_at;
static legacy_u32 listed_count;
static legacy_u32 listed_index;
static legacy_u32 find_calls;
static legacy_u32 first_search_empty;
static legacy_s8 listed_names[130][13];
static legacy_u8 font_definition[20];

static void trace_text(const legacy_s8 *text)
{
	while (*text != 0) {
		trace_word((legacy_u8)*text++);
	}
	trace_word(0);
}

legacy_s16 font_prefix_width(const legacy_s8 *text, legacy_s16 count)
{
	trace_word(20);
	trace_word(count);
	legacy_s16 width = 0;
	while (count-- > 0 && *text != 0) {
		trace_word((legacy_u8)*text);
		width += 2 + ((legacy_u8)*text++ % 5);
	}
	trace_word(0);
	return width;
}
legacy_u16 legacy_near_string_length(const legacy_s8 *text)
{
	return (legacy_u16)strlen(text);
}
void sprite_xor_rect_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 w, legacy_s16 h,
							 legacy_s16 color)
{
	trace_word(21);
	trace_rectangle(x, w, y, h);
	trace_word(color);
}
void sprite_fill_rect_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 w, legacy_s16 h,
							  legacy_s16 color)
{
	trace_word(22);
	trace_rectangle(x, w, y, h);
	trace_word(color);
}
void sprite_fill_rect(legacy_s16 x, legacy_s16 y, legacy_s16 w, legacy_s16 h, legacy_s16 color)
{
	trace_word(23);
	trace_rectangle(x, w, y, h);
	trace_word(color);
}
legacy_u32 timer_set_deadline(legacy_u32 ticks)
{
	trace_word(24);
	trace_word(ticks);
	trace_word(ticks >> 16);
	return ticks;
}
legacy_u32 slow_timer_set_deadline(legacy_u32 ticks)
{
	trace_word(25);
	trace_word(ticks);
	return ticks;
}
legacy_s16 slow_timer_deadline_reached(void)
{
	trace_word(26);
	return keyboard_index % 2 == 0;
}
legacy_s16 timer_deadline_reached(void)
{
	trace_word(27);
	return keyboard_index >= timeout_at;
}
void dos_kb_clear_numlock(void)
{
	trace_word(28);
}
static void edit_callback(void)
{
	trace_word(29);
}
legacy_s16 kb_call_readchar_callback(void)
{
	trace_word(30);
	trace_word(keyboard_index);
	assert(keyboard_index < keyboard_count);
	return keyboard_keys[keyboard_index++];
}
void preRender_line(legacy_s16 x1, legacy_s16 y1, legacy_s16 x2, legacy_s16 y2, legacy_s16 color)
{
	trace_word(31);
	trace_rectangle(x1, x2, y1, y2);
	trace_word(color);
}
void far *locate_text_res(void far *resource, const legacy_s8 *id)
{
	(void)resource;
	trace_word(33);
	trace_text(id);
	static legacy_s8 layout[] =
		"Prompt @]Directory @]Up @]Row @]Row @]Row @]Row @]Row @]Row @]Row @]";
	if (strcmp(id, "loa") == 0) {
		return layout;
	}
	if (strcmp(id, "sav") == 0) {
		return (void *)"Prompt @]Directory @]Name @]";
	}
	return (void *)(strcmp(id, "lsu") == 0 ? "UP" : "DOWN");
}
legacy_s16 font_centered_text_x(const legacy_s8 *text)
{
	trace_word(34);
	return 160 - font_text_width(text) / 2;
}
void parse_filepath_separators(legacy_s8 *destination, const legacy_s8 *source)
{
	trace_word(35);
	trace_text(source);
	strcpy(destination, source);
}
const legacy_s8 *file_combine_and_find(const legacy_s8 *directory, const legacy_s8 *name,
									   const legacy_s8 *extension)
{
	trace_word(36);
	trace_text(directory);
	trace_text(name);
	trace_text(extension);
	listed_index = 0;
	find_calls++;
	if (listed_count == 0 || (first_search_empty && find_calls == 1)) {
		return 0;
	}
#ifdef __EMSCRIPTEN__
	if (matching_directory != NULL && strcmp(directory, matching_directory) != 0) {
		return 0;
	}
#endif
	return listed_names[listed_index++];
}
const legacy_s8 *file_find_next_alt(void)
{
	trace_word(37);
	trace_word(listed_index);
	if (listed_index >= listed_count) {
		return 0;
	}
	return listed_names[listed_index++];
}

static void reset_case(void)
{
	for (legacy_u32 i = 0; i < 512; i++) {
		scripted_keys[i] = KEY_ENTER;
		scripted_hits[i] = -1;
		scripted_buttons[i] = 0;
		keyboard_keys[i] = KEY_ENTER;
	}
	input_index = 0;
	keyboard_index = 0;
	keyboard_count = 512;
	timer_calls = 0;
	timeout_at = 511;
	save_succeeds = 1;
	pop_calls = 0;
	find_calls = 0;
	first_search_empty = 0;
	font_glyph_height = 8;
	dialog_fnt_colour = 7;
	dialog_background_color = 3;
	dialog_border_color = 4;
	active_font_definition = font_definition;
	memset(font_definition, 0, sizeof(font_definition));
	font_definition[0] = 7;
	font_definition[2] = 3;
	font_definition[18] = 8;
#ifdef __EMSCRIPTEN__
	test_soft_keyboard_enabled = 0;
	test_soft_keyboard_active = 0;
	test_soft_keyboard_begins = 0;
	test_soft_keyboard_ends = 0;
	empty_list_messages = 0;
	matching_directory = NULL;
#endif
}

static void check_hash(const legacy_char *name, legacy_u64 expected)
{
#ifdef UI_RECORD_BASELINE
	printf("%s %016" LEGACY_PRIx64 "\n", name, trace_hash);
	(void)expected;
#else
	if (trace_hash != expected) {
		fprintf(stderr, "%s: got %016" LEGACY_PRIx64 " expected %016" LEGACY_PRIx64 "\n", name,
				trace_hash, expected);
		assert(trace_hash == expected);
	}
#endif
	trace_hash = UINT64_C(1469598103934665603);
}

static void test_file_dialog(void)
{
	legacy_s8 filename[16];
	static const legacy_u32 counts[] = {0, 1, 6, 7, 8, 127, 128, 130};
	legacy_s8 directory[32];
	for (legacy_u32 c = 0; c < sizeof(counts) / sizeof(counts[0]); c++) {
		for (legacy_u32 scenario = 0; scenario < 14; scenario++) {
			reset_case();
			listed_count = counts[c];
			g_is_busy = (legacy_s8)(scenario * 23);
			for (legacy_u32 i = 0; i < listed_count; i++) {
				legacy_u32 value = (i * 47) % 131;
				snprintf(listed_names[i], 13, "%c%03" LEGACY_PRIu32 ".RPL",
						 (legacy_s16)('A' + value % 26), value);
			}
			strcpy(directory, "DOS");
			strcpy(filename, "UNCHANGED");
			trace_word(c);
			trace_word(scenario);
			if (scenario == 0) {
				save_succeeds = 0;
			}
			if (scenario == 1) {
				scripted_keys[0] = KEY_ESCAPE;
			}
			if (scenario == 2) {
				scripted_keys[0] = KEY_SPACE;
			}
			if (scenario == 3) {
				scripted_keys[0] = KEY_UP;
				keyboard_keys[0] = KEY_ESCAPE;
			}
			if (scenario == 4) {
				for (legacy_u32 i = 0; i < 140; i++) {
					scripted_keys[i] = KEY_DOWN;
				}
			}
			if (scenario == 5) {
				scripted_keys[0] = 'z';
			}
			if (scenario == 6) {
				scripted_keys[0] = 'A';
				scripted_keys[1] = '!';
			}
			if (scenario == 7) {
				scripted_hits[0] = 8;
			}
			if (scenario == 8) {
				scripted_hits[0] = 0;
				scripted_buttons[0] = 1;
				keyboard_keys[0] = KEY_ESCAPE;
			}
			if (scenario == 9) {
				first_search_empty = 1;
				keyboard_keys[0] = KEY_ENTER;
			}
			if (scenario == 10) {
				scripted_hits[0] = 9;
				scripted_buttons[0] = 2;
				scripted_keys[0] = KEY_ESCAPE;
			}
			if (scenario == 11) {
				scripted_keys[0] = KEY_DOWN;
				scripted_hits[1] = 1;
				scripted_buttons[1] = 3;
			}
			if (scenario == 12) {
				scripted_hits[0] = 0;
				scripted_buttons[0] = 0;
			}
			if (scenario == 13) {
				scripted_keys[0] = KEY_UP;
				keyboard_keys[0] = 'N';
				keyboard_keys[1] = KEY_ENTER;
			}
			if (listed_count == 0) {
				keyboard_keys[0] = KEY_ESCAPE;
			}
			legacy_s16 result = do_fileselect_dialog(directory, filename, "RPL", "Choose file");
			trace_word(result);
			trace_text(directory);
			trace_text(filename);
			trace_word(input_index);
			trace_word(keyboard_index);
			trace_word(find_calls);
			trace_word(listed_index);
			trace_word((legacy_u8)g_is_busy);
			trace_word(pop_calls);
			assert(g_is_busy == (legacy_s8)(scenario * 23));
			assert(pop_calls == (scenario == 0 ? 0U : 1U));
			assert(listed_index <= 128);
			if (result == 0) {
				assert(strcmp(filename, "UNCHANGED") == 0);
			}
		}
	}
	check_hash("file selection", UINT64_C(0x1bf0dbeff6692627));
}

static void test_read_line(void)
{
	static const legacy_u16 keys[] = {
		KEY_RIGHT,	KEY_RIGHT, KEY_LEFT,   KEY_HOME,	  KEY_END,	KEY_LEFT,
		KEY_INSERT, 'X',	   KEY_DELETE, KEY_BACKSPACE, KEY_HOME, KEY_BACKSPACE,
		KEY_INSERT, 'z',	   123,		   0xffff,		  KEY_DOWN, KEY_TAB,
		0,			0,		   KEY_ENTER};
	static const legacy_s16 endings[] = {KEY_ENTER, KEY_ESCAPE, KEY_UP, KEY_DOWN, KEY_TAB};
	legacy_s8 text[32];
	static const legacy_s16 widths[] = {0, 1, 10, 100, -1};
	for (legacy_u32 flags = 0; flags < 32; flags++) {
		for (legacy_u32 w = 0; w < 5; w++) {
			for (legacy_u32 scenario = 0; scenario < 5; scenario++) {
				reset_case();
				strcpy(text, "Ab cd");
				if (scenario == 0) {
					keyboard_keys[0] = 'Q';
					keyboard_keys[1] = endings[flags % 5];
				}
				if (scenario == 1) {
					for (legacy_u32 i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
						keyboard_keys[i] = keys[i];
					}
				}
				legacy_s16 initial = 0;
				if (scenario == 2) {
					initial = 'R';
					keyboard_keys[0] = KEY_ESCAPE;
				}
				if (scenario == 3) {
					keyboard_keys[0] = 0;
					keyboard_keys[1] = 0;
					timeout_at = 2;
				}
				if (scenario == 4) {
					initial = KEY_RIGHT;
					keyboard_keys[0] = 'T';
					keyboard_keys[1] = KEY_ENTER;
				}
				trace_word(flags);
				trace_word(w);
				trace_word(scenario);
				legacy_s16 result =
					read_line(flags, text, initial, 8, widths[w], 32760, -4, edit_callback, 77);
				trace_word(result);
				trace_text(text);
				trace_word(keyboard_index);
				assert(text[8] == 0);
			}
		}
	}
	check_hash("text editing", UINT64_C(0xb53d8bcfdddfe7ac));
}

static void test_read_line_wrapper(void)
{
	legacy_s8 text[32];
	for (legacy_u32 scenario = 0; scenario < 3; scenario++) {
		reset_case();
		strcpy(text, "Name ");
		keyboard_keys[0] = (scenario == 0 ? KEY_ENTER : (scenario == 1 ? KEY_ESCAPE : 'Z'));
		legacy_s16 result = call_read_line(text, 8, 4, 5, 0x12345678UL);
		trace_word(result);
		trace_text(text);
		trace_word(keyboard_index);
		assert(text[strlen(text) - 1] != ' ');
	}
	check_hash("edit wrapper", UINT64_C(0x490af67e25420ce6));
}

static void test_read_line_empty_fields(void)
{
	static const legacy_u16 endings[] = {KEY_ENTER, KEY_ESCAPE, KEY_UP, KEY_DOWN, KEY_TAB};
	for (legacy_u32 ending = 0; ending < sizeof(endings) / sizeof(endings[0]); ending++) {
		for (legacy_u32 scenario = 0; scenario < 6; scenario++) {
			reset_case();
			legacy_s16 capacity = scenario == 5 ? 0 : 8;
			legacy_s8 *text = malloc((size_t)capacity + 1U);
			assert(text != NULL);
			strcpy(text, scenario == 0 || scenario == 5 ? "" : scenario == 1 ? "        " : "Name");
			legacy_u32 key_count = 0;
			if (scenario == 2) {
				for (legacy_u32 index = 0; index < 4; index++) {
					keyboard_keys[key_count++] = KEY_DELETE;
				}
			} else if (scenario == 3) {
				for (legacy_u32 index = 0; index < 4; index++) {
					keyboard_keys[key_count++] = KEY_RIGHT;
				}
				for (legacy_u32 index = 0; index < 4; index++) {
					keyboard_keys[key_count++] = KEY_BACKSPACE;
				}
			} else if (scenario == 4) {
				keyboard_keys[key_count++] = KEY_SPACE;
			}
			keyboard_keys[key_count++] = endings[ending];
			keyboard_count = key_count;
			legacy_s16 result = call_read_line(text, capacity, 4, 5, 0);
			assert((legacy_u16)result == endings[ending]);
			assert(keyboard_index == key_count);
			assert(strcmp(text, "") == 0);
			free(text);
		}
	}
	trace_hash = UINT64_C(1469598103934665603);
}

static void test_character_limit(void)
{
	static legacy_s8 text[65536];
	static const legacy_u16 capacities[] = {0, 1, 2, 8, 0x8000, 0xffff};
	for (legacy_u32 capacity = 0; capacity < sizeof(capacities) / sizeof(capacities[0]);
		 capacity++) {
		for (legacy_u32 scenario = 0; scenario < 4; scenario++) {
			reset_case();
			memset(text, 0, sizeof(text));
			strcpy(text, "A");
			keyboard_keys[0] = KEY_HOME;
			keyboard_keys[1] = KEY_LEFT;
			keyboard_keys[2] = KEY_INSERT;
			keyboard_keys[3] = 'B';
			keyboard_keys[4] = KEY_END;
			keyboard_keys[5] = KEY_RIGHT;
			keyboard_keys[6] = KEY_DELETE;
			keyboard_keys[7] = KEY_BACKSPACE;
			keyboard_keys[8] = KEY_ENTER;
			trace_word(capacities[capacity]);
			trace_word(scenario);
			legacy_s16 result = read_line(scenario, text, 0, (legacy_s16)capacities[capacity], 0,
										  -32768, 32767, edit_callback, 0);
			trace_word(result);
			trace_text(text);
			trace_word(keyboard_index);
			trace_word((legacy_u8)text[(legacy_u16)(capacities[capacity] - 1U)]);
		}
	}
	check_hash("character limits", UINT64_C(0x9e0d9cfda6cf1f47));
}

#define SAVE_TEST_DIRECTORY_CAPACITY 32
#define SAVE_TEST_FILENAME_CAPACITY 16

enum SAVE_TEST_ACTION {
	SAVE_TEST_CONFIRM,
	SAVE_TEST_CANCEL,
	SAVE_TEST_EDIT_DIRECTORY,
	SAVE_TEST_BACKGROUND_FAILURE,
	SAVE_TEST_ACTION_COUNT
};

static void test_save_dialog(void)
{
	static const legacy_u16 confirm_keys[] = {'N', 'E', 'W', ' ', 'R', KEY_ENTER};
	static const legacy_u16 cancel_keys[] = {'N', KEY_ESCAPE};
	static const legacy_u16 directory_keys[] = {'N', 'E', 'W',		 KEY_TAB,  'D',
												'I', 'R', KEY_ENTER, KEY_ENTER};
	for (enum SAVE_TEST_ACTION action = SAVE_TEST_CONFIRM; action < SAVE_TEST_ACTION_COUNT;
		 action++) {
		reset_case();
		legacy_s8 directory[SAVE_TEST_DIRECTORY_CAPACITY] = "OLDIR";
		legacy_s8 filename[SAVE_TEST_FILENAME_CAPACITY] = "OLD";
		const legacy_u16 *keys = confirm_keys;
		legacy_u32 key_count = sizeof(confirm_keys) / sizeof(confirm_keys[0]);
		if (action == SAVE_TEST_CANCEL) {
			keys = cancel_keys;
			key_count = sizeof(cancel_keys) / sizeof(cancel_keys[0]);
		} else if (action == SAVE_TEST_EDIT_DIRECTORY) {
			keys = directory_keys;
			key_count = sizeof(directory_keys) / sizeof(directory_keys[0]);
		} else if (action == SAVE_TEST_BACKGROUND_FAILURE) {
			save_succeeds = 0;
		}
		for (legacy_u32 i = 0; i < key_count; i++) {
			keyboard_keys[i] = keys[i];
		}
		keyboard_count = key_count;
		legacy_s16 result = do_savefile_dialog(directory, filename, "Save file");
		assert(result == (action == SAVE_TEST_CONFIRM || action == SAVE_TEST_EDIT_DIRECTORY));
		assert(pop_calls == (action != SAVE_TEST_BACKGROUND_FAILURE));
		assert(keyboard_index == (action == SAVE_TEST_BACKGROUND_FAILURE ? 0U : key_count));
		assert(strcmp(directory, action == SAVE_TEST_EDIT_DIRECTORY ? "DIR" : "OLDIR") == 0);
		const legacy_char *expected_name = action == SAVE_TEST_CONFIRM			? "NEW_R"
										   : action == SAVE_TEST_CANCEL			? "N"
										   : action == SAVE_TEST_EDIT_DIRECTORY ? "NEW"
																				: "OLD";
		assert(strcmp(filename, expected_name) == 0);
	}
	puts("Save dialog text editing, confirmation, cancellation and background restoration passed.");
}

#ifdef __EMSCRIPTEN__
static void test_mobile_empty_load_paths(void)
{
	static legacy_s8 extensions[][4] = {"RPL", "TRK"};
	for (legacy_u32 extension = 0; extension < 2; extension++) {
		for (legacy_u32 scenario = 0; scenario < 3; scenario++) {
			reset_case();
			test_soft_keyboard_enabled = 1;
			matching_directory = "DIR";
			listed_count = 1;
			snprintf(listed_names[0], sizeof(listed_names[0]), "TEST.%s", extensions[extension]);
			legacy_s8 directory[32];
			strcpy(directory, scenario == 0 ? "DIR" : "BAD");
			legacy_s8 filename[16] = "UNCHANGED";
			if (scenario == 0) {
				/* Submit a bad path once, then leave the empty list. Accept and
				 * navigation keys must not select nonexistent filenames. */
				scripted_keys[0] = 0;
				scripted_hits[0] = 0;
				scripted_buttons[0] = 1;
				keyboard_keys[0] = 'B';
				keyboard_keys[1] = 'A';
				keyboard_keys[2] = 'D';
				keyboard_keys[3] = KEY_ENTER;
				keyboard_count = 4;
				scripted_keys[1] = KEY_ENTER;
				scripted_keys[2] = KEY_SPACE;
				scripted_keys[3] = KEY_DOWN;
				scripted_hits[3] = 9;
				scripted_buttons[3] = 1;
				scripted_keys[4] = KEY_ESCAPE;
			} else if (scenario == 1) {
				/* An initially empty search waits for an explicit path tap;
				 * editing it to a valid directory makes files selectable. */
				scripted_keys[0] = KEY_ENTER;
				scripted_keys[1] = KEY_SPACE;
				scripted_keys[2] = KEY_DOWN;
				scripted_hits[2] = 9;
				scripted_buttons[2] = 1;
				scripted_keys[3] = 0;
				scripted_hits[3] = 0;
				scripted_buttons[3] = 1;
				keyboard_keys[0] = 'D';
				keyboard_keys[1] = 'I';
				keyboard_keys[2] = 'R';
				keyboard_keys[3] = KEY_ENTER;
				keyboard_count = 4;
				scripted_keys[4] = KEY_ENTER;
			} else {
				/* Cancelling an explicit edit exits even when no files exist. */
				scripted_keys[0] = 0;
				scripted_hits[0] = 0;
				scripted_buttons[0] = 1;
				keyboard_keys[0] = KEY_ESCAPE;
				keyboard_count = 1;
			}
			legacy_s16 result =
				do_fileselect_dialog(directory, filename, extensions[extension], "Load file");
			assert(result == (scenario == 1));
			if (scenario == 1) {
				assert(strcmp(filename, listed_names[0]) == 0);
			} else {
				assert(strcmp(filename, "UNCHANGED") == 0);
			}
			assert(strcmp(directory, scenario == 1 ? "DIR" : "BAD") == 0);
			assert(find_calls == (scenario == 2 ? 1U : 2U));
			assert(input_index == (scenario == 2 ? 1U : 5U));
			assert(keyboard_index == keyboard_count);
			assert(test_soft_keyboard_begins == 1);
			assert(test_soft_keyboard_ends == 1);
			assert(!test_soft_keyboard_active);
			assert(empty_list_messages != 0);
			assert(pop_calls == 1);
		}
	}
	puts("Mobile replay/track empty-load paths wait for explicit editing and recover safely.");
}
#endif

int main(void)
{
	test_file_dialog();
	test_read_line();
	test_read_line_wrapper();
	test_read_line_empty_fields();
	test_character_limit();
	test_save_dialog();
#ifdef __EMSCRIPTEN__
	test_mobile_empty_load_paths();
#endif
	puts("UI file-selection and text-editing regression checks passed.");
	return 0;
}
