#ifndef RESTUNTS_EXTENDED_FILE_H
#define RESTUNTS_EXTENDED_FILE_H

#include "legacy.h"

/* SDL3 format I/O returns zero on success, like the legacy file routines.
 * Errors leave the current track intact and can be shown by the caller. */
int extended_file_load_track(const legacy_s8 *path);
int extended_file_load_replay(const legacy_s8 *path);
int extended_file_write_replay(const legacy_s8 *path);
const legacy_s8 *extended_file_error(void);

#endif
