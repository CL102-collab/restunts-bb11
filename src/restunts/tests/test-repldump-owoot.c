#include <assert.h>
#include <string.h>

#define RESTUNTS_HEADLESS
#include "../repldump/repldump.c"
#include "../c/ui_text.h"

#undef memcpy
#undef strcmp

struct GAMESTATE state;
legacy_s16 owoot_enabled;
legacy_u16 framespersec = 20;
legacy_u16 elapsed_time1;

static legacy_u32 open_count, write_count, close_count, remove_count;
static legacy_u32 output_length;
static legacy_u8 output_bytes[32];
static legacy_u16 expected_write_length;
static legacy_s32 output_exists, create_fails, write_is_short, close_fails;
static const char *expected_name;

legacy_u16 dos_file_open(const legacy_s8 *path, legacy_s16 create)
{
	assert(strcmp((const legacy_char *)path, expected_name) == 0);
	assert(create == DOS_FILE_CREATE);
	open_count++;
	if (create_fails) {
		return 0;
	}
	output_exists = 1;
	output_length = 0;
	return 5;
}

legacy_u16 dos_file_write(legacy_u16 handle, const void far *source, legacy_u16 length)
{
	assert(handle == 5);
	assert(output_exists);
	assert(length == expected_write_length);
	write_count++;
	legacy_u16 written = write_is_short ? length - 1 : length;
	memcpy(output_bytes, source, written);
	output_length = written;
	return written;
}

legacy_s16 dos_file_close(legacy_u16 handle)
{
	assert(handle == 5);
	close_count++;
	return close_fails ? -1 : 0;
}

legacy_s16 dos_file_remove(const legacy_s8 *path)
{
	assert(strcmp((const legacy_char *)path, expected_name) == 0);
	remove_count++;
	output_exists = 0;
	return 0;
}

static void reset_output(const char *name)
{
	memset(&state, 0, sizeof(state));
	memset(output_bytes, 0xcc, sizeof(output_bytes));
	open_count = write_count = close_count = remove_count = output_length = 0;
	output_exists = create_fails = write_is_short = close_fails = 0;
	expected_name = name;
	owoot_enabled = 1;
	time_enabled = 0;
	framespersec = 20;
	elapsed_time1 = 0;
	expected_write_length = 4;
}

static void assert_result(const char *result)
{
	assert(output_exists && output_length == 4);
	assert(memcmp(output_bytes, result, 4) == 0);
	assert(output_bytes[4] == 0xcc);
	assert(open_count == 1 && write_count == 1 && close_count == 1 && remove_count == 1);
}

static void test_replay_extensions(void)
{
	static const char *names[] = {"R0000.rpl", "R0000.RPL", "R0000.RpL", "R0000"};
	for (legacy_u32 index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
		legacy_s8 name[REPLDUMP_OUTPUT_NAME_SIZE];
		strcpy(name, names[index]);
		repldump_strip_replay_extension(name);
		assert(strcmp((const legacy_char *)name, "R0000") == 0);
	}
	legacy_s8 name[] = "R0000.rpx";
	repldump_strip_replay_extension(name);
	assert(strcmp((const legacy_char *)name, "R0000.rpx") == 0);
}

static void test_output_names(void)
{
	struct {
		legacy_u8 before;
		legacy_s8 name[REPLDUMP_OUTPUT_NAME_SIZE];
		legacy_u8 after;
	} output;
	memset(&output, 0xcc, sizeof(output));
	assert(repldump_output_name(output.name, "12345678", ".owo"));
	assert(strcmp((const legacy_char *)output.name, "12345678.owo") == 0);
	assert(output.before == 0xcc && output.after == 0xcc);
	assert(repldump_output_name(output.name, "R0000", ".BNI"));
	assert(strcmp((const legacy_char *)output.name, "R0000.BNI") == 0);
	memset(output.name, 0xcc, sizeof(output.name));
	assert(!repldump_output_name(output.name, "123456789", ".owo"));
	for (legacy_u32 index = 0; index < sizeof(output.name); index++) {
		assert((legacy_u8)output.name[index] == 0xcc);
	}
	assert(output.before == 0xcc && output.after == 0xcc);
	reset_output("123456789.owo");
	assert(!repldump_write_owoot_result("123456789", 1));
	assert(open_count == 0 && write_count == 0 && close_count == 0 && remove_count == 0);
}

