#include <string.h>
#include "extended_parallel.h"
#include "../c/csrpl.h"

static const legacy_u8 source_track_signature[] = {'C', 'S', 'T', 'R', 'K', 26U, '\r', '\n'};
static const legacy_u8 source_replay_signature[] = {'C', 'S', 'R', 'P', 'L', '\r', '\n', 26U};

static legacy_int source_is_csrpl(const legacy_u8 *source, size_t size)
{
	return source && size >= sizeof(source_replay_signature) &&
		   !memcmp(source, source_replay_signature, sizeof(source_replay_signature));
}

const legacy_char *extended_parallel_source_extension(const legacy_u8 *source, size_t size)
{
	if (source_is_csrpl(source, size)) {
		return "csrpl";
	}
	if (source && size >= sizeof(source_track_signature) &&
		!memcmp(source, source_track_signature, sizeof(source_track_signature))) {
		return "cstrk";
	}
	return size == REPLAY_TRACK_SIZE ? "trk" : "rpl";
}

/* The input history follows the complete embedded track in CSRPL. The parent
 * and every worker must evaluate the same seed, including long recordings. */
legacy_int extended_parallel_source_replay_inputs(const legacy_u8 *source, size_t size,
												  const legacy_u8 **inputs, legacy_uint *length)
{
	if (source_is_csrpl(source, size)) {
		struct CSRPL_VIEW replay;
		if (!csrpl_decode(source, size, &replay, NULL, 0) || !replay.metadata.frame_count ||
			replay.metadata.frame_count > genghost_sim_frame_limit() ||
			replay.metadata.frames_per_second != genghost_sim_frame_rate()) {
			return -1;
		}
		*inputs = replay.inputs;
		*length = replay.metadata.frame_count;
		return 0;
	}
	if (!source || size <= REPLAY_INPUT_OFFSET) {
		return -1;
	}
	struct GAMEINFO replay;
	replay_gameinfo_decode(&replay, source);
	if (replay_file_size(replay.game_recordedframes) != size || !replay.game_recordedframes ||
		replay.game_recordedframes > genghost_sim_frame_limit() ||
		replay.game_framespersec != genghost_sim_frame_rate()) {
		return -1;
	}
	*inputs = source + REPLAY_INPUT_OFFSET;
	*length = replay.game_recordedframes;
	return 0;
}
