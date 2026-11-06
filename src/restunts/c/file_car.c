#include <stddef.h>
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA_TRACE)
#include <stdio.h>
#endif
#include "externs.h"
#include "fileio.h"
#include "memmgr.h"
#include "platform.h"
#include "shape2d/shape2d.h"
#include "fatal.h"
#include "resource.h"
#include "ui/ui_dialog.h"
#include "audio/audio_control.h"
#ifdef RESTUNTS_SDL3
#include "driving_rules.h"
#ifndef RESTUNTS_HEADLESS
#endif
#endif

#include "fileio_internal.h"

enum FILE_CAR_ROOT {
	FILE_CAR_DATA_LOOSE,
	FILE_CAR_BASE_LOOSE,
	FILE_CAR_DATA_DIRECTORY,
	FILE_CAR_BASE_DIRECTORY,
	FILE_CAR_BUNDLED_DIRECTORY,
	FILE_CAR_ROOT_COUNT
};

enum FILE_CAR_FAMILY { FILE_CAR_NONE, FILE_CAR_PHYSICS, FILE_CAR_MODEL, FILE_CAR_DASHBOARD };
#ifdef RESTUNTS_SDL3
#define FILE_CAR_HYPERVISION_MODEL (FILE_CAR_DASHBOARD + 1U)
#endif

static legacy_s8 file_car_base[FILE_CAR_PATH_SIZE];
static const legacy_s8 *const file_car_physics_extensions[] = {".res", ".pre", 0};
static const legacy_s8 *const file_car_model_extensions[] = {".p3s", ".3sh", 0};
#ifdef RESTUNTS_SDL3
static const legacy_s8 *const file_car_hypervision_extensions[] = {".h3d", 0};
#endif
static const legacy_s8 *const file_car_dashboard_extensions[] = {".pvs", ".vsh", ".xvs",
																 ".pes", ".esh", 0};

static legacy_s8 file_car_upper(legacy_s8 value)
{
	return value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value;
}

static legacy_u16 file_car_equal(const legacy_s8 *left, const legacy_s8 *right)
{
	while (*left != 0 && file_car_upper(*left) == file_car_upper(*right)) {
		left++;
		right++;
	}
	return *left == 0 && *right == 0;
}

static legacy_u16 file_car_append(legacy_s8 *destination, legacy_u16 capacity,
								  const legacy_s8 *source)
{
	legacy_u16 used = 0;
	while (destination[used] != 0) {
		used++;
	}
	while (*source != 0) {
		if (used + 1U >= capacity) {
			return 0;
		}
		destination[used++] = *source++;
	}
	destination[used] = 0;
	return 1;
}

void file_car_set_base(const legacy_s8 *directory)
{
	file_car_base[0] = 0;
	if (directory != 0 && !file_car_append(file_car_base, sizeof(file_car_base), directory)) {
		file_car_base[0] = 0;
	}
}

static const legacy_s8 *const *file_car_extensions(legacy_u16 family)
{
	if (family == FILE_CAR_PHYSICS) {
		return file_car_physics_extensions;
	}
	if (family == FILE_CAR_MODEL) {
		return file_car_model_extensions;
	}
#ifdef RESTUNTS_SDL3
	if (family == FILE_CAR_HYPERVISION_MODEL) {
		return file_car_hypervision_extensions;
	}
#endif
	return file_car_dashboard_extensions;
}

/* Only DOS car resource names participate: unrelated game files and explicit
 * paths retain their original behavior. Car IDs are exactly four characters. */
