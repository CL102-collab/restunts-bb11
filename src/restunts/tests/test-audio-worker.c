/* Exercise production audio commands and real worker/device clocks. A main-thread
 * stall must not stall music, PCM production or a previously published engine. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include "../c/audio/audio_control.h"
#include "../c/audio/audio_internal.h"
#include "../c/car/car_audio.h"
#include "../c/memmgr.h"
#include "../c/platform.h"
#include "../c/startup_internal.h"
#include "../platform/sdl3/audio_commands.h"
#include "../platform/sdl3/audio_resources_worker.h"
#include "../platform/sdl3/audio_worker.h"
#include "../platform/sdl3/pc_speaker_audio.h"
#include "../platform/sdl3/mt32_audio.h"
#include "../platform/sdl3/sdl3.h"
#ifdef RESTUNTS_MT32
#include <mt32emu/mt32emu.h>
#endif

#define TEST_EXPECTED_ARGUMENTS 2
#define TEST_ROM_ARGUMENTS 4
#define TEST_PATH_BYTES 1024U
#define TEST_MAIN_STALL_MS 350U
#define TEST_PAUSE_MS 250U
#define TEST_RESUME_MS 120U
#define TEST_WARMUP_MS 80U
#define TEST_ENGINE_SETTLE_MS 650U
#define TEST_ENGINE_CHANGE_MS 160U
#define TEST_STOP_IDLE_MS 80U
#define TEST_MS_PER_SECOND 1000U
#define TEST_TICK_MARGIN 12U
#define TEST_QUEUE_MARGIN_FRAMES (SDL3_AUDIO_WORKER_TICK_FRAMES * 2U)
#define TEST_ENGINE_LOW_RPM 1600
#define TEST_ENGINE_HIGH_RPM 6400
#define TEST_UNPUBLISHED_BYTE 0xa5
#define TEST_RELEASED_BYTE 0xdd
#define TEST_NO_ENGINE (-1)
#define TEST_ENGINE_WIRE_HEADER_BYTES 8U
#define TEST_ENGINE_WIRE_ID_BYTES 5U
#define TEST_ENGINE_WIRE_BYTES                                                                     \
	(TEST_ENGINE_WIRE_HEADER_BYTES + AUDIO_ENGINE_RESOURCE_COUNT * TEST_ENGINE_WIRE_ID_BYTES)
#define TEST_ENGINE_WIRE_INITIALIZED_OFFSET 6U

extern void audio_start_engine(legacy_s16 index);
extern void audio_update_engine_spatial(legacy_s16 index, legacy_s16 rpm, legacy_s16 first_x,
										legacy_s16 first_y, legacy_s16 first_z, legacy_s16 second_x,
										legacy_s16 second_y, legacy_s16 second_z,
										legacy_s16 interval);

struct worker_snapshot {
	struct AUDIO_CHANNEL channels[AUDIO_CHANNEL_COUNT];
	struct AUDIO_CONTEXT contexts[AUDIO_CONTEXT_COUNT];
	struct AUDIO_TIMER engine;
	legacy_s16 engine_index;
	legacy_u8 engine_voice_active;
	legacy_u8 engine_note;
	legacy_u8 music_active;
	legacy_u8 music_channels;
	legacy_u8 active_timers;
	legacy_u8 active_voices;
	legacy_u8 sample_owned;
	legacy_u8 mt32_partials_active;
};

static SDL_ThreadID main_thread;
static legacy_u32 gameplay_callbacks;
static legacy_u8 test_sound_mode;

static void gameplay_callback(void)
{
	assert(SDL_GetCurrentThreadID() == main_thread);
	gameplay_callbacks++;
}

static void snapshot_on_worker(void *argument)
{
	struct worker_snapshot *snapshot = argument;
	assert(sdl3_audio_worker_is_worker());
	assert(sdl3_audio_resources_is_execution());
	memcpy(snapshot->channels, audio_channels, sizeof(snapshot->channels));
	memcpy(snapshot->contexts, dos_audio_contexts, sizeof(snapshot->contexts));
	snapshot->music_active = audio_music_active;
	snapshot->music_channels = audio_music_channel_count;
	snapshot->active_timers = 0;
	for (size_t index = 0; index < AUDIO_TIMER_COUNT; index++) {
		snapshot->active_timers += audio_timers[index].active != 0;
	}
	snapshot->active_voices = 0;
	for (size_t index = 0; index < AUDIO_CONTEXT_COUNT; index++) {
		snapshot->active_voices += dos_audio_contexts[index].state != AUDIO_CONTEXT_STATE_FREE;
	}
	if (test_sound_mode == STARTUP_SOUND_PC_SPEAKER) {
		snapshot->sample_owned = pc_speaker_audio_driver()->sample_data != NULL;
	}
#ifdef RESTUNTS_MT32
	if (test_sound_mode == STARTUP_SOUND_MT32) {
		snapshot->mt32_partials_active = mt32emu_has_active_partials(mt32_audio_driver()->instance);
	}
#endif
	if (snapshot->engine_index >= 0) {
		assert((legacy_u16)snapshot->engine_index < AUDIO_TIMER_COUNT);
		snapshot->engine = audio_timers[snapshot->engine_index];
		legacy_s16 context = snapshot->engine.engine_context;
		assert(context >= 0 && (legacy_u16)context < AUDIO_CONTEXT_COUNT);
		snapshot->engine_voice_active =
			dos_audio_contexts[context].state != AUDIO_CONTEXT_STATE_FREE;
		if (test_sound_mode == STARTUP_SOUND_PC_SPEAKER) {
			legacy_u8 driver_channel = dos_audio_contexts[context].driver_channel;
			assert(driver_channel < PC_SPEAKER_CONTEXT_COUNT);
			snapshot->engine_note = pc_speaker_audio_driver()->voices[driver_channel].note;
		}
	}
}

static struct worker_snapshot snapshot(legacy_s16 engine)
{
	struct worker_snapshot result;
	memset(&result, 0, sizeof(result));
	result.engine_index = engine;
	assert(sdl3_audio_worker_call(snapshot_on_worker, &result));
	return result;
}

static void check_queue(const struct SDL3_AUDIO_WORKER_STATS *statistics)
{
	assert(statistics->active);
	assert(statistics->queued_frames > 0);
	if (statistics->max_queued_frames > SDL3_AUDIO_WORKER_LEAD_FRAMES + TEST_QUEUE_MARGIN_FRAMES) {
		fprintf(stderr,
				"Worker queue: %" LEGACY_PRId32 " current, %" LEGACY_PRIu32
				" maximum frames after %" LEGACY_PRIu64 " ticks.\n",
				statistics->queued_frames, statistics->max_queued_frames, statistics->ticks);
	}
	assert(statistics->max_queued_frames <=
		   SDL3_AUDIO_WORKER_LEAD_FRAMES + TEST_QUEUE_MARGIN_FRAMES);
}

static void check_progress(const struct SDL3_AUDIO_WORKER_STATS *before,
						   const struct SDL3_AUDIO_WORKER_STATS *after, legacy_u64 elapsed_ms)
{
	legacy_u64 expected = elapsed_ms * DOS_TIMER_REALTIME_TICKS_PER_SECOND / TEST_MS_PER_SECOND;
	legacy_u64 elapsed_ticks = after->ticks - before->ticks;
	legacy_u64 callback_ticks = after->callback_ticks - before->callback_ticks;
	if (elapsed_ticks + TEST_TICK_MARGIN < expected ||
		elapsed_ticks > expected + TEST_TICK_MARGIN ||
		callback_ticks + TEST_TICK_MARGIN < expected) {
		fprintf(stderr,
				"Worker stall: %" LEGACY_PRIu64 " ms, %" LEGACY_PRIu64
				" audio ticks, %" LEGACY_PRIu64 " callback ticks.\n",
				elapsed_ms, elapsed_ticks, callback_ticks);
	}
	assert(elapsed_ticks + TEST_TICK_MARGIN >= expected);
	assert(elapsed_ticks <= expected + TEST_TICK_MARGIN);
	assert(callback_ticks + TEST_TICK_MARGIN >= expected);
	check_queue(after);
}

static void copy_resources(const legacy_char *directory, legacy_u8 include_mt32)
{
	static const legacy_char *const names[] = {
		"PCSKIDMS.VCE", "PCENG1.VCE",	"ADSKIDMS.VCE", "ADENG1.VCE", "GEENG.SFX", "SKIDTITL.KMS",
		"SKIDSLCT.KMS", "SKIDOVER.KMS", "MTSKIDMS.VCE", "MTENG1.VCE", "MT32.PLB"};
	for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
		if (!include_mt32 && names[index][0] == 'M' && names[index][1] == 'T') {
			continue;
		}
		legacy_char source[TEST_PATH_BYTES];
		legacy_int length = SDL_snprintf(source, sizeof(source), "%s/%s", directory, names[index]);
		assert(length > 0 && (size_t)length < sizeof(source));
		assert(SDL_CopyFile(source, names[index]));
	}
}

static void check_main_stall(void)
{
	struct worker_snapshot before = snapshot(TEST_NO_ENGINE);
	assert(before.music_active && before.music_channels != 0);
	struct SDL3_AUDIO_WORKER_STATS start, finish;
	sdl3_audio_worker_get_stats(&start);
	legacy_u32 main_callbacks_before = gameplay_callbacks;
	legacy_u64 start_ms = SDL_GetTicks();
	SDL_Delay(TEST_MAIN_STALL_MS);
	/* Read counters before any dispatched snapshot could wake the worker. */
	sdl3_audio_worker_get_stats(&finish);
	legacy_u64 elapsed_ms = SDL_GetTicks() - start_ms;
	assert(gameplay_callbacks == main_callbacks_before);
	check_progress(&start, &finish, elapsed_ms);
	struct worker_snapshot after = snapshot(TEST_NO_ENGINE);
	assert(after.music_active);
	if (test_sound_mode == STARTUP_SOUND_MT32) {
		assert(after.mt32_partials_active);
	}
	assert(memcmp(before.channels, after.channels, sizeof(before.channels)) != 0 ||
		   memcmp(before.contexts, after.contexts, sizeof(before.contexts)) != 0);
	sdl3_timer_pump();
	assert(gameplay_callbacks > main_callbacks_before);
}

