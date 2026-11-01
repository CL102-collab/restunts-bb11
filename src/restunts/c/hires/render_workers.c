#include "render_workers.h"

#if !defined(__DJGPP__) && !defined(__EMSCRIPTEN__)
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <errno.h>
#include <stdlib.h>

/* Serial rendering avoids worker scheduling overhead by default. Explicit
 * requests and automatic CPU detection share a bounded worker pool. */
#define RENDER_DEFAULT_WORKERS 0
#define RENDER_MAX_WORKERS 7
#define RENDER_WORKERS_AUTO "auto"

struct RENDER_WORKER {
	SDL_Thread *thread;
	SDL_Semaphore *start;
};

static struct RENDER_WORKER hires_workers[RENDER_MAX_WORKERS];
static SDL_Semaphore *hires_completed;
static SDL_AtomicInt hires_next_job;
static legacy_s32 hires_initialized;
static legacy_s32 hires_worker_count;
static legacy_s32 hires_stopping;
static legacy_s32 hires_job_count;
static void (*job_function)(void *, legacy_s32);
static void *hires_job_context;

static void hires_run_jobs(void)
{
	for (;;) {
		legacy_s32 index = SDL_AddAtomicInt(&hires_next_job, 1);
		if (index >= hires_job_count) {
			return;
		}
		job_function(hires_job_context, index);
	}
}

static legacy_int SDLCALL hires_worker_main(void *argument)
{
	struct RENDER_WORKER *worker = argument;
	for (;;) {
		SDL_WaitSemaphore(worker->start);
		if (hires_stopping) {
			return 0;
		}
		hires_run_jobs();
		SDL_SignalSemaphore(hires_completed);
	}
}

static legacy_s32 hires_requested_workers(void)
{
	const legacy_char *setting = SDL_getenv("RESTUNTS_RENDER_WORKERS");
	if (setting == NULL || *setting == 0) {
		return RENDER_DEFAULT_WORKERS;
	}
	if (SDL_strcmp(setting, RENDER_WORKERS_AUTO) == 0) {
		legacy_s32 cores = SDL_GetNumLogicalCPUCores();
		if (cores <= 1) {
			return RENDER_DEFAULT_WORKERS;
		}
		return cores - 1 > RENDER_MAX_WORKERS ? RENDER_MAX_WORKERS : cores - 1;
	}
	legacy_char *end;
	errno = 0;
	legacy_s64 value = strtol(setting, &end, 10);
	if (errno != 0 || end == setting || *end != 0 || value < 0) {
		return RENDER_DEFAULT_WORKERS;
	}
	return value > RENDER_MAX_WORKERS ? RENDER_MAX_WORKERS : (legacy_s32)value;
}

static void hires_initialize_workers(void)
{
	if (hires_initialized) {
		return;
	}
	hires_initialized = 1;
	legacy_s32 requested = hires_requested_workers();
	if (requested <= 0) {
		return;
	}
	hires_completed = SDL_CreateSemaphore(0);
	if (hires_completed == NULL) {
		return;
	}
	for (legacy_s32 index = 0; index < requested; index++) {
		struct RENDER_WORKER *worker = &hires_workers[index];
		worker->start = SDL_CreateSemaphore(0);
		if (worker->start == NULL) {
			break;
		}
		worker->thread = SDL_CreateThread(hires_worker_main, "renderer", worker);
		if (worker->thread == NULL) {
			SDL_DestroySemaphore(worker->start);
			worker->start = NULL;
			break;
		}
		hires_worker_count++;
	}
}

legacy_s32 hires_render_workers_count(void)
{
	hires_initialize_workers();
	return hires_worker_count;
}

void hires_render_workers_shutdown(void)
{
	hires_stopping = 1;
	for (legacy_s32 index = 0; index < hires_worker_count; index++) {
		SDL_SignalSemaphore(hires_workers[index].start);
	}
	for (legacy_s32 index = 0; index < hires_worker_count; index++) {
		SDL_WaitThread(hires_workers[index].thread, NULL);
		SDL_DestroySemaphore(hires_workers[index].start);
		hires_workers[index].thread = NULL;
		hires_workers[index].start = NULL;
	}
	if (hires_completed != NULL) {
		SDL_DestroySemaphore(hires_completed);
	}
	hires_completed = NULL;
	hires_worker_count = 0;
	hires_initialized = 0;
	hires_stopping = 0;
}

legacy_s32 hires_render_workers_run(legacy_s32 count, void (*job)(void *, legacy_s32),
									void *context)
{
	if (count <= 1) {
		if (count == 1) {
			job(context, 0);
		}
		return 0;
	}
	hires_initialize_workers();
	legacy_s32 used = hires_worker_count < count ? hires_worker_count : count - 1;
	if (used == 0) {
		for (legacy_s32 index = 0; index < count; index++) {
			job(context, index);
		}
		return 0;
	}
	hires_job_count = count;
	job_function = job;
	hires_job_context = context;
	SDL_SetAtomicInt(&hires_next_job, 0);
	for (legacy_s32 index = 0; index < used; index++) {
		SDL_SignalSemaphore(hires_workers[index].start);
	}
	hires_run_jobs();
	for (legacy_s32 index = 0; index < used; index++) {
		SDL_WaitSemaphore(hires_completed);
	}
	return used;
}
#else
legacy_s32 hires_render_workers_count(void)
{
	return 0;
}

legacy_s32 hires_render_workers_run(legacy_s32 count, void (*job)(void *, legacy_s32),
									void *context)
{
	for (legacy_s32 index = 0; index < count; index++) {
		job(context, index);
	}
	return 0;
}

void hires_render_workers_shutdown(void)
{
}
#endif