static legacy_u16 file_car_family(const legacy_s8 *filename, legacy_u16 *stem_length)
{
	legacy_u16 length = 0;
	for (const legacy_s8 *cursor = filename; *cursor != 0; cursor++) {
		if (*cursor == '/' || *cursor == '\\' || *cursor == ':' || *cursor == '*' ||
			*cursor == '?') {
			return FILE_CAR_NONE;
		}
		length++;
	}
	legacy_u16 family = FILE_CAR_NONE;
	legacy_u16 prefix = 0;
	if (length >= sizeof("car") - 1U && file_car_upper(filename[0]) == 'C' &&
		file_car_upper(filename[1]) == 'A' && file_car_upper(filename[2]) == 'R') {
		family = FILE_CAR_PHYSICS;
		prefix = sizeof("car") - 1U;
	} else if (length >= sizeof("st") - 1U && file_car_upper(filename[0]) == 'S' &&
			   file_car_upper(filename[1]) == 'T') {
		family = FILE_CAR_MODEL;
		prefix = sizeof("st") - 1U;
		if (length == sizeof("stda") - 1U + FILE_CAR_ID_LENGTH + sizeof(".vsh") - 1U &&
			file_car_upper(filename[2]) == 'D' &&
			(file_car_upper(filename[3]) == 'A' || file_car_upper(filename[3]) == 'B')) {
			family = FILE_CAR_DASHBOARD;
			prefix = sizeof("stda") - 1U;
		}
	}
	*stem_length = prefix + FILE_CAR_ID_LENGTH;
	if (family == FILE_CAR_NONE || length != *stem_length + sizeof(".res") - 1U) {
		return FILE_CAR_NONE;
	}
	for (legacy_u16 index = prefix; index < *stem_length; index++) {
		if (filename[index] == '.') {
			return FILE_CAR_NONE;
		}
	}
#ifdef RESTUNTS_SDL3
	if (family == FILE_CAR_MODEL && file_car_equal(filename + *stem_length, ".h3d")) {
		family = FILE_CAR_HYPERVISION_MODEL;
	}
#endif
	const legacy_s8 *const *extensions = file_car_extensions(family);
	for (legacy_u16 index = 0; extensions[index] != 0; index++) {
		if (file_car_equal(filename + *stem_length, extensions[index])) {
			return family;
		}
	}
	return FILE_CAR_NONE;
}

static legacy_u16 file_resource_path(legacy_u16 root, const legacy_s8 *filename,
									 legacy_s8 path[FILE_CAR_PATH_SIZE], const legacy_s8 *directory,
									 const legacy_s8 *bundled)
{
	path[0] = 0;
	if (root == FILE_CAR_BASE_LOOSE || root == FILE_CAR_BASE_DIRECTORY) {
		if (file_car_base[0] == 0 || !file_car_append(path, FILE_CAR_PATH_SIZE, file_car_base)) {
			return 0;
		}
	} else if (root == FILE_CAR_BUNDLED_DIRECTORY) {
		if (bundled == 0 || !file_car_append(path, FILE_CAR_PATH_SIZE, bundled)) {
			return 0;
		}
	}
	legacy_u16 used = 0;
	while (path[used] != 0) {
		used++;
	}
	if (used != 0 && path[used - 1U] != '/' && path[used - 1U] != '\\' && path[used - 1U] != ':' &&
		!file_car_append(path, FILE_CAR_PATH_SIZE, "/")) {
		return 0;
	}
	if ((root == FILE_CAR_DATA_DIRECTORY || root == FILE_CAR_BASE_DIRECTORY) &&
		!file_car_append(path, FILE_CAR_PATH_SIZE, directory)) {
		return 0;
	}
	return file_car_append(path, FILE_CAR_PATH_SIZE, filename);
}

static legacy_u16 file_car_path(legacy_u16 root, const legacy_s8 *filename,
								legacy_s8 path[FILE_CAR_PATH_SIZE])
{
	return file_resource_path(root, filename, path, "cars/",
#ifdef RESTUNTS_CAR_DIRECTORY
							  RESTUNTS_CAR_DIRECTORY
#else
							  0
#endif
	);
}

/* Select a directory before choosing a packed/unpacked representation. This
 * keeps a loose PRE/P3S/PVS override ahead of a bundled RES/3SH/VSH, too. */