static void test_result_contents(void)
{
	reset_output("R0000.owo");
	assert(repldump_write_owoot_result("R0000", 1));
	assert_result("pass");
	reset_output("R0019.owo");
	/* A new validation must replace a successful verdict from an earlier run. */
	output_exists = 1;
	output_length = 4;
	memcpy(output_bytes, "pass", 4);
	assert(repldump_write_owoot_result("R0019", 0));
	assert_result("fail");
}

static void test_completion_requires_finish(void)
{
	static const legacy_s8 events[] = {CRASH_EVENT_NONE,  CRASH_EVENT_COLLISION,
									   CRASH_EVENT_WATER, CRASH_EVENT_FINISH,
									   CRASH_EVENT_EXIT,  CRASH_EVENT_IMMEDIATE_STOP};
	for (legacy_u32 index = 0; index < sizeof(events) / sizeof(events[0]); index++) {
		reset_output("R0000.owo");
		state.playerstate.car_crashBmpFlag = events[index];
		assert(repldump_complete_owoot_result("R0000"));
		assert_result(events[index] == CRASH_EVENT_FINISH ? "pass" : "fail");
	}
}

static void test_disabled_does_not_touch_results(void)
{
	reset_output("R0000.owo");
	owoot_enabled = 0;
	assert(repldump_write_owoot_result("R0000", 0));
	assert(!output_exists);
	assert(open_count == 0 && write_count == 0 && close_count == 0 && remove_count == 0);
	output_exists = 1;
	output_length = 4;
	memcpy(output_bytes, "fail", 4);
	state.playerstate.car_crashBmpFlag = CRASH_EVENT_FINISH;
	assert(repldump_write_owoot_result("R0000", 1));
	assert(repldump_complete_owoot_result("R0000"));
	assert(output_exists && output_length == 4 && memcmp(output_bytes, "fail", 4) == 0);
	assert(open_count == 0 && write_count == 0 && close_count == 0 && remove_count == 0);
}

static void test_io_failures_remove_verdict(void)
{
	reset_output("R0000.owo");
	output_exists = 1;
	memcpy(output_bytes, "pass", 4);
	create_fails = 1;
	assert(!repldump_write_owoot_result("R0000", 1));
	assert(!output_exists);
	assert(open_count == 1 && write_count == 0 && close_count == 0 && remove_count == 1);

	reset_output("R0000.owo");
	write_is_short = 1;
	assert(!repldump_write_owoot_result("R0000", 1));
	assert(!output_exists);
	assert(open_count == 1 && write_count == 1 && close_count == 1 && remove_count == 2);

	reset_output("R0000.owo");
	close_fails = 1;
	assert(!repldump_write_owoot_result("R0000", 1));
	assert(!output_exists);
	assert(open_count == 1 && write_count == 1 && close_count == 1 && remove_count == 2);
}

static void enable_time_output(const legacy_char *name)
{
	reset_output(name);
	time_enabled = 1;
	state.playerstate.car_crashBmpFlag = CRASH_EVENT_FINISH;
	state.game_pEndFrame = 3660;
	/* Penalties and later replay frames do not belong in driving time. */
	state.game_penalty = 720;
	state.game_total_finish = 4260;
	state.game_frame = 3680;
	expected_write_length = 13;
}

static void assert_time_result(const legacy_char *result)
{
	legacy_u16 length = (legacy_u16)strlen(result);
	assert(output_exists && output_length == length);
	assert(memcmp(output_bytes, result, length) == 0);
	assert(output_bytes[length] == 0xcc);
	assert(open_count == 1 && close_count == 1);
	assert(write_count == (length != 0 ? 1U : 0U));
}

