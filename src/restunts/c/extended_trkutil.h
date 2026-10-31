#ifndef RESTUNTS_EXTENDED_TRKUTIL_H
#define RESTUNTS_EXTENDED_TRKUTIL_H
#include "legacy.h"
legacy_s16 extended_trkutil_row_position(legacy_u16 row, legacy_s16 *position);
legacy_s16 extended_trkutil_column_position(legacy_u16 column, legacy_s16 *position);
legacy_s16 extended_trkutil_row_table_count(legacy_s16 width, legacy_s16 height);
legacy_u8 extended_trkutil_horizon(legacy_u8 legacy_horizon);
#endif
