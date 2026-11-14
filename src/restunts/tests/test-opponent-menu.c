#include <string.h>

/* Reuse the car-menu drawing fixture, replacing input and ownership hooks
 * so this test drives the real opponent menu and checks each refresh. */
#define main car_snapshot_main
#define menu_background_draw car_fixture_menu_background_draw
#define menu_background_unload car_fixture_menu_background_unload
#define input_checking car_fixture_input_checking
#define mouse_multi_hittest car_fixture_mouse_multi_hittest
#define locate_text_res car_fixture_locate_text_res
#define file_load_resfile car_fixture_file_load_resfile
#define unload_resource car_fixture_unload_resource
#define mmgr_free car_fixture_mmgr_free
#define sprite_make_wnd car_fixture_sprite_make_wnd
#define sprite_free_wnd car_fixture_sprite_free_wnd
#define draw_button car_fixture_draw_button
#define font_draw_text car_fixture_font_draw_text
#define sprite_clear_target car_fixture_sprite_clear_target
#define sprite_clear_shape_alt car_fixture_sprite_clear_shape_alt
#define sprite_select_mcga_backbuffer car_fixture_sprite_select_mcga_backbuffer
#define sprite_select_render_window car_fixture_sprite_select_render_window
#define frame_hires_status_draw_text car_fixture_frame_status_draw_text
#define frame_supersight_status_draw_text car_fixture_frame_status_draw_text
#define sprite_blit_to_video car_fixture_sprite_blit_to_video
#include "test-car-menu.c"
#undef main
#undef menu_background_draw
#undef menu_background_unload
#undef sprite_clear_target
#undef sprite_clear_shape_alt
#undef sprite_select_mcga_backbuffer
#undef sprite_select_render_window
#undef frame_hires_status_draw_text
#undef frame_supersight_status_draw_text
#undef sprite_blit_to_video
#undef input_checking
#undef mouse_multi_hittest
#undef locate_text_res
#undef file_load_resfile
#undef unload_resource
#undef mmgr_free
#undef sprite_make_wnd
#undef sprite_free_wnd
#undef draw_button
#undef font_draw_text
#include "../c/ghost/ghost.h"
#define EXTENDED_TEST_GHOST_SELECTION
#include "extended-replay-test-stubs.h"
#define OPPONENT_TEST_BUTTON_COUNT 5U
#ifdef RESTUNTS_SDL3
#include "../c/opponent/opponent_portrait.h"
#include "../c/shape2d/shape2d_internal.h"
static legacy_u32 portrait_draws, enhanced_portrait_draws, portrait_unloads;
static legacy_u32 background_draws, enhanced_background_draws, background_unloads;
static legacy_u32 background_overlay_draws;
static legacy_u8 background_overlay_ready;
static legacy_u8 background_mapped_draws, background_button_draws, background_ready;
static legacy_u8 background_window_mode, background_backbuffer_mode;

enum OPPONENT_TEST_MAPPED_STAGE {
	OPPONENT_TEST_MAPPED_CLEAR,
	OPPONENT_TEST_MAPPED_BACKGROUND,
	OPPONENT_TEST_MAPPED_PORTRAIT,
	OPPONENT_TEST_MAPPED_CLIP
};

void opponent_portrait_draw(const struct SPRITE *target, const struct SHAPE2D *original,
							legacy_u8 opponent)
{
	assert(target == &drawing_sprite && original == &fixture_shapes[1]);
	assert(opponent == (legacy_u8)gameconfig.game_opponenttype);
	assert(background_ready != 0 && background_mapped_draws == OPPONENT_TEST_MAPPED_PORTRAIT);
	portrait_draws++;
	if (renderer_active != 0 && opponent != 0) {
		enhanced_portrait_draws++;
	}
}

void opponent_portrait_unload(void)
{
	portrait_unloads++;
}
#endif
#undef memcpy

#define OPPONENT_TEST_EVENT_CAPACITY 32U
#define OPPONENT_TEST_RESOURCE_COUNT 3U
#define OPPONENT_TEST_PORTRAIT_X 112U
#define OPPONENT_TEST_PORTRAIT_Y 6U
#define OPPONENT_TEST_PORTRAIT_WIDTH 80U
#define OPPONENT_TEST_PORTRAIT_HEIGHT 83U
#define OPPONENT_TEST_INTERIOR_X 2U
#define OPPONENT_TEST_INTERIOR_Y 2U
#define OPPONENT_TEST_INTERIOR_WIDTH 74U
#define OPPONENT_TEST_INTERIOR_HEIGHT 79U
#define OPPONENT_TEST_PORTRAIT_SENTINEL 7U
#define OPPONENT_TEST_BLACK 0U
#define OPPONENT_TEST_WHITE 15U
#define OPPONENT_TEST_EYE_COUNT 2U
#define OPPONENT_TEST_MIN_ZIGZAG_TURNS 2U

enum OPPONENT_TEST_DRAW_TARGET { OPPONENT_TEST_RENDER_WINDOW, OPPONENT_TEST_BACKBUFFER };

static legacy_u8 opponent_expiry_test, opponent_draw_target, opponent_window_notice;
static legacy_u8 opponent_backbuffer_notice;
static legacy_u32 opponent_background_draws, opponent_background_copies;
static legacy_u32 opponent_notice_presentations, opponent_notice_erasures;
static legacy_u8 opponent_presented_notice;

