#ifndef RESTUNTS_GENGHOST_EXTENDED_SOURCE_H
#define RESTUNTS_GENGHOST_EXTENDED_SOURCE_H

#include "search.h"
#include "../c/extended/csrpl.h"

#define GENGHOST_SOURCE_TRACK_NAME_SIZE 256U

/* The CLI treats source containers uniformly after format-specific admission. */
struct GENGHOST_SOURCE {
	legacy_u8 *file, *track, *seed_inputs;
	size_t file_length;
	legacy_uint track_length, seed_length;
	legacy_u16 frame_rate;
	legacy_int extended, is_replay;
	struct GAMEINFO replay;
	struct CSRPL_VIEW extended_replay;
};

legacy_char *extended_source_join_path(const legacy_char *directory, const legacy_char *name);
const legacy_char *extended_source_base_name(const legacy_char *path);
legacy_char *extended_source_with_suffix(const legacy_char *path, const legacy_char *suffix);
legacy_int extended_source_read(const legacy_char *source, const legacy_char *cwd,
								const legacy_char *data_dir, legacy_int replay_only,
								legacy_u8 **data, size_t *length, legacy_char **resolved_path);
void extended_source_record_override(legacy_u16 *explicit_rules, legacy_u16 rule);
legacy_int extended_source_decode(struct GENGHOST_SOURCE *source, legacy_u8 *file,
								  size_t file_length, struct GENGHOST_OPTIONS *selected_options,
								  legacy_u16 explicit_rules, legacy_int verify,
								  const legacy_char *car_argument,
								  legacy_char car[REPLAY_CAR_ID_SIZE + 1U],
								  const legacy_char *fine_tune_option);
legacy_char *extended_source_default_output(const struct GENGHOST_SOURCE *source,
											const legacy_char *cwd, const legacy_char *track_name,
											const legacy_char *car, legacy_int fine_tune);
legacy_int extended_source_validate_output(const struct GENGHOST_SOURCE *source,
										   const legacy_char *output);
void extended_source_configure(const struct GENGHOST_SOURCE *source, legacy_int verify,
							   legacy_int fine_tune);
legacy_int extended_source_materialize_manual(struct GENGHOST_SOURCE *source,
											  const struct GENGHOST_OPTIONS *options,
											  const legacy_char car[REPLAY_CAR_ID_SIZE]);

#endif
