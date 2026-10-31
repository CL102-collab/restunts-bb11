#include "extended_maneuvers.h"
#include "maneuvers_internal.h"
#include "../c/extended_track.h"
#include "../c/gamestate.h"

#define MANEUVER_BARREL_HORIZON 160U
#define MANEUVER_BARREL_APPROACH_SPEED 100.0
#define MANEUVER_EXTENDED_POSITION_SCALE 64.0
#define MANEUVER_EXTENDED_COORDINATE_LIMIT 32767.0

const legacy_char *extended_genghost_maneuver_name(enum GENGHOST_MANEUVER_KIND kind)
{
	return kind == GENGHOST_MANEUVER_BARREL_ROLL ? "barrel roll" : NULL;
}

legacy_int extended_genghost_slalom_flight_allowed(const struct GENGHOST_MANEUVER *action)
{
	/* A combined slalom has a roof, so an ordinary over-stone flight cannot
	 * infer clearance through its shell. */
	return !action->approach.tunnel_slalom;
}

legacy_int extended_genghost_maneuver_skip_feature(const struct GENGHOST_MANEUVER *feature)
{
	return feature->approach.roll_ramp != 0;
}

legacy_int extended_genghost_maneuver_preserves_inputs(const struct GENGHOST_DRIVER *driver,
													   const struct GENGHOST_MANEUVER *action)
{
	return driver->barrel_active || action->kind == GENGHOST_MANEUVER_BARREL_ROLL;
}

legacy_s16 extended_genghost_maneuver_target_height(const struct GENGHOST_WAYPOINT *target,
													const struct CARSTATE *car)
{
	if (!extended_track_active() || !target->height_specified) {
		return 0;
	}
	legacy_f64 height = target->y - car->car_position.ly / MANEUVER_EXTENDED_POSITION_SCALE;
	return (legacy_s16)(height < -MANEUVER_EXTENDED_COORDINATE_LIMIT
							? -MANEUVER_EXTENDED_COORDINATE_LIMIT
						: height > MANEUVER_EXTENDED_COORDINATE_LIMIT
							? MANEUVER_EXTENDED_COORDINATE_LIMIT
							: height);
}

void extended_genghost_add_barrel_maneuvers(const struct GENGHOST_ROUTE *route,
											const struct GENGHOST_DRIVER *driver,
											const struct GENGHOST_MANEUVER *ordinary, size_t begin,
											size_t end, struct GENGHOST_MANEUVER *actions,
											size_t *count, size_t capacity)
{
	struct GENGHOST_MANEUVER action;
	/* A barrel launch is a directional road followed by real unsupported
	 * flight. Keep its centered approach distinct from the stock corkscrew
	 * bypass/launch recipes, including while OWOOT requires the full roll. */
	size_t roll_begin = begin, roll_end = end;
	for (legacy_uint number = 0U; number < 2U && roll_begin < route->point_count; ++number) {
		if (route->points[roll_begin].roll_ramp) {
			static const legacy_f64 scales[] = {1.0, 0.85, 1.15};
			for (legacy_uint variant = 0U; variant < sizeof(scales) / sizeof(scales[0]);
				 ++variant) {
				action =
					genghost_maneuver_feature_action(ordinary, route, driver, roll_begin, roll_end);
				action.kind = GENGHOST_MANEUVER_BARREL_ROLL;
				action.horizon_frames = MANEUVER_BARREL_HORIZON;
				action.road_only = 1U;
				action.gene.stunt_mode = GENGHOST_STUNT_BARREL;
				action.gene.lateral_offset = 0.0;
				action.gene.speed_scale = scales[variant];
				action.gene.corner_scale = 1.0;
				action.gene.powergear_bias = 0.0;
				action.approach_speed = MANEUVER_BARREL_APPROACH_SPEED * scales[variant];
				action.exit_speed = action.approach_speed;
				genghost_maneuver_add_action(actions, count, capacity, &action);
			}
		}
		roll_begin = genghost_maneuver_feature_ahead(route, driver, roll_end + 1U, &roll_end);
	}
}
