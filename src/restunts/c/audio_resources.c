#include <stddef.h>
#include "audio.h"
#include "audio_internal.h"
#include "externs.h"
#include "fatal.h"
#include "fileio.h"
#include "legacy.h"
#include "memmgr.h"
#include "platform.h"
#include "resource.h"
#include "timing.h"
#include "audio_control.h"
#include "resource_bytes.h"
#include "residue.h"
#ifdef RESTUNTS_SDL3
#include "../platform/sdl3/music.h"
#endif

#define AUDIO_DRIVER_PREFIX_LENGTH 14U
#define AUDIO_PADDED_ID_LENGTH (AUDIO_RESOURCE_ID_LENGTH + 1U)
#define AUDIO_FILENAME_BUFFER_LENGTH 34U
#define AUDIO_FILENAME_EXTENSION_LENGTH 4U
#define AUDIO_SPECIAL_DRIVER_MODE 18234
#define AUDIO_DRIVER_LOAD_FAILURE 2
#define AUDIO_DEFAULT_RATE 127U
#define AUDIO_DRIVER_CHANNEL_COUNT_ERROR 255U
#define AUDIO_DIRECT_CHANNEL_FLAG_THRESHOLD 127U
#define AUDIO_DEFAULT_MASTER_VOLUME 100U
#define AUDIO_DRIVER_MASTER_STATE_COMMAND 4
#define DOS_SEGMENT_WRAP_PARAGRAPHS 4096U
#define AUDIO_RESOURCE_CHUNK_COUNT_OFFSET 4U
#define AUDIO_RESOURCE_TABLE_OFFSET 6U
#define AUDIO_RESOURCE_OFFSET_ENTRY_SIZE 4U
#define AUDIO_RESOURCE_DIRECTORY_ENTRY_SIZE 8U
#define AUDIO_RESOURCE_CHUNK_LENGTH_SIZE 4U
#define AUDIO_SONG_INSTRUMENT_COUNT_OFFSET 6U
#define AUDIO_SONG_INSTRUMENT_TABLE_OFFSET 7U
#define AUDIO_FAR_POINTER_WORD_SHIFT 8U
#define AUDIO_SEQUENCE_REFERENCE_RECORD_SIZE 5U
#define AUDIO_SEQUENCE_COMMAND_ONLY_SIZE 1U
#define AUDIO_SEQUENCE_BYTE_ARGUMENT_SIZE 2U
#define AUDIO_SEQUENCE_WORD_ARGUMENT_SIZE 3U
#define AUDIO_SEQUENCE_CALL_PREFIX_SIZE 2U
#define AUDIO_SONG_HEADER_PREFIX_SIZE 2U
#define AUDIO_SONG_HEADER_MAPPED_FLAG_OFFSET 5U
#define AUDIO_SONG_HEADER_MAPPED 1U
#define AUDIO_SONG_DATA_SCALE_OFFSET 4U
#define AUDIO_SONG_DATA_SCALE_SHIFT 3U
#define AUDIO_SONG_DATA_PREFIX_SIZE 1U
#define AUDIO_RESOURCE_TYPE_OFFSET 4U
#define AUDIO_RESOURCE_READY_FLAG_OFFSET 5U
#define AUDIO_RESOURCE_READY 1U
#define AUDIO_ENGINE_INITIAL_TICK_STEP 128U
#define AUDIO_MUSIC_CHANNEL_COUNT_SCALE_SHIFT 2U
#define AUDIO_MUSIC_CHANNEL_COUNT_OFFSET 7U
#define AUDIO_MUSIC_PRIORITY 32U

static legacy_s16 audio_missing_file_fatal;
static legacy_s8 audio_driver_prefix[AUDIO_DRIVER_PREFIX_LENGTH];
static legacy_u8 audio_padded_id[AUDIO_PADDED_ID_LENGTH];
static legacy_s8 audio_filename_buffer[AUDIO_FILENAME_BUFFER_LENGTH];

static legacy_s16 legacy_toupper(legacy_s16 ch)
{
	if (ch >= 'a' && ch <= 'z') {
		ch -= ' ';
	}

	return ch;
}

legacy_s8 *pad_id(const legacy_s8 far *source)
{
	legacy_u8 *destination = audio_padded_id;
	for (legacy_u16 index = 0; index < AUDIO_RESOURCE_ID_LENGTH; index++) {
		destination[index] = (legacy_u8)source[index];
		if (destination[index] == 0) {
			destination[index] = ' ';
		}
	}
	audio_padded_id[AUDIO_RESOURCE_ID_LENGTH] = 0;
	return (legacy_s8 *)destination;
}

void audioresource_copy_4_bytes(legacy_u8 far *destination, const legacy_u8 far *source)
{
	destination[0] = source[0];
	destination[1] = source[1];
	destination[2] = source[2];
	destination[3] = source[3];
}

static void audio_append_filename_part(legacy_s8 *destination, const legacy_s8 *source)
{
	while (*destination != 0) {
		destination++;
	}
	do {
		*destination++ = *source;
	} while (*source++ != 0);
}

