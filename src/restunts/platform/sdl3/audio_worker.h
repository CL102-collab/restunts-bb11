#ifndef RESTUNTS_SDL3_AUDIO_WORKER_H
#define RESTUNTS_SDL3_AUDIO_WORKER_H

#include "../../c/legacy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL3_AUDIO_WORKER_SAMPLE_RATE 44100U
#define SDL3_AUDIO_WORKER_TICK_FRAMES 441U
#define SDL3_AUDIO_WORKER_LEAD_FRAMES (SDL3_AUDIO_WORKER_TICK_FRAMES * 3U)
#define SDL3_AUDIO_WORKER_CALLBACK_CAPACITY 2U

typedef void (*sdl3_audio_worker_command)(void *argument);
typedef void (*sdl3_audio_worker_callback)(void);

struct SDL3_AUDIO_WORKER_STATS {
	legacy_u64 ticks;
	legacy_u64 callback_ticks;
	legacy_u64 callback_invocations;
	legacy_u64 commands;
	/* Queue-empty observations after priming, including explicit stream clears.
	 * This counter alone does not prove an audible device underrun. */
	legacy_u64 underruns;
	legacy_u64 late_wakeups;
	legacy_u64 max_wake_gap_ns;
	legacy_u64 max_command_latency_ns;
	legacy_s32 queued_frames;
	legacy_u32 max_queued_frames;
	legacy_u8 active;
	legacy_u8 suspended;
};

/* Start is explicit. Unit fixtures, batch tools, DOS and the serial browser do
 * not silently acquire a background thread. Stop drains pending commands and
 * joins the thread before the caller releases emulator/resource state. */
legacy_int sdl3_audio_worker_start(void);
void sdl3_audio_worker_stop(void);
legacy_u8 sdl3_audio_worker_is_active(void);
legacy_u8 sdl3_audio_worker_is_worker(void);

/* Synchronous FIFO dispatch: the argument may live on the caller's stack and
 * contain return values. Completion acknowledges all callback writes. Calls on
 * the worker itself, or while no worker exists, execute directly. */
legacy_int sdl3_audio_worker_call(sdl3_audio_worker_command command, void *argument);

/* Only the audio sequence and engine callbacks belong here. Return values match
 * DOS_TIMER_CALLBACK_REGISTRATION_SUCCEEDED / FAILED. */
legacy_s16 sdl3_audio_worker_register_callback(sdl3_audio_worker_callback callback);
void sdl3_audio_worker_unregister_callback(sdl3_audio_worker_callback callback);
void sdl3_audio_worker_set_suspended(legacy_u8 suspended);

/* Copies counters without executing commands, pumping timers or rendering. */
void sdl3_audio_worker_get_stats(struct SDL3_AUDIO_WORKER_STATS *statistics);

/* Implemented by the selected native audio backend: queued input frames at
 * 44.1kHz, or -1 when no synth output stream is initialized. */
legacy_s32 sdl3_audio_queued_frames(void);

#ifdef __cplusplus
}
#endif

#endif
