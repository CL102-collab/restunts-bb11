/* Independent native audio clock. The game submits synchronous commands;
 * the worker alone advances audio callbacks and produces emulator PCM. */
#include "audio_worker.h"
#include "../../c/platform.h"
#include <string.h>
#ifdef RESTUNTS_AUDIO_WORKER
#include "audio_resources_worker.h"
#endif

#if !defined(__EMSCRIPTEN__) && !defined(__DJGPP__)
#include <SDL3/SDL.h>

#define AUDIO_WORKER_NS_PER_SECOND 1000000000ULL
#define AUDIO_WORKER_TICK_NS (AUDIO_WORKER_NS_PER_SECOND / DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define AUDIO_WORKER_POLL_MS 2
#define AUDIO_WORKER_MAX_BURST_TICKS 6U
#define AUDIO_WORKER_MAX_COMMAND_BURST 16U
#define AUDIO_WORKER_LATE_WAKE_NS (AUDIO_WORKER_TICK_NS * 2U)

extern void sdl3_audio_update(void);

struct AUDIO_WORKER_COMMAND {
	sdl3_audio_worker_command callback;
	void *argument;
	struct AUDIO_WORKER_COMMAND *next;
	legacy_u64 submitted_ns;
	legacy_u8 complete;
};

static SDL_SpinLock synchronization_lock;
/* Synchronization objects have process lifetime. Keeping them across stop/start
 * allows snapshots and concurrent command acknowledgments to remain valid. */
static SDL_Mutex *worker_mutex;
static SDL_Condition *worker_condition;
static SDL_Thread *worker_thread;
static SDL_ThreadID worker_id;
static legacy_u8 worker_running;
static legacy_u8 worker_started;
static legacy_u8 worker_stopping;
static legacy_u8 worker_joining;
static legacy_u8 callbacks_suspended;
static sdl3_audio_worker_callback callbacks[SDL3_AUDIO_WORKER_CALLBACK_CAPACITY];
static struct AUDIO_WORKER_COMMAND *first_command;
static struct AUDIO_WORKER_COMMAND *last_command;
static struct SDL3_AUDIO_WORKER_STATS worker_statistics;

static legacy_u8 audio_worker_synchronization(void)
{
	SDL_LockSpinlock(&synchronization_lock);
	if (worker_mutex == NULL) {
		worker_mutex = SDL_CreateMutex();
	}
	if (worker_condition == NULL && worker_mutex != NULL) {
		worker_condition = SDL_CreateCondition();
	}
	legacy_u8 ready = worker_mutex != NULL && worker_condition != NULL;
	SDL_UnlockSpinlock(&synchronization_lock);
	return ready;
}

legacy_u8 sdl3_audio_worker_is_active(void)
{
	if (!audio_worker_synchronization()) {
		return 0;
	}
	SDL_LockMutex(worker_mutex);
	legacy_u8 active = worker_running;
	SDL_UnlockMutex(worker_mutex);
	return active;
}

legacy_u8 sdl3_audio_worker_is_worker(void)
{
	if (!audio_worker_synchronization()) {
		return 0;
	}
	SDL_LockMutex(worker_mutex);
	legacy_u8 owner = worker_running && worker_id == SDL_GetCurrentThreadID();
	SDL_UnlockMutex(worker_mutex);
	return owner;
}

legacy_int sdl3_audio_worker_call(sdl3_audio_worker_command command, void *argument)
{
	if (command == NULL) {
		return 0;
	}
	if (!audio_worker_synchronization()) {
		command(argument);
		return 1;
	}
	struct AUDIO_WORKER_COMMAND pending;
	memset(&pending, 0, sizeof(pending));
	pending.callback = command;
	pending.argument = argument;
	pending.submitted_ns = SDL_GetTicksNS();
	SDL_LockMutex(worker_mutex);
	while (worker_running && !worker_started) {
		SDL_WaitCondition(worker_condition, worker_mutex);
	}
	if (worker_running && worker_id == SDL_GetCurrentThreadID()) {
		SDL_UnlockMutex(worker_mutex);
		command(argument);
		return 1;
	}
	/* A stop already in progress must finish before an unavailable-worker
	 * fallback can touch the same emulator state from a caller thread. */
	while (worker_running && worker_stopping) {
		SDL_WaitCondition(worker_condition, worker_mutex);
	}
	if (!worker_running) {
		SDL_UnlockMutex(worker_mutex);
		command(argument);
		return 1;
	}
	if (last_command != NULL) {
		last_command->next = &pending;
	} else {
		first_command = &pending;
	}
	last_command = &pending;
	SDL_BroadcastCondition(worker_condition);
	while (!pending.complete) {
		SDL_WaitCondition(worker_condition, worker_mutex);
	}
	SDL_UnlockMutex(worker_mutex);
	return 1;
}

