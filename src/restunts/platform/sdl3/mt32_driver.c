/* MIDI/MT-32 protocol adapter for the game's existing native sequencer.
 * Instrument offsets and callback behavior are verified against the original
 * MT15.DRV supplied with the supported game data; no DOS code is executed. */
#include "mt32_driver.h"
#include "../../c/audio/audio_internal.h"
#include <string.h>

#define MT32_MIDI_DATA_MASK 127U
#define MT32_MIDI_CHANNEL_MASK 15U
#define MT32_MIDI_DATA_BITS 7U
#define MT32_NOTE_OFF 0x80U
#define MT32_NOTE_ON 0x90U
#define MT32_CONTROL_CHANGE 0xb0U
#define MT32_PROGRAM_CHANGE 0xc0U
#define MT32_PITCH_BEND 0xe0U
#define MT32_CONTROL_MODULATION 1U
#define MT32_CONTROL_VOLUME 7U
#define MT32_CONTROL_PAN 10U
#define MT32_CONTROL_ALL_SOUNDS_OFF 120U
#define MT32_CONTROL_RESET 121U
#define MT32_CONTROL_ALL_NOTES_OFF 123U
#define MT32_PATCH_TEMP_ADDRESS 3U
#define MT32_PATCH_MEMORY_ADDRESS 5U
#define MT32_TIMBRE_MEMORY_ADDRESS 8U
#define MT32_SYSTEM_ADDRESS 16U
#define MT32_MASTER_VOLUME_ADDRESS 22U
#define MT32_PART_CHANNEL_FIRST 1U
#define MT32_PART_CHANNEL_LAST 8U
#define MT32_PATCH_TEMP_STRIDE 16U
#define MT32_PATCH_BEND_RANGE_OFFSET 4U
#define MT32_PATCH_BYTES 8U
#define MT32_TIMBRE_BYTES 246U
#define MT32_TIMBRE_GROUP_MEMORY 2U
#define MT32_SYSEX_PREFIX_BYTES 5U
#define MT32_SYSEX_ADDRESS_BYTES 3U
#define MT32_SYSEX_TRAILER_BYTES 2U
#define MT32_SYSEX_PAYLOAD_MAX MT32_TIMBRE_BYTES
#define MT32_BANK_MAX_PATCHES 128U
#define MT32_BANK_MAX_TIMBRES 64U
#define MT32_RESOURCE_SIZE 93U
#define MT32_RESOURCE_VELOCITY_OFFSET 21U
#define MT32_RESOURCE_BEND_RANGE_OFFSET 18U
#define MT32_RESOURCE_ENVELOPE_ROUTE_OFFSET 25U
#define MT32_RESOURCE_MODULATION_ROUTE_OFFSET 40U
#define MT32_RESOURCE_NOTE_SEQUENCE_OFFSET 53U
#define MT32_RESOURCE_PROGRAM_OFFSET 68U
#define MT32_RESOURCE_VOLUME_OFFSET 69U
#define MT32_RESOURCE_PAN_OFFSET 70U
#define MT32_CONTEXT_BASE_NOTE 0U
#define MT32_CONTEXT_VELOCITY 1U
#define MT32_CONTEXT_PLAYING_NOTE 3U
#define MT32_ROUTE_BEND 2U
#define MT32_ROUTE_MODULATION 4U

static legacy_u8 mt32_valid_channel(legacy_s16 channel)
{
	return channel >= 0 && (legacy_u16)channel < MT32_MIDI_CHANNEL_COUNT;
}

void mt32_driver_send_data(struct MT32_DRIVER *driver, const legacy_u8 *data, size_t length)
{
	if (driver->sink != NULL && data != NULL && length != 0) {
		driver->sink(driver->instance, data, length);
	}
}

static void mt32_message(struct MT32_DRIVER *driver, legacy_s16 channel, legacy_u8 command,
						 legacy_u16 first, legacy_u16 second, size_t length)
{
	if (!mt32_valid_channel(channel)) {
		return;
	}
	legacy_u8 bytes[] = {command | ((legacy_u8)channel & MT32_MIDI_CHANNEL_MASK),
						 first & MT32_MIDI_DATA_MASK, second & MT32_MIDI_DATA_MASK};
	mt32_driver_send_data(driver, bytes, length);
}

