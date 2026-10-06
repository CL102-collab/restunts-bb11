#include <string.h>
#define main car_snapshot_main
#define menu_background_draw car_fixture_menu_background_draw
#define input_checking car_fixture_input_checking
#define mouse_multi_hittest car_fixture_mouse_multi_hittest
#define locate_text_res car_fixture_locate_text_res
#define menu_update_idle_counter car_fixture_menu_update_idle_counter
#define sprite_make_wnd car_fixture_sprite_make_wnd
#define sprite_free_wnd car_fixture_sprite_free_wnd
#define sprite_blit_to_video car_fixture_sprite_blit_to_video
#define draw_button car_fixture_draw_button
#define menu_animate_button_highlight car_fixture_menu_animate_button_highlight
#include "test-car-menu.c"
#undef main
#undef menu_background_draw
#undef input_checking
#undef mouse_multi_hittest
#undef locate_text_res
#undef menu_update_idle_counter
#include "../c/highscore.h"
#include "../c/skybox.h"
#include "../c/ghost.h"
#undef sprite_make_wnd
#undef sprite_free_wnd
#undef sprite_blit_to_video
#undef draw_button
#undef menu_animate_button_highlight
#ifdef RESTUNTS_SDL3
#include "../c/opponent_portrait.h"
#endif

static legacy_u8 track_toggle_test, track_window_live, track_skybox_live, track_shapes_live;
static legacy_u8 track_initial_supersight, track_expiry_test, track_shortcut_supersight;
static legacy_u32 track_window_allocations, track_window_releases, track_presentations;
static legacy_u32 track_preview_draws, track_title_draws, track_button_draws;
static legacy_u32 track_highscore_draws, track_highscore_entries, track_setup_calls;
static const legacy_s16 track_expected_selection[] = {0, 1, 1, 1, 2};

#ifdef RESTUNTS_SDL3
void menu_background_draw(const struct SPRITE *target, const struct SHAPE2D *original,
						  enum MENU_BACKGROUND kind)
{
	assert(target == &drawing_sprite && original == &fixture_shapes[0]);
	assert(kind == MENU_BACKGROUND_OPPONENT);
}

void menu_background_draw_overlay(const struct SPRITE *target, const struct SHAPE2D *original,
								  enum MENU_BACKGROUND kind)
{
	assert(target == &drawing_sprite && original == &fixture_shapes[0]);
	assert(kind == MENU_BACKGROUND_OPPONENT);
}

void opponent_portrait_draw(const struct SPRITE *target, const struct SHAPE2D *original,
							legacy_u8 opponent)
{
	(void)target;
	(void)original;
	(void)opponent;
}

void opponent_portrait_unload(void)
{
}
#endif

struct SPRITE *sprite_make_wnd(legacy_u16 width, legacy_u16 height, legacy_u16 color)
{
	if (track_toggle_test != 0) {
		assert(track_window_live == 0);
		track_window_live = 1;
		track_window_allocations++;
	}
	return car_fixture_sprite_make_wnd(width, height, color);
}

void sprite_free_wnd(struct SPRITE *window)
{
	if (track_toggle_test != 0) {
		assert(window == render_window_sprite && track_window_live != 0);
		assert(track_skybox_live == 0 && track_shapes_live == 0);
		track_window_live = 0;
		track_window_releases++;
	}
	car_fixture_sprite_free_wnd(window);
}

legacy_s16 sprite_blit_to_video(struct SPRITE *sprite, legacy_s16 mode)
{
	if (track_toggle_test != 0) {
		assert(sprite == render_window_sprite && track_window_live != 0);
		assert(track_skybox_live == 0 && track_shapes_live == 0);
		assert((legacy_u8)mode ==
			   (track_presentations == 0 ? MENU_BLIT_MODE_INITIAL : MENU_BLIT_MODE_REFRESH));
		assert(track_title_draws == track_preview_draws);
		assert(track_highscore_draws == track_preview_draws);
		assert(track_highscore_entries == track_preview_draws);
		assert(track_button_draws == track_preview_draws * 3U);
		track_presentations++;
	}
	return car_fixture_sprite_blit_to_video(sprite, mode);
}

void draw_button(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
				 legacy_s16 top_color, legacy_s16 bottom_color, legacy_s16 fill_color,
				 legacy_s16 font_color)
{
	if (track_toggle_test != 0) {
		track_button_draws++;
	}
	car_fixture_draw_button(text, x, y, width, height, top_color, bottom_color, fill_color,
							font_color);
}