static const legacy_s8 *audio_find_last_backslash(const legacy_s8 *text)
{
	const legacy_s8 *match = 0;
	while (*text != 0) {
		if (*text == '\\') {
			match = text;
		}
		text++;
	}
	return match;
}

legacy_s8 *audio_make_filename(const legacy_s8 *filename, const legacy_s8 *extension,
							   const legacy_s8 *inserted_path)
{
	legacy_s8 *separator = audio_filename_buffer;
	const legacy_s8 *source = filename;
	do {
		*separator++ = *source;
	} while (*source++ != 0);
	separator = (legacy_s8 *)audio_find_last_backslash(audio_filename_buffer);
	if (separator != 0) {
		separator[1] = 0;
	} else {
		audio_filename_buffer[0] = 0;
	}

	audio_append_filename_part(audio_filename_buffer, inserted_path);
	const legacy_s8 *basename = audio_find_last_backslash(filename);
	if (basename != 0) {
		basename++;
	} else {
		basename = filename;
	}
	audio_append_filename_part(audio_filename_buffer, basename);

	legacy_u16 length = 0;
	while (audio_filename_buffer[length] != 0) {
		length++;
	}
	if (length <= AUDIO_FILENAME_EXTENSION_LENGTH ||
		audio_filename_buffer[length - AUDIO_FILENAME_EXTENSION_LENGTH] != '.') {
		audio_append_filename_part(audio_filename_buffer, ".");
		audio_append_filename_part(audio_filename_buffer, extension);
	}
	return audio_filename_buffer;
}

static void audio_load_driver_bank(void)
{
	static const legacy_s8 mt32_bank_filename[] = "mt32.plb";
	if (dos_audio_uses_direct_channels != 0) {
		void far *bank = file_load_binary_nofatal(mt32_bank_filename);
		if (bank != 0) {
			dos_audio_driver_load_bank(bank);
			mmgr_release((legacy_s8 far *)bank);
			dos_audio_master_volume = AUDIO_DEFAULT_MASTER_VOLUME;
			dos_audio_driver_set_master_state(AUDIO_DRIVER_MASTER_STATE_COMMAND,
											  (void far *)dos_audio_master_state);
		}
	}
}

legacy_s16 audio_load_dos_driver(const legacy_s8 *driver, legacy_s16 unused, legacy_s16 mode)
{
	(void)unused;
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	/* Native builds implement the AdLib driver in C; executable DOS drivers
	 * cannot run in their flat address space. Always select its instruments. */
	(void)driver;
	(void)mode;
	if (dos_audio_driver_binary != 0) {
		dos_audio_shutdown();
	} else {
		add_exit_handler(dos_audio_shutdown);
	}
	audio_driver_prefix[0] = 'a';
	audio_driver_prefix[1] = 'd';
	audio_driver_prefix[2] = 0;
	dos_audio_special_mode = 0;
	dos_audio_uses_direct_channels = 0;
	audio_music_rate = AUDIO_DEFAULT_RATE;
	audio_effect_rate = AUDIO_DEFAULT_RATE;
	legacy_u8 channel_count = dos_audio_driver_initialize();
	dos_audio_context_count = channel_count;
	if (channel_count == 0 || channel_count > AUDIO_CONTEXT_COUNT) {
		return AUDIO_DRIVER_LOAD_FAILURE;
	}
	/* This is an initialization marker only, never an executable allocation. */
	dos_audio_driver_binary = audio_driver_prefix;
#else
	if (mode == AUDIO_SPECIAL_DRIVER_MODE) {
		dos_audio_special_mode = 1;
	}
	if (dos_audio_driver_binary != 0) {
		dos_audio_shutdown();
	} else {
		add_exit_handler(dos_audio_shutdown);
	}
	dos_audio_driver_binary = 0;

	legacy_u16 driver_length = 0;
	while (driver[driver_length] != 0) {
		driver_length++;
	}
	legacy_u16 basename_offset = 0;
	for (legacy_u16 scan_offset = driver_length; scan_offset != 0; scan_offset--) {
		if (driver[scan_offset] == '\\' || driver[scan_offset] == ':') {
			basename_offset = LEGACY_U16_WRAP_ADD(scan_offset, 1U);
			break;
		}
	}
	audio_driver_prefix[0] = driver[basename_offset];
	audio_driver_prefix[1] = driver[basename_offset + 1U];
	audio_driver_prefix[2] = 0;

	static const legacy_s8 empty_path[] = "";
	static const legacy_s8 driver_extension[] = "drv";
	dos_audio_driver_binary =
		file_load_binary_nofatal(audio_make_filename(driver, driver_extension, empty_path));
	audio_music_rate = AUDIO_DEFAULT_RATE;
	audio_effect_rate = AUDIO_DEFAULT_RATE;
	static const legacy_s8 missing_driver_message[] = "Can't find driver!\n";
	if (dos_audio_driver_binary == 0) {
		fatal_error(missing_driver_message);
		return AUDIO_DRIVER_LOAD_FAILURE;
	}

	legacy_u8 channel_count = dos_audio_driver_initialize();
	dos_audio_context_count = channel_count;
	if (channel_count == 0 || channel_count == AUDIO_DRIVER_CHANNEL_COUNT_ERROR) {
		return AUDIO_DRIVER_LOAD_FAILURE;
	}
	if (channel_count > AUDIO_DIRECT_CHANNEL_FLAG_THRESHOLD) {
		dos_audio_context_count = AUDIO_CONTEXT_COUNT;
		dos_audio_uses_direct_channels = 1;
		dos_audio_special_mode = 0;
	}

#endif
	audio_reset_channels();
	timer_reg_callback(audio_sequence_timer);
	audio_load_driver_bank();
	audio_suspended = AUDIO_STATE_DISABLED;
	audio_music_enabled = AUDIO_STATE_ENABLED;
	audio_music_active = AUDIO_STATE_DISABLED;
	audio_effects_enabled = AUDIO_STATE_ENABLED;
	return 0;
}

