#include "extended_ghost.h"
#include "cstrk.h"
#include "extended_track.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>

int extended_ghost_read(const legacy_s8 *path, struct EXTENDED_GHOST_REPLAY *out,
						struct GAMEINFO *config)
{
	legacy_u16 file = dos_file_open(path, DOS_FILE_OPEN_EXISTING);
	if (!file) {
		return 0;
	}
	legacy_s32 length = -1;
	if (dos_file_seek(file, 0, DOS_FILE_SEEK_END) == 0) {
		length = dos_file_tell(file);
	}
	if (length < (legacy_s32)CSRPL_HEADER_SIZE ||
		dos_file_seek(file, 0, DOS_FILE_SEEK_BEGIN) != 0) {
		dos_file_close(file);
		return 0;
	}
	struct EXTENDED_GHOST_REPLAY candidate = {0};
	candidate.bytes = malloc((size_t)length);
	if (!candidate.bytes) {
		dos_file_close(file);
		return 0;
	}
	candidate.size = (size_t)length;
	size_t offset = 0;
	while (offset < candidate.size) {
		legacy_u16 chunk =
			(legacy_u16)(candidate.size - offset > 16384U ? 16384U : candidate.size - offset);
		if (dos_file_read(file, candidate.bytes + offset, chunk) != chunk) {
			free(candidate.bytes);
			dos_file_close(file);
			return 0;
		}
		offset += chunk;
	}
	dos_file_close(file);
	if (!csrpl_decode(candidate.bytes, candidate.size, &candidate.view, NULL, 0)) {
		free(candidate.bytes);
		return 0;
	}
	const struct CSRPL_METADATA *metadata = &candidate.view.metadata;
	struct GAMEINFO next;
	memset(&next, 0, sizeof(next));
	memcpy(next.game_playercarid, metadata->player_car_id, REPLAY_CAR_ID_SIZE);
	next.game_playermaterial = metadata->player_material;
	next.game_playertransmission = metadata->player_transmission;
	next.game_opponenttype = metadata->opponent_type;
	memcpy(next.game_opponentcarid, metadata->opponent_car_id, REPLAY_CAR_ID_SIZE);
	next.game_opponentmaterial = metadata->opponent_material;
	next.game_opponenttransmission = metadata->opponent_transmission;
	memcpy(next.game_trackname, metadata->track_name, REPLAY_TRACK_NAME_SIZE);
	next.game_trackname[REPLAY_TRACK_NAME_SIZE - 1U] = 0;
	next.game_framespersec = metadata->frames_per_second;
	/* Kept solely as the legacy nonzero marker. The full count lives in view. */
	next.game_recordedframes =
		metadata->frame_count > LEGACY_S16_MAX ? LEGACY_S16_MAX : (legacy_u16)metadata->frame_count;
	*out = candidate;
	*config = next;
	return 1;
}

void extended_ghost_free(struct EXTENDED_GHOST_REPLAY *replay)
{
	free(replay->bytes);
	memset(replay, 0, sizeof(*replay));
}

int extended_ghost_matches_track(const struct EXTENDED_GHOST_REPLAY *replay)
{
	if (!replay->bytes || !extended_track_active()) {
		return 0;
	}
	uint8_t *data = NULL;
	size_t size = 0;
	if (!cstrk_encode(extended_track_model(), &data, &size, NULL, 0)) {
		return 0;
	}
	int matches =
		size == replay->view.track_size && memcmp(data, replay->view.track_data, size) == 0;
	free(data);
	return matches;
}
