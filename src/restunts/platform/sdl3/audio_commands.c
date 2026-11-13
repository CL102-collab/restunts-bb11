/* Audio commands carry immutable values and owned resource snapshots. Only the
 * audio owner runs the legacy sequencer, drivers and emulator state machines. */
#include "audio_commands.h"
#include "audio_completions.h"
#include "audio_worker.h"
#include "audio_resources_worker.h"
#include "sdl3.h"
#include "music.h"
#include "mt32_audio.h"
#include "../../c/audio/audio_control.h"
#include "../../c/audio/audio_internal.h"
#include "../../c/car/car_audio.h"
#include "../../c/externs.h"
#include "../../c/memmgr.h"
#include "../../c/startup_internal.h"
#include "../../c/platform.h"
#include "../../c/residue.h"
#include "../../c/resource.h"
#include "../../c/timing.h"
#include <stdint.h>
#include <string.h>
#if defined(__EMSCRIPTEN__) && !defined(RESTUNTS_BROWSER_AUDIO_RUNTIME)
#include <emscripten.h>
#endif

#define AUDIO_COMMAND_ENGINE_ID_BYTES 5U
#define AUDIO_COMMAND_ENGINE_HEADER_BYTES 8U
#define AUDIO_COMMAND_ENGINE_WIRE_BYTES                                                            \
	(AUDIO_COMMAND_ENGINE_HEADER_BYTES +                                                           \
	 AUDIO_ENGINE_RESOURCE_COUNT * AUDIO_COMMAND_ENGINE_ID_BYTES)
#define AUDIO_COMMAND_ENGINE_PARAMETERS_OFFSET 2U
#define AUDIO_COMMAND_ENGINE_INITIALIZED_OFFSET 6U
#define AUDIO_COMMAND_ENGINE_RESERVED_OFFSET 7U
#define AUDIO_COMMAND_MASTER_STATE_OPERATION 4
#define AUDIO_COMMAND_DRIVER_LOAD_FAILURE 2
#define AUDIO_COMMAND_FAR_SEGMENT_SHIFT 16U
#define AUDIO_COMMAND_MAX_SPATIAL_VALUES 9U

static legacy_u8 requested, enabled, suspended;
static struct SDL3_AUDIO_COMMAND *executing;

extern void audio_enable_music(void);
extern void audio_disable_music(void);
extern void audio_enable_effects(void);
extern void audio_disable_effects(void);
extern legacy_s16 audio_effect_channel_idle(legacy_s16 channel);
extern void audio_start_engine(legacy_s16 index);
extern void audio_stop_engine(legacy_s16 index);
extern void audio_stop_skid_sound(legacy_s16 index);
extern void audio_play_bump(legacy_s16 index);
extern void audio_play_scrape(legacy_s16 index);
extern void audio_play_paved_skid(legacy_s16 index);
extern void audio_play_offroad_skid(legacy_s16 index);
extern legacy_s16 audio_set_music_channel_volume(legacy_s16 value);
extern void audio_stop_effect_channel(legacy_s16 channel);
extern void audio_update_engine_spatial(legacy_s16, legacy_s16, legacy_s16, legacy_s16, legacy_s16,
										legacy_s16, legacy_s16, legacy_s16, legacy_s16);

legacy_u8 sdl3_audio_dispatch_is_owner(void)
{
#ifdef RESTUNTS_BROWSER_AUDIO_RUNTIME
	return 1;
#else
	return sdl3_audio_worker_is_worker();
#endif
}

legacy_u8 sdl3_audio_dispatch_needed(void)
{
	return enabled && !sdl3_audio_dispatch_is_owner();
}

legacy_u8 sdl3_audio_command_requested(void)
{
	return requested;
}
legacy_u8 sdl3_audio_command_active(void)
{
	return enabled;
}
void sdl3_audio_command_enable(legacy_u8 value)
{
	requested = value;
}
legacy_u8 sdl3_audio_command_callbacks_suspended(void)
{
	return suspended;
}

static legacy_u32 encode_pointer(const void *pointer)
{
	return ((legacy_u32)dos_memory_pointer_segment(pointer) << AUDIO_COMMAND_FAR_SEGMENT_SHIFT) |
		   dos_memory_pointer_offset(pointer);
}

static void *decode_pointer(legacy_u32 pointer)
{
	return dos_memory_make_pointer((legacy_u16)(pointer >> AUDIO_COMMAND_FAR_SEGMENT_SHIFT),
								   (legacy_u16)pointer);
}