void far *load_sfx_ge(const legacy_s8 *filename, const legacy_s8 *extension,
					  const legacy_s8 *inserted_path)
{
	void far *result =
		file_load_binary_nofatal(audio_make_filename(filename, extension, inserted_path));
	if (result != 0) {
		return result;
	}

	legacy_s8 compressed_extension[AUDIO_RESOURCE_ID_LENGTH];
	compressed_extension[0] = 'P';
	compressed_extension[1] = extension[0];
	compressed_extension[2] = extension[1];
	compressed_extension[3] = 0;
	result =
		file_decomp_nofatal(audio_make_filename(filename, compressed_extension, inserted_path));
	if (result != 0) {
		return result;
	}

	result = file_load_binary_nofatal(audio_make_filename(filename, extension, "ge"));
	if (result != 0) {
		return result;
	}

	result = file_decomp_nofatal(audio_make_filename(filename, compressed_extension, "ge"));
	if (result != 0) {
		return result;
	}

	result = file_load_binary_nofatal(audio_make_filename(filename, extension, ""));
	if (result != 0) {
		return result;
	}

	result = file_decomp_nofatal(audio_make_filename(filename, compressed_extension, ""));
	if (result != 0) {
		return result;
	}

	return file_load_binary_nofatal(filename);
}

/* The special audio modes keep their samples under their own extension and
 * fall back to the standard one before the load counts as a failure. */
static void far *load_audio_file(const legacy_s8 *filename, const legacy_s8 *special_extension,
								 const legacy_s8 *extension, const legacy_s8 *error_message)
{
	void far *result = 0;
	if (dos_audio_special_mode != 0) {
		result = load_sfx_ge(filename, special_extension, audio_driver_prefix);
	}
	if (result == 0) {
		result = load_sfx_ge(filename, extension, audio_driver_prefix);
	}
	if (result == 0 && audio_missing_file_fatal != 0) {
		fatal_error(error_message, filename);
	}
	return result;
}

void far *load_sfx_file(const legacy_s8 *filename)
{
	return load_audio_file(filename, "dsf", "sfx", "cannot load sfx file %s");
}

void far *load_song_file(const legacy_s8 *filename)
{
	void far *result = load_sfx_ge(filename, "kms", audio_driver_prefix);
	if (result == 0 && audio_missing_file_fatal != 0) {
		fatal_error("cannot load song file %s", filename);
	}
	return result;
}

void far *load_voice_file(const legacy_s8 *filename)
{
	return load_audio_file(filename, "dvc", "vce", "cannot load voice file %s");
}

legacy_s16 audioresource_compare_chunknames(legacy_s16 case_sensitive,
											const legacy_s8 far *first_name,
											const legacy_s8 far *second_name, legacy_s16 count)
{
	legacy_u16 remaining = (legacy_u16)count;
	if (remaining == 0) {
		return 1;
	}
#if !defined(RESTUNTS_SDL3) && !defined(RESTUNTS_AMIGA)
	legacy_u16 first_offset = (legacy_u16)dos_memory_pointer_offset(first_name);
	legacy_u16 first_segment = (legacy_u16)dos_memory_pointer_segment(first_name);
	legacy_u16 second_offset = (legacy_u16)dos_memory_pointer_offset(second_name);
	legacy_u16 second_segment = (legacy_u16)dos_memory_pointer_segment(second_name);
#endif
	do {
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
		/* Host objects may straddle a registered 64 KiB page. Advance the
		 * pointers themselves, not offsets inside one DOS segment alias. */
		legacy_u8 first = (legacy_u8)*first_name++;
		legacy_u8 second = (legacy_u8)*second_name++;
#else
		legacy_u8 first =
			*(const legacy_u8 far *)dos_memory_make_pointer(first_segment, first_offset);
		legacy_u8 second =
			*(const legacy_u8 far *)dos_memory_make_pointer(second_segment, second_offset);
#endif
		if (first == 0 || second == 0) {
			return 1;
		}
		if (case_sensitive != 0) {
			if (first != second) {
				return 0;
			}
		} else if (legacy_toupper(second) != legacy_toupper(first)) {
			return 0;
		}
#if !defined(RESTUNTS_SDL3) && !defined(RESTUNTS_AMIGA)
		first_offset = LEGACY_U16_WRAP_ADD(first_offset, 1U);
		second_offset = LEGACY_U16_WRAP_ADD(second_offset, 1U);
#endif
		remaining--;
	} while (remaining != 0);
	return 1;
}

