#include <stddef.h>
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA_TRACE)
#include <stdio.h>
#endif
#include "externs.h"
#include "fileio.h"
#include "memmgr.h"
#include "platform.h"
#include "shape2d.h"
#include "fatal.h"
#include "resource.h"
#include "ui_dialog.h"
#include "audio_control.h"
#ifdef RESTUNTS_SDL3
#include "extended_file.h"
#include "extended_track.h"
#ifndef RESTUNTS_HEADLESS
#include "extended_replay_runtime.h"
#endif
#endif

#ifdef RESTUNTS_HEADLESS
static const legacy_s8 headless_file_error[] = "File error: %s";
static const legacy_s8 headless_write_error[] = "File write error: %s";
static const legacy_s8 headless_size_error[] = "File size error: %s";
static const legacy_s8 headless_pack_error[] = "Invalid packed resource: %s";
#define file_read_error_format headless_file_error
#define file_write_error_format headless_write_error
#define file_size_error_format headless_size_error
#define invalid_pack_type_error_format headless_pack_error
#endif

#ifdef RESTUNTS_AMIGA_TRACE
#define AMIGA_DECOMP_TRACE(stage)                                                                  \
	do {                                                                                           \
		puts("Amiga decompress: " stage);                                                          \
		fflush(stdout);                                                                            \
	} while (0)
#else
#define AMIGA_DECOMP_TRACE(stage) ((void)0)
#endif

#define FILE_IO_PAGE_SIZE 16384
#define FILE_IO_PAGE_SEGMENT_GAP 1024
#define DOS_FILENAME_LENGTH 13
#ifdef RESTUNTS_SDL3
#define FILE_FIND_NAME_SIZE 15U
#else
#define FILE_FIND_NAME_SIZE DOS_FILENAME_LENGTH
#endif
#define FILE_FIND_PATH_SIZE FILE_CAR_PATH_SIZE
#define FILE_COMBINE_PATH_SIZE 80
#define RESOURCE_NAME_BUFFER_SIZE 80

#define RS_RLE_ESCLEN_MAX 16
#define RS_RLE_ESCLOOKUP_LEN 256
#define RS_RLE_ESCSEQ_POS 1

#define RS_VLE_ESC_LEN 16
#define RS_VLE_ALPH_LEN 256
#define RS_VLE_ESC_WIDTH 64
#define RS_VLE_NUM_SYMB 128

#define DOS_PARAGRAPH_SHIFT 4U
#define DOS_PARAGRAPH_MASK 15L
#define BYTE_HIGH_BIT 128U
#define BYTE_SHIFT LEGACY_BYTE_BITS
#define BYTE_BIT_COUNT LEGACY_BYTE_BITS

enum RLE_COUNT_CODE { RLE_BYTE_COUNT_CODE = 1, RLE_WORD_COUNT_CODE = 3 };

#define RLE_SEQUENCE_TRAILER_SIZE 2
#define VLE_EXTENDED_CODE_START_WIDTH 7U
#define VLE_ALPHABET_BRANCH_FACTOR 2U
#define VLE_CODE_WORD_SECOND_BYTE_OFFSET 1U
#define COMPRESSION_WORKSPACE_PARAGRAPHS 4U

enum COMPRESSION_TYPE { COMPRESSION_RLE_TYPE = 1, COMPRESSION_VLE_TYPE = 2 };

#define FILE_ERROR_DIALOG_ABORT 2

#define COMPR_HEADER_SIZE 4U
#define COMPR_SIZE_LOW_OFFSET 1U
#define COMPR_SIZE_HIGH_OFFSET 3U

#define COMPR_RLE_HEADER_SIZE 5U
#define COMPR_RLE_SIZE_HIGH_OFFSET 2U
#define COMPR_RLE_ESCLEN_OFFSET 4U

typedef legacy_u16 fileio_handle;

#define FILEIO_INVALID_HANDLE 0U
enum FILE_CAR_ROOT {
	FILE_CAR_DATA_LOOSE,
	FILE_CAR_BASE_LOOSE,
	FILE_CAR_DATA_DIRECTORY,
	FILE_CAR_BASE_DIRECTORY,
	FILE_CAR_BUNDLED_DIRECTORY,
	FILE_CAR_ROOT_COUNT
};

enum FILE_CAR_FAMILY { FILE_CAR_NONE, FILE_CAR_PHYSICS, FILE_CAR_MODEL, FILE_CAR_DASHBOARD };

static legacy_s8 file_car_base[FILE_CAR_PATH_SIZE];
static const legacy_s8 *const file_car_physics_extensions[] = {".res", ".pre", 0};
static const legacy_s8 *const file_car_model_extensions[] = {".p3s", ".3sh", 0};
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
	const legacy_s8 *const *extensions = file_car_extensions(family);
	for (legacy_u16 index = 0; extensions[index] != 0; index++) {
		if (file_car_equal(filename + *stem_length, extensions[index])) {
			return family;
		}
	}
	return FILE_CAR_NONE;
}

static legacy_u16 file_car_path(legacy_u16 root, const legacy_s8 *filename,
								legacy_s8 path[FILE_CAR_PATH_SIZE])
{
	path[0] = 0;
	if (root == FILE_CAR_BASE_LOOSE || root == FILE_CAR_BASE_DIRECTORY) {
		if (file_car_base[0] == 0 || !file_car_append(path, FILE_CAR_PATH_SIZE, file_car_base)) {
			return 0;
		}
	} else if (root == FILE_CAR_BUNDLED_DIRECTORY) {
#ifdef RESTUNTS_CAR_DIRECTORY
		if (!file_car_append(path, FILE_CAR_PATH_SIZE, RESTUNTS_CAR_DIRECTORY)) {
			return 0;
		}
#else
		return 0;
#endif
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
		!file_car_append(path, FILE_CAR_PATH_SIZE, "cars/")) {
		return 0;
	}
	return file_car_append(path, FILE_CAR_PATH_SIZE, filename);
}

/* Select a directory before choosing a packed/unpacked representation. This
 * keeps a loose PRE/P3S/PVS override ahead of a bundled RES/3SH/VSH, too. */
static legacy_u16 file_car_resolve(const legacy_s8 *filename, legacy_s8 path[FILE_CAR_PATH_SIZE])
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
	}
	return 0;
}

