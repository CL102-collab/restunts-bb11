#ifndef RESTUNTS_ASSET_PATH_H
#define RESTUNTS_ASSET_PATH_H

#include "legacy.h"
#include <stdio.h>

/* Capture the executable directory before --data-dir changes the working
 * directory. The returned path ends in a directory separator and is borrowed. */
void asset_path_initialize(const legacy_char *executable);
const legacy_char *asset_path_base(void);

/* Open a UTF-8 asset path with a standard stdio mode. The caller owns the
 * returned stream and closes it with fclose(), or transfers that ownership. */
FILE *asset_path_open_file(const legacy_char *path, const legacy_char *mode);

#endif