legacy_s16 menu_animate_button_highlight(legacy_s16 item_index, const struct BUTTON_AREA *buttons,
										 legacy_s16 second_color, legacy_s16 first_color)
{
	if (track_toggle_test != 0) {
		assert(frame_index <
			   sizeof(track_expected_selection) / sizeof(track_expected_selection[0]));
		assert(item_index == track_expected_selection[frame_index]);
	}
	return car_fixture_menu_animate_button_highlight(item_index, buttons, second_color,
													 first_color);
}

legacy_s16 ranking_entry_order[HIGHSCORE_ENTRY_COUNT];

void copy_string(legacy_s8 *destination, legacy_s8 *source)
{
	_strcpy(destination, source);
}

static legacy_u16 menu_keys[16];
static legacy_s16 menu_hits[16];
static legacy_u32 active_menu;
static legacy_u8 menu_track_map[1802];
static struct HIGHSCORE_ENTRY menu_scores[8];
static legacy_s16 menu_ghost_selected;
static legacy_u8 change_track, ghost_track_tile;
static legacy_u32 ghost_track_checks;

legacy_s16 ghost_is_selected(void)
{
	return menu_ghost_selected;
}

void ghost_clear(void)
{
	menu_ghost_selected = 0;
}

void ghost_check_track(void)
{
	ghost_track_checks++;
	if (menu_track_map[20] != ghost_track_tile) {
		ghost_clear();
	}
}

legacy_s16 ghost_select_replay(const legacy_s8 *directory, const legacy_s8 *name)
{
	assert(directory == replay_directory);
	(void)name;
	menu_ghost_selected = 1;
	return 0;
}

legacy_u16 show_dialog(legacy_s16 dialog_type, legacy_s16 save_background, void *text, legacy_u16 x,
					   legacy_u16 y, legacy_s16 border_color, legacy_s16 *disabled_choices,
					   legacy_s16 initial_choice)
{
	(void)dialog_type;
	(void)save_background;
	(void)text;
	(void)x;
	(void)y;
	(void)border_color;
	(void)disabled_choices;
	(void)initial_choice;
	assert(!"Unexpected replay selection failure");
	return 0;
}

