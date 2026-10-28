/* Exercise the real merged selector with the existing UI fixture's renderer and
 * keyboard stubs. Keep these checks independent of the legacy trace baselines. */
#define main legacy_file_input_fixture_main
#define file_combine_and_find legacy_fixture_file_combine_and_find
#define file_find_next_alt legacy_fixture_file_find_next_alt
#define font_set_colors legacy_fixture_font_set_colors
#define font_draw_text_opaque legacy_fixture_font_draw_text_opaque
#define input_checking legacy_fixture_input_checking
#include "test-ui-file-input.c"
#undef main
#undef file_combine_and_find
#undef file_find_next_alt
#undef font_set_colors
#undef font_draw_text_opaque
#undef input_checking

static const legacy_s8 *const *legacy_paths;
static const legacy_s8 *const *extended_paths;
static const legacy_s8 *const *active_paths;
static size_t active_index;
static const legacy_s8 *expected_legacy_suffix;
static const legacy_s8 *expected_extended_suffix;
static unsigned legacy_searches, extended_searches, modal_depth;
static legacy_s16 foreground, background;
static struct {
	legacy_s8 text[32];
	legacy_s16 foreground, background;
} drawn[256];
static size_t drawn_count;
static legacy_s8 choose_display_name[32];
static unsigned navigation_prepared;

void sdl3_input_push_modal(void)
{
	modal_depth++;
}

void sdl3_input_pop_modal(void)
{
	assert(modal_depth != 0);
	modal_depth--;
}

void font_set_colors(legacy_s16 color, legacy_s16 background_color)
{
	foreground = color;
	background = background_color;
	legacy_fixture_font_set_colors(color, background_color);
}

void font_draw_text_opaque(const legacy_s8 *text, legacy_s16 x, legacy_s16 y)
{
	if (strstr(text, ".trk") || strstr(text, ".cstrk") || strstr(text, ".rpl") ||
		strstr(text, ".csrpl")) {
		assert(drawn_count < sizeof(drawn) / sizeof(drawn[0]));
		assert(strlen(text) < sizeof(drawn[0].text));
		strcpy(drawn[drawn_count].text, text);
		drawn[drawn_count].foreground = foreground;
		drawn[drawn_count++].background = background;
	}
	legacy_fixture_font_draw_text_opaque(text, x, y);
}

const legacy_s8 *file_combine_and_find(const legacy_s8 *directory, const legacy_s8 *name,
									   const legacy_s8 *extension)
{
	assert(strcmp(directory, "tracks") == 0);
	assert(strcmp(name, "*") == 0);
	if (strcmp(extension, expected_legacy_suffix) == 0) {
		legacy_searches++;
		active_paths = legacy_paths;
	} else {
		assert(strcmp(extension, expected_extended_suffix) == 0);
		extended_searches++;
		active_paths = extended_paths;
	}
	active_index = 0;
	return active_paths && active_paths[0] ? active_paths[active_index++] : NULL;
}

const legacy_s8 *file_find_next_alt(void)
{
	return active_paths && active_paths[active_index] ? active_paths[active_index++] : NULL;
}

legacy_s16 input_checking(legacy_s16 delta)
{
	if (choose_display_name[0] && !navigation_prepared) {
		size_t row = 0;
		while (row < drawn_count && strcmp(drawn[row].text, choose_display_name) != 0) {
			row++;
		}
		assert(row < drawn_count && row < 7);
		for (size_t index = 0; index < row; index++) {
			scripted_keys[index] = KEY_DOWN;
		}
		scripted_keys[row] = KEY_ENTER;
		navigation_prepared = 1;
	}
	return legacy_fixture_input_checking(delta);
}

static unsigned saw_color(const legacy_s8 *name, legacy_s16 color, legacy_s16 back)
{
	unsigned count = 0;
	for (size_t i = 0; i < drawn_count; i++) {
		if (strcmp(drawn[i].text, name) == 0 && drawn[i].foreground == color &&
			drawn[i].background == back) {
			count++;
		}
	}
	return count;
}

static void configure_selector(const legacy_s8 *legacy_suffix, const legacy_s8 *extended_suffix,
							   const legacy_s8 *const *old_files, const legacy_s8 *const *new_files)
{
	reset_case();
	expected_legacy_suffix = legacy_suffix;
	expected_extended_suffix = extended_suffix;
	legacy_paths = old_files;
	extended_paths = new_files;
	legacy_searches = extended_searches = 0;
	drawn_count = 0;
	choose_display_name[0] = 0;
	navigation_prepared = 0;
	g_is_busy = 9;
}

