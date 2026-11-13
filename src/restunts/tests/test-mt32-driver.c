/* Protocol checks use an independent MIDI capture, without proprietary ROMs
 * or an audio device. Original MT instrument bytes are supplied as fixtures. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../platform/sdl3/mt32_driver.h"
#include "../c/audio/audio_internal.h"

#define TEST_CAPTURE_BYTES 8192U
#define TEST_RESOURCE_BYTES 93U
#define TEST_PATCH_BYTES 8U
#define TEST_TIMBRE_BYTES 246U
#define TEST_SYSEX_OVERHEAD 10U
#define TEST_MUSIC_CHANNEL 2
#define TEST_EFFECT_CHANNEL 1
#define TEST_INSTRUMENT_PROGRAM 30U
#define TEST_INSTRUMENT_VOLUME 99U
#define TEST_NOTE 60U
#define TEST_VELOCITY 80U

struct MIDI_CAPTURE {
	legacy_u8 bytes[TEST_CAPTURE_BYTES];
	size_t length;
};

static void capture(void *instance, const legacy_u8 *bytes, size_t length)
{
	struct MIDI_CAPTURE *output = instance;
	assert(output->length + length <= sizeof(output->bytes));
	memcpy(output->bytes + output->length, bytes, length);
	output->length += length;
}

static void expect(struct MIDI_CAPTURE *output, const legacy_u8 *bytes, size_t length)
{
	assert(output->length == length);
	assert(memcmp(output->bytes, bytes, length) == 0);
	output->length = 0;
}

static void instrument(legacy_u8 *resource)
{
	memset(resource, 0, TEST_RESOURCE_BYTES);
	LEGACY_WRITE_U32_LE(resource, TEST_RESOURCE_BYTES);
	resource[18] = 12;
	resource[21] = 1;
	resource[68] = TEST_INSTRUMENT_PROGRAM;
	resource[69] = TEST_INSTRUMENT_VOLUME;
	resource[70] = 64;
}

static void verify_sysex(const legacy_u8 *bytes, size_t length)
{
	static const legacy_u8 header[] = {0xf0, 0x41, 0x10, 0x16, 0x12};
	assert(length >= TEST_SYSEX_OVERHEAD);
	assert(memcmp(bytes, header, sizeof(header)) == 0);
	assert(bytes[length - 1U] == 0xf7);
	legacy_u32 checksum = 0;
	for (size_t index = sizeof(header); index < length - 1U; index++) {
		assert(bytes[index] < 128);
		checksum += bytes[index];
	}
	assert((checksum & 127U) == 0);
}

static void check_notes(struct MT32_DRIVER *driver, struct MIDI_CAPTURE *output,
						legacy_u8 *resource)
{
	struct AUDIO_CHANNEL source;
	struct AUDIO_CONTEXT context;
	memset(&source, 0, sizeof(source));
	memset(&context, 0, sizeof(context));
	context.state = AUDIO_CONTEXT_STATE_PLAYING;
	source.channel = AUDIO_MUSIC_CHANNEL_FIRST;
	mt32_driver_activate(driver, TEST_MUSIC_CHANNEL, &context, &source, TEST_NOTE, TEST_VELOCITY,
						 resource);
	const legacy_u8 note[] = {0x92, TEST_NOTE, TEST_VELOCITY};
	expect(output, note, sizeof(note));
	/* A release must stop the last retriggered pitch, rather than the original
	 * note, and an effect's modulation must remain distinct from pitch bend. */
	resource[53] = 1;
	resource[40] = 4;
	resource[25] = 2;
	context.sequence_value = 7;
	context.modulation = 42;
	context.level = 64;
	mt32_driver_update_context(driver, TEST_MUSIC_CHANNEL, &context, resource);
	const legacy_u8 update[] = {0x82, TEST_NOTE, 0, 0x92, TEST_NOTE + 7U, TEST_VELOCITY, 0xb2, 1,
								42,	  0xe2,		 0, 64};
	expect(output, update, sizeof(update));
	mt32_driver_stop(driver, TEST_MUSIC_CHANNEL, &context);
	const legacy_u8 stop[] = {0x82, TEST_NOTE + 7U, 0};
	expect(output, stop, sizeof(stop));
	resource[21] = 0;
	mt32_driver_activate(driver, TEST_MUSIC_CHANNEL, &context, &source, TEST_NOTE, 1, resource);
	const legacy_u8 fixed_velocity[] = {0x92, TEST_NOTE, 127};
	expect(output, fixed_velocity, sizeof(fixed_velocity));
}

