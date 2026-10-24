#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#endif
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include "../c/legacy.h"

#define TEST_ARGUMENT_CAPACITY 6
#define TEST_ARGUMENT_TEXT_CAPACITY 64
#define TEST_EXIT_HANDLER_CAPACITY 2
#define TEST_GAME_RESULT 7
#define TEST_EXECUTABLE_DIRECTORY "/test/executable/"
#define TEST_ARGUMENT_COUNT(arguments)                                                             \
	((legacy_int)(sizeof(arguments) / sizeof((arguments)[0]) - 1U))

legacy_s32 sdl3_batch_mode;

static bool SDLCALL test_sdl_init(SDL_InitFlags flags);
static const legacy_char *SDLCALL test_sdl_error(void);
#ifdef _WIN32
static BOOL WINAPI test_set_current_directory(const WCHAR *directory);
#else
static legacy_int test_chdir(const legacy_char *directory);
#endif
static legacy_int test_atexit(void (*handler)(void));
static void *test_malloc(size_t size);
static void test_free(void *allocation);

#define RESTUNTS_FULL
#define main test_entry_main
#define SDL_Init test_sdl_init
#define SDL_GetError test_sdl_error
#ifdef _WIN32
#define SetCurrentDirectoryW test_set_current_directory
#else
#define chdir test_chdir
#endif
#define atexit test_atexit
#define malloc test_malloc
#define free test_free
#include "../platform/sdl3/main.c"
#undef free
#undef malloc
#undef atexit
#undef chdir
#ifdef _WIN32
#undef SetCurrentDirectoryW
#endif
#undef SDL_GetError
#undef SDL_Init
#undef main

static legacy_char **original_arguments;
static legacy_char *original_pointers[TEST_ARGUMENT_CAPACITY];
static legacy_char original_text[TEST_ARGUMENT_CAPACITY][TEST_ARGUMENT_TEXT_CAPACITY];
static legacy_int original_count;
static legacy_int expected_count;
static const legacy_char *const *expected_arguments;
static legacy_s16 information_result;
static legacy_int directory_calls;
#ifdef _WIN32
static const WCHAR *expected_windows_directory = L"data-directory";
static legacy_u8 fail_directory;
#endif
static legacy_u16 asset_base_initialized, car_base_calls;
static legacy_int initialization_calls;
static legacy_int game_calls;
static legacy_int fail_allocation;
static legacy_int fail_registration;
static legacy_int fail_initialization;
static legacy_int exit_from_game;
static legacy_u8 real_event_startup;
static void *live_allocation;
static void (*exit_handlers[TEST_EXIT_HANDLER_CAPACITY])(void);
static legacy_int exit_handler_count;
static jmp_buf exit_target;

static void assert_original_arguments(void)
{
	for (legacy_int index = 0; index <= original_count; index++) {
		assert(original_arguments[index] == original_pointers[index]);
		if (index < original_count) {
			assert(strcmp(original_arguments[index], original_text[index]) == 0);
		}
	}
}

static void reset_test(legacy_int argc, legacy_char **argv, legacy_int game_argc,
					   const legacy_char *const *game_argv)
{
	assert(live_allocation == NULL);
	assert(exit_handler_count == 0);
	assert(argc < TEST_ARGUMENT_CAPACITY);
	original_count = argc;
	original_arguments = argv;
	for (legacy_int index = 0; index <= argc; index++) {
		original_pointers[index] = argv[index];
		if (index < argc) {
			assert(strlen(argv[index]) < TEST_ARGUMENT_TEXT_CAPACITY);
			strcpy(original_text[index], argv[index]);
		}
	}
	expected_count = game_argc;
	expected_arguments = game_argv;
	information_result = COMMAND_LINE_CONTINUE;
	directory_calls = 0;
#ifdef _WIN32
	fail_directory = 0;
#endif
	asset_base_initialized = car_base_calls = 0;
	initialization_calls = 0;
	game_calls = 0;
	fail_allocation = 0;
	fail_registration = 0;
	fail_initialization = 0;
	exit_from_game = 0;
}