static void select_name(const legacy_s8 *expected_name, const legacy_s8 *expected_suffix)
{
	legacy_s8 directory[32] = "tracks", filename[9] = "OLD";
	strcpy(choose_display_name, expected_name);
	strcat(choose_display_name, expected_suffix);
	assert(do_format_fileselect_dialog(directory, filename, (legacy_s8 *)expected_legacy_suffix,
									   "Choose file") == 1);
	assert(strcmp(filename, expected_name) == 0);
	assert(strcmp(file_dialog_selected_extension(), expected_suffix) == 0);
	assert(legacy_searches == 1 && extended_searches == 1);
	assert(g_is_busy == 9 && modal_depth == 0);
}

static void test_tracks_keep_identical_stems_and_full_names(void)
{
	static const legacy_s8 *const old_files[] = {"C:\\GAME\\ZZZZZZZZ.TRK", "C:\\GAME\\TWIN.TRK",
												 "/game/ALPHA.TRK", NULL};
	static const legacy_s8 *const new_files[] = {"C:\\GAME\\TWIN.CSTRK", "/game/BETATEST.CSTRK",
												 "/game/TOOLONG99.CSTRK", "/game/.CSTRK", NULL};
	configure_selector(".trk", ".cstrk", old_files, new_files);
	select_name("TWIN", ".cstrk");
	assert(drawn_count >= 5);
	assert(strcmp(drawn[0].text, "ALPHA.trk") == 0);
	assert(strcmp(drawn[1].text, "BETATEST.cstrk") == 0);
	assert((strcmp(drawn[2].text, "TWIN.trk") == 0 && strcmp(drawn[3].text, "TWIN.cstrk") == 0) ||
		   (strcmp(drawn[3].text, "TWIN.trk") == 0 && strcmp(drawn[2].text, "TWIN.cstrk") == 0));
	assert(strcmp(drawn[4].text, "ZZZZZZZZ.trk") == 0);
	assert(saw_color("TWIN.cstrk", 1, dialog_background_color));
	assert(saw_color("TWIN.cstrk", 1, dialog_fnt_colour));
	assert(saw_color("TWIN.trk", dialog_fnt_colour, dialog_background_color));
	assert(saw_color("BETATEST.cstrk", 1, dialog_background_color));

	/* Moving back to the legacy file of the same name resets its format. */
	configure_selector(".trk", ".cstrk", old_files, new_files);
	select_name("TWIN", ".trk");
	configure_selector(".trk", ".cstrk", old_files, new_files);
	select_name("BETATEST", ".cstrk");
}

static void test_replays_keep_identical_stems_and_blue_selection(void)
{
	static const legacy_s8 *const old_files[] = {"SAME.RPL", "OLD.RPL", NULL};
	static const legacy_s8 *const new_files[] = {"SAME.CSRPL", "12345678.CSRPL", NULL};
	configure_selector(".rpl", ".csrpl", old_files, new_files);
	select_name("SAME", ".csrpl");
	assert(strcmp(drawn[0].text, "12345678.csrpl") == 0);
	assert(saw_color("12345678.csrpl", 1, dialog_fnt_colour));
	assert(saw_color("SAME.csrpl", 1, dialog_background_color));
	assert(saw_color("SAME.csrpl", 1, dialog_fnt_colour));
	assert(saw_color("SAME.rpl", dialog_fnt_colour, dialog_background_color));
	configure_selector(".rpl", ".csrpl", old_files, new_files);
	select_name("SAME", ".rpl");
}

static void test_cancel_leaves_name_and_drops_previous_suffix(void)
{
	static const legacy_s8 *const old_files[] = {"OLD.TRK", NULL};
	static const legacy_s8 *const no_files[] = {NULL};
	configure_selector(".trk", ".cstrk", old_files, no_files);
	scripted_keys[0] = KEY_ESCAPE;
	legacy_s8 directory[32] = "tracks", filename[9] = "KEEP";
	assert(do_format_fileselect_dialog(directory, filename, ".trk", "Choose file") == 0);
	assert(strcmp(filename, "KEEP") == 0);
	assert(strcmp(file_dialog_selected_extension(), ".trk") == 0);
	assert(g_is_busy == 9 && modal_depth == 0);
}

int main(void)
{
	test_tracks_keep_identical_stems_and_full_names();
	test_replays_keep_identical_stems_and_blue_selection();
	test_cancel_leaves_name_and_drops_previous_suffix();
	puts("Extended track/replay selectors retain formats, identical stems, "
		 "full names and blue rows.");
	return 0;
}
