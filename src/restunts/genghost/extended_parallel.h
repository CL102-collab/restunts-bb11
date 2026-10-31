#ifndef RESTUNTS_GENGHOST_EXTENDED_PARALLEL_H
#define RESTUNTS_GENGHOST_EXTENDED_PARALLEL_H

#include "simulation.h"

const legacy_char *extended_parallel_source_extension(const legacy_u8 *source, size_t size);
legacy_int extended_parallel_source_replay_inputs(const legacy_u8 *source, size_t size,
												  const legacy_u8 **inputs, legacy_uint *length);

#endif