static void run_exit_handlers(void)
{
	while (exit_handler_count != 0) {
		exit_handlers[--exit_handler_count]();
	}
	assert(live_allocation == NULL);
	assert_original_arguments();
}

static void *test_malloc(size_t size)
{
	assert(live_allocation == NULL);
	live_allocation = fail_allocation ? NULL : malloc(size);
	return live_allocation;
}

static void test_free(void *allocation)
{
	assert(allocation == live_allocation);
	free(allocation);
	live_allocation = NULL;
}

static legacy_int test_atexit(void (*handler)(void))
{
	if (fail_registration) {
		return -1;
	}
	assert(exit_handler_count < TEST_EXIT_HANDLER_CAPACITY);
	exit_handlers[exit_handler_count++] = handler;
	return 0;
}

#ifdef _WIN32
static BOOL WINAPI test_set_current_directory(const WCHAR *directory)
{
	assert(car_base_calls == 1);
	directory_calls++;
	assert(wcscmp(directory, expected_windows_directory) == 0);
	return fail_directory == 0;
}
#else
static legacy_int test_chdir(const legacy_char *directory)
{
	assert(car_base_calls == 1);
	directory_calls++;
	assert(strcmp(directory, "data-directory") == 0);
	return 0;
}
#endif

static bool SDLCALL test_sdl_init(SDL_InitFlags flags)
{
	if (real_event_startup != 0) {
		assert_original_arguments();
		initialization_calls++;
		return SDL_Init(flags);
	}
	assert(flags == (sdl3_batch_mode ? 0 : SDL_INIT_EVENTS));
	assert_original_arguments();
	initialization_calls++;
	return fail_initialization == 0;
}

static const legacy_char *SDLCALL test_sdl_error(void)
{
	return "expected test failure";
}

legacy_s16 command_line_print_info(legacy_s32 argc, legacy_s8 *argv[])
{
	(void)argc;
	assert((legacy_char **)argv == original_arguments);
	assert_original_arguments();
	return information_result;
}

void asset_path_initialize(const legacy_char *executable)
{
	assert(executable == (original_count > 0 ? original_arguments[0] : NULL));
	assert(directory_calls == 0);
	asset_base_initialized = 1;
}

const legacy_char *asset_path_base(void)
{
	assert(asset_base_initialized != 0 && directory_calls == 0);
	return TEST_EXECUTABLE_DIRECTORY;
}

void file_car_set_base(const legacy_s8 *directory)
{
	assert(asset_base_initialized != 0 && directory_calls == 0);
	assert(strcmp((const legacy_char *)directory, TEST_EXECUTABLE_DIRECTORY) == 0);
	car_base_calls++;
}

void full_data_initialize(void)
{
	assert(initialization_calls == 1);
}

void sdl3_platform_shutdown(void)
{
	/* The argument storage survives shutdown, including a direct exit(). */
	assert(live_allocation != NULL);
	assert_original_arguments();
	if (real_event_startup != 0) {
		SDL_QuitSubSystem(SDL_INIT_EVENTS);
	}
}

legacy_s16 run_main_menu_loop(legacy_s16 argc, legacy_s8 *argv[])
{
	if (real_event_startup != 0) {
		/* Android prepares JNI on its UI thread but runs the game on SDLThread. */
		assert(SDL_IsMainThread());
		assert(SDL_WasInit(SDL_INIT_EVENTS) != 0);
		assert(SDL_WasInit(SDL_INIT_VIDEO) == 0);
		SDL_Event event;
		(void)SDL_PollEvent(&event);
	}
	game_calls++;
	assert(argc == expected_count);
	assert((legacy_char **)argv != original_arguments);
	assert(argv[argc] == NULL);
	for (legacy_int index = 0; index < argc; index++) {
		assert(strcmp((const legacy_char *)argv[index], expected_arguments[index]) == 0);
		for (legacy_int source = 0; source < original_count; source++) {
			assert((legacy_char *)argv[index] != original_arguments[source]);
		}
		argv[index][0] = '!';
	}
	/* Exercise both dump-style text edits and benchmark-style vector compaction. */
	if (argc > SDL3_FIRST_OPTION_INDEX) {
		argv[0] = argv[SDL3_FIRST_OPTION_INDEX];
		argv[SDL3_FIRST_OPTION_INDEX] = NULL;
	}
	assert_original_arguments();
	if (exit_from_game) {
		run_exit_handlers();
		longjmp(exit_target, 1);
	}
	return TEST_GAME_RESULT;
}