legacy_u16 file_car_resolve(const legacy_s8 *filename, legacy_s8 path[FILE_CAR_PATH_SIZE])
{
	legacy_u16 stem_length;
	legacy_u16 family = file_car_family(filename, &stem_length);
	if (family == FILE_CAR_NONE) {
		return 0;
	}
	const legacy_s8 *const *extensions = file_car_extensions(family);
	legacy_s8 alternate[DOS_FILENAME_LENGTH];
	for (legacy_u16 index = 0; index < stem_length; index++) {
		alternate[index] = filename[index];
	}
	for (legacy_u16 root = FILE_CAR_DATA_LOOSE; root < FILE_CAR_ROOT_COUNT; root++) {
		if (!file_car_path(root, filename, path)) {
			continue;
		}
		fileio_handle handle = dos_file_open(path, DOS_FILE_OPEN_EXISTING);
		if (handle != FILEIO_INVALID_HANDLE) {
			dos_file_close(handle);
			return 1;
		}
		for (legacy_u16 index = 0; extensions[index] != 0; index++) {
			alternate[stem_length] = 0;
			if (!file_car_append(alternate, sizeof(alternate), extensions[index]) ||
				!file_car_path(root, alternate, path)) {
				continue;
			}
			handle = dos_file_open(path, DOS_FILE_OPEN_EXISTING);
			if (handle != FILEIO_INVALID_HANDLE) {
				dos_file_close(handle);
				return 0;
			}
		}
#ifdef RESTUNTS_SDL3
		/* A replacement classic model masks companions from lower-priority
		 * directories. An explicit companion in this root was checked above. */
		if (family == FILE_CAR_HYPERVISION_MODEL) {
			for (legacy_u16 index = 0; file_car_model_extensions[index] != 0; index++) {
				alternate[stem_length] = 0;
				if (!file_car_append(alternate, sizeof(alternate),
									 file_car_model_extensions[index]) ||
					!file_car_path(root, alternate, path)) {
					continue;
				}
				handle = dos_file_open(path, DOS_FILE_OPEN_EXISTING);
				if (handle != FILEIO_INVALID_HANDLE) {
					dos_file_close(handle);
					return 0;
				}
			}
		}
#endif
	}
	return 0;
}

/* Track names use the same loose-before-subdirectory precedence as cars. An
 * explicitly selected directory remains isolated from the automatic search. */
const legacy_s8 *file_track_query(const legacy_s8 *filename)
{
	while (filename[0] == '.' && (filename[1] == '/' || filename[1] == '\\')) {
		filename += 2;
	}
	const legacy_s8 *extension = 0;
	legacy_u16 length = 0;
	for (const legacy_s8 *cursor = filename; *cursor != 0; cursor++) {
		if (*cursor == '/' || *cursor == '\\' || *cursor == ':' ||
			++length >= FILE_FIND_NAME_SIZE) {
			return 0;
		}
		if (*cursor == '.') {
			extension = cursor;
		}
	}
	if (extension == 0 || extension == filename ||
		(!file_car_equal(extension, ".trk") && !file_car_equal(extension, ".cstrk") &&
		 !file_car_equal(extension, ".hig") && !file_car_equal(extension, ".cshig"))) {
		return 0;
	}
	return filename;
}

static legacy_u16 file_track_path(legacy_u16 root, const legacy_s8 *filename,
								  legacy_s8 path[FILE_CAR_PATH_SIZE])
{
	return file_resource_path(root, filename, path, "tracks/", 0);
}

legacy_u16 file_track_resolve(const legacy_s8 *filename, legacy_s8 path[FILE_CAR_PATH_SIZE])
{
	filename = file_track_query(filename);
	if (filename == 0) {
		return 0;
	}
	for (legacy_u16 root = FILE_CAR_DATA_LOOSE; root < FILE_CAR_ROOT_COUNT; root++) {
		if (!file_track_path(root, filename, path)) {
			continue;
		}
		fileio_handle handle = dos_file_open(path, DOS_FILE_OPEN_EXISTING);
		if (handle != FILEIO_INVALID_HANDLE) {
			dos_file_close(handle);
			return 1;
		}
	}
	return 0;
}

static struct {
	legacy_u16 root;
	legacy_s8 pattern[FILE_FIND_NAME_SIZE];
	legacy_s8 previous[FILE_FIND_NAME_SIZE];
	legacy_s8 path[FILE_CAR_PATH_SIZE];
} file_track_search;

