#ifndef RESTUNTS_SDL3_CAR_ZIP_H
#define RESTUNTS_SDL3_CAR_ZIP_H

#include <stdio.h>
#include "../../c/legacy.h"

/* Read one top-level DOS resource without extracting it. The caller owns the
 * archive and frees the returned buffer. NULL reports an absent/invalid member. */
legacy_u8 *sdl3_car_zip_read(FILE *archive, const legacy_s8 *member, legacy_u32 *length);

#endif
