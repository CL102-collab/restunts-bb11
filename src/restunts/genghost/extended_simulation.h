#ifndef RESTUNTS_GENGHOST_EXTENDED_SIMULATION_H
#define RESTUNTS_GENGHOST_EXTENDED_SIMULATION_H

#include "simulation.h"

void extended_genghost_sim_set_metadata(const struct CSRPL_METADATA *metadata);
legacy_uint extended_genghost_sim_frame(void);
legacy_uint extended_genghost_sim_frame_limit(void);
void extended_genghost_sim_capture(struct GENGHOST_SNAPSHOT *saved);
void extended_genghost_sim_restore(const struct GENGHOST_SNAPSHOT *saved);
legacy_int extended_genghost_sim_load(const legacy_u8 *track, legacy_uint track_size,
									  legacy_int *handled);
void extended_genghost_sim_configure(void);
legacy_int extended_genghost_sim_begin(legacy_int handled);
legacy_u8 extended_genghost_sim_input(legacy_u8 input);
legacy_int extended_genghost_sim_record(legacy_u8 input);
legacy_int extended_genghost_sim_result(struct GENGHOST_RESULT *result);
legacy_int extended_genghost_sim_write(const legacy_char *path, const legacy_char *track_name,
									   const legacy_u8 *inputs, legacy_uint length,
									   legacy_int *handled);

#endif