static void test_dispatch(legacy_int argc, legacy_char **argv, legacy_int game_argc,
						  const legacy_char *const *game_argv, legacy_int direct_exit)
{
	reset_test(argc, argv, game_argc, game_argv);
	exit_from_game = direct_exit;
	if (setjmp(exit_target) == 0) {
		assert(test_entry_main(argc, argv) == TEST_GAME_RESULT);
		assert(!direct_exit);
		run_exit_handlers();
	} else {
		assert(direct_exit);
	}
	assert(game_calls == 1);
	assert(car_base_calls == 1);
	assert(initialization_calls == 1);
}

static legacy_int SDLCALL test_game_thread(void *unused)
{
	(void)unused;
	legacy_char program[] = "restunts";
	legacy_char *arguments[] = {program, NULL};
	const legacy_char *expected[] = {"restunts"};
	test_dispatch(TEST_ARGUMENT_COUNT(arguments), arguments, TEST_ARGUMENT_COUNT(arguments),
				  expected, 0);
	return COMMAND_LINE_SUCCESS;
}

static void test_event_thread_startup(void)
{
	/* Match SDL's Android JNI setup before the dedicated game thread starts. */
	SDL_SetMainReady();
	assert(SDL_IsMainThread());
	assert(SDL_WasInit(SDL_INIT_EVENTS) == 0);
	real_event_startup = 1;
	SDL_Thread *thread = SDL_CreateThread(test_game_thread, "game-startup", NULL);
	assert(thread != NULL);
	legacy_int result;
	SDL_WaitThread(thread, &result);
	real_event_startup = 0;
	assert(result == COMMAND_LINE_SUCCESS);
	assert(SDL_WasInit(SDL_INIT_EVENTS) == 0);
	/* Join before full shutdown invalidates SDL thread handles. Event cleanup
	 * happens on the game thread; the bootstrap thread owns the remaining SDL state. */
	assert(SDL_IsMainThread());
	SDL_Quit();
	puts("SDL3 event polling before video on a dedicated game thread passed");
}

#ifdef _WIN32
static void test_windows_utf8_directory(void)
{
	legacy_char program[] = "restunts";
	legacy_char data_option[] = "--data-dir";
	legacy_char directory[] = "Game Data \xC3\xA1rv\xC3\xADz & test";
	legacy_char *arguments[] = {program, data_option, directory, NULL};
	const legacy_char *expected[] = {"restunts", NULL};
	expected_windows_directory = L"Game Data \x00E1rv\x00EDz & test";
	test_dispatch(TEST_ARGUMENT_COUNT(arguments), arguments, TEST_ARGUMENT_COUNT(expected),
				  expected, 0);
	assert(directory_calls == 1);
	/* A failed directory change must release its conversion buffer. */
	reset_test(TEST_ARGUMENT_COUNT(arguments), arguments, 0, NULL);
	fail_directory = 1;
	assert(test_entry_main(original_count, arguments) == COMMAND_LINE_FAILURE);
	assert(directory_calls == 1 && initialization_calls == 0 && live_allocation == NULL);
	reset_test(TEST_ARGUMENT_COUNT(arguments), arguments, 0, NULL);
	fail_allocation = 1;
	assert(test_entry_main(original_count, arguments) == COMMAND_LINE_FAILURE);
	assert(directory_calls == 0 && initialization_calls == 0 && live_allocation == NULL);
	/* Invalid UTF-8 is rejected before invoking the Windows directory API. */
	legacy_char invalid[] = "\xC3";
	legacy_char *malformed[] = {program, data_option, invalid, NULL};
	reset_test(TEST_ARGUMENT_COUNT(malformed), malformed, 0, NULL);
	assert(test_entry_main(original_count, malformed) == COMMAND_LINE_FAILURE);
	assert(directory_calls == 0 && initialization_calls == 0 && live_allocation == NULL);
	expected_windows_directory = L"data-directory";
}
#endif