void sdl3_audio_command_quiesce(legacy_u8 usage)
{
	if (usage & AUDIO_RESOURCE_USAGE_MUSIC) {
		audio_stop_music();
	}
	if (usage & AUDIO_RESOURCE_USAGE_ENGINE) {
		audio_remove_driver_timer();
		for (legacy_s16 channel = AUDIO_EFFECT_CHANNEL_FIRST;
			 (legacy_u16)channel <= AUDIO_EFFECT_CHANNEL_LAST; channel++) {
			audio_stop_effect_channel(channel);
		}
	}
}

void sdl3_audio_command_execute(void *argument)
{
	struct SDL3_AUDIO_COMMAND *command = argument;
	struct SDL3_AUDIO_COMMAND *previous = executing;
	executing = command;
	const legacy_s32 *a = command->args;
	command->result = 0;
	command->failed = 0;
	/* External commands never enter the legacy engine with an invalid handle.
	 * Engine initialization failures are acknowledged before main-thread UI. */
	if (command->op == AUDIO_COMMAND_ENGINE_SPATIAL ||
		(command->op >= AUDIO_COMMAND_START_ENGINE &&
		 command->op <= AUDIO_COMMAND_PLAY_OFFROAD_SKID)) {
		if ((legacy_u32)a[0] >= AUDIO_TIMER_COUNT) {
			command->failed = 1;
			executing = previous;
			return;
		}
		if (!audio_timers[a[0]].active) {
			executing = previous;
			return;
		}
	}
	switch (command->op) {
		case AUDIO_COMMAND_CONFIGURE:
			sdl3_audio_resources_set_execution(0);
			sdl3_audio_configure((legacy_u8)a[0], "/roms", command->text, (legacy_u8)a[1]);
			dos_timer_setup_interrupt();
			requested = 1;
			command->result = audio_load_dos_driver("ad15.drv", 0, 0);
			command->failed = command->result != 0;
			sdl3_audio_resources_set_execution(1);
			break;
		case AUDIO_COMMAND_INIT_RESOURCES: {
			void *song = sdl3_audio_resource_clone_key(
				command->keys[0], command->data[0], command->bytes[0], AUDIO_RESOURCE_USAGE_MUSIC);
			void *voices = sdl3_audio_resource_clone_key(
				command->keys[1], command->data[1], command->bytes[1], AUDIO_RESOURCE_USAGE_MUSIC);
			void *header = song && voices ? init_audio_resources(song, voices,
																 (const legacy_s8 *)command->text)
										  : NULL;
			command->pointer_result = header ? encode_pointer(header) : 0;
			command->failed = header == NULL;
			break;
		}
		case AUDIO_COMMAND_FINALIZE:
			load_audio_finalize(decode_pointer((legacy_u32)a[0]));
			command->flush = 1;
			break;
		case AUDIO_COMMAND_INIT_ENGINE: {
			void *voices = sdl3_audio_resource_clone_key(
				command->keys[0], command->data[0], command->bytes[0], AUDIO_RESOURCE_USAGE_ENGINE);
			void *effects = sdl3_audio_resource_clone_key(
				command->keys[1], command->data[1], command->bytes[1], AUDIO_RESOURCE_USAGE_ENGINE);
			struct FULL_AUDIO_ENGINE_DEFINITION definition;
			memset(&definition, 0, sizeof(definition));
			if (!voices || !effects || command->bytes[2] != AUDIO_COMMAND_ENGINE_WIRE_BYTES) {
				command->failed = 1;
				command->result = -1;
				break;
			}
			const legacy_u8 *wire = command->data[2];
			definition.sample_count = LEGACY_READ_U16_LE(wire);
			memcpy(definition.reserved_parameters, wire + AUDIO_COMMAND_ENGINE_PARAMETERS_OFFSET,
				   AUDIO_ENGINE_RESERVED_PARAMETER_SIZE);
			definition.initialized = wire[AUDIO_COMMAND_ENGINE_INITIALIZED_OFFSET];
			definition.reserved_initialization_byte = wire[AUDIO_COMMAND_ENGINE_RESERVED_OFFSET];
			for (size_t index = 0; index < AUDIO_ENGINE_RESOURCE_COUNT; index++) {
				definition.resource_ids[index] = (const legacy_s8 *)wire +
												 AUDIO_COMMAND_ENGINE_HEADER_BYTES +
												 index * AUDIO_COMMAND_ENGINE_ID_BYTES;
			}
			legacy_u8 available = 0;
			for (size_t index = 0; index < AUDIO_TIMER_COUNT; index++) {
				available |= !audio_timers[index].active;
			}
			/* The legacy resolver is fatal for a missing primary instrument, and
		 * its error handler draws UI. Validate before invoking it on the owner. */
			if (!available || definition.initialized != 0 ||
				locate_shape_nofatal(voices, definition.resource_ids[0]) == NULL) {
				command->failed = 1;
				command->result = -1;
				break;
			}
			command->result = audio_init_engine((legacy_s16)a[0], &definition, voices, effects);
			command->failed = command->result < 0;
			break;
		}
		case AUDIO_COMMAND_ENGINE_SPATIAL:
			audio_update_engine_spatial((legacy_s16)a[0], (legacy_s16)a[1], (legacy_s16)a[2],
										(legacy_s16)a[3], (legacy_s16)a[4], (legacy_s16)a[5],
										(legacy_s16)a[6], (legacy_s16)a[7], (legacy_s16)a[8]);
			break;
		case AUDIO_COMMAND_RELEASE_RESOURCE:
			sdl3_audio_resource_release_key(command->keys[0]);
			break;
		case AUDIO_COMMAND_ENABLE_MUSIC:
			audio_enable_music();
			break;
		case AUDIO_COMMAND_DISABLE_MUSIC:
			audio_disable_music();
			break;
		case AUDIO_COMMAND_TOGGLE_MUSIC:
			command->result = audio_toggle_music();
			break;
		case AUDIO_COMMAND_STOP_MUSIC:
			audio_stop_music();
			command->flush = 1;
			break;
		case AUDIO_COMMAND_ENABLE_EFFECTS:
			audio_enable_effects();
			break;
		case AUDIO_COMMAND_DISABLE_EFFECTS:
			audio_disable_effects();
			break;
		case AUDIO_COMMAND_TOGGLE_EFFECTS:
			command->result = audio_toggle_effects();
			break;
		case AUDIO_COMMAND_EFFECT_CHANNEL_IDLE:
			command->result = audio_effect_channel_idle((legacy_s16)a[0]);
			break;
		case AUDIO_COMMAND_RESET_CHANNELS:
			audio_reset_channels();
			break;
		case AUDIO_COMMAND_SUSPEND:
			audio_suspend();
			command->flush = 1;
			break;
		case AUDIO_COMMAND_RESUME:
			audio_resume();
			break;
		case AUDIO_COMMAND_ADD_DRIVER_TIMER:
			audio_add_driver_timer();
			break;
		case AUDIO_COMMAND_REMOVE_DRIVER_TIMER:
			audio_remove_driver_timer();
			break;
		case AUDIO_COMMAND_START_ENGINE:
			audio_start_engine((legacy_s16)a[0]);
			break;
		case AUDIO_COMMAND_STOP_ENGINE:
			audio_stop_engine((legacy_s16)a[0]);
			break;
		case AUDIO_COMMAND_STOP_SKID_SOUND:
			audio_stop_skid_sound((legacy_s16)a[0]);
			break;
		case AUDIO_COMMAND_PLAY_BUMP:
			audio_play_bump((legacy_s16)a[0]);
			break;
		case AUDIO_COMMAND_PLAY_SCRAPE:
			audio_play_scrape((legacy_s16)a[0]);
			break;
		case AUDIO_COMMAND_PLAY_CRASH_AND_STOP_ENGINE:
			audio_play_crash_and_stop_engine((legacy_s16)a[0]);
			break;
		case AUDIO_COMMAND_PLAY_PAVED_SKID:
			audio_play_paved_skid((legacy_s16)a[0]);
			break;
		case AUDIO_COMMAND_PLAY_OFFROAD_SKID:
			audio_play_offroad_skid((legacy_s16)a[0]);
			break;
		case AUDIO_COMMAND_SET_MUSIC_CHANNEL_VOLUME:
			command->result = audio_set_music_channel_volume((legacy_s16)a[0]);
			break;
		case AUDIO_COMMAND_FADE_STEP:
			if (a[1]) {
				dos_audio_master_volume = (legacy_u8)a[0];
				dos_audio_driver_set_master_state(AUDIO_COMMAND_MASTER_STATE_OPERATION,
												  dos_audio_master_state);
			} else {
				audio_set_music_channel_volume((legacy_s16)a[0]);
			}
			break;
		case AUDIO_COMMAND_OGG_SET_ENABLED:
			sdl3_music_set_enabled(a[0]);
			break;
		case AUDIO_COMMAND_OGG_TOGGLE:
			command->result = sdl3_music_toggle();
			command->flush = 1;
			break;
		case AUDIO_COMMAND_OGG_GET_ENABLED:
			command->result = sdl3_music_get_enabled();
			break;
		case AUDIO_COMMAND_OGG_SYNC:
			sdl3_music_sync();
			break;
		case AUDIO_COMMAND_OGG_SET_VOLUME:
			sdl3_music_set_volume((legacy_s16)a[0]);
			break;
		case AUDIO_COMMAND_CALLBACKS_SUSPENDED:
			suspended = a[0] != 0;
			sdl3_audio_worker_set_suspended(suspended);
			sdl3_music_sync();
			command->flush = 1;
			break;
		case AUDIO_COMMAND_SET_FINISH_CALLBACK:
			sdl3_audio_completion_register((legacy_s16)a[0], a[1], (legacy_u32)a[2]);
			break;
		case AUDIO_COMMAND_SHUTDOWN:
			sdl3_audio_resources_reset();
			dos_audio_shutdown();
			break;
		default:
			command->failed = 1;
			break;
	}
	command->legacy_hihat = legacy_closed_hihat_offset;
	executing = previous;
}

