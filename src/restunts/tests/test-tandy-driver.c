/* TD15 protocol regression coverage. */
#include <assert.h>
#include <string.h>
#include "../platform/sdl3/tandy_driver.h"
#include "../c/audio/audio_internal.h"

#define TEST_RESOURCE_BYTES 93U
#define TEST_TONE_FIRST 1U
#define TEST_TONE_SECOND 2U
#define TEST_NOISE_CHANNEL 4U
#define TEST_SPEAKER_CHANNEL 5U
#define TEST_NOTE_C 60U
#define TEST_NOTE_A 69U
#define TEST_C_PERIOD 214U
#define TEST_A_PERIOD 127U
#define TEST_SPEAKER_C_PERIOD 2280U
#define TEST_VELOCITY_OFFSET 21U
#define TEST_ENVELOPE_OFFSET 25U
#define TEST_MODULATION_OFFSET 40U
#define TEST_DETUNE_OFFSET 17U
#define TEST_BEND_OFFSET 18U
#define TEST_RENDER_FRAMES 512U
#define TEST_SAMPLE_BYTES 32U
#define TEST_SAMPLE_VALUE 240U
#define TEST_SAMPLE_NOTE 48U
#define TEST_VOLUME 64U
#define TEST_VELOCITY 63U
#define TEST_LEVEL 0x07ff
#define TEST_VOLUME_ROUTE 3U
#define TEST_PERIOD_ROUTE 2U
#define TEST_ATTENUATION 13U
#define TEST_PITCH 2048
#define TEST_BEND 12U
#define TEST_DETUNE 3U
#define TEST_MODULATION 5U
#define TEST_PERIOD_LEVEL 10
#define TEST_POSITIVE_PERIOD 200U
#define TEST_NEGATIVE_PERIOD 280U
#define TEST_MUTE_ATTENUATION 15U
#define TEST_NOISE_NOTE 4U
#define TEST_NOISE_CONTROL 6U
#define TEST_SECOND_TONE_PERIOD_REGISTER 2U
#define TEST_SECOND_TONE_VOLUME_REGISTER 3U
#define TEST_NOISE_REGISTER 6U

static legacy_u8 audible(const legacy_s16 *samples, size_t frames)
{
	for (size_t frame = 0; frame < frames; frame++) {
		if (samples[frame] != 0) {
			return 1;
		}
	}
	return 0;
}

static void test_tables(void)
{
	struct TANDY_DRIVER driver;
	tandy_driver_initialize(&driver, MAME_PSG_NCR8496);
	assert(tandy_driver_note_period(TEST_TONE_FIRST, TEST_NOTE_C) == TEST_C_PERIOD);
	assert(tandy_driver_note_period(TEST_TONE_FIRST, TEST_NOTE_A) == TEST_A_PERIOD);
	assert(tandy_driver_note_period(TEST_SPEAKER_CHANNEL, TEST_NOTE_C) == TEST_SPEAKER_C_PERIOD);
	tandy_driver_shutdown(&driver);
}

static void test_volume_and_pitch(void)
{
	struct TANDY_DRIVER driver;
	struct AUDIO_CONTEXT context = {0};
	struct AUDIO_CHANNEL source = {0};
	legacy_u8 resource[TEST_RESOURCE_BYTES] = {0};
	context.state = AUDIO_CONTEXT_STATE_PLAYING;
	source.channel = AUDIO_EFFECT_CHANNEL_FIRST;
	source.volume = AUDIO_ENGINE_MAX_VOLUME;
	tandy_driver_initialize(&driver, MAME_PSG_PSSJ3);
	tandy_driver_volume(&driver, TEST_TONE_FIRST, TEST_VOLUME);
	resource[TEST_VELOCITY_OFFSET] = 1;
	resource[TEST_ENVELOPE_OFFSET] = TEST_VOLUME_ROUTE;
	context.level = TEST_LEVEL;
	tandy_driver_activate(&driver, TEST_TONE_FIRST, &context, &source, TEST_NOTE_C, TEST_VELOCITY,
						  resource, NULL, 0);
	tandy_driver_update_context(&driver, TEST_TONE_FIRST, &context, resource);
	assert(driver.psg.registers[1] == TEST_ATTENUATION);
	resource[TEST_DETUNE_OFFSET] = TEST_DETUNE;
	resource[TEST_BEND_OFFSET] = TEST_BEND;
	resource[TEST_MODULATION_OFFSET] = TEST_PERIOD_ROUTE;
	resource[TEST_ENVELOPE_OFFSET] = TEST_PERIOD_ROUTE;
	context.modulation = TEST_MODULATION;
	context.level = TEST_PERIOD_LEVEL;
	source.pitch = TEST_PITCH;
	tandy_driver_update_context(&driver, TEST_TONE_FIRST, &context, resource);
	assert(driver.voices[TEST_TONE_FIRST].period == TEST_POSITIVE_PERIOD);
	source.pitch = (legacy_u16)(-TEST_PITCH);
	tandy_driver_update_context(&driver, TEST_TONE_FIRST, &context, resource);
	assert(driver.voices[TEST_TONE_FIRST].period == TEST_NEGATIVE_PERIOD);
	tandy_driver_shutdown(&driver);
}