static void check_pause_resume(void)
{
	dos_timer_set_callbacks_suspended(1);
	struct worker_snapshot before = snapshot(TEST_NO_ENGINE);
	struct SDL3_AUDIO_WORKER_STATS start, finish;
	sdl3_audio_worker_get_stats(&start);
	assert(start.suspended);
	SDL_Delay(TEST_PAUSE_MS);
	sdl3_audio_worker_get_stats(&finish);
	assert(finish.callback_ticks == start.callback_ticks && finish.ticks > start.ticks);
	struct worker_snapshot after = snapshot(TEST_NO_ENGINE);
	assert(memcmp(before.channels, after.channels, sizeof(before.channels)) == 0);
	assert(memcmp(before.contexts, after.contexts, sizeof(before.contexts)) == 0);
	dos_timer_set_callbacks_suspended(0);
	sdl3_audio_worker_get_stats(&start);
	legacy_u64 start_ms = SDL_GetTicks();
	SDL_Delay(TEST_RESUME_MS);
	sdl3_audio_worker_get_stats(&finish);
	check_progress(&start, &finish, SDL_GetTicks() - start_ms);
	/* The suspended quarter second must not be replayed on resume. */
	assert(finish.callback_ticks - start.callback_ticks <=
		   TEST_RESUME_MS * DOS_TIMER_REALTIME_TICKS_PER_SECOND / TEST_MS_PER_SECOND +
			   TEST_TICK_MARGIN);
}