#ifdef RESTUNTS_SDL3
void menu_background_draw(const struct SPRITE *target, const struct SHAPE2D *original,
						  enum MENU_BACKGROUND kind)
{
	assert(target == &drawing_sprite && original == &fixture_shapes[0]);
	assert(kind == MENU_BACKGROUND_OPPONENT);
	assert(background_mapped_draws == OPPONENT_TEST_MAPPED_BACKGROUND);
	assert(background_button_draws == 0 && background_ready == 0);
	assert(opponent_draw_target == (video_uses_page_flipping != 0 ? OPPONENT_TEST_BACKBUFFER
																  : OPPONENT_TEST_RENDER_WINDOW));
	background_draws++;
	enhanced_background_draws += renderer_active != 0;
	background_ready = 1;
	if (opponent_draw_target == OPPONENT_TEST_BACKBUFFER) {
		background_backbuffer_mode = renderer_active;
	} else {
		background_window_mode = renderer_active;
	}
}

void menu_background_draw_overlay(const struct SPRITE *target, const struct SHAPE2D *original,
								  enum MENU_BACKGROUND kind)
{
	assert(target == &drawing_sprite && original == &fixture_shapes[0]);
	assert(kind == MENU_BACKGROUND_OPPONENT);
	assert(background_ready != 0 && background_mapped_draws == OPPONENT_TEST_MAPPED_CLIP);
	assert(background_overlay_ready == 0 && background_draws == portrait_draws);
	assert(opponent_draw_target == (video_uses_page_flipping != 0 ? OPPONENT_TEST_BACKBUFFER
																  : OPPONENT_TEST_RENDER_WINDOW));
	background_overlay_ready = 1;
	background_overlay_draws++;
}

void menu_background_unload(void)
{
	background_unloads++;
}
#endif

void sprite_clear_target(legacy_u8 color)
{
#ifdef RESTUNTS_SDL3
	background_mapped_draws = OPPONENT_TEST_MAPPED_CLEAR;
	background_button_draws = background_ready = background_overlay_ready = 0;
#endif
	if (opponent_draw_target == OPPONENT_TEST_RENDER_WINDOW) {
		opponent_window_notice = 0;
	} else {
		opponent_backbuffer_notice = 0;
	}
	opponent_background_draws++;
	car_fixture_sprite_clear_target(color);
}

void sprite_clear_shape_alt(struct SHAPE2D *shape, legacy_s16 x, legacy_s16 y)
{
	assert(opponent_draw_target == OPPONENT_TEST_BACKBUFFER);
	assert(shape == render_window_sprite->sprite_bitmapptr);
	opponent_window_notice = opponent_backbuffer_notice;
#ifdef RESTUNTS_SDL3
	assert(background_ready != 0 && background_mapped_draws == OPPONENT_TEST_MAPPED_CLIP);
	assert(background_overlay_ready != 0);
	background_window_mode = background_backbuffer_mode;
#endif
	opponent_background_copies++;
	car_fixture_sprite_clear_shape_alt(shape, x, y);
}

void sprite_select_mcga_backbuffer(void)
{
	opponent_draw_target = OPPONENT_TEST_BACKBUFFER;
	car_fixture_sprite_select_mcga_backbuffer();
}

void sprite_select_render_window(void)
{
	opponent_draw_target = OPPONENT_TEST_RENDER_WINDOW;
	car_fixture_sprite_select_render_window();
}

struct RECTANGLE *frame_renderer_status_draw_text(void)
{
	assert(opponent_draw_target == OPPONENT_TEST_RENDER_WINDOW);
	opponent_window_notice = display_status_active;
	return car_fixture_frame_status_draw_text();
}

legacy_s16 sprite_blit_to_video(struct SPRITE *sprite, legacy_s16 mode)
{
	assert(sprite == render_window_sprite);
	assert(opponent_window_notice == display_status_active);
#ifdef RESTUNTS_SDL3
	assert(background_ready != 0 && background_window_mode == renderer_active);
#endif
	if (opponent_window_notice != 0) {
		opponent_notice_presentations++;
	} else if (opponent_presented_notice != 0) {
		opponent_notice_erasures++;
	}
	opponent_presented_notice = opponent_window_notice;
	return car_fixture_sprite_blit_to_video(sprite, mode);
}

static const legacy_u8 previous_opponent[7] = {6, 6, 1, 2, 3, 4, 5};
static const legacy_u8 next_opponent[7] = {1, 2, 3, 4, 5, 6, 1};
static legacy_u16 opponent_keys[OPPONENT_TEST_EVENT_CAPACITY];
static legacy_s16 opponent_hits[OPPONENT_TEST_EVENT_CAPACITY];
static legacy_u8 expected_opponents[OPPONENT_TEST_EVENT_CAPACITY];
static legacy_u8 expected_loads[OPPONENT_TEST_EVENT_CAPACITY];
static legacy_u8 resource_live[OPPONENT_TEST_RESOURCE_COUNT];
static legacy_u32 event_count, event_index, expected_load_count, load_count;
static legacy_u32 resource_allocations, resource_releases, window_allocations, window_releases;
static legacy_u32 case_count, transition_count;
static legacy_u8 window_live;
static legacy_s16 ghost_selected, ghost_selection_result;
static legacy_s8 file_dialog_result;
static legacy_u32 ghost_dialogs, ghost_selections, ghost_button_draws, ghost_descriptions;
static legacy_u32 clock_descriptions, car_menu_calls, error_dialogs;
static legacy_u32 ghost_picture_checks, clock_picture_checks;
static legacy_u32 opponent_ghost_fill_calls;
static legacy_u8 opponent_picture[OPPONENT_TEST_PORTRAIT_HEIGHT][OPPONENT_TEST_PORTRAIT_WIDTH];