legacy_s16 input_checking(legacy_s16 delta)
{
	trace_word(2000);
	trace_word(delta);
	assert(frame_index < 16U);
	if (track_expiry_test != 0 && frame_index == 2) {
		assert(display_status_active != 0 && display_status_draw_count == 1);
		display_status_expire_pending = 1;
	}
	return menu_keys[frame_index];
}
legacy_s16 mouse_multi_hittest(legacy_s16 count, const struct BUTTON_AREA *buttons)
{
	trace_word(2001);
	trace_word(count);
	trace_pointer(buttons);
	return menu_hits[frame_index++];
}
legacy_s8 *locate_text_res(legacy_s8 *resource, const legacy_s8 *name)
{
	trace_word(2002);
	trace_pointer(resource);
	trace_text(name);
	if (_strcmp(name, opponent_description_id) == 0 ||
		_strcmp(name, opponent_racing_car_label_id) == 0) {
		return (legacy_s8 *)"First]Second]]Third]";
	}
	return (legacy_s8 *)name;
}
void menu_update_idle_counter(legacy_u16 elapsed, legacy_s16 limit)
{
	trace_word(2003);
	trace_word(elapsed);
	trace_word(limit);
	if (active_menu == 1U && scenario % 6U == 5U) {
		idle_expired = 1;
	}
}
void check_input(void)
{
	trace_word(2004);
}
void show_waiting(void)
{
	trace_word(2005);
	trace_word(waitflag);
}
void *file_load_resource(legacy_s16 type, const legacy_s8 *name)
{
	trace_word(2006);
	trace_word(type);
	return file_load_resfile(name);
}
void locate_many_resources(legacy_s8 *resource, const legacy_s8 *names, legacy_s8 **pointers)
{
	trace_word(2007);
	trace_pointer(resource);
	trace_text(names);
	for (legacy_u32 i = 0; i < 7; i++) {
		pointers[i] = (legacy_s8 *)&fixture_shapes[1];
	}
}
void sprite_draw_palette_mapped(struct SHAPE2D *shape)
{
	trace_word(2008);
	trace_pointer(shape);
}
void run_car_menu(legacy_s8 *id, legacy_s8 *material, legacy_s8 *transmission, legacy_u16 opponent)
{
	trace_word(2009);
	trace_word(opponent);
	for (legacy_u32 i = 0; i < 4U; i++) {
		trace_word((legacy_u8)id[i]);
		id[i] = "VETT"[i];
	}
	*material = 2;
	*transmission = 1;
}
legacy_s8 *_strcat(legacy_s8 *destination, const legacy_s8 *source)
{
	legacy_s8 *end = destination;
	while (*end != 0) {
		end++;
	}
	_strcpy(end, source);
	return destination;
}
legacy_s8 *locate_shape_alt(legacy_s8 *resource, const legacy_s8 *name)
{
	trace_word(2010);
	trace_pointer(resource);
	trace_text(name);
	return (legacy_s8 *)name;
}
struct RECTANGLE *intro_draw_text(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 color,
								  legacy_s16 flag)
{
	if (track_toggle_test != 0 && _strcmp(text, (legacy_s8 *)"'DEFAULT'") == 0) {
		track_title_draws++;
	}
	trace_word(2011);
	trace_text(text);
	trace_word(x);
	trace_word(y);
	trace_word(color);
	trace_word(flag);
	static struct RECTANGLE rectangle;
	return &rectangle;
}
legacy_s16 font_centered_text_x(const legacy_s8 *text)
{
	trace_word(2012);
	trace_text(text);
	return 40;
}
legacy_s16 track_setup(void)
{
	if (track_toggle_test != 0) {
		track_setup_calls++;
	}
	trace_word(2013);
	if (menu_track_map[20] >= 182 && menu_track_map[20] <= 252) {
		menu_track_map[20] = 4;
	}
	return 0;
}
void load_tracks_menu_shapes(void)
{
	trace_word(2014);
	menu_track_map[20] = change_track;
}
void load_skybox(legacy_s8 index)
{
	if (track_toggle_test != 0) {
		assert(track_window_live != 0 && track_skybox_live == 0);
		track_skybox_live = 1;
	}
	trace_word(2015);
	trace_word(index);
}
void unload_skybox(void)
{
	if (track_toggle_test != 0) {
		assert(track_skybox_live != 0 && track_shapes_live == 0);
		track_skybox_live = 0;
	}
	trace_word(2016);
}
legacy_s16 shape3d_load_all(void)
{
	if (track_toggle_test != 0) {
		assert(track_skybox_live != 0 && track_shapes_live == 0);
		track_shapes_live = 1;
	}
	trace_word(2017);
	return 0;
}
void shape3d_free_all(void)
{
	if (track_toggle_test != 0) {
		assert(track_shapes_live != 0);
		track_shapes_live = 0;
	}
	trace_word(2018);
}
void draw_track_preview(void)
{
	if (track_toggle_test != 0) {
		assert(track_window_live != 0 && track_skybox_live != 0 && track_shapes_live != 0);
		legacy_u8 expected =
			track_expiry_test != 0
				? (track_preview_draws == 0 ? track_initial_supersight : track_shortcut_supersight)
				: (track_initial_supersight ^ (track_preview_draws % 2U));
		assert(supersight_enabled == expected);
		track_preview_draws++;
	}
	trace_word(2019);
}
legacy_s16 highscore_load_or_create(legacy_s16 create)
{
	if (track_toggle_test != 0) {
		track_highscore_draws++;
	}
	trace_word(2020);
	trace_word(create);
	track_highscore_table = (legacy_s8 *)menu_scores;
	ranking_entry_order[0] = 0;
	menu_scores[0].time = scenario % 2U == 0 ? 65535U : 123U;
	return scenario % 3U == 0;
}
void print_highscore_entry(legacy_s16 entry, legacy_u8 *offsets)
{
	if (track_toggle_test != 0) {
		track_highscore_entries++;
	}
	trace_word(2021);
	trace_word(entry);
	for (legacy_u32 i = 0; i < 4; i++) {
		offsets[i] = i * 2;
		*(&resID_byte1 + i * 2) = 'a' + i;
		*(&resID_byte1 + i * 2 + 1) = 0;
	}
}
legacy_s8 do_fileselect_dialog(legacy_s8 *directory, legacy_s8 *name, legacy_s8 *extension,
							   legacy_s8 *prompt)
{
	trace_word(2022);
	trace_text(directory);
	trace_text(name);
	trace_text(extension);
	trace_text(prompt);
	return scenario % 2U;
}
void file_build_path(const legacy_s8 *directory, const legacy_s8 *name, const legacy_s8 *extension,
					 legacy_s8 *destination)
{
	trace_word(2023);
	trace_text(directory);
	trace_text(name);
	trace_text(extension);
	_strcpy(destination, (legacy_s8 *)"fixture.trk");
}
void *file_read_fatal(const legacy_s8 *name, void *destination)
{
	trace_word(2024);
	trace_text(name);
	assert(destination == menu_track_map);
	menu_track_map[20] = change_track;
	return destination;
}