legacy_int main(void)
{
	legacy_char program[] = "restunts";
	legacy_char data_option[] = "--data-dir";
	legacy_char directory[] = "data-directory";
	legacy_char option[] = "--nointro";
	legacy_char replay[] = "replay.rpl";
	legacy_char *filtered[] = {program, data_option, directory, option, replay, NULL};
	legacy_char *plain[] = {program, option, replay, NULL};
	legacy_char *directory_only[] = {program, data_option, directory, NULL};
	legacy_char *missing_directory[] = {program, data_option, NULL};
	legacy_char *empty[] = {NULL};
	const legacy_char *expected[] = {"restunts", "--nointro", "replay.rpl"};
	test_dispatch(TEST_ARGUMENT_COUNT(filtered), filtered, TEST_ARGUMENT_COUNT(plain), expected, 0);
	assert(directory_calls == 1);
	test_dispatch(TEST_ARGUMENT_COUNT(plain), plain, TEST_ARGUMENT_COUNT(plain), expected, 1);
	assert(directory_calls == 0);
	test_dispatch(TEST_ARGUMENT_COUNT(directory_only), directory_only, SDL3_FIRST_OPTION_INDEX,
				  expected, 0);
	test_dispatch(TEST_ARGUMENT_COUNT(empty), empty, 0, NULL, 0);

	reset_test(TEST_ARGUMENT_COUNT(missing_directory), missing_directory, 0, NULL);
	information_result = COMMAND_LINE_SUCCESS;
	assert(test_entry_main(original_count, original_arguments) == COMMAND_LINE_SUCCESS);
	assert(directory_calls == 0 && initialization_calls == 0 && exit_handler_count == 0);
	assert(car_base_calls == 0);
	information_result = COMMAND_LINE_CONTINUE;
	assert(test_entry_main(original_count, original_arguments) == COMMAND_LINE_FAILURE);
	assert(directory_calls == 0 && initialization_calls == 0 && exit_handler_count == 0);
	assert_original_arguments();

	reset_test(TEST_ARGUMENT_COUNT(plain), plain, 0, NULL);
	assert(test_entry_main((legacy_int)LEGACY_S16_MAX + 1, plain) == COMMAND_LINE_FAILURE);
	assert(initialization_calls == 0 && live_allocation == NULL);
	fail_allocation = 1;
	assert(test_entry_main(original_count, plain) == COMMAND_LINE_FAILURE);
	assert(initialization_calls == 0 && live_allocation == NULL);
	fail_allocation = 0;
	fail_registration = 1;
	assert(test_entry_main(original_count, plain) == COMMAND_LINE_FAILURE);
	assert(initialization_calls == 0 && live_allocation == NULL);
	fail_registration = 0;
	fail_initialization = 1;
	assert(test_entry_main(original_count, plain) == COMMAND_LINE_FAILURE);
	assert(game_calls == 0 && live_allocation == NULL);
	run_exit_handlers();
	test_event_thread_startup();
	reset_test(TEST_ARGUMENT_COUNT(plain), plain, TEST_ARGUMENT_COUNT(plain), expected);
	sdl3_batch_mode = 1;
	assert(test_entry_main(original_count, plain) == TEST_GAME_RESULT);
	run_exit_handlers();
	sdl3_batch_mode = 0;
#ifdef _WIN32
	test_windows_utf8_directory();
#endif
	puts("SDL3 entry-point argument tests passed");
	return 0;
}