legacy_u16 shape2d_get_pos_x(const struct SHAPE2D *shape)
{
	assert(shape == &fixture_shapes[1]);
	return shape->position_x;
}

legacy_u16 shape2d_get_pos_y(const struct SHAPE2D *shape)
{
	assert(shape == &fixture_shapes[1]);
	return shape->position_y;
}

void sprite_fill_rect_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
							  legacy_s16 color)
{
	assert(gameconfig.game_opponenttype == 0 && ghost_selected != 0);
#ifdef RESTUNTS_SDL3
	assert(background_ready != 0 && background_mapped_draws == OPPONENT_TEST_MAPPED_PORTRAIT);
	assert(background_overlay_ready == 0);
#endif
	legacy_s16 local_x = x - fixture_shapes[1].position_x;
	legacy_s16 local_y = y - fixture_shapes[1].position_y;
	assert(local_x >= (legacy_s16)OPPONENT_TEST_INTERIOR_X &&
		   local_y >= (legacy_s16)OPPONENT_TEST_INTERIOR_Y);
	assert(width > 0 && height > 0);
	assert(local_x + width <=
		   (legacy_s16)(OPPONENT_TEST_INTERIOR_X + OPPONENT_TEST_INTERIOR_WIDTH));
	assert(local_y + height <=
		   (legacy_s16)(OPPONENT_TEST_INTERIOR_Y + OPPONENT_TEST_INTERIOR_HEIGHT));
	if (opponent_ghost_fill_calls++ == 0) {
		assert(color == OPPONENT_TEST_BLACK);
		assert(local_x == OPPONENT_TEST_INTERIOR_X && local_y == OPPONENT_TEST_INTERIOR_Y);
		assert(width == OPPONENT_TEST_INTERIOR_WIDTH && height == OPPONENT_TEST_INTERIOR_HEIGHT);
	} else {
		assert(color == OPPONENT_TEST_WHITE);
	}
	for (legacy_s16 row = local_y; row < local_y + height; row++) {
		for (legacy_s16 column = local_x; column < local_x + width; column++) {
			opponent_picture[row][column] = (legacy_u8)color;
		}
	}
}

static void assert_ghost_picture(void)
{
	assert(opponent_ghost_fill_calls > 1);
	legacy_u16 left = OPPONENT_TEST_PORTRAIT_WIDTH, right = 0;
	legacy_u16 top = OPPONENT_TEST_PORTRAIT_HEIGHT, bottom = 0;
	for (legacy_u16 y = 0; y < OPPONENT_TEST_PORTRAIT_HEIGHT; y++) {
		for (legacy_u16 x = 0; x < OPPONENT_TEST_PORTRAIT_WIDTH; x++) {
			legacy_u8 color = opponent_picture[y][x];
			if (x < OPPONENT_TEST_INTERIOR_X ||
				x >= OPPONENT_TEST_INTERIOR_X + OPPONENT_TEST_INTERIOR_WIDTH ||
				y < OPPONENT_TEST_INTERIOR_Y ||
				y >= OPPONENT_TEST_INTERIOR_Y + OPPONENT_TEST_INTERIOR_HEIGHT) {
				assert(color == OPPONENT_TEST_PORTRAIT_SENTINEL);
				continue;
			}
			assert(color == OPPONENT_TEST_BLACK || color == OPPONENT_TEST_WHITE);
			assert(color == opponent_picture[y][OPPONENT_TEST_INTERIOR_X * 2 +
												OPPONENT_TEST_INTERIOR_WIDTH - 1 - x]);
			if (color == OPPONENT_TEST_WHITE) {
				if (x < left) {
					left = x;
				}
				if (x > right) {
					right = x;
				}
				if (y < top) {
					top = y;
				}
				if (y > bottom) {
					bottom = y;
				}
			}
		}
	}
	/* Leave a black margin around the silhouette and retain its rounded crown. */
	assert(left > OPPONENT_TEST_INTERIOR_X && top > OPPONENT_TEST_INTERIOR_Y);
	assert(right + 1U < OPPONENT_TEST_INTERIOR_X + OPPONENT_TEST_INTERIOR_WIDTH);
	assert(bottom + 1U < OPPONENT_TEST_INTERIOR_Y + OPPONENT_TEST_INTERIOR_HEIGHT);
	assert(opponent_picture[top][left] == OPPONENT_TEST_BLACK);
	/* Two enclosed black gaps in the upper half are the eye cutouts. */
	legacy_u16 maximum_eye_gaps = 0;
	for (legacy_u16 y = top; y <= (top + bottom) / 2; y++) {
		legacy_u16 row_left = left, row_right = right;
		while (row_left < right && opponent_picture[y][row_left] != OPPONENT_TEST_WHITE) {
			row_left++;
		}
		while (row_right > row_left && opponent_picture[y][row_right] != OPPONENT_TEST_WHITE) {
			row_right--;
		}
		legacy_u16 gaps = 0;
		for (legacy_u16 x = row_left + 1; x < row_right; x++) {
			if (opponent_picture[y][x] == OPPONENT_TEST_BLACK &&
				opponent_picture[y][x - 1] == OPPONENT_TEST_WHITE) {
				gaps++;
			}
		}
		assert(gaps <= OPPONENT_TEST_EYE_COUNT);
		if (gaps > maximum_eye_gaps) {
			maximum_eye_gaps = gaps;
		}
	}
	assert(maximum_eye_gaps == OPPONENT_TEST_EYE_COUNT);
	/* A zigzag hem repeatedly changes direction; a flat or rounded bottom fails. */
	legacy_s16 previous_direction = 0;
	legacy_u16 previous_bottom = 0, turns = 0;
	for (legacy_u16 x = left; x <= right; x++) {
		legacy_u16 column_bottom = bottom;
		while (column_bottom > top && opponent_picture[column_bottom][x] != OPPONENT_TEST_WHITE) {
			column_bottom--;
		}
		if (x != left && column_bottom != previous_bottom) {
			legacy_s16 direction = column_bottom > previous_bottom ? 1 : -1;
			if (previous_direction != 0 && direction != previous_direction) {
				turns++;
			}
			previous_direction = direction;
		}
		previous_bottom = column_bottom;
	}
	assert(turns >= OPPONENT_TEST_MIN_ZIGZAG_TURNS);
	ghost_picture_checks++;
}

