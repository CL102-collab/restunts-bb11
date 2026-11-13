/* Check the original PC15 note/pitch/arbitration protocol without SDL or
 * game data. PCM ownership is independent of the sequencer resource lifetime. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platform/sdl3/pc_speaker_driver.h"
#include "../c/audio/audio_internal.h"

#define TEST_NOTE 60U
#define TEST_NOTE_PERIOD 2280U
#define TEST_ENGINE_VALUE 1600U
#define TEST_ENGINE_PERIOD 11931U
#define TEST_BEND 4096U
#define TEST_BENT_PERIOD 2153U
#define TEST_RESOURCE_BYTES 93U
#define TEST_SAMPLE_BYTES 1000U
#define TEST_SAMPLE_NOTE 30U
#define TEST_SAMPLE_RATE 44100U
#define TEST_RENDER_FRAMES 4410U
#define TEST_MAX_CROSSINGS 54U
#define TEST_MIN_CROSSINGS 50U

static void check_pitch(struct PC_SPEAKER_DRIVER *driver)
{
	struct AUDIO_CONTEXT context;
	struct AUDIO_CHANNEL source;
	legacy_u8 resource[TEST_RESOURCE_BYTES];
	memset(&context, 0, sizeof(context));
	memset(&source, 0, sizeof(source));
	memset(resource, 0, sizeof(resource));
	context.state = AUDIO_CONTEXT_STATE_PLAYING;
	assert(pc_speaker_driver_note_period(TEST_NOTE) == TEST_NOTE_PERIOD);
	pc_speaker_driver_engine_value(driver, 1, &context, TEST_ENGINE_VALUE);
	assert(driver->voices[1].base_period == TEST_ENGINE_PERIOD);
	pc_speaker_driver_activate(driver, 1, &context, &source, AUDIO_NOTE_USE_DRIVER_VALUE, NULL, 0);
	pc_speaker_driver_update_context(driver, 1, &context, resource);
	assert(driver->voices[1].period == TEST_ENGINE_PERIOD);
	assert(LEGACY_READ_U16_LE(context.driver_private_state + 1) == TEST_ENGINE_PERIOD);
	pc_speaker_driver_activate(driver, 1, &context, &source, TEST_NOTE, NULL, 0);
	resource[18] = 2;
	source.pitch = TEST_BEND;
	pc_speaker_driver_update_context(driver, 1, &context, resource);
	assert(driver->voices[1].period == TEST_BENT_PERIOD);
	source.pitch = 0;
	resource[17] = (legacy_u8)-3;
	resource[40] = 2;
	resource[25] = 2;
	context.modulation = -20;
	context.level = 40;
	pc_speaker_driver_update_context(driver, 1, &context, resource);
	assert(driver->voices[1].period == TEST_NOTE_PERIOD + 3U - 20U + 40U);
	resource[53] = 1;
	context.sequence_value = 12;
	pc_speaker_driver_update_context(driver, 1, &context, resource);
	assert(driver->voices[1].period == pc_speaker_driver_note_period(TEST_NOTE + 12U) + 23U);
	pc_speaker_driver_release(driver, 1);
}

static void check_arbitration(struct PC_SPEAKER_DRIVER *driver)
{
	struct AUDIO_CHANNEL music;
	struct AUDIO_CHANNEL effect;
	memset(&music, 0, sizeof(music));
	memset(&effect, 0, sizeof(effect));
	effect.channel = AUDIO_EFFECT_CHANNEL_FIRST;
	for (legacy_s16 channel = 1; channel < 7; channel++) {
		pc_speaker_driver_activate(driver, channel, NULL, &music, TEST_NOTE + channel, NULL, 0);
		pc_speaker_driver_volume(driver, channel, AUDIO_ENGINE_MAX_VOLUME);
	}
	pc_speaker_driver_mix(driver);
	assert(driver->selected_voice == 6);
	pc_speaker_driver_release(driver, 6);
	pc_speaker_driver_mix(driver);
	assert(driver->selected_voice == 5);
	pc_speaker_driver_release(driver, 5);
	driver->mix_phase = 0;
	const legacy_s8 expected[] = {3, 3, 1, 1, 3};
	for (size_t index = 0; index < sizeof(expected); index++) {
		pc_speaker_driver_mix(driver);
		assert(driver->selected_voice == expected[index]);
	}
	pc_speaker_driver_prepare(driver, 2, &effect);
	legacy_u8 phase = driver->mix_phase;
	pc_speaker_driver_set_music_muted(driver, 1);
	assert(driver->mix_phase == phase && driver->selected_voice == 2);
	assert(driver->voices[1].active && driver->voices[3].active);
	pc_speaker_driver_set_music_muted(driver, 0);
	assert(driver->mix_phase == phase && driver->selected_voice != -1);
	pc_speaker_driver_start(driver);
	pc_speaker_driver_mix(driver);
	assert(!driver->output_active && driver->selected_voice == -1);
}

static void check_waveform(struct PC_SPEAKER_DRIVER *driver)
{
	legacy_s16 samples[TEST_RENDER_FRAMES];
	pc_speaker_driver_activate(driver, 1, NULL, NULL, TEST_NOTE, NULL, 0);
	pc_speaker_driver_mix(driver);
	assert(driver->output_active && driver->output_period == TEST_NOTE_PERIOD);
	pc_speaker_driver_render(driver, samples, TEST_RENDER_FRAMES, TEST_SAMPLE_RATE);
	legacy_u32 crossings = 0;
	for (size_t index = 1; index < TEST_RENDER_FRAMES; index++) {
		crossings += samples[index] > 0 && samples[index - 1U] < 0;
	}
	/* 1193180 / 2280 Hz, over 100 ms: preserve PIT pitch rather than
     * quantizing the original continuous engine or note divisors. */
	assert(crossings >= TEST_MIN_CROSSINGS && crossings <= TEST_MAX_CROSSINGS);
	assert(driver->tone_phase != 0);
	legacy_u32 phase = driver->tone_phase;
	pc_speaker_driver_mix(driver);
	assert(driver->tone_phase == phase);
	driver->voices[1].period = 0;
	pc_speaker_driver_mix(driver);
	assert(driver->tone_phase == 0);
	assert(driver->output_active && driver->output_period == 0);
	pc_speaker_driver_start(driver);
}

