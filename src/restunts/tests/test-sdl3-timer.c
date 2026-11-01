#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include "../platform/sdl3/sdl3.h"
#include "../c/platform.h"

enum {
	TEST_START_MS = 1000,
	TEST_TICK_MS = 10,
	TEST_PARTIAL_TICK_MS = TEST_TICK_MS / 2,
	TEST_INITIAL_TICKS = 3,
	TEST_POLL_DELAY_MS = 1,
	TEST_EXPLICIT_DELAY_MS = 7,
	TEST_NS_PER_MS = 1000000,
	TEST_SUB_MS_NS = 12345,
	TEST_MAX_DISPATCHES = 32
};

static legacy_u64 mock_time_ms;
static legacy_u64 mock_time_ns;
static legacy_u32 first_calls;
static legacy_u32 second_calls;
static legacy_u32 audio_calls;
static legacy_u32 music_sync_calls;
static legacy_s32 music_paused;
static legacy_u32 delay_calls;
static legacy_u32 last_delay_ms;
static legacy_u32 expected_delay_ms = TEST_POLL_DELAY_MS;
static legacy_u8 inside_callback;

static legacy_u64 test_get_ticks(void)
{
	return mock_time_ms;
}

static legacy_u64 test_get_ticks_ns(void)
{
	return mock_time_ns;
}

static void test_delay(legacy_u32 milliseconds)
{
	assert(milliseconds == expected_delay_ms);
	last_delay_ms = milliseconds;
	delay_calls++;
}

/* Mock the clocks before including the implementation so clock regressions are
 * deterministic and this test never waits for wall-clock time to pass. */
#define SDL_GetTicks test_get_ticks
#define SDL_GetTicksNS test_get_ticks_ns
#define SDL_Delay test_delay
#include "../platform/sdl3/timer.c"
#undef SDL_GetTicks
#undef SDL_GetTicksNS
#undef SDL_Delay

void sdl3_music_sync(void)
{
	music_sync_calls++;
	music_paused = sdl3_timer_callbacks_suspended();
}

void sdl3_platform_pump(void)
{
	sdl3_timer_pump();
}

void sdl3_audio_update(void)
{
	assert(first_calls == second_calls);
	/* Bound dispatches even while callbacks are suspended: unsigned clock
	 * underflow must fail this test promptly instead of hanging the runner. */
	assert(++audio_calls <= TEST_MAX_DISPATCHES);
}

static void first_callback(void)
{
	assert(!inside_callback);
	inside_callback = true;
	assert(first_calls == second_calls);
	assert(++first_calls <= TEST_MAX_DISPATCHES);
	legacy_u32 delays_before_callback = delay_calls;
	sdl3_timer_pump();
	(void)dos_timer_get_realtime_counter();
	(void)timer_get_counter();
	assert(delay_calls == delays_before_callback);
	inside_callback = false;
}

static void second_callback(void)
{
	assert(first_calls == second_calls + 1U);
	second_calls++;
}

static void register_callbacks(void)
{
	assert(dos_timer_register_callback(first_callback) ==
		   DOS_TIMER_CALLBACK_REGISTRATION_SUCCEEDED);
	assert(dos_timer_register_callback(second_callback) ==
		   DOS_TIMER_CALLBACK_REGISTRATION_SUCCEEDED);
}

static void reset_timer(void)
{
	dos_timer_shutdown();
	mock_time_ms = TEST_START_MS;
	mock_time_ns = mock_time_ms * TEST_NS_PER_MS + TEST_SUB_MS_NS;
	first_calls = second_calls = audio_calls = delay_calls = 0;
	music_sync_calls = 0;
	music_paused = false;
	inside_callback = false;
	dos_timer_setup_interrupt();
	register_callbacks();
}

static void expect_counters(legacy_u32 game_ticks, legacy_u32 realtime_ticks,
							legacy_u32 callback_ticks, legacy_u32 audio_ticks)
{
	assert(timer_get_counter() == game_ticks);
	assert(dos_timer_get_realtime_counter() == realtime_ticks);
	assert(timer_get_slow_counter() == realtime_ticks / DOS_TIMER_DEFAULT_DIVIDER_PERIOD);
	assert(first_calls == callback_ticks);
	assert(second_calls == callback_ticks);
	assert(audio_calls == audio_ticks);
}

static void test_platform_delay(void)
{
	reset_timer();
	expected_delay_ms = 0;
	sdl3_platform_delay(0);
	assert(delay_calls == 1U && last_delay_ms == 0);
	expected_delay_ms = TEST_EXPLICIT_DELAY_MS;
	sdl3_platform_delay(TEST_EXPLICIT_DELAY_MS);
	assert(delay_calls == 2U && last_delay_ms == TEST_EXPLICIT_DELAY_MS);
	expected_delay_ms = TEST_POLL_DELAY_MS;
	(void)timer_get_counter();
	assert(delay_calls == 3U && last_delay_ms == TEST_POLL_DELAY_MS);
}

