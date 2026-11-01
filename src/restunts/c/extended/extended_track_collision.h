#ifndef RESTUNTS_EXTENDED_TRACK_COLLISION_H
#define RESTUNTS_EXTENDED_TRACK_COLLISION_H

#include "../math.h"

#ifdef RESTUNTS_SDL3
/* Dynamic CSTRK planes must survive nested collision and camera queries. */
struct EXTENDED_TRACK_COLLISION_SNAPSHOT {
	struct PLANE plane;
	struct VECTORLONG exact_normal;
	legacy_u8 plane_valid, exact_normal_valid;
};

void build_extended_track_terrain(struct VECTOR *position);
/* Exact selected mesh-plane contact, retaining the wheel's 26.6 coordinates. */
legacy_s16 track_selected_plane_contact_fixed(const struct VECTORLONG *position, legacy_s32 *height,
											  legacy_s16 *distance);
#endif
/* Precise 26.6 sweeps retain sub-unit wheel clearance on finite CSTRK solids.
 * Native word-resource walls and legacy tracks retain their original queries. */
legacy_s16 track_solid_obstacle_contact_fixed(struct VECTORLONG *first, struct VECTORLONG *second,
											  legacy_s16 *fraction);
/* Wheel-wall sweeps omit drivable ramp-top and internal-seam entry, while
 * preserving side, bottom, terminal-cap, support and slalom collisions. */
legacy_s16 track_solid_wall_contact_fixed(struct VECTORLONG *first, struct VECTORLONG *second,
										  legacy_s16 *fraction);
legacy_s16 track_wall_intersects_segment_fixed(struct VECTORLONG *first, struct VECTORLONG *second);

#endif