legacy_u16 file_open_resource(const legacy_s8 *filename)
{
	fileio_handle handle = dos_file_open(filename, DOS_FILE_OPEN_EXISTING);
	if (handle == FILEIO_INVALID_HANDLE) {
		legacy_s8 path[FILE_CAR_PATH_SIZE];
		if (file_car_resolve(filename, path)) {
			handle = dos_file_open(path, DOS_FILE_OPEN_EXISTING);
		}
	}
	return handle;
}

static fileio_handle fileio_open(const legacy_s8 *path, legacy_s16 create)
{
	return create == DOS_FILE_OPEN_EXISTING ? file_open_resource(path)
											: dos_file_open(path, create);
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

static legacy_s16 fileio_close(fileio_handle file)
{
	return dos_file_close(file);
}

static size_t fileio_read(void far *dst, size_t size, size_t nmemb, fileio_handle file)
{
	return dos_file_read(file, dst, (legacy_u16)(size * nmemb));
}

static size_t fileio_write(const void far *src, size_t size, size_t nmemb, fileio_handle file)
{
	return dos_file_write(file, src, (legacy_u16)(size * nmemb));
}

static legacy_s16 fileio_seek(fileio_handle file, legacy_s32 offset, legacy_s16 origin)
{
	return dos_file_seek(file, offset, origin);
}

static legacy_s32 fileio_tell(fileio_handle file)
{
	return dos_file_tell(file);
}

static legacy_s16 fileio_error(void)
{
	return dos_file_error();
}

static legacy_s16 fileio_remove(const legacy_s8 *path)
{
	return dos_file_remove(path);
}

struct file_find_dos {
	legacy_s8 path[FILE_FIND_PATH_SIZE];
	legacy_s8 *dirdelim;
} g_find;

// Find file matching given query. Returns pointer to first matched filename
// including path from the query. NULL is returned on error/no hits.
// FIXME: DOS specific implementation.
const legacy_s8 *file_find(const legacy_s8 *query)
{
	legacy_s8 car_path[FILE_CAR_PATH_SIZE];
	const legacy_s8 *found_name = dos_file_find_first(query);
	if (found_name == 0 && file_car_resolve(query, car_path)) {
		query = car_path;
		found_name = dos_file_find_first(query);
	}
	if (found_name == 0) {
		return 0;
	}

	// Copy path from query.
	legacy_s8 *chdst = g_find.dirdelim = g_find.path;
	for (legacy_s8 const *chsrc = query; *chsrc; ++chsrc, ++chdst) {
		if (chdst + 1U >= g_find.path + sizeof(g_find.path)) {
			return 0;
		}
		*chdst = *chsrc;

		if (*chdst == ':' || *chdst == '\\' || *chdst == '/') {
			g_find.dirdelim = chdst + 1;
		}
	}

	// Copy found filename to result path.
	if (g_find.dirdelim + FILE_FIND_NAME_SIZE > g_find.path + sizeof(g_find.path)) {
		return 0;
	}
	strcpy(g_find.dirdelim, found_name);

	return g_find.path;
}

// Returns next found filename from file_find() query.
// FIXME: DOS specific implementation.
const legacy_s8 *file_find_next(void)
{
	const legacy_s8 *found_name = dos_file_find_next();
	if (found_name == 0) {
		return 0;
	}

	// Copy found filename to result path.
	if (g_find.dirdelim + FILE_FIND_NAME_SIZE > g_find.path + sizeof(g_find.path)) {
		return 0;
	}
	strcpy(g_find.dirdelim, found_name);

	return g_find.path;
}

const legacy_s8 *file_find_next_alt(void)
{
	return file_find_next();
}

void file_build_path(const legacy_s8 *dir, const legacy_s8 *name, const legacy_s8 *ext,
					 legacy_s8 *dst)
{
	legacy_s16 dirlen;

	if (dir) {
		strcpy(dst, dir);
		dirlen = strlen(dir);
	} else {
		dst[0] = 0;
		dirlen = 0;
	}

	// Add directory separator if needed.
	if (dirlen && dir[dirlen - 1] != ':' && dir[dirlen - 1] != '\\' && dir[dirlen - 1] != '/') {
		strcat(dst, "\\");
	}

	strcat(dst, name);
	strcat(dst, ext);
}

const legacy_s8 *file_combine_and_find(const legacy_s8 *dir, const legacy_s8 *name,
									   const legacy_s8 *ext)
{
	// The original reserves 80 bytes of stack for this (var_50 = byte ptr -80).
	legacy_s8 path[FILE_COMBINE_PATH_SIZE];

	file_build_path(dir, name, ext, path);

	return file_find(path);
}

// Round a byte count up to the number of whole 16-byte paragraphs holding it.
static legacy_u16 file_bytes_to_paras(legacy_s32 length)
{
	return (legacy_u16)((length >> DOS_PARAGRAPH_SHIFT) + (length & DOS_PARAGRAPH_MASK ? 1 : 0));
}

// Get number of 16-byte blocks needed to store entire file.
legacy_u16 file_paras(const legacy_s8 *filename, legacy_s16 fatal)
{
	fileio_handle file;
	if ((file = fileio_open(filename, DOS_FILE_OPEN_EXISTING)) != FILEIO_INVALID_HANDLE) {
		fileio_seek(file, 0, DOS_FILE_SEEK_END);
		legacy_s32 length = fileio_tell(file);
		fileio_close(file);

		if (!fileio_error()) {
			// May overflow, but all Stunts files are rather small.
			return file_bytes_to_paras(length);
		}
	}

	if (fatal) {
		fatal_error(file_read_error_format, filename);
	}

	return 0;
}

legacy_u16 file_paras_fatal(const legacy_s8 *filename)
{
	return file_paras(filename, 1);
}

legacy_u16 file_paras_nofatal(const legacy_s8 *filename)
{
	return file_paras(filename, 0);
}

// Get number of 16-byte blocks needed to store the final result of an assumed compressed file.
legacy_u16 file_decomp_paras(const legacy_s8 *filename, legacy_s16 fatal)
{
	fileio_handle file;
	legacy_u8 header[COMPR_HEADER_SIZE];
	if ((file = fileio_open(filename, DOS_FILE_OPEN_EXISTING)) != FILEIO_INVALID_HANDLE) {
		fileio_read(header, sizeof(header), 1, file);
		fileio_close(file);

		if (!fileio_error()) {
			// May overflow, but all Stunts files are rather small.
			legacy_s32 length = (legacy_s32)LEGACY_READ_U16_LE(header + COMPR_SIZE_LOW_OFFSET) |
								((legacy_s32)header[COMPR_SIZE_HIGH_OFFSET] << LEGACY_WORD_BITS);
			return file_bytes_to_paras(length);
		}
	}

	if (fatal) {
		fatal_error(file_size_error_format, filename);
	}

	return 0;
}

legacy_u16 file_decomp_paras_fatal(const legacy_s8 *filename)
{
	return file_decomp_paras(filename, 1);
}

legacy_u16 file_decomp_paras_nofatal(const legacy_s8 *filename)
{
	return file_decomp_paras(filename, 0);
}

// Read entire file to given destination. Optionally handle errors as fatal.
void far *file_read(const legacy_s8 *filename, void far *dst, legacy_s16 fatal)
{
	void far *curdst = dst;

	legacy_s16 readlen;
	fileio_handle file;
	if ((file = fileio_open(filename, DOS_FILE_OPEN_EXISTING)) != FILEIO_INVALID_HANDLE) {
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
		/* The caller allocates the file's actual size. Windows can reject an
		 * oversized read request that crosses the end of a small stack object,
		 * even if EOF would otherwise limit the number of transferred bytes. */
		legacy_s32 remaining = -1;
		if (fileio_seek(file, 0, DOS_FILE_SEEK_END) == 0) {
			remaining = fileio_tell(file);
		}
		if (remaining >= 0 && fileio_seek(file, 0, DOS_FILE_SEEK_BEGIN) == 0) {
			while (remaining != 0) {
				legacy_u16 count = remaining > FILE_IO_PAGE_SIZE ? FILE_IO_PAGE_SIZE : remaining;
				readlen = fileio_read(curdst, count, 1, file);
				remaining -= readlen;
				if (readlen != count) {
					break;
				}
				/* Native stack/heap segments are identifiers, not paragraph bases. */
				if (remaining != 0) {
					curdst = (legacy_u8 *)curdst + readlen;
				}
			}
		}
#else
		// Read one page at a time.
		do {
			readlen = fileio_read(curdst, FILE_IO_PAGE_SIZE, 1, file);
			curdst = dos_memory_make_pointer(dos_memory_pointer_segment(curdst) +
												 FILE_IO_PAGE_SEGMENT_GAP,
											 dos_memory_pointer_offset(dst));
		} while (readlen == FILE_IO_PAGE_SIZE);
#endif

		fileio_close(file);

		if (!fileio_error()) {
			return dst;
		}
	}

	if (fatal) {
		fatal_error(file_read_error_format, filename);
	}

	return 0;
}

// Read entire file to given destination, handle errors as fatal.
void far *file_read_fatal(const legacy_s8 *filename, void far *dst)
{
	return file_read(filename, dst, 1);
}

// Read entire file to given destination, returns NULL pointer if errors occur.
void far *file_read_nofatal(const legacy_s8 *filename, void far *dst)
{
	return file_read(filename, dst, 0);
}

// Write given source buffer to file.
//
// NOTE the polarity of `fatal`: the original tests it the other way round
// (`cmp [bp+var_fatal],0 / jnz` jumps to the RETURN), so the entry point named
// file_write_fatal, which passes 1, hands the DOS error code back to its
// caller, and file_write_nofatal, which passes 0, is the one that aborts.
// That looks like a slip in the 1990 code, but the callers are built around
// it - asmorig/seg009.asm:2299 checks the result and puts up a disk-error
// panel - so it is reproduced rather than corrected.
legacy_s16 file_write(const legacy_s8 *filename, void far *src, legacy_u32 length, legacy_s16 fatal)
{
	fileio_handle file;
	legacy_u16 retval = 0;
#ifdef __EMSCRIPTEN__
	legacy_u8 close_failed = 0;
#endif
	if ((file = fileio_open(filename, DOS_FILE_CREATE)) != FILEIO_INVALID_HANDLE) {
		// Write one page at a time.
		while (length != 0) {
			legacy_u16 wrtlen = length > FILE_IO_PAGE_SIZE ? FILE_IO_PAGE_SIZE : length;

			if (fileio_write(src, wrtlen, 1, file) != wrtlen) {
				// Either DOS interrupt 33 function 64 set carry, or it wrote
				// fewer bytes than asked (`cmp ax, cx / jnz` -> `mov ax, 1`).
				retval = 1;
				break;
			}
			length -= wrtlen;
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
			if (length != 0) {
				src = (legacy_u8 *)src + wrtlen;
			}
#else
			src =
				dos_memory_make_pointer(dos_memory_pointer_segment(src) + FILE_IO_PAGE_SEGMENT_GAP,
										dos_memory_pointer_offset(src));
#endif
		}

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
		/* A save is complete only after close flushes all local bytes. Browser
		 * close also commits the asynchronous persistent copy. */
		if (fileio_close(file) != 0) {
			retval = 1;
#ifdef __EMSCRIPTEN__
			close_failed = 1;
#endif
		}
#else
		/* Preserve the original DOS behavior, which ignores close failures. */
		fileio_close(file);
#endif
		(void)fileio_error();

		if (retval == 0) {
#ifdef __ANDROID__
			/* The shared copy is optional; its failure must retain the local save. */
			android_saved_file_written(filename);
#endif
			return 0;
		}
	} else {
		// The create failed. The original enters the error tail with the DOS
		// error code in ax; the shim does not carry that out, so any non-zero
		// status stands in for it - every caller only tests for non-zero.
		retval = 1;
	}

	// loc_32570 closes the handle and unlinks the file on EVERY error, before
	// it ever looks at the flag, so a truncated file is never left on disk.
#ifdef __EMSCRIPTEN__
	/* A failed browser commit may leave the previous disk file intact. Keep
	 * that file and the new MEMFS bytes available for retry or export. */
	if (!close_failed) {
		fileio_remove(filename);
	}
#else
	fileio_remove(filename);
#endif

	if (!fatal) {
		fatal_error(file_write_error_format, filename);
	}

	return retval;
}

// Write given source buffer to file, handle errors as fatal.
legacy_s16 file_write_fatal(const legacy_s8 *filename, void far *src, legacy_u32 length)
{
	return file_write(filename, src, length, 1);
}

// Write given source buffer to file, returns a non-zero value on error.
legacy_s16 file_write_nofatal(const legacy_s8 *filename, void far *src, legacy_u32 length)
{
	return file_write(filename, src, length, 0);
}

// Sequential byte runs pass of run-length encoding.
//
// The original opens with a bail-out this has no equivalent for. It is not
// reproduced. With a single escape code the caller's
// subhdr->esc[RS_RLE_ESCSEQ_POS] would be reading past the table anyway, and
// the original's own bail-out returns with ax:dx undefined, which the caller
// then feeds into copy_paras_reverse - so neither side is coherent there. No
// Stunts resource carries esclen == 1.
legacy_u32 file_decomp_rle_seq(legacy_u8 huge *src, legacy_u8 huge *dst, legacy_u32 srclen,
							   legacy_u8 esc)
{
	legacy_u8 huge *srcend = src + srclen;
	legacy_u8 huge *dststart = dst;

	while (src < srcend) {
		legacy_u8 cur = *src++;

		// Byte sequence start.
		if (cur == esc) {
			legacy_u8 huge *seqstart = src;

			// Copy sequence.
			while ((cur = *src++) != esc) {
				*dst++ = cur;
			}

			// Number of repetitions, already written once.
			legacy_u8 rep = (*src++) - 1;
			legacy_u8 huge *seqend = src;

			// Copy remaining repetitions. The original is a do-while
			// (`dec dl / jnz short loc_30D5A`), so a stored count of 1
			// makes dl wrap and it copies 256 times where this copies none.
			// Counts of 2 and up agree exactly, and a count of 1 would mean
			// "repeat this run once", which the encoder has no reason to
			// emit.
			while (rep--) {
				src = seqstart;
				while (src < seqend - RLE_SEQUENCE_TRAILER_SIZE) {
					*dst++ = *src++;
				}
			}

			src = seqend;
		}
		// No sequence.
		else {
			*dst++ = cur;
		}
	}

	return dst - dststart;
}

// Single byte runs pass of run-length encoding.
legacy_u32 file_decomp_rle_single(legacy_u8 huge *src, legacy_u8 huge *dst, legacy_u32 len,
								  legacy_u8 *esclookup)
{
	legacy_u8 huge *dststart = dst;
	legacy_u8 huge *dstend = dst + len;

	legacy_u16 repw;
	legacy_u8 rep;
	while (dst < dstend) {
		legacy_u8 cur = *src++;

		if (esclookup[cur]) {
			switch (esclookup[cur]) {
				case RLE_BYTE_COUNT_CODE:
					rep = *src++;
					cur = *src++;

					while (rep--) {
						*dst++ = cur;
					}
					break;

				case RLE_WORD_COUNT_CODE:
					repw = *src++;
					repw |= *src++ << BYTE_SHIFT;
					cur = *src++;

					while (repw--) {
						*dst++ = cur;
					}
					break;

				default:
					rep = esclookup[cur] - 1;
					cur = *src++;

					while (rep--) {
						*dst++ = cur;
					}
					break;
			}
		} else {
			*dst++ = cur;
		}
	}

	return dst - dststart;
}

static void far *file_relocate_decomp_output(void far *destination, legacy_u16 decompparas,
											 legacy_u32 length)
{
	legacy_u16 paras = file_bytes_to_paras((legacy_s32)length);
	void far *source =
		dos_memory_make_pointer(decompparas - paras + dos_memory_pointer_segment(destination),
								dos_memory_pointer_offset(destination));
	copy_paras_reverse(dos_memory_pointer_segment(destination), dos_memory_pointer_segment(source),
					   paras);
	return source;
}

// Decompress run-length encoded sub-file.
legacy_u32 file_decomp_rle(legacy_u8 huge *src, legacy_u8 huge *dst, legacy_u16 decompparas)
{
	(void)decompparas;

	// Get decompressed size from header.
	legacy_u32 len = LEGACY_READ_U16_LE(src + COMPR_SIZE_LOW_OFFSET) |
					 ((legacy_u32)src[COMPR_SIZE_HIGH_OFFSET] << LEGACY_WORD_BITS);
	legacy_u8 huge *origsrc = src += COMPR_HEADER_SIZE;

	// Get source size and escape codes.
	legacy_u32 srclen =
		LEGACY_READ_U16_LE(src) | ((legacy_u32)src[COMPR_RLE_SIZE_HIGH_OFFSET] << LEGACY_WORD_BITS);
	legacy_u8 esclen = src[COMPR_RLE_ESCLEN_OFFSET];
	legacy_u8 huge *escapes = src + COMPR_RLE_HEADER_SIZE;

	// MSB denotes skipping the initial pass for byte sequence runs. Match the
	// original's strict compare against 128: exactly 128 still runs the pass,
	// using the byte in the sequence-escape slot even though the declared
	// escape-code count is zero.
	legacy_u16 skipseq = esclen > BYTE_HIGH_BIT;
	esclen &= (legacy_u8)~BYTE_HIGH_BIT;

	// Set pos to after escape codes.
	src = origsrc + COMPR_RLE_HEADER_SIZE + esclen;

	// Escape code lookup.
	legacy_u8 esclookup[RS_RLE_ESCLOOKUP_LEN];
	for (legacy_u16 i = 0; i < RS_RLE_ESCLOOKUP_LEN; ++i) {
		esclookup[i] = 0;
	}

	/* Keep this induction variable full width. GCC 6 for the 68000 can
	 * lower the 16-bit form to an address-register countdown whose upper
	 * bits retain part of escapes, preventing the loop from terminating. */
	for (legacy_u32 i = 0; i < esclen; ++i) {
		esclookup[escapes[i]] = i + 1;
	}

#ifdef RESTUNTS_AMIGA_TRACE
	fprintf(stdout,
			"Amiga decompress: RLE length=%" LEGACY_PRIu32 " source=%" LEGACY_PRIu32
			" escapes=%u skip=%u\n",
			len, srclen, (legacy_uint)esclen, (legacy_uint)skipseq);
	fflush(stdout);
#endif
	if (!skipseq) {
		AMIGA_DECOMP_TRACE("RLE sequence begin");
		legacy_u32 passlen = file_decomp_rle_seq(src, dst, srclen, escapes[RS_RLE_ESCSEQ_POS]);

		AMIGA_DECOMP_TRACE("RLE sequence complete");
		// In main decomp func:
		src = file_relocate_decomp_output(dst, decompparas, passlen);
	}

	// The original discards the single pass's count and returns the size out
	// of the compression header instead. The two only differ when the last
	// run overshoots - file_decomp_rle_single
	// stops on `dst < dstend` and a run that straddles the end writes past it,
	// so its count can exceed len. The declared length must still be returned:
	// file_decomp uses it to position the source for a following pass.
	AMIGA_DECOMP_TRACE("RLE single begin");
	file_decomp_rle_single(src, dst, len, esclookup);
	AMIGA_DECOMP_TRACE("RLE single complete");
	return len;
}

struct VLE_DECODER {
	legacy_u16 escape_alphabet_offsets[RS_VLE_ESC_LEN];
	legacy_u16 escape_code_limits[RS_VLE_ESC_LEN];
	legacy_u8 alphabet[RS_VLE_ALPH_LEN];
	legacy_u8 symbols[RS_VLE_ALPH_LEN];
	legacy_u8 widths[RS_VLE_ALPH_LEN];
	legacy_u8 huge *source;
	legacy_u16 additive;
	legacy_u16 current_word;
	legacy_u8 current_width;
	legacy_u8 current_symbol;
};

static legacy_u8 huge *file_vle_read_alphabet(struct VLE_DECODER *decoder, legacy_u8 huge *source,
											  legacy_u8 escape_length)
{
	// Generate the canonical code limits and alphabet offsets for each width.
	legacy_u16 index;
	legacy_u16 alphabet_length;
	legacy_u16 code_limit;
	for (index = 0, code_limit = 0, alphabet_length = 0; index < escape_length;
		 ++index, code_limit *= VLE_ALPHABET_BRANCH_FACTOR) {
		decoder->escape_alphabet_offsets[index] = alphabet_length - code_limit;
		legacy_u8 width_symbol_count = *source++;
		code_limit += width_symbol_count;
		alphabet_length += width_symbol_count;
		decoder->escape_code_limits[index] = code_limit;
	}
	for (index = 0; index < alphabet_length; ++index) {
		decoder->alphabet[index] = *source++;
	}
	return source;
}

static void file_vle_prepare_lookup(struct VLE_DECODER *decoder, legacy_u8 huge *source,
									legacy_u8 escape_length)
{
	// Expand short codes into byte lookup tables; pad longer prefixes as escapes.
	legacy_u16 width = 1;
	legacy_u16 width_distribution =
		(escape_length >= BYTE_BIT_COUNT ? BYTE_BIT_COUNT : escape_length);
	legacy_u8 symbol_count = RS_VLE_NUM_SYMB;
	legacy_u16 alphabet_index;
	legacy_u16 index;
	for (index = 0, alphabet_index = 0; width <= width_distribution; ++width, symbol_count >>= 1) {
		for (legacy_u8 symbol_width = *source++; symbol_width > 0;
			 --symbol_width, ++alphabet_index) {
			for (legacy_u8 symbols_left = symbol_count; symbols_left; --symbols_left, ++index) {
				decoder->symbols[index] = decoder->alphabet[alphabet_index];
				decoder->widths[index] = width;
			}
		}
	}
	for (; index < RS_VLE_ALPH_LEN; ++index) {
		decoder->widths[index] = RS_VLE_ESC_WIDTH;
	}
}

static void file_vle_write_symbol(struct VLE_DECODER *decoder, legacy_u8 huge *destination,
								  legacy_u8 symbol)
{
	if (decoder->additive) {
		decoder->current_symbol += symbol;
	} else {
		decoder->current_symbol = symbol;
	}
	*destination = decoder->current_symbol;
}

static legacy_u8 file_vle_read_byte(struct VLE_DECODER *decoder)
{
	return *decoder->source++;
}

static legacy_u8 file_vle_expand_escape(struct VLE_DECODER *decoder, legacy_u8 huge *destination)
{
	legacy_u8 code = decoder->current_word;
	decoder->current_word >>= BYTE_SHIFT;
	legacy_u16 index = VLE_EXTENDED_CODE_START_WIDTH;
	while (1) {
		if (!decoder->current_width) {
			code = file_vle_read_byte(decoder);
			decoder->current_width = BYTE_BIT_COUNT;
		}
		decoder->current_word =
			(decoder->current_word << 1) + ((code & BYTE_HIGH_BIT) == BYTE_HIGH_BIT);
		code <<= 1;
		--decoder->current_width;
		++index;
		if (decoder->current_word < decoder->escape_code_limits[index]) {
			decoder->current_word += decoder->escape_alphabet_offsets[index];
			file_vle_write_symbol(decoder, destination, decoder->alphabet[decoder->current_word]);
			break;
		}
	}
	decoder->current_word = (code << decoder->current_width) | file_vle_read_byte(decoder);
	legacy_u8 next_width = BYTE_BIT_COUNT - decoder->current_width;
	decoder->current_width = BYTE_BIT_COUNT;
	return next_width;
}

static legacy_u8 file_vle_decode_direct(struct VLE_DECODER *decoder, legacy_u8 huge *destination,
										legacy_u8 code, legacy_u8 next_width)
{
	file_vle_write_symbol(decoder, destination, decoder->symbols[code]);
	if (decoder->current_width < next_width) {
		decoder->current_word <<= decoder->current_width;
		next_width -= decoder->current_width;
		decoder->current_width = BYTE_BIT_COUNT;
		decoder->current_word |= file_vle_read_byte(decoder);
	}
	return next_width;
}

// Decompress variable-length encoded sub-file.
legacy_u32 file_decomp_vle(legacy_u8 huge *src, legacy_u8 huge *dst, legacy_u16 decompparas)
{
	(void)decompparas;
	legacy_u32 lenleft;
	legacy_u32 len = lenleft = LEGACY_READ_U16_LE(src + COMPR_SIZE_LOW_OFFSET) |
							   ((legacy_u32)src[COMPR_SIZE_HIGH_OFFSET] << LEGACY_WORD_BITS);
	src += COMPR_HEADER_SIZE;
	legacy_u8 escape_length = *src++;
	struct VLE_DECODER decoder;
	decoder.additive = (escape_length & BYTE_HIGH_BIT) == BYTE_HIGH_BIT;
	escape_length &= (legacy_u8)~BYTE_HIGH_BIT;
	// Save the code stream while revisiting the width data for direct lookups.
	decoder.source = file_vle_read_alphabet(&decoder, src, escape_length);
	file_vle_prepare_lookup(&decoder, src, escape_length);
	decoder.current_word =
		*decoder.source << BYTE_SHIFT | *(decoder.source + VLE_CODE_WORD_SECOND_BYTE_OFFSET);
	decoder.source += LEGACY_WORD_BYTES;
	decoder.current_width = BYTE_BIT_COUNT;
	decoder.current_symbol = 0;
	// The original writes len + 1 bytes but returns the declared header length.
	++lenleft;
	while (lenleft) {
		legacy_u8 code = decoder.current_word >> BYTE_SHIFT;
		legacy_u8 next_width = decoder.widths[code];
		if (next_width > BYTE_BIT_COUNT) {
			next_width = file_vle_expand_escape(&decoder, dst);
		} else {
			next_width = file_vle_decode_direct(&decoder, dst, code, next_width);
		}
		dst++;
		--lenleft;
		decoder.current_word <<= next_width;
		decoder.current_width -= next_width;
	}
	return len;
}

static legacy_s16 file_decomp_pass(legacy_u8 far *source, legacy_u8 far *destination,
								   legacy_u16 paragraphs, legacy_u32 *length)
{
	switch (*source) {
		case COMPRESSION_RLE_TYPE:
			*length = file_decomp_rle(source, destination, paragraphs);
			return 1;
		case COMPRESSION_VLE_TYPE:
			*length = file_decomp_vle(source, destination, paragraphs);
			return 1;
	}
	return 0;
}

/* Tail snapshots own their storage. Initialize even the workspace before placing
 * compressed input at its historical offset, so unwritten bytes are stable. */
static void far *file_alloc_tail_pages(const legacy_s8 *filename, legacy_u16 paragraphs,
									   legacy_u16 tail_bytes)
{
	legacy_u8 huge *result = mmgr_alloc_pages(filename, paragraphs);
	if (tail_bytes) {
		legacy_u32 length = (legacy_u32)paragraphs << DOS_PARAGRAPH_SHIFT;
		for (legacy_u32 index = 0; index < length; index++) {
			result[index] = 0;
		}
	}
	return result;
}

// Decompress file. Returns pointer to result, NULL or raises fatal error.
static void far *file_decomp_internal(const legacy_s8 *filename, legacy_s16 fatal,
									  legacy_u16 tail_bytes)
{
	// Ordinary cached resources have already discarded the decompression tail.
	legacy_u8 far *dst = tail_bytes ? 0 : mmgr_get_chunk_by_name(filename);
	if (dst) {
		return dst;
	}

	AMIGA_DECOMP_TRACE("header begin");
	legacy_u16 decompparas = file_decomp_paras(filename, fatal);
	AMIGA_DECOMP_TRACE("header complete");

	legacy_s16 err = 0;
	legacy_u32 passlen;
	if (decompparas) {
		// Allocate extra paragraphs for alphabet and escape tables
		// overhead used during decompression.
		legacy_u16 retained_paragraphs = decompparas + file_bytes_to_paras(tail_bytes);
		decompparas += COMPRESSION_WORKSPACE_PARAGRAPHS;
		legacy_u16 allocated_paragraphs =
			retained_paragraphs > decompparas ? retained_paragraphs : decompparas;
		AMIGA_DECOMP_TRACE("allocation begin");
		dst = file_alloc_tail_pages(filename, allocated_paragraphs, tail_bytes);
		AMIGA_DECOMP_TRACE("allocation complete");
		/* Extra retained storage must not move the compressed source or any
		 * intermediate pass: those offsets determine the legacy tail bytes. */

		AMIGA_DECOMP_TRACE("file size begin");
		legacy_u16 paras = file_paras(filename, fatal);
		AMIGA_DECOMP_TRACE("file size complete");
		if (paras) {
			legacy_u8 far *src =
				dos_memory_make_pointer(decompparas - paras + dos_memory_pointer_segment(dst),
										dos_memory_pointer_offset(dst));
			AMIGA_DECOMP_TRACE("read begin");
			src = file_read(filename, src, fatal);
			AMIGA_DECOMP_TRACE("read complete");
			if (src) {
				legacy_u8 passes = *src;

				// If the multi-pass flag is set, the first byte contains the
				// number of compression passes and the next three bytes holds
				// the final decompressed size.
				if (passes & BYTE_HIGH_BIT) {
					passes &= (legacy_u8)~BYTE_HIGH_BIT;
					src += COMPR_HEADER_SIZE;
				}
				// Flag not set, first byte is compression type of the first
				// and only pass.
				else {
					passes = 1;
				}

				// Decode all compression passes.
				while (!err && passes) {
#ifdef RESTUNTS_AMIGA_TRACE
					fprintf(stdout, "Amiga decompress: pass begin, remaining=%u codec=%u\n",
							(legacy_uint)passes, (legacy_uint)*src);
					fflush(stdout);
#endif
					if (file_decomp_pass(src, dst, decompparas, &passlen) == 0) {
						err = 1;
					}

					AMIGA_DECOMP_TRACE("pass complete");
					// Set source for next pass.
					if (!err && (--passes != 0)) {
						AMIGA_DECOMP_TRACE("relocate begin");
						src = file_relocate_decomp_output(dst, decompparas, passlen);
						AMIGA_DECOMP_TRACE("relocate complete");
					}
				}

				// Free unneeded overhead.
				if (!err) {
					AMIGA_DECOMP_TRACE("resize begin");
					mmgr_resize_memory(dos_memory_pointer_offset(dst),
									   dos_memory_pointer_segment(dst), retained_paragraphs);
					AMIGA_DECOMP_TRACE("resize complete");

					return dst;
				}
			}
		}
	}

	if (tail_bytes && dst) {
		mmgr_release(dst);
	}
	if (fatal) {
		fatal_error(invalid_pack_type_error_format, filename);
	}

	return 0;
}

void far *file_decomp(const legacy_s8 *filename, legacy_s16 fatal)
{
	return file_decomp_internal(filename, fatal, 0);
}

void far *file_decomp_fatal(const legacy_s8 *filename)
{
	return file_decomp(filename, 1);
}

void far *file_decomp_nofatal(const legacy_s8 *filename)
{
	return file_decomp(filename, 0);
}

// Allocates, reads and returns a pointer to the contents of a binary file
static void far *file_load_binary_internal(const legacy_s8 *filename, legacy_s16 fatal,
										   legacy_u16 tail_bytes)
{
	void far *memptr = tail_bytes ? 0 : mmgr_get_chunk_by_name(filename);
	if (!tail_bytes && dos_memory_pointer_segment(memptr) != 0) {
		return memptr;
	}

	legacy_s16 numparas = file_paras(filename, fatal);
	if (numparas == 0) {
		return 0;
	}
	numparas += file_bytes_to_paras(tail_bytes);
	memptr = file_alloc_tail_pages(filename, (legacy_u16)numparas, tail_bytes);
	void far *result = file_read(filename, memptr, fatal);
	if (tail_bytes && result == 0) {
		mmgr_release(memptr);
	}
	return result;
}

void far *file_load_binary(const legacy_s8 *filename, legacy_s16 fatal)
{
	return file_load_binary_internal(filename, fatal, 0);
}

void far *file_load_binary_nofatal(const legacy_s8 *filename)
{
	return file_load_binary(filename, 0);
}

void far *file_load_binary_fatal(const legacy_s8 *filename)
{
	return file_load_binary(filename, 1);
}

#ifndef RESTUNTS_HEADLESS
// One attempt at loading a resource of the given type; 0 means it failed.
static void far *file_try_load_resource(legacy_s16 resource_type, const legacy_s8 *filename)
{
	switch (resource_type) {
		case FILE_RESOURCE_BINARY_FATAL:
		case FILE_RESOURCE_BINARY_OPTIONAL:
			return file_load_binary_nofatal(filename);

		case FILE_RESOURCE_SHAPE2D:
			return file_load_shape2d_nofatal(filename);

		case FILE_RESOURCE_SHAPE2D_COLLECTION:
			return file_load_shape2d_res_nofatal(filename);

		case FILE_RESOURCE_SONG:
			return load_song_file(filename);

		case FILE_RESOURCE_VOICE:
			return load_voice_file(filename);

		case FILE_RESOURCE_SOUND_EFFECTS:
			return load_sfx_file(filename);

		case FILE_RESOURCE_COMPRESSED_OPTIONAL:
			return file_decomp_nofatal(filename);

		case FILE_RESOURCE_SHAPE2D_ALTERNATE:
			return file_load_shape2d_nofatal2(filename);
	}

	return 0;
}
#endif

#ifndef RESTUNTS_HEADLESS
static legacy_s16 file_show_resource_error(const legacy_s8 *filename)
{
#ifdef RESTUNTS_SDL3
	/* The DOS retry dialog has no filename; keep it available in the game log. */
	fprintf(stderr, "Cannot load game resource: %s\n", filename);
#else
	(void)filename;
#endif
	return show_disk_error_dialog();
}
#endif

void far *file_load_resource(legacy_s16 resource_type, const legacy_s8 *filename)
{
	void far *result;
#ifdef RESTUNTS_HEADLESS
	if (resource_type == FILE_RESOURCE_BINARY_FATAL ||
		resource_type == FILE_RESOURCE_BINARY_OPTIONAL) {
		result = file_load_binary_nofatal(filename);
	} else if (resource_type == FILE_RESOURCE_COMPRESSED_OPTIONAL) {
		result = file_decomp_nofatal(filename);
	} else {
		result = 0;
	}
	if (result == 0 && resource_type == FILE_RESOURCE_BINARY_FATAL) {
		fatal_error(headless_file_error, filename);
	}
	return result;
#else
	while (1) {
		result = file_try_load_resource(resource_type, filename);
		// Optional resource types report failure to the caller; every other
		// type shows a dialog and retries until the dialog gives up.
		if (result != 0 || resource_type == FILE_RESOURCE_BINARY_OPTIONAL ||
			resource_type == FILE_RESOURCE_COMPRESSED_OPTIONAL) {
			return result;
		}

		legacy_s16 dearesult = file_show_resource_error(filename);
		if (dearesult == FILE_ERROR_DIALOG_ABORT) {
			return 0;
		}
	}
#endif
}

static void far *file_load_suffixed_resource(legacy_s16 resource_type, const legacy_s8 *filename,
											 const legacy_s8 *suffix, legacy_s8 *name)
{
	strcpy(name, filename);
	strcat(name, suffix);
	return file_load_resource(resource_type, name);
}

void far *file_load_resfile(const legacy_s8 *filename)
{
	legacy_s8 name[RESOURCE_NAME_BUFFER_SIZE];
	void far *result;
#ifdef RESTUNTS_HEADLESS
	result = file_load_suffixed_resource(FILE_RESOURCE_BINARY_OPTIONAL, filename, ".res", name);
	if (result != 0) {
		return result;
	}
	result = file_load_suffixed_resource(FILE_RESOURCE_COMPRESSED_OPTIONAL, filename, ".pre", name);
	if (result == 0) {
		fatal_error(headless_file_error, filename);
	}
	return result;
#else
	while (1) {
		result = file_load_suffixed_resource(FILE_RESOURCE_BINARY_OPTIONAL, filename, ".res", name);
		if (result != 0) {
			return result;
		}

		result =
			file_load_suffixed_resource(FILE_RESOURCE_COMPRESSED_OPTIONAL, filename, ".pre", name);
		if (result != 0) {
			return result;
		}

		file_show_resource_error(filename);
	}
#endif
}

void far *file_load_resfile_with_tail(const legacy_s8 *filename, legacy_u16 tail_bytes)
{
	if (tail_bytes == 0) {
		return file_load_resfile(filename);
	}
	legacy_s8 name[RESOURCE_NAME_BUFFER_SIZE];
	for (;;) {
		strcpy(name, filename);
		strcat(name, ".res");
		void far *result = file_load_binary_internal(name, 0, tail_bytes);
		if (result != 0) {
			return result;
		}
		strcpy(name, filename);
		strcat(name, ".pre");
		result = file_decomp_internal(name, 0, tail_bytes);
		if (result != 0) {
			return result;
		}
#ifdef RESTUNTS_HEADLESS
		fatal_error(headless_file_error, filename);
		return 0;
#else
		file_show_resource_error(filename);
#endif
	}
}

/* Inspect the actual file length before trusting either resource header. The
 * legacy allocation sizes are paragraph-rounded and cannot detect truncation. */
static legacy_s32 file_game_resource_size(const legacy_s8 *filename, legacy_u16 packed)
{
	fileio_handle file = fileio_open(filename, DOS_FILE_OPEN_EXISTING);
	if (file == FILEIO_INVALID_HANDLE) {
		return -1;
	}
	legacy_s32 length = 0;
	if (fileio_seek(file, 0, DOS_FILE_SEEK_END) == 0) {
		length = fileio_tell(file);
	}
	const legacy_u32 maximum_size = ((legacy_u32)LEGACY_S16_MAX - COMPRESSION_WORKSPACE_PARAGRAPHS)
									<< DOS_PARAGRAPH_SHIFT;
	if (length < (legacy_s32)RESOURCE_FILE_DIRECTORY_OFFSET || (legacy_u32)length > maximum_size) {
		length = 0;
	} else if (packed) {
		legacy_u8 header[COMPR_HEADER_SIZE];
		if (fileio_seek(file, 0, DOS_FILE_SEEK_BEGIN) != 0 ||
			fileio_read(header, sizeof(header), 1, file) != sizeof(header)) {
			length = 0;
		} else {
			legacy_u32 unpacked_size =
				LEGACY_READ_U16_LE(header + COMPR_SIZE_LOW_OFFSET) |
				((legacy_u32)header[COMPR_SIZE_HIGH_OFFSET] << LEGACY_WORD_BITS);
			legacy_u8 compression = header[0];
			legacy_u16 valid_compression =
				compression == COMPRESSION_RLE_TYPE || compression == COMPRESSION_VLE_TYPE ||
				((compression & BYTE_HIGH_BIT) != 0 && (compression & (BYTE_HIGH_BIT - 1U)) != 0);
			if (valid_compression == 0 || unpacked_size < RESOURCE_FILE_DIRECTORY_OFFSET ||
				unpacked_size > maximum_size ||
				file_bytes_to_paras(length) > file_bytes_to_paras((legacy_s32)unpacked_size) +
												  COMPRESSION_WORKSPACE_PARAGRAPHS) {
				length = 0;
			} else {
				length = (legacy_s32)unpacked_size;
			}
		}
	}
	fileio_close(file);
	return fileio_error() ? 0 : length;
}

legacy_u16 file_game_version_matches(void)
{
	static const legacy_s8 unpacked_name[] = "misc.res";
	static const legacy_s8 packed_name[] = "misc.pre";
	static const legacy_s8 version_id[] = "gver";
	static const legacy_s8 expected_version[] = "Version 1.1 (Feb 12 1991)";
	legacy_s32 length = file_game_resource_size(unpacked_name, 0);
	legacy_u16 packed = length < 0;
	if (packed) {
		length = file_game_resource_size(packed_name, 1);
	}
	if (length <= 0) {
		return 0;
	}
	legacy_u8 far *resource =
		packed ? file_decomp_nofatal(packed_name) : file_load_binary_nofatal(unpacked_name);
	if (resource == 0) {
		return 0;
	}
	legacy_u16 matches =
		resource_text_equals(resource, (legacy_u32)length, version_id, expected_version);
	/* This startup probe must not keep an extra live or cached allocation. */
	mmgr_release(resource);
	return matches;
}

void unload_resource(void far *resptr)
{
	mmgr_free(resptr);
}

#ifndef RESTUNTS_HEADLESS
void far *file_load_3dres(const legacy_s8 *filename)
{
	legacy_s8 name[RESOURCE_NAME_BUFFER_SIZE];
	while (1) {
		void far *result =
			file_load_suffixed_resource(FILE_RESOURCE_COMPRESSED_OPTIONAL, filename, ".p3s", name);
		if (result != 0) {
			return result;
		}

		result = file_load_suffixed_resource(FILE_RESOURCE_BINARY_OPTIONAL, filename, ".3sh", name);
		if (result != 0) {
			return result;
		}

		file_show_resource_error(filename);
	}
}

void far *file_prepare_audiores(const legacy_s8 *songfile, const legacy_s8 *voicefile,
								const legacy_s8 *name)
{
	voicefileptr = file_load_resource(FILE_RESOURCE_VOICE, voicefile);
	songfileptr = file_load_resource(FILE_RESOURCE_SONG, songfile);
	void far *audiores = init_audio_resources(songfileptr, voicefileptr, name);
	is_audioloaded = 1;
	return audiores;
}

void file_load_audiores(const legacy_s8 *songfile, const legacy_s8 *voicefile,
						const legacy_s8 *name)
{
	load_audio_finalize(file_prepare_audiores(songfile, voicefile, name));
}
#endif

static void file_set_replay_filename(const legacy_s8 *path)
{
	const legacy_s8 *name = path;
	const legacy_s8 *extension = 0;
	for (const legacy_s8 *character = path; *character != 0; character++) {
		if (*character == '\\' || *character == '/' || *character == ':') {
			name = character + 1;
			extension = 0;
		} else if (*character == '.') {
			extension = character;
		}
	}

	legacy_u16 length = 0;
	while (length + 1U < REPLAY_FILENAME_SIZE && name[length] != 0 && name + length != extension) {
		replay_filename[length] = name[length];
		length++;
	}
	replay_filename[length] = 0;
}

legacy_s16 file_load_replay(const legacy_s8 *dir, const legacy_s8 *name)
{
	return file_load_replay_format(dir, name, ".rpl");
}

legacy_s16 file_load_replay_format(const legacy_s8 *dir, const legacy_s8 *name,
								   const legacy_s8 *extension)
{
#if defined(RESTUNTS_SDL3) && !defined(RESTUNTS_HEADLESS)
	if (strcmp(extension, ".csrpl") == 0) {
		file_build_path(dir, name, extension, g_path_buf);
		g_is_busy = 1;
		legacy_s16 result = (legacy_s16)extended_file_load_replay(g_path_buf);
		if (result == 0) {
			file_set_replay_filename(g_path_buf);
		}
		g_is_busy = 0;
		return result;
	}
	extended_track_unload();
	extended_replay_runtime_clear();
#else
	(void)extension;
#endif
	file_build_path(dir, name, ".rpl", g_path_buf);

	g_is_busy = 1;
	file_read_fatal(g_path_buf, replay_header_buffer);
	replay_gameinfo_decode(&gameconfig, (const legacy_u8 far *)replay_header_buffer);
	file_set_replay_filename(g_path_buf);
	g_is_busy = 0;
	return 0;
}

legacy_s16 file_write_replay(const legacy_s8 *filename)
{
#if defined(RESTUNTS_SDL3) && !defined(RESTUNTS_HEADLESS)
	if (extended_replay_is_active()) {
		g_is_busy = 1;
		legacy_s16 result = (legacy_s16)extended_file_write_replay(filename);
		if (result == 0) {
			file_set_replay_filename(filename);
		}
		g_is_busy = 0;
		return result;
	}
#endif
	replay_gameinfo_encode((legacy_u8 far *)replay_header_buffer, &gameconfig);

	g_is_busy = 1;
	legacy_s16 ret = file_write_fatal(filename, replay_header_buffer,
									  replay_file_size(gameconfig.game_recordedframes));
	if (ret == 0) {
		file_set_replay_filename(filename);
	}
	g_is_busy = 0;

	return ret;
}