static void reset_menu_case(legacy_u32 index, legacy_u32 kind)
{
	scenario = index;
	active_menu = kind;
	frame_index = allocation_index = sprite_index = 0;
	idle_expired = 0;
	menu_ghost_selected = change_track = ghost_track_tile = 0;
	ghost_track_checks = 0;
	menu_track_map[20] = 0;
	video_uses_page_flipping = index % 2U;
	fontnptr = (legacy_s8 *)resource_bytes[62];
	font_glyph_height = 5 + index % 4U;
	memset(&gameconfig, 0, sizeof(gameconfig));
	gameconfig.game_playercarid[0] = 'C';
	gameconfig.game_playercarid[1] = 'O';
	gameconfig.game_playercarid[2] = 'U';
	gameconfig.game_playercarid[3] = 'N';
	_strcpy(gameconfig.game_trackname, (legacy_s8 *)"DEFAULT");
	gameconfig.game_playermaterial = index % 8U;
	gameconfig.game_opponenttype = index % 7U;
	gameconfig.game_opponentcarid[0] = -1;
	if (index % 2U) {
		for (legacy_u32 i = 0; i < 4U; i++) {
			gameconfig.game_opponentcarid[i] = "VETT"[i];
		}
	}
	if (kind == 0U && index % 6U == 1U && gameconfig.game_opponenttype == 0) {
		gameconfig.game_opponenttype = 1;
	}
	track_element_map = menu_track_map;
	menu_track_map[900] = index % 5U;
	for (legacy_u32 i = 0; i < 16; i++) {
		menu_keys[i] = KEY_ENTER;
		menu_hits[i] = kind == 0 ? 4 : 2;
	}
	trace_word(kind);
	trace_word(index);
}

static void test_opponent_navigation(void)
{
	for (legacy_u32 index = 0; index < 42U; index++) {
		reset_menu_case(index, 0);
		switch (index % 6U) {
			case 0:
				menu_keys[0] = KEY_ESCAPE;
				break;
			case 1:
				menu_hits[0] = 0;
				menu_hits[1] = 1;
				break;
			case 2:
				menu_hits[0] = 2;
				menu_keys[0] = KEY_SPACE;
				menu_hits[1] = 3;
				break;
			case 3:
				menu_hits[0] = 3;
				if (gameconfig.game_opponenttype == 0) {
					menu_keys[0] = 0;
				}
				break;
			case 4:
				menu_keys[0] = KEY_LEFT;
				menu_hits[0] = -1;
				menu_keys[1] = KEY_LEFT;
				menu_hits[1] = -1;
				menu_keys[2] = KEY_SPACE;
				menu_hits[2] = -1;
				break;
			case 5:
				menu_keys[0] = KEY_RIGHT;
				menu_hits[0] = -1;
				menu_keys[1] = KEY_RIGHT;
				menu_hits[1] = -1;
				menu_keys[2] = KEY_RIGHT;
				menu_hits[2] = -1;
				menu_keys[3] = KEY_ENTER;
				menu_hits[3] = -1;
				break;
		}
		run_opponent_menu();
		trace_word(gameconfig.game_opponenttype);
		for (legacy_u32 i = 0; i < 4U; i++) {
			trace_word((legacy_u8)gameconfig.game_opponentcarid[i]);
		}
		trace_word(gameconfig.game_opponentmaterial);
		trace_word(gameconfig.game_opponenttransmission);
	}
}

static void test_track_navigation(void)
{
	for (legacy_u32 index = 0; index < 18U; index++) {
		reset_menu_case(index, 1);
		switch (index % 6U) {
			case 0:
				menu_keys[0] = KEY_ESCAPE;
				menu_hits[0] = -1;
				break;
			case 1:
				menu_hits[0] = 0;
				break;
			case 2:
				menu_hits[0] = 1;
				break;
			case 3:
				menu_keys[0] = KEY_LEFT;
				menu_hits[0] = -1;
				menu_hits[1] = -1;
				break;
			case 4:
				menu_keys[0] = KEY_RIGHT;
				menu_hits[0] = -1;
				menu_keys[1] = KEY_RIGHT;
				menu_hits[1] = -1;
				menu_hits[2] = -1;
				break;
		}
		run_tracks_menu(index % 2U);
		trace_word(waitflag);
		trace_word(idle_expired);
	}
}

