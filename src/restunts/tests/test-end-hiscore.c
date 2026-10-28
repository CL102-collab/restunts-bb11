#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../c/fileio.h"
#include "../c/legacy.h"
#include "../c/memmgr.h"
#include "../c/menu_internal.h"
#include "../c/platform.h"
#include "../c/replay.h"
#include "../c/resource.h"
#include "../c/shape2d.h"
#include "../c/ui_text.h"
#include "../c/timing.h"
#include "../c/ui_input.h"
#include "../c/game_input.h"
#include "../c/ui_dialog.h"
#include "../c/race_stats.h"
#include "../c/audio_control.h"
#include "../c/car_resources.h"
#include "../c/highscore.h"
#include "../c/menu_common.h"
#include "../c/externs.h"
#include "extended-replay-test-stubs.h"
#include "../c/keyboard.h"
#ifdef RESTUNTS_SDL3
#include "../c/hires.h"
#include "../c/opponent_animation.h"
#include "../c/shape2d_internal.h"
#endif

#undef printf

struct SPRITE *render_window_sprite;
static legacy_u64 trace_hash = UINT64_C(1469598103934665603);
static legacy_u32 scenario, input_index, mouse_index, track_attempts;
static legacy_u32 allocation_index, sprite_index, random_index;
static legacy_u8 resource_bytes[16][32];
static legacy_u8 fixture_track[1802];
static legacy_u8 fixture_map[1802];
static legacy_u8 fixture_sequence[] = {1, 2, 3, 0};
static struct SHAPE2D fixture_shapes[4];
static struct SPRITE fixture_sprites[4];
static struct HIGHSCORE_ENTRY fixture_scores[HIGHSCORE_ENTRY_COUNT];

#define FIXTURE_POINTER_SCENARIO 1U
#define FIXTURE_POINTER_OPPONENT_ID 2
#define FIXTURE_PLAYER_FINISH_TIME 650
#define FIXTURE_OPPONENT_WIN_TIME 550
#define FIXTURE_OPPONENT_LOSS_TIME 750
#define FIXTURE_BUTTON_CENTER_DIVISOR 2

enum FIXTURE_RESULT_BUTTON {
	FIXTURE_RESULT_REPLAY_BUTTON = 1,
	FIXTURE_RESULT_RACE_BUTTON,
	FIXTURE_RESULT_MAIN_MENU_BUTTON
};
enum FIXTURE_POINTER_LAYOUT {
	FIXTURE_POINTER_SOLO,
	FIXTURE_POINTER_OPPONENT,
	FIXTURE_POINTER_NO_HIGHSCORE,
	FIXTURE_POINTER_LAYOUT_COUNT
};

static legacy_u8 pointer_case, pointer_keyboard;
static legacy_u32 pointer_polls;
static legacy_u16 pointer_key;
static enum FIXTURE_RESULT_BUTTON pointer_button;
static struct BUTTON_AREA pointer_area;

#ifdef RESTUNTS_SDL3
#define FIXTURE_ANIMATION_WIDTH 30U
#define FIXTURE_ANIMATION_WIDTH_VARIANTS 4U
#define FIXTURE_ANIMATION_HEIGHT 12
#define FIXTURE_ANIMATION_RIGHT 312
#define FIXTURE_ANIMATION_TOP 43
#define FIXTURE_DECODE_FAILURE_DRAW 3U
#define FIXTURE_SHORTCUT_COUNT 3U

static legacy_u8 movie_available, display_shortcuts, hires_active;
static legacy_u8 movie_loaded, movie_won, movie_drawn, fallback_pending;
static legacy_u32 movie_draws, movie_unloads, original_draws, previous_original_draws;
static legacy_u32 fallback_restorations, movie_draws_between_originals, display_shortcut_count;
static legacy_u32 movie_failure_draw;

legacy_s32 hires_enabled(void)
{
	return hires_active;
}

void opponent_animation_load(legacy_u8 opponent, legacy_u8 won)
{
	assert(movie_loaded == 0);
	assert(opponent == (legacy_u8)gameconfig.game_opponenttype);
	assert(gState_total_finish_time != 0 || gState_opponent_finish_time != 0);
	legacy_u8 expected_win =
		gState_opponent_finish_time != 0 &&
		(gState_total_finish_time == 0 ||
		 (legacy_u16)gState_opponent_finish_time < (legacy_u16)gState_total_finish_time);
	assert(won == expected_win);
	movie_loaded = 1;
	movie_won = won;
}

