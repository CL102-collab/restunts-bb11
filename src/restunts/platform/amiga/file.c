#include "amiga.h"
#include "../../c/platform.h"
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <string.h>
#ifdef RESTUNTS_AMIGA_TRACE
#include <stdio.h>
#endif

#define FILE_HANDLE_COUNT 20U
#define FILE_PATH_BYTES 256U
#define FILE_NAME_BYTES 13U
static BPTR handles[FILE_HANDLE_COUNT];
static legacy_s16 last_error;
static BPTR directory_lock;
/* Examine/ExNext convert this address to a BCPL pointer (address >> 2).
 * Natural 68000 struct alignment is only two bytes, including under LTO. */
static struct FileInfoBlock file_info __attribute__((aligned(4)));
static legacy_char pattern[FILE_PATH_BYTES];
static legacy_char found_name[FILE_NAME_BYTES];

static legacy_u8 convert_path(const legacy_s8 *source, legacy_char *destination)
{
	legacy_u16 i;
	for (i = 0; i < FILE_PATH_BYTES; ++i) {
		legacy_char value = (legacy_char)source[i];
		destination[i] = value == '\\' ? '/' : value;
		if (value == 0) return 1;
	}
	last_error = 1;
	return 0;
}

static BPTR file_handle(legacy_u16 handle)
{
	if (handle < 5U || handle >= FILE_HANDLE_COUNT || handles[handle] == 0) {
		last_error = 1; return 0;
	}
	return handles[handle];
}

legacy_u16 dos_file_open(const legacy_s8 *path, legacy_s16 create)
{
	legacy_char name[FILE_PATH_BYTES];
	legacy_u16 i;
	last_error = 0;
	if (!convert_path(path, name)) return 0;
	for (i = 5; i < FILE_HANDLE_COUNT; ++i) if (handles[i] == 0) {
		handles[i] = Open(name, create == DOS_FILE_OPEN_EXISTING ? MODE_OLDFILE : MODE_NEWFILE);
		if (handles[i] != 0) return i;
		break;
	}
#ifdef RESTUNTS_AMIGA_TRACE
	printf("Amiga file: cannot open %s (DOS error %ld)\n", name, (long)IoErr());
	fflush(stdout);
#endif
	last_error = 1; return 0;
}
legacy_s16 dos_file_close(legacy_u16 handle)
{
	BPTR file = file_handle(handle);
	if (file == 0) return -1;
	handles[handle] = 0;
	if (!Close(file)) { last_error = 1; return -1; }
	return 0;
}
legacy_u16 dos_file_read(legacy_u16 handle, void *destination, legacy_u16 length)
{
	BPTR file = file_handle(handle);
	LONG count;
	if (file == 0) return 0;
	count = Read(file, destination, length);
	if (count < 0) { last_error = 1; return 0; }
	return (legacy_u16)count;
}
legacy_u16 dos_file_write(legacy_u16 handle, const void *source, legacy_u16 length)
{
	BPTR file = file_handle(handle);
	LONG count;
	if (file == 0) return 0;
	count = Write(file, (APTR)source, length);
	if (count != length) last_error = 1;
	return count < 0 ? 0 : (legacy_u16)count;
}
legacy_s16 dos_file_seek(legacy_u16 handle, legacy_s32 offset, legacy_s16 origin)
{
	BPTR file = file_handle(handle);
	static const LONG modes[3] = { OFFSET_BEGINNING, OFFSET_CURRENT, OFFSET_END };
	if (file == 0 || origin < 0 || origin > 2 || Seek(file, offset, modes[origin]) < 0) {
		last_error = 1; return -1;
	}
	return 0;
}
legacy_s32 dos_file_tell(legacy_u16 handle)
{
	BPTR file = file_handle(handle);
	LONG offset;
	if (file == 0) return -1;
	offset = Seek(file, 0, OFFSET_CURRENT);
	if (offset < 0) last_error = 1;
	return offset;
}
legacy_s16 dos_file_error(void) { legacy_s16 value = last_error; last_error = 0; return value; }
legacy_s16 dos_file_remove(const legacy_s8 *path)
{
	legacy_char name[FILE_PATH_BYTES];
	if (!convert_path(path, name) || !DeleteFile(name)) { last_error = 1; return -1; }
	return 0;
}

static legacy_char lower(legacy_char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
static legacy_u8 matches(const legacy_char *p, const legacy_char *name)
{
	const legacy_char *star = NULL, *retry = NULL;
	if (strcmp(p, "*.*") == 0) p = "*";
	while (*name) {
		if (*p == '?' || (*p != 0 && lower(*p) == lower(*name))) { ++p; ++name; }
		else if (*p == '*') { star = ++p; retry = name; }
		else if (star != NULL) { p = star; name = ++retry; }
		else return 0;
	}
	while (*p == '*') ++p;
	return *p == 0;
}
const legacy_s8 *dos_file_find_next(void)
{
	if (directory_lock == 0) return NULL;
	while (ExNext(directory_lock, &file_info)) {
		const legacy_char *name = (const legacy_char *)file_info.fib_FileName;
		if (file_info.fib_DirEntryType < 0 && strlen(name) < FILE_NAME_BYTES &&
			matches(pattern, name)) {
			strcpy(found_name, name);
			return (const legacy_s8 *)found_name;
		}
	}
	UnLock(directory_lock); directory_lock = 0;
	return NULL;
}
const legacy_s8 *dos_file_find_first(const legacy_s8 *query)
{
	legacy_char path[FILE_PATH_BYTES];
	legacy_char *separator, *volume;
	const legacy_s8 *result;
	if (directory_lock != 0) { UnLock(directory_lock); directory_lock = 0; }
	if (!convert_path(query, path)) return NULL;
	separator = strrchr(path, '/'); volume = strrchr(path, ':');
	if (volume != NULL && (separator == NULL || volume > separator)) separator = volume;
	if (separator != NULL) {
		strcpy(pattern, separator + 1);
		/* A volume colon is part of the directory name. */
		if (*separator == ':') separator[1] = 0; else *separator = 0;
	} else { strcpy(pattern, path); path[0] = 0; }
	directory_lock = Lock(path, ACCESS_READ);
	if (directory_lock == 0 || !Examine(directory_lock, &file_info)) {
#ifdef RESTUNTS_AMIGA_TRACE
		printf("Amiga file: cannot search %s (DOS error %ld)\n", path, (long)IoErr());
		fflush(stdout);
#endif
		if (directory_lock != 0) UnLock(directory_lock);
		directory_lock = 0; return NULL;
	}
	result = dos_file_find_next();
#ifdef RESTUNTS_AMIGA_TRACE
	printf("Amiga file: find %s => %s\n", (const char *)query,
		result != NULL ? (const char *)result : "no match");
	fflush(stdout);
#endif
	return result;
}

void amiga_file_shutdown(void)
{
	legacy_u16 i;
	for (i = 5; i < FILE_HANDLE_COUNT; ++i) if (handles[i] != 0) {
		Close(handles[i]); handles[i] = 0;
	}
	if (directory_lock != 0) { UnLock(directory_lock); directory_lock = 0; }
}
