#ifndef RESTUNTS_EXTENDED_CSTRK_HORIZON_H
#define RESTUNTS_EXTENDED_CSTRK_HORIZON_H

#include "cstrk.h"

/* Validate the bounded in-memory filename and its revision-specific admission. */
legacy_int cstrk_horizon_valid(const struct CSTRK_TRACK *track, legacy_char *error,
							   size_t error_size);

#endif