static void mt32_sysex(struct MT32_DRIVER *driver, const legacy_u8 address[3],
					   const legacy_u8 *data, size_t length)
{
	if (length > MT32_SYSEX_PAYLOAD_MAX) {
		return;
	}
	legacy_u8 bytes[MT32_SYSEX_PREFIX_BYTES + MT32_SYSEX_ADDRESS_BYTES + MT32_SYSEX_PAYLOAD_MAX +
					MT32_SYSEX_TRAILER_BYTES];
	static const legacy_u8 header[MT32_SYSEX_PREFIX_BYTES] = {0xf0U, 0x41U, 0x10U, 0x16U, 0x12U};
	memcpy(bytes, header, sizeof(header));
	memcpy(bytes + sizeof(header), address, MT32_SYSEX_ADDRESS_BYTES);
	memcpy(bytes + sizeof(header) + MT32_SYSEX_ADDRESS_BYTES, data, length);
	legacy_u32 sum = 0;
	for (size_t index = sizeof(header); index < sizeof(header) + MT32_SYSEX_ADDRESS_BYTES + length;
		 index++) {
		sum += bytes[index];
	}
	size_t end = sizeof(header) + MT32_SYSEX_ADDRESS_BYTES + length;
	bytes[end++] = (legacy_u8)(-sum & MT32_MIDI_DATA_MASK);
	bytes[end++] = 0xf7U;
	mt32_driver_send_data(driver, bytes, end);
}

static legacy_u8 mt32_output_volume(const struct MT32_DRIVER *driver, legacy_s16 channel)
{
	return driver->music_muted && driver->music_channels[channel] ? 0 : driver->volumes[channel];
}

void mt32_driver_volume(struct MT32_DRIVER *driver, legacy_s16 channel, legacy_u16 volume)
{
	if (!mt32_valid_channel(channel)) {
		return;
	}
	driver->volumes[channel] = volume & MT32_MIDI_DATA_MASK;
	mt32_message(driver, channel, MT32_CONTROL_CHANGE, MT32_CONTROL_VOLUME,
				 mt32_output_volume(driver, channel), 3U);
}

void mt32_driver_control(struct MT32_DRIVER *driver, legacy_s16 channel, legacy_u16 control,
						 legacy_u16 value)
{
	if (control == MT32_CONTROL_VOLUME) {
		mt32_driver_volume(driver, channel, value);
	} else {
		mt32_message(driver, channel, MT32_CONTROL_CHANGE, control, value, 3U);
	}
}

void mt32_driver_bend(struct MT32_DRIVER *driver, legacy_s16 channel, legacy_u16 value)
{
	mt32_message(driver, channel, MT32_PITCH_BEND, value, value >> MT32_MIDI_DATA_BITS, 3U);
}

void mt32_driver_silence_channel(struct MT32_DRIVER *driver, legacy_s16 channel)
{
	mt32_driver_control(driver, channel, MT32_CONTROL_ALL_NOTES_OFF, 0);
}

void mt32_driver_reset(struct MT32_DRIVER *driver)
{
	for (legacy_s16 channel = 0; (legacy_u16)channel < MT32_MIDI_CHANNEL_COUNT; channel++) {
		mt32_driver_silence_channel(driver, channel);
		mt32_driver_control(driver, channel, MT32_CONTROL_RESET, 0);
	}
}

void mt32_driver_initialize(struct MT32_DRIVER *driver, mt32_midi_sink sink, void *instance)
{
	memset(driver, 0, sizeof(*driver));
	driver->sink = sink;
	driver->instance = instance;
	memset(driver->volumes, AUDIO_ENGINE_MAX_VOLUME, sizeof(driver->volumes));
	mt32_driver_reset(driver);
}

void mt32_driver_set_music_muted(struct MT32_DRIVER *driver, legacy_u8 muted)
{
	muted = muted != 0;
	if (driver->music_muted == muted) {
		return;
	}
	driver->music_muted = muted;
	for (legacy_s16 channel = 0; (legacy_u16)channel < MT32_MIDI_CHANNEL_COUNT; channel++) {
		if (driver->music_channels[channel]) {
			mt32_message(driver, channel, MT32_CONTROL_CHANGE, MT32_CONTROL_VOLUME,
						 mt32_output_volume(driver, channel), 3U);
		}
	}
}

