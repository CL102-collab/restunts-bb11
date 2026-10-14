#include "amiga.h"
#include "../../c/platform.h"
#include "../../c/presentation.h"
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <string.h>
#ifdef RESTUNTS_AMIGA_TRACE
#include <stdio.h>
#endif

struct Device *TimerBase;
static struct MsgPort *timer_port;
static struct timerequest *timer_request;
static void (*callbacks[DOS_TIMER_USABLE_CALLBACK_COUNT])(void);
static legacy_u32 last_tick, game_counter, realtime_counter, last_counter, slow_counter;
static legacy_u16 divider;
static legacy_u8 initialized, suspended, dispatching;
static struct timeval origin;

static legacy_u8 open_clock(void)
{
	if (TimerBase != NULL) return 1;
	timer_port = CreateMsgPort();
	if (timer_port != NULL) timer_request = (struct timerequest *)CreateIORequest(timer_port, sizeof(*timer_request));
	if (timer_request == NULL || OpenDevice("timer.device", UNIT_MICROHZ,
		(struct IORequest *)timer_request, 0) != 0) {
		if (timer_request != NULL) DeleteIORequest((struct IORequest *)timer_request);
		if (timer_port != NULL) DeleteMsgPort(timer_port);
		timer_request = NULL;
		timer_port = NULL;
		return 0;
	}
	TimerBase = timer_request->tr_node.io_Device;
	GetSysTime(&origin);
	return 1;
}

legacy_u32 amiga_clock_ticks(void)
{
	struct timeval now;
	if (!open_clock()) return 0;
	GetSysTime(&now);
	SubTime(&now, &origin);
	return (legacy_u32)now.tv_secs * 100UL + now.tv_micro / 10000UL;
}

legacy_u64 presentation_now(void)
{
	return (legacy_u64)amiga_clock_ticks() * 10000000ULL;
}

void amiga_timer_pump(void)
{
	legacy_u32 now;
#ifdef RESTUNTS_AMIGA_TRACE
	static legacy_u8 reported_slow_batches;
	legacy_u32 first_tick, callbacks_finished, batch_finished;
#endif
	if (!initialized || dispatching) return;
	dispatching = 1;
	amiga_audio_begin_update();
	now = amiga_clock_ticks();
	/* A user-adjusted system clock must not create billions of callbacks. */
	if (now < last_tick) last_tick = now;
#ifdef RESTUNTS_AMIGA_TRACE
	first_tick = last_tick;
#endif
	while (now - last_tick >= 1U) {
		legacy_u16 i;
		void (*pending[DOS_TIMER_USABLE_CALLBACK_COUNT])(void);
		++last_tick; ++realtime_counter;
		if (++divider == DOS_TIMER_DEFAULT_DIVIDER_PERIOD) { divider = 0; ++slow_counter; }
		if (!suspended) {
			++game_counter;
			memcpy(pending, callbacks, sizeof(pending));
			for (i = 0; i < DOS_TIMER_USABLE_CALLBACK_COUNT; ++i) if (pending[i]) pending[i]();
		}
	}
#ifdef RESTUNTS_AMIGA_TRACE
	callbacks_finished = amiga_clock_ticks();
#endif
	amiga_audio_end_update();
#ifdef RESTUNTS_AMIGA_TRACE
	batch_finished = amiga_clock_ticks();
	if (reported_slow_batches < 8U && batch_finished - now > 10U) {
		++reported_slow_batches;
		printf("Amiga timer: ticks=%lu callbacks_ms=%lu audio_commit_ms=%lu\n",
			(unsigned long)(last_tick - first_tick),
			(unsigned long)(callbacks_finished - now) * 10UL,
			(unsigned long)(batch_finished - callbacks_finished) * 10UL);
		fflush(stdout);
	}
#endif
	dispatching = 0;
}

void dos_timer_setup_interrupt(void)
{
	if (initialized) return;
	if (!open_clock()) { dos_show_startup_error((const legacy_s8 *)"Cannot open timer.device\n"); dos_process_exit(1); }
	last_tick = amiga_clock_ticks();
	game_counter = realtime_counter = last_counter = slow_counter = 0;
	divider = 0; suspended = 0; initialized = 1;
	memset(callbacks, 0, sizeof(callbacks));
}
legacy_s16 dos_timer_register_callback(void (*callback)(void))
{
	legacy_u16 i;
	if (callback == NULL) return DOS_TIMER_CALLBACK_REGISTRATION_FAILED;
	/* A callback begins now, not at the last poll before resource loading.
	 * Deliver the old interval using only the callbacks that existed then. */
	amiga_timer_pump();
	for (i = 0; i < DOS_TIMER_USABLE_CALLBACK_COUNT; ++i) if (callbacks[i] == NULL) {
		callbacks[i] = callback; return DOS_TIMER_CALLBACK_REGISTRATION_SUCCEEDED;
	}
	return DOS_TIMER_CALLBACK_REGISTRATION_FAILED;
}
void dos_timer_unregister_callback(void (*callback)(void))
{
	legacy_u16 i;
	for (i = 0; i < DOS_TIMER_USABLE_CALLBACK_COUNT; ++i) if (callbacks[i] == callback) {
		memmove(callbacks + i, callbacks + i + 1,
			(DOS_TIMER_USABLE_CALLBACK_COUNT - i - 1U) * sizeof(callbacks[0]));
		callbacks[DOS_TIMER_USABLE_CALLBACK_COUNT - 1U] = NULL; return;
	}
}
void dos_timer_reset_counter(void) { amiga_timer_pump(); game_counter = last_counter = 0; }
void dos_timer_set_callbacks_suspended(legacy_s16 value) { amiga_timer_pump(); suspended = (value & 255) != 0; }
legacy_u32 dos_timer_get_realtime_counter(void) { amiga_platform_poll(); return realtime_counter; }
legacy_u32 timer_get_counter(void) { amiga_platform_poll(); return game_counter; }
legacy_u32 timer_get_delta(void) { legacy_u32 now = timer_get_counter(), delta = now - last_counter; last_counter = now; return delta; }
legacy_u32 timer_get_slow_counter(void) { amiga_platform_poll(); return slow_counter; }
void dos_timer_shutdown(void)
{
	initialized = 0; memset(callbacks, 0, sizeof(callbacks));
	if (TimerBase != NULL) { CloseDevice((struct IORequest *)timer_request); TimerBase = NULL; }
	if (timer_request != NULL) { DeleteIORequest((struct IORequest *)timer_request); timer_request = NULL; }
	if (timer_port != NULL) { DeleteMsgPort(timer_port); timer_port = NULL; }
}
