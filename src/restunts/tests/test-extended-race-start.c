/* Exercise the real race-start gate; section GC discards unrelated startup
 * and menu code. Formatting details belong to test-extended-validation.c. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../c/restunts.c"
#undef printf
#undef strlen
#undef strcmp
#undef strcpy
#undef memcpy

legacy_s8 idle_expired;
legacy_s8 passed_security;
legacy_u16 dialog_border_color;
legacy_u8 track_validation_column;
legacy_u8 track_validation_row;
legacy_s8 random_wait_legacy_bytes[1];

static legacy_s16 setup_result;
static int extended_active;
static int editor_available;
#define TEST_FUTURE_REVISION 101U
#define TEST_SHORT_FUTURE_REVISION 15U
#define TEST_NEXT_REVISION (CSTRK_VERSION + 1U)

static struct CSTRK_TRACK fixture_track;
static unsigned setup_calls;
static legacy_u16 warning_calls;
static unsigned dialog_calls;
static unsigned menu_calls;
static unsigned file_find_calls;
static unsigned video_status_calls;
static char captured_message[192];
static legacy_s8 captured_warning[TRACK_VERSION_WARNING_SIZE];

legacy_s16 track_setup(void)
{
	assert(warning_calls == (extended_active && fixture_track.version > CSTRK_VERSION ? 1U : 0U));
	++setup_calls;
	assert(idle_expired == 0);
	return setup_result;
}

int extended_track_active(void)
{
	return extended_active;
}

const struct CSTRK_TRACK *extended_track_model(void)
{
	assert(extended_active);
	return &fixture_track;
}

legacy_u16 show_dialog(legacy_s16 type, legacy_s16 save, void far *text, legacy_u16 x, legacy_u16 y,
					   legacy_s16 color, legacy_s16 *flags, legacy_s16 selected)
{
	assert(extended_active && menu_calls == 0);
	assert(save == DIALOG_SAVE_BACKGROUND);
	assert(x == DIALOG_AUTO_POSITION && y == DIALOG_AUTO_POSITION);
	assert(color == dialog_border_color && flags == NULL && selected == 0);
	assert(text != NULL && strlen((const char *)text) < sizeof(captured_message));
	if (setup_calls == 0) {
		assert(fixture_track.version > CSTRK_VERSION && warning_calls == 0);
		assert(type == DIALOG_TYPE_MENU);
		const legacy_char *choice = strchr((const legacy_char *)text, '[');
		assert(choice != NULL && strcmp(choice, "[ OK ]") == 0);
		strcpy((legacy_char *)captured_warning, (const legacy_char *)text);
		++warning_calls;
	} else {
		assert(setup_calls == 1 && setup_result != TRACK_SETUP_OK);
		assert(type == DIALOG_TYPE_ACKNOWLEDGEMENT);
		strcpy(captured_message, (const char *)text);
	}
	++dialog_calls;
	return 0;
}

void run_tracks_menu(legacy_s16 reload)
{
	assert(reload == 1 && setup_calls == 1 && setup_result != TRACK_SETUP_OK);
	/* The player must acknowledge the failure before returning to the menu. */
	assert(dialog_calls == warning_calls + (extended_active ? 1U : 0U));
	++menu_calls;
}

const legacy_s8 *file_find(const legacy_s8 *query)
{
	static const legacy_s8 editor[] = "TEDIT.TRK";
	assert(idle_expired != 0 && strcmp((const char *)query, "tedit.*") == 0);
	++file_find_calls;
	return editor_available ? editor : NULL;
}

legacy_s16 dos_video_get_status(void)
{
	return (legacy_s16)(video_status_calls++ & 1U);
}

legacy_s16 _rand(void)
{
	return 0;
}

legacy_s16 get_kevinrandom(void)
{
	return 0;
}

void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	assert(!"Race-start fixture unexpectedly failed the security check");
}

