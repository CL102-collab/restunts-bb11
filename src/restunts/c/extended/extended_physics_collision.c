#include "../legacy.h"
#include "../math/math.h"
#include "../physics/physics_internal.h"
#include "../physics/physics_collision_internal.h"
#include "../track/track_collision.h"
#include "../track/track_objects.h"
#include "extended_physics_collision.h"

#ifdef RESTUNTS_SDL3
#include "extended_track.h"
#include "extended_geometry.h"
#if !defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY)
#include "extended_replay_runtime.h"
#endif

legacy_s32 extended_collision_coordinate_delta(legacy_s16 current, legacy_s16 previous,
											   legacy_s32 legacy_delta)
{
	return extended_track_active() ? LEGACY_S16_WRAP_SUB(current, previous) : legacy_delta;
}

const struct PLANE far *
extended_collision_snapshot_plane(const struct TRACK_COLLISION_SNAPSHOT *saved)
{
	return saved->extended.plane_valid ? &saved->extended.plane : saved->plane;
}

static legacy_s16 track_sweep_uses_fixed_positions(void)
{
	if (!extended_track_active()) {
		return 0;
	}
#if !defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY)
	/* Replaying an older recording retains its original contact decisions. */
	if (extended_replay_is_active() &&
		extended_replay_version() < CSRPL_FIXED_POINT_SOLID_VERSION) {
		return 0;
	}
#endif
	return 1;
}

static void sweep_fixed_position(struct VECTORLONG *result, const struct VECTORLONG *previous,
								 const struct VECTORLONG *current, legacy_s16 fraction)
{
	result->lx = LEGACY_S32_WRAP_ADD_S16(
		previous->lx, scale_position_delta(current->lx, previous->lx, fraction, TRIG_FIXED_ONE));
	result->ly = LEGACY_S32_WRAP_ADD_S16(
		previous->ly, scale_position_delta(current->ly, previous->ly, fraction, TRIG_FIXED_ONE));
	result->lz = LEGACY_S32_WRAP_ADD_S16(
		previous->lz, scale_position_delta(current->lz, previous->lz, fraction, TRIG_FIXED_ONE));
}

static legacy_s16 sweep_fixed_track_solid(struct VECTORLONG *previous, struct VECTORLONG *current,
										  legacy_s16 *fraction)
{
	legacy_s16 contact;
	if (!track_solid_wall_contact_fixed(previous, current, &contact) ||
		track_solid_obstacle_contact_fixed(previous, previous, &contact)) {
		return 0;
	}
	legacy_s16 clear_fraction = 0, contact_fraction = TRIG_FIXED_ONE;
	while (contact_fraction - clear_fraction > 1) {
		legacy_s16 midpoint = clear_fraction + (contact_fraction - clear_fraction) / 2;
		struct VECTORLONG end;
		sweep_fixed_position(&end, previous, current, midpoint);
		if (track_solid_wall_contact_fixed(previous, &end, &contact)) {
			contact_fraction = midpoint;
		} else {
			clear_fraction = midpoint;
		}
	}
	*fraction = clear_fraction;
	return 1;
}

static legacy_s16 sweep_fixed_track_wall_span(struct VECTORLONG *previous_first,
											  struct VECTORLONG *previous_second,
											  struct VECTORLONG *current_first,
											  struct VECTORLONG *current_second,
											  legacy_s16 *fraction)
{
	if (!track_wall_intersects_segment_fixed(current_first, current_second) ||
		track_wall_intersects_segment_fixed(previous_first, previous_second)) {
		return 0;
	}
	legacy_s16 clear_fraction = 0, contact_fraction = TRIG_FIXED_ONE;
	while (contact_fraction - clear_fraction > 1) {
		legacy_s16 midpoint = clear_fraction + (contact_fraction - clear_fraction) / 2;
		struct VECTORLONG first, second;
		sweep_fixed_position(&first, previous_first, current_first, midpoint);
		sweep_fixed_position(&second, previous_second, current_second, midpoint);
		if (track_wall_intersects_segment_fixed(&first, &second)) {
			contact_fraction = midpoint;
		} else {
			clear_fraction = midpoint;
		}
	}
	*fraction = clear_fraction;
	return 1;
}

legacy_s16 extended_sweep_track_solid_obstacle(struct VECTORLONG *previous,
											   struct VECTORLONG *current, legacy_s16 *fraction,
											   legacy_s16 *result)
{
	if (!track_sweep_uses_fixed_positions()) {
		return 0;
	}
	*result = sweep_fixed_track_solid(previous, current, fraction);
	return 1;
}

legacy_s16 extended_sweep_track_wall_span(struct VECTORLONG *previous_first,
										  struct VECTORLONG *previous_second,
										  struct VECTORLONG *current_first,
										  struct VECTORLONG *current_second, legacy_s16 *fraction,
										  legacy_s16 *result)
{
	if (!track_sweep_uses_fixed_positions()) {
		return 0;
	}
	*result = sweep_fixed_track_wall_span(previous_first, previous_second, current_first,
										  current_second, fraction);
	return 1;
}

legacy_s16 extended_get_track_collision_points(legacy_s16 column_arg, legacy_s16 row_arg,
											   struct VECTOR *output, legacy_s16 *result)
{
	if (!extended_track_active()) {
		return 0;
	}
	const struct CSTRK_OBJECT *object = extended_track_object_at(column_arg, row_arg);
	if (!object) {
		*result = 0;
		return 1;
	}
	/* The generated launch ramp supplies its own finite concrete supports;
	 * its backing corkscrew ID must not introduce invisible native posts. */
	if (object->roll_ramp) {
		*result = 0;
		return 1;
	}
	legacy_u16 id = extended_track_object_id(object);
	if (id >= sizeof(trkObjectList) / sizeof(trkObjectList[0])) {
		*result = 0;
		return 1;
	}
	const struct VECTOR *points;
	legacy_u16 count = physics_collision_model_points((legacy_u8)id, &points);
	struct VECTOR origin;
	extended_geometry_object_origin(extended_track_model(), object, &origin);
	physics_transform_collision_points(points, output, count, trkObjectList[id].ss_rotY, 0, 0, 0);
	for (legacy_u16 i = 0; i < count; i++) {
		if (extended_geometry_uses_grade(object)) {
			output[i].y = LEGACY_S16_WRAP_ADD(
				output[i].y, extended_geometry_object_grade(object, output[i].x, output[i].z));
		}
		output[i].x = LEGACY_S16_WRAP_ADD(output[i].x, origin.x);
		output[i].y = LEGACY_S16_WRAP_ADD(output[i].y, origin.y);
		output[i].z = LEGACY_S16_WRAP_ADD(output[i].z, origin.z);
	}
	*result = (legacy_s16)count;
	return 1;
}
#endif