void opponent_animation_unload(void)
{
	assert(fallback_pending == 0);
	movie_unloads++;
}

legacy_s32 opponent_animation_draw(const struct SPRITE *target, legacy_s16 x, legacy_s16 y,
								   legacy_s16 width, legacy_s16 height)
{
	assert(movie_loaded != 0);
	assert(target == &drawing_sprite);
	assert((legacy_u16)width ==
		   (FIXTURE_ANIMATION_WIDTH + scenario % FIXTURE_ANIMATION_WIDTH_VARIANTS) *
			   video_shape_width_scale);
	assert(height == FIXTURE_ANIMATION_HEIGHT);
	assert(x == FIXTURE_ANIMATION_RIGHT - width && y == FIXTURE_ANIMATION_TOP);
	if (movie_draws != 0 && original_draws == previous_original_draws) {
		movie_draws_between_originals++;
	}
	previous_original_draws = original_draws;
	movie_draws++;
	legacy_u8 drawn = movie_available != 0 && hires_active != 0 &&
					  (movie_failure_draw == 0 || movie_draws < movie_failure_draw);
	if (movie_drawn != 0 && drawn == 0 && hires_active != 0) {
		fallback_pending = 1;
	}
	movie_drawn = drawn;
	return drawn;
}

legacy_s16 handle_ingame_kb_shortcuts(legacy_s16 key)
{
	assert(key == KEY_F12 || key == KEY_SHIFT_F12);
	hires_active = key == KEY_F12 ? !hires_active : 1;
	if (hires_active == 0) {
		movie_drawn = 0;
	}
	display_shortcut_count++;
	return 1;
}
#endif

static void trace_word(legacy_u16 value)
{
	trace_hash = (trace_hash ^ (value & 255U)) * UINT64_C(1099511628211);
	trace_hash = (trace_hash ^ (value >> 8)) * UINT64_C(1099511628211);
}

static void trace_text(const legacy_s8 *text)
{
	if (text == 0) {
		trace_word(65535U);
		return;
	}
	while (*text != 0) {
		trace_word((legacy_u8)*text++);
	}
	trace_word(0);
}

static void trace_pointer(const void *pointer)
{
	if (pointer == 0) {
		trace_word(0);
		return;
	}
	if (pointer == fixture_track) {
		trace_word(2);
		return;
	}
	if (pointer == fixture_scores) {
		trace_word(3);
		return;
	}
	if (pointer == fixture_sequence) {
		trace_word(4);
		return;
	}
	for (legacy_u32 i = 0; i < 16U; i++) {
		if (pointer == resource_bytes[i]) {
			trace_word(100U + i);
			return;
		}
	}
	for (legacy_u32 i = 0; i < 4U; i++) {
		if (pointer == &fixture_sprites[i]) {
			trace_word(200U + i);
			return;
		}
		if (pointer == &fixture_shapes[i]) {
			trace_word(300U + i);
			return;
		}
	}
	trace_word(1);
}

legacy_s8 *_strcat(legacy_s8 *dest, const legacy_s8 *src)
{
	legacy_s8 *result = dest;
	while (*dest != 0) {
		dest++;
	}
	do {
		*dest++ = *src;
	} while (*src++ != 0);
	return result;
}

legacy_s8 *_strcpy(legacy_s8 *dest, const legacy_s8 *src)
{
	legacy_s8 *result = dest;
	do {
		*dest++ = *src;
	} while (*src++ != 0);
	return result;
}

legacy_u16 _strlen(const legacy_s8 *str)
{
	legacy_u16 length = 0;
	while (*str++ != 0) {
		length++;
	}
	return length;
}

void audio_unload(void)
{
	trace_word(1003);
}

legacy_s16 call_read_line(legacy_s8 *text, legacy_s16 max_characters, legacy_s16 x, legacy_s16 y,
						  legacy_u32 timeout)
{
	trace_word(1004);
	trace_text(text);
	trace_word((legacy_u16)max_characters);
	trace_word((legacy_u16)x);
	trace_word((legacy_u16)y);
	trace_word((legacy_u16)timeout);
	_strcpy(text, (const legacy_s8 *)"Driver");
	return 1;
}

void check_input(void)
{
	trace_word(1005);
}

void copy_string(legacy_s8 *destination, legacy_s8 *source)
{
	trace_word(1006);
	trace_pointer(destination);
	trace_text(source);
	_strcpy(destination, source);
}

