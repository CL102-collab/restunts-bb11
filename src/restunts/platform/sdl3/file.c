#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../../c/platform.h"
#if defined(__ANDROID__) && !defined(RESTUNTS_HEADLESS)
#include <jni.h>
#include <SDL3/SDL.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define FILE_HANDLE_COUNT 64U
#define FILE_FIRST_HANDLE 5U
#define FILE_PATH_SIZE 1024U
#define FILE_NAME_SIZE 13U

#ifdef _WIN32
#include "file_windows.h"
#else
typedef DIR NativeDirectory;

static NativeDirectory *native_directory_open(const legacy_char *path)
{
	return opendir(path);
}

static const legacy_char *native_directory_read(NativeDirectory *directory)
{
	struct dirent *entry = readdir(directory);
	return entry != NULL ? entry->d_name : NULL;
}

static void native_directory_close(NativeDirectory *directory)
{
	closedir(directory);
}

static FILE *native_file_open(const legacy_char *path, legacy_s16 create)
{
	return fopen(path, create == DOS_FILE_OPEN_EXISTING ? "rb" : "wb+");
}

static legacy_int native_file_remove(const legacy_char *path)
{
	return remove(path);
}

static legacy_u16 native_regular_file(const legacy_char *path)
{
	struct stat info;
	return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}
#endif

#if defined(__ANDROID__) && defined(RESTUNTS_HEADLESS)
void android_saved_file_written(const legacy_s8 *path)
{
	/* The Android launcher publishes the command-line process's completed output. */
	(void)path;
}
#elif defined(__ANDROID__)
static const legacy_char android_save_method[] = "persistSavedFile";
static const legacy_char android_save_signature[] = "(Ljava/lang/String;)V";

void android_saved_file_written(const legacy_s8 *path)
{
	JNIEnv *env = SDL_GetAndroidJNIEnv();
	if (env == NULL) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot share saved file %s: %s", path,
					SDL_GetError());
		return;
	}
	jobject activity = SDL_GetAndroidActivity();
	jclass activity_class = NULL;
	jmethodID persist = NULL;
	jstring filename = NULL;
	if (activity != NULL && !(*env)->ExceptionCheck(env)) {
		activity_class = (*env)->GetObjectClass(env, activity);
	}
	if (activity_class != NULL && !(*env)->ExceptionCheck(env)) {
		persist = (*env)->GetStaticMethodID(env, activity_class, android_save_method,
											android_save_signature);
	}
	if (persist != NULL && !(*env)->ExceptionCheck(env)) {
		filename = (*env)->NewStringUTF(env, (const legacy_char *)path);
	}
	if (filename != NULL && !(*env)->ExceptionCheck(env)) {
		(*env)->CallStaticVoidMethod(env, activity_class, persist, filename);
	}
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionClear(env);
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot share saved file %s: Java exception",
					path);
	} else if (filename == NULL) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Cannot share saved file %s: Android callback unavailable", path);
	}
	if (filename != NULL) {
		(*env)->DeleteLocalRef(env, filename);
	}
	if (activity_class != NULL) {
		(*env)->DeleteLocalRef(env, activity_class);
	}
	if (activity != NULL) {
		(*env)->DeleteLocalRef(env, activity);
	}
}
#endif

enum FILE_IO_DIRECTION { FILE_IO_NONE, FILE_IO_READ, FILE_IO_WRITE };

static FILE *files[FILE_HANDLE_COUNT];
static legacy_u8 file_directions[FILE_HANDLE_COUNT];
#ifdef __EMSCRIPTEN__
static legacy_u8 file_modified[FILE_HANDLE_COUNT];
static legacy_char file_paths[FILE_HANDLE_COUNT][FILE_PATH_SIZE];

/* Pure MEMFS operations must stay synchronous, including exit handlers where
 * Emscripten has already shut down Asyncify. Browser policy owns the decision. */
