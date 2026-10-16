#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../c/command_line.h"
#include "../c/platform.h"

#define COMMAND_LINE_TEST_OUTPUT_CAPACITY 8192U
#ifdef RESTUNTS_SDL3
#define COMMAND_LINE_TEST_GROUP_SEPARATOR "\n\n"
#else
#define COMMAND_LINE_TEST_GROUP_SEPARATOR "\r\n\r\n"
#endif

#define COMMAND_LINE_TEST_LARGE_ARGUMENT_COUNT (LEGACY_U16_SIGN_BIT + 1U)
#define COMMAND_LINE_TEST_ARGUMENT_COUNT(arguments)                                                \
	((legacy_s16)(sizeof(arguments) / sizeof((arguments)[0])))

#ifndef COMMAND_LINE_TEST_VERSION
#define COMMAND_LINE_TEST_VERSION "Version 0123456 (Feb 12 1991)"
#endif

enum COMMAND_LINE_TEST_WRITE_MODE {
	COMMAND_LINE_TEST_WRITE_COMPLETE,
	COMMAND_LINE_TEST_WRITE_FAILED,
	COMMAND_LINE_TEST_WRITE_SHORT
};

static legacy_char output[COMMAND_LINE_TEST_OUTPUT_CAPACITY];
static legacy_u16 output_length;
static legacy_u16 write_calls;
static legacy_u16 fail_on_call;
static enum COMMAND_LINE_TEST_WRITE_MODE write_mode;

legacy_s16 dos_write_stdout(const legacy_s8 *text, legacy_u16 length)
{
	write_calls++;
	if (write_mode == COMMAND_LINE_TEST_WRITE_FAILED && write_calls == fail_on_call) {
		return -1;
	}
	if (write_mode == COMMAND_LINE_TEST_WRITE_SHORT && write_calls == fail_on_call && length != 0) {
		length--;
	}
	assert(output_length + length < sizeof(output));
	memcpy(output + output_length, text, length);
	output_length += length;
	output[output_length] = '\0';
	return (legacy_s16)length;
}

static legacy_s16 run_arguments(legacy_s32 count, legacy_s8 *arguments[])
{
	output_length = 0;
	write_calls = 0;
	output[0] = '\0';
	return command_line_print_info(count, arguments);
}

static void expect_text(const legacy_char *text)
{
	assert(strstr(output, text) != NULL);
}

static const legacy_char *expect_help_group(const legacy_char *cursor,
											const legacy_char *const options[], legacy_u16 count)
{
	const legacy_char *separator = strstr(cursor, COMMAND_LINE_TEST_GROUP_SEPARATOR);
	assert(separator != NULL);
	assert(strstr(cursor, options[0]) == cursor);
	for (legacy_u16 index = 0; index < count; index++) {
		const legacy_char *option = strstr(cursor, options[index]);
		assert(option != NULL && option < separator);
		cursor = option + strlen(options[index]);
	}
	return separator + strlen(COMMAND_LINE_TEST_GROUP_SEPARATOR);
}

static void test_help_contents(void)
{
	legacy_s8 *arguments[] = {(legacy_s8 *)"restunts", (legacy_s8 *)"--help"};
	assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(arguments), arguments) ==
		   COMMAND_LINE_SUCCESS);
	static const legacy_char *info_options[] = {"  --help, -h", "  --version, -v",
#if defined(RESTUNTS_SDL3) && !defined(__DJGPP__)
												"  --licenses"
#endif
	};
	static const legacy_char *startup_options[] = {
#ifdef RESTUNTS_SDL3
		"  --data-dir DIR",
#endif
		"  --nointro",
#ifdef RESTUNTS_SDL3
		"  --ogg:on|off",
		"  --fps:on|off",
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
		"  --control-layout:on|off",
#endif
#endif
	};
	static const legacy_char *physics_options[] = {"  --pg:on|off", "  --lc:on|off",
												   "  --lcb:on|off", "  /amigagrass", "  --owoot"};
#ifdef RESTUNTS_SDL3
	static const legacy_char *hypervision_options[] = {"  --hv:auto", "  --hv:full", "  --hv:high",
													   "  --hv:medium", "  --hv:low"};
#endif
	static const legacy_char *legacy_options[] = {"  /ns", "  /sXX", "  /h", "  /nd"};
	const legacy_char *cursor = strstr(output, info_options[0]);
	assert(cursor != NULL);
	cursor =
		expect_help_group(cursor, info_options, COMMAND_LINE_TEST_ARGUMENT_COUNT(info_options));
	cursor = expect_help_group(cursor, startup_options,
							   COMMAND_LINE_TEST_ARGUMENT_COUNT(startup_options));
	cursor = expect_help_group(cursor, physics_options,
							   COMMAND_LINE_TEST_ARGUMENT_COUNT(physics_options));
#ifdef RESTUNTS_SDL3
	cursor = expect_help_group(cursor, hypervision_options,
							   COMMAND_LINE_TEST_ARGUMENT_COUNT(hypervision_options));
