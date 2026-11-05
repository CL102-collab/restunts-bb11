#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "../c/legacy.h"

enum {
	TEST_MAX_WORKERS = 63,
	TEST_RESOURCE_BUDGET = TEST_MAX_WORKERS + 1,
	TEST_JOB_COUNT = 97,
	TEST_JOB_PASSES = 3,
	TEST_JOB_DELAY_MS = 1,
	TEST_FAILURE_LIMIT = 3,
	TEST_RAPID_CORES = 4,
	TEST_RAPID_CONTEXTS = 2,
	TEST_RAPID_PASSES = 128
};

static legacy_int detected_cores;
static legacy_s32 thread_budget;
static legacy_s32 semaphore_budget;
static legacy_s32 created_threads;
static legacy_s32 created_semaphores;
static legacy_s32 live_semaphores;

static legacy_int detect_cores(void)
{
	return detected_cores;
}

static SDL_Thread *create_thread(SDL_ThreadFunction function, const legacy_char *name,
								 void *argument)
{
	if (thread_budget == 0) {
		return NULL;
	}
	thread_budget--;
	SDL_Thread *thread = SDL_CreateThread(function, name, argument);
	if (thread != NULL) {
		created_threads++;
	}
	return thread;
}

static SDL_Semaphore *create_semaphore(Uint32 initial_value)
{
	if (semaphore_budget == 0) {
		return NULL;
	}
	semaphore_budget--;
	SDL_Semaphore *semaphore = SDL_CreateSemaphore(initial_value);
	if (semaphore != NULL) {
		created_semaphores++;
		live_semaphores++;
	}
	return semaphore;
}

static void destroy_semaphore(SDL_Semaphore *semaphore)
{
	assert(semaphore != NULL && live_semaphores > 0);
	SDL_DestroySemaphore(semaphore);
	live_semaphores--;
}

#undef SDL_CreateThread
#define SDL_CreateThread create_thread
#define SDL_CreateSemaphore create_semaphore
#define SDL_DestroySemaphore destroy_semaphore
#define SDL_GetNumLogicalCPUCores detect_cores
#include "../c/hires/render_workers.c"

struct JOB_RESULTS {
	legacy_s32 visits[TEST_JOB_COUNT];
	SDL_ThreadID threads[TEST_JOB_COUNT];
};

static void record_job(void *argument, legacy_s32 index)
{
	struct JOB_RESULTS *results = argument;
	assert(index >= 0 && index < TEST_JOB_COUNT);
	results->visits[index]++;
	results->threads[index] = SDL_GetCurrentThreadID();
	SDL_Delay(TEST_JOB_DELAY_MS);
}

static void check_jobs(legacy_s32 expected_workers)
{
	for (legacy_s32 pass = 0; pass < TEST_JOB_PASSES; pass++) {
		struct JOB_RESULTS results = {0};
		assert(hires_render_workers_run(TEST_JOB_COUNT, record_job, &results) == expected_workers);
		legacy_s32 background_jobs = 0;
		for (legacy_s32 index = 0; index < TEST_JOB_COUNT; index++) {
			assert(results.visits[index] == 1);
			background_jobs += results.threads[index] != SDL_GetCurrentThreadID();
		}
		assert((background_jobs != 0) == (expected_workers != 0));
	}
}

static void configure_workers(const legacy_char *setting)
{
	hires_render_workers_shutdown();
	hires_render_workers_shutdown();
	assert(live_semaphores == 0);
	if (setting == NULL) {
		assert(SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "RESTUNTS_RENDER_WORKERS"));
	} else {
		assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "RESTUNTS_RENDER_WORKERS", setting,
										  true));
	}
	hires_render_workers_set_enabled(1);
	thread_budget = TEST_RESOURCE_BUDGET;
	semaphore_budget = TEST_RESOURCE_BUDGET;
	created_threads = 0;
	created_semaphores = 0;
}

struct PHASE_RESULTS {
	legacy_s32 visits[TEST_JOB_COUNT], values[TEST_JOB_COUNT];
	legacy_s32 seed;
};

static void record_forward_phase(void *argument, legacy_s32 index)
{
	struct PHASE_RESULTS *results = argument;
	assert(index >= 0 && index < TEST_JOB_COUNT);
	results->visits[index]++;
	results->values[index] = results->seed + index;
}

static void record_reverse_phase(void *argument, legacy_s32 index)
{
	struct PHASE_RESULTS *results = argument;
	assert(index >= 0 && index < TEST_JOB_COUNT);
	results->visits[index]++;
	results->values[index] = results->seed - index;
}

/* Back-to-back dispatches publish changing callbacks and contexts, then expose
 * every job's writes immediately after the synchronous join, without delays. */