void draw_button(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
				 legacy_s16 top_color, legacy_s16 bottom_color, legacy_s16 fill_color,
				 legacy_s16 font_color)
{
	trace_word(1007);
	trace_text(text);
	trace_word((legacy_u16)x);
	trace_word((legacy_u16)y);
	trace_word((legacy_u16)width);
	trace_word((legacy_u16)height);
	trace_word((legacy_u16)top_color);
	trace_word((legacy_u16)bottom_color);
	trace_word((legacy_u16)fill_color);
	trace_word((legacy_u16)font_color);
}

void draw_three_color_beveled_border(legacy_s16 x, legacy_s16 y, legacy_s16 width,
									 legacy_s16 height, legacy_s16 outer_color,
									 legacy_s16 inner_color, legacy_s16 opposite_color)
{
	trace_word(1008);
	trace_word((legacy_u16)x);
	trace_word((legacy_u16)y);
	trace_word((legacy_u16)width);
	trace_word((legacy_u16)height);
	trace_word((legacy_u16)outer_color);
	trace_word((legacy_u16)inner_color);
	trace_word((legacy_u16)opposite_color);
}

void ensure_file_exists(legacy_s16 unused)
{
	trace_word(1009);
	trace_word((legacy_u16)unused);
}

void file_build_path(const legacy_s8 *dir, const legacy_s8 *name, const legacy_s8 *ext,
					 legacy_s8 *dst)
{
	trace_word(1010);
	trace_text(dir);
	trace_text(name);
	trace_text(ext);
	trace_pointer(dst);
	_strcpy(dst, name);
	_strcat(dst, ext);
}

void file_load_audiores(const legacy_s8 *songfile, const legacy_s8 *voicefile,
						const legacy_s8 *name)
{
	trace_word(1011);
	trace_text(songfile);
	trace_text(voicefile);
	trace_text(name);
}

void *file_load_resfile(const legacy_s8 *filename)
{
	trace_word(1012);
	trace_text(filename);
	assert(allocation_index < 16U);
	return resource_bytes[allocation_index++];
}

void *file_load_resource(legacy_s16 resource_type, const legacy_s8 *filename)
{
	trace_word(1013);
	trace_word((legacy_u16)resource_type);
	trace_text(filename);
	if (resource_type == FILE_RESOURCE_BINARY_OPTIONAL) {
		track_attempts++;
		if (scenario % 5U == 2U) {
			return 0;
		}
		if (scenario % 5U == 1U && track_attempts == 1U) {
			return 0;
		}
		return fixture_track;
	}
	assert(allocation_index < 16U);
	return resource_bytes[allocation_index++];
}

void *file_read_nofatal(const legacy_s8 *filename, void *dst)
{
	trace_word(1014);
	trace_text(filename);
	trace_pointer(dst);
	return scenario % 7U < 2U ? 0 : dst;
}

legacy_s16 file_write_fatal(const legacy_s8 *filename, void *src, legacy_u32 length)
{
	trace_word(1015);
	trace_text(filename);
	trace_pointer(src);
	trace_word((legacy_u16)length);
	const legacy_u8 *bytes = src;
	for (legacy_u32 i = 0; i < length; i++) {
		trace_word(bytes[i]);
	}
	return scenario % 7U == 0U;
}

legacy_s16 font_centered_text_x(const legacy_s8 *text)
{
	trace_word(1016);
	trace_text(text);
	return (legacy_s16)(160 - _strlen(text) * 2);
}

void font_draw_text(const legacy_s8 *text, legacy_s16 x, legacy_s16 y)
{
	trace_word(1017);
	trace_text(text);
	trace_word((legacy_u16)x);
	trace_word((legacy_u16)y);
}

void font_set_colors(legacy_s16 color, legacy_s16 background_color)
{
	trace_word(1018);
	trace_word((legacy_u16)color);
	trace_word((legacy_u16)background_color);
}

void font_set_fontdef(void)
{
	trace_word(1019);
}

void font_set_fontdef2(void *data)
{
	trace_word(1020);
	trace_pointer(data);
}

legacy_s16 font_text_width(const legacy_s8 *text)
{
	trace_word(1021);
	trace_text(text);
	return (legacy_s16)(_strlen(text) * 4);
}