legacy_s16 ghost_is_selected(void)
{
	return ghost_selected;
}

void ghost_clear(void)
{
	ghost_selected = 0;
}

legacy_s16 ghost_select_replay(const legacy_s8 *directory, const legacy_s8 *name)
{
	assert(directory == replay_directory);
	assert(_strcmp(name, (legacy_s8 *)"GHOST") == 0);
	assert(gameconfig.game_opponenttype == 0);
	ghost_selections++;
	if (ghost_selection_result == 0) {
		ghost_selected = 1;
	}
	return ghost_selection_result;
}

legacy_s8 do_fileselect_dialog(legacy_s8 *directory, legacy_s8 *name, legacy_s8 *extension,
							   legacy_s8 *prompt)
{
	assert(directory == replay_directory);
	assert(_strcmp(extension, (legacy_s8 *)".rpl") == 0);
	assert(_strcmp(prompt, (legacy_s8 *)"Load Replay") == 0);
	assert(gameconfig.game_opponenttype == 0);
	assert(window_live != 0);
	ghost_dialogs++;
	if (file_dialog_result != 0) {
		_strcpy(name, (legacy_s8 *)"GHOST");
	}
	return file_dialog_result;
}

legacy_u16 show_dialog(legacy_s16 dialog_type, legacy_s16 save_background, void *text, legacy_u16 x,
					   legacy_u16 y, legacy_s16 border_color, legacy_s16 *disabled_choices,
					   legacy_s16 initial_choice)
{
	assert(dialog_type == DIALOG_TYPE_ACKNOWLEDGEMENT);
	assert(save_background == DIALOG_SAVE_BACKGROUND);
	assert(_strcmp(text, (legacy_s8 *)"Unable to load ghost replay.]") == 0);
	assert(x == DIALOG_AUTO_POSITION && y == DIALOG_AUTO_POSITION);
	assert(border_color == dialog_border_color);
	assert(disabled_choices == 0 && initial_choice == 0);
	error_dialogs++;
	return 0;
}

void draw_button(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
				 legacy_s16 top_color, legacy_s16 bottom_color, legacy_s16 fill_color,
				 legacy_s16 font_color)
{
#ifdef RESTUNTS_SDL3
	assert(background_ready != 0 && background_mapped_draws == OPPONENT_TEST_MAPPED_BACKGROUND);
	background_button_draws++;
#endif
	if (x == 21 + 3 * 56) {
		assert(_strcmp(text, gameconfig.game_opponenttype == 0 ? (legacy_s8 *)"Ghost"
															   : opponent_car_button_id) == 0);
		if (gameconfig.game_opponenttype == 0) {
			ghost_button_draws++;
		}
	}
	car_fixture_draw_button(text, x, y, width, height, top_color, bottom_color, fill_color,
							font_color);
}

void font_draw_text(const legacy_s8 *text, legacy_s16 x, legacy_s16 y)
{
	if (gameconfig.game_opponenttype == 0 && ghost_selected != 0) {
		assert_ghost_picture();
	} else {
		assert(opponent_ghost_fill_calls == 0);
		if (gameconfig.game_opponenttype == 0) {
			clock_picture_checks++;
		}
	}
#ifdef RESTUNTS_SDL3
	assert(background_ready != 0 && background_mapped_draws == OPPONENT_TEST_MAPPED_CLIP);
	assert(background_overlay_ready != 0);
#endif
	if (gameconfig.game_opponenttype == 0) {
		assert(_strcmp(text, ghost_selected != 0 ? (legacy_s8 *)"Race against a Ghost."
												 : (legacy_s8 *)"Race against the Clock.") == 0);
		if (ghost_selected != 0) {
			ghost_descriptions++;
		} else {
			clock_descriptions++;
		}
	}
	car_fixture_font_draw_text(text, x, y);
}

