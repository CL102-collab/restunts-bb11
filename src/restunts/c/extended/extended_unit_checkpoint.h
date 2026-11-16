#ifndef RESTUNTS_EXTENDED_UNIT_CHECKPOINT_H
#define RESTUNTS_EXTENDED_UNIT_CHECKPOINT_H

#include "../legacy.h"

struct UNIT_ROUTE;
struct CARSTATE;
struct UNIT_PLAN;

/* Include narrow gate apertures in planned route lines. */
void unit_checkpoint_align_route(struct UNIT_ROUTE *route);

/* Every gate on the selected route must be physically crossed before leaving
 * its final sample. The native checkpoint observer owns the visited state, so
 * recording and playback derive the same route provenance after a rewind. */
legacy_int unit_checkpoint_can_advance(const struct UNIT_ROUTE *route, legacy_u32 cursor);
/* Align with the actual aperture instead of a shifted racing line. A missed
 * gate can be crossed from its far side; recovery derives entirely from the
 * car pose and native visited bits, with no extra mutable phase to snapshot. */
legacy_int unit_checkpoint_guide(const struct UNIT_ROUTE *route, legacy_u32 cursor,
								 const struct CARSTATE *car, struct UNIT_PLAN *plan);

#endif
