/* Check the original PC15 note/pitch/arbitration protocol without SDL or
 * game data. PCM ownership is independent of the sequencer resource lifetime. */
#include <assert.h>
#include <math.h>
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
#define TEST_TICK_FRAMES (TEST_SAMPLE_RATE / 100U)
#define TEST_IDLE_TICKS 100U
#define TEST_IDLE_MIN_CROSSINGS 15U
#define TEST_IDLE_MAX_CROSSINGS 25U
#define TEST_IDLE_MIN_MEAN_SQUARE 1000000U
#define TEST_HIGH_NOTE 119U
#define TEST_HIGH_DIVISOR 75U
#define TEST_ALIAS_HARMONIC 3U
#define TEST_ALIAS_MAX_POWER_RATIO 0.001
#define TEST_PI 3.14159265358979323846
#define TEST_REFERENCE_AMPLITUDE 5000.0
#define TEST_RESPONSE_TOLERANCE 0.003

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

static void check_original_driver_vectors(struct PC_SPEAKER_DRIVER *driver)
{
	/* These results were captured by executing the shipped PC15.DRV x86 code,
	 * rather than calculated using the C adapter. Context update entry 0x27
	 * jumps to 0x423; engine conversion entry 0x24 jumps to 0x3f9. */
	static const struct {
		legacy_u8 note;
		legacy_s16 bend;
		legacy_s8 detune;
		legacy_u8 bend_range;
		legacy_u8 sequence_mode;
		legacy_u8 sequence;
		legacy_u8 modulation_mode;
		legacy_s16 modulation;
		legacy_u8 envelope_mode;
		legacy_s16 level;
		legacy_u16 expected_period;
	} updates[] = {
		{60, 4095, 0, 2, 0, 0, 0, 0, 0, 0, 2157},
		{60, 4096, 0, 2, 0, 0, 0, 0, 0, 0, 2153},
		{60, -4096, 0, 2, 0, 0, 0, 0, 0, 0, 2420},
		{40, -8192, -37, 12, 0, 0, 0, 0, 0, 0, 14479},
		{100, 8191, 7, 12, 0, 0, 0, 0, 0, 0, 112},
		{60, 4096, -3, 2, 1, 12, 2, -20, 2, 40, 1098},
		{90, 0, 127, 0, 0, 0, 2, -32768, 2, -32768, 276},
		{24, 0, -128, 0, 1, 12, 2, 1234, 2, -4567, 5916},
	};
	static const struct {
		legacy_u16 value;
		legacy_u16 expected_period;
	} engines[] = {{0, 0},		 {1, 0},	   {83, 0},		 {125, 0},		{255, 0},	 {304, 0},
				   {305, 62593}, {306, 62388}, {500, 38181}, {1600, 11931}, {65535, 291}};
	struct AUDIO_CONTEXT context;
	struct AUDIO_CHANNEL source;
	legacy_u8 resource[TEST_RESOURCE_BYTES];
	memset(&context, 0, sizeof(context));
	memset(&source, 0, sizeof(source));
	context.state = AUDIO_CONTEXT_STATE_PLAYING;
	for (size_t index = 0; index < sizeof(updates) / sizeof(updates[0]); index++) {
		memset(resource, 0, sizeof(resource));
		resource[17] = (legacy_u8)updates[index].detune;
		resource[18] = updates[index].bend_range;
		resource[25] = updates[index].envelope_mode;
		resource[40] = updates[index].modulation_mode;
		resource[53] = updates[index].sequence_mode;
		source.pitch = (legacy_u16)updates[index].bend;
		context.sequence_value = updates[index].sequence;
		context.modulation = updates[index].modulation;
		context.level = updates[index].level;
		pc_speaker_driver_activate(driver, 1, &context, &source, updates[index].note, NULL, 0);
		pc_speaker_driver_update_context(driver, 1, &context, resource);
		assert(driver->voices[1].period == updates[index].expected_period);
		assert(LEGACY_READ_U16_LE(context.driver_private_state + 3) ==
			   updates[index].expected_period);
	}
	for (size_t index = 0; index < sizeof(engines) / sizeof(engines[0]); index++) {
		pc_speaker_driver_engine_value(driver, 1, &context, engines[index].value);
		assert(driver->voices[1].base_period == engines[index].expected_period);
	}
	/* The original LM002 idle target is 750 RPM / resource divisor 6 = 125.
	 * Its zero base divisor wraps under the actual ENGI modulation; these
	 * are long audible PIT cycles, not a reason to suppress the engine. */
	memset(resource, 0, sizeof(resource));
	resource[40] = 2;
	source.pitch = 0;
	pc_speaker_driver_engine_value(driver, 1, &context, 125);
	pc_speaker_driver_activate(driver, 1, &context, &source, AUDIO_NOTE_USE_DRIVER_VALUE, NULL, 0);
	static const legacy_s16 idle_modulation[] = {-119, -238, 0};
	static const legacy_u16 idle_periods[] = {65417, 65298, 0};
	for (size_t index = 0; index < sizeof(idle_periods) / sizeof(idle_periods[0]); index++) {
		context.modulation = idle_modulation[index];
		pc_speaker_driver_update_context(driver, 1, &context, resource);
		assert(driver->voices[1].period == idle_periods[index]);
	}
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
	legacy_f64 remaining = driver->tone_remaining;
	legacy_u32 latched = driver->tone_period;
	pc_speaker_driver_mix(driver);
	assert(driver->tone_remaining == remaining && driver->tone_period == latched);
	driver->voices[1].period = 0;
	pc_speaker_driver_mix(driver);
	assert(driver->tone_remaining == remaining && driver->tone_period == latched);
	assert(driver->output_active && driver->output_period == 0);
	pc_speaker_driver_start(driver);
}