static void test_regular_progression(void)
{
	reset_timer();
	assert(hires_presentation_now() == mock_time_ns);
	expect_counters(0, 0, 0, 0);

	mock_time_ms += TEST_TICK_MS - 1U;
	expect_counters(0, 0, 0, 0);
	mock_time_ms++;
	expect_counters(1, 1, 1, 1);

	mock_time_ms = TEST_START_MS + TEST_INITIAL_TICKS * TEST_TICK_MS + TEST_PARTIAL_TICK_MS;
	expect_counters(TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS);
	mock_time_ms = TEST_START_MS + (TEST_INITIAL_TICKS + 1U) * TEST_TICK_MS - 1U;
	expect_counters(TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS);
	mock_time_ms++;
	expect_counters(TEST_INITIAL_TICKS + 1U, TEST_INITIAL_TICKS + 1U, TEST_INITIAL_TICKS + 1U,
					TEST_INITIAL_TICKS + 1U);

	mock_time_ms = TEST_START_MS + DOS_TIMER_DEFAULT_DIVIDER_PERIOD * TEST_TICK_MS;
	expect_counters(DOS_TIMER_DEFAULT_DIVIDER_PERIOD, DOS_TIMER_DEFAULT_DIVIDER_PERIOD,
					DOS_TIMER_DEFAULT_DIVIDER_PERIOD, DOS_TIMER_DEFAULT_DIVIDER_PERIOD);
}

static void test_backward_clock(void)
{
	reset_timer();
	mock_time_ms += TEST_INITIAL_TICKS * TEST_TICK_MS + TEST_PARTIAL_TICK_MS;
	expect_counters(TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS);

	/* A regressed sample contributes no ticks, and establishes a new baseline. */
	mock_time_ms = TEST_START_MS + TEST_TICK_MS;
	expect_counters(TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS);
	mock_time_ms += TEST_TICK_MS - 1U;
	expect_counters(TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS);
	mock_time_ms++;
	expect_counters(TEST_INITIAL_TICKS + 1U, TEST_INITIAL_TICKS + 1U, TEST_INITIAL_TICKS + 1U,
					TEST_INITIAL_TICKS + 1U);
	mock_time_ms += TEST_TICK_MS;
	expect_counters(TEST_INITIAL_TICKS + 2U, TEST_INITIAL_TICKS + 2U, TEST_INITIAL_TICKS + 2U,
					TEST_INITIAL_TICKS + 2U);
}

static void test_music_pause_transition(void)
{
	reset_timer();
	mock_time_ms += TEST_PARTIAL_TICK_MS;
	dos_timer_set_callbacks_suspended(DOS_TIMER_CALLBACK_SUSPENDED_MASK);
	/* No timer tick has elapsed: the device must pause immediately, without
	 * waiting for sdl3_audio_update to notice the changed callback state. */
	assert(music_sync_calls == 1U && music_paused);
	assert(sdl3_timer_callbacks_suspended());
	assert(first_calls == 0 && second_calls == 0 && audio_calls == 0);

	mock_time_ms += TEST_INITIAL_TICKS * TEST_TICK_MS;
	dos_timer_set_callbacks_suspended(false);
	/* Pending ticks belong to the suspended interval, then the device resumes
	 * before another timer tick or sequencer callback can run. */
	assert(music_sync_calls == 2U && !music_paused);
	assert(!sdl3_timer_callbacks_suspended());
	assert(first_calls == 0 && second_calls == 0 && audio_calls == TEST_INITIAL_TICKS);
	mock_time_ms += TEST_TICK_MS;
	expect_counters(1, TEST_INITIAL_TICKS + 1U, 1, TEST_INITIAL_TICKS + 1U);
}

static void test_paused_clock_recovery(void)
{
	reset_timer();
	mock_time_ms += TEST_INITIAL_TICKS * TEST_TICK_MS + TEST_PARTIAL_TICK_MS;
	expect_counters(TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS);
	dos_timer_set_callbacks_suspended(true);

	mock_time_ms = TEST_START_MS + TEST_TICK_MS;
	expect_counters(TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS);
	mock_time_ms += TEST_TICK_MS;
	expect_counters(TEST_INITIAL_TICKS, TEST_INITIAL_TICKS + 1U, TEST_INITIAL_TICKS,
					TEST_INITIAL_TICKS + 1U);
	mock_time_ms += TEST_TICK_MS;
	expect_counters(TEST_INITIAL_TICKS, TEST_INITIAL_TICKS + 2U, TEST_INITIAL_TICKS,
					TEST_INITIAL_TICKS + 2U);

	dos_timer_set_callbacks_suspended(false);
	mock_time_ms += TEST_TICK_MS;
	expect_counters(TEST_INITIAL_TICKS + 1U, TEST_INITIAL_TICKS + 3U, TEST_INITIAL_TICKS + 1U,
					TEST_INITIAL_TICKS + 3U);
}

static void test_shutdown_restart(void)
{
	reset_timer();
	mock_time_ms += TEST_INITIAL_TICKS * TEST_TICK_MS;
	expect_counters(TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS);
	dos_timer_set_callbacks_suspended(true);
	dos_timer_shutdown();
	mock_time_ms += DOS_TIMER_DEFAULT_DIVIDER_PERIOD * TEST_TICK_MS;
	expect_counters(TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS);

	dos_timer_setup_interrupt();
	expect_counters(0, 0, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS);
	mock_time_ms += TEST_TICK_MS;
	expect_counters(1, 1, TEST_INITIAL_TICKS, TEST_INITIAL_TICKS + 1U);

	register_callbacks();
	mock_time_ms += TEST_TICK_MS;
	expect_counters(2, 2, TEST_INITIAL_TICKS + 1U, TEST_INITIAL_TICKS + 2U);
	dos_timer_shutdown();
}

legacy_int main(void)
{
	test_platform_delay();
	test_regular_progression();
	test_backward_clock();
	test_music_pause_transition();
	test_paused_clock_recovery();
	test_shutdown_restart();
	puts("SDL3 timer regression tests passed");
	return 0;
}