static legacy_u8 audio_worker_command(void)
{
	SDL_LockMutex(worker_mutex);
	struct AUDIO_WORKER_COMMAND *command = first_command;
	if (command == NULL) {
		SDL_UnlockMutex(worker_mutex);
		return 0;
	}
	first_command = command->next;
	if (first_command == NULL) {
		last_command = NULL;
	}
	SDL_UnlockMutex(worker_mutex);
	command->callback(command->argument);
	legacy_u64 latency = SDL_GetTicksNS() - command->submitted_ns;
	SDL_LockMutex(worker_mutex);
	worker_statistics.commands++;
	if (latency > worker_statistics.max_command_latency_ns) {
		worker_statistics.max_command_latency_ns = latency;
	}
	command->complete = 1;
	SDL_BroadcastCondition(worker_condition);
	SDL_UnlockMutex(worker_mutex);
	return 1;
}

static void audio_worker_tick(void)
{
	sdl3_audio_worker_callback pending[SDL3_AUDIO_WORKER_CALLBACK_CAPACITY];
	SDL_LockMutex(worker_mutex);
	legacy_u8 suspended = callbacks_suspended;
	memcpy(pending, callbacks, sizeof(pending));
	SDL_UnlockMutex(worker_mutex);
	legacy_u64 invoked = 0;
	if (!suspended) {
		for (size_t index = 0; index < SDL3_AUDIO_WORKER_CALLBACK_CAPACITY; index++) {
			if (pending[index] != NULL) {
				pending[index]();
				invoked++;
			}
		}
	}
	sdl3_audio_update();
	SDL_LockMutex(worker_mutex);
	worker_statistics.ticks++;
	worker_statistics.callback_ticks += !suspended && invoked != 0;
	worker_statistics.callback_invocations += invoked;
	SDL_UnlockMutex(worker_mutex);
}

static void audio_worker_queue_state(legacy_s32 frames, legacy_u8 *primed, legacy_u8 *was_empty)
{
	SDL_LockMutex(worker_mutex);
	worker_statistics.queued_frames = frames;
	if (frames > 0) {
		if ((legacy_u32)frames > worker_statistics.max_queued_frames) {
			worker_statistics.max_queued_frames = (legacy_u32)frames;
		}
		*primed = 1;
		*was_empty = 0;
	} else if (frames == 0 && *primed && !*was_empty) {
		worker_statistics.underruns++;
		*was_empty = 1;
	}
	SDL_UnlockMutex(worker_mutex);
}

static legacy_int SDLCALL audio_worker_thread_main(void *unused)
{
	(void)unused;
	SDL_LockMutex(worker_mutex);
	worker_id = SDL_GetCurrentThreadID();
	SDL_UnlockMutex(worker_mutex);
#ifdef RESTUNTS_AUDIO_WORKER
	legacy_u8 execution_ready = sdl3_audio_resources_set_execution(1);
#else
	legacy_u8 execution_ready = 1;
#endif
	SDL_LockMutex(worker_mutex);
	worker_started = 1;
	if (!execution_ready) {
		worker_running = 0;
		worker_id = 0;
	}
	SDL_BroadcastCondition(worker_condition);
	SDL_UnlockMutex(worker_mutex);
	if (!execution_ready) {
		return 1;
	}
	legacy_u64 previous_wake = SDL_GetTicksNS();
	legacy_u64 fallback_tick = previous_wake + AUDIO_WORKER_TICK_NS;
	legacy_u8 queue_clock = 0;
	legacy_u8 primed = 0;
	legacy_u8 was_empty = 0;
	for (;;) {
		legacy_u64 now = SDL_GetTicksNS();
		legacy_u64 wake_gap = now - previous_wake;
		previous_wake = now;
		SDL_LockMutex(worker_mutex);
		if (wake_gap > worker_statistics.max_wake_gap_ns) {
			worker_statistics.max_wake_gap_ns = wake_gap;
		}
		worker_statistics.late_wakeups += wake_gap > AUDIO_WORKER_LATE_WAKE_NS;
		SDL_UnlockMutex(worker_mutex);
		for (legacy_u32 index = 0; index < AUDIO_WORKER_MAX_COMMAND_BURST; index++) {
			if (!audio_worker_command()) {
				break;
			}
		}
		SDL_LockMutex(worker_mutex);
		legacy_u8 finish = worker_stopping && first_command == NULL;
		SDL_UnlockMutex(worker_mutex);
		if (finish) {
			break;
		}
		for (legacy_u32 index = 0; index < AUDIO_WORKER_MAX_BURST_TICKS; index++) {
			SDL_LockMutex(worker_mutex);
			/* One due audio tick may run between command bursts so concurrent
			 * producers cannot starve PCM indefinitely. */
			legacy_u8 interrupted = worker_stopping || (first_command != NULL && index != 0);
			SDL_UnlockMutex(worker_mutex);
			if (interrupted) {
				break;
			}
			legacy_s32 queued = sdl3_audio_queued_frames();
			audio_worker_queue_state(queued, &primed, &was_empty);
			now = SDL_GetTicksNS();
			if (queued >= 0) {
				queue_clock = 1;
				if ((legacy_u32)queued >= SDL3_AUDIO_WORKER_LEAD_FRAMES) {
					break;
				}
			} else {
				if (queue_clock) {
					fallback_tick = now + AUDIO_WORKER_TICK_NS;
					queue_clock = 0;
				}
				if (now < fallback_tick) {
					break;
				}
				fallback_tick += AUDIO_WORKER_TICK_NS;
				/* No device is a fallback, not a reason to replay an
				 * arbitrarily long pause in a tight catch-up loop. */
				if (now > fallback_tick + AUDIO_WORKER_TICK_NS * AUDIO_WORKER_MAX_BURST_TICKS) {
					fallback_tick = now + AUDIO_WORKER_TICK_NS;
				}
			}
			audio_worker_tick();
		}
		SDL_LockMutex(worker_mutex);
		if (first_command == NULL && !worker_stopping) {
			SDL_WaitConditionTimeout(worker_condition, worker_mutex, AUDIO_WORKER_POLL_MS);
		}
		SDL_UnlockMutex(worker_mutex);
	}
#ifdef RESTUNTS_AUDIO_WORKER
	sdl3_audio_resources_set_execution(0);
#endif
	SDL_LockMutex(worker_mutex);
	worker_running = 0;
	worker_id = 0;
	SDL_BroadcastCondition(worker_condition);
	SDL_UnlockMutex(worker_mutex);
	return 0;
}

