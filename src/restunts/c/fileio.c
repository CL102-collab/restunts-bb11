#include "extended/extended_fileio.h"
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

#ifdef RESTUNTS_HEADLESS
const legacy_s8 headless_file_error[] = "File error: %s";
const legacy_s8 headless_write_error[] = "File write error: %s";
const legacy_s8 headless_size_error[] = "File size error: %s";
const legacy_s8 headless_pack_error[] = "Invalid packed resource: %s";
#endif

static fileio_handle fileio_open(const legacy_s8 *path, legacy_s16 create)
{
	return create == DOS_FILE_OPEN_EXISTING ? file_open_resource(path)
											: dos_file_open(path, create);
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
legacy_u16 file_bytes_to_paras(legacy_s32 length)
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

void file_set_replay_filename(const legacy_s8 *path)
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
	legacy_s16 result;
	if (extended_fileio_load_replay(dir, name, extension, &result)) {
		return result;
	}
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
	legacy_s16 result;
	if (extended_fileio_write_replay(filename, &result)) {
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
