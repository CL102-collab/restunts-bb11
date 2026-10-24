#include "../../c/asset_path.h"
#include <SDL3/SDL.h>
#ifdef _WIN32
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>

#define ASSET_NUL_TERMINATED_LENGTH (-1)

static WCHAR *asset_path_to_wide(const legacy_char *text)
{
	legacy_int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text,
										   ASSET_NUL_TERMINATED_LENGTH, NULL, 0);
	if (count <= 0 || (size_t)count > SIZE_MAX / sizeof(WCHAR)) {
		return NULL;
	}
	WCHAR *wide = malloc((size_t)count * sizeof(*wide));
	if (wide == NULL) {
		return NULL;
	}
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, ASSET_NUL_TERMINATED_LENGTH, wide,
							count) != count) {
		free(wide);
		return NULL;
	}
	return wide;
}
#endif

FILE *asset_path_open_file(const legacy_char *path, const legacy_char *mode)
{
	if (path == NULL || mode == NULL) {
		return NULL;
	}
#ifdef _WIN32
	WCHAR *wide_path = asset_path_to_wide(path);
	if (wide_path == NULL) {
		return NULL;
	}
	WCHAR *wide_mode = asset_path_to_wide(mode);
	FILE *file = wide_mode != NULL ? _wfopen(wide_path, wide_mode) : NULL;
	free(wide_mode);
	free(wide_path);
	return file;
#else
	return fopen(path, mode);
#endif
}

#if !defined(_WIN32) && !defined(__DJGPP__) && !defined(__EMSCRIPTEN__)
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static legacy_char *executable_directory;
static legacy_s32 cleanup_registered;

static void asset_path_cleanup(void)
{
	free(executable_directory);
	executable_directory = NULL;
}

#ifndef __ANDROID__
static legacy_char *asset_path_resolve(const legacy_char *executable)
{
	struct stat info;
	if (stat(executable, &info) != 0 || !S_ISREG(info.st_mode) || access(executable, X_OK) != 0) {
		return NULL;
	}
	return realpath(executable, NULL);
}

static legacy_char *asset_path_search(const legacy_char *executable)
{
	if (executable == NULL || executable[0] == '\0') {
		return NULL;
	}
	if (strchr(executable, '/') != NULL) {
		return asset_path_resolve(executable);
	}
	const legacy_char *environment_path = getenv("PATH");
	if (environment_path == NULL) {
		return NULL;
	}
	legacy_char *search_path = strdup(environment_path);
	if (search_path == NULL) {
		return NULL;
	}
	legacy_char *directory = search_path;
	legacy_char *resolved = NULL;
	while (directory != NULL) {
		legacy_char *next = strchr(directory, ':');
		if (next != NULL) {
			*next++ = '\0';
		}
		legacy_char candidate[PATH_MAX];
		legacy_s32 length = snprintf(candidate, sizeof(candidate), "%s/%s",
									 directory[0] != '\0' ? directory : ".", executable);
		if (length >= 0 && length < (legacy_s32)sizeof(candidate)) {
			resolved = asset_path_resolve(candidate);
			if (resolved != NULL) {
				break;
			}
		}
		directory = next;
	}
	free(search_path);
	return resolved;
}
#endif
#endif

void asset_path_initialize(const legacy_char *executable)
{
#if !defined(_WIN32) && !defined(__DJGPP__) && !defined(__EMSCRIPTEN__)
	if (executable_directory != NULL) {
		return;
	}
#ifdef __ANDROID__
	(void)executable;
	const legacy_char *base = SDL_GetAndroidInternalStoragePath();
	if (base != NULL) {
		executable_directory = malloc(strlen(base) + sizeof("/"));
		if (executable_directory != NULL) {
			sprintf(executable_directory, "%s/", base);
		}
	}
#else
	const legacy_char *base = SDL_GetBasePath();
	if (base != NULL) {
		executable_directory = strdup(base);
	} else {
		executable_directory = asset_path_search(executable);
		if (executable_directory != NULL) {
			/* realpath resolves symlinks and produces an absolute executable path. */
			legacy_char *separator = strrchr(executable_directory, '/');
			separator[1] = '\0';
		}
	}
#endif
	if (executable_directory != NULL && !cleanup_registered) {
		if (atexit(asset_path_cleanup) != 0) {
			asset_path_cleanup();
			return;
		}
		cleanup_registered = 1;
	}
#else
	(void)executable;
#endif
}

const legacy_char *asset_path_base(void)
{
#if !defined(_WIN32) && !defined(__DJGPP__) && !defined(__EMSCRIPTEN__)
	if (executable_directory != NULL) {
		return executable_directory;
	}
#endif
	return SDL_GetBasePath();
}