legacy_s16 input_checking(legacy_s16 elapsed)
{
	(void)elapsed;
	assert(event_index < event_count);
	assert((legacy_u8)gameconfig.game_opponenttype == expected_opponents[event_index]);
	if (opponent_expiry_test != 0 && event_index == 1) {
		assert(display_status_active != 0 && display_status_draw_count == 1);
		display_status_expire_pending = 1;
	}
	return (legacy_s16)opponent_keys[event_index];
}

legacy_s16 mouse_multi_hittest(legacy_s16 count, const struct BUTTON_AREA *buttons)
{
	assert(count == 5 && buttons == opponentmenu_buttons);
	assert(event_index < event_count);
	return opponent_hits[event_index++];
}

static void *allocate_resource(legacy_u32 index)
{
	assert(index < OPPONENT_TEST_RESOURCE_COUNT && resource_live[index] == 0);
	resource_live[index] = 1;
	resource_allocations++;
	return resource_bytes[index];
}

static void release_resource(void *resource)
{
	for (legacy_u32 index = 0; index < OPPONENT_TEST_RESOURCE_COUNT; index++) {
		if (resource == resource_bytes[index]) {
			assert(resource_live[index] != 0);
			resource_live[index] = 0;
			resource_releases++;
			return;
		}
	}
	assert(!"Opponent menu released an unknown resource");
}

void *file_load_resfile(const legacy_s8 *filename)
{
	if (_strcmp(filename, opponent_misc_resource_name) == 0) {
		return allocate_resource(0);
	}
	/* A wrap failure used to request opp/ rather than the sixth opponent. */
	if (filename[0] != 'o' || filename[1] != 'p' || filename[2] != 'p' || filename[3] < '1' ||
		filename[3] > '6' || filename[4] != 0) {
		fprintf(stderr, "Invalid opponent resource requested: %s\n", (const legacy_char *)filename);
		assert(!"Opponent resource must be opp1 through opp6");
	}
	assert(load_count < expected_load_count);
	assert((legacy_u8)(filename[3] - '0') == expected_loads[load_count++]);
	assert(filename[3] - '0' == gameconfig.game_opponenttype);
	return allocate_resource(2);
}

void *file_load_resource(legacy_s16 type, const legacy_s8 *name)
{
	assert(type == FILE_RESOURCE_SHAPE2D_ALTERNATE);
	assert(_strcmp(name, opponent_menu_shapes_name) == 0);
	return allocate_resource(1);
}

void unload_resource(void *resource)
{
	release_resource(resource);
}

void *mmgr_free(legacy_s8 *resource)
{
	assert(resource == (legacy_s8 *)resource_bytes[1]);
	release_resource(resource);
	return 0;
}

struct SPRITE *sprite_make_wnd(legacy_u16 width, legacy_u16 height, legacy_u16 color)
{
	assert(width == 320 && height == 200 && color == 15);
	assert(window_live == 0);
	window_live = 1;
	window_allocations++;
	fixture_sprites[0].sprite_bitmapptr = &fixture_shapes[0];
	return &fixture_sprites[0];
}

void sprite_free_wnd(struct SPRITE *window)
{
	assert(window == &fixture_sprites[0] && window_live != 0);
	window_live = 0;
	window_releases++;
}

legacy_s8 *locate_text_res(legacy_s8 *resource, const legacy_s8 *name)
{
	if (_strcmp(name, (legacy_s8 *)"rep") == 0) {
		assert(resource == mainresptr);
		return (legacy_s8 *)"Load Replay";
	}
	if (_strcmp(name, opponent_description_id) == 0) {
		assert(resource == (legacy_s8 *)resource_bytes[2] && resource_live[2] != 0);
		return (legacy_s8 *)"Opponent]";
	}
	assert(resource == (legacy_s8 *)resource_bytes[0] && resource_live[0] != 0);
	return _strcmp(name, opponent_racing_car_label_id) == 0
			   ? (legacy_s8 *)"Race against the Clock.]"
			   : (legacy_s8 *)name;
}

void locate_many_resources(legacy_s8 *resource, const legacy_s8 *names, legacy_s8 **pointers)
{
	assert(resource == (legacy_s8 *)resource_bytes[1]);
	assert(names == opponent_portrait_shape_ids);
	for (legacy_u32 index = 0; index < 7; index++) {
		pointers[index] = (legacy_s8 *)&fixture_shapes[1];
	}
}

void sprite_draw_palette_mapped(struct SHAPE2D *shape)
{
	if (shape == &fixture_shapes[1]) {
		memset(opponent_picture, OPPONENT_TEST_PORTRAIT_SENTINEL, sizeof(opponent_picture));
		opponent_ghost_fill_calls = 0;
	}
	assert(shape == &fixture_shapes[0] || shape == &fixture_shapes[1]);
	assert((legacy_u8)gameconfig.game_opponenttype <= 6);
#ifdef RESTUNTS_SDL3
	if (background_mapped_draws == OPPONENT_TEST_MAPPED_CLEAR) {
		assert(shape == &fixture_shapes[0] && background_ready == 0);
	} else {
		assert(background_ready != 0 && background_button_draws == OPPONENT_TEST_BUTTON_COUNT);
		assert(shape == (background_mapped_draws == OPPONENT_TEST_MAPPED_BACKGROUND
							 ? &fixture_shapes[1]
							 : &fixture_shapes[0]));
	}
	background_mapped_draws++;
	assert(background_mapped_draws <= OPPONENT_TEST_MAPPED_CLIP);
#endif
}