void mt32_driver_prepare(struct MT32_DRIVER *driver, legacy_s16 channel,
						 struct AUDIO_CHANNEL *source, const legacy_u8 *resource)
{
	if (!mt32_valid_channel(channel) || resource == NULL ||
		LEGACY_READ_U32_LE(resource) < MT32_RESOURCE_SIZE) {
		return;
	}
	legacy_u8 was_music = driver->music_channels[channel];
	driver->music_channels[channel] =
		source != NULL && source->channel < AUDIO_EFFECT_CHANNEL_FIRST;
	if (was_music != driver->music_channels[channel]) {
		/* An effect may reuse a channel muted for replacement music. */
		mt32_driver_volume(driver, channel, driver->volumes[channel]);
	}
	mt32_message(driver, channel, MT32_PROGRAM_CHANGE, resource[MT32_RESOURCE_PROGRAM_OFFSET], 0,
				 2U);
	/* MT15 1.1 programs bend sensitivity through Roland SysEx. This also works
	 * on the original MT-32, whose firmware does not implement the GM RPN. */
	if ((legacy_u16)channel >= MT32_PART_CHANNEL_FIRST &&
		(legacy_u16)channel <= MT32_PART_CHANNEL_LAST) {
		legacy_u8 part = (legacy_u8)(channel - MT32_PART_CHANNEL_FIRST);
		legacy_u8 address[] = {
			MT32_PATCH_TEMP_ADDRESS, 0,
			(legacy_u8)(part * MT32_PATCH_TEMP_STRIDE + MT32_PATCH_BEND_RANGE_OFFSET)};
		mt32_sysex(driver, address, resource + MT32_RESOURCE_BEND_RANGE_OFFSET, 1U);
	}
	/* Rhythm and unassigned channels have no melodic part bend range. MT15's
	 * out-of-range address is ignored by hardware; masking it would overwrite
	 * a different part's settings. */
	if (resource[MT32_RESOURCE_VOLUME_OFFSET] != 0) {
		mt32_driver_volume(driver, channel, resource[MT32_RESOURCE_VOLUME_OFFSET]);
	}
	mt32_driver_control(driver, channel, MT32_CONTROL_PAN, resource[MT32_RESOURCE_PAN_OFFSET]);
}

void mt32_driver_activate(struct MT32_DRIVER *driver, legacy_s16 channel,
						  struct AUDIO_CONTEXT *context, struct AUDIO_CHANNEL *source,
						  legacy_s16 pitch, legacy_u16 velocity, const legacy_u8 *resource)
{
	if (!mt32_valid_channel(channel) || context == NULL || resource == NULL) {
		return;
	}
	driver->music_channels[channel] =
		source != NULL && source->channel < AUDIO_EFFECT_CHANNEL_FIRST;
	context->driver_private_state[MT32_CONTEXT_BASE_NOTE] = (legacy_u8)pitch;
	context->driver_private_state[MT32_CONTEXT_PLAYING_NOTE] = (legacy_u8)pitch;
	context->driver_private_state[MT32_CONTEXT_VELOCITY] = resource[MT32_RESOURCE_VELOCITY_OFFSET]
															   ? velocity & MT32_MIDI_DATA_MASK
															   : AUDIO_ENGINE_MAX_VOLUME;
	mt32_message(driver, channel, MT32_NOTE_ON, pitch,
				 context->driver_private_state[MT32_CONTEXT_VELOCITY], 3U);
}

void mt32_driver_stop(struct MT32_DRIVER *driver, legacy_s16 channel, struct AUDIO_CONTEXT *context)
{
	if (context != NULL) {
		mt32_message(driver, channel, MT32_NOTE_OFF,
					 context->driver_private_state[MT32_CONTEXT_PLAYING_NOTE], 0, 3U);
	}
}