static void test_independent_tones_and_music_mute(void)
{
	struct TANDY_DRIVER driver;
	struct AUDIO_CONTEXT context = {0};
	struct AUDIO_CHANNEL music = {0};
	struct AUDIO_CHANNEL effect = {0};
	legacy_u8 resource[TEST_RESOURCE_BYTES] = {0};
	legacy_s16 samples[TEST_RENDER_FRAMES];
	context.state = AUDIO_CONTEXT_STATE_PLAYING;
	music.channel = AUDIO_MUSIC_CHANNEL_FIRST;
	effect.channel = AUDIO_EFFECT_CHANNEL_FIRST;
	tandy_driver_initialize(&driver, MAME_PSG_NCR8496);
	tandy_driver_activate(&driver, TEST_TONE_FIRST, &context, &music, TEST_NOTE_C,
						  AUDIO_ENGINE_MAX_VOLUME, resource, NULL, 0);
	tandy_driver_activate(&driver, TEST_TONE_SECOND, &context, &effect, TEST_NOTE_A,
						  AUDIO_ENGINE_MAX_VOLUME, resource, NULL, 0);
	assert(driver.psg.registers[0] == TEST_C_PERIOD);
	assert(driver.psg.registers[TEST_SECOND_TONE_PERIOD_REGISTER] == TEST_A_PERIOD);
	tandy_driver_activate(&driver, TEST_NOISE_CHANNEL, &context, &effect, TEST_NOISE_NOTE,
						  AUDIO_ENGINE_MAX_VOLUME, resource, NULL, 0);
	assert(driver.psg.registers[TEST_NOISE_REGISTER] == TEST_NOISE_CONTROL);
	tandy_driver_release(&driver, TEST_NOISE_CHANNEL);
	tandy_driver_set_music_muted(&driver, 1);
	assert(driver.psg.registers[1] == TEST_MUTE_ATTENUATION);
	assert(driver.psg.registers[TEST_SECOND_TONE_VOLUME_REGISTER] == 0);
	tandy_driver_render(&driver, samples, TEST_RENDER_FRAMES, TANDY_SAMPLE_RATE);
	assert(audible(samples, TEST_RENDER_FRAMES));
	tandy_driver_release(&driver, TEST_TONE_SECOND);
	tandy_driver_render(&driver, samples, TEST_RENDER_FRAMES, TANDY_SAMPLE_RATE);
	assert(!audible(samples, TEST_RENDER_FRAMES));
	tandy_driver_set_music_muted(&driver, 0);
	assert(driver.psg.registers[1] == 0);
	tandy_driver_start(&driver);
	tandy_driver_render(&driver, samples, TEST_RENDER_FRAMES, TANDY_SAMPLE_RATE);
	assert(!audible(samples, TEST_RENDER_FRAMES));
	tandy_driver_shutdown(&driver);
}

