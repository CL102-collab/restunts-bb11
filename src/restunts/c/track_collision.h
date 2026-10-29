#ifndef RESTUNTS_TRACK_COLLISION_H
#define RESTUNTS_TRACK_COLLISION_H

#include "track_types.h"
#ifdef RESTUNTS_SDL3
#include "extended_geometry.h"
#endif

#define ROAD_HALF_WIDTH 120
#define ELEVATED_DECK_CLEARANCE 390
#define TUNNEL_HEIGHT 144
#define SLALOM_POLE_INNER_X 23
#define SLALOM_POLE_OUTER_X 97
#define SLALOM_POLE_NEAR_Z 241
#define SLALOM_POLE_FAR_Z 271

/* Collision planes and walls for the currently selected track object. */

extern legacy_s16 planindex;
extern legacy_s16 planindex_copy;
extern legacy_s8 current_surf_type;
extern legacy_s16 wallindex;
extern legacy_s16 elRdWallRelated;
extern legacy_s16 wallHeight;
extern legacy_s16 wallStartX;
extern legacy_s16 wallStartZ;
extern legacy_s16 wallOrientation;
extern struct PLANE far *planptr;
extern struct PLANE far *current_planptr;
extern legacy_s16 elem_xCenter;
extern legacy_s16 elem_zCenter;
extern legacy_s16 terrainHeight;
extern legacy_s8 track_wall_collision_enabled;
extern struct TRACK_WALL far *wallptr;

struct TRACK_COLLISION_SNAPSHOT {
#ifdef RESTUNTS_SDL3
	/* Dynamic extended planes must survive nested collision/camera queries. */
	struct PLANE extended_plane;
	legacy_u8 extended_plane_valid;
#endif
	legacy_s16 plane_index;
	struct PLANE far *plane;
	legacy_s16 wall_index;
	legacy_s16 wall_height;
	legacy_s16 wall_lower_bound;
	legacy_u8 corkscrew;
	legacy_s8 surface_type;
	legacy_s8 wall_collision_enabled;
	legacy_s16 terrain_height;
	legacy_s16 element_x;
	legacy_s16 element_z;
	legacy_s16 wall_x;
	legacy_s16 wall_z;
	legacy_s16 wall_orientation;
};

/* Preserve collision selection around renderer-only camera queries. */
void track_collision_capture(struct TRACK_COLLISION_SNAPSHOT *saved);
void track_collision_restore(const struct TRACK_COLLISION_SNAPSHOT *saved);

void build_track_object(struct VECTOR *, struct VECTOR *);
#ifdef RESTUNTS_SDL3
void build_extended_track_terrain(struct VECTOR *position);
#endif
/* Segment queries preserve the currently selected collision state. */
legacy_s16 track_wall_intersects_segment(struct VECTOR *first, struct VECTOR *second);
/* Contact fractions use Q14; a segment already inside returns zero. */
legacy_s16 track_solid_obstacle_contact(struct VECTOR *first, struct VECTOR *second,
										legacy_s16 *fraction);
legacy_s16 track_surface_contains_point(struct VECTOR *point);

typedef legacy_s16 (*TRACK_SURFACE_SWEEP_TEST)(struct VECTOR *previous, struct VECTOR *current,
											   legacy_s16 *fraction);
legacy_s16 sweep_track_surface_candidates(struct VECTOR *previous, struct VECTOR *current,
										  TRACK_SURFACE_SWEEP_TEST test, legacy_s16 *fraction);

#define TRACK_PLAN_RESOURCE_COUNT 536U
#define TRACK_WALL_RESOURCE_COUNT 191U

void load_track_collision_resources(void);

void track_collision_resources_decode(const legacy_u8 far *plane_source,
									  const legacy_u8 far *wall_source);

legacy_s16 plane_signed_distance(legacy_s16 plane_index, legacy_s16 x, legacy_s16 y, legacy_s16 z);

#endif
