#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "../c/asset_path.h"
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
#include <unistd.h>
#endif

#define TEST_PATH_CAPACITY 1024U
#define TEST_NUL_TERMINATED_LENGTH (-1)

static const legacy_char payload[] = "Binary asset\r\n\x1a\0complete stream";

static const legacy_char *test_sdl_base_path(void)
{
	return NULL;
}

/* Exercise the production stream helper without initializing a display. */
#define SDL_GetBasePath test_sdl_base_path
#include "../platform/sdl3/asset_path.c"
#undef SDL_GetBasePath

legacy_int main(void)
{
	legacy_char path[TEST_PATH_CAPACITY];
#ifdef _WIN32
	WCHAR temporary[MAX_PATH];
	WCHAR directory[MAX_PATH];
	WCHAR filename[MAX_PATH];
	static const WCHAR suffix[] = L"\\asset \u0151\u0171\u6771\u4eac.bin";
	DWORD length = GetTempPathW(MAX_PATH, temporary);
	assert(length > 0 && length < MAX_PATH);
	assert(GetTempFileNameW(temporary, L"ras", 0, directory) != 0);
	assert(DeleteFileW(directory));
	assert(CreateDirectoryW(directory, NULL));
	assert(wcslen(directory) + wcslen(suffix) < MAX_PATH);
	wcscpy(filename, directory);
	wcscat(filename, suffix);
	HANDLE native =
		CreateFileW(filename, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	assert(native != INVALID_HANDLE_VALUE);
	DWORD written;
	assert(WriteFile(native, payload, sizeof(payload), &written, NULL));
	assert(written == sizeof(payload));
	assert(CloseHandle(native));
	assert(WideCharToMultiByte(CP_UTF8, 0, filename, TEST_NUL_TERMINATED_LENGTH, path, sizeof(path),
							   NULL, NULL) > 0);
#else
	legacy_char directory[] = "/tmp/restunts-asset-stream-XXXXXX";
	assert(mkdtemp(directory) != NULL);
	legacy_int length = snprintf(
		path, sizeof(path), "%s/asset \xc5\x91\xc5\xb1\xe6\x9d\xb1\xe4\xba\xac.bin", directory);
	assert(length > 0 && (size_t)length < sizeof(path));
	FILE *native = fopen(path, "wb");
	assert(native != NULL);
	assert(fwrite(payload, 1, sizeof(payload), native) == sizeof(payload));
	assert(fclose(native) == 0);
#endif
	FILE *file = asset_path_open_file(path, "rb");
	assert(file != NULL);
	legacy_char actual[sizeof(payload)];
	assert(fread(actual, 1, sizeof(actual), file) == sizeof(actual));
	assert(memcmp(actual, payload, sizeof(payload)) == 0);
	assert(fgetc(file) == EOF);
	assert(fclose(file) == 0);
	/* A second open verifies ownership stays with the caller after reading. */
	file = asset_path_open_file(path, "rb");
	assert(file != NULL);
	assert(fclose(file) == 0);
	assert(asset_path_open_file(NULL, "rb") == NULL);
	assert(asset_path_open_file(path, NULL) == NULL);
#ifdef _WIN32
	assert(asset_path_open_file("\xc3(", "rb") == NULL);
	assert(asset_path_open_file(path, "\xc3(") == NULL);
	assert(DeleteFileW(filename));
	assert(RemoveDirectoryW(directory));
#else
	assert(remove(path) == 0);
	assert(rmdir(directory) == 0);
#endif
	puts("UTF-8 asset stream tests passed.");
	return 0;
}
