#ifndef RESTUNTS_TRACKOBJ_INTERNAL_H
#define RESTUNTS_TRACKOBJ_INTERNAL_H

#include "track_collision.h"

/* Shared native element geometry used by legacy and extended track queries. */
#define TRACK_ARC_CENTER_OFFSET 1024
#define LARGE_CORNER_INNER_RADIUS 1416
#define LARGE_CORNER_OUTER_RADIUS 1656
#define BANKED_CORNER_INNER_OFFSET 120
#define BANKED_CORNER_OUTER_OFFSET 126
#define ELEVATED_CORNER_OUTER_OFFSET 150
#define NO_PLANE_INDEX 0
#define TRACK_WALL_NONE -1
#define GRASS_HEIGHT_HASH_SHIFT 8U
#define GRASS_HEIGHT_VARIATION_MASK 1U
#define NON_GRASS_HEIGHT_OFFSET 2
#define SLALOM_POLE_WALL_HEIGHT 42
#define TRACK_SLALOM_BOX_COUNT 2U

struct TRACK_OBJECT_SAMPLE {
	struct VECTOR position;
	struct VECTOR next_position;
	legacy_s16 height;
	legacy_s16 next_height;
	legacy_s16 physical_model;
	legacy_s16 surface_type;
	legacy_s16 absolute_x;
	legacy_s16 absolute_z;
	legacy_s16 wall_orientation_modifier;
};

struct TRACK_SOLID_BOX {
	struct VECTOR minimum;
	struct VECTOR maximum;
};

extern const struct TRACK_SOLID_BOX slalom_boxes[TRACK_SLALOM_BOX_COUNT];
void track_rotate_local(struct VECTOR *position, legacy_s16 orientation);
legacy_s16 track_radius_in_band(legacy_s16 x, legacy_s16 z, legacy_s16 low, legacy_s16 high);
legacy_s16 track_arc_radius(const struct VECTOR *position);
void track_object_tunnel(struct TRACK_OBJECT_SAMPLE *sample);
void track_object_slalom(struct TRACK_OBJECT_SAMPLE *sample);
void track_object_model(struct TRACK_OBJECT_SAMPLE *sample);
void track_object_world_collision(const struct VECTOR *world_position,
								  legacy_s16 element_orientation,
								  legacy_s16 wall_orientation_modifier);
legacy_s16 solid_box_contact(const struct VECTOR *first, const struct VECTOR *second,
							 const struct TRACK_SOLID_BOX *box, legacy_s16 *fraction);
legacy_s16 selected_wall_intersects_segment(struct VECTOR *first, struct VECTOR *second);

#endif
