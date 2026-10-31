#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <SDL3/SDL.h>
#include "extended_source.h"
#include "../c/cstrk.h"
#include "../c/csrpl.h"

#define GENGHOST_FORMAT_SIGNATURE_SIZE 8U
#define GENGHOST_MAX_TRACK_SIZE                                                                    \
	(CSTRK_HEADER_SIZE + (CSTRK_MAX_DIMENSION + 1U) * (CSTRK_MAX_DIMENSION + 1U) +                 \
	 2U * CSTRK_MAX_DIMENSION * CSTRK_MAX_DIMENSION + CSTRK_OBJECT_SIZE * CSTRK_MAX_OBJECTS +      \
	 CSTRK_MAX_NOTES)
#define GENGHOST_MAX_SOURCE_SIZE (CSRPL_HEADER_SIZE + GENGHOST_MAX_TRACK_SIZE + GENGHOST_MAX_FRAMES)
#define GENGHOST_SOURCE_ERROR_SIZE 256U

static const legacy_u8 cstrk_signature[GENGHOST_FORMAT_SIGNATURE_SIZE] = {'C', 'S', 'T',  'R',
																		  'K', 26U, '\r', '\n'};
static const legacy_u8 csrpl_signature[GENGHOST_FORMAT_SIGNATURE_SIZE] = {'C', 'S',	 'R',  'P',
																		  'L', '\r', '\n', 26U};

legacy_char *extended_source_join_path(const legacy_char *directory, const legacy_char *name)
{
	if (name[0] == '/' || name[0] == '\\' || (isalpha((legacy_u8)name[0]) && name[1] == ':')) {
		return SDL_strdup(name);
	}
	size_t a = strlen(directory), b = strlen(name);
	legacy_char *result = malloc(a + b + 2U);
	if (result) {
		snprintf(result, a + b + 2U, "%s/%s", directory, name);
	}
	return result;
}

const legacy_char *extended_source_base_name(const legacy_char *path)
{
	const legacy_char *name = path;
	for (; *path; ++path) {
		if (*path == '/' || *path == '\\') {
			name = path + 1;
		}
	}
	return name;
}

legacy_char *extended_source_with_suffix(const legacy_char *path, const legacy_char *suffix)
{
	size_t size = strlen(path) + strlen(suffix) + 1U;
	legacy_char *result = malloc(size);
	if (result) {
		snprintf(result, size, "%s%s", path, suffix);
	}
	return result;
}

static legacy_int read_file(const legacy_char *path, legacy_u8 **data, size_t *length)
{
	FILE *file = fopen(path, "rb");
	if (!file) {
		return -1;
	}
	if (fseek(file, 0, SEEK_END)) {
		fclose(file);
		return -1;
	}
	legacy_s64 file_size = ftell(file);
	if (file_size < 0 || (legacy_u64)file_size > GENGHOST_MAX_SOURCE_SIZE ||
		fseek(file, 0, SEEK_SET)) {
		fclose(file);
		return -1;
	}
	*length = (size_t)file_size;
	*data = malloc(*length + 1U);
	if (!*data) {
		fclose(file);
		return -1;
	}
	legacy_int failed = fread(*data, 1, *length, file) != *length;
	if (fclose(file)) {
		failed = 1;
	}
	if (failed) {
		free(*data);
		*data = NULL;
		return -1;
	}
	return 0;
}

legacy_int extended_source_read(const legacy_char *source, const legacy_char *cwd,
								const legacy_char *data_dir, legacy_int replay_only,
								legacy_u8 **data, size_t *length, legacy_char **resolved_path)
{
	static const legacy_char *track_suffixes[] = {"",		".CSTRK", ".cstrk", ".TRK", ".trk",
												  ".CSRPL", ".csrpl", ".RPL",	".rpl"};
	static const legacy_char *replay_suffixes[] = {"", ".CSRPL", ".csrpl", ".RPL", ".rpl"};
	const legacy_char **suffixes = replay_only ? replay_suffixes : track_suffixes;
	legacy_uint suffix_count = replay_only ? sizeof(replay_suffixes) / sizeof(replay_suffixes[0])
										   : sizeof(track_suffixes) / sizeof(track_suffixes[0]);
	for (legacy_uint directory = 0; directory < 2U; ++directory) {
		for (legacy_uint i = 0; i < suffix_count; ++i) {
			legacy_char *name = extended_source_with_suffix(source, suffixes[i]);
			legacy_char *path =
				name ? extended_source_join_path(directory ? data_dir : cwd, name) : NULL;
			free(name);
			if (!path) {
				return -1;
			}
			legacy_int status = read_file(path, data, length);
			if (!status) {
				*resolved_path = path;
				return 0;
			}
			free(path);
		}
	}
	return -1;
}