legacy_int sdl3_audio_worker_start(void)
{
	if (!audio_worker_synchronization()) {
		return 0;
	}
	SDL_LockMutex(worker_mutex);
	if (worker_running && !worker_stopping) {
		SDL_UnlockMutex(worker_mutex);
		return 1;
	}
	legacy_u8 previous = worker_thread != NULL;
	legacy_u8 owner = worker_id == SDL_GetCurrentThreadID();
	SDL_UnlockMutex(worker_mutex);
	if (owner && previous) {
		return 0;
	}
	if (previous) {
		sdl3_audio_worker_stop();
	}
	SDL_LockMutex(worker_mutex);
	/* Another start may have completed while a prior thread was joined. */
	if (worker_running) {
		legacy_int ready = !worker_stopping;
		SDL_UnlockMutex(worker_mutex);
		return ready;
	}
	memset(&worker_statistics, 0, sizeof(worker_statistics));
	worker_statistics.queued_frames = -1;
	worker_stopping = 0;
	worker_joining = 0;
	worker_running = 1;
	worker_started = 0;
	worker_id = 0;
	worker_thread = SDL_CreateThread(audio_worker_thread_main, "Restunts audio", NULL);
	if (worker_thread == NULL) {
		worker_running = 0;
		SDL_UnlockMutex(worker_mutex);
		return 0;
	}
	while (!worker_started) {
		SDL_WaitCondition(worker_condition, worker_mutex);
	}
	legacy_u8 ready = worker_running;
	SDL_UnlockMutex(worker_mutex);
	if (!ready) {
		sdl3_audio_worker_stop();
#ifdef RESTUNTS_AUDIO_WORKER
		SDL_SetError("%s", sdl3_audio_resources_error());
#endif
	}
	return ready;
}

void sdl3_audio_worker_stop(void)
{
	if (!audio_worker_synchronization()) {
		return;
	}
	SDL_LockMutex(worker_mutex);
	worker_stopping = 1;
	SDL_BroadcastCondition(worker_condition);
	if (worker_running && worker_id == SDL_GetCurrentThreadID()) {
		SDL_UnlockMutex(worker_mutex);
		return;
	}
	while (worker_joining) {
		SDL_WaitCondition(worker_condition, worker_mutex);
	}
	SDL_Thread *thread = worker_thread;
	if (thread == NULL) {
		SDL_UnlockMutex(worker_mutex);
		return;
	}
	worker_joining = 1;
	SDL_UnlockMutex(worker_mutex);
	SDL_WaitThread(thread, NULL);
	SDL_LockMutex(worker_mutex);
	worker_thread = NULL;
	worker_joining = 0;
	SDL_BroadcastCondition(worker_condition);
	SDL_UnlockMutex(worker_mutex);
}

struct AUDIO_WORKER_CALLBACK_CHANGE {
	sdl3_audio_worker_callback callback;
	legacy_s16 result;
};