EM_JS(legacy_int, persist_file_needed, (const legacy_char *path), {
	try {
		const predicate = Module['persistFileNeeded'];
		return predicate == null || predicate(UTF8ToString(path)) ? 1 : 0;
	} catch (error) {
		console.error('Cannot determine file persistence:', error);
		return -1;
	}
});

/* Await the browser write before reporting a successful DOS close/delete. */
EM_ASYNC_JS(legacy_int, persist_file, (const legacy_char *path, legacy_int removed), {
	try {
		await Module['persistFile'](UTF8ToString(path), removed != 0);
		return 0;
	} catch (error) {
		console.error('Cannot persist game file:', error);
		return -1;
	}
});

static legacy_int commit_file(const legacy_char *path, legacy_int removed)
{
	legacy_int needed = persist_file_needed(path);
	return needed > 0 ? persist_file(path, removed) : needed;
}
#endif
static legacy_s16 file_error;
static legacy_char **matches;
static size_t match_count;
static size_t match_index;

static legacy_u8 lower_ascii(legacy_u8 value)
{
	return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

static legacy_s32 compare_names(const legacy_char *left, const legacy_char *right)
{
	while (*left != 0 && lower_ascii(*left) == lower_ascii(*right)) {
		left++;
		right++;
	}
	return (legacy_s32)lower_ascii(*left) - (legacy_s32)lower_ascii(*right);
}

/* Resolve each component case-insensitively, retaining an exact match when
 * available. Original resources and saved configurations mix DOS casing. */
static legacy_s32 resolve_path(const legacy_char *source, legacy_char result[FILE_PATH_SIZE])
{
	legacy_char path[FILE_PATH_SIZE];
	size_t length = strlen(source);
	if (length == 0 || length >= sizeof(path)) {
		file_error = 1;
		return 0;
	}
	for (size_t index = 0; index <= length; index++) {
		path[index] = source[index] == '\\' ? '/' : source[index];
	}
	result[0] = 0;
	legacy_char *component = path;
	if (*component == '/') {
		strcpy(result, "/");
		component++;
	}
#if defined(_WIN32) || defined(__DJGPP__)
	if (component[0] != 0 && component[1] == ':') {
		result[0] = component[0];
		result[1] = ':';
		result[2] = 0;
		component += 2;
		if (*component == '/') {
			strcat(result, "/");
			component++;
		}
	}
#endif
	while (*component != 0) {
		legacy_char *separator = strchr(component, '/');
		if (separator != NULL) {
			*separator = 0;
		}
		legacy_char selected[FILE_PATH_SIZE];
		strcpy(selected, component);
		NativeDirectory *directory = native_directory_open(result[0] != 0 ? result : ".");
		if (directory != NULL) {
			const legacy_char *entry;
			legacy_s32 found = 0;
			while ((entry = native_directory_read(directory)) != NULL) {
				if (compare_names(component, entry) == 0 &&
					(!found || strcmp(entry, selected) < 0 || strcmp(entry, component) == 0)) {
					strcpy(selected, entry);
					found = 1;
					if (strcmp(selected, component) == 0) {
						break;
					}
				}
			}
			native_directory_close(directory);
		}
		size_t used = strlen(result);
		legacy_s32 needs_separator =
			used != 0 && result[used - 1] != '/' && result[used - 1] != ':';
		if (used + needs_separator + strlen(selected) >= FILE_PATH_SIZE) {
			file_error = 1;
			return 0;
		}
		if (needs_separator) {
			strcat(result, "/");
		}
		strcat(result, selected);
		if (separator == NULL) {
			break;
		}
		component = separator + 1;
	}
	return 1;
}

static FILE *get_file(legacy_u16 handle)
{
	if (handle < FILE_FIRST_HANDLE || handle >= FILE_HANDLE_COUNT || files[handle] == NULL) {
		file_error = 1;
		return NULL;
	}
	return files[handle];
}

/* DOS handles allow reads and writes in any order. C update streams require a
 * positioning operation between them, even when the logical offset is unchanged. */
static FILE *get_file_for_io(legacy_u16 handle, legacy_u8 direction)
{
	FILE *file = get_file(handle);
	if (file == NULL) {
		return NULL;
	}
	if (file_directions[handle] != FILE_IO_NONE && file_directions[handle] != direction &&
		fseek(file, 0, SEEK_CUR) != 0) {
		file_error = 1;
		return NULL;
	}
	file_directions[handle] = direction;
	return file;
}

legacy_u16 dos_file_open(const legacy_s8 *path, legacy_s16 create)
{
	legacy_char resolved[FILE_PATH_SIZE];
	file_error = 0;
	if (!resolve_path((const legacy_char *)path, resolved)) {
		return 0;
	}
	for (legacy_u16 handle = FILE_FIRST_HANDLE; handle < FILE_HANDLE_COUNT; handle++) {
		if (files[handle] == NULL) {
			files[handle] = native_file_open(resolved, create);
			if (files[handle] != NULL) {
				file_directions[handle] = FILE_IO_NONE;
#ifdef __EMSCRIPTEN__
				file_modified[handle] = create != DOS_FILE_OPEN_EXISTING;
				strcpy(file_paths[handle], resolved);
#endif
				return handle;
			}
			break;
		}
	}
	file_error = 1;
	return 0;
}

legacy_s16 dos_file_close(legacy_u16 handle)
{
	FILE *file = get_file(handle);
	if (file == NULL) {
		return -1;
	}
#ifdef __EMSCRIPTEN__
	legacy_u8 modified = file_modified[handle];
	legacy_char resolved[FILE_PATH_SIZE];
	if (modified) {
		strcpy(resolved, file_paths[handle]);
	}
	file_modified[handle] = 0;
	file_paths[handle][0] = 0;
#endif
	files[handle] = NULL;
	if (fclose(file) != 0) {
		file_error = 1;
		return -1;
	}
#ifdef __EMSCRIPTEN__
	if (modified && commit_file(resolved, 0) != 0) {
		file_error = 1;
		return -1;
	}
#endif
	return 0;
}

legacy_u16 dos_file_read(legacy_u16 handle, void *destination, legacy_u16 length)
{
	FILE *file = get_file_for_io(handle, FILE_IO_READ);
	if (file == NULL) {
		return 0;
	}
	size_t count = fread(destination, 1, length, file);
	if (ferror(file)) {
		file_error = 1;
	}
	return (legacy_u16)count;
}

legacy_u16 dos_file_write(legacy_u16 handle, const void *source, legacy_u16 length)
{
	FILE *file = get_file_for_io(handle, FILE_IO_WRITE);
	if (file == NULL) {
		return 0;
	}
	size_t count = fwrite(source, 1, length, file);
#ifdef __EMSCRIPTEN__
	if (count != 0) {
		file_modified[handle] = 1;
	}
#endif
	if (count != length) {
		file_error = 1;
	}
	return (legacy_u16)count;
}

legacy_s16 dos_file_seek(legacy_u16 handle, legacy_s32 offset, legacy_s16 origin)
{
	FILE *file = get_file(handle);
	legacy_s32 origins[] = {SEEK_SET, SEEK_CUR, SEEK_END};
	if (file == NULL || origin < 0 || origin > DOS_FILE_SEEK_END ||
		fseek(file, offset, origins[origin]) != 0) {
		file_error = 1;
		return -1;
	}
	file_directions[handle] = FILE_IO_NONE;
	return 0;
}

legacy_s32 dos_file_tell(legacy_u16 handle)
{
	FILE *file = get_file(handle);
	/* Preserve the full ftell result until its legacy-range check. */
	legacy_s64 position = file != NULL ? ftell(file) : -1;
	if (position < 0 || position > (legacy_s64)LEGACY_S32_MAX) {
		file_error = 1;
		return -1;
	}
	return (legacy_s32)position;
}

legacy_s16 dos_file_error(void)
{
	legacy_s16 result = file_error;
	file_error = 0;
	return result;
}

legacy_s16 dos_file_remove(const legacy_s8 *path)
{
	legacy_char resolved[FILE_PATH_SIZE];
	if (!resolve_path((const legacy_char *)path, resolved)) {
		return -1;
	}
#ifdef __EMSCRIPTEN__
	/* Retain the virtual file when its persistent deletion fails. Check the
	 * virtual path first so a missing file cannot delete an external copy. */
	struct stat info;
	if (stat(resolved, &info) != 0 || commit_file(resolved, 1) != 0) {
		file_error = 1;
		return -1;
	}
#endif
	if (native_file_remove(resolved) != 0) {
		file_error = 1;
		return -1;
	}
	return 0;
}

static legacy_s32 wildcard_matches(const legacy_char *pattern, const legacy_char *name)
{
	const legacy_char *star = NULL;
	const legacy_char *retry = NULL;
	if (strcmp(pattern, "*.*") == 0) {
		pattern = "*";
	}
	while (*name != 0) {
		if (*pattern == '?' || (*pattern != 0 && lower_ascii(*pattern) == lower_ascii(*name))) {
			pattern++;
			name++;
		} else if (*pattern == '*') {
			star = ++pattern;
			retry = name;
		} else if (star != NULL) {
			pattern = star;
			name = ++retry;
		} else {
			return 0;
		}
	}
	while (*pattern == '*') {
		pattern++;
	}
	return *pattern == 0;
}

/* Match the comparator result type required by qsort. */
static legacy_int sort_names(const void *left, const void *right)
{
	const legacy_char *a = *(const legacy_char *const *)left;
	const legacy_char *b = *(const legacy_char *const *)right;
	legacy_s32 result = compare_names(a, b);
	return result != 0 ? result : strcmp(a, b);
}

const legacy_s8 *dos_file_find_next(void)
{
	return match_index < match_count ? (const legacy_s8 *)matches[match_index++] : NULL;
}

const legacy_s8 *dos_file_find_first(const legacy_s8 *query)
{
	for (size_t index = 0; index < match_count; index++) {
		free(matches[index]);
	}
	free(matches);
	matches = NULL;
	match_count = match_index = 0;
	legacy_char resolved[FILE_PATH_SIZE];
	if (!resolve_path((const legacy_char *)query, resolved)) {
		return NULL;
	}
	legacy_char *pattern = strrchr(resolved, '/');
	const legacy_char *directory_name = ".";
	if (pattern != NULL) {
		*pattern++ = 0;
		directory_name = resolved[0] != 0 ? resolved : "/";
	} else {
		pattern = resolved;
	}
	NativeDirectory *directory = native_directory_open(directory_name);
	if (directory == NULL) {
		return NULL;
	}
	const legacy_char *entry;
	while ((entry = native_directory_read(directory)) != NULL) {
		if (entry[0] == '.' || strlen(entry) >= FILE_NAME_SIZE ||
			!wildcard_matches(pattern, entry)) {
			continue;
		}
		legacy_char full_path[FILE_PATH_SIZE];
		if (snprintf(full_path, sizeof(full_path), "%s/%s", directory_name, entry) >=
				(legacy_s32)sizeof(full_path) ||
			!native_regular_file(full_path)) {
			continue;
		}
		legacy_char **grown = realloc(matches, (match_count + 1) * sizeof(*matches));
		if (grown == NULL) {
			file_error = 1;
			break;
		}
		matches = grown;
		matches[match_count] = calloc(FILE_NAME_SIZE, 1);
		if (matches[match_count] == NULL) {
			file_error = 1;
			break;
		}
		strcpy(matches[match_count++], entry);
	}
	native_directory_close(directory);
	if (match_count > 1) {
		qsort(matches, match_count, sizeof(*matches), sort_names);
	}
	return dos_file_find_next();
}