void format_frame_as_string(legacy_s8 *destination, legacy_s16 frame_count,
							legacy_s16 include_hundredths)
{
	trace_word(1022);
	trace_pointer(destination);
	trace_word((legacy_u16)frame_count);
	trace_word((legacy_u16)include_hundredths);
	snprintf((legacy_char *)destination, 18, "%d", frame_count);
}

void format_integer(legacy_s8 *destination, legacy_s16 value, legacy_s16 zero_pad, legacy_s16 width)
{
	trace_word(1023);
	trace_pointer(destination);
	trace_word((legacy_u16)value);
	trace_word((legacy_u16)zero_pad);
	trace_word((legacy_u16)width);
	snprintf((legacy_char *)destination, 18, "%d", value);
}

legacy_s16 get_kevinrandom(void)
{
	trace_word(1024);

	return (legacy_s16)(scenario * 13U + random_index++);
}

legacy_s16 get_super_random(void)
{
	trace_word(1025);

	return (legacy_s16)(scenario * 17U + random_index++);
}

legacy_s16 input_checking(legacy_s16 frame_delta)
{
	if (pointer_case != 0) {
		/* A tap supplies its new position and activation in the same input sample. */
		assert(pointer_polls++ == 0);
		mouse_xpos = (pointer_area.x1 + pointer_area.x2) / FIXTURE_BUTTON_CENTER_DIVISOR;
		mouse_ypos = (pointer_area.y1 + pointer_area.y2) / FIXTURE_BUTTON_CENTER_DIVISOR;
		kbormouse = pointer_keyboard == 0;
		return LEGACY_S16_FROM_BITS(pointer_key);
	}
#ifdef RESTUNTS_SDL3
	assert(fallback_pending == 0);
	if (display_shortcuts != 0 && display_shortcut_count < FIXTURE_SHORTCUT_COUNT) {
		return display_shortcut_count < FIXTURE_SHORTCUT_COUNT - 1U ? KEY_F12 : KEY_SHIFT_F12;
	}
#endif
	trace_word(1026);
	trace_word((legacy_u16)frame_delta);
	static const legacy_u16 keys[] = {KEY_LEFT, KEY_RIGHT, 0, KEY_ENTER};
	assert(input_index < 100U);
	return keys[input_index++ % 4U];
}

legacy_s8 *locate_shape_alt(legacy_s8 *data, const legacy_s8 *name)
{
	trace_word(1027);
	trace_pointer(data);
	trace_text(name);
	return (legacy_s8 *)fixture_sequence;
}

legacy_s8 *locate_shape_fatal(legacy_s8 *data, const legacy_s8 *name)
{
	trace_word(1028);
	trace_pointer(data);
	trace_text(name);
	return (legacy_s8 *)&fixture_shapes[0];
}

legacy_s8 *locate_text_res(legacy_s8 *data, const legacy_s8 *name)
{
	trace_word(1029);
	trace_pointer(data);
	trace_text(name);
	return (legacy_s8 *)"A short result text";
}

legacy_s16 menu_animate_button_highlight(legacy_s16 item_index, const struct BUTTON_AREA *buttons,
										 legacy_s16 second_color, legacy_s16 first_color)
{
	if (pointer_case != 0) {
		assert(item_index == FIXTURE_RESULT_REPLAY_BUTTON);
		pointer_area = buttons[pointer_button];
	}
	trace_word(1030);
	trace_word((legacy_u16)item_index);
	trace_pointer(buttons);
	trace_word((legacy_u16)second_color);
	trace_word((legacy_u16)first_color);
	return (legacy_s16)(10U + input_index % 3U);
}

void menu_reset_animation_timers(void)
{
	trace_word(1031);
}

void mmgr_release(void *ptr)
{
	trace_word(1032);
	trace_pointer(ptr);
}

void mouse_draw_opaque_check(void)
{
	trace_word(1033);
}

void mouse_draw_transparent_check(void)
{
	trace_word(1034);
}