static void audio_worker_register(void *argument)
{
	struct AUDIO_WORKER_CALLBACK_CHANGE *change = argument;
	SDL_LockMutex(worker_mutex);
	for (size_t index = 0; index < SDL3_AUDIO_WORKER_CALLBACK_CAPACITY; index++) {
		if (callbacks[index] == change->callback) {
			change->result = DOS_TIMER_CALLBACK_REGISTRATION_SUCCEEDED;
			SDL_UnlockMutex(worker_mutex);
			return;
		}
	}
	for (size_t index = 0; index < SDL3_AUDIO_WORKER_CALLBACK_CAPACITY; index++) {
		if (callbacks[index] == NULL) {
			callbacks[index] = change->callback;
			change->result = DOS_TIMER_CALLBACK_REGISTRATION_SUCCEEDED;
			break;
		}
	}
	SDL_UnlockMutex(worker_mutex);
}

legacy_s16 sdl3_audio_worker_register_callback(sdl3_audio_worker_callback callback)
{
	if (callback == NULL || !audio_worker_synchronization()) {
		return DOS_TIMER_CALLBACK_REGISTRATION_FAILED;
	}
	struct AUDIO_WORKER_CALLBACK_CHANGE change = {callback, DOS_TIMER_CALLBACK_REGISTRATION_FAILED};
	sdl3_audio_worker_call(audio_worker_register, &change);
	return change.result;
}

static void audio_worker_unregister(void *argument)
{
	struct AUDIO_WORKER_CALLBACK_CHANGE *change = argument;
	SDL_LockMutex(worker_mutex);
	for (size_t index = 0; index < SDL3_AUDIO_WORKER_CALLBACK_CAPACITY; index++) {
		if (callbacks[index] == change->callback) {
			for (size_t following = index + 1U; following < SDL3_AUDIO_WORKER_CALLBACK_CAPACITY;
				 following++) {
				callbacks[following - 1U] = callbacks[following];
			}
			callbacks[SDL3_AUDIO_WORKER_CALLBACK_CAPACITY - 1U] = NULL;
			break;
		}
	}
	SDL_UnlockMutex(worker_mutex);
}

void sdl3_audio_worker_unregister_callback(sdl3_audio_worker_callback callback)
{
	if (!audio_worker_synchronization()) {
		return;
	}
	struct AUDIO_WORKER_CALLBACK_CHANGE change = {callback, DOS_TIMER_CALLBACK_REGISTRATION_FAILED};
	sdl3_audio_worker_call(audio_worker_unregister, &change);
}

static void audio_worker_suspend(void *argument)
{
	SDL_LockMutex(worker_mutex);
	callbacks_suspended = *(legacy_u8 *)argument != 0;
	SDL_UnlockMutex(worker_mutex);
}

void sdl3_audio_worker_set_suspended(legacy_u8 suspended)
{
	if (!audio_worker_synchronization()) {
		return;
	}
	sdl3_audio_worker_call(audio_worker_suspend, &suspended);
}

void sdl3_audio_worker_get_stats(struct SDL3_AUDIO_WORKER_STATS *statistics)
{
	if (statistics == NULL) {
		return;
	}
	if (!audio_worker_synchronization()) {
		memset(statistics, 0, sizeof(*statistics));
		return;
	}
	SDL_LockMutex(worker_mutex);
	*statistics = worker_statistics;
	statistics->active = worker_running;
	statistics->suspended = callbacks_suspended;
	SDL_UnlockMutex(worker_mutex);
}

#else

/* Native threads are deliberately absent in DOS and serial Emscripten builds.
 * Browser realm transport is supplied by its own adapter. */
legacy_int sdl3_audio_worker_start(void)
{
	return 0;
}

void sdl3_audio_worker_stop(void)
{
}

legacy_u8 sdl3_audio_worker_is_active(void)
{
	return 0;
}

legacy_u8 sdl3_audio_worker_is_worker(void)
{
	return 0;
}

legacy_int sdl3_audio_worker_call(sdl3_audio_worker_command command, void *argument)
{
	if (command == NULL) {
		return 0;
	}
	command(argument);
	return 1;
}

legacy_s16 sdl3_audio_worker_register_callback(sdl3_audio_worker_callback callback)
{
	(void)callback;
	return DOS_TIMER_CALLBACK_REGISTRATION_FAILED;
}

void sdl3_audio_worker_unregister_callback(sdl3_audio_worker_callback callback)
{
	(void)callback;
}

void sdl3_audio_worker_set_suspended(legacy_u8 suspended)
{
	(void)suspended;
}

void sdl3_audio_worker_get_stats(struct SDL3_AUDIO_WORKER_STATS *statistics)
{
	if (statistics != NULL) {
		memset(statistics, 0, sizeof(*statistics));
	}
}

#endif
