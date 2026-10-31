#ifndef RESTUNTS_EXTENDED_GAMESTEP_H
#define RESTUNTS_EXTENDED_GAMESTEP_H
#include "../legacy.h"
legacy_s16 extended_gamestep_input(legacy_s8 *input, legacy_s16 store_checkpoints);
legacy_s16 extended_gamestep_legacy_checkpoints(void);
void extended_gamestep_after_step(void);
#endif