legacy_s16 mouse_multi_hittest(legacy_s16 count, const struct BUTTON_AREA *buttons)
{
	if (pointer_case != 0) {
		if (kbormouse == 0) {
			return -1;
		}
		for (legacy_s16 i = 0; i < count; i++) {
			if (buttons[i].x1 <= mouse_xpos && mouse_xpos <= buttons[i].x2 &&
				buttons[i].y1 <= mouse_ypos && mouse_ypos <= buttons[i].y2) {
				return i;
			}
		}
		return -1;
	}
	trace_word(1035);
	trace_word((legacy_u16)count);
	trace_pointer(buttons);
	for (legacy_u32 i = 0; i < (legacy_u32)count; i++) {
		trace_word(buttons[i].x1);
		trace_word(buttons[i].x2);
		trace_word(buttons[i].y1);
		trace_word(buttons[i].y2);
	}
	mouse_index++;
	if (count == 4 && mouse_index <= 5U) {
		return 0;
	}
	return (legacy_s16)((scenario % 3U) + (count == 4));
}

legacy_u16 shape2d_get_height(const struct SHAPE2D *shape)
{
	trace_word(1036);
	trace_pointer(shape);
	return 12;
}

legacy_u16 shape2d_get_width(const struct SHAPE2D *shape)
{
	trace_word(1037);
	trace_pointer(shape);
	return (legacy_u16)(30U + scenario % 4U);
}

void shape2d_rle_copy(struct SHAPE2D *shape, legacy_s16 x, legacy_s16 y)
{
#ifdef RESTUNTS_SDL3
	original_draws++;
	if (fallback_pending != 0) {
		fallback_pending = 0;
		fallback_restorations++;
	}
#endif
	trace_word(1038);
	trace_pointer(shape);
	trace_word((legacy_u16)x);
	trace_word((legacy_u16)y);
}

legacy_u16 show_dialog(legacy_s16 dialog_type, legacy_s16 save_background, void *text_resource,
					   legacy_u16 x_argument, legacy_u16 y_argument, legacy_s16 border_color,
					   legacy_s16 *disabled_choices, legacy_s16 initial_choice)
{
	trace_word(1039);
	trace_word((legacy_u16)dialog_type);
	trace_word((legacy_u16)save_background);
	trace_pointer(text_resource);
	trace_word((legacy_u16)x_argument);
	trace_word((legacy_u16)y_argument);
	trace_word((legacy_u16)border_color);
	trace_pointer(disabled_choices);
	trace_word((legacy_u16)initial_choice);
	if (dialog_type == DIALOG_TYPE_PLACEHOLDERS) {
		disabled_choices[0] = 20;
		disabled_choices[1] = 30;
		return 1;
	}
	return scenario % 5U != 2U;
}

legacy_s16 show_disk_error_dialog(void)
{
	trace_word(1040);

	return 2;
}

legacy_s16 sprite_blit_to_video(struct SPRITE *sprite, legacy_s16 mode)
{
	trace_word(1041);
	trace_pointer(sprite);
	trace_word((legacy_u16)mode);
	return 0;
}

void sprite_clear_target(legacy_u8 color)
{
	trace_word(1042);
	trace_word((legacy_u16)color);
}

void sprite_copy_image_at(struct SHAPE2D *shape, legacy_s16 x, legacy_s16 y)
{
	trace_word(1043);
	trace_pointer(shape);
	trace_word((legacy_u16)x);
	trace_word((legacy_u16)y);
}

void sprite_free_wnd(struct SPRITE *wndsprite)
{
	trace_word(1044);
	trace_pointer(wndsprite);
}

struct SPRITE *sprite_make_wnd(legacy_u16 width, legacy_u16 height, legacy_u16 color)
{
	trace_word(1045);
	trace_word((legacy_u16)width);
	trace_word((legacy_u16)height);
	trace_word((legacy_u16)color);
	assert(sprite_index < 4U);
	fixture_sprites[sprite_index].sprite_bitmapptr = &fixture_shapes[sprite_index];
	return &fixture_sprites[sprite_index++];
}

void sprite_putimage(struct SHAPE2D *shape)
{
	trace_word(1046);
	trace_pointer(shape);
}

void sprite_select_render_window(void)
{
	trace_word(1047);
}

void sprite_select_render_window_and_clear(void)
{
	trace_word(1048);
}

void sprite_select_screen_compat(void)
{
	trace_word(1049);
}

void sprite_select_target(struct SPRITE *target_sprite)
{
	trace_word(1050);
	trace_pointer(target_sprite);
}

void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	trace_word(1051);
	trace_word((legacy_u16)left);
	trace_word((legacy_u16)right);
	trace_word((legacy_u16)top);
	trace_word((legacy_u16)bottom);
}

legacy_u32 timer_get_delta_alt(void)
{
	trace_word(1052);

	return 3;
}

void unload_resource(void *resptr)
{
	trace_word(1053);
	trace_pointer(resptr);
}

