#ifndef RESTUNTS_GHOST_LINE_INTERNAL_H
#define RESTUNTS_GHOST_LINE_INTERNAL_H
#include "ghost.h"
legacy_s16 ghost_seek(legacy_u16 file, legacy_s32 offset);
void ghost_line_clear(void);
void ghost_line_finish(void);
void ghost_line_record(const struct CARSTATE *car);
#endif
