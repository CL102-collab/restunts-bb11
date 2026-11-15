#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extended_simulation.h"
#include "../c/externs.h"
#include "../c/restunts.h"
#include "../c/camera.h"
#include "../c/track/track_objects.h"
#include "../c/memmgr.h"
#include "../c/race/race_resources.h"
#include "../c/race/race_stats.h"
#include "../c/physics/physics_internal.h"
#include "../c/wheel_transform.h"
#include "../c/crash_state.h"
#include "../c/track/trackdata_layout.h"
#include "../c/game_input.h"
#include "../c/extended/extended_track.h"
#include "../c/extended/extended_geometry.h"
#include "../c/driving_rules.h"

#undef memcpy
#undef strlen

#if !defined(RESTUNTS_SDL3) || !defined(RESTUNTS_HEADLESS)
#error genghost requires the SDL3 headless engine
#endif

static struct CSRPL_METADATA replay_metadata;

static legacy_int has_replay_metadata;
static struct CSRPL_VIEW source_replay;

static legacy_u8 *original_extended_track;

static size_t original_extended_track_size;

#define GENGHOST_ERROR_CAPACITY 256U

#define GENGHOST_WORLD_POSITION_SCALE 64L

#define GENGHOST_INPUT_MASK CSRPL_INPUT_MASK

void extended_genghost_sim_set_metadata(const struct CSRPL_METADATA *metadata)
{
	has_replay_metadata = metadata != NULL;
	memset(&source_replay, 0, sizeof(source_replay));
	if (metadata) {
		replay_metadata = *metadata;
	} else {
		memset(&replay_metadata, 0, sizeof(replay_metadata));
	}
}

void extended_genghost_sim_set_replay(const struct CSRPL_VIEW *replay)
{
	extended_genghost_sim_set_metadata(replay ? &replay->metadata : NULL);
	if (replay) {
		source_replay = *replay;
	}
}

legacy_uint extended_genghost_sim_frame(void)
{
	return extended_replay_is_active() ? extended_replay_frame() : (legacy_u16)state.game_frame;
}

legacy_uint extended_genghost_sim_frame_limit(void)
{
	return extended_replay_is_active() || extended_track_active() ? GENGHOST_MAX_FRAMES
																  : GENGHOST_LEGACY_MAX_FRAMES;
}

legacy_int extended_genghost_sim_load(const legacy_u8 *track, legacy_uint track_size,
									  legacy_int *handled)
{
	static const legacy_u8 track_magic[] = {'C', 'S', 'T', 'R', 'K', 0x1a, '\r', '\n'};
	*handled =
		track_size >= sizeof(track_magic) && memcmp(track, track_magic, sizeof(track_magic)) == 0;
	free(original_extended_track);
	original_extended_track = NULL;
	original_extended_track_size = 0;
	if (*handled) {
		legacy_char error[GENGHOST_ERROR_CAPACITY];
		if (!extended_track_load(track, track_size, error, sizeof(error)) ||
			!cstrk_encode(extended_track_model(), &original_extended_track,
						  &original_extended_track_size, error, sizeof(error))) {
			fprintf(stderr, "Cannot simulate extended track: %s\n", error);
			return -1;
		}
	} else if (has_replay_metadata) {
		if ((replay_metadata.flags & CSRPL_FLAG_CLASSIC_TRACK) == 0U ||
			track_size != CSRPL_CLASSIC_TRACK_SIZE) {
			fputs("Classic CSRPL track metadata does not match its payload\n", stderr);
			return -1;
		}
		original_extended_track = malloc(track_size);
		if (!original_extended_track) {
			return -1;
		}
		memcpy(original_extended_track, track, track_size);
		original_extended_track_size = track_size;
	}
	return 0;
}

void extended_genghost_sim_configure(void)
{
	if (has_replay_metadata) {
		gameconfig.game_playermaterial = replay_metadata.player_material;
		gameconfig.game_opponenttype = replay_metadata.opponent_type;
		if (gameconfig.game_opponenttype) {
			memcpy(gameconfig.game_opponentcarid, replay_metadata.opponent_car_id,
				   REPLAY_CAR_ID_SIZE);
		}
		gameconfig.game_opponentmaterial = replay_metadata.opponent_material;
		gameconfig.game_opponenttransmission = replay_metadata.opponent_transmission;
	}
}

