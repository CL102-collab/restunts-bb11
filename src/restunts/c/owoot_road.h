#ifndef RESTUNTS_OWOOT_ROAD_H
#define RESTUNTS_OWOOT_ROAD_H

#include "owoot.h"
#include "gamestate.h"

#define OWOOT_WHEEL_RING_MAX OWOOT_WHEEL_RING_COUNT
#define OWOOT_WHEEL_VERTEX_MAX OWOOT_WHEEL_VERTEX_COUNT

/* Test the rendered road footprint in an element's local XZ coordinates.
 * Height is ignored; triangle edges are part of the footprint. */
legacy_s16 track_road_contains_local_point(legacy_s16 physical_model,
										   const struct VECTOR *position);

/* Supply two convex wheel rims in matching cyclic order: the first ring_count
 * vertices form one rim and the next ring_count form the other. The complete
 * tire volume must overlap a road surface vertically, allowing the physics
 * contact tolerance only when road_contact identifies this wheel's current
 * paved/dirt/ice contact. Airborne wheels must reach the exact surface height.
 * This camera-independent query does not change track state. */
legacy_s16 track_road_overlaps_wheel(const struct VECTOR *vertices, legacy_u16 ring_count,
									 legacy_s16 road_contact);

/* Project a tire into a closed pipe's portal coordinates: x is lateral,
 * y is height above its floor, and z is ignored. Some tire area must enter
 * the aperture interior; touching its outer ceiling does not pass through. */
legacy_s16 track_pipe_aperture_overlaps_wheel(const struct VECTOR *vertices, legacy_u16 count);
/* The same portal coordinates/interior rule for the rectangular tunnel. */
legacy_s16 track_tunnel_aperture_overlaps_wheel(const struct VECTOR *vertices, legacy_u16 count);

/* Sweep the convex envelope of all four tires and body-contact corners
 * through the actual slalom barrier footprints, ignoring their height.
 * center/rotation locate the tile; motion is the current-minus-previous car
 * translation in world coordinates.
 * This prevents hopping the barriers while retaining the full road width. */
legacy_s16 track_slalom_wheel_envelope_crosses_barrier(
	const struct VECTOR vertices[CARSTATE_WHEEL_COUNT][OWOOT_WHEEL_VERTEX_MAX],
	const legacy_u16 counts[CARSTATE_WHEEL_COUNT], const struct VECTOR body[CARSTATE_WHEEL_COUNT],
	const struct VECTOR *center, legacy_s16 rotation, const struct VECTOR *motion);

#endif
