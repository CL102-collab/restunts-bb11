#ifndef RESTUNTS_FILEIO_H
#define RESTUNTS_FILEIO_H

#include "legacy.h"

enum FILE_RESOURCE_TYPE {
	FILE_RESOURCE_BINARY_FATAL = 0,
	FILE_RESOURCE_BINARY_OPTIONAL = 1,
	FILE_RESOURCE_SHAPE2D = 2,
	FILE_RESOURCE_SHAPE2D_COLLECTION = 3,
	FILE_RESOURCE_SONG = 4,
	FILE_RESOURCE_VOICE = 5,
	FILE_RESOURCE_SOUND_EFFECTS = 6,
	FILE_RESOURCE_COMPRESSED_OPTIONAL = 7,
	FILE_RESOURCE_SHAPE2D_ALTERNATE = 8
};

#ifdef __cplusplus
extern "C" {
#endif

#define FILE_CAR_ID_LENGTH 4U
#define FILE_CAR_ID_BUFFER_SIZE (FILE_CAR_ID_LENGTH + 1U)
#ifdef RESTUNTS_DOS16
#define FILE_CAR_PATH_SIZE 128U
#else
#define FILE_CAR_PATH_SIZE 1024U
#endif

/* Preserve the executable/asset directory before selecting the game-data dir.
 * Loose game data takes priority over cars/ and bundled replacement assets. */
void file_car_set_base(const legacy_s8 *directory);
legacy_u16 file_find_car_ids(legacy_s8 car_ids[][FILE_CAR_ID_BUFFER_SIZE], legacy_u16 capacity);
/* Open an existing resource, including the car search directories. */
legacy_u16 file_open_resource(const legacy_s8 *filename);

const legacy_s8 *file_find(const legacy_s8 *query);
const legacy_s8 *file_find_next(void);
const legacy_s8 *file_find_next_alt(void);

void file_build_path(const legacy_s8 *dir, const legacy_s8 *name, const legacy_s8 *ext,
					 legacy_s8 *dst);
const legacy_s8 *file_combine_and_find(const legacy_s8 *dir, const legacy_s8 *name,
									   const legacy_s8 *ext);

legacy_u16 file_paras(const legacy_s8 *filename, legacy_s16 fatal);
legacy_u16 file_paras_fatal(const legacy_s8 *filename);
legacy_u16 file_paras_nofatal(const legacy_s8 *filename);

legacy_u16 file_decomp_paras(const legacy_s8 *filename, legacy_s16 fatal);
legacy_u16 file_decomp_paras_fatal(const legacy_s8 *filename);
legacy_u16 file_decomp_paras_nofatal(const legacy_s8 *filename);

void far *file_read(const legacy_s8 *filename, void far *dst, legacy_s16 fatal);
void far *file_read_fatal(const legacy_s8 *filename, void far *dst);
void far *file_read_nofatal(const legacy_s8 *filename, void far *dst);

legacy_s16 file_write(const legacy_s8 *filename, void far *src, legacy_u32 length,
					  legacy_s16 fatal);
legacy_s16 file_write_fatal(const legacy_s8 *filename, void far *src, legacy_u32 length);
legacy_s16 file_write_nofatal(const legacy_s8 *filename, void far *src, legacy_u32 length);

void far *file_decomp(const legacy_s8 *filename, legacy_s16 fatal);
void far *file_decomp_fatal(const legacy_s8 *filename);
void far *file_decomp_nofatal(const legacy_s8 *filename);

void far *file_load_binary(const legacy_s8 *filename, legacy_s16 fatal);
void far *file_load_binary_nofatal(const legacy_s8 *filename);
void far *file_load_binary_fatal(const legacy_s8 *filename);

/* Validate the original game-data version without requiring unmodified assets. */
legacy_u16 file_game_version_matches(void);
void far *file_load_resfile(const legacy_s8 *filename);
/* With a nonzero tail, load a fresh snapshot whose allocation retains at least
 * tail_bytes after the rounded resource length, including legacy decoder
 * residue. Unwritten storage is zero. Release with mmgr_release, not caching
 * unload_resource; ordinary cached resources do not preserve this tail. */
void far *file_load_resfile_with_tail(const legacy_s8 *filename, legacy_u16 tail_bytes);
void far *file_load_resource(legacy_s16 resource_type, const legacy_s8 *filename);
void unload_resource(void far *resptr);
/* Load and map a song without starting it; pass the result to load_audio_finalize
 * once other resources are ready. audio_unload releases the prepared resources. */
void far *file_prepare_audiores(const legacy_s8 *songfile, const legacy_s8 *voicefile,
								const legacy_s8 *name);
void file_load_audiores(const legacy_s8 *songfile, const legacy_s8 *voicefile,
						const legacy_s8 *name);
void far *file_load_3dres(const legacy_s8 *filename);

legacy_s16 file_load_replay(const legacy_s8 *dir, const legacy_s8 *name);
legacy_s16 file_write_replay(const legacy_s8 *filename);

#ifdef __cplusplus
}
#endif

#endif
