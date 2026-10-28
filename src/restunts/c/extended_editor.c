#include "extended_editor.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// clang-format off
EM_JS(int, extended_editor_open, (void), {
	const editor = window.open(new URL('track-editor.html', window.location.href).href, '_blank');
	if (editor === null) {
		return 0;
	}
	editor.opener = null;
	return 1;
});
// clang-format on
#else

static int extended_editor_open_path(const char *path)
{
	SDL_PathInfo info;
	if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
		return 0;
	}
	char uri[8192] = "file://";
	size_t used = strlen(uri);
#ifdef _WIN32
	uri[used++] = '/';
#endif
	const char digits[] = "0123456789ABCDEF";
	for (const unsigned char *cursor = (const unsigned char *)path; *cursor; cursor++) {
		unsigned char value = *cursor == '\\' ? '/' : *cursor;
		if (used + 4U >= sizeof(uri)) {
			return 0;
		}
		if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
			(value >= '0' && value <= '9') || strchr("/-_.~:", value)) {
			uri[used++] = (char)value;
		} else {
			uri[used++] = '%';
			uri[used++] = digits[value >> 4];
			uri[used++] = digits[value & 15];
		}
	}
	uri[used] = 0;
	return SDL_OpenURL(uri);
}

int extended_editor_open(void)
{
	char path[2048];
	const char *base = SDL_GetBasePath();
	if (base &&
		SDL_snprintf(path, sizeof(path), "%s../share/restunts/editor/track-editor.html", base) <
			(int)sizeof(path) &&
		extended_editor_open_path(path)) {
		return 1;
	}
#ifdef RESTUNTS_EDITOR_DIRECTORY
	if (SDL_snprintf(path, sizeof(path), "%s/track-editor.html", RESTUNTS_EDITOR_DIRECTORY) <
			(int)sizeof(path) &&
		extended_editor_open_path(path)) {
		return 1;
	}
#endif
	return 0;
}
#endif