legacy_s16 audioresource_get_chunk_index(legacy_s16 extra_name_stride, legacy_s16 chunk_count,
										 const legacy_s8 *requested_name,
										 const legacy_u8 far *chunk_names)
{
	legacy_s16 count = LEGACY_S16_FROM_BITS(chunk_count);
	if (count <= 0) {
		return -1;
	}
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	const legacy_s8 *requested_name_far = requested_name;
	const legacy_u8 *candidate = chunk_names;
#else
	const legacy_s8 far *requested_name_far = (const legacy_s8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(requested_name), dos_memory_pointer_offset(requested_name));
	legacy_u16 names_offset = (legacy_u16)dos_memory_pointer_offset(chunk_names);
	legacy_u16 names_segment = (legacy_u16)dos_memory_pointer_segment(chunk_names);
#endif
	for (legacy_s16 index = 0; index < count; index = LEGACY_S16_WRAP_ADD(index, 1)) {
#if !defined(RESTUNTS_SDL3) && !defined(RESTUNTS_AMIGA)
		const legacy_u8 far *candidate =
			(const legacy_u8 far *)dos_memory_make_pointer(names_segment, names_offset);
#endif
		if (audioresource_compare_chunknames(0, (const legacy_s8 far *)candidate,
											 requested_name_far, AUDIO_RESOURCE_ID_LENGTH)) {
			return index;
		}
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
		candidate += LEGACY_U16_WRAP_ADD(AUDIO_RESOURCE_ID_LENGTH, extra_name_stride);
#else
		names_offset = LEGACY_U16_WRAP_ADD(
			names_offset, LEGACY_U16_WRAP_ADD(AUDIO_RESOURCE_ID_LENGTH, extra_name_stride));
#endif
	}
	return -1;
}

static void far *audio_far_pointer_add_normalized(void far *pointer, legacy_u16 increment)
{
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	return (legacy_u8 *)pointer + increment;
#else
	legacy_u16 old_offset = (legacy_u16)dos_memory_pointer_offset(pointer);
	legacy_u16 segment = (legacy_u16)dos_memory_pointer_segment(pointer);
	legacy_u16 new_offset = LEGACY_U16_WRAP_ADD(old_offset, increment);
	if (new_offset < old_offset) {
		segment = LEGACY_U16_WRAP_ADD(segment, DOS_SEGMENT_WRAP_PARAGRAPHS);
	}
	return dos_memory_make_pointer(segment, new_offset);
#endif
}

void far *audioresource_find(void far *resource, const legacy_s8 *chunk_name)
{
	legacy_u8 far *bytes = (legacy_u8 far *)resource;
#if !defined(RESTUNTS_SDL3) && !defined(RESTUNTS_AMIGA)
	legacy_u16 resource_offset = (legacy_u16)dos_memory_pointer_offset(resource);
	legacy_u16 resource_segment = (legacy_u16)dos_memory_pointer_segment(resource);
#endif
	legacy_u16 chunk_count =
		resource_read_u16le((const legacy_u8 far *)audio_far_pointer_add_normalized(
			bytes, AUDIO_RESOURCE_CHUNK_COUNT_OFFSET));
	legacy_s16 chunk_index =
		audioresource_get_chunk_index(0, chunk_count, chunk_name,
									  (const legacy_u8 far *)audio_far_pointer_add_normalized(
										  bytes, AUDIO_RESOURCE_TABLE_OFFSET));
	if (chunk_index < 0) {
		return 0;
	}

#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	const legacy_u8 *offset_entry = bytes + AUDIO_RESOURCE_TABLE_OFFSET +
									(legacy_u32)chunk_count * AUDIO_RESOURCE_ID_LENGTH +
									(legacy_u32)chunk_index * AUDIO_RESOURCE_OFFSET_ENTRY_SIZE;
	legacy_u16 relative_offset = (legacy_u16)resource_read_u32le(offset_entry);
	return bytes + AUDIO_RESOURCE_TABLE_OFFSET +
		   (legacy_u32)chunk_count * AUDIO_RESOURCE_DIRECTORY_ENTRY_SIZE + relative_offset;
#else
	legacy_u16 table_offset = LEGACY_U16_WRAP_ADD(
		resource_offset, LEGACY_U16_WRAP_MUL(chunk_count, AUDIO_RESOURCE_ID_LENGTH));
	table_offset = LEGACY_U16_WRAP_ADD(
		table_offset, LEGACY_U16_WRAP_MUL(chunk_index, AUDIO_RESOURCE_OFFSET_ENTRY_SIZE));
	table_offset = LEGACY_U16_WRAP_ADD(table_offset, AUDIO_RESOURCE_TABLE_OFFSET);
	legacy_u8 far *offset_entry =
		(legacy_u8 far *)dos_memory_make_pointer(resource_segment, table_offset);
	legacy_u16 relative_offset = (legacy_u16)resource_read_u32le(offset_entry);
	legacy_u16 result_offset = LEGACY_U16_WRAP_ADD(
		resource_offset, LEGACY_U16_WRAP_MUL(chunk_count, AUDIO_RESOURCE_DIRECTORY_ENTRY_SIZE));
	result_offset = LEGACY_U16_WRAP_ADD(result_offset, relative_offset);
	result_offset = LEGACY_U16_WRAP_ADD(result_offset, AUDIO_RESOURCE_TABLE_OFFSET);
	return dos_memory_make_pointer(resource_segment, result_offset);
#endif
}