void sdl3_audio_command_impact(void)
{
	if (executing != NULL && audio_suspended == AUDIO_STATE_DISABLED) {
		executing->impact = 1;
	}
}

#if defined(__EMSCRIPTEN__) && !defined(RESTUNTS_BROWSER_AUDIO_RUNTIME)
EM_ASYNC_JS(
	void, browser_audio_command,
	(legacy_u32 p, legacy_u32 ao, legacy_u32 ko, legacy_u32 po, legacy_u32 bo, legacy_u32 to,
	 legacy_u32 ro, legacy_u32 pro, legacy_u32 ho, legacy_u32 io, legacy_u32 fo, legacy_u32 eo),
	{
		try {
			const args = Array.from(HEAP32.subarray((p + ao) >> 2, ((p + ao) >> 2) + 12));
			const resources = [];
			for (let index = 0; index < 3; index++) {
				const length = HEAPU32[((p + bo) >> 2) + index];
				const key = HEAPU32[((p + ko) >> 2) + index * 2];
				if (length || key) {
					const pointer = HEAPU32[((p + po) >> 2) + index];
					resources.push(
						{id : key, slot : index, bytes : HEAPU8.slice(pointer, pointer + length)});
				}
			}
			await Module['restuntsBrowserAudio'].command(HEAPU32[p >> 2], args, resources,
														 UTF8ToString(p + to));
			const response = Module['lastAudioResponse'];
			HEAP32[(p + ro) >> 2] = response.result || 0;
			HEAPU32[(p + pro) >> 2] = response.pointer_result || 0;
			HEAPU16[(p + ho) >> 1] = response.legacy_hihat || 0;
			HEAPU8[p + io] = response.impact ? 1 : 0;
			HEAPU8[p + fo] = response.flush ? 1 : 0;
		} catch (error) {
			HEAPU8[p + eo] = 1;
			Module['printErr'](error.message || String(error));
		}
	});