static void replay_driving_rules(struct GENGHOST_OPTIONS *options, legacy_u16 recorded,
								 legacy_u16 explicit_rules)
{
	if (!(explicit_rules & DRIVING_RULE_POWER_GEAR)) {
		options->powergear = (recorded & DRIVING_RULE_POWER_GEAR) != 0;
	}
	if (!(explicit_rules & DRIVING_RULE_LEGACY_COLLISION)) {
		options->collision = (recorded & DRIVING_RULE_LEGACY_COLLISION) != 0;
	}
	if (!(explicit_rules & DRIVING_RULE_LEFT_CORNER_BIAS)) {
		options->left_bias = (recorded & DRIVING_RULE_LEFT_CORNER_BIAS) != 0;
	}
	if (!(explicit_rules & DRIVING_RULE_DUAL_WAY_SWITCHING)) {
		options->dual_way_switching = (recorded & DRIVING_RULE_DUAL_WAY_SWITCHING) != 0;
	}
	if (!(explicit_rules & DRIVING_RULE_CHICANE_FAST_GRASS)) {
		options->chicane_fast_grass = (recorded & DRIVING_RULE_CHICANE_FAST_GRASS) != 0;
	}
	if (!(explicit_rules & DRIVING_RULE_AMIGA_GRASS)) {
		options->amiga_grass = (recorded & DRIVING_RULE_AMIGA_GRASS) != 0;
	}
	if (!(explicit_rules & DRIVING_RULE_OWOOT)) {
		options->owoot = (recorded & DRIVING_RULE_OWOOT) != 0;
	}
}

static legacy_u16 selected_driving_rules(const struct GENGHOST_OPTIONS *options)
{
	return (options->powergear ? DRIVING_RULE_POWER_GEAR : 0U) |
		   (options->collision ? DRIVING_RULE_LEGACY_COLLISION : 0U) |
		   (options->left_bias ? DRIVING_RULE_LEFT_CORNER_BIAS : 0U) |
		   (options->dual_way_switching ? DRIVING_RULE_DUAL_WAY_SWITCHING : 0U) |
		   (options->chicane_fast_grass ? DRIVING_RULE_CHICANE_FAST_GRASS : 0U) |
		   (options->amiga_grass ? DRIVING_RULE_AMIGA_GRASS : 0U) |
		   (options->owoot ? DRIVING_RULE_OWOOT : 0U);
}

void extended_source_record_override(legacy_u16 *explicit_rules, legacy_u16 rule)
{
	*explicit_rules |= rule;
}

