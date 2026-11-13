#ifndef RESTUNTS_SDL3_AUDIO_COMMANDS_H
#define RESTUNTS_SDL3_AUDIO_COMMANDS_H

#include "../../c/legacy.h"
#include <stddef.h>

#define SDL3_AUDIO_COMMAND_ARGUMENTS 12U
#define SDL3_AUDIO_COMMAND_RESOURCES 3U
#define SDL3_AUDIO_COMMAND_TEXT_BYTES 1024U

enum SDL3_AUDIO_COMMAND_TYPE {
	AUDIO_COMMAND_CONFIGURE = 1,
	AUDIO_COMMAND_INIT_RESOURCES,
	AUDIO_COMMAND_FINALIZE,
	AUDIO_COMMAND_INIT_ENGINE,
	AUDIO_COMMAND_ENGINE_SPATIAL,
	AUDIO_COMMAND_RELEASE_RESOURCE,
	AUDIO_COMMAND_ENABLE_MUSIC,
	AUDIO_COMMAND_DISABLE_MUSIC,
	AUDIO_COMMAND_TOGGLE_MUSIC,
	AUDIO_COMMAND_STOP_MUSIC,
	AUDIO_COMMAND_ENABLE_EFFECTS,
	AUDIO_COMMAND_DISABLE_EFFECTS,
	AUDIO_COMMAND_TOGGLE_EFFECTS,
	AUDIO_COMMAND_EFFECT_CHANNEL_IDLE,
	AUDIO_COMMAND_RESET_CHANNELS,
	AUDIO_COMMAND_SUSPEND,
	AUDIO_COMMAND_RESUME,
	AUDIO_COMMAND_ADD_DRIVER_TIMER,
	AUDIO_COMMAND_REMOVE_DRIVER_TIMER,
	AUDIO_COMMAND_START_ENGINE,
	AUDIO_COMMAND_STOP_ENGINE,
	AUDIO_COMMAND_STOP_SKID_SOUND,
	AUDIO_COMMAND_PLAY_BUMP,
	AUDIO_COMMAND_PLAY_SCRAPE,
	AUDIO_COMMAND_PLAY_CRASH_AND_STOP_ENGINE,
	AUDIO_COMMAND_PLAY_PAVED_SKID,
	AUDIO_COMMAND_PLAY_OFFROAD_SKID,
	AUDIO_COMMAND_SET_MUSIC_CHANNEL_VOLUME,
	AUDIO_COMMAND_FADE_STEP,
	AUDIO_COMMAND_OGG_SET_ENABLED,
	AUDIO_COMMAND_OGG_TOGGLE,
	AUDIO_COMMAND_OGG_GET_ENABLED,
	AUDIO_COMMAND_OGG_SYNC,
	AUDIO_COMMAND_OGG_SET_VOLUME,
	AUDIO_COMMAND_CALLBACKS_SUSPENDED,
	AUDIO_COMMAND_SHUTDOWN,
	AUDIO_COMMAND_FILE,
	AUDIO_COMMAND_SET_FINISH_CALLBACK
};

/* Payloads are command-duration borrowed bytes. The executor clones all banks
 * into its private arena before acknowledging a preparation command. */
struct SDL3_AUDIO_COMMAND {
	legacy_u32 op;
	legacy_s32 args[SDL3_AUDIO_COMMAND_ARGUMENTS];
	legacy_u64 keys[SDL3_AUDIO_COMMAND_RESOURCES];
	const void *data[SDL3_AUDIO_COMMAND_RESOURCES];
	size_t bytes[SDL3_AUDIO_COMMAND_RESOURCES];
	legacy_char text[SDL3_AUDIO_COMMAND_TEXT_BYTES];
	legacy_s32 result;
	legacy_u32 pointer_result;
	legacy_u16 legacy_hihat;
	legacy_u8 impact;
	legacy_u8 flush;
	legacy_u8 failed;
};

legacy_u8 sdl3_audio_dispatch_needed(void);
legacy_u8 sdl3_audio_dispatch_is_owner(void);
legacy_s32 sdl3_audio_command_scalars(legacy_u32 op, const legacy_s32 *args, size_t count);
void sdl3_audio_command_send(struct SDL3_AUDIO_COMMAND *command);
void sdl3_audio_command_execute(void *command);
void sdl3_audio_command_impact(void);
void sdl3_audio_command_quiesce(legacy_u8 usage);
void sdl3_audio_command_tick(void);
void sdl3_audio_command_enable(legacy_u8 enabled);
legacy_int sdl3_audio_command_start(void);
legacy_u8 sdl3_audio_command_requested(void);
legacy_u8 sdl3_audio_command_active(void);
legacy_u8 sdl3_audio_command_shutdown(void);
void sdl3_audio_clear_queued(void);
legacy_u8 sdl3_audio_command_callbacks_suspended(void);
legacy_int sdl3_audio_command_browser_load(void);
void sdl3_audio_get_configuration(legacy_u8 *mode, legacy_u8 *disabled,
								  const legacy_char **directory, const legacy_char **model);
void sdl3_audio_command_release(const void *source);
void *sdl3_audio_command_prepare(void *song, void *voices, const legacy_s8 *name);
legacy_s16 sdl3_audio_command_engine(legacy_s16 type, const void *definition, void *voices,
									 void *effects);
void sdl3_audio_command_spatial(const legacy_s16 *values);
void sdl3_audio_command_finalize(void *resource);
void sdl3_audio_command_pump(void);

#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__)
#define AUDIO_ASYNC0(op)                                                                           \
	do {                                                                                           \
		if (sdl3_audio_dispatch_needed()) {                                                        \
			sdl3_audio_command_scalars(op, NULL, 0);                                               \
			return;                                                                                \
		}                                                                                          \
	} while (0)
#define AUDIO_ASYNC1(op, a)                                                                        \
	do {                                                                                           \
		if (sdl3_audio_dispatch_needed()) {                                                        \
			const legacy_s32 audio_arguments[] = {(legacy_s32)(a)};                                \
			sdl3_audio_command_scalars(op, audio_arguments, 1);                                    \
			return;                                                                                \
		}                                                                                          \
	} while (0)
#define AUDIO_ASYNC_RESULT0(op)                                                                    \
	do {                                                                                           \
		if (sdl3_audio_dispatch_needed()) {                                                        \
			return (legacy_s16)sdl3_audio_command_scalars(op, NULL, 0);                            \
		}                                                                                          \
	} while (0)
#define AUDIO_ASYNC_RESULT1(op, a)                                                                 \
	do {                                                                                           \
		if (sdl3_audio_dispatch_needed()) {                                                        \
			const legacy_s32 audio_arguments[] = {(legacy_s32)(a)};                                \
			return (legacy_s16)sdl3_audio_command_scalars(op, audio_arguments, 1);                 \
		}                                                                                          \
	} while (0)
#else
#define AUDIO_ASYNC0(op)                                                                           \
	do {                                                                                           \
	} while (0)
#define AUDIO_ASYNC1(op, a)                                                                        \
	do {                                                                                           \
	} while (0)
#define AUDIO_ASYNC_RESULT0(op)                                                                    \
	do {                                                                                           \
	} while (0)
#define AUDIO_ASYNC_RESULT1(op, a)                                                                 \
	do {                                                                                           \
	} while (0)
#endif

#endif
