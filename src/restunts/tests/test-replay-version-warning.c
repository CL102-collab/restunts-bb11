/* Exercise the real replay-selection boundary; section GC discards unrelated I/O. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../c/externs.h"
#include "../c/fileio.h"
#include "../c/platform.h"
#include "../c/replay.h"
#include "../c/ui_dialog.h"
#include "../c/extended_replay_runtime.h"
#include "../c/driving_rules.h"
#undef memcpy
#undef strcmp
#undef strcpy
#undef strlen
#undef printf

#define TEST_PATH_SIZE 128U
#define TEST_MESSAGE_SIZE 512U
#define TEST_WARNING_CAPACITY 2U
#define TEST_FUTURE_REVISION 101U
#define TEST_SHORT_FUTURE_REVISION 15U
#define TEST_FILE_HANDLE 1U

legacy_s8 g_is_busy;
legacy_s8 g_path_buf[TEST_PATH_SIZE];
legacy_s8 replay_filename[REPLAY_FILENAME_SIZE];
legacy_u16 dialog_border_color;
struct GAMEINFO gameconfig;
static legacy_s8 legacy_header[REPLAY_GAMEINFO_SIZE];
legacy_s8 *replay_header_buffer = legacy_header;
#ifndef RESTUNTS_HEADLESS
const legacy_s8 file_read_error_format[] = "Unexpected replay read error: %s";
#endif

static legacy_u16 fixture_version, fixture_rules, current_rules;
static legacy_int fixture_load_result;
static legacy_u16 load_calls, warning_calls, version_calls, rules_calls, current_rules_calls;
static legacy_u16 unload_calls, clear_calls, legacy_decode_calls;
static legacy_char captured_message[TEST_MESSAGE_SIZE];
static legacy_char captured_messages[TEST_WARNING_CAPACITY][TEST_MESSAGE_SIZE];
static legacy_s32 file_position;

int extended_file_load_replay(const legacy_s8 *path)
{
	assert(g_is_busy == 1);
	assert(strcmp((const legacy_char *)path, "Fixture\\RECORDED.csrpl") == 0);
	++load_calls;
	return fixture_load_result;
}

legacy_u16 extended_replay_version(void)
{
	assert(load_calls == 1 && fixture_load_result == 0 && g_is_busy == 0);
	++version_calls;
	return fixture_version;
}

legacy_u16 extended_replay_driving_rules(void)
{
	assert(load_calls == 1 && fixture_load_result == 0 && g_is_busy == 0);
	++rules_calls;
	return fixture_rules;
}

legacy_u16 driving_rules_current(void)
{
	assert(g_is_busy == 0);
	++current_rules_calls;
	return current_rules;
}

void extended_track_unload(void)
{
	++unload_calls;
}
void extended_replay_runtime_clear(void)
{
	++clear_calls;
}

legacy_u16 show_dialog(legacy_s16 type, legacy_s16 save, void far *text, legacy_u16 x, legacy_u16 y,
					   legacy_s16 color, legacy_s16 *disabled, legacy_s16 selected)
{
	assert(g_is_busy == 0 && load_calls == 1 && fixture_load_result == 0);
	assert(type == DIALOG_TYPE_MENU && save == DIALOG_SAVE_BACKGROUND);
	assert(x == DIALOG_AUTO_POSITION && y == DIALOG_AUTO_POSITION);
	assert(color == dialog_border_color && disabled == NULL && selected == 0);
	assert(text != NULL && strlen((const legacy_char *)text) < sizeof(captured_message));
	const legacy_char *choice = strchr((const legacy_char *)text, '[');
	assert(choice != NULL && strcmp(choice, "[ OK ]") == 0);
	assert(warning_calls < TEST_WARNING_CAPACITY);
	strcpy(captured_message, (const legacy_char *)text);
	strcpy(captured_messages[warning_calls], (const legacy_char *)text);
	++warning_calls;
	return 0;
}

legacy_u16 dos_file_open(const legacy_s8 *path, legacy_s16 create)
{
	assert(strcmp((const legacy_char *)path, "Fixture\\LEGACY.rpl") == 0);
	assert(create == DOS_FILE_OPEN_EXISTING && g_is_busy == 1);
	file_position = 0;
	return TEST_FILE_HANDLE;
}
legacy_s16 dos_file_seek(legacy_u16 handle, legacy_s32 offset, legacy_s16 origin)
{
	assert(handle == TEST_FILE_HANDLE && offset == 0);
	assert(origin == DOS_FILE_SEEK_BEGIN || origin == DOS_FILE_SEEK_END);
	file_position = origin == DOS_FILE_SEEK_END ? sizeof(legacy_header) : 0;
	return 0;
}
legacy_s32 dos_file_tell(legacy_u16 handle)
{
	assert(handle == TEST_FILE_HANDLE);
	return file_position;
}
legacy_u16 dos_file_read(legacy_u16 handle, void far *destination, legacy_u16 count)
{
	assert(handle == TEST_FILE_HANDLE && count == sizeof(legacy_header));
	assert(destination == legacy_header && file_position == 0);
	memset(destination, 0, count);
	file_position = count;
	return count;
}
legacy_s16 dos_file_close(legacy_u16 handle)
{
	assert(handle == TEST_FILE_HANDLE);
	return 0;
}
legacy_s16 dos_file_error(void)
{
	return 0;
}
void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	assert(!"Unexpected replay read failure");
}
void replay_gameinfo_decode(struct GAMEINFO *out, const legacy_u8 far *header)
{
	assert(out == &gameconfig && header == (const legacy_u8 *)legacy_header);
	++legacy_decode_calls;
}

static void reset_fixture(void)
{
	g_is_busy = 0;
	dialog_border_color = 9;
	load_calls = warning_calls = version_calls = rules_calls = current_rules_calls = 0;
	fixture_version = CSRPL_VERSION;
	fixture_load_result = 0;
	fixture_rules = current_rules = 0;
	unload_calls = clear_calls = legacy_decode_calls = 0;
	captured_message[0] = '\0';
	memset(captured_messages, 0, sizeof(captured_messages));
	strcpy((legacy_char *)replay_filename, "PREVIOUS");
}

#ifndef RESTUNTS_HEADLESS
static void check_revision(legacy_u16 revision)
{
	reset_fixture();
	fixture_version = revision;
	fixture_load_result = 0;
	assert(file_load_replay_format("Fixture", "RECORDED", ".csrpl") == 0);
	assert(load_calls == 1 && version_calls == 1 && rules_calls == 1 && g_is_busy == 0);
	assert(current_rules_calls == 1);
	assert(unload_calls == 0 && clear_calls == 0 && legacy_decode_calls == 0);
	assert(strcmp((const legacy_char *)replay_filename, "RECORDED") == 0);
	assert(warning_calls == (revision > CSRPL_VERSION));
	if (warning_calls != 0) {
		legacy_char expected[TEST_MESSAGE_SIZE];
		snprintf(expected, sizeof(expected), "Replay recorded with revision v%" LEGACY_PRIu32,
				 (legacy_u32)revision);
		assert(strstr(captured_message, expected) != NULL);
		snprintf(expected, sizeof(expected), "Game records revision v%" LEGACY_PRIu32,
				 (legacy_u32)CSRPL_VERSION);
		assert(strstr(captured_message, expected) != NULL);
		assert(strstr(captured_message, "is newer") != NULL);
		assert(strstr(captured_message, "Physics may differ") != NULL);
		assert(strstr(captured_message, "playback may desync") != NULL);
		assert(strstr(captured_message, "Press OK to continue playback") != NULL);
	}
}

static const struct {
	legacy_u16 bit;
	const legacy_char *name;
} test_rules[] = {{DRIVING_RULE_POWER_GEAR, "Power Gear"},
				  {DRIVING_RULE_LEGACY_COLLISION, "Original Collision Bugs"},
				  {DRIVING_RULE_LEFT_CORNER_BIAS, "Left Corner Bias"},
				  {DRIVING_RULE_DUAL_WAY_SWITCHING, "Dual-Way Switching"},
				  {DRIVING_RULE_CHICANE_FAST_GRASS, "Chicane Fast-Grass"},
				  {DRIVING_RULE_AMIGA_GRASS, "Amiga Grass"},
				  {DRIVING_RULE_OWOOT, "OWOOT"}};

static void check_driving_rules(legacy_u16 running, legacy_u16 recorded, legacy_u16 version)
{
	reset_fixture();
	current_rules = running;
	fixture_rules = recorded;
	fixture_version = version;
	assert(file_load_replay_format("Fixture", "RECORDED", ".csrpl") == 0);
	assert(load_calls == 1 && version_calls == 1 && rules_calls == 1 && g_is_busy == 0);
	assert(current_rules_calls == 1 && current_rules == running);
	assert(warning_calls == (version > CSRPL_VERSION) + (recorded != running));
	if (version > CSRPL_VERSION) {
		assert(strstr(captured_messages[0], "is newer") != NULL);
	}
	if (recorded != running) {
		assert(strstr(captured_message, "different driving rules") != NULL);
		assert(strstr(captured_message, "Playback may desync") != NULL);
		const legacy_char *next = strstr(captured_message, "Recorded rules:");
		assert(next != NULL);
		for (size_t index = 0; index < sizeof(test_rules) / sizeof(test_rules[0]); index++) {
			legacy_char expected[TEST_MESSAGE_SIZE];
			snprintf(expected, sizeof(expected), "%s: %s]", test_rules[index].name,
					 recorded & test_rules[index].bit ? "On" : "Off");
			const legacy_char *line = strstr(next, expected);
			assert(line != NULL);
			next = line + strlen(expected);
		}
		assert(strstr(next, "Press OK to continue playback") != NULL);
	}
}
#endif

int main(void)
{
#ifndef RESTUNTS_HEADLESS
	check_revision(CSRPL_VERSION);
	check_revision(CSRPL_VERSION + 1U);
	check_revision(TEST_SHORT_FUTURE_REVISION);
	check_revision(TEST_FUTURE_REVISION);
	check_revision(LEGACY_U16_MAX);

	/* Every matching startup setting is silent, not just the default mask. */
	for (legacy_u16 rules = 0; rules <= DRIVING_RULE_KNOWN_MASK; rules++) {
		check_driving_rules(rules, rules, CSRPL_VERSION);
	}
	for (size_t index = 0; index < sizeof(test_rules) / sizeof(test_rules[0]); index++) {
		check_driving_rules(0, test_rules[index].bit, CSRPL_VERSION);
		check_driving_rules(DRIVING_RULE_KNOWN_MASK,
							DRIVING_RULE_KNOWN_MASK ^ test_rules[index].bit, CSRPL_VERSION);
	}
	check_driving_rules(DRIVING_RULE_KNOWN_MASK, 0, CSRPL_VERSION);
	check_driving_rules(0, DRIVING_RULE_KNOWN_MASK, CSRPL_VERSION);
	check_driving_rules(0, DRIVING_RULE_KNOWN_MASK, TEST_FUTURE_REVISION);

	/* Older revisions are rejected before selection; the warning helper is silent. */
	reset_fixture();
	file_warn_replay_version(CSRPL_VERSION - 1U);
	file_warn_replay_version(0U);
	assert(warning_calls == 0 && version_calls == 0 && load_calls == 0);

	reset_fixture();
	fixture_version = TEST_FUTURE_REVISION;
	fixture_rules = DRIVING_RULE_KNOWN_MASK;
	fixture_load_result = 1;
	assert(file_load_replay_format("Fixture", "RECORDED", ".csrpl") != 0);
	assert(load_calls == 1 && version_calls == 0 && rules_calls == 0 && warning_calls == 0);
	assert(current_rules_calls == 0 && g_is_busy == 0);
	assert(strcmp((const legacy_char *)replay_filename, "PREVIOUS") == 0);

	reset_fixture();
	fixture_version = TEST_FUTURE_REVISION;
	fixture_rules = DRIVING_RULE_KNOWN_MASK;
	assert(file_load_replay_format("Fixture", "LEGACY", ".rpl") == 0);
	assert(load_calls == 0 && version_calls == 0 && rules_calls == 0 && warning_calls == 0);
	assert(current_rules_calls == 0 && g_is_busy == 0);
	assert(unload_calls == 1 && clear_calls == 1 && legacy_decode_calls == 1);
	assert(strcmp((const legacy_char *)replay_filename, "LEGACY") == 0);
#else
	reset_fixture();
	file_warn_replay_version(TEST_FUTURE_REVISION);
	file_warn_replay_driving_rules(DRIVING_RULE_KNOWN_MASK);
	assert(warning_calls == 0 && current_rules_calls == 0);
#endif
	puts("Replay revision and driving-rules warnings passed");
	return 0;
}