void check_input(void)
{
}

void show_waiting(void)
{
}

void run_car_menu(legacy_s8 *id, legacy_s8 *material, legacy_s8 *transmission, legacy_u16 opponent)
{
	assert(id == gameconfig.game_opponentcarid);
	assert(material == &gameconfig.game_opponentmaterial);
	assert(transmission == &gameconfig.game_opponenttransmission);
	assert(opponent != 0 && opponent == (legacy_u8)gameconfig.game_opponenttype);
	assert(window_live == 0 && resource_live[2] == 0);
	memcpy(id, "VETT", 4);
	*material = 2;
	*transmission = TRANSMISSION_AUTOMATIC;
	car_menu_calls++;
}

static void expect_load(legacy_u8 opponent)
{
	if (opponent != 0) {
		assert(expected_load_count < OPPONENT_TEST_EVENT_CAPACITY);
		expected_loads[expected_load_count++] = opponent;
	}
}

static void add_event(legacy_u16 key, legacy_u8 opponent)
{
	assert(event_count < OPPONENT_TEST_EVENT_CAPACITY);
	opponent_keys[event_count] = key;
	opponent_hits[event_count] = -1;
	expected_opponents[event_count++] = opponent;
}

static void begin_case(legacy_u8 opponent, legacy_u8 page_flipping)
{
	assert(resource_allocations == resource_releases && window_allocations == window_releases);
	memset(resource_live, 0, sizeof(resource_live));
	memset(&gameconfig, 0, sizeof(gameconfig));
	memcpy(gameconfig.game_playercarid, "COUN", 4);
	gameconfig.game_opponentcarid[0] = -1;
	gameconfig.game_opponenttype = (legacy_s8)opponent;
	gameconfig.game_playermaterial = 3;
	video_uses_page_flipping = page_flipping;
	fontnptr = (legacy_s8 *)resource_bytes[62];
	font_glyph_height = 8;
	ghost_selected = ghost_selection_result = 0;
	file_dialog_result = 1;
	ghost_dialogs = ghost_selections = ghost_button_draws = ghost_descriptions = 0;
	clock_descriptions = car_menu_calls = error_dialogs = 0;
	ghost_picture_checks = clock_picture_checks = opponent_ghost_fill_calls = 0;
	fixture_shapes[1].position_x = OPPONENT_TEST_PORTRAIT_X;
	fixture_shapes[1].position_y = OPPONENT_TEST_PORTRAIT_Y;
	event_count = event_index = expected_load_count = load_count = 0;
	resource_allocations = resource_releases = window_allocations = window_releases = 0;
	expect_load(opponent);
	status_only_test = 1;
	renderer_active = fps_display_enabled = 0;
	display_status_active = display_status_expire_pending = opponent_expiry_test = 0;
	display_status_draw_count = display_status_expire_count = display_shift_shortcuts = 0;
	opponent_draw_target = OPPONENT_TEST_RENDER_WINDOW;
	opponent_window_notice = opponent_backbuffer_notice = opponent_presented_notice = 0;
	opponent_background_draws = opponent_background_copies = 0;
	opponent_notice_presentations = opponent_notice_erasures = 0;
#ifdef RESTUNTS_SDL3
	portrait_draws = enhanced_portrait_draws = portrait_unloads = 0;
	background_draws = enhanced_background_draws = background_unloads = background_overlay_draws =
		0;
	background_mapped_draws = OPPONENT_TEST_MAPPED_CLEAR;
	background_button_draws = background_ready = background_overlay_ready = 0;
	background_window_mode = background_backbuffer_mode = LEGACY_U8_MAX;
#endif
}

static void finish_case(legacy_u8 opponent, legacy_u32 refresh_count)
{
	legacy_u8 original_fps_preference = fps_display_enabled;
	run_opponent_menu();
	assert(fps_display_enabled == original_fps_preference);
	status_only_test = 0;
	assert(event_index == event_count && load_count == expected_load_count);
	assert((legacy_u8)gameconfig.game_opponenttype == opponent);
	assert(resource_allocations == resource_releases && window_allocations == window_releases);
	assert(resource_allocations == expected_load_count + 2 && window_allocations == refresh_count);
	assert(window_live == 0);
	assert(ghost_picture_checks == ghost_descriptions);
	assert(clock_picture_checks == clock_descriptions);
	for (legacy_u32 index = 0; index < OPPONENT_TEST_RESOURCE_COUNT; index++) {
		assert(resource_live[index] == 0);
	}
	if (opponent != 0) {
		assert(memcmp(gameconfig.game_opponentcarid, car_menu_calls != 0 ? "VETT" : "COUN", 4) ==
			   0);
		assert(gameconfig.game_opponentmaterial == (car_menu_calls != 0 ? 2 : 0));
		assert(gameconfig.game_opponenttransmission ==
			   (car_menu_calls != 0 ? TRANSMISSION_AUTOMATIC : TRANSMISSION_MANUAL));
	} else {
		assert(gameconfig.game_opponentcarid[0] == -1);
	}
#ifdef RESTUNTS_SDL3
	assert(portrait_unloads == 1 && background_unloads == 1);
	assert(background_draws == opponent_background_draws);
	assert(background_overlay_draws == background_draws);
#endif
	case_count++;
}

