#include "extended_file.h"
#include "cstrk.h"
#include "csrpl.h"
#include "extended_track.h"
#include "extended_replay_runtime.h"
#include "externs.h"
#include "platform.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define EXTENDED_IO_CHUNK 16384U
#define EXTENDED_MAX_TRACK_BYTES                                                                   \
	(CSTRK_HEADER_SIZE + 91U * 91U + 2U * 90U * 90U + CSTRK_OBJECT_SIZE * CSTRK_MAX_OBJECTS +      \
	 CSTRK_MAX_NOTES)

static char extended_io_error[192];

const legacy_s8 *extended_file_error(void)
{
	return (const legacy_s8 *)extended_io_error;
}

static int extended_io_fail(const char *message)
{
	size_t length = strlen(message);
	if (length >= sizeof(extended_io_error)) {
		length = sizeof(extended_io_error) - 1U;
	}
	memcpy(extended_io_error, message, length);
	extended_io_error[length] = 0;
	return 1;
}

static uint8_t *extended_io_read(const legacy_s8 *path, size_t maximum, size_t *size)
{
	extended_io_error[0] = 0;
	legacy_u16 handle = dos_file_open(path, DOS_FILE_OPEN_EXISTING);
	if (!handle) {
		extended_io_fail("Unable to open the selected file.");
		return NULL;
	}
	legacy_s32 length = -1;
	if (dos_file_seek(handle, 0, DOS_FILE_SEEK_END) == 0) {
		length = dos_file_tell(handle);
	}
	if (length <= 0 || (size_t)length > maximum ||
		dos_file_seek(handle, 0, DOS_FILE_SEEK_BEGIN) != 0) {
		dos_file_close(handle);
		extended_io_fail("Invalid or unsupported file size.");
		return NULL;
	}
	uint8_t *bytes = malloc((size_t)length);
	if (!bytes) {
		dos_file_close(handle);
		extended_io_fail("Not enough memory to load this file.");
		return NULL;
	}
	size_t offset = 0;
	while (offset < (size_t)length) {
		legacy_u16 count =
			(legacy_u16)(((size_t)length - offset > EXTENDED_IO_CHUNK) ? EXTENDED_IO_CHUNK
																	   : (size_t)length - offset);
		if (dos_file_read(handle, bytes + offset, count) != count) {
			free(bytes);
			dos_file_close(handle);
			extended_io_fail("The selected file could not be read completely.");
			return NULL;
		}
		offset += count;
	}
	dos_file_close(handle);
	*size = (size_t)length;
	return bytes;
}

int extended_file_load_track(const legacy_s8 *path)
{
	size_t size;
	uint8_t *bytes = extended_io_read(path, EXTENDED_MAX_TRACK_BYTES, &size);
	if (!bytes) {
		return 1;
	}
	int success = extended_track_load(bytes, size, extended_io_error, sizeof(extended_io_error));
	free(bytes);
	if (success) {
		extended_replay_runtime_clear();
	}
	return success ? 0 : 1;
}

static void extended_metadata_to_gameinfo(const struct CSRPL_METADATA *metadata)
{
	memcpy(gameconfig.game_playercarid, metadata->player_car_id, REPLAY_CAR_ID_SIZE);
	gameconfig.game_playermaterial = metadata->player_material;
	gameconfig.game_playertransmission = metadata->player_transmission;
	gameconfig.game_opponenttype = metadata->opponent_type;
	memcpy(gameconfig.game_opponentcarid, metadata->opponent_car_id, REPLAY_CAR_ID_SIZE);
	gameconfig.game_opponentmaterial = metadata->opponent_material;
	gameconfig.game_opponenttransmission = metadata->opponent_transmission;
	memcpy(gameconfig.game_trackname, metadata->track_name, REPLAY_TRACK_NAME_SIZE);
	gameconfig.game_trackname[REPLAY_TRACK_NAME_SIZE - 1U] = 0;
	gameconfig.game_framespersec = metadata->frames_per_second;
}