static void test_time_option_parser(void)
{
	static const legacy_char *options[] = {"--time",  "--TiMe",	   "time",	  "/time",
										   "--times", "--time:on", "--time ", "--time:off"};
	for (legacy_u16 index = 0; index < sizeof(options) / sizeof(options[0]); index++) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"repldump", (legacy_s8 *)"R0000.rpl",
								  (legacy_s8 *)options[index]};
		time_enabled = 1;
		repldump_configure_time(3, arguments);
		assert(time_enabled == (index < 2 ? 1 : 0));
	}
	legacy_s8 *arguments[] = {(legacy_s8 *)"--time", (legacy_s8 *)"--time", (legacy_s8 *)"--TIME"};
	repldump_configure_time(3, arguments);
	assert(time_enabled);
	repldump_configure_time(1, arguments);
	assert(!time_enabled);
	/* argv[1] is the replay stem, even when its filename is literally --time. */
	legacy_s8 *replay_only[] = {(legacy_s8 *)"repldump", (legacy_s8 *)"--time"};
	time_enabled = 1;
	repldump_configure_time(2, replay_only);
	assert(!time_enabled);
	replay_only[1] = (legacy_s8 *)"R0000";
	time_enabled = 1;
	repldump_configure_time(2, replay_only);
	assert(!time_enabled);
}

static void test_time_matches_viewer_formatter(void)
{
	static const struct {
		legacy_u16 rate, frames;
		const legacy_char *expected;
	} cases[] = {{20, 0, "0:00.00"},	{20, 3660, "3:03.00"}, {20, 3661, "3:03.05"},
				 {20, 3659, "3:02.95"}, {20, 1199, "0:59.95"}, {20, 1200, "1:00.00"},
				 {10, 1833, "3:03.30"}, {30, 3599, "1:59.87"}, {20, 65535U, "54:36.75"}};
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		legacy_s8 formatted[32], viewer[32];
		memset(formatted, 0xcc, sizeof(formatted));
		framespersec = cases[index].rate;
		legacy_u16 length = repldump_format_time(formatted, cases[index].frames);
		assert(length == strlen(cases[index].expected));
		assert(memcmp(formatted, cases[index].expected, length) == 0);
		assert(formatted[length] == 0);
		assert((legacy_u8)formatted[length + 1U] == 0xcc);
		format_frame_as_string(viewer, LEGACY_S16_FROM_BITS(cases[index].frames), 1);
		legacy_s8 *digits = viewer;
		while (*digits == ' ') {
			digits++;
		}
		assert(strlen(digits) == length && memcmp(formatted, digits, length) == 0);
	}
}

static void test_penalty_time_formatter(void)
{
	static const struct {
		legacy_u16 rate, frames;
		const legacy_char *expected;
	} cases[] = {{20, 0, "0:00"},	 {20, 600, "0:30"}, {20, 4800, "4:00"}, {20, 1199, "0:59"},
				 {20, 1200, "1:00"}, {10, 301, "0:30"}, {10, 2400, "4:00"}, {20, 65535U, "54:36"}};
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		legacy_s8 formatted[32];
		memset(formatted, 0xcc, sizeof(formatted));
		framespersec = cases[index].rate;
		legacy_u16 length = repldump_format_penalty_time(formatted, cases[index].frames);
		assert(length == strlen(cases[index].expected));
		assert(memcmp(formatted, cases[index].expected, length) == 0);
		assert(formatted[length] == 0);
		assert((legacy_u8)formatted[length + 1U] == 0xcc);
	}
}