static void initialize_engine_command(struct SDL3_AUDIO_COMMAND *command, legacy_u8 *wire,
									  void *voices, void *effects)
{
	memset(command, 0, sizeof(*command));
	memset(wire, 0, TEST_ENGINE_WIRE_BYTES);
	command->op = AUDIO_COMMAND_INIT_ENGINE;
	command->args[0] = PLAYER_ENGINE_LEGACY_TYPE;
	command->keys[0] = (legacy_u64)(uintptr_t)voices;
	command->keys[1] = (legacy_u64)(uintptr_t)effects;
	command->data[0] = voices;
	command->data[1] = effects;
	command->data[2] = wire;
	command->bytes[0] = mmgr_get_chunk_size_bytes(voices);
	command->bytes[1] = mmgr_get_chunk_size_bytes(effects);
	command->bytes[2] = TEST_ENGINE_WIRE_BYTES;
	LEGACY_WRITE_U16_LE(wire, player_engine_definition.sample_count);
	memcpy(wire + 2, player_engine_definition.reserved_parameters,
		   AUDIO_ENGINE_RESERVED_PARAMETER_SIZE);
	for (size_t index = 0; index < AUDIO_ENGINE_RESOURCE_COUNT; index++) {
		SDL_strlcpy((legacy_char *)wire + TEST_ENGINE_WIRE_HEADER_BYTES +
						index * TEST_ENGINE_WIRE_ID_BYTES,
					(const legacy_char *)player_engine_definition.resource_ids[index],
					TEST_ENGINE_WIRE_ID_BYTES);
	}
}