static void audio_map_percussion_instruments(void far *instruments)
{
	audio_bass_drum_resource = audioresource_find(instruments, "BASD");
	audio_snare_resource = audioresource_find(instruments, "SNAR");
	audio_tom_resource = audioresource_find(instruments, "TOMM");
	audio_ride_resource = audioresource_find(instruments, "RIDE");
	audio_crash_resource = audioresource_find(instruments, "CRSH");
	audio_closed_hihat_resource = audioresource_find(instruments, "CHHT");
	audio_open_hihat_resource = audioresource_find(instruments, "OHHT");
}

void audio_map_song_instruments(void far *song, void far *instruments)
{
	legacy_u8 far *header = (legacy_u8 far *)audioresource_find(song, "hdr1");
	if (header == 0) {
		return;
	}

	legacy_u16 count = header[AUDIO_SONG_INSTRUMENT_COUNT_OFFSET];
	legacy_s8 name[AUDIO_RESOURCE_ID_LENGTH];
	for (legacy_u16 index = 0; index < count; ++index) {
		legacy_u16 name_offset =
			AUDIO_SONG_INSTRUMENT_TABLE_OFFSET + index * AUDIO_RESOURCE_ID_LENGTH;
		name[0] = header[name_offset];
		name[1] = header[name_offset + 1U];
		name[2] = header[name_offset + 2U];
		name[3] = header[name_offset + 3U];
		void far *instrument = audioresource_find(instruments, name);
		legacy_u16 pointer_offset = (legacy_u16)dos_memory_pointer_offset(instrument);
		legacy_u16 pointer_segment = (legacy_u16)dos_memory_pointer_segment(instrument);
		header[name_offset] = (legacy_u8)pointer_offset;
		header[name_offset + 1U] = (legacy_u8)(pointer_offset >> AUDIO_FAR_POINTER_WORD_SHIFT);
		header[name_offset + 2U] = (legacy_u8)pointer_segment;
		header[name_offset + 3U] = (legacy_u8)(pointer_segment >> AUDIO_FAR_POINTER_WORD_SHIFT);
	}

	audio_map_percussion_instruments(instruments);
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	/* The original lookup retained the bank's segment, so its offset includes
	 * the chunk's displacement. Native pointer normalization loses that alias. */
	legacy_closed_hihat_offset =
		audio_closed_hihat_resource == 0
			? 0
			: (legacy_u16)((legacy_u8 *)audio_closed_hihat_resource - (legacy_u8 *)instruments +
						   dos_memory_pointer_offset(instruments));
#else
	legacy_closed_hihat_offset = dos_memory_pointer_offset(audio_closed_hihat_resource);
#endif
}

static void audio_write_far_pointer_to_resource(legacy_u8 far *destination, legacy_u16 offset,
												legacy_u16 segment)
{
	destination[0] = (legacy_u8)offset;
	destination[1] = (legacy_u8)(offset >> AUDIO_FAR_POINTER_WORD_SHIFT);
	destination[2] = (legacy_u8)segment;
	destination[3] = (legacy_u8)(segment >> AUDIO_FAR_POINTER_WORD_SHIFT);
}

static void audio_patch_song_reference(legacy_u8 far *destination, legacy_u16 name_table_offset,
									   legacy_u16 offset_table_offset, legacy_u16 first_data_offset,
									   legacy_u16 resource_segment, legacy_u16 chunk_count)
{
	legacy_s8 name[AUDIO_RESOURCE_ID_LENGTH];
	name[0] = destination[0];
	name[1] = destination[1];
	name[2] = destination[2];
	name[3] = destination[3];
	const legacy_u8 far *names =
		(const legacy_u8 far *)dos_memory_make_pointer(resource_segment, name_table_offset);
	legacy_s16 chunk_index = audioresource_get_chunk_index(0, chunk_count, name, names);
	if (chunk_index < 0) {
		return;
	}

	const legacy_u8 far *offset_entry = (const legacy_u8 far *)dos_memory_make_pointer(
		resource_segment,
		LEGACY_U16_WRAP_ADD(
			offset_table_offset,
			LEGACY_U16_WRAP_MUL((legacy_u16)chunk_index, AUDIO_RESOURCE_OFFSET_ENTRY_SIZE)));
	legacy_u16 relative_offset = (legacy_u16)resource_read_u32le(offset_entry);
	audio_write_far_pointer_to_resource(
		destination, LEGACY_U16_WRAP_ADD(first_data_offset, relative_offset), resource_segment);
}