legacy_int extended_source_decode(struct GENGHOST_SOURCE *source, legacy_u8 *file,
								  size_t file_length, struct GENGHOST_OPTIONS *selected_options,
								  legacy_u16 explicit_rules, legacy_int verify,
								  const legacy_char *car_argument,
								  legacy_char car[REPLAY_CAR_ID_SIZE + 1U],
								  const legacy_char *fine_tune_option)
{
	struct GENGHOST_OPTIONS options = *selected_options;
	legacy_u16 simulation_rate = GENGHOST_FRAME_RATE;
	legacy_u8 *track = file, *source_inputs = NULL, *seed_inputs = NULL;
	legacy_uint track_length = (legacy_uint)file_length, seed_length = 0, source_frames = 0;
	struct GAMEINFO replay = {0};
	struct CSRPL_VIEW extended_replay = {0};
	legacy_int extended = 0, is_replay = 0;
	legacy_char source_error[GENGHOST_SOURCE_ERROR_SIZE];
	if (file_length >= sizeof(csrpl_signature) &&
		memcmp(file, csrpl_signature, sizeof(csrpl_signature)) == 0) {
		if (!csrpl_decode(file, file_length, &extended_replay, source_error,
						  sizeof(source_error))) {
			fprintf(stderr, "Invalid CSRPL replay: %s\n", source_error);
			return -1;
		}
		const struct CSRPL_METADATA *metadata = &extended_replay.metadata;
		if (metadata->frame_count > GENGHOST_MAX_FRAMES) {
			fprintf(stderr, "Replay exceeds the generator's %u-frame limit\n", GENGHOST_MAX_FRAMES);
			return -1;
		}
		extended = is_replay = 1;
		track = file + (extended_replay.track_data - file);
		track_length = (legacy_uint)extended_replay.track_size;
		source_inputs = file + (extended_replay.inputs - file);
		source_frames = metadata->frame_count;
		memcpy(replay.game_playercarid, metadata->player_car_id, REPLAY_CAR_ID_SIZE);
		replay.game_playermaterial = metadata->player_material;
		replay.game_playertransmission = metadata->player_transmission;
		replay.game_opponenttype = metadata->opponent_type;
		replay.game_framespersec = metadata->frames_per_second;
		memcpy(replay.game_trackname, metadata->track_name, REPLAY_TRACK_NAME_SIZE);
		replay_driving_rules(&options, metadata->driving_rules, explicit_rules);
		if (metadata->driving_rules != selected_driving_rules(&options)) {
			fputs("Using explicitly overridden CSRPL driving rules for this simulation\n", stderr);
		}
		if (metadata->version > CSRPL_VERSION) {
			fprintf(stderr,
					"CSRPL version %u is newer than supported version %u; using known layout\n",
					metadata->version, CSRPL_VERSION);
		}
	} else if (file_length >= sizeof(cstrk_signature) &&
			   memcmp(file, cstrk_signature, sizeof(cstrk_signature)) == 0) {
		if (!cstrk_validate_bytes(file, file_length, source_error, sizeof(source_error))) {
			fprintf(stderr, "Invalid CSTRK track: %s\n", source_error);
			return -1;
		}
		extended = 1;
	} else if (file_length != REPLAY_TRACK_SIZE && file_length >= REPLAY_INPUT_OFFSET) {
		replay_gameinfo_decode(&replay, file);
		if ((replay.game_framespersec != GAME_FRAME_RATE_LOW &&
			 replay.game_framespersec != GENGHOST_FRAME_RATE) ||
			replay.game_recordedframes > GENGHOST_LEGACY_MAX_FRAMES ||
			replay_file_size(replay.game_recordedframes) != file_length ||
			replay.game_playertransmission < 0 || replay.game_playertransmission > 1) {
			fputs("Invalid or unsupported replay; expected a complete Stunts 1.1 RPL\n", stderr);
			return -1;
		}
		is_replay = 1;
		track = file + REPLAY_GAMEINFO_SIZE;
		track_length = REPLAY_TRACK_SIZE;
		source_inputs = file + REPLAY_INPUT_OFFSET;
		source_frames = replay.game_recordedframes;
	} else if (file_length != REPLAY_TRACK_SIZE) {
		fputs("Invalid track/replay; expected TRK, RPL, CSTRK or CSRPL data\n", stderr);
		return -1;
	}
	if (options.fine_tune && !is_replay) {
		fprintf(stderr, "%s requires a complete replay (.RPL or .CSRPL), not a track-only file\n",
				fine_tune_option);
		return -1;
	}
	if (is_replay) {
		if (extended) {
			/* Preserve the source timing even when an automatic/opponent replay
			 * supplies only its track rather than compatible driving inputs. */
			simulation_rate = replay.game_framespersec;
		}
		if (options.fine_tune && !extended && replay.game_framespersec != GENGHOST_FRAME_RATE) {
			fputs("Legacy fine-tuning requires a 20 Hz RPL replay\n", stderr);
			return -1;
		}
		if (options.fine_tune || (verify && !car_argument)) {
			for (legacy_uint i = 0; i < REPLAY_CAR_ID_SIZE; ++i) {
				if (!isalnum((legacy_u8)replay.game_playercarid[i]) &&
					replay.game_playercarid[i] != '_') {
					fputs("Invalid CAR code in input replay header\n", stderr);
					return -1;
				}
				car[i] = (legacy_char)toupper((legacy_u8)replay.game_playercarid[i]);
			}
		}
		if ((options.fine_tune || verify || replay.game_opponenttype == 0) &&
			(options.fine_tune || memcmp(replay.game_playercarid, car, REPLAY_CAR_ID_SIZE) == 0)) {
			if (options.fine_tune || verify ||
				replay.game_playertransmission == TRANSMISSION_MANUAL) {
				seed_inputs = source_inputs;
				seed_length = source_frames;
				simulation_rate = replay.game_framespersec;
			}
		}
	}
	if (options.fine_tune && (!seed_inputs || !seed_length)) {
		fprintf(stderr, "%s requires a nonempty replay\n", fine_tune_option);
		return -1;
	}
	if (options.fine_tune && replay.game_playertransmission == TRANSMISSION_AUTOMATIC) {
		fprintf(stderr, "%s preserves automatic seed shifts as manual inputs\n", fine_tune_option);
	}
	if (options.fine_tune && replay.game_opponenttype != 0) {
		fprintf(stderr, "%s requires a solo replay; opponent collisions change the simulation\n",
				fine_tune_option);
		return -1;
	}
	if (verify && (!seed_inputs || !seed_length)) {
		fputs("--verify requires a nonempty replay with matching car\n", stderr);
		return -1;
	}
	if (verify && replay.game_opponenttype != 0 && !extended) {
		fputs("--verify requires a solo legacy replay; opponent metadata is supported in CSRPL\n",
			  stderr);
		return -1;
	}
	source->file = file;
	source->file_length = file_length;
	source->track = track;
	source->track_length = track_length;
	source->seed_inputs = seed_inputs;
	source->seed_length = seed_length;
	source->replay = replay;
	source->extended_replay = extended_replay;
	source->extended = extended;
	source->is_replay = is_replay;
	source->frame_rate = simulation_rate;
	*selected_options = options;
	return 0;
}