static void check_engine_command_failures(void)
{
	void *voices = load_voice_file("eng1");
	void *effects = load_sfx_file("eng");
	assert(voices != NULL && effects != NULL);
	struct SDL3_AUDIO_COMMAND command;
	legacy_u8 wire[TEST_ENGINE_WIRE_BYTES];
	initialize_engine_command(&command, wire, voices, effects);
	memcpy(wire + TEST_ENGINE_WIRE_HEADER_BYTES, "NONE", TEST_ENGINE_WIRE_ID_BYTES);
	/* Missing instruments must be acknowledged without entering fatal game UI. */
	assert(sdl3_audio_worker_call(sdl3_audio_command_execute, &command));
	assert(command.failed && command.result == -1);
	assert(snapshot(TEST_NO_ENGINE).active_timers == 0);

	initialize_engine_command(&command, wire, voices, effects);
	wire[TEST_ENGINE_WIRE_INITIALIZED_OFFSET] = 1;
	assert(sdl3_audio_worker_call(sdl3_audio_command_execute, &command));
	assert(command.failed && command.result == -1);
	assert(snapshot(TEST_NO_ENGINE).active_timers == 0);

	for (legacy_u32 index = 0; index < AUDIO_TIMER_COUNT; index++) {
		initialize_engine_command(&command, wire, voices, effects);
		assert(sdl3_audio_worker_call(sdl3_audio_command_execute, &command));
		assert(!command.failed && command.result == (legacy_s32)index);
	}
	assert(snapshot(TEST_NO_ENGINE).active_timers == AUDIO_TIMER_COUNT);
	initialize_engine_command(&command, wire, voices, effects);
	assert(sdl3_audio_worker_call(sdl3_audio_command_execute, &command));
	assert(command.failed && command.result == -1);
	assert(snapshot(TEST_NO_ENGINE).active_timers == AUDIO_TIMER_COUNT);
	mmgr_release(effects);
	mmgr_release(voices);
	assert(snapshot(TEST_NO_ENGINE).active_timers == 0);

	static const legacy_u32 operations[] = {AUDIO_COMMAND_ENGINE_SPATIAL,
											AUDIO_COMMAND_START_ENGINE,
											AUDIO_COMMAND_STOP_ENGINE,
											AUDIO_COMMAND_STOP_SKID_SOUND,
											AUDIO_COMMAND_PLAY_BUMP,
											AUDIO_COMMAND_PLAY_SCRAPE,
											AUDIO_COMMAND_PLAY_CRASH_AND_STOP_ENGINE,
											AUDIO_COMMAND_PLAY_PAVED_SKID,
											AUDIO_COMMAND_PLAY_OFFROAD_SKID};
	for (size_t index = 0; index < sizeof(operations) / sizeof(operations[0]); index++) {
		const legacy_s32 invalid[] = {-1, AUDIO_TIMER_COUNT};
		for (size_t value = 0; value < sizeof(invalid) / sizeof(invalid[0]); value++) {
			memset(&command, 0, sizeof(command));
			command.op = operations[index];
			command.args[0] = invalid[value];
			assert(sdl3_audio_worker_call(sdl3_audio_command_execute, &command));
			assert(command.failed);
		}
		/* Valid but retired handles must not follow pointers into released banks. */
		memset(&command, 0, sizeof(command));
		command.op = operations[index];
		assert(sdl3_audio_worker_call(sdl3_audio_command_execute, &command));
		assert(!command.failed);
	}
	assert(snapshot(TEST_NO_ENGINE).active_timers == 0);
	assert(sdl3_audio_worker_is_active());
}