static void test_direction(legacy_u8 initial, legacy_u8 direction, legacy_u32 repeats,
						   legacy_u8 page_flipping)
{
	legacy_u8 opponent = initial;
	begin_case(initial, page_flipping);
	if (direction != 0) {
		add_event(KEY_RIGHT, opponent);
	}
	for (legacy_u32 index = 0; index < repeats; index++) {
		add_event(KEY_ENTER, opponent);
		opponent = direction != 0 ? next_opponent[opponent] : previous_opponent[opponent];
		expect_load(opponent);
		transition_count++;
	}
	/* Navigate from Last/Next to Done, keeping hover disabled throughout. */
	add_event(KEY_LEFT, opponent);
	if (direction != 0) {
		add_event(KEY_LEFT, opponent);
	}
	add_event(KEY_ENTER, opponent);
	finish_case(opponent, repeats + 1);
}

static void test_return_to_clock(legacy_u8 page_flipping)
{
	begin_case(6, page_flipping);
	add_event(KEY_RIGHT, 6);
	add_event(KEY_RIGHT, 6);
	add_event(KEY_ENTER, 6); /* None unloads the existing opponent. */
	add_event(KEY_LEFT, 0);
	add_event(KEY_LEFT, 0);
	add_event(KEY_ENTER, 0); /* Last after None must select the sixth opponent. */
	expect_load(6);
	add_event(KEY_RIGHT, 6);
	add_event(KEY_RIGHT, 6);
	add_event(KEY_ENTER, 6);
	add_event(KEY_RIGHT, 0); /* Clock -> Ghost -> Done. */
	add_event(KEY_RIGHT, 0);
	add_event(KEY_ENTER, 0);
	finish_case(0, 4);
	transition_count += 3;
}

static void test_ghost_selection(legacy_u8 page_flipping, legacy_u8 already_selected,
								 legacy_u8 use_mouse, legacy_u8 result)
{
	begin_case(0, page_flipping);
	ghost_selected = already_selected;
	file_dialog_result = result != 1;
	ghost_selection_result = result == 2;
	if (use_mouse != 0) {
		add_event(KEY_ENTER, 0);
		opponent_hits[event_count - 1] = 3;
	} else {
		add_event(KEY_LEFT, 0); /* Last -> Done -> Ghost. */
		add_event(KEY_LEFT, 0);
		add_event(KEY_ENTER, 0);
	}
	add_event(KEY_RIGHT, 0);
	add_event(KEY_ENTER, 0);
	finish_case(0, already_selected == 0 && result == 0 ? 2 : 1);
	assert(ghost_dialogs == 1 && ghost_selections == (result != 1));
	assert(error_dialogs == (result == 2));
	assert(ghost_selected == (already_selected != 0 || result == 0));
	assert(ghost_descriptions == (already_selected != 0 || result == 0));
	assert(clock_descriptions == (already_selected == 0));
	assert(ghost_button_draws == window_allocations);
	assert(car_menu_calls == 0);
	assert(memcmp(gameconfig.game_playercarid, "COUN", 4) == 0);
	assert(gameconfig.game_playermaterial == 3);
}

static void test_clear_ghost(legacy_u8 page_flipping, legacy_u8 selection)
{
	begin_case(0, page_flipping);
	ghost_selected = 1;
	add_event(KEY_ENTER, 0);
	opponent_hits[event_count - 1] = selection;
	legacy_u8 opponent = selection == 0 ? 6 : (selection == 1 ? 1 : 0);
	expect_load(opponent);
	add_event(KEY_ENTER, opponent);
	opponent_hits[event_count - 1] = 4;
	finish_case(opponent, 2);
	assert(ghost_selected == 0 && ghost_descriptions == 1);
	assert(clock_descriptions == (selection == 2));
	assert(ghost_dialogs == 0);
}

static void test_escape_back(legacy_u8 page_flipping, legacy_u8 opponent, legacy_u8 selection,
							 legacy_u8 use_mouse)
{
	begin_case(opponent, page_flipping);
	ghost_selected = 1;
	if (use_mouse == 0) {
		for (legacy_u8 index = 0; index < selection; index++) {
			add_event(KEY_RIGHT, opponent);
		}
	}
	add_event(KEY_ESCAPE, opponent);
	if (use_mouse != 0) {
		opponent_hits[event_count - 1] = selection;
	}
	finish_case(opponent, 1);
	assert(ghost_selected == 1 && ghost_dialogs == 0 && car_menu_calls == 0);
}

static void test_escape_after_opponent_change(legacy_u8 page_flipping)
{
	begin_case(3, page_flipping);
	add_event(KEY_RIGHT, 3);
	add_event(KEY_ENTER, 3);
	expect_load(4);
	add_event(KEY_ESCAPE, 4);
	finish_case(4, 2);
	assert(ghost_dialogs == 0 && car_menu_calls == 0);
}

static void test_opponent_car(legacy_u8 page_flipping, legacy_u8 initial_fps)
{
	begin_case(3, page_flipping);
	fps_display_enabled = initial_fps;
	add_event(KEY_ENTER, 3);
	opponent_hits[event_count - 1] = 3;
	expect_load(3);
	/* Car returns to this menu with Car still selected; Back must not reopen it. */
	add_event(KEY_ESCAPE, 3);
	finish_case(3, 2);
	assert(car_menu_calls == 1 && ghost_dialogs == 0 && ghost_button_draws == 0);
}