static void check_program_and_muting(struct MT32_DRIVER *driver, struct MIDI_CAPTURE *output,
									 legacy_u8 *resource)
{
	struct AUDIO_CHANNEL music;
	struct AUDIO_CHANNEL effect;
	memset(&music, 0, sizeof(music));
	memset(&effect, 0, sizeof(effect));
	effect.channel = AUDIO_EFFECT_CHANNEL_FIRST;
	mt32_driver_prepare(driver, TEST_MUSIC_CHANNEL, &music, resource);
	assert(output->length == 19U);
	assert(output->bytes[0] == 0xc2);
	assert(output->bytes[1] == TEST_INSTRUMENT_PROGRAM);
	verify_sysex(output->bytes + 2U, 11U);
	assert(output->bytes[7] == 3U);
	assert(output->bytes[9] == 20U);
	assert(output->bytes[10] == 12U);
	assert(output->bytes[13] == 0xb2 && output->bytes[14] == 7);
	assert(output->bytes[16] == 0xb2 && output->bytes[17] == 10);
	/* Preparation generated a preliminary volume when ownership changed. It
	 * was already music-owned by check_notes, so the sequence above is stable. */
	output->length = 0;
	mt32_driver_prepare(driver, TEST_EFFECT_CHANNEL, &effect, resource);
	output->length = 0;
	mt32_driver_set_music_muted(driver, 1);
	const legacy_u8 muted[] = {0xb2, 7, 0};
	expect(output, muted, sizeof(muted));
	mt32_driver_volume(driver, TEST_MUSIC_CHANNEL, 88);
	expect(output, muted, sizeof(muted));
	mt32_driver_volume(driver, TEST_EFFECT_CHANNEL, 77);
	const legacy_u8 effect_volume[] = {0xb1, 7, 77};
	expect(output, effect_volume, sizeof(effect_volume));
	mt32_driver_set_music_muted(driver, 0);
	const legacy_u8 unmuted[] = {0xb2, 7, 88};
	expect(output, unmuted, sizeof(unmuted));
	/* An effect can claim a channel previously used by muted music. */
	mt32_driver_set_music_muted(driver, 1);
	output->length = 0;
	resource[69] = 0;
	mt32_driver_prepare(driver, TEST_MUSIC_CHANNEL, &effect, resource);
	assert(output->bytes[0] == 0xb2 && output->bytes[1] == 7 && output->bytes[2] == 88);
	output->length = 0;
}

static void check_rhythm_channel(struct MT32_DRIVER *driver, struct MIDI_CAPTURE *output,
								 legacy_u8 *resource)
{
	struct AUDIO_CHANNEL source;
	memset(&source, 0, sizeof(source));
	resource[69] = TEST_INSTRUMENT_VOLUME;
	mt32_driver_set_music_muted(driver, 0);
	output->length = 0;
	/* The actual MT music DRUM instrument uses MIDI channel 9. It must not
	 * send a malformed bend address or alias part 1's bend sensitivity. */
	const legacy_s16 channels[] = {0, 9, 10, 15};
	for (size_t index = 0; index < sizeof(channels) / sizeof(channels[0]); index++) {
		legacy_s16 channel = channels[index];
		driver->music_channels[channel] = 1;
		mt32_driver_prepare(driver, channel, &source, resource);
		const legacy_u8 expected[] = {0xc0U | (legacy_u8)channel,
									  TEST_INSTRUMENT_PROGRAM,
									  0xb0U | (legacy_u8)channel,
									  7,
									  TEST_INSTRUMENT_VOLUME,
									  0xb0U | (legacy_u8)channel,
									  10,
									  64};
		expect(output, expected, sizeof(expected));
	}
}

static void check_bank(struct MT32_DRIVER *driver, struct MIDI_CAPTURE *output)
{
	legacy_u8 bank[1U + TEST_PATCH_BYTES * 2U + TEST_TIMBRE_BYTES];
	memset(bank, 0, sizeof(bank));
	bank[0] = 2;
	bank[1] = 2;
	memcpy(bank + 1U + TEST_PATCH_BYTES, "Engine 1  ", 10U);
	assert(mt32_driver_load_bank(driver, bank, sizeof(bank)));
	size_t patch_packet = TEST_SYSEX_OVERHEAD + TEST_PATCH_BYTES;
	size_t timbre_packet = TEST_SYSEX_OVERHEAD + TEST_TIMBRE_BYTES;
	assert(output->length == patch_packet * 2U + timbre_packet);
	verify_sysex(output->bytes, patch_packet);
	assert(output->bytes[5] == 5 && output->bytes[6] == 0 && output->bytes[7] == 0);
	verify_sysex(output->bytes + patch_packet, timbre_packet);
	assert(output->bytes[patch_packet + 5U] == 8);
	assert(memcmp(output->bytes + patch_packet + 8U, "Engine 1  ", 10U) == 0);
	verify_sysex(output->bytes + patch_packet + timbre_packet, patch_packet);
	assert(output->bytes[patch_packet + timbre_packet + 7U] == TEST_PATCH_BYTES);
	output->length = 0;
	assert(!mt32_driver_load_bank(driver, bank, sizeof(bank) - 1U));
	assert(output->length == 0);
	bank[2] = 128;
	assert(!mt32_driver_load_bank(driver, bank, sizeof(bank)));
	assert(output->length == 0);
	mt32_driver_master_volume(driver, 100);
	verify_sysex(output->bytes, output->length);
	assert(output->bytes[5] == 16 && output->bytes[6] == 0 && output->bytes[7] == 22);
	assert(output->bytes[8] == 100);
	output->length = 0;
}

legacy_int main(void)
{
	struct MIDI_CAPTURE output;
	struct MT32_DRIVER driver;
	memset(&output, 0, sizeof(output));
	mt32_driver_initialize(&driver, capture, &output);
	assert(output.length == MT32_MIDI_CHANNEL_COUNT * 6U);
	output.length = 0;
	legacy_u8 resource[TEST_RESOURCE_BYTES];
	instrument(resource);
	check_notes(&driver, &output, resource);
	check_program_and_muting(&driver, &output, resource);
	check_rhythm_channel(&driver, &output, resource);
	check_bank(&driver, &output);
	puts("MT-32 driver protocol checks passed.");
	return 0;
}
