#ifndef RESTUNTS_EXTENDED_TRACKOBJ_H
#define RESTUNTS_EXTENDED_TRACKOBJ_H

#include "../track/track_collision.h"

#ifdef RESTUNTS_SDL3
void extended_trackobj_reset(void);
legacy_s16 extended_trackobj_build(struct VECTOR *position, struct VECTOR *next);
void extended_trackobj_capture(struct TRACK_COLLISION_SNAPSHOT *saved);
void extended_trackobj_restore(const struct TRACK_COLLISION_SNAPSHOT *saved);
legacy_s32 extended_trackobj_coordinate_delta(legacy_s16 first, legacy_s16 second,
											  legacy_s32 difference);
legacy_s16 extended_trackobj_solid_contact(struct VECTOR *first, struct VECTOR *second,
										   legacy_s16 *fraction, legacy_s16 *hit);
#endif

#endif