static void test_time_file_contents(void)
{
	enable_time_output("R0000.tim");
	assert(repldump_write_time_result("R0000", 1));
	assert_time_result("3:03.00\r\n0:30");
	/* Both lines use latched finish fields, despite a changing live penalty. */
	enable_time_output("R0000.tim");
	state.game_total_finish = state.game_pEndFrame;
	state.game_penalty = 4800;
	assert(repldump_write_time_result("R0000", 1));
	assert_time_result("3:03.00\r\n0:00");
	enable_time_output("R0000.tim");
	state.game_total_finish = 8460;
	state.game_penalty = 0;
	assert(repldump_write_time_result("R0000", 1));
	assert_time_result("3:03.00\r\n4:00");
	enable_time_output("R0000.tim");
	state.game_pEndFrame = 3659;
	elapsed_time1 = 2;
	state.game_total_finish = 4261;
	assert(repldump_write_time_result("R0000", 1));
	assert_time_result("3:03.05\r\n0:30");
	enable_time_output("R0000.tim");
	state.game_pEndFrame = 3641;
	elapsed_time1 = 20;
	state.game_total_finish = 4261;
	assert(repldump_write_time_result("R0000", 1));
	assert_time_result("3:03.05\r\n0:30");
	enable_time_output("R0000.tim");
	state.game_pEndFrame = LEGACY_S16_FROM_BITS(65535U);
	elapsed_time1 = 1;
	state.game_total_finish = 0;
	assert(repldump_write_time_result("R0000", 1));
	assert_time_result("0:00.00\r\n0:00");
	/* Wrapped score subtraction must still recover the original 30 seconds. */
	enable_time_output("R0000.tim");
	state.game_pEndFrame = LEGACY_S16_FROM_BITS(65530U);
	state.game_total_finish = 594;
	expected_write_length = 14;
	assert(repldump_write_time_result("R0000", 1));
	assert_time_result("54:36.50\r\n0:30");
}

static void test_unfinished_time_file_is_empty(void)
{
	static const legacy_s8 events[] = {CRASH_EVENT_NONE, CRASH_EVENT_COLLISION, CRASH_EVENT_WATER,
									   CRASH_EVENT_EXIT, CRASH_EVENT_IMMEDIATE_STOP};
	for (legacy_u16 index = 0; index < sizeof(events) / sizeof(events[0]); index++) {
		enable_time_output("R0000.tim");
		state.playerstate.car_crashBmpFlag = events[index];
		output_exists = 1;
		output_length = 13;
		memcpy(output_bytes, "3:03.00\r\n0:30", 13);
		assert(repldump_write_time_result("R0000", 1));
		assert(output_exists && output_length == 0);
		assert(open_count == 1 && close_count == 1 && write_count == 0);
	}
	/* Pre-simulation initialization must discard even a previously latched finish. */
	enable_time_output("R0000.tim");
	assert(repldump_write_time_result("R0000", 0));
	assert_time_result("");
}

static void test_time_disabled_does_not_touch_results(void)
{
	reset_output("R0000.tim");
	output_exists = 1;
	output_length = 13;
	memcpy(output_bytes, "3:03.00\r\n0:30", 13);
	state.playerstate.car_crashBmpFlag = CRASH_EVENT_FINISH;
	assert(repldump_write_time_result("R0000", 0));
	assert(repldump_write_time_result("R0000", 1));
	assert(output_exists && output_length == 13 &&
		   memcmp(output_bytes, "3:03.00\r\n0:30", 13) == 0);
	assert(open_count == 0 && write_count == 0 && close_count == 0 && remove_count == 0);
}

static void test_time_io_failures_remove_output(void)
{
	enable_time_output("R0000.tim");
	output_exists = 1;
	output_length = 13;
	memcpy(output_bytes, "3:03.00\r\n0:30", 13);
	create_fails = 1;
	assert(!repldump_write_time_result("R0000", 1));
	assert(!output_exists && write_count == 0);
	enable_time_output("R0000.tim");
	write_is_short = 1;
	assert(!repldump_write_time_result("R0000", 1));
	assert(!output_exists && close_count == 1);
	enable_time_output("R0000.tim");
	close_fails = 1;
	assert(!repldump_write_time_result("R0000", 1));
	assert(!output_exists);
	enable_time_output("R0000.tim");
	close_fails = 1;
	assert(!repldump_write_time_result("R0000", 0));
	assert(!output_exists);
	enable_time_output("123456789.tim");
	assert(!repldump_write_time_result("123456789", 1));
	assert(open_count == 0 && write_count == 0 && close_count == 0 && remove_count == 0);
}

int main(void)
{
	test_replay_extensions();
	test_output_names();
	test_result_contents();
	test_completion_requires_finish();
	test_disabled_does_not_touch_results();
	test_io_failures_remove_verdict();
	test_time_option_parser();
	test_time_matches_viewer_formatter();
	test_penalty_time_formatter();
	test_time_file_contents();
	test_unfinished_time_file_is_empty();
	test_time_disabled_does_not_touch_results();
	test_time_io_failures_remove_output();
	return 0;
}