#endif
	cursor =
		expect_help_group(cursor, legacy_options, COMMAND_LINE_TEST_ARGUMENT_COUNT(legacy_options));
	assert(strstr(cursor, "Defaults:") == cursor);
	assert(strstr(output, "ss:") == NULL);
	static const legacy_char *removed_options[] = {
		"/nointro", "/pg:",	 "/lc:",		 "/lcb:",		"/owoot",
		"  lcb:",	"  hv:", "--amigagrass", "/amigagrass:"};
	for (legacy_u16 index = 0; index < COMMAND_LINE_TEST_ARGUMENT_COUNT(removed_options); index++) {
		assert(strstr(output, removed_options[index]) == NULL);
	}
	expect_text("Defaults: intro and sound on; --pg:on, --lc:on, --lcb:on; --owoot off.");
	expect_text("Amiga grass grip starts off; enable with /amigagrass.");
	expect_text("Use original Amiga grass grip (stock cars).");
	expect_text("no effect");
#ifdef RESTUNTS_SDL3
	expect_text("HyperVision starts off.");
	expect_text("Ogg music starts off.");
	expect_text("FPS display starts off.");
	expect_text("F11 toggles it during play.");
	expect_text("FPS display options are case-insensitive.");
	expect_text("Shift+F10 switches music sources outside races.");
	expect_text("Ogg music options are also case-insensitive.");
#ifdef __DJGPP__
	assert(strstr(output, "--licenses") == NULL);
#endif
#else
	assert(strstr(output, "--data-dir") == NULL);
	assert(strstr(output, "--ogg") == NULL);
	assert(strstr(output, "--fps") == NULL);
	assert(strstr(output, "hv:") == NULL);
	assert(strstr(output, "--licenses") == NULL);
	expect_text("AdLib");
	expect_text("Hercules");
	expect_text("AD");
	expect_text("PC");
	expect_text("MT");
	expect_text("TD");
	expect_text("SB");
	expect_text("AdLib is the default driver.");
#endif
}

static void test_aliases_and_version(void)
{
	static const legacy_char *aliases[][2] = {{"--help", "-h"}, {"--version", "-v"}};
	legacy_char long_form_output[COMMAND_LINE_TEST_OUTPUT_CAPACITY];
	for (legacy_u16 index = 0; index < COMMAND_LINE_TEST_ARGUMENT_COUNT(aliases); index++) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"restunts", (legacy_s8 *)aliases[index][0]};
		assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(arguments), arguments) ==
			   COMMAND_LINE_SUCCESS);
		strcpy(long_form_output, output);
		arguments[1] = (legacy_s8 *)aliases[index][1];
		assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(arguments), arguments) ==
			   COMMAND_LINE_SUCCESS);
		assert(strcmp(long_form_output, output) == 0);
	}
	expect_text("Chocolate Stunts (Restunts)");
	expect_text(COMMAND_LINE_TEST_VERSION);
	assert(strstr(output, "0123456789") == NULL);
	assert(strstr(output, "--help") == NULL);
}

static void test_normal_and_similar_arguments(void)
{
	static const legacy_char *normal_arguments[] = {
		"/h",		 "/sAD",	   "/ns",		"/nd",		 "--nointro",  "--hv:full",
		"--pg:off",	 "--lc:on",	   "--lcb:off", "--owoot",	 "--licenses", "--data-dir",
		"",			 "-H",		   "-V",		"-help",	 "-version",   "/help",
		"/v",		 "/version",   "help",		"version",	 "--helpful",  "--version=1",
		"-hv",		 "-vh",		   "--ogg:on",	"--ogg:off", "--hv:auto",  "--fps:on",
		"--fps:off", "/amigagrass"};
	legacy_s8 *executable_only[] = {(legacy_s8 *)"--help"};
	assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(executable_only), executable_only) ==
		   COMMAND_LINE_CONTINUE);
	assert(output_length == 0);
	for (legacy_u16 index = 0; index < COMMAND_LINE_TEST_ARGUMENT_COUNT(normal_arguments);
		 index++) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"restunts", (legacy_s8 *)normal_arguments[index]};
		assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(arguments), arguments) ==
			   COMMAND_LINE_CONTINUE);
		assert(output_length == 0);
	}
}

