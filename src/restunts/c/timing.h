#ifndef RESTUNTS_TIMING_H
#define RESTUNTS_TIMING_H

#include "legacy.h"

void timer_reg_callback(void(far *callback)(void));
void timer_remove_callback(void(far *callback)(void));
legacy_u32 timer_get_delta_alt(void);
legacy_u32 timer_set_deadline(legacy_u32 ticks);
legacy_u32 timer_wait_for_deadline(void);
legacy_s16 timer_deadline_reached(void);
legacy_u32 timer_wait_ticks(legacy_u32 ticks);
legacy_u32 slow_timer_set_deadline(legacy_u32 ticks);
legacy_s16 slow_timer_deadline_reached(void);
legacy_u32 slow_timer_wait_ticks(legacy_u32 ticks);

#endif
