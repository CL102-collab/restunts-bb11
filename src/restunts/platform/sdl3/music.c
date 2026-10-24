/* Optional Ogg Vorbis replacements. Decoding and resource ownership stay on
 * the main thread; SDL consumes a bounded queue of decoded samples. */
#include "music.h"
#include "sdl3.h"
#include "../../c/asset_path.h"
#include "../../c/platform.h"
#include "../../c/audio_internal.h"
#include <stdio.h>
#include <string.h>
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MUSIC_PATH_SIZE 1024
#define MUSIC_CHANNELS_MONO 1
#define MUSIC_CHANNELS_STEREO 2
#define MUSIC_PLAYBACK_SAMPLE_RATE 44100
#define MUSIC_MIN_SAMPLE_RATE 8000U
#define MUSIC_MAX_SAMPLE_RATE 192000U
#define MUSIC_DECODE_FRAMES 2048
#define MUSIC_QUEUE_MILLISECONDS 100U
#define MUSIC_MILLISECONDS_PER_SECOND 1000U
#define MUSIC_ASSET_DIRECTORY "assets/music/"
#define MUSIC_PACKAGE_DIRECTORY "music/"
#define MUSIC_NO_TRACK (-1)
#define MUSIC_CLOSE_ASSET_STREAM 1

static const legacy_char *const music_names[] = {"titl", "slct", "over", "vict"};
#define MUSIC_TRACK_COUNT (sizeof(music_names) / sizeof(music_names[0]))

static const void *music_resources[MUSIC_TRACK_COUNT];
static legacy_u8 music_failed[MUSIC_TRACK_COUNT];
static stb_vorbis *music_decoder;
static SDL_AudioStream *music_stream;
static const void *music_resource;
static legacy_s32 music_track = MUSIC_NO_TRACK;
static legacy_s32 music_audio_initialized;
static legacy_s32 music_paused;
static legacy_s32 music_channels;
static legacy_s32 music_queue_bytes;
static legacy_s32 music_enabled;
static legacy_s32 music_loading;
static legacy_u64 music_elapsed_ticks;

void sdl3_music_register(const void *resource, const legacy_s8 *name)
{
	for (size_t index = 0; index < MUSIC_TRACK_COUNT; ++index) {
		/* Resources can be freed and their address reused for another song or effect. */
		if (music_resources[index] == resource) {
			music_resources[index] = NULL;
		}
	}
	if (resource == NULL || name == NULL) {
		return;
	}
	for (size_t index = 0; index < MUSIC_TRACK_COUNT; ++index) {
		if (SDL_strcasecmp((const legacy_char *)name, music_names[index]) == 0) {
			music_resources[index] = resource;
			music_failed[index] = 0;
			return;
		}
	}
}

static stb_vorbis *music_open(const legacy_char *base, const legacy_char *directory,
							  legacy_s32 track)
{
	legacy_char path[MUSIC_PATH_SIZE];
	legacy_s32 length =
		snprintf(path, sizeof(path), "%s%s%s.ogg", base, directory, music_names[track]);
	if (length < 0 || (size_t)length >= sizeof(path)) {
		return NULL;
	}
	FILE *file = asset_path_open_file(path, "rb");
	if (file == NULL) {
		return NULL;
	}
	/* The decoder owns and closes the stream, including decoder failures. */
	legacy_int error;
	stb_vorbis *decoder = stb_vorbis_open_file(file, MUSIC_CLOSE_ASSET_STREAM, &error, NULL);
	if (decoder != NULL) {
		stb_vorbis_info info = stb_vorbis_get_info(decoder);
		if (info.channels < MUSIC_CHANNELS_MONO || info.channels > MUSIC_CHANNELS_STEREO ||
			info.sample_rate < MUSIC_MIN_SAMPLE_RATE || info.sample_rate > MUSIC_MAX_SAMPLE_RATE) {
			stb_vorbis_close(decoder);
			return NULL;
		}
	}
	return decoder;
}