static void check_engine(void)
{
	void *voices = load_voice_file("eng1");
	void *effects = load_sfx_file("eng");
	assert(voices != NULL && effects != NULL);
	size_t effects_bytes = mmgr_get_chunk_size_bytes(effects);
	audio_add_driver_timer();
	legacy_s16 engine =
		audio_init_engine(PLAYER_ENGINE_LEGACY_TYPE, &player_engine_definition, voices, effects);
	assert(engine >= 0);
	audio_start_engine(engine);
	audio_update_engine_spatial(engine, TEST_ENGINE_LOW_RPM, 0, 0, 0, 0, 0, 0, 1);
	SDL_Delay(TEST_ENGINE_SETTLE_MS);
	struct worker_snapshot before = snapshot(engine);
	assert(before.engine.engine_active && before.engine.target_pitch != 0 &&
		   before.engine.target_volume != 0 && before.engine_voice_active);
	if (test_sound_mode == STARTUP_SOUND_PC_SPEAKER) {
		assert(before.engine_note == AUDIO_NOTE_USE_DRIVER_VALUE);
	}
	struct SDL3_AUDIO_WORKER_STATS start, finish;
	sdl3_audio_worker_get_stats(&start);
	legacy_u64 start_ms = SDL_GetTicks();
	SDL_Delay(TEST_MAIN_STALL_MS);
	sdl3_audio_worker_get_stats(&finish);
	check_progress(&start, &finish, SDL_GetTicks() - start_ms);
	struct worker_snapshot held = snapshot(engine);
	assert(held.engine.engine_active && held.engine.target_pitch == before.engine.target_pitch &&
		   held.engine.target_volume == before.engine.target_volume);
	assert(held.engine.current_pitch >= before.engine.current_pitch);
	audio_update_engine_spatial(engine, TEST_ENGINE_HIGH_RPM, 0, 0, 0, 0, 0, 0, 1);
	struct worker_snapshot changed = snapshot(engine);
	assert(changed.engine.target_pitch > held.engine.target_pitch);
	SDL_Delay(TEST_ENGINE_CHANGE_MS);
	struct worker_snapshot advanced = snapshot(engine);
	assert(advanced.engine.current_pitch > held.engine.current_pitch);
	/* Releasing a source bank must synchronously quiesce its private users. */
	mmgr_release(effects);
	memset(effects, TEST_RELEASED_BYTE, effects_bytes);
	struct worker_snapshot stopped = snapshot(TEST_NO_ENGINE);
	assert(stopped.active_timers == 0);
	mmgr_release(voices);
}

static void check_music_and_lifetime(void)
{
	static const legacy_s8 *const songs[] = {"skidtitl", "skidslct", "skidover"};
	static const legacy_s8 *const names[] = {"titl", "slct", "over"};
	void *voices = load_voice_file("skidms");
	assert(voices != NULL);
	for (size_t index = 0; index < sizeof(songs) / sizeof(songs[0]); index++) {
		void *song = load_song_file(songs[index]);
		assert(song != NULL);
		size_t bytes = mmgr_get_chunk_size_bytes(song);
		legacy_u8 *saved = malloc(bytes);
		assert(saved != NULL);
		memcpy(saved, song, bytes);
		void *prepared = init_audio_resources(song, voices, names[index]);
		assert(prepared != NULL);
		assert(memcmp(saved, song, bytes) == 0);
		/* The command has acknowledged an owned copy, not a borrowed bank. */
		memset(song, TEST_UNPUBLISHED_BYTE, bytes);
		load_audio_finalize(prepared);
		SDL_Delay(TEST_WARMUP_MS);
		check_main_stall();
		if (index == 0) {
			check_pause_resume();
			check_engine_command_failures();
			check_engine();
		}
		memcpy(song, saved, bytes);
		free(saved);
		/* Do not explicitly stop first: release itself must cancel playback. */
		mmgr_release(song);
		memset(song, TEST_RELEASED_BYTE, bytes);
		struct worker_snapshot stopped = snapshot(TEST_NO_ENGINE);
		assert(!stopped.music_active && stopped.music_channels == 0);
		assert(stopped.active_voices == 0 && !stopped.sample_owned);
		SDL_Delay(TEST_STOP_IDLE_MS);
		stopped = snapshot(TEST_NO_ENGINE);
		assert(!stopped.music_active && stopped.active_voices == 0);
	}
	mmgr_release(voices);
}

