#ifndef RESTUNTS_EXTENDED_PHYSICS_COLLISION_H
#define RESTUNTS_EXTENDED_PHYSICS_COLLISION_H

#include "../track/track_types.h"

#ifdef RESTUNTS_SDL3
struct TRACK_COLLISION_SNAPSHOT;

legacy_s32 extended_collision_coordinate_delta(legacy_s16 current, legacy_s16 previous,
											   legacy_s32 legacy_delta);
const struct PLANE far *
extended_collision_snapshot_plane(const struct TRACK_COLLISION_SNAPSHOT *saved);
/* Return true when extended physics supplied the result, including no contact. */
legacy_s16 extended_sweep_track_solid_obstacle(struct VECTORLONG *previous,
											   struct VECTORLONG *current, legacy_s16 *fraction,
											   legacy_s16 *result);
legacy_s16 extended_sweep_track_wall_span(struct VECTORLONG *previous_first,
										  struct VECTORLONG *previous_second,
										  struct VECTORLONG *current_first,
										  struct VECTORLONG *current_second, legacy_s16 *fraction,
										  legacy_s16 *result);
legacy_s16 extended_get_track_collision_points(legacy_s16 column_arg, legacy_s16 row_arg,
											   struct VECTOR *output, legacy_s16 *result);
/* Scale the ordinary auxiliary obstacle box for one pine in a scenery group. */
void extended_track_collision_bounds(legacy_s16 column, legacy_s16 row, legacy_u8 index,
									 struct POINT2D bounds[2]);
#endif

#endif