static void initialize_score_fixture(legacy_u32 index)
{
	legacy_u8 *bytes = (legacy_u8 *)fixture_scores;
	for (legacy_u32 i = 0; i < sizeof(fixture_scores); i++) {
		bytes[i] = 0;
	}
	for (legacy_u32 i = 0; i < HIGHSCORE_ENTRY_COUNT; i++) {
		_strcpy(fixture_scores[i].player_name, (const legacy_s8 *)"Player");
		_strcpy(fixture_scores[i].car_name, (const legacy_s8 *)"Car");
		_strcpy(fixture_scores[i].opponent, (const legacy_s8 *)"Opp");
		fixture_scores[i].time = 1000U + 20U * i;
	}
	for (legacy_u32 i = 0; i < 1802U; i++) {
		fixture_track[i] = fixture_map[i] = i % 256U;
	}
	if (index % 5U == 3U) {
		fixture_track[100] ^= 1;
	}
	track_highscore_table = (legacy_s8 *)fixture_scores;
	track_element_map = fixture_map;
}

static void initialize_end_screen(legacy_u32 index)
{
#ifdef RESTUNTS_SDL3
	hires_active = movie_available;
	movie_loaded = movie_won = movie_drawn = fallback_pending = 0;
	movie_draws = movie_unloads = original_draws = previous_original_draws = 0;
	fallback_restorations = movie_draws_between_originals = display_shortcut_count = 0;
#endif
	initialize_score_fixture(index);
	scenario = index;
	input_index = 0;
	mouse_index = 0;
	track_attempts = 0;
	allocation_index = 0;
	sprite_index = 0;
	random_index = 0;
	video_uses_page_flipping = index % 2U;
	video_shape_width_scale = 1U + index % 2U;
	framespersec = index % 3U == 0U ? 10 : 20;
	fontnptr = (legacy_s8 *)resource_bytes[15];
	mainresptr = (legacy_s8 *)resource_bytes[14];
	font_glyph_height = 8;
	gameconfig.game_opponenttype = index % 4U == 0U ? 0 : 2;
	_strcpy(gameconfig.game_trackname, (const legacy_s8 *)"TRACK");
	_strcpy(gnam_string, (const legacy_s8 *)"Car");
	_strcpy(gsna_string, (const legacy_s8 *)"O");
	_strcpy(opponent_highscore_name, (const legacy_s8 *)"OP");
	gState_total_finish_time = (index % 3U) * 650U;
	gState_opponent_finish_time = ((index / 3U) % 3U) * 550U;
	gState_frame = index * 10U;
	gState_penalty = index % 5U == 0U ? 5 : 0;
	gState_pEndFrame = index % 2U == 0U ? 0U : 400U;
	gState_oEndFrame = index % 7U == 0U ? 200 : gState_pEndFrame;
	elapsed_time1 = index % 5U;
	gState_travDist = 400000U + index;
	gState_impactSpeed = index % 3U == 0U ? 2000 : 0;
	gState_topSpeed = 16000;
	gState_jumpCount = index % 4U;
	replay_recording_flags = (index / 9U) % 8U;
	end_opening_variant = index % 3U;
	end_closing_variant = (index / 3U) % 3U;
	end_outcome_variant = index % 4U;
}

static void run_end_screen_case(legacy_u32 index)
{
	initialize_end_screen(index);
	trace_word(index);
	legacy_u16 result = end_hiscore();
#ifdef RESTUNTS_SDL3
	legacy_u8 decisive = gameconfig.game_opponenttype != 0 &&
						 (gState_total_finish_time != 0 || gState_opponent_finish_time != 0);
	assert(movie_loaded == decisive);
	assert(movie_unloads == 1U);
	assert((movie_draws != 0) == decisive);
	assert(fallback_pending == 0);
#endif
	trace_word(result);
	trace_word(end_opening_variant);
	trace_word(end_closing_variant);
	trace_word(end_outcome_variant);
	trace_word(previous_end_opening_variant);
	trace_word(previous_end_closing_variant);
	trace_word(previous_end_outcome_variant);
	for (legacy_u32 i = 0; i < sizeof(fixture_scores); i++) {
		trace_word(((legacy_u8 *)fixture_scores)[i]);
	}
	for (legacy_u32 i = 0; i < HIGHSCORE_ENTRY_COUNT; i++) {
		trace_word(ranking_entry_order[i]);
	}
}

