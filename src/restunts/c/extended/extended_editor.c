#include "extended_editor.h"
#include <SDL3/SDL.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// clang-format off
EM_JS(legacy_int, extended_editor_open_browser, (void), {
	const editor = window.open(new URL('tracked.html', window.location.href).href, '_blank');
	if (editor === null) {
		return 0;
	}
	editor.opener = null;
	return 1;
});
// clang-format on

/* Keep a C definition so static archives retain the EM_JS import metadata. */
legacy_int extended_editor_open(void)
{
	return extended_editor_open_browser();
}
#else

enum { EDITOR_PATH_CAPACITY = 2048, EDITOR_URI_CAPACITY = 8192 };

static legacy_int extended_editor_open_path(const legacy_char *path)
{
	SDL_PathInfo info;
	if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
		return 0;
	}
	legacy_char uri[EDITOR_URI_CAPACITY] = "file://";
	size_t used = strlen(uri);
#ifdef _WIN32
	uri[used++] = '/';
#endif
	const legacy_char digits[] = "0123456789ABCDEF";
	for (const legacy_u8 *cursor = (const legacy_u8 *)path; *cursor; cursor++) {
		legacy_u8 value = *cursor == '\\' ? '/' : *cursor;
		if (used + 4U >= sizeof(uri)) {
			return 0;
		}
		if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
			(value >= '0' && value <= '9') || strchr("/-_.~:", value)) {
			uri[used++] = (legacy_char)value;
		} else {
			uri[used++] = '%';
			uri[used++] = digits[value >> 4];
			uri[used++] = digits[value & 15];
		}
	}
	uri[used] = 0;
	return SDL_OpenURL(uri);
}

legacy_int extended_editor_open(void)
{
	legacy_char path[EDITOR_PATH_CAPACITY];
	const legacy_char *base = SDL_GetBasePath();
#if defined(__linux__) && !defined(__ANDROID__)
	if (base) {
		legacy_int length = SDL_snprintf(path, sizeof(path), "%srestunts-launcher", base);
		SDL_PathInfo info;
		if (length >= 0 && length < (legacy_int)sizeof(path) && SDL_GetPathInfo(path, &info) &&
			info.type == SDL_PATHTYPE_FILE) {
			/* Reuse the launcher's persistent export, host environment and
			 * browser sandbox support instead of exposing an AppImage mount. */
			const legacy_char *arguments[] = {path, "--track-editor", NULL};
			SDL_Process *process = SDL_CreateProcess(arguments, false);
			if (!process) {
				return 0;
			}
			legacy_int status = 0;
			legacy_int waited = SDL_WaitProcess(process, true, &status);
			SDL_DestroyProcess(process);
			return waited && status == 0;
		}
	}
#endif
	if (base &&
		SDL_snprintf(path, sizeof(path), "%s../share/restunts/editor/tracked.html", base) <
			(legacy_int)sizeof(path) &&
		extended_editor_open_path(path)) {
		return 1;
	}
#ifdef RESTUNTS_EDITOR_DIRECTORY
	if (SDL_snprintf(path, sizeof(path), "%s/tracked.html", RESTUNTS_EDITOR_DIRECTORY) <
			(legacy_int)sizeof(path) &&
		extended_editor_open_path(path)) {
		return 1;
	}
#endif
	return 0;
}
#endif