static stb_vorbis *music_find(legacy_s32 track)
{
	stb_vorbis *decoder = music_open("", MUSIC_ASSET_DIRECTORY, track);
	const legacy_char *base = asset_path_base();
	if (decoder == NULL && base != NULL) {
		decoder = music_open(base, MUSIC_ASSET_DIRECTORY, track);
	}
	if (decoder == NULL) {
		decoder = music_open("", MUSIC_PACKAGE_DIRECTORY, track);
	}
	if (decoder == NULL && base != NULL) {
		decoder = music_open(base, MUSIC_PACKAGE_DIRECTORY, track);
	}
#ifdef RESTUNTS_MUSIC_DIRECTORY
	if (decoder == NULL) {
		decoder = music_open("", RESTUNTS_MUSIC_DIRECTORY "/", track);
	}
#endif
	return decoder;
}

static void music_close(void)
{
	SDL_DestroyAudioStream(music_stream);
	music_stream = NULL;
	if (music_decoder != NULL) {
		stb_vorbis_close(music_decoder);
		music_decoder = NULL;
	}
	if (music_audio_initialized) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		music_audio_initialized = 0;
	}
	sdl3_audio_set_music_muted(0);
}

void sdl3_music_stop(void)
{
	music_close();
	music_track = MUSIC_NO_TRACK;
	music_resource = NULL;
	music_elapsed_ticks = 0;
}

void sdl3_music_tick(void)
{
	/* The original sequencer calls this only for an active, unpaused tick.
	 * Both sources therefore share loading, mute and dialog-pause behavior. */
	if (music_resource != NULL) {
		music_elapsed_ticks++;
	}
}

void sdl3_music_set_volume(legacy_s16 value)
{
	if (value < 0) {
		value = 0;
	} else if (value > (legacy_s16)AUDIO_ENGINE_MAX_VOLUME) {
		value = AUDIO_ENGINE_MAX_VOLUME;
	}
	if (music_stream != NULL) {
		SDL_SetAudioStreamGain(music_stream, (legacy_f32)value / AUDIO_ENGINE_MAX_VOLUME);
	}
}

void sdl3_music_sync(void)
{
	if (music_stream == NULL || music_loading) {
		return;
	}
	legacy_s32 paused = audio_music_active == AUDIO_STATE_DISABLED ||
						audio_music_enabled == AUDIO_STATE_DISABLED ||
						audio_suspended == AUDIO_STATE_ENABLED || sdl3_timer_callbacks_suspended();
	if (paused != music_paused) {
		legacy_s32 succeeded = paused ? SDL_PauseAudioStreamDevice(music_stream)
									  : SDL_ResumeAudioStreamDevice(music_stream);
		if (!succeeded) {
			music_failed[music_track] = 1;
			music_close();
			return;
		}
		music_paused = paused;
	}
}

static legacy_s32 music_fill(void)
{
	legacy_s16 samples[MUSIC_DECODE_FRAMES * MUSIC_CHANNELS_STEREO];
	legacy_s32 queued = SDL_GetAudioStreamQueued(music_stream);
	if (queued < 0) {
		return 0;
	}
	while (queued < music_queue_bytes) {
		legacy_s32 frames = stb_vorbis_get_samples_short_interleaved(
			music_decoder, music_channels, samples, MUSIC_DECODE_FRAMES * music_channels);
		if (stb_vorbis_get_error(music_decoder) != VORBIS__no_error) {
			return 0;
		}
		if (frames == 0) {
			/* All four original songs repeat. Reject empty/broken streams instead
			 * of repeatedly seeking without producing any samples. */
			if (!stb_vorbis_seek_start(music_decoder)) {
				return 0;
			}
			frames = stb_vorbis_get_samples_short_interleaved(
				music_decoder, music_channels, samples, MUSIC_DECODE_FRAMES * music_channels);
			if (frames == 0 || stb_vorbis_get_error(music_decoder) != VORBIS__no_error) {
				return 0;
			}
		}
		legacy_s32 bytes = frames * music_channels * sizeof(samples[0]);
		if (!SDL_PutAudioStreamData(music_stream, samples, bytes)) {
			return 0;
		}
		queued += bytes;
	}
	return 1;
}