static void test_position_and_precedence(void)
{
	legacy_s8 *help_first[] = {(legacy_s8 *)"restunts", (legacy_s8 *)"--nointro",
							   (legacy_s8 *)"--hv:invalid", (legacy_s8 *)"-h",
							   (legacy_s8 *)"--version"};
	assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(help_first), help_first) ==
		   COMMAND_LINE_SUCCESS);
	expect_text("--help");

	legacy_s8 *version_first[] = {(legacy_s8 *)"restunts", (legacy_s8 *)"/ns", (legacy_s8 *)"-v",
								  (legacy_s8 *)"--help"};
	assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(version_first), version_first) ==
		   COMMAND_LINE_SUCCESS);
	expect_text(COMMAND_LINE_TEST_VERSION);
	assert(strstr(output, "--help") == NULL);

	legacy_s8 *data_directory[] = {(legacy_s8 *)"restunts", (legacy_s8 *)"--data-dir",
								   (legacy_s8 *)"missing-data", (legacy_s8 *)"/ns",
								   (legacy_s8 *)"--help"};
	assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(data_directory), data_directory) ==
		   COMMAND_LINE_SUCCESS);
	expect_text("--help");

	/* A late --data-dir is not recognized by the SDL3 entry point. */
	legacy_s8 *late_directory[] = {(legacy_s8 *)"restunts", (legacy_s8 *)"/ns",
								   (legacy_s8 *)"--data-dir", (legacy_s8 *)"--help"};
	assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(late_directory), late_directory) ==
		   COMMAND_LINE_SUCCESS);
	expect_text("--help");
}

static void test_data_directory_operands(void)
{
	static const legacy_char *flags[] = {"--help", "-h", "--version", "-v"};
	for (legacy_u16 index = 0; index < COMMAND_LINE_TEST_ARGUMENT_COUNT(flags); index++) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"restunts", (legacy_s8 *)"--data-dir",
								  (legacy_s8 *)flags[index]};
#ifdef RESTUNTS_SDL3
		assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(arguments), arguments) ==
			   COMMAND_LINE_CONTINUE);
		assert(output_length == 0);
#else
		assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(arguments), arguments) ==
			   COMMAND_LINE_SUCCESS);
#endif
	}

	legacy_s8 *followed_by_version[] = {(legacy_s8 *)"restunts", (legacy_s8 *)"--data-dir",
										(legacy_s8 *)"--help", (legacy_s8 *)"-v"};
	assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(followed_by_version),
						 followed_by_version) == COMMAND_LINE_SUCCESS);
#ifdef RESTUNTS_SDL3
	expect_text(COMMAND_LINE_TEST_VERSION);
	assert(strstr(output, "--help") == NULL);
#else
	expect_text("--help");
#endif
}

static void test_large_argument_count(void)
{
	const legacy_s32 count = COMMAND_LINE_TEST_LARGE_ARGUMENT_COUNT;
	legacy_s8 **arguments =
		malloc((COMMAND_LINE_TEST_LARGE_ARGUMENT_COUNT + 1U) * sizeof(*arguments));
	assert(arguments != NULL);
	arguments[0] = (legacy_s8 *)"restunts";
	for (legacy_s32 index = 1; index < count; index++) {
		arguments[index] = (legacy_s8 *)"/ns";
	}
	arguments[count] = NULL;
	assert(run_arguments(count, arguments) == COMMAND_LINE_CONTINUE);
	assert(output_length == 0);

	/* The last option lies beyond both the signed 16-bit count and index limits. */
	arguments[count - 1] = (legacy_s8 *)"--help";
	assert(run_arguments(count, arguments) == COMMAND_LINE_SUCCESS);
	expect_text("--help");
	arguments[count - 1] = (legacy_s8 *)"--version";
	assert(run_arguments(count, arguments) == COMMAND_LINE_SUCCESS);
	expect_text(COMMAND_LINE_TEST_VERSION);
	assert(strstr(output, "--help") == NULL);
	free(arguments);
}

static void test_output_failure(void)
{
	static const legacy_char *flags[] = {"--help", "-h", "--version", "-v"};
	for (legacy_u16 index = 0; index < COMMAND_LINE_TEST_ARGUMENT_COUNT(flags); index++) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"restunts", (legacy_s8 *)flags[index]};
		write_mode = COMMAND_LINE_TEST_WRITE_COMPLETE;
		assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(arguments), arguments) ==
			   COMMAND_LINE_SUCCESS);
		legacy_u16 successful_write_calls = write_calls;
		for (fail_on_call = 1; fail_on_call <= successful_write_calls; fail_on_call++) {
			write_mode = COMMAND_LINE_TEST_WRITE_FAILED;
			assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(arguments), arguments) ==
				   COMMAND_LINE_FAILURE);
			assert(write_calls == fail_on_call);
			write_mode = COMMAND_LINE_TEST_WRITE_SHORT;
			assert(run_arguments(COMMAND_LINE_TEST_ARGUMENT_COUNT(arguments), arguments) ==
				   COMMAND_LINE_FAILURE);
			assert(write_calls == fail_on_call);
		}
	}
	write_mode = COMMAND_LINE_TEST_WRITE_COMPLETE;
}

legacy_int main(void)
{
	test_help_contents();
	test_aliases_and_version();
	test_normal_and_similar_arguments();
	test_position_and_precedence();
	test_data_directory_operands();
	test_large_argument_count();
	test_output_failure();
	return EXIT_SUCCESS;
}