static void check_pcm_lifetime(struct PC_SPEAKER_DRIVER *driver)
{
	size_t bytes = PC_SPEAKER_SAMPLE_HEADER_BYTES + TEST_SAMPLE_BYTES;
	legacy_u8 *resource = malloc(bytes);
	assert(resource != NULL);
	memset(resource, 0, bytes);
	LEGACY_WRITE_U32_LE(resource + PC_SPEAKER_SAMPLE_LENGTH_OFFSET, TEST_SAMPLE_BYTES);
	memset(resource + PC_SPEAKER_SAMPLE_HEADER_BYTES, 255, TEST_SAMPLE_BYTES);
	struct AUDIO_CHANNEL music;
	memset(&music, 0, sizeof(music));
	pc_speaker_driver_activate(driver, 0, NULL, &music, TEST_SAMPLE_NOTE, resource, bytes);
	assert(driver->sample_active &&
		   driver->sample_data != resource + PC_SPEAKER_SAMPLE_HEADER_BYTES);
	memset(resource, 0, bytes);
	free(resource);
	pc_speaker_driver_mix(driver);
	assert(driver->selected_voice == 0 && !driver->output_active);
	legacy_s16 samples[TEST_RENDER_FRAMES];
	pc_speaker_driver_render(driver, samples, 1, TEST_SAMPLE_RATE);
	assert(samples[0] > 0 && driver->sample_position != 0);
	legacy_u64 position = driver->sample_position;
	pc_speaker_driver_set_music_muted(driver, 1);
	pc_speaker_driver_render(driver, samples, 1, TEST_SAMPLE_RATE);
	assert(samples[0] == 0 && driver->sample_position > position);
	pc_speaker_driver_set_music_muted(driver, 0);
	pc_speaker_driver_render(driver, samples, TEST_RENDER_FRAMES, TEST_SAMPLE_RATE);
	pc_speaker_driver_render(driver, samples, TEST_RENDER_FRAMES, TEST_SAMPLE_RATE);
	assert(!driver->sample_active && !driver->voices[0].active);
	pc_speaker_driver_release(driver, 0);
	assert(driver->sample_data == NULL);
}

legacy_int main(void)
{
	struct PC_SPEAKER_DRIVER driver;
	pc_speaker_driver_initialize(&driver);
	check_pitch(&driver);
	check_arbitration(&driver);
	check_waveform(&driver);
	check_pcm_lifetime(&driver);
	pc_speaker_driver_shutdown(&driver);
	puts("PC speaker pitch, arbitration, replacement muting, and PCM lifetime checks passed.");
	return 0;
}