static void test_speaker_bass(void)
{
	struct TANDY_DRIVER driver;
	struct AUDIO_CONTEXT context = {0};
	struct AUDIO_CHANNEL source = {0};
	legacy_u8 resource[TEST_RESOURCE_BYTES] = {0};
	legacy_s16 samples[TEST_RENDER_FRAMES];
	context.state = AUDIO_CONTEXT_STATE_PLAYING;
	source.channel = AUDIO_MUSIC_CHANNEL_FIRST;
	tandy_driver_initialize(&driver, MAME_PSG_PSSJ3);
	tandy_driver_activate(&driver, TEST_SPEAKER_CHANNEL, &context, &source, TEST_NOTE_C,
						  AUDIO_ENGINE_MAX_VOLUME, resource, NULL, 0);
	tandy_driver_mix(&driver);
	tandy_driver_render(&driver, samples, TEST_RENDER_FRAMES, TANDY_SAMPLE_RATE);
	assert(audible(samples, TEST_RENDER_FRAMES));
	tandy_driver_set_music_muted(&driver, 1);
	tandy_driver_render(&driver, samples, TEST_RENDER_FRAMES, TANDY_SAMPLE_RATE);
	assert(!audible(samples, TEST_RENDER_FRAMES));
	tandy_driver_shutdown(&driver);
}

static void test_owned_sample_and_bounds(void)
{
	struct TANDY_DRIVER driver;
	struct AUDIO_CONTEXT context = {0};
	struct AUDIO_CHANNEL source = {0};
	legacy_u8 resource[TEST_RESOURCE_BYTES] = {0};
	legacy_u8 sample[TANDY_SAMPLE_HEADER_BYTES + TEST_SAMPLE_BYTES] = {0};
	legacy_s16 samples[TEST_RENDER_FRAMES];
	LEGACY_WRITE_U32_LE(sample + TANDY_SAMPLE_LENGTH_OFFSET, TEST_SAMPLE_BYTES);
	memset(sample + TANDY_SAMPLE_HEADER_BYTES, TEST_SAMPLE_VALUE, TEST_SAMPLE_BYTES);
	context.state = AUDIO_CONTEXT_STATE_PLAYING;
	source.channel = AUDIO_EFFECT_CHANNEL_FIRST;
	tandy_driver_initialize(&driver, MAME_PSG_PSSJ3);
	tandy_driver_activate(&driver, TANDY_SAMPLE_CHANNEL, &context, &source, TEST_SAMPLE_NOTE,
						  AUDIO_ENGINE_MAX_VOLUME, resource, sample, sizeof(sample));
	assert(driver.sample.sample_active);
	assert(driver.sample.sample_data != sample + TANDY_SAMPLE_HEADER_BYTES);
	memset(sample + TANDY_SAMPLE_HEADER_BYTES, PC_SPEAKER_PCM_UNSIGNED_CENTER, TEST_SAMPLE_BYTES);
	assert(driver.sample.sample_data[0] == TEST_SAMPLE_VALUE);
	tandy_driver_render(&driver, samples, TEST_RENDER_FRAMES, TANDY_SAMPLE_RATE);
	assert(audible(samples, TEST_RENDER_FRAMES));
	assert(!driver.sample.sample_active);
	assert(!driver.voices[TANDY_SAMPLE_CHANNEL].active);
	LEGACY_WRITE_U32_LE(sample + TANDY_SAMPLE_LENGTH_OFFSET, TEST_SAMPLE_BYTES + 1U);
	tandy_driver_activate(&driver, TANDY_SAMPLE_CHANNEL, &context, &source, TEST_SAMPLE_NOTE,
						  AUDIO_ENGINE_MAX_VOLUME, resource, sample, sizeof(sample));
	assert(!driver.sample.sample_active);
	assert(driver.sample.sample_data == NULL);
	tandy_driver_activate(&driver, TANDY_SAMPLE_CHANNEL, &context, &source, TEST_SAMPLE_NOTE,
						  AUDIO_ENGINE_MAX_VOLUME, resource, sample,
						  TANDY_SAMPLE_HEADER_BYTES - 1U);
	assert(!driver.sample.sample_active);
	tandy_driver_shutdown(&driver);
	assert(driver.sample.sample_data == NULL);
}

legacy_int main(void)
{
	test_tables();
	test_volume_and_pitch();
	test_independent_tones_and_music_mute();
	test_speaker_bass();
	test_owned_sample_and_bounds();
	return 0;
}
