#ifndef RESTUNTS_EXTENDED_FILEIO_H
#define RESTUNTS_EXTENDED_FILEIO_H
#include "legacy.h"
void file_set_replay_filename(const legacy_s8 *path);
#ifdef RESTUNTS_SDL3
legacy_s16 extended_fileio_load_replay(const legacy_s8 *directory, const legacy_s8 *name,
									   const legacy_s8 *extension, legacy_s16 *result);
legacy_s16 extended_fileio_write_replay(const legacy_s8 *filename, legacy_s16 *result);
#endif
#endif