static const legacy_s8 *file_track_find_result(const legacy_s8 *found)
{
	legacy_s8 query[FILE_CAR_PATH_SIZE];
	legacy_s8 preferred[FILE_CAR_PATH_SIZE];
	while (file_track_search.root < FILE_CAR_ROOT_COUNT) {
		while (found != 0) {
			/* Native directory enumeration sorts names without case. The other
			 * supported filesystems already prohibit case-only duplicates. */
			if (!file_car_equal(found, file_track_search.previous) &&
				file_track_path(file_track_search.root, found, file_track_search.path) &&
				file_track_resolve(found, preferred) &&
				file_car_equal(file_track_search.path, preferred)) {
				strcpy(file_track_search.previous, found);
				return file_track_search.path;
			}
			found = dos_file_find_next();
		}
		file_track_search.root++;
		file_track_search.previous[0] = 0;
		if (file_track_search.root < FILE_CAR_ROOT_COUNT &&
			file_track_path(file_track_search.root, file_track_search.pattern, query)) {
			found = dos_file_find_first(query);
		}
	}
	return 0;
}

const legacy_s8 *file_track_find_first(const legacy_s8 *query)
{
	const legacy_s8 *pattern = file_track_query(query);
	file_track_search.root = FILE_CAR_DATA_LOOSE;
	file_track_search.previous[0] = 0;
	strcpy(file_track_search.pattern, pattern);
	return file_track_find_result(dos_file_find_first(pattern));
}

const legacy_s8 *file_track_find_next(void)
{
	if (file_track_search.root >= FILE_CAR_ROOT_COUNT) {
		return 0;
	}
	return file_track_find_result(dos_file_find_next());
}

legacy_u16 file_open_resource(const legacy_s8 *filename)
{
	fileio_handle handle = dos_file_open(filename, DOS_FILE_OPEN_EXISTING);
	if (handle == FILEIO_INVALID_HANDLE) {
		legacy_s8 path[FILE_CAR_PATH_SIZE];
		if (file_car_resolve(filename, path) || file_track_resolve(filename, path)) {
			handle = dos_file_open(path, DOS_FILE_OPEN_EXISTING);
		}
	}
	return handle;
}

legacy_u16 file_find_car_ids(legacy_s8 car_ids[][FILE_CAR_ID_BUFFER_SIZE], legacy_u16 capacity)
{
	legacy_u16 count = 0;
	legacy_s8 query[FILE_CAR_PATH_SIZE];
	for (legacy_u16 root = FILE_CAR_DATA_LOOSE; root < FILE_CAR_ROOT_COUNT; root++) {
		for (legacy_u16 extension = 0; file_car_physics_extensions[extension] != 0; extension++) {
			legacy_s8 pattern[DOS_FILENAME_LENGTH] = "car????";
			if (!file_car_append(pattern, sizeof(pattern),
								 file_car_physics_extensions[extension]) ||
				!file_car_path(root, pattern, query)) {
				continue;
			}
			const legacy_s8 *found = dos_file_find_first(query);
			while (found != 0 && count < capacity) {
				legacy_u16 stem_length;
				if (file_car_family(found, &stem_length) == FILE_CAR_PHYSICS) {
					legacy_s8 id[FILE_CAR_ID_BUFFER_SIZE];
					for (legacy_u16 index = 0; index < FILE_CAR_ID_LENGTH; index++) {
						id[index] = file_car_upper(found[sizeof("car") - 1U + index]);
					}
					id[FILE_CAR_ID_LENGTH] = 0;
					legacy_u16 duplicate = 0;
					for (legacy_u16 index = 0; index < count; index++) {
						if (file_car_equal(id, car_ids[index])) {
							duplicate = 1;
							break;
						}
					}
					if (!duplicate) {
						for (legacy_u16 index = 0; index < FILE_CAR_ID_BUFFER_SIZE; index++) {
							car_ids[count][index] = id[index];
						}
						count++;
					}
				}
				found = dos_file_find_next();
			}
		}
	}
	return count;
}