int extended_file_load_replay(const legacy_s8 *path)
{
	size_t size;
	uint8_t *bytes = extended_io_read(path, LEGACY_S32_MAX, &size);
	if (!bytes) {
		return 1;
	}
	struct CSRPL_VIEW view;
	int success = csrpl_decode(bytes, size, &view, extended_io_error, sizeof(extended_io_error));
	if (success) {
		/* Prepare the replay in an isolated owner first. Track activation also
		 * allocates before committing, so failure preserves the complete session. */
		struct EXTENDED_REPLAY_SUSPENDED previous;
		struct GAMEINFO saved_config = gameconfig;
		legacy_s16 saved_frame = state.game_frame;
		legacy_u16 saved_elapsed1 = elapsed_time1, saved_elapsed2 = elapsed_time2;
		extended_replay_runtime_suspend(&previous);
		success = extended_replay_runtime_load(&view);
		if (!success) {
			extended_io_fail("Not enough memory for replay input history.");
		} else {
			success = extended_track_load(view.track_data, view.track_size, extended_io_error,
										  sizeof(extended_io_error));
		}
		if (success) {
			extended_replay_free(&previous.recording);
			extended_replay_history_free(&previous.history);
			extended_metadata_to_gameinfo(&view.metadata);
		} else {
			gameconfig = saved_config;
			state.game_frame = saved_frame;
			elapsed_time1 = saved_elapsed1;
			elapsed_time2 = saved_elapsed2;
			extended_replay_runtime_resume(&previous);
		}
	}
	free(bytes);
	return success ? 0 : 1;
}

int extended_file_write_replay(const legacy_s8 *path)
{
	extended_io_error[0] = 0;
	if (!extended_track_active() || !extended_replay_is_active()) {
		return extended_io_fail("No Chocolate Stunts recording is available.");
	}
	uint8_t *track_bytes = NULL;
	size_t track_size = 0;
	if (!cstrk_encode(extended_track_model(), &track_bytes, &track_size, extended_io_error,
					  sizeof(extended_io_error))) {
		return 1;
	}
	struct CSRPL_METADATA metadata;
	memset(&metadata, 0, sizeof(metadata));
	memcpy(metadata.player_car_id, gameconfig.game_playercarid, REPLAY_CAR_ID_SIZE);
	metadata.player_material = gameconfig.game_playermaterial;
	metadata.player_transmission = gameconfig.game_playertransmission;
	metadata.opponent_type = gameconfig.game_opponenttype;
	memcpy(metadata.opponent_car_id, gameconfig.game_opponentcarid, REPLAY_CAR_ID_SIZE);
	metadata.opponent_material = gameconfig.game_opponentmaterial;
	metadata.opponent_transmission = gameconfig.game_opponenttransmission;
	memcpy(metadata.track_name, gameconfig.game_trackname, REPLAY_TRACK_NAME_SIZE);
	metadata.frames_per_second = gameconfig.game_framespersec;
	metadata.frame_count = extended_replay_recorded_frames();
	uint8_t *bytes = NULL;
	size_t size = 0;
	int success = csrpl_encode(&metadata, track_bytes, track_size, extended_replay_inputs(),
							   metadata.frame_count, &bytes, &size, extended_io_error,
							   sizeof(extended_io_error));
	free(track_bytes);
	if (!success) {
		return 1;
	}
	/* Write alongside the destination, then replace it only after close has
	 * succeeded. Browser .tmp files remain in MEMFS until the final commit. */
	legacy_s8 temporary[1024];
	size_t prefix = 0;
	for (size_t index = 0; path[index]; index++) {
		if (path[index] == '/' || path[index] == '\\' || path[index] == ':') {
			prefix = index + 1U;
		}
	}
	if (prefix + 13U > sizeof(temporary)) {
		free(bytes);
		return extended_io_fail("The replay directory path is too long.");
	}
	memcpy(temporary, path, prefix);
	static unsigned temporary_serial;
	int available = 0;
	for (unsigned attempt = 0; attempt < 65536U; attempt++) {
		snprintf((char *)temporary + prefix, sizeof(temporary) - prefix, "_C%06X.tmp",
				 ++temporary_serial & 0xffffffU);
		legacy_u16 existing = dos_file_open(temporary, DOS_FILE_OPEN_EXISTING);
		if (existing) {
			dos_file_close(existing);
		} else {
			available = 1;
			break;
		}
	}
	legacy_u16 handle = available ? dos_file_open(temporary, DOS_FILE_CREATE) : 0;
	if (!handle) {
		free(bytes);
		return extended_io_fail("Unable to create the replay file.");
	}
	size_t offset = 0;
	while (offset < size) {
		legacy_u16 count =
			(legacy_u16)((size - offset > EXTENDED_IO_CHUNK) ? EXTENDED_IO_CHUNK : size - offset);
		if (dos_file_write(handle, bytes + offset, count) != count) {
			break;
		}
		offset += count;
	}
	int close_failed = dos_file_close(handle) != 0;
	free(bytes);
	if (offset != size || close_failed) {
		dos_file_remove(temporary);
		return extended_io_fail("The replay file could not be saved completely.");
	}
	if (sdl3_file_replace(temporary, path) != 0) {
		dos_file_remove(temporary);
		return extended_io_fail(
			"Unable to replace the replay file; the previous file was retained.");
	}
	return 0;
}