static void check_rapid_phases(void)
{
	configure_workers("auto");
	detected_cores = TEST_RAPID_CORES;
	legacy_s32 workers = hires_render_workers_count();
	assert(workers == TEST_RAPID_CORES - 1);
	const legacy_s32 counts[] = {
		0, 1, 2, workers, workers + 1, workers + 2, TEST_JOB_COUNT - 1, TEST_JOB_COUNT};
	void (*functions[])(void *, legacy_s32) = {record_forward_phase, record_reverse_phase};
	struct PHASE_RESULTS contexts[TEST_RAPID_CONTEXTS] = {0};
	for (legacy_u32 pass = 0; pass < TEST_RAPID_PASSES; pass++) {
		struct PHASE_RESULTS *results = &contexts[pass % SDL_arraysize(contexts)];
		memset(results, 0, sizeof(*results));
		results->seed = (legacy_s32)pass * TEST_JOB_COUNT;
		legacy_u32 function = (pass / SDL_arraysize(contexts)) % SDL_arraysize(functions);
		legacy_s32 count = counts[pass % SDL_arraysize(counts)];
		legacy_s32 used = count <= 1 ? 0 : count <= workers ? count - 1 : workers;
		assert(hires_render_workers_run(count, functions[function], results) == used);
		for (legacy_s32 index = 0; index < TEST_JOB_COUNT; index++) {
			assert(results->visits[index] == (index < count));
			legacy_s32 expected = index < count ? results->seed + (function ? -index : index) : 0;
			assert(results->values[index] == expected);
		}
	}
}

legacy_int main(void)
{
	assert(SDL_Init(0));
	const legacy_char *defaults[] = {NULL, ""};
	const legacy_int cores[] = {INT_MIN, 0, 1, 2, 4, 64, INT_MAX};
	const legacy_s32 expected[] = {0, 0, 0, 1, 3, TEST_MAX_WORKERS, TEST_MAX_WORKERS};
	for (legacy_u32 setting = 0; setting < sizeof(defaults) / sizeof(defaults[0]); setting++) {
		for (legacy_u32 index = 0; index < sizeof(cores) / sizeof(cores[0]); index++) {
			configure_workers(defaults[setting]);
			hires_render_workers_set_enabled(0);
			detected_cores = cores[index];
			assert(hires_render_workers_count() == 0);
			check_jobs(0);
			assert(created_threads == 0 && created_semaphores == 0);
		}
	}
	/* Disabled always stays serial, even when a diagnostic override requests jobs. */
	configure_workers("auto");
	hires_render_workers_set_enabled(0);
	detected_cores = TEST_RESOURCE_BUDGET;
	assert(hires_render_workers_count() == 0);
	check_jobs(0);
	assert(created_threads == 0 && created_semaphores == 0);
	for (legacy_u32 index = 0; index < sizeof(cores) / sizeof(cores[0]); index++) {
		configure_workers("auto");
		detected_cores = cores[index];
		assert(hires_render_workers_count() == expected[index]);
		check_jobs(expected[index]);
	}
	configure_workers(NULL);
	detected_cores = TEST_FAILURE_LIMIT;
	check_jobs(TEST_FAILURE_LIMIT - 1);
	hires_render_workers_set_enabled(0);
	assert(live_semaphores == 0);
	check_jobs(0);
	hires_render_workers_set_enabled(1);
	check_jobs(TEST_FAILURE_LIMIT - 1);
	/* Resource exhaustion keeps the workers already created, or runs serially. */
	for (legacy_s32 available = 0; available < TEST_FAILURE_LIMIT; available++) {
		configure_workers("auto");
		detected_cores = TEST_RESOURCE_BUDGET;
		thread_budget = available;
		check_jobs(available);
		assert(hires_render_workers_count() == available);
	}
	for (legacy_s32 available = 0; available < TEST_FAILURE_LIMIT; available++) {
		configure_workers("auto");
		semaphore_budget = available;
		legacy_s32 expected_workers = available > 0 ? available - 1 : 0;
		check_jobs(expected_workers);
		assert(hires_render_workers_count() == expected_workers);
	}
	const legacy_char *settings[] = {
		"0",	   "1",	 "2",		 "63", "999",
		"invalid", "-2", "2workers", " ",  "999999999999999999999999999999999999",
		"auto2"};
	const legacy_s32 configured[] = {0, 1, 2, TEST_MAX_WORKERS, TEST_MAX_WORKERS, 0, 0, 0, 0, 0, 0};
	detected_cores = 1;
	for (legacy_u32 index = 0; index < sizeof(settings) / sizeof(settings[0]); index++) {
		configure_workers(settings[index]);
		/* A previous library error must not reject a valid numeric override. */
		errno = ERANGE;
		assert(hires_render_workers_count() == configured[index]);
		check_jobs(configured[index]);
	}
	configure_workers("auto");
	detected_cores = TEST_RESOURCE_BUDGET;
	struct JOB_RESULTS single = {0};
	assert(hires_render_workers_run(0, record_job, &single) == 0);
	assert(single.visits[0] == 0);
	assert(hires_render_workers_run(1, record_job, &single) == 0);
	assert(single.visits[0] == 1 && single.threads[0] == SDL_GetCurrentThreadID());
	assert(created_threads == 0 && created_semaphores == 0);
	check_jobs(TEST_MAX_WORKERS);
	check_rapid_phases();
	hires_render_workers_shutdown();
	hires_render_workers_shutdown();
	assert(live_semaphores == 0);
	SDL_Quit();
	puts("Renderer serial defaults, overrides, CPU detection, failure recovery and ownership "
		 "passed.");
	return 0;
}