legacy_char *extended_source_default_output(const struct GENGHOST_SOURCE *source,
											const legacy_char *cwd, const legacy_char *track_name,
											const legacy_char *car, legacy_int fine_tune)
{
	legacy_char *output;
	if (source->extended) {
		legacy_char default_name[GENGHOST_SOURCE_TRACK_NAME_SIZE + REPLAY_CAR_ID_SIZE +
								 sizeof("-FT.csrpl")];
		if (fine_tune) {
			snprintf(default_name, sizeof(default_name), "%s-FT.csrpl", track_name);
		} else {
			snprintf(default_name, sizeof(default_name), "%s-%s.csrpl", track_name, car);
		}
		output = extended_source_join_path(cwd, default_name);
	} else if (fine_tune) {
		legacy_char prefix[6] = {0}, default_name[13];
		for (legacy_uint i = 0; i < 5U && track_name[i]; ++i) {
			legacy_u8 letter = (legacy_u8)toupper((legacy_u8)track_name[i]);
			prefix[i] = (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9')
							? (legacy_char)letter
							: '_';
		}
		snprintf(default_name, sizeof(default_name), "%s-FT.rpl", prefix);
		output = extended_source_join_path(cwd, default_name);
	} else {
		/* The game's replay picker only enumerates DOS 8.3 filenames. */
		legacy_char prefix[4] = {0}, default_name[13];
		for (legacy_uint i = 0; i < 3U && track_name[i]; ++i) {
			legacy_u8 letter = (legacy_u8)toupper((legacy_u8)track_name[i]);
			prefix[i] = '_';
			if ((letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9')) {
				prefix[i] = (legacy_char)letter;
			}
		}
		snprintf(default_name, sizeof(default_name), "%s-%s.rpl", prefix, car);
		output = extended_source_join_path(cwd, default_name);
	}
	return output;
}

legacy_int extended_source_validate_output(const struct GENGHOST_SOURCE *source,
										   const legacy_char *output)
{
	const legacy_char *output_extension = strrchr(extended_source_base_name(output), '.');
	if (source->extended &&
		(!output_extension || SDL_strcasecmp(output_extension, ".csrpl") != 0)) {
		fputs("CSTRK ghosts require an output filename ending in .csrpl\n", stderr);
		return -1;
	}
	return 0;
}

void extended_source_configure(const struct GENGHOST_SOURCE *source, legacy_int verify,
							   legacy_int fine_tune)
{
	if (source->extended && source->is_replay) {
		struct CSRPL_METADATA metadata = source->extended_replay.metadata;
		metadata.frames_per_second = source->frame_rate;
		if (!verify && !fine_tune) {
			/* Generation uses the embedded track for a solo ghost; existing
			 * opponent-dependent inputs are not a compatible search seed. */
			metadata.opponent_type = 0;
			/* New inputs use this engine's physics, even when the track came
			 * from a recording made by a newer engine. */
			metadata.version = CSRPL_VERSION;
		}
		genghost_sim_set_replay_metadata(&metadata);
	}
}

legacy_int extended_source_materialize_manual(struct GENGHOST_SOURCE *source,
											  const struct GENGHOST_OPTIONS *options,
											  const legacy_char car[REPLAY_CAR_ID_SIZE])
{
	legacy_char source_error[GENGHOST_SOURCE_ERROR_SIZE];
	source->replay.game_playertransmission = TRANSMISSION_MANUAL;
	memcpy(source->replay.game_playercarid, car, REPLAY_CAR_ID_SIZE);
	if (source->extended) {
		struct CSRPL_METADATA metadata = source->extended_replay.metadata;
		metadata.player_transmission = TRANSMISSION_MANUAL;
		metadata.driving_rules = selected_driving_rules(options);
		memcpy(metadata.player_car_id, car, REPLAY_CAR_ID_SIZE);
		legacy_u8 *manual_file = NULL;
		size_t manual_length = 0;
		if (!csrpl_encode(&metadata, source->track, source->track_length, source->seed_inputs,
						  source->seed_length, &manual_file, &manual_length, source_error,
						  sizeof(source_error))) {
			fprintf(stderr, "Cannot materialize manual CSRPL seed: %s\n", source_error);
			return -1;
		}
		free(source->file);
		source->file = manual_file;
		source->file_length = manual_length;
		if (!csrpl_decode(source->file, source->file_length, &source->extended_replay, source_error,
						  sizeof(source_error))) {
			return -1;
		}
		source->seed_inputs = source->file + (source->extended_replay.inputs - source->file);
		source->track = source->file + (source->extended_replay.track_data - source->file);
	} else {
		replay_gameinfo_encode(source->file, &source->replay);
	}
	return 0;
}
