#ifndef RESTUNTS_EXTENDED_STATECRS_H
#define RESTUNTS_EXTENDED_STATECRS_H

#include "../legacy.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
void extended_crash_record_end(legacy_s16 car_index);
void extended_crash_record_finish(legacy_s16 car_index);
legacy_s16 extended_crash_opponent_finish_time(legacy_s16 finish);
#ifndef RESTUNTS_HEADLESS
void extended_crash_capture_stats(void);
#endif
#endif

#endif