#endif

void sdl3_audio_command_send(struct SDL3_AUDIO_COMMAND *command)
{
#if defined(__EMSCRIPTEN__) && !defined(RESTUNTS_BROWSER_AUDIO_RUNTIME)
	browser_audio_command(
		(legacy_u32)(uintptr_t)command, offsetof(struct SDL3_AUDIO_COMMAND, args),
		offsetof(struct SDL3_AUDIO_COMMAND, keys), offsetof(struct SDL3_AUDIO_COMMAND, data),
		offsetof(struct SDL3_AUDIO_COMMAND, bytes), offsetof(struct SDL3_AUDIO_COMMAND, text),
		offsetof(struct SDL3_AUDIO_COMMAND, result),
		offsetof(struct SDL3_AUDIO_COMMAND, pointer_result),
		offsetof(struct SDL3_AUDIO_COMMAND, legacy_hihat),
		offsetof(struct SDL3_AUDIO_COMMAND, impact), offsetof(struct SDL3_AUDIO_COMMAND, flush),
		offsetof(struct SDL3_AUDIO_COMMAND, failed));
	legacy_closed_hihat_offset = command->legacy_hihat;
#else
	sdl3_audio_worker_call(sdl3_audio_command_execute, command);
#endif
	if (command->impact && command->args[0] == audio_player_engine_channel && is_in_replay == 0 &&
		state.game_inputmode == GAME_INPUT_MODE_ACTIVE) {
		sdl3_car_impact_feedback();
	}
	if (command->failed) {
		SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio command failed: %s",
					 sdl3_audio_resources_error());
	}
}