static void check_shutdown_restart(void)
{
	dos_audio_shutdown();
	struct SDL3_AUDIO_WORKER_STATS stopped, later;
	sdl3_audio_worker_get_stats(&stopped);
	assert(!stopped.active && !sdl3_audio_command_active());
	assert(pc_speaker_audio_driver()->sample_data == NULL);
	assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	SDL_Delay(TEST_STOP_IDLE_MS);
	sdl3_audio_worker_get_stats(&later);
	assert(later.ticks == stopped.ticks && later.commands == stopped.commands);
	/* A joined worker must permit the next audio session to initialize cleanly. */
	assert(audio_load_dos_driver("pc15.drv", 0, 0) == 0);
	SDL_Delay(TEST_WARMUP_MS);
	sdl3_audio_worker_get_stats(&later);
	check_queue(&later);
	void *voices = load_voice_file("skidms");
	void *song = load_song_file("skidtitl");
	assert(voices != NULL && song != NULL);
	void *prepared = init_audio_resources(song, voices, "titl");
	assert(prepared != NULL);
	load_audio_finalize(prepared);
	SDL_Delay(TEST_WARMUP_MS);
	struct worker_snapshot playing = snapshot(TEST_NO_ENGINE);
	assert(playing.music_active && playing.music_channels != 0);
	/* Shutdown must also quiesce a live private bank before joining. */
	dos_audio_shutdown();
	sdl3_audio_worker_get_stats(&later);
	assert(!later.active && SDL_WasInit(SDL_INIT_AUDIO) == 0);
	assert(pc_speaker_audio_driver()->sample_data == NULL);
	mmgr_release(song);
	mmgr_release(voices);
}

static void check_mode(legacy_u8 mode, const legacy_char *rom_directory, const legacy_char *model)
{
	test_sound_mode = mode;
	mmgr_init_conventional_arena();
	dos_timer_setup_interrupt();
	assert(dos_timer_register_callback(gameplay_callback) ==
		   DOS_TIMER_CALLBACK_REGISTRATION_SUCCEEDED);
	sdl3_audio_configure(mode, rom_directory, model, 0);
	sdl3_audio_command_enable(1);
	assert(audio_load_dos_driver("ad15.drv", 0, 0) == 0);
	assert(sdl3_audio_worker_is_active() && sdl3_audio_command_active());
	assert(!sdl3_audio_resources_is_execution());
	assert(strcmp(SDL_GetCurrentAudioDriver(), "dummy") == 0);
	check_music_and_lifetime();
	check_shutdown_restart();
	dos_timer_shutdown();
	printf("Audio worker mode %u: main stalls, pause, engine and resource lifetime passed.\n",
		   (legacy_uint)mode);
}

/* Usage: test-audio-worker GAME_DIR [ROM_DIR MODEL]. Normal CI uses PC/Ad Lib;
 * the optional arguments also exercise real Munt with user-provided ROMs. */
legacy_int main(legacy_int argc, legacy_char **argv)
{
	assert(argc == TEST_EXPECTED_ARGUMENTS || argc == TEST_ROM_ARGUMENTS);
	SDL_SetMainReady();
	assert(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
	copy_resources(argv[1], argc == TEST_ROM_ARGUMENTS);
	main_thread = SDL_GetCurrentThreadID();
	sdl3_batch_mode = 0;
	check_mode(STARTUP_SOUND_PC_SPEAKER, NULL, NULL);
	check_mode(STARTUP_SOUND_ADLIB, NULL, NULL);
	if (argc == TEST_ROM_ARGUMENTS) {
#ifdef RESTUNTS_MT32
		check_mode(STARTUP_SOUND_MT32, argv[2], argv[3]);
#else
		assert(!"This build does not support the optional MT-32 worker check.");
#endif
	}
	SDL_Quit();
	return 0;
}