static legacy_u16 audio_skip_variable_length_field(legacy_u16 resource_segment,
												   legacy_u16 cursor_offset)
{
	legacy_u8 far *cursor =
		(legacy_u8 far *)dos_memory_make_pointer(resource_segment, cursor_offset);
	while ((cursor[0] & AUDIO_VARIABLE_LENGTH_CONTINUATION_BIT) != 0) {
		cursor_offset = LEGACY_U16_WRAP_ADD(cursor_offset, 1U);
		cursor = (legacy_u8 far *)dos_memory_make_pointer(resource_segment, cursor_offset);
	}
	return LEGACY_U16_WRAP_ADD(cursor_offset, 1U);
}

struct AUDIO_SONG_MAPPING {
	legacy_u16 resource_segment;
	legacy_u16 chunk_count;
	legacy_u16 name_table_offset;
	legacy_u16 offset_table_offset;
	legacy_u16 first_data_offset;
};

static void audio_map_header_tracks(const struct AUDIO_SONG_MAPPING *mapping,
									legacy_u16 cursor_offset)
{
	cursor_offset = LEGACY_U16_WRAP_ADD(cursor_offset, AUDIO_SONG_HEADER_PREFIX_SIZE);
	legacy_u8 far *cursor =
		(legacy_u8 far *)dos_memory_make_pointer(mapping->resource_segment, cursor_offset);
	cursor_offset = LEGACY_U16_WRAP_ADD(
		cursor_offset,
		LEGACY_U16_WRAP_ADD(LEGACY_U16_WRAP_MUL(cursor[0], AUDIO_RESOURCE_ID_LENGTH), 1U));
	cursor = (legacy_u8 far *)dos_memory_make_pointer(mapping->resource_segment, cursor_offset);
	legacy_u16 reference_count = cursor[0];
	cursor_offset = LEGACY_U16_WRAP_ADD(cursor_offset, 1U);
	while (reference_count != 0) {
		cursor = (legacy_u8 far *)dos_memory_make_pointer(mapping->resource_segment, cursor_offset);
		audio_patch_song_reference(cursor, mapping->name_table_offset, mapping->offset_table_offset,
								   mapping->first_data_offset, mapping->resource_segment,
								   mapping->chunk_count);
		cursor_offset = LEGACY_U16_WRAP_ADD(cursor_offset, AUDIO_SEQUENCE_REFERENCE_RECORD_SIZE);
		reference_count--;
	}
}

static legacy_u16 audio_map_sequence_command(const struct AUDIO_SONG_MAPPING *mapping,
											 legacy_u16 cursor_offset, legacy_u16 event)
{
	legacy_u8 far *cursor;

	switch (event - AUDIO_SEQUENCE_COMMAND_BASE) {
		case AUDIO_SEQUENCE_COMMAND_RETURN:
		case AUDIO_SEQUENCE_COMMAND_STOP:
		case AUDIO_SEQUENCE_COMMAND_RESTART:
		case AUDIO_SEQUENCE_COMMAND_LOOP_END:
			cursor_offset = LEGACY_U16_WRAP_ADD(cursor_offset, AUDIO_SEQUENCE_COMMAND_ONLY_SIZE);
			break;

		case AUDIO_SEQUENCE_COMMAND_SET_CONTROL:
		case AUDIO_SEQUENCE_COMMAND_SET_PITCH:
			cursor_offset = LEGACY_U16_WRAP_ADD(cursor_offset, AUDIO_SEQUENCE_WORD_ARGUMENT_SIZE);
			break;

		case AUDIO_SEQUENCE_COMMAND_CALL:
			cursor_offset = LEGACY_U16_WRAP_ADD(cursor_offset, AUDIO_SEQUENCE_CALL_PREFIX_SIZE);
			cursor =
				(legacy_u8 far *)dos_memory_make_pointer(mapping->resource_segment, cursor_offset);
			audio_patch_song_reference(cursor, mapping->name_table_offset,
									   mapping->offset_table_offset, mapping->first_data_offset,
									   mapping->resource_segment, mapping->chunk_count);
			cursor_offset = LEGACY_U16_WRAP_ADD(cursor_offset, AUDIO_FAR_POINTER_SIZE);
			break;

		case AUDIO_SEQUENCE_COMMAND_SKIP_PAYLOAD:
		case AUDIO_SEQUENCE_COMMAND_SEND_DRIVER_DATA:
			cursor_offset = LEGACY_U16_WRAP_ADD(cursor_offset, AUDIO_SEQUENCE_COMMAND_ONLY_SIZE);
			cursor =
				(legacy_u8 far *)dos_memory_make_pointer(mapping->resource_segment, cursor_offset);
			cursor_offset = LEGACY_U16_WRAP_ADD(cursor_offset, LEGACY_U16_WRAP_ADD(cursor[0], 1U));
			break;
	}
	return cursor_offset;
}