static void check_idle_counter(struct PC_SPEAKER_DRIVER *driver)
{
	/* Shipped PC engine modulation at idle repeatedly writes these counts.
	 * Restarting mode3 every 10ms never reaches its roughly 27ms half-cycle. */
	static const legacy_u16 periods[] = {0U, 65417U, 65298U};
	legacy_s16 samples[TEST_TICK_FRAMES];
	legacy_s16 previous = 0;
	legacy_u32 crossings = 0;
	legacy_u64 energy = 0;
	pc_speaker_driver_activate(driver, 1, NULL, NULL, TEST_NOTE, NULL, 0);
	for (legacy_u32 tick = 0; tick < TEST_IDLE_TICKS; tick++) {
		driver->voices[1].period = periods[tick % (sizeof(periods) / sizeof(periods[0]))];
		pc_speaker_driver_mix(driver);
		pc_speaker_driver_render(driver, samples, TEST_TICK_FRAMES, TEST_SAMPLE_RATE);
		for (size_t frame = 0; frame < TEST_TICK_FRAMES; frame++) {
			crossings += samples[frame] > 0 && previous <= 0;
			energy += (legacy_u32)((legacy_s32)samples[frame] * samples[frame]);
			previous = samples[frame];
		}
	}
	assert(crossings >= TEST_IDLE_MIN_CROSSINGS && crossings <= TEST_IDLE_MAX_CROSSINGS);
	assert(energy > (legacy_u64)TEST_IDLE_MIN_MEAN_SQUARE * TEST_SAMPLE_RATE);
	pc_speaker_driver_start(driver);
}

static legacy_f64 spectral_power(const legacy_s16 *samples, legacy_f64 frequency)
{
	legacy_f64 real = 0.0, imaginary = 0.0;
	for (legacy_u32 frame = 0; frame < TEST_SAMPLE_RATE; frame++) {
		legacy_f64 angle = 2.0 * TEST_PI * frequency * frame / TEST_SAMPLE_RATE;
		legacy_f64 window = 0.5 - 0.5 * cos(2.0 * TEST_PI * frame / (TEST_SAMPLE_RATE - 1U));
		real += samples[frame] * window * cos(angle);
		imaginary += samples[frame] * window * sin(angle);
	}
	return real * real + imaginary * imaginary;
}