legacy_s32 sdl3_audio_command_scalars(legacy_u32 op, const legacy_s32 *args, size_t count)
{
	struct SDL3_AUDIO_COMMAND command;
	memset(&command, 0, sizeof(command));
	command.op = op;
	if (count > SDL3_AUDIO_COMMAND_ARGUMENTS) {
		return -1;
	}
	if (count != 0) {
		memcpy(command.args, args, count * sizeof(*args));
	}
	sdl3_audio_command_send(&command);
	return command.result;
}

static void command_resource(struct SDL3_AUDIO_COMMAND *command, size_t index, const void *resource)
{
	command->keys[index] = (legacy_u64)(uintptr_t)resource;
	command->data[index] = resource;
	command->bytes[index] = mmgr_get_chunk_size_bytes((legacy_s8 *)resource);
}

void *sdl3_audio_command_prepare(void *song, void *voices, const legacy_s8 *name)
{
	struct SDL3_AUDIO_COMMAND command;
	memset(&command, 0, sizeof(command));
	command.op = AUDIO_COMMAND_INIT_RESOURCES;
	command_resource(&command, 0, song);
	command_resource(&command, 1, voices);
	SDL_strlcpy(command.text, (const legacy_char *)name, sizeof(command.text));
	sdl3_audio_command_send(&command);
	return (void *)(uintptr_t)command.pointer_result;
}

void sdl3_audio_command_finalize(void *resource)
{
	const legacy_s32 args[] = {(legacy_s32)(uintptr_t)resource};
	sdl3_audio_command_scalars(AUDIO_COMMAND_FINALIZE, args, 1);
}

legacy_s16 sdl3_audio_command_engine(legacy_s16 type, const void *source, void *voices,
									 void *effects)
{
	struct SDL3_AUDIO_COMMAND command;
	legacy_u8 wire[AUDIO_COMMAND_ENGINE_WIRE_BYTES];
	const struct FULL_AUDIO_ENGINE_DEFINITION *definition = source;
	memset(&command, 0, sizeof(command));
	memset(wire, 0, sizeof(wire));
	command.op = AUDIO_COMMAND_INIT_ENGINE;
	command.args[0] = type;
	command_resource(&command, 0, voices);
	command_resource(&command, 1, effects);
	LEGACY_WRITE_U16_LE(wire, definition->sample_count);
	memcpy(wire + AUDIO_COMMAND_ENGINE_PARAMETERS_OFFSET, definition->reserved_parameters,
		   AUDIO_ENGINE_RESERVED_PARAMETER_SIZE);
	wire[AUDIO_COMMAND_ENGINE_INITIALIZED_OFFSET] = definition->initialized;
	wire[AUDIO_COMMAND_ENGINE_RESERVED_OFFSET] = definition->reserved_initialization_byte;
	for (size_t index = 0; index < AUDIO_ENGINE_RESOURCE_COUNT; index++) {
		SDL_strlcpy((legacy_char *)wire + AUDIO_COMMAND_ENGINE_HEADER_BYTES +
						index * AUDIO_COMMAND_ENGINE_ID_BYTES,
					(const legacy_char *)definition->resource_ids[index],
					AUDIO_COMMAND_ENGINE_ID_BYTES);
	}
	command.data[2] = wire;
	command.bytes[2] = sizeof(wire);
	sdl3_audio_command_send(&command);
	return (legacy_s16)command.result;
}

void sdl3_audio_command_spatial(const legacy_s16 *values)
{
	legacy_s32 args[AUDIO_COMMAND_MAX_SPATIAL_VALUES];
	for (size_t index = 0; index < AUDIO_COMMAND_MAX_SPATIAL_VALUES; index++) {
		args[index] = values[index];
	}
	sdl3_audio_command_scalars(AUDIO_COMMAND_ENGINE_SPATIAL, args,
							   AUDIO_COMMAND_MAX_SPATIAL_VALUES);
}

