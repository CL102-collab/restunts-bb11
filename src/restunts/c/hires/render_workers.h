#ifndef RESTUNTS_RENDER_WORKERS_H
#define RESTUNTS_RENDER_WORKERS_H

#include "../legacy.h"

#if !defined(__DJGPP__) && (!defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__))
#if defined(_MSC_VER)
#define RESTUNTS_RENDER_THREAD_LOCAL __declspec(thread)
#else
#define RESTUNTS_RENDER_THREAD_LOCAL __thread
#endif
#else
#define RESTUNTS_RENDER_THREAD_LOCAL
#endif

/* Synchronous CPU jobs. The caller also works; no job may call SDL video APIs.
 * Returns the number of background workers used. DOS and nonthreaded browsers
 * run serially. The startup option defaults to off, including environment overrides. */
void hires_render_workers_set_enabled(legacy_u8 enabled);
legacy_s32 hires_render_workers_run(legacy_s32 count, void (*job)(void *, legacy_s32),
									void *context);
legacy_s32 hires_render_workers_count(void);
void hires_render_workers_shutdown(void);

#endif
