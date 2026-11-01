#ifndef RESTUNTS_RENDER_WORKERS_H
#define RESTUNTS_RENDER_WORKERS_H

#include "../legacy.h"

/* Synchronous CPU jobs. The caller also works; no job may call SDL video APIs.
 * Returns the number of background workers used. DOS and browsers run serially. */
legacy_s32 hires_render_workers_run(legacy_s32 count, void (*job)(void *, legacy_s32),
									void *context);
legacy_s32 hires_render_workers_count(void);
void hires_render_workers_shutdown(void);

#endif