#ifdef RESTUNTS_SDL3
static void test_external_animation_interaction(void)
{
	movie_available = 1;
	movie_failure_draw = FIXTURE_DECODE_FAILURE_DRAW;
	run_end_screen_case(1U);
	assert(movie_won == 0 && fallback_restorations == 1U);
	assert(movie_draws_between_originals != 0);

	movie_failure_draw = 0;
	display_shortcuts = 1;
	/* Exercise both the score-entry continue loop and the regular result menu. */
	run_end_screen_case(1U);
	assert(display_shortcut_count == FIXTURE_SHORTCUT_COUNT && hires_active != 0);
	run_end_screen_case(2U);
	assert(display_shortcut_count == FIXTURE_SHORTCUT_COUNT && hires_active != 0);
	assert(movie_draws_between_originals != 0);
}
#endif

static void run_immediate_pointer_case(enum FIXTURE_POINTER_LAYOUT layout, legacy_u8 won,
									   enum FIXTURE_RESULT_BUTTON button, legacy_u16 key,
									   legacy_u8 keyboard)
{
	initialize_end_screen(FIXTURE_POINTER_SCENARIO);
	gameconfig.game_opponenttype = layout == FIXTURE_POINTER_SOLO ? 0 : FIXTURE_POINTER_OPPONENT_ID;
	gState_total_finish_time = FIXTURE_PLAYER_FINISH_TIME;
	gState_opponent_finish_time = won ? FIXTURE_OPPONENT_LOSS_TIME : FIXTURE_OPPONENT_WIN_TIME;
	replay_recording_flags = REPLAY_RECORDING_MODIFIED_FLAG;
	if (layout == FIXTURE_POINTER_NO_HIGHSCORE) {
		/* An unavailable highscore removes the first button even with an opponent. */
		fixture_track[0] ^= 1;
	}
	kbormouse = 0;
	pointer_polls = 0;
	pointer_button = button;
	pointer_key = key;
	pointer_keyboard = keyboard;
	pointer_case = 1;
	legacy_u16 selected = end_hiscore();
	pointer_case = 0;
	assert(pointer_polls == 1U);
	assert(selected == (legacy_u16)((keyboard ? FIXTURE_RESULT_REPLAY_BUTTON : button) -
									FIXTURE_RESULT_REPLAY_BUTTON));
}

static void test_immediate_pointer_selection(void)
{
	static const legacy_u16 activation_keys[] = {KEY_SPACE, KEY_ENTER};
	for (enum FIXTURE_POINTER_LAYOUT layout = FIXTURE_POINTER_SOLO;
		 layout < FIXTURE_POINTER_LAYOUT_COUNT; layout++) {
		for (legacy_u8 won = 0; won <= 1U; won++) {
			for (enum FIXTURE_RESULT_BUTTON button = FIXTURE_RESULT_REPLAY_BUTTON;
				 button <= FIXTURE_RESULT_MAIN_MENU_BUTTON; button++) {
				for (legacy_u32 i = 0; i < sizeof(activation_keys) / sizeof(activation_keys[0]);
					 i++) {
					run_immediate_pointer_case(layout, won, button, activation_keys[i], 0);
					/* Keyboard activation retains the default without a pointer hit. */
					run_immediate_pointer_case(layout, won, button, activation_keys[i], 1);
				}
			}
		}
	}
	puts("Immediate race-result pointer selection passed (win/loss, both menu layouts).");
}

int main(void)
{
	for (legacy_u32 index = 0; index < 360U; index++) {
		run_end_screen_case(index);
	}
	/* Full-entry trace includes race outcomes, score eligibility, disk retry/cancel,
	 * text variants, animations, table entry, menu toggles and cleanup. Native
	 * integer conversions evaluate font and shape metric calls only once. Input
	 * sampling precedes pointer selection so taps use their fresh position. */
#ifdef HIGHSCORE_RECORD_BASELINE
	fprintf(stdout, "highscore=0x%016" LEGACY_PRIx64 "\n", trace_hash);
#else
	assert(trace_hash == UINT64_C(0x9008eb08ba4359d5));
#endif
#ifdef RESTUNTS_SDL3
	test_external_animation_interaction();
	puts("External opponent animation lifecycle and display toggles passed.");
#endif
	test_immediate_pointer_selection();
	puts("End-of-race interaction snapshots passed (360 scenarios).");
	return 0;
}