static void test_ghost_renderer_toggle(legacy_u8 page_flipping)
{
	begin_case(0, page_flipping);
	ghost_selected = 1;
	/* Also honor a portrait resource relocated from its stock screen position. */
	fixture_shapes[1].position_x += OPPONENT_TEST_INTERIOR_WIDTH;
	fixture_shapes[1].position_y += OPPONENT_TEST_INTERIOR_Y;
	add_event(KEY_F12, 0);
	add_event(KEY_F12, 0);
	add_event(KEY_ENTER, 0);
	opponent_hits[event_count - 1] = 4;
	finish_case(0, 1);
	assert(ghost_selected != 0 && renderer_active == 0);
	assert(ghost_picture_checks == 3 && clock_picture_checks == 0);
	assert(opponent_background_draws == 3);
	assert(opponent_background_copies == (page_flipping != 0 ? 3 : 0));
}

static void test_notice_expiry(legacy_u8 page_flipping, legacy_u16 shortcut, legacy_u8 initial_fps)
{
	begin_case(3, page_flipping);
	fps_display_enabled = initial_fps;
	opponent_expiry_test = 1;
	add_event(shortcut, 3);
	add_event(0, 3);
	add_event(KEY_ENTER, 3);
	opponent_hits[event_count - 1] = 4;
	finish_case(3, 1);
	assert(renderer_active == 1 && fps_display_enabled == initial_fps);
	assert(display_shift_shortcuts == (shortcut == (legacy_u16)KEY_SHIFT_F12));
	assert(display_status_draw_count == 1);
	assert(display_status_expire_count == 1 && display_status_active == 0);
	assert(opponent_background_draws == 3);
	assert(opponent_background_copies == (page_flipping != 0 ? 3 : 0));
	assert(opponent_notice_presentations == 1 && opponent_notice_erasures == 1);
#ifdef RESTUNTS_SDL3
	/* The third portrait belongs to the fresh background that erases the notice. */
	assert(portrait_draws == 3 && enhanced_portrait_draws == 2);
	assert(background_draws == 3 && enhanced_background_draws == 2);
#endif
	opponent_expiry_test = 0;
}

static void test_renderer_toggle(legacy_u8 page_flipping, legacy_u8 opponent, legacy_u8 initial_fps)
{
	begin_case(opponent, page_flipping);
	fps_display_enabled = initial_fps;
	add_event(KEY_F12, opponent);
	add_event(KEY_F12, opponent);
	add_event(KEY_ENTER, opponent);
	opponent_hits[event_count - 1] = 4;
	finish_case(opponent, 1);
	assert(renderer_active == 0);
	assert(display_status_draw_count == 2 && display_status_active != 0);
	assert(opponent_background_draws == 3);
	assert(opponent_background_copies == (page_flipping != 0 ? 3 : 0));
	assert(opponent_notice_presentations == 2 && opponent_notice_erasures == 0);
#ifdef RESTUNTS_SDL3
	assert(portrait_draws == 3 && enhanced_portrait_draws == (opponent != 0));
	assert(background_draws == 3 && enhanced_background_draws == 1);
#endif
}

int main(void)
{
	for (legacy_u8 page_flipping = 0; page_flipping < 2; page_flipping++) {
		for (legacy_u8 initial = 0; initial < 7; initial++) {
			for (legacy_u8 direction = 0; direction < 2; direction++) {
				test_direction(initial, direction, 1, page_flipping);
				test_direction(initial, direction, 19, page_flipping);
			}
		}
		test_return_to_clock(page_flipping);
		test_escape_after_opponent_change(page_flipping);
		test_ghost_renderer_toggle(page_flipping);
		for (legacy_u8 selection = 0; selection < OPPONENT_TEST_BUTTON_COUNT; selection++) {
			for (legacy_u8 use_mouse = 0; use_mouse < 2; use_mouse++) {
				test_escape_back(page_flipping, 0, selection, use_mouse);
				test_escape_back(page_flipping, 3, selection, use_mouse);
			}
		}
		for (legacy_u8 already_selected = 0; already_selected < 2; already_selected++) {
			for (legacy_u8 use_mouse = 0; use_mouse < 2; use_mouse++) {
				for (legacy_u8 result = 0; result < 3; result++) {
					test_ghost_selection(page_flipping, already_selected, use_mouse, result);
				}
			}
		}
		for (legacy_u8 selection = 0; selection < 3; selection++) {
			test_clear_ghost(page_flipping, selection);
		}
		for (legacy_u8 fps = FRAME_FPS_DISPLAY_OFF; fps < FRAME_FPS_DISPLAY_MODE_COUNT; fps++) {
			test_opponent_car(page_flipping, fps);
			test_renderer_toggle(page_flipping, 3, fps);
			test_renderer_toggle(page_flipping, 0, fps);
			test_notice_expiry(page_flipping, (legacy_u16)KEY_F12, fps);
#ifdef RESTUNTS_SDL3
			test_notice_expiry(page_flipping, (legacy_u16)KEY_SHIFT_F12, fps);
#endif
		}
	}
	printf("test-opponent-menu: passed %" LEGACY_PRIu32 " sessions, %" LEGACY_PRIu32
		   " transitions\n",
		   case_count, transition_count);
	return 0;
}
