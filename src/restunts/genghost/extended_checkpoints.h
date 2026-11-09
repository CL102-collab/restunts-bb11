#ifndef RESTUNTS_GENGHOST_EXTENDED_CHECKPOINTS_H
#define RESTUNTS_GENGHOST_EXTENDED_CHECKPOINTS_H

#include "routes.h"

#define GENGHOST_CHECKPOINT_BLUE 1U
#define GENGHOST_CHECKPOINT_RED 2U
#define GENGHOST_CHECKPOINT_NONE 4U

/* A nonzero result identifies colors whose complete authored checkpoint set
 * occurs on this path. NONE means the track has no checkpoint requirement. */
legacy_u8 extended_genghost_checkpoint_colors(const legacy_int *path, size_t count);
legacy_u8 extended_genghost_checkpoint_piece_color(legacy_int piece);
legacy_int extended_genghost_checkpoint_piece_required(const struct GENGHOST_ROUTE *route,
													   legacy_int piece);
/* The first required gate not yet crossed by the car, or piece_count. */
size_t extended_genghost_checkpoint_frontier(const struct GENGHOST_ROUTE *route);
legacy_int extended_genghost_checkpoint_target(const struct GENGHOST_ROUTE *route,
											   const struct GENGHOST_DRIVER *driver,
											   legacy_f64 lookahead,
											   struct GENGHOST_WAYPOINT *target);

#endif