static legacy_s32 music_open_current(void)
{
	if (music_track == MUSIC_NO_TRACK || music_failed[music_track]) {
		return 0;
	}
	music_loading = 1;
	music_decoder = music_find(music_track);
	if (music_decoder == NULL || !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		goto failed;
	}
	music_audio_initialized = 1;
	stb_vorbis_info info = stb_vorbis_get_info(music_decoder);
	legacy_u32 length = stb_vorbis_stream_length_in_samples(music_decoder);
	if (length == 0 || stb_vorbis_get_error(music_decoder) != VORBIS__no_error) {
		goto failed;
	}
	music_channels = info.channels;
	music_queue_bytes =
		(legacy_s32)(info.sample_rate * MUSIC_QUEUE_MILLISECONDS / MUSIC_MILLISECONDS_PER_SECOND *
					 music_channels * sizeof(legacy_s16));
	/* Keep DOS Sound Blaster hardware within its supported rate. SDL resamples
	 * the file's input rate to the device rate, including 48/96/192 kHz music. */
	SDL_AudioSpec device_spec = {SDL_AUDIO_S16, music_channels, MUSIC_PLAYBACK_SAMPLE_RATE};
	SDL_AudioSpec source_spec = {SDL_AUDIO_S16, music_channels, (legacy_s32)info.sample_rate};
	music_stream =
		SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &device_spec, NULL, NULL);
	if (music_stream == NULL || !SDL_SetAudioStreamFormat(music_stream, &source_spec, NULL)) {
		goto failed;
	}
	/* Account for device/file setup before seeking. The original sequence keeps
	 * running during a Shift+F10 switch, but is still stopped during song loading. */
	sdl3_timer_pump();
	legacy_u32 frame = (legacy_u32)((music_elapsed_ticks * info.sample_rate /
									 DOS_TIMER_REALTIME_TICKS_PER_SECOND) %
									length);
	if (!stb_vorbis_seek(music_decoder, frame) || !music_fill()) {
		goto failed;
	}
	sdl3_music_set_volume(audio_music_rate);
	music_paused = 1;
	sdl3_audio_set_music_muted(1);
	music_loading = 0;
	sdl3_music_sync();
	return music_stream != NULL;

failed:
	music_close();
	music_loading = 0;
	return 0;
}

legacy_s32 sdl3_music_start(const void *resource)
{
	sdl3_music_stop();
	if (sdl3_batch_mode || resource == NULL) {
		return 0;
	}
	for (size_t index = 0; index < MUSIC_TRACK_COUNT; ++index) {
		if (music_resources[index] == resource) {
			music_track = (legacy_s32)index;
			music_resource = resource;
			break;
		}
	}
	return music_enabled ? music_open_current() : 0;
}

void sdl3_music_set_enabled(legacy_s32 enabled)
{
	enabled = enabled != 0;
	if (music_enabled == enabled) {
		return;
	}
	/* Catch up before choosing the new source so pending timer ticks cannot be
	 * charged to a freshly opened decoder or lost when returning to AdLib. */
	sdl3_timer_pump();
	music_enabled = enabled;
	if (music_enabled && music_resource != NULL && !sdl3_batch_mode) {
		music_open_current();
	} else {
		music_close();
	}
}

legacy_s32 sdl3_music_toggle(void)
{
	if (sdl3_batch_mode || music_resource == NULL) {
		return 0;
	}
	sdl3_music_set_enabled(!music_enabled);
	return 1;
}

void sdl3_music_update(void)
{
	if (music_stream == NULL || music_loading) {
		return;
	}
	sdl3_music_sync();
	if (music_stream != NULL && !music_paused && !music_fill()) {
		/* The original sequence has kept running silently, so a failed decoder
		 * can return to it at the current note without restarting the song. */
		music_failed[music_track] = 1;
		music_close();
	}
}

void sdl3_music_shutdown(void)
{
	sdl3_music_stop();
	music_enabled = 0;
	memset(music_resources, 0, sizeof(music_resources));
	memset(music_failed, 0, sizeof(music_failed));
}