void sdl3_audio_command_release(const void *source)
{
	if (!sdl3_audio_dispatch_needed()) {
		return;
	}
	struct SDL3_AUDIO_COMMAND command;
	memset(&command, 0, sizeof(command));
	command.op = AUDIO_COMMAND_RELEASE_RESOURCE;
	command.keys[0] = (legacy_u64)(uintptr_t)source;
	sdl3_audio_command_send(&command);
}

legacy_int sdl3_audio_command_start(void)
{
	if (!requested || sdl3_batch_mode) {
		return 1;
	}
	legacy_u8 mode, disabled;
	const legacy_char *directory, *model;
	sdl3_audio_get_configuration(&mode, &disabled, &directory, &model);
	if (disabled) {
		return 1;
	}
#if !defined(__EMSCRIPTEN__)
	if (sdl3_audio_queued_frames() < 0) {
		return 1;
	}
#endif
	sdl3_audio_clear_queued();
	sdl3_audio_resources_set_quiesce(sdl3_audio_command_quiesce);
	timer_remove_callback(audio_sequence_timer);
	sdl3_audio_worker_register_callback(audio_sequence_timer);
	enabled = 1;
#if !defined(__EMSCRIPTEN__)
	if (!sdl3_audio_worker_start()) {
		enabled = 0;
		return 0;
	}
#endif
	return 1;
}

legacy_u8 sdl3_audio_command_shutdown(void)
{
	if (!sdl3_audio_dispatch_needed()) {
		return 0;
	}
	struct SDL3_AUDIO_COMMAND command;
	memset(&command, 0, sizeof(command));
	command.op = AUDIO_COMMAND_SHUTDOWN;
	sdl3_audio_command_send(&command);
	sdl3_audio_worker_stop();
	sdl3_audio_completions_clear();
	enabled = 0;
	sdl3_audio_worker_unregister_callback(audio_sequence_timer);
	if (SDL_getenv("RESTUNTS_AUDIO_DIAGNOSTICS") != NULL) {
		struct SDL3_AUDIO_WORKER_STATS statistics;
		sdl3_audio_worker_get_stats(&statistics);
		SDL_Log("Audio worker: ticks=%" LEGACY_PRIu64 ", commands=%" LEGACY_PRIu64
				", queue-empty observations=%" LEGACY_PRIu64 ", late wakes=%" LEGACY_PRIu64
				", maximum queued frames=%" LEGACY_PRIu32,
				statistics.ticks, statistics.commands, statistics.underruns,
				statistics.late_wakeups, statistics.max_queued_frames);
	}
	return 1;
}

/* Completion callbacks run only while the game thread pumps its events. */
void sdl3_audio_command_pump(void)
{
	sdl3_audio_completions_pump();
}
void sdl3_audio_command_tick(void)
{
	sdl3_audio_update();
}

#if defined(__EMSCRIPTEN__) && !defined(RESTUNTS_BROWSER_AUDIO_RUNTIME)
EM_ASYNC_JS(legacy_int, browser_audio_prepare_files, (const legacy_char *directory), {
	try {
		await Module['restuntsBrowserAudio'].prepareFiles(directory ? UTF8ToString(directory) : "");
		return 1;
	} catch (error) {
		Module['printErr'](error.message || String(error));
		return 0;
	}
});
#endif

legacy_int sdl3_audio_command_browser_load(void)
{
#if defined(__EMSCRIPTEN__) && !defined(RESTUNTS_BROWSER_AUDIO_RUNTIME)
	legacy_u8 mode, disabled;
	const legacy_char *directory, *model;
	sdl3_audio_get_configuration(&mode, &disabled, &directory, &model);
	if (!browser_audio_prepare_files(mode == STARTUP_SOUND_MT32 ? directory : NULL)) {
		return AUDIO_COMMAND_DRIVER_LOAD_FAILURE;
	}
	struct SDL3_AUDIO_COMMAND command;
	memset(&command, 0, sizeof(command));
	command.op = AUDIO_COMMAND_CONFIGURE;
	command.args[0] = mode;
	command.args[1] = disabled;
	SDL_strlcpy(command.text, model ? model : "", sizeof(command.text));
	sdl3_audio_command_send(&command);
	enabled = !command.failed;
	return command.failed ? AUDIO_COMMAND_DRIVER_LOAD_FAILURE : 0;
#else
	return 0;
#endif
}
