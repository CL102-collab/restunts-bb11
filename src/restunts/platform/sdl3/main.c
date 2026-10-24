#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "../../c/restunts.h"
#include "../../c/command_line.h"
#include "../../c/asset_path.h"
#include "../../c/fileio.h"
#include "sdl3.h"

extern void full_data_initialize(void);
extern legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[]);

#define SDL3_FIRST_OPTION_INDEX 1
#define SDL3_DATA_DIRECTORY_INDEX 2
#define SDL3_DATA_DIRECTORY_ARGUMENT_COUNT 2
#define SDL3_DIRECTORY_SUCCESS 0
#define SDL3_DIRECTORY_FAILURE (-1)
#ifdef _WIN32
#define SDL3_NUL_TERMINATED_LENGTH (-1)
#endif

static legacy_char **game_arguments;

static void free_game_arguments(void)
{
	free(game_arguments);
	game_arguments = NULL;
}

static legacy_int enter_data_directory(const legacy_char *directory)
{
#ifdef _WIN32
	/* SDL supplies UTF-8 arguments. The XP ANSI CRT cannot open every folder
	 * chosen by the Unicode launcher; change only the process directory here. */
	legacy_int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, directory,
										   SDL3_NUL_TERMINATED_LENGTH, NULL, 0);
	if (count <= 0 || (size_t)count > SIZE_MAX / sizeof(WCHAR)) {
		return SDL3_DIRECTORY_FAILURE;
	}
	WCHAR *wide_directory = malloc((size_t)count * sizeof(*wide_directory));
	if (wide_directory == NULL) {
		return SDL3_DIRECTORY_FAILURE;
	}
	legacy_int result = SDL3_DIRECTORY_FAILURE;
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, directory, SDL3_NUL_TERMINATED_LENGTH,
							wide_directory, count) == count &&
		SetCurrentDirectoryW(wide_directory)) {
		result = SDL3_DIRECTORY_SUCCESS;
	}
	free(wide_directory);
	return result;
#else
	return chdir(directory);
#endif
}

/* Match the argument and result types required by the C runtime. */
legacy_int main(legacy_int argc, legacy_char **argv)
{
#ifdef RESTUNTS_FULL
	legacy_s16 info_result = command_line_print_info((legacy_s32)argc, (legacy_s8 **)argv);
	if (info_result != COMMAND_LINE_CONTINUE) {
		if (fflush(stdout) != 0) {
			return COMMAND_LINE_FAILURE;
		}
		return info_result;
	}
#endif
#if !defined(__DJGPP__) && !defined(RESTUNTS_HEADLESS)
	if (argc == 2 && strcmp(argv[1], "--licenses") == 0) {
		fputs("Nuked OPL2 Lite - Copyright (C) 2026 Nuke.YKT.\n"
			  "GNU LGPL version 2.1 or, at your option, any later version.\n"
			  "Provided WITHOUT ANY WARRANTY. See the complete license at\n"
			  "share/licenses/restunts/Nuked-OPL2-LICENSE in the package,\n"
			  "or third_party/nuked-opl2-lite/LICENSE in the source checkout.\n"
			  "Library source and rebuild instructions: share/restunts/nuked-opl2-lite/\n"
			  "You may modify this application for your own use and reverse engineer it\n"
			  "to debug modifications to this LGPL-covered library.\n"
			  "See THIRD-PARTY-NOTICES.txt for dependency notices.\n",
			  stdout);
		return 0;
	}
#endif
#if defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_PIXLDUMP)
	sdl3_batch_mode = 1;
#endif
#ifndef RESTUNTS_HEADLESS
	asset_path_initialize(argc > 0 ? argv[0] : NULL);
	file_car_set_base((const legacy_s8 *)asset_path_base());
#else
	const legacy_char *asset_directory = getenv("RESTUNTS_ASSETS_DIR");
	file_car_set_base(
		(const legacy_s8 *)(asset_directory != NULL ? asset_directory : SDL_GetBasePath()));
#endif
	/* Accept a resource directory without changing the historical game and
 * dump argument syntax. By default resources are read from the current dir. */
	legacy_int argument_offset = 0;
	if (argc > SDL3_FIRST_OPTION_INDEX &&
		strcmp(argv[SDL3_FIRST_OPTION_INDEX], "--data-dir") == 0) {
		if (argc <= SDL3_DATA_DIRECTORY_INDEX ||
			enter_data_directory(argv[SDL3_DATA_DIRECTORY_INDEX]) != SDL3_DIRECTORY_SUCCESS) {
			fputs("Cannot open --data-dir directory\n", stderr);
			return 1;
		}
		argument_offset = SDL3_DATA_DIRECTORY_ARGUMENT_COUNT;
		argc -= argument_offset;
	}
	if (argc > (legacy_int)LEGACY_S16_MAX) {
		fputs("Too many arguments\n", stderr);
		return 1;
	}
	/* The platform may retain the process arguments. Own both the vector and
	 * strings, since legacy consumers can reorder arguments or edit their text. */
	size_t argument_bytes = ((size_t)argc + 1U) * sizeof(*game_arguments);
	for (legacy_int index = 0; index < argc; index++) {
		const legacy_char *argument = argv[index == 0 ? 0 : index + argument_offset];
		size_t length = strlen(argument);
		if (length >= SIZE_MAX - argument_bytes) {
			fputs("Command-line arguments are too large\n", stderr);
			return 1;
		}
		argument_bytes += length + 1U;
	}
	game_arguments = malloc(argument_bytes);
	if (game_arguments == NULL) {
		fputs("Cannot allocate command-line arguments\n", stderr);
		return 1;
	}
	legacy_char *argument_text = (legacy_char *)(game_arguments + argc + 1);
	for (legacy_int index = 0; index < argc; index++) {
		const legacy_char *argument = argv[index == 0 ? 0 : index + argument_offset];
		size_t length = strlen(argument) + 1U;
		game_arguments[index] = argument_text;
		memcpy(argument_text, argument, length);
		argument_text += length;
	}
	game_arguments[argc] = NULL;
	/* Register before SDL shutdown so arguments also survive exit() paths. */
	if (atexit(free_game_arguments) != 0) {
		free_game_arguments();
		fputs("Cannot register command-line cleanup\n", stderr);
		return 1;
	}
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
#ifdef __ANDROID__
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
	/* Keep a TV remote available for menus instead of opening it as the gamepad. */
	SDL_SetHint(SDL_HINT_TV_REMOTE_AS_JOYSTICK, "0");
#endif
	/* Keyboard setup polls before video is initialized. Establish event ownership
	 * on the game thread first; batch tools do not need interactive events. */
	if (!SDL_Init(sdl3_batch_mode ? 0 : SDL_INIT_EVENTS)) {
		free_game_arguments();
		fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
		return 1;
	}
#ifdef RESTUNTS_HEADLESS
	atexit(SDL_Quit);
#else
	atexit(sdl3_platform_shutdown);
	full_data_initialize();
#endif
#if defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_PIXLDUMP)
	return stuntsmain((legacy_s16)argc, (legacy_s8 **)game_arguments);
#else
	return run_main_menu_loop((legacy_s16)argc, (legacy_s8 **)game_arguments);
#endif
}