legacy_int extended_genghost_sim_begin(legacy_int handled)
{
	if (handled || has_replay_metadata) {
		if (has_replay_metadata) {
			/* Verification keeps the applied opponent decisions. Solo searches
			 * populate their own input samples through step(). */
			struct CSRPL_VIEW replay = source_replay;
			replay.metadata = replay_metadata;
			if ((replay.metadata.flags & CSRPL_FLAG_OPPONENT_COMMANDS) == 0U) {
				replay.metadata.frame_count = 0;
			}
			replay.metadata.driving_rules = driving_rules_current();
			if (!extended_replay_runtime_load(&replay)) {
				return -1;
			}
		} else {
			extended_replay_runtime_begin();
		}
	}
	return 0;
}

legacy_int extended_genghost_sim_record(legacy_u8 input)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	extended_replay_set_input_frame(genghost_sim_frame());
	return extended_replay_record_input(input) ? 1 : -1;
}

legacy_u8 extended_genghost_sim_input(legacy_u8 input)
{
	return input & GENGHOST_INPUT_MASK;
}

legacy_int extended_genghost_sim_result(struct GENGHOST_RESULT *result)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	result->finish_frame = extended_replay_clocks.player_end;
	result->total_frames = extended_replay_clocks.player_finish;
	result->penalty_frames = result->finished ? result->total_frames - result->finish_frame
											  : extended_replay_clocks.penalty;
	return 1;
}

legacy_int extended_genghost_sim_write(const legacy_char *path, const legacy_char *track_name,
									   const legacy_u8 *inputs, legacy_uint length,
									   legacy_int *handled)
{
	*handled = extended_replay_is_active();
	if (*handled) {
		struct CSRPL_METADATA metadata = replay_metadata;
		metadata.version = has_replay_metadata ? replay_metadata.version : CSRPL_VERSION;
		metadata.driving_rules = driving_rules_current();
		memcpy(metadata.player_car_id, gameconfig.game_playercarid, CSRPL_CAR_ID_SIZE);
		metadata.player_material = gameconfig.game_playermaterial;
		metadata.player_transmission = gameconfig.game_playertransmission;
		metadata.opponent_type = gameconfig.game_opponenttype;
		if (metadata.opponent_type <= CSRPL_CLASSIC_OPPONENT_LAST) {
			metadata.flags &= (legacy_u32)~CSRPL_FLAG_OPPONENT_COMMANDS;
			memset(metadata.opponent_id, 0, sizeof(metadata.opponent_id));
		}
		memcpy(metadata.opponent_car_id, gameconfig.game_opponentcarid, CSRPL_CAR_ID_SIZE);
		metadata.opponent_material = gameconfig.game_opponentmaterial;
		metadata.opponent_transmission = gameconfig.game_opponenttransmission;
		memset(metadata.track_name, 0, sizeof(metadata.track_name));
		size_t name_length = strlen(track_name);
		if (name_length >= sizeof(metadata.track_name)) {
			name_length = sizeof(metadata.track_name) - 1U;
		}
		memcpy(metadata.track_name, track_name, name_length);
		metadata.frames_per_second = gameconfig.game_framespersec;
		metadata.frame_count = length;
		legacy_u8 *bytes = NULL;
		size_t size = 0;
		legacy_char error[GENGHOST_ERROR_CAPACITY];
		legacy_uint command_count = (metadata.flags & CSRPL_FLAG_OPPONENT_COMMANDS) ? length : 0U;
		if (command_count > extended_replay_opponent_command_count()) {
			fputs("Cannot encode incomplete opponent command history\n", stderr);
			return -1;
		}
		if (!csrpl_encode_with_commands(&metadata, original_extended_track,
										original_extended_track_size, inputs, length,
										extended_replay_opponent_commands(), command_count, &bytes,
										&size, error, sizeof(error))) {
			fprintf(stderr, "Cannot encode extended ghost: %s\n", error);
			return -1;
		}
		FILE *output = fopen(path, "wb");
		if (!output) {
			perror(path);
			free(bytes);
			return -1;
		}
		legacy_int failed = fwrite(bytes, 1, size, output) != size;
		if (fclose(output)) {
			failed = 1;
		}
		free(bytes);
		return failed ? -1 : 0;
	}
	return 0;
}