static void reset_fixture(int extended, legacy_s16 result, legacy_s8 idle)
{
	extended_active = extended;
	fixture_track.version = CSTRK_VERSION;
	setup_result = result;
	idle_expired = idle;
	passed_security = 1;
	dialog_border_color = 9;
	track_validation_column = 17;
	track_validation_row = 31;
	editor_available = 0;
	setup_calls = warning_calls = dialog_calls = menu_calls = file_find_calls = video_status_calls =
		0;
	captured_message[0] = '\0';
	captured_warning[0] = '\0';
}

static void check_success(int extended)
{
	reset_fixture(extended, TRACK_SETUP_OK, 0);
	assert(main_menu_prepare_track() == 1);
	assert(setup_calls == 1 && dialog_calls == 0 && menu_calls == 0);
	assert(file_find_calls == 0 && video_status_calls != 0);
}

static void check_extended_failure(void)
{
	reset_fixture(1, TRACK_SETUP_NO_RUNWAY, 0);
	assert(main_menu_prepare_track() == 0);
	assert(setup_calls == 1 && dialog_calls == 1 && menu_calls == 1);
	assert(file_find_calls == 0 && video_status_calls == 0);
	assert(strstr(captured_message, "Column 18, row 32") != NULL);
	assert(strstr(captured_message, "runway") != NULL);
}

static void check_newer_revision(legacy_u16 revision, legacy_s16 result)
{
	reset_fixture(1, result, 0);
	fixture_track.version = revision;
	assert(main_menu_prepare_track() == (result == TRACK_SETUP_OK ? 1 : 0));
	assert(setup_calls == 1 && warning_calls == 1);
	assert(dialog_calls == (result == TRACK_SETUP_OK ? 1U : 2U));
	assert(menu_calls == (result == TRACK_SETUP_OK ? 0U : 1U));
	legacy_s8 expected[TRACK_VERSION_WARNING_SIZE];
	snprintf((legacy_char *)expected, sizeof(expected), "Track revision v%" LEGACY_PRIu32,
			 (legacy_u32)revision);
	assert(strstr((const legacy_char *)captured_warning, (const legacy_char *)expected) != NULL);
	snprintf((legacy_char *)expected, sizeof(expected), "Game supports revision v%" LEGACY_PRIu32,
			 (legacy_u32)CSTRK_VERSION);
	assert(strstr((const legacy_char *)captured_warning, (const legacy_char *)expected) != NULL);
	assert(strstr((const legacy_char *)captured_warning, "may not work correctly") != NULL);
	assert(strstr((const legacy_char *)captured_warning, "Press OK to continue racing") != NULL);
	if (result != TRACK_SETUP_OK) {
		assert(strstr(captured_message, "runway") != NULL);
	}
}

static void check_legacy_failure(void)
{
	reset_fixture(0, TRACK_SETUP_NO_RUNWAY, 0);
	assert(main_menu_prepare_track() == 0);
	assert(setup_calls == 1 && dialog_calls == 0 && menu_calls == 1);
	assert(file_find_calls == 0 && video_status_calls == 0);
	assert(captured_message[0] == '\0');
}

static void check_idle_editor_gate(int available)
{
	reset_fixture(1, TRACK_SETUP_NO_RUNWAY, 1);
	fixture_track.version = TEST_FUTURE_REVISION;
	editor_available = available;
	assert(main_menu_prepare_track() == (available ? 1 : -1));
	assert(file_find_calls == 1 && setup_calls == 0 && dialog_calls == 0 && menu_calls == 0);
	assert(video_status_calls == 0);
}

int main(void)
{
	check_success(1);
	check_success(0);
	check_extended_failure();
	check_legacy_failure();
	check_newer_revision(TEST_NEXT_REVISION, TRACK_SETUP_OK);
	check_newer_revision(TEST_SHORT_FUTURE_REVISION, TRACK_SETUP_OK);
	check_newer_revision(TEST_FUTURE_REVISION, TRACK_SETUP_OK);
	check_newer_revision(LEGACY_U16_MAX, TRACK_SETUP_OK);
	check_newer_revision(TEST_FUTURE_REVISION, TRACK_SETUP_NO_RUNWAY);
	check_idle_editor_gate(0);
	check_idle_editor_gate(1);
	puts("Extended race-start checks passed");
	return 0;
}