static void mt32_context_route(struct MT32_DRIVER *driver, legacy_s16 channel, legacy_u8 route,
							   legacy_s16 value)
{
	if (route == MT32_ROUTE_MODULATION) {
		mt32_driver_control(driver, channel, MT32_CONTROL_MODULATION, (legacy_u8)value);
	} else if (route == MT32_ROUTE_BEND) {
		mt32_driver_bend(
			driver, channel,
			(legacy_u16)(((legacy_u8)value & MT32_MIDI_DATA_MASK) << MT32_MIDI_DATA_BITS));
	}
}

void mt32_driver_update_context(struct MT32_DRIVER *driver, legacy_s16 channel,
								struct AUDIO_CONTEXT *context, const legacy_u8 *resource)
{
	if (!mt32_valid_channel(channel) || context == NULL || resource == NULL ||
		context->state == 0) {
		return;
	}
	legacy_u8 note =
		context->driver_private_state[MT32_CONTEXT_BASE_NOTE] + context->sequence_value;
	if (resource[MT32_RESOURCE_NOTE_SEQUENCE_OFFSET] == 1U &&
		note != context->driver_private_state[MT32_CONTEXT_PLAYING_NOTE]) {
		mt32_driver_stop(driver, channel, context);
		context->driver_private_state[MT32_CONTEXT_PLAYING_NOTE] = note;
		mt32_message(driver, channel, MT32_NOTE_ON, note,
					 context->driver_private_state[MT32_CONTEXT_VELOCITY], 3U);
	}
	mt32_context_route(driver, channel, resource[MT32_RESOURCE_MODULATION_ROUTE_OFFSET],
					   context->modulation);
	mt32_context_route(driver, channel, resource[MT32_RESOURCE_ENVELOPE_ROUTE_OFFSET],
					   context->level);
}

static legacy_int mt32_bank_size_valid(const legacy_u8 *bank, size_t length)
{
	if (bank == NULL || length == 0 || bank[0] > MT32_BANK_MAX_PATCHES) {
		return 0;
	}
	size_t position = 1;
	legacy_u32 timbres = 0;
	for (legacy_u32 index = 0; index < bank[0]; index++) {
		if (length - position < MT32_PATCH_BYTES) {
			return 0;
		}
		legacy_u8 memory = bank[position] == MT32_TIMBRE_GROUP_MEMORY;
		size_t size = MT32_PATCH_BYTES + (memory ? MT32_TIMBRE_BYTES : 0U);
		if (length - position < size || (memory && ++timbres > MT32_BANK_MAX_TIMBRES)) {
			return 0;
		}
		for (size_t byte = 0; byte < size; byte++) {
			if (bank[position + byte] > MT32_MIDI_DATA_MASK) {
				return 0;
			}
		}
		position += size;
	}
	return 1;
}

legacy_int mt32_driver_load_bank(struct MT32_DRIVER *driver, const legacy_u8 *bank, size_t length)
{
	if (!mt32_bank_size_valid(bank, length)) {
		return 0;
	}
	size_t position = 1;
	legacy_u32 timbre = 0;
	for (legacy_u32 patch = 0; patch < bank[0]; patch++) {
		legacy_u8 memory = bank[position] == MT32_TIMBRE_GROUP_MEMORY;
		legacy_u32 offset = patch * MT32_PATCH_BYTES;
		legacy_u8 address[] = {MT32_PATCH_MEMORY_ADDRESS, offset >> MT32_MIDI_DATA_BITS,
							   offset & MT32_MIDI_DATA_MASK};
		mt32_sysex(driver, address, bank + position, MT32_PATCH_BYTES);
		position += MT32_PATCH_BYTES;
		if (memory) {
			address[0] = MT32_TIMBRE_MEMORY_ADDRESS;
			address[1] = (legacy_u8)(timbre++ * 2U);
			address[2] = 0;
			mt32_sysex(driver, address, bank + position, MT32_TIMBRE_BYTES);
			position += MT32_TIMBRE_BYTES;
		}
	}
	return 1;
}

void mt32_driver_master_volume(struct MT32_DRIVER *driver, legacy_u8 volume)
{
	legacy_u8 address[] = {MT32_SYSTEM_ADDRESS, 0, MT32_MASTER_VOLUME_ADDRESS};
	mt32_sysex(driver, address, &volume, 1U);
}
