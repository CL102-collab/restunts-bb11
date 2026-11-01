#include "sdl3.h"
#include "music.h"
#include "../../c/platform.h"
#include "../../c/hires/presentation.h"
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define TIMER_CALLBACK_CAPACITY DOS_TIMER_USABLE_CALLBACK_COUNT
#define TIMER_TICK_MS 10U
#define TIMER_POLL_DELAY_MS 1U

static void (*callbacks[TIMER_CALLBACK_CAPACITY])(void);
static legacy_u64 last_tick;
static legacy_u32 game_counter;
static legacy_u32 realtime_counter;
static legacy_u32 last_counter;
static legacy_u32 slow_counter;
static legacy_u32 slow_divider;
static legacy_u8 initialized;
static legacy_u8 suspended;
static legacy_u8 dispatching;

#ifdef __EMSCRIPTEN__
#define BROWSER_EVENT_YIELD_INTERVAL_MS 8U

static legacy_u64 last_browser_yield;

/* Resume on a message task, including after a timed wait. Resuming directly
 * from setTimeout would nest the next polling timer and eventually clamp every
 * requested 0/1ms sleep to at least 4ms. Asyncify permits one suspended C stack. */
EM_ASYNC_JS(void, browser_delay, (legacy_u32 milliseconds), {
	if (!Module['restuntsBrowserPacing']) {
		const state = {channel : new MessageChannel(), resume : null};
		state.channel.port1.onmessage = function()
		{
			const resume = state.resume;
			state.resume = null;
			resume();
		};
		Module['restuntsBrowserPacing'] = state;
	}
	const state = Module['restuntsBrowserPacing'];
	await new Promise(function(resolve) {
		state.resume = resolve;
		if (milliseconds > 0) {
			setTimeout(function() { state.channel.port2.postMessage(null); }, milliseconds);
		} else {
			state.channel.port2.postMessage(null);
		}
	});
});
#endif

void sdl3_platform_delay(legacy_u32 milliseconds)
{
#ifdef __EMSCRIPTEN__
	browser_delay(milliseconds);
	/* Polling and presentation share this timestamp, so an explicit timer or
	 * race wait also satisfies the input-only loop's cooperative yield. */
	last_browser_yield = SDL_GetTicks();
#else
	SDL_Delay(milliseconds);
#endif
}

#ifdef __EMSCRIPTEN__
void sdl3_browser_yield_if_due(void)
{
	if (SDL_GetTicks() - last_browser_yield >= BROWSER_EVENT_YIELD_INTERVAL_MS) {
		sdl3_platform_delay(0);
	}
}
#endif

legacy_u64 hires_presentation_now(void)
{
	return SDL_GetTicksNS();
}

void sdl3_timer_rebase(void)
{
	last_tick = SDL_GetTicks();
}

void sdl3_timer_pump(void)
{
	if (!initialized || dispatching) {
		return;
	}
	dispatching = true;
	legacy_u64 now = SDL_GetTicks();
	/* A backward clock sample must not wrap the unsigned elapsed time and
	 * trap the main thread dispatching timer/audio callbacks. Rebase so the
	 * normal cadence also resumes after a persistent clock reset. */
	if (now < last_tick) {
		last_tick = now;
	}
	while (now - last_tick >= TIMER_TICK_MS) {
		last_tick += TIMER_TICK_MS;
		realtime_counter++;
		if (++slow_divider == DOS_TIMER_DEFAULT_DIVIDER_PERIOD) {
			slow_divider = 0;
			slow_counter++;
		}
		if (!suspended) {
			game_counter++;
			/* Snapshot: callbacks can unregister themselves during dispatch. */
			void (*pending[TIMER_CALLBACK_CAPACITY])(void);
			memcpy(pending, callbacks, sizeof(pending));
			for (legacy_u32 index = 0; index < TIMER_CALLBACK_CAPACITY; index++) {
				if (pending[index] != NULL) {
					pending[index]();
				}
			}
		}
		sdl3_audio_update();
	}
	dispatching = false;
}

legacy_s16 dos_timer_register_callback(void (*callback)(void))
{
	for (legacy_u32 index = 0; index < TIMER_CALLBACK_CAPACITY; index++) {
		if (callbacks[index] == NULL) {
			callbacks[index] = callback;
			return DOS_TIMER_CALLBACK_REGISTRATION_SUCCEEDED;
		}
	}
	return DOS_TIMER_CALLBACK_REGISTRATION_FAILED;
}

void dos_timer_unregister_callback(void (*callback)(void))
{
	for (legacy_u32 index = 0; index < TIMER_CALLBACK_CAPACITY; index++) {
		if (callbacks[index] == callback) {
			memmove(callbacks + index, callbacks + index + 1U,
					(TIMER_CALLBACK_CAPACITY - index - 1U) * sizeof(callbacks[0]));
			callbacks[TIMER_CALLBACK_CAPACITY - 1U] = NULL;
			return;
		}
	}
}

void dos_timer_setup_interrupt(void)
{
	if (initialized) {
		return;
	}
	initialized = true;
	last_tick = SDL_GetTicks();
	game_counter = 0;
	realtime_counter = 0;
	last_counter = 0;
	slow_counter = 0;
	slow_divider = 0;
	suspended = false;
	memset(callbacks, 0, sizeof(callbacks));
}

void dos_timer_shutdown(void)
{
	initialized = false;
	memset(callbacks, 0, sizeof(callbacks));
}

void dos_timer_reset_counter(void)
{
	sdl3_timer_pump();
	game_counter = 0;
	last_counter = 0;
}

legacy_s32 sdl3_timer_callbacks_suspended(void)
{
	return suspended;
}

void dos_timer_set_callbacks_suspended(legacy_s16 value)
{
	sdl3_timer_pump();
	suspended = (value & DOS_TIMER_CALLBACK_SUSPENDED_MASK) != 0;
	sdl3_music_sync();
}

legacy_u32 dos_timer_get_realtime_counter(void)
{
	sdl3_platform_pump();
	return realtime_counter;
}

legacy_u32 timer_get_counter(void)
{
	static legacy_u32 previous_read;
	sdl3_platform_pump();
	/* Legacy waits poll this accessor. Yield without delaying timer callbacks
	 * or consuming an entire 10ms tick, so menus and frame pacing stay responsive. */
	if (!dispatching && initialized && previous_read == game_counter) {
		sdl3_platform_delay(TIMER_POLL_DELAY_MS);
		sdl3_platform_pump();
	}
	previous_read = game_counter;
	return game_counter;
}

legacy_u32 timer_get_delta(void)
{
	legacy_u32 current = timer_get_counter();
	legacy_u32 delta = current - last_counter;
	last_counter = current;
	return delta;
}

legacy_u32 timer_get_slow_counter(void)
{
	(void)timer_get_counter();
	return slow_counter;
}
