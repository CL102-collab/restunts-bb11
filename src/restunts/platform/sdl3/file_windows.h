#ifndef RESTUNTS_SDL3_FILE_WINDOWS_H
#define RESTUNTS_SDL3_FILE_WINDOWS_H

#include <windows.h>

/* Private adapter for file.c. MinGW's narrow dirent normalizes the process
 * directory through the ANSI code page, even when opening ".". Keep every
 * filesystem boundary wide and expose UTF-8 names to the shared DOS resolver. */
#define FILE_WINDOWS_NUL_LENGTH (-1)

typedef struct {
	_WDIR *handle;
	legacy_char name[FILE_PATH_SIZE];
} NativeDirectory;

static legacy_u16 native_wide_path(const legacy_char *path, WCHAR wide[FILE_PATH_SIZE])
{
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, FILE_WINDOWS_NUL_LENGTH, wide,
							   FILE_PATH_SIZE) != 0;
}

static NativeDirectory *native_directory_open(const legacy_char *path)
{
	WCHAR wide[FILE_PATH_SIZE];
	if (!native_wide_path(path, wide)) {
		return NULL;
	}
	_WDIR *handle = _wopendir(wide);
	if (handle == NULL) {
		return NULL;
	}
	NativeDirectory *directory = malloc(sizeof(*directory));
	if (directory == NULL) {
		_wclosedir(handle);
		return NULL;
	}
	directory->handle = handle;
	return directory;
}

static const legacy_char *native_directory_read(NativeDirectory *directory)
{
	struct _wdirent *entry;
	while ((entry = _wreaddir(directory->handle)) != NULL) {
		if (WideCharToMultiByte(CP_UTF8, 0, entry->d_name, FILE_WINDOWS_NUL_LENGTH, directory->name,
								sizeof(directory->name), NULL, NULL) != 0) {
			return directory->name;
		}
	}
	return NULL;
}

static void native_directory_close(NativeDirectory *directory)
{
	_wclosedir(directory->handle);
	free(directory);
}

static FILE *native_file_open(const legacy_char *path, legacy_s16 create)
{
	WCHAR wide[FILE_PATH_SIZE];
	return native_wide_path(path, wide)
			   ? _wfopen(wide, create == DOS_FILE_OPEN_EXISTING ? L"rb" : L"wb+")
			   : NULL;
}

static legacy_int native_file_remove(const legacy_char *path)
{
	WCHAR wide[FILE_PATH_SIZE];
	return native_wide_path(path, wide) ? _wremove(wide) : -1;
}

static legacy_u16 native_regular_file(const legacy_char *path)
{
	WCHAR wide[FILE_PATH_SIZE];
	if (!native_wide_path(path, wide)) {
		return 0;
	}
	DWORD attributes = GetFileAttributesW(wide);
	return attributes != INVALID_FILE_ATTRIBUTES &&
		   (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) == 0;
}

#endif