static void test_ghost_track_changes(void)
{
	for (legacy_u32 action = 0; action < 3; action++) {
		for (legacy_u8 changed = 0; changed < 2; changed++) {
			/* Even scenarios cancel Load; odd scenarios accept it. */
			reset_menu_case(action == 0 ? 102 : 103, 1);
			gameconfig.game_opponenttype = 0;
			menu_ghost_selected = 1;
			change_track = changed;
			menu_hits[0] = action == 2 ? 1 : 0;
			run_tracks_menu(0);
			assert(ghost_track_checks == (action != 0));
			assert(menu_ghost_selected == (action == 0 || changed == 0));
			assert(gameconfig.game_opponenttype == 0);
		}
	}
	/* Loading the same track must compare its canonical elements to the ghost. */
	reset_menu_case(103, 1);
	gameconfig.game_opponenttype = 0;
	menu_ghost_selected = 1;
	ghost_track_tile = menu_track_map[20] = 4;
	change_track = 182;
	menu_hits[0] = 0;
	run_tracks_menu(0);
	assert(ghost_track_checks == 1);
	assert(menu_track_map[20] == 4 && menu_ghost_selected == 1);
}

static void test_track_supersight_case(legacy_u8 page_flipping, legacy_u8 initial_mode,
									   legacy_u8 expire, legacy_u16 shortcut, legacy_u8 initial_fps)
{
	reset_menu_case(1, 1);
	video_uses_page_flipping = page_flipping;
	supersight_enabled = track_initial_supersight = initial_mode;
	fps_display_enabled = initial_fps;
	status_only_test = 1;
	track_expiry_test = expire;
	track_shortcut_supersight = initial_mode ^ 1U;
#ifdef RESTUNTS_SDL3
	if (shortcut == (legacy_u16)KEY_SHIFT_F12) {
		track_shortcut_supersight = 1;
	}
#endif
	display_status_active = display_status_expire_pending = 0;
	display_status_draw_count = display_status_expire_count = display_shift_shortcuts = 0;
	track_window_live = track_skybox_live = track_shapes_live = 0;
	track_window_allocations = track_window_releases = track_presentations = 0;
	track_preview_draws = track_title_draws = track_button_draws = 0;
	track_highscore_draws = track_highscore_entries = track_setup_calls = 0;
	menu_keys[0] = menu_keys[3] = KEY_RIGHT;
	menu_keys[1] = shortcut;
	menu_keys[2] = expire != 0 ? 0 : (legacy_u16)KEY_F12;
	menu_keys[4] = KEY_ENTER;
	for (legacy_u32 index = 0; index < 5; index++) {
		menu_hits[index] = -1;
	}
	track_toggle_test = 1;
	run_tracks_menu(0);
	track_toggle_test = 0;
	status_only_test = 0;
	assert(fps_display_enabled == initial_fps);
	assert(frame_index == 5 && track_presentations == 5);
	assert(track_preview_draws == 3);
	assert(supersight_enabled == (expire != 0 ? track_shortcut_supersight : initial_mode));
	assert(display_shift_shortcuts == (shortcut == (legacy_u16)KEY_SHIFT_F12));
	assert(display_status_draw_count == (expire != 0 ? 1 : 2));
	assert(display_status_expire_count == expire && display_status_active == (expire == 0));
	/* Expiration rebuilds the entire background, including its preview,
	 * while preserving the selected button and all resource lifetimes. */
	assert(track_window_allocations == 3 && track_window_releases == 3);
	assert(track_window_live == 0 && track_skybox_live == 0 && track_shapes_live == 0);
	assert(track_setup_calls == 0 && ghost_track_checks == 0);
	track_expiry_test = 0;
}

static void test_track_supersight_toggle(void)
{
	for (legacy_u8 page_flipping = 0; page_flipping < 2; page_flipping++) {
		for (legacy_u8 initial_mode = 0; initial_mode < 2; initial_mode++) {
			for (legacy_u8 fps = FRAME_FPS_DISPLAY_OFF; fps < FRAME_FPS_DISPLAY_MODE_COUNT; fps++) {
				test_track_supersight_case(page_flipping, initial_mode, 0, (legacy_u16)KEY_F12,
										   fps);
				test_track_supersight_case(page_flipping, initial_mode, 1, (legacy_u16)KEY_F12,
										   fps);
#ifdef RESTUNTS_SDL3
				test_track_supersight_case(page_flipping, initial_mode, 1,
										   (legacy_u16)KEY_SHIFT_F12, fps);
#endif
			}
		}
	}
}

int main(void)
{
	test_opponent_navigation();
	test_track_navigation();
	assert(trace_hash == UINT64_C(0xf466790d466a90a8));
	test_ghost_track_changes();
	test_track_supersight_toggle();
	printf("test-menu-navigation: passed\n");
	return 0;
}