static void audio_map_track_events(const struct AUDIO_SONG_MAPPING *mapping,
								   legacy_u16 cursor_offset, legacy_u16 chunk_end_offset)
{
	while (cursor_offset < chunk_end_offset) {
		cursor_offset = audio_skip_variable_length_field(mapping->resource_segment, cursor_offset);
		legacy_u8 far *cursor =
			(legacy_u8 far *)dos_memory_make_pointer(mapping->resource_segment, cursor_offset);
		legacy_u16 event = cursor[0];

		if (event < AUDIO_SEQUENCE_COMMAND_BASE || event > AUDIO_SEQUENCE_COMMAND_LAST) {
			if (event >= AUDIO_SEQUENCE_STATUS_BIT) {
				cursor_offset =
					LEGACY_U16_WRAP_ADD(cursor_offset, AUDIO_SEQUENCE_COMMAND_ONLY_SIZE);
			}
			cursor_offset = LEGACY_U16_WRAP_ADD(cursor_offset, AUDIO_SEQUENCE_COMMAND_ONLY_SIZE);
			cursor_offset =
				audio_skip_variable_length_field(mapping->resource_segment, cursor_offset);
			continue;
		}
		if (audio_sequence_command_has_byte_argument(
				(legacy_u8)(event - AUDIO_SEQUENCE_COMMAND_BASE))) {
			cursor_offset = LEGACY_U16_WRAP_ADD(cursor_offset, AUDIO_SEQUENCE_BYTE_ARGUMENT_SIZE);
			continue;
		}

		cursor_offset = audio_map_sequence_command(mapping, cursor_offset, event);
	}
}

void audio_map_song_tracks(void far *song)
{
	legacy_u8 far *bytes = (legacy_u8 far *)song;
	legacy_u16 resource_offset = (legacy_u16)dos_memory_pointer_offset(song);
	struct AUDIO_SONG_MAPPING mapping;
	mapping.resource_segment = (legacy_u16)dos_memory_pointer_segment(song);
	mapping.chunk_count = resource_read_u16le((const legacy_u8 far *)dos_memory_make_pointer(
		mapping.resource_segment,
		LEGACY_U16_WRAP_ADD(resource_offset, AUDIO_RESOURCE_CHUNK_COUNT_OFFSET)));
	mapping.name_table_offset = LEGACY_U16_WRAP_ADD(resource_offset, AUDIO_RESOURCE_TABLE_OFFSET);
	mapping.offset_table_offset =
		LEGACY_U16_WRAP_ADD(mapping.name_table_offset,
							LEGACY_U16_WRAP_MUL(mapping.chunk_count, AUDIO_RESOURCE_ID_LENGTH));
	mapping.first_data_offset = LEGACY_U16_WRAP_ADD(
		resource_offset,
		LEGACY_U16_WRAP_ADD(
			AUDIO_RESOURCE_TABLE_OFFSET,
			LEGACY_U16_WRAP_MUL(mapping.chunk_count, AUDIO_RESOURCE_DIRECTORY_ENTRY_SIZE)));
	legacy_u16 header_index = (legacy_u16)audioresource_get_chunk_index(
		0, mapping.chunk_count, "hdr1",
		(const legacy_u8 far *)dos_memory_make_pointer(mapping.resource_segment,
													   mapping.name_table_offset));

	for (legacy_u16 index = 0; index < mapping.chunk_count; ++index) {
		legacy_u16 relative_offset =
			(legacy_u16)resource_read_u32le((const legacy_u8 far *)dos_memory_make_pointer(
				mapping.resource_segment,
				LEGACY_U16_WRAP_ADD(mapping.offset_table_offset,
									LEGACY_U16_WRAP_MUL(index, AUDIO_RESOURCE_OFFSET_ENTRY_SIZE))));
		legacy_u16 chunk_offset = LEGACY_U16_WRAP_ADD(mapping.first_data_offset, relative_offset);
		bytes = (legacy_u8 far *)dos_memory_make_pointer(mapping.resource_segment, chunk_offset);
		legacy_u16 chunk_end_offset =
			LEGACY_U16_WRAP_ADD(chunk_offset, (legacy_u16)resource_read_u32le(bytes));
		legacy_u16 cursor_offset =
			LEGACY_U16_WRAP_ADD(chunk_offset, AUDIO_RESOURCE_CHUNK_LENGTH_SIZE);

		if (index == header_index) {
			audio_map_header_tracks(&mapping, cursor_offset);
			continue;
		}

		audio_map_track_events(&mapping, cursor_offset, chunk_end_offset);
	}
}