static void check_high_tone(struct PC_SPEAKER_DRIVER *driver)
{
	static legacy_s16 whole[TEST_SAMPLE_RATE], chunked[TEST_SAMPLE_RATE];
	pc_speaker_driver_activate(driver, 1, NULL, NULL, TEST_HIGH_NOTE, NULL, 0);
	pc_speaker_driver_mix(driver);
	assert(driver->output_period == TEST_HIGH_DIVISOR);
	struct PC_SPEAKER_DRIVER uninterrupted = *driver;
	pc_speaker_driver_render(&uninterrupted, whole, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE);
	for (legacy_u32 frame = 0; frame < TEST_SAMPLE_RATE; frame += TEST_TICK_FRAMES) {
		pc_speaker_driver_mix(driver);
		pc_speaker_driver_render(driver, chunked + frame, TEST_TICK_FRAMES, TEST_SAMPLE_RATE);
	}
	assert(memcmp(whole, chunked, sizeof(whole)) == 0);
	legacy_f64 fundamental = (legacy_f64)PC_SPEAKER_PIT_HZ / TEST_HIGH_DIVISOR;
	legacy_f64 alias = TEST_ALIAS_HARMONIC * fundamental - TEST_SAMPLE_RATE;
	/* A point-sampled square creates a loud 3.6kHz whistle from the third
	 * harmonic of this 15.9kHz note. The reference speaker's finite travel and
	 * sample integration suppress it by more than 30dB relative to the note. */
	assert(spectral_power(whole, alias) <
		   TEST_ALIAS_MAX_POWER_RATIO * spectral_power(whole, fundamental));
	pc_speaker_driver_start(driver);
}

static void check_reference_response(struct PC_SPEAKER_DRIVER *driver)
{
	/* AC RMS / peak amplitude from independent DOSBox internal-WAV recordings
	 * of a DOS program holding these PIT counts. Repeated count-only writes
	 * give the same response; they do not restart the waveform every tick. */
	static const struct {
		legacy_u16 divisor;
		legacy_f64 response;
	} cases[] = {{2280U, 0.973894}, {570U, 0.891488}, {143U, 0.463174}, {75U, 0.205689}};
	static legacy_s16 samples[TEST_SAMPLE_RATE];
	for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		pc_speaker_driver_activate(driver, 1, NULL, NULL, TEST_NOTE, NULL, 0);
		driver->voices[1].period = cases[index].divisor;
		pc_speaker_driver_mix(driver);
		pc_speaker_driver_render(driver, samples, TEST_RENDER_FRAMES, TEST_SAMPLE_RATE);
		pc_speaker_driver_render(driver, samples, TEST_SAMPLE_RATE, TEST_SAMPLE_RATE);
		legacy_f64 sum = 0.0, squares = 0.0;
		for (size_t frame = 0; frame < TEST_SAMPLE_RATE; frame++) {
			sum += samples[frame];
			squares += (legacy_f64)samples[frame] * samples[frame];
		}
		legacy_f64 mean = sum / TEST_SAMPLE_RATE;
		legacy_f64 response =
			sqrt(squares / TEST_SAMPLE_RATE - mean * mean) / TEST_REFERENCE_AMPLITUDE;
		assert(fabs(response - cases[index].response) < TEST_RESPONSE_TOLERANCE);
		pc_speaker_driver_start(driver);
	}
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
	check_original_driver_vectors(&driver);
	check_arbitration(&driver);
	check_waveform(&driver);
	check_idle_counter(&driver);
	check_high_tone(&driver);
	check_reference_response(&driver);
	check_pcm_lifetime(&driver);
	pc_speaker_driver_shutdown(&driver);
	puts("PC speaker pitch, arbitration, replacement muting, and PCM lifetime checks passed.");
	return 0;
}