void far *init_audio_resources(void far *song, void far *instruments, const legacy_s8 *name)
{
	legacy_u8 far *song_chunk = (legacy_u8 far *)audioresource_find(song, name);
	if (song_chunk == 0) {
		return 0;
	}
	legacy_u8 far *header = (legacy_u8 far *)audioresource_find(song_chunk, "hdr1");
	if (header == 0) {
		return 0;
	}

	if (header[AUDIO_SONG_HEADER_MAPPED_FLAG_OFFSET] != AUDIO_SONG_HEADER_MAPPED) {
		audio_map_song_instruments(song_chunk, instruments);
		audio_map_song_tracks(song_chunk);
		header[AUDIO_SONG_HEADER_MAPPED_FLAG_OFFSET] = AUDIO_SONG_HEADER_MAPPED;
		legacy_u16 data_offset =
			LEGACY_U16_WRAP_ADD((legacy_u16)dos_memory_pointer_offset(song_chunk),
								(legacy_u16)((legacy_u16)song_chunk[AUDIO_SONG_DATA_SCALE_OFFSET]
											 << AUDIO_SONG_DATA_SCALE_SHIFT));
		data_offset = LEGACY_U16_WRAP_ADD(data_offset, AUDIO_SONG_DATA_PREFIX_SIZE);
		void far *data =
			dos_memory_make_pointer(dos_memory_pointer_segment(song_chunk), data_offset);
		audio_write_far_pointer(header, data);
	} else {
		/* Other audio resources can replace these shared percussion bindings.
		 * Keep the historical CHHT offset used by track-boundary physics unchanged. */
		audio_map_percussion_instruments(instruments);
	}

#ifdef RESTUNTS_SDL3
	sdl3_music_register(header, name);
#endif
	return header;
}

void load_audio_finalize(void far *audio_resource)
{
	audio_update_lock = AUDIO_UPDATE_LOCKED;
	audio_stop_music();
	legacy_u8 far *resource = (legacy_u8 far *)audio_resource;
	if (resource == 0 || resource[AUDIO_RESOURCE_TYPE_OFFSET] != AUDIO_RESOURCE_TYPE_SONG ||
		resource[AUDIO_RESOURCE_READY_FLAG_OFFSET] != AUDIO_RESOURCE_READY) {
		return;
	}

	dos_audio_driver_reset();
	audio_sequence_elapsed_ticks = 0;
	audio_sequence_tick_period = AUDIO_ENGINE_INITIAL_TICK_STEP;
#ifdef RESTUNTS_SDL3
	/* Always keep the original sequence alive. Shift+F10 selects which source is
	 * audible without losing its notes, controllers or current position. */
	sdl3_music_start(resource);
	/* File probing and audio-device setup can take time before either source starts. */
	dos_audio_driver_reset();
#endif
	legacy_u16 data_offset =
		LEGACY_U16_WRAP_ADD((legacy_u16)((legacy_u16)resource[AUDIO_SONG_INSTRUMENT_COUNT_OFFSET]
										 << AUDIO_MUSIC_CHANNEL_COUNT_SCALE_SHIFT),
							AUDIO_MUSIC_CHANNEL_COUNT_OFFSET);
	audio_music_channel_count = resource[data_offset++];
	audio_init_channel_range(0, LEGACY_S16_FROM_BITS((legacy_u16)(audio_music_channel_count - 1U)),
							 resource, data_offset, audio_music_rate, AUDIO_MUSIC_PRIORITY);
	audio_music_active = AUDIO_STATE_ENABLED;
	audio_update_lock = AUDIO_UPDATE_UNLOCKED;
#ifdef RESTUNTS_SDL3
	sdl3_music_sync();
#endif
}

void audioresource_copy_n_bytes(const legacy_u8 far *source, legacy_u8 far *destination,
								legacy_s16 size)
{
	legacy_s16 remaining = LEGACY_S16_FROM_BITS(size);
	if (remaining <= 0) {
		return;
	}
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	do {
		*destination++ = *source++;
		remaining--;
	} while (remaining != 0);
#else
	legacy_u16 source_offset = (legacy_u16)dos_memory_pointer_offset(source);
	legacy_u16 source_segment = (legacy_u16)dos_memory_pointer_segment(source);
	legacy_u16 destination_offset = (legacy_u16)dos_memory_pointer_offset(destination);
	legacy_u16 destination_segment = (legacy_u16)dos_memory_pointer_segment(destination);
	do {
		*(legacy_u8 far *)dos_memory_make_pointer(destination_segment, destination_offset) =
			*(const legacy_u8 far *)dos_memory_make_pointer(source_segment, source_offset);
		source_offset = LEGACY_U16_WRAP_ADD(source_offset, 1U);
		destination_offset = LEGACY_U16_WRAP_ADD(destination_offset, 1U);
		remaining = LEGACY_S16_WRAP_SUB(remaining, 1);
	} while (remaining != 0);
#endif
}

void audio_set_missing_file_fatal(legacy_s16 value)
{
	audio_missing_file_fatal = value;
}
