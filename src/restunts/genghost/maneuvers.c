#include <math.h>
#include <string.h>

#include "maneuvers.h"
#include "../c/externs.h"
#include "../c/game_input.h"
#include "../c/track_objects.h"
#include "../c/track_collision.h"
#include "../c/trackdata_layout.h"
#include "../c/extended_track.h"

#undef memcpy

/* Legacy c/math.h precedes the platform header in this build. */
extern legacy_f64 fabs(legacy_f64);
extern legacy_f64 sqrt(legacy_f64);
extern legacy_f64 atan(legacy_f64);
extern legacy_f64 sin(legacy_f64);
extern legacy_f64 fmin(legacy_f64, legacy_f64);
extern legacy_f64 fmax(legacy_f64, legacy_f64);

#define MANEUVER_ANGLE_SCALE (ANGLE_FULL_TURN / 6.28318530717958647693)
#define MANEUVER_MPH_TRAVEL (11.0 / 30.0)
#define MANEUVER_BARREL_HORIZON 160U
#define MANEUVER_BARREL_APPROACH_SPEED 100.0

static legacy_f64 bounded(legacy_f64 value, legacy_f64 low, legacy_f64 high)
{
	return value < low ? low : value > high ? high : value;
}

const legacy_char *genghost_maneuver_name(enum GENGHOST_MANEUVER_KIND kind)
{
	static const legacy_char *const names[] = {
		"follow",		   "accelerate",		 "brake",	   "tile touch", "stunt",
		"stunt alternate", "chicane fast grass", "launch",	   "power gear", "landing",
		"slalom center",   "slalom jump",		 "barrel roll"};
	return (legacy_uint)kind < sizeof(names) / sizeof(names[0]) ? names[kind] : "unknown";
}

legacy_int genghost_maneuver_powergear_ready(void)
{
	const struct CARSTATE *car = &state.playerstate;
	if (car->car_changing_gear || car->car_current_gear <= 0 ||
		car->car_sumSurfRearWheels == CAR_WHEEL_CONTACT_NONE || simd_player.car_mass <= 0 ||
		(legacy_u16)car->car_currpm > (legacy_u16)simd_player.max_rpm) {
		return 0;
	}
	legacy_uint rpm = (legacy_u16)car->car_currpm;
	if ((rpm >> 7U) >= sizeof(simd_player.torque_curve)) {
		return 0;
	}
	legacy_u8 torque = car->car_current_gear <= 1 && rpm < 2600U
						   ? (legacy_u8)simd_player.idle_torque
						   : (legacy_u8)simd_player.torque_curve[rpm >> 7U];
	/* update_car_speed decrements the limiter before grounded_acceleration_delta. */
	if (car->car_engineLimiterTimer != 0 && car->car_engineLimiterTimer != 1 && rpm < 5000U) {
		torque = ((legacy_u8)simd_player.idle_torque + torque) >> 1U;
	}
	legacy_s16 force = LEGACY_S16_WRAP_ADD(
		LEGACY_S16_WRAP_SUB(car->car_pseudoGravity,
							simd_player.aerorestable[car->car_rev_speed >> 10U]),
		LEGACY_S16_FROM_BITS(
			(legacy_u16)(LEGACY_U16_WRAP_MUL(car->car_gearratioshr8, torque) >> 4U)));
	if (force >= 0) {
		return 0;
	}
	legacy_s32 product = (legacy_s32)force * 25;
	legacy_s16 original =
		LEGACY_S16_FROM_BITS((legacy_u16)((legacy_u32)product / (legacy_u16)simd_player.car_mass));
	legacy_s16 corrected = LEGACY_S16_FROM_BITS((legacy_u16)(product / simd_player.car_mass));
	return LEGACY_S16_SAR(original, 1) > 0 && LEGACY_S16_SAR(corrected, 1) < 0;
}

static legacy_int is_chicane(legacy_int model)
{
	return model == PHYSICAL_MODEL_CHICANE_RIGHT_LEFT || model == PHYSICAL_MODEL_CHICANE_LEFT_RIGHT;
}

static legacy_int can_launch(legacy_int model)
{
	return model == PHYSICAL_MODEL_RAMP || model == PHYSICAL_MODEL_SOLID_RAMP ||
		   model == PHYSICAL_MODEL_LOOP || model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A ||
		   model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B ||
		   model == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT ||
		   (model >= PHYSICAL_MODEL_ELEVATED_ROAD && model <= PHYSICAL_MODEL_OVERPASS);
}

static size_t feature_ahead(const struct GENGHOST_ROUTE *route,
							const struct GENGHOST_DRIVER *driver, size_t begin, size_t *end)
{
	while (begin < route->point_count && begin &&
		   route->points[begin - 1U].piece_index == route->points[begin].piece_index) {
		--begin;
	}
	while (begin < route->point_count &&
		   route->points[begin].distance <= driver->progress + 1800.0) {
		*end = begin;
		while (*end + 1U < route->point_count &&
			   route->points[*end + 1U].piece_index == route->points[begin].piece_index) {
			++*end;
		}
		if (*end > begin && route->points[*end].distance > driver->progress + 40.0 &&
			genghost_stunt_catalog_model(route->points[begin].physical_model)) {
			return begin;
		}
		begin = *end + 1U;
	}
	return route->point_count;
}

static struct GENGHOST_WAYPOINT landing_ahead(const struct GENGHOST_ROUTE *route, size_t start,
											  legacy_uint skip)
{
	size_t index = start;
	while (index + 1U < route->point_count && skip) {
		++index;
		if (route->points[index].piece_index != route->points[index - 1U].piece_index) {
			--skip;
		}
	}
	return route->points[index];
}

/* A terrain slope need not have a catalog stunt model. Actual native route
 * heights are enough to propose preparatory shifts before an ascent; unknown
 * heights never become an invented ramp. The simulator decides its usefulness. */
static legacy_int uphill_ahead(const struct GENGHOST_ROUTE *route,
							   const struct GENGHOST_DRIVER *driver, size_t *crest)
{
	*crest = driver->waypoint;
	for (size_t i = driver->waypoint + 1U; i < route->point_count; ++i) {
		const struct GENGHOST_WAYPOINT *a = &route->points[i - 1U], *b = &route->points[i];
		if (b->distance > driver->progress + 1800.0) {
			break;
		}
		if (a->height_specified && b->height_specified && b->y > a->y + 24.0) {
			*crest = i;
			return 1;
		}
	}
	return 0;
}

static void turn_quarter(legacy_f64 x, legacy_f64 z, legacy_int angle, legacy_f64 *tx,
						 legacy_f64 *tz)
{
	switch (((legacy_uint)angle & 1023U) / 256U) {
		case 1U:
			*tx = z;
			*tz = -x;
			break;
		case 2U:
			*tx = -x;
			*tz = -z;
			break;
		case 3U:
			*tx = -z;
			*tz = x;
			break;
		default:
			*tx = x;
			*tz = z;
			break;
	}
}

static void straight_chicane(struct GENGHOST_MANEUVER *action, legacy_uint variant)
{
	legacy_int piece = action->approach.piece;
	legacy_uint tile = (legacy_u8)track_route_element_ids[piece];
	const struct TRACKOBJECT *object = &trkObjectList[tile];
	legacy_f64 ox, oz;
	legacy_s16 orientation;
	genghost_route_piece_frame(piece, &ox, &oz, &orientation);
	legacy_f64 ax, az, bx, bz, cx, cz;
	turn_quarter(action->approach.x - ox, action->approach.z - oz, -orientation, &ax, &az);
	turn_quarter(action->exit.x - ox, action->exit.z - oz, -orientation, &bx, &bz);
	turn_quarter(state.playerstate.car_position.lx / 64.0 - ox,
				 state.playerstate.car_position.lz / 64.0 - oz, -orientation, &cx, &cz);
	(void)ax;
	(void)bx;
	(void)cz;
	legacy_f64 half_width = 0.0;
	for (legacy_uint i = 0U; i < 4U; ++i) {
		half_width = fmax(half_width, fabs(simd_player.wheel_coords[i].x / 64.0));
	}
	/* The object continuation flags define its actual rectangular footprint.
	 * The straight alternative stays inside it instead of following the S bend.
	 * One keeps the incoming lateral position; the other enters the middle.
	 * Those are geometric hypotheses, not recorded human coordinates. */
	legacy_f64 world_half_x = object->ss_multiTileFlag & MULTI_TILE_COLUMN_EDGE_FLAG
								  ? (legacy_f64)TRACK_TILE_SIZE
								  : 512.0;
	legacy_f64 world_half_z =
		object->ss_multiTileFlag & MULTI_TILE_ROW_EDGE_FLAG ? (legacy_f64)TRACK_TILE_SIZE : 512.0;
	legacy_f64 local_half_x = ((legacy_uint)orientation & 256U) ? world_half_z : world_half_x;
	legacy_f64 margin = fmax(32.0, half_width + 24.0);
	legacy_f64 x = variant == 1U ? bounded(cx, -local_half_x + margin, local_half_x - margin) : 0.0;
	turn_quarter(x, az, orientation, &action->approach.x, &action->approach.z);
	turn_quarter(x, bz, orientation, &action->exit.x, &action->exit.z);
	action->approach.x += ox;
	action->approach.z += oz;
	action->exit.x += ox;
	action->exit.z += oz;
	genghost_route_target_height(&action->approach);
	genghost_route_target_height(&action->exit);
	action->exit_heading_x = action->exit.x - action->approach.x;
	action->exit_heading_z = action->exit.z - action->approach.z;
	legacy_f64 length = fabs(bz - az);
	if (length > 1.0) {
		action->exit_heading_x /= length;
		action->exit_heading_z /= length;
	}
}

static void add_action(struct GENGHOST_MANEUVER *actions, size_t *count, size_t capacity,
					   const struct GENGHOST_MANEUVER *action)
{
	if (*count < capacity) {
		actions[(*count)++] = *action;
	}
}

static struct GENGHOST_MANEUVER feature_action(const struct GENGHOST_MANEUVER *ordinary,
											   const struct GENGHOST_ROUTE *route,
											   const struct GENGHOST_DRIVER *driver, size_t begin,
											   size_t end)
{
	struct GENGHOST_MANEUVER feature = *ordinary;
	feature.approach = route->points[begin];
	feature.exit = route->points[end];
	feature.feature_begin = feature.approach.distance;
	feature.feature_end = feature.exit.distance;
	feature.physical_model = feature.approach.physical_model;
	feature.approach_speed = feature.exit_speed = 250.0;
	feature.exit_heading_x = feature.exit.x - feature.approach.x;
	feature.exit_heading_z = feature.exit.z - feature.approach.z;
	legacy_f64 length = sqrt(feature.exit_heading_x * feature.exit_heading_x +
							 feature.exit_heading_z * feature.exit_heading_z);
	if (length > 1.0) {
		feature.exit_heading_x /= length;
		feature.exit_heading_z /= length;
	}
	feature.horizon_frames = (legacy_uint)bounded(
		(feature.feature_end - driver->progress + 240.0) /
			fmax(20.0, state.playerstate.car_actual_speed / 256.0 * MANEUVER_MPH_TRAVEL),
		24.0, 64.0);
	feature.gene.line_mode = 0U;
	feature.gene.shortcut_skip = 0U;
	feature.gene.slide_brake = 0.0;
	return feature;
}

/* Keep an actual flight's incoming line rather than steering over to the
 * grounded center corridor while above a stone. No height or takeoff state is
 * invented: the caller proposes this only after native wheel contact is lost.
 * Both collision modes must still validate body clearance and the landing. */
static legacy_int slalom_flight(struct GENGHOST_MANEUVER *action)
{
	legacy_int piece = action->approach.piece;
	/* The combined stunt has a roof: an ordinary slalom's over-stone flight
	 * proposal cannot infer clearance through that shell. */
	if (action->approach.tunnel_slalom) {
		return 0;
	}
	const struct CARSTATE *car = &state.playerstate;
	legacy_f64 ox, oz;
	legacy_s16 orientation;
	genghost_route_piece_frame(piece, &ox, &oz, &orientation);
	legacy_f64 ax, az, bx, bz, cx, cz, dx, dz;
	turn_quarter(action->approach.x - ox, action->approach.z - oz, -orientation, &ax, &az);
	turn_quarter(action->exit.x - ox, action->exit.z - oz, -orientation, &bx, &bz);
	turn_quarter(car->car_position.lx / 64.0 - ox, car->car_position.lz / 64.0 - oz, -orientation,
				 &cx, &cz);
	turn_quarter((car->car_position.lx - (legacy_f64)car->car_previous_position.lx) / 64.0,
				 (car->car_position.lz - (legacy_f64)car->car_previous_position.lz) / 64.0,
				 -orientation, &dx, &dz);
	legacy_f64 direction = bz >= az ? 1.0 : -1.0;
	/* Native body half-length is point one's x coordinate. Point zero holds
	 * width/height, while point one's y coordinate is a collision radius. */
	legacy_f64 rear = fabs((legacy_f64)simd_player.collide_points[1].px);
	for (legacy_uint i = 0U; i < 4U; ++i) {
		rear = fmax(rear, fabs(simd_player.wheel_coords[i].z / 64.0));
	}
	legacy_f64 clear = direction * (SLALOM_POLE_FAR_Z + rear + 80.0);
	if (direction * dz <= 0.25 || direction * (clear - cz) <= 0.0) {
		return 0;
	}
	legacy_f64 x = cx + dx * (clear - cz) / dz;
	action->approach.x = car->car_position.lx / 64.0;
	action->approach.y = car->car_position.ly / 64.0;
	action->approach.z = car->car_position.lz / 64.0;
	turn_quarter(x, clear, orientation, &action->exit.x, &action->exit.z);
	action->exit.x += ox;
	action->exit.z += oz;
	action->exit.height_specified = 0;
	genghost_route_target_height(&action->exit);
	legacy_f64 length = sqrt(dx * dx + dz * dz);
	turn_quarter(dx / length, dz / length, orientation, &action->exit_heading_x,
				 &action->exit_heading_z);
	return 1;
}

size_t genghost_maneuvers(const struct GENGHOST_ROUTE *route, const struct GENGHOST_DRIVER *driver,
						  const struct GENGHOST_POLICY *policy, legacy_int powergear,
						  legacy_int collision, legacy_int owoot, struct GENGHOST_MANEUVER *actions,
						  size_t capacity)
{
	(void)collision; /* The per-tick native-tested catalog chooses the matching recipe. */
	if (!route || !driver || !policy || !actions || !capacity || !route->point_count ||
		driver->waypoint >= route->point_count || driver->segment >= GENGHOST_POLICY_SEGMENTS) {
		return 0U;
	}
	if (capacity > GENGHOST_MANEUVER_CAPACITY) {
		capacity = GENGHOST_MANEUVER_CAPACITY;
	}
	const struct CARSTATE *car = &state.playerstate;
	size_t count = 0U, end = 0U;
	size_t begin = feature_ahead(route, driver, driver->waypoint, &end);
	struct GENGHOST_MANEUVER ordinary;
	memset(&ordinary, 0, sizeof(ordinary));
	ordinary.kind = GENGHOST_MANEUVER_FOLLOW;
	ordinary.horizon_frames = 24U;
	ordinary.gene = policy->segment[driver->segment];
	ordinary.approach = ordinary.exit = route->points[driver->waypoint];
	ordinary.feature_begin = ordinary.feature_end = driver->progress;
	ordinary.physical_model = route->points[driver->waypoint].physical_model;
	ordinary.approach_speed = ordinary.exit_speed = ordinary.approach.speed;
	add_action(actions, &count, capacity, &ordinary);
	struct GENGHOST_MANEUVER action = ordinary;
	action.kind = GENGHOST_MANEUVER_ACCELERATE;
	action.gene.speed_scale *= 1.20;
	action.gene.corner_scale *= 1.12;
	action.gene.slide_brake = 0.0;
	add_action(actions, &count, capacity, &action);
	action = ordinary;
	action.kind = GENGHOST_MANEUVER_BRAKE;
	action.horizon_frames = 12U;
	action.gene.speed_scale *= 0.72;
	action.gene.corner_scale *= 0.78;
	add_action(actions, &count, capacity, &action);
	size_t crest;
	legacy_int ascent = uphill_ahead(route, driver, &crest);
	legacy_int feature_launch =
		begin < route->point_count && can_launch(route->points[begin].physical_model);
	legacy_int climbing =
		car->car_pseudoGravity < -12 && car->car_rotate.y > 12 && car->car_rotate.y < 192;
	if (powergear && genghost_powergear_kind() && (owoot || !feature_launch) &&
		(ascent || climbing || genghost_maneuver_powergear_ready() || (owoot && feature_launch))) {
		/* OWOOT still allows shifting on an actual road slope. These schedules
		 * preserve the road controller and cannot acquire an off-road landing
		 * target. They also cover terrain hills without a catalog stunt model. */
		for (legacy_uint schedule = 0U; schedule < 3U; ++schedule) {
			action = ordinary;
			action.kind = GENGHOST_MANEUVER_POWERGEAR;
			action.horizon_frames = 64U;
			action.shift_mode = schedule;
			action.road_only = 1U;
			action.gene.stunt_mode = 0U;
			action.gene.line_mode = 0U;
			action.gene.shortcut_skip = 0U;
			action.gene.lateral_offset = bounded(action.gene.lateral_offset, -0.6, 0.6);
			action.gene.powergear_bias = 1.0;
			action.gene.shift_rpm_scale = schedule == 0U ? 0.86 : 1.0;
			action.exit = route->points[crest];
			action.feature_end = action.exit.distance;
			add_action(actions, &count, capacity, &action);
		}
	}
	/* Reserve narrow center guidance and a real-flight continuation before
	 * nearby launch/landing variants can fill the bounded proposal array. Look
	 * through the preceding stunt too: DEFAULT's first slalom follows a ramp. */
	size_t slalom_begin = begin, slalom_end = end;
	for (legacy_uint number = 0U; number < 2U && slalom_begin < route->point_count; ++number) {
		if (route->points[slalom_begin].physical_model == PHYSICAL_MODEL_SLALOM &&
			(!owoot || genghost_slalom_center_fits())) {
			action = feature_action(&ordinary, route, driver, slalom_begin, slalom_end);
			action.kind = GENGHOST_MANEUVER_SLALOM_CENTER;
			action.road_only = 1U;
			action.gene.stunt_mode = GENGHOST_STUNT_SLALOM_CENTER;
			action.gene.lateral_offset = 0.0;
			action.gene.speed_scale = 1.5;
			add_action(actions, &count, capacity, &action);
			if (!owoot && car->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE &&
				car->car_actual_speed > 0U && slalom_flight(&action)) {
				action.kind = GENGHOST_MANEUVER_SLALOM_JUMP;
				action.road_only = 0U;
				add_action(actions, &count, capacity, &action);
			}
		}
		slalom_begin = feature_ahead(route, driver, slalom_end + 1U, &slalom_end);
	}
	/* A barrel launch is a directional road followed by real unsupported
	 * flight. Keep its centered approach distinct from the stock corkscrew
	 * bypass/launch recipes, including while OWOOT requires the full roll. */
	size_t roll_begin = begin, roll_end = end;
	for (legacy_uint number = 0U; number < 2U && roll_begin < route->point_count; ++number) {
		if (route->points[roll_begin].roll_ramp) {
			static const legacy_f64 scales[] = {1.0, 0.85, 1.15};
			for (legacy_uint variant = 0U; variant < sizeof(scales) / sizeof(scales[0]);
				 ++variant) {
				action = feature_action(&ordinary, route, driver, roll_begin, roll_end);
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
				add_action(actions, &count, capacity, &action);
			}
		}
		roll_begin = feature_ahead(route, driver, roll_end + 1U, &roll_end);
	}
	if (owoot) {
		return count;
	}
	/* These are native tile-recognition proposals, never mandatory Euclidean
	 * checkpoints. The actual route confirmation state and penalties survive
	 * in the simulator snapshot and decide whether each shortcut pays off. */
	static const legacy_uint skips[] = {0U, 1U, 3U, 6U};
	for (legacy_uint i = 0U; i < sizeof(skips) / sizeof(skips[0]); ++i) {
		action = ordinary;
		action.kind = GENGHOST_MANEUVER_TILE_TOUCH;
		action.gene.line_mode = 1U;
		action.gene.shortcut_skip = skips[i];
		action.gene.stunt_mode = 0U;
		action.gene.slide_brake = 0.0;
		action.gene.lookahead_scale = i > 1U ? 1.4 : 1.0;
		add_action(actions, &count, capacity, &action);
	}
	/* Reserve landing diversity before optional nearby stunt alternatives can
	 * consume the bounded action array. Every airborne expansion retains all
	 * three landing goals with the normal full-capacity caller. */
	if (car->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE && car->car_actual_speed > 0U) {
		for (legacy_uint variant = 0U; variant < 3U; ++variant) {
			action = ordinary;
			action.kind = GENGHOST_MANEUVER_LANDING;
			action.horizon_frames = 32U;
			action.exit = landing_ahead(route, driver->waypoint, 1U + variant * 2U);
			action.exit_speed = 250.0;
			action.gene.powergear_bias = powergear ? 1.0 : 0.0;
			add_action(actions, &count, capacity, &action);
		}
	}
	/* A bypassed piece can still be nearest to the spline. Offer the next
	 * feature too, so a grass approach to a chicane or launch is not hidden
	 * behind a stunt the car is deliberately avoiding. */
	for (legacy_uint feature_number = 0U;
		 feature_number < 2U && begin < route->point_count && count < capacity; ++feature_number) {
		struct GENGHOST_MANEUVER feature = feature_action(&ordinary, route, driver, begin, end);
		if (feature.approach.roll_ramp) {
			begin = feature_ahead(route, driver, end + 1U, &end);
			continue;
		}
		for (legacy_uint variant = 0U; variant < 4U; ++variant) {
			action = feature;
			action.kind =
				variant % 2U ? GENGHOST_MANEUVER_STUNT_ALTERNATE : GENGHOST_MANEUVER_STUNT;
			action.gene.stunt_mode = variant % 2U ? GENGHOST_STUNT_HUMAN_ALT : GENGHOST_STUNT_HUMAN;
			action.gene.lateral_offset = variant < 2U ? 0.0 : 0.6;
			add_action(actions, &count, capacity, &action);
		}
		if (is_chicane(feature.physical_model)) {
			for (legacy_uint variant = 0U; variant < 3U; ++variant) {
				action = feature;
				action.kind = GENGHOST_MANEUVER_CHICANE_GRASS;
				action.gene.stunt_mode = GENGHOST_STUNT_HUMAN;
				action.gene.lateral_offset = 0.0;
				if (variant != 0U) {
					action.hold_throttle = 1U;
					straight_chicane(&action, variant);
				}
				add_action(actions, &count, capacity, &action);
			}
		}
		if (can_launch(feature.physical_model)) {
			for (legacy_uint variant = 0U; variant < 3U; ++variant) {
				action = feature;
				action.kind = GENGHOST_MANEUVER_LAUNCH;
				action.horizon_frames = 64U;
				action.gene.stunt_mode = feature.physical_model >= PHYSICAL_MODEL_ELEVATED_ROAD &&
												 feature.physical_model <= PHYSICAL_MODEL_OVERPASS
											 ? GENGHOST_STUNT_HUMAN_ALT
											 : 4U;
				action.gene.lateral_offset = variant == 0U ? 0.0 : variant == 1U ? -0.5 : 0.5;
				action.gene.shortcut_skip = variant;
				action.gene.speed_scale = 1.5;
				action.gene.powergear_bias = powergear ? 1.0 : 0.0;
				action.exit = landing_ahead(route, end, 1U + variant * 2U);
				add_action(actions, &count, capacity, &action);
			}
			if (powergear && genghost_powergear_kind()) {
				/* The same approach has several deliberate gear schedules. A
				 * temporarily slower high gear must survive until its launch or
				 * uphill state can reveal the real native unsigned-force boost. */
				for (legacy_uint schedule = 0U; schedule < 3U; ++schedule) {
					action = feature;
					action.kind = GENGHOST_MANEUVER_POWERGEAR;
					action.horizon_frames = 64U;
					action.shift_mode = schedule;
					action.gene.powergear_bias = 1.0;
					action.gene.speed_scale = 1.5;
					action.gene.stunt_mode = schedule == 2U ? 4U : 0U;
					action.gene.shift_rpm_scale = schedule == 0U ? 0.86 : 1.0;
					action.exit = landing_ahead(route, end, 2U);
					add_action(actions, &count, capacity, &action);
				}
			}
		}
		begin = feature_ahead(route, driver, end + 1U, &end);
	}

	return count;
}

static legacy_f64 wheelbase(void)
{
	legacy_f64 front =
		((legacy_f64)simd_player.wheel_coords[0].z + simd_player.wheel_coords[1].z) / 2.0;
	legacy_f64 rear =
		((legacy_f64)simd_player.wheel_coords[2].z + simd_player.wheel_coords[3].z) / 2.0;
	return bounded(fabs(front - rear) / 64.0, 20.0, 160.0);
}

static legacy_u8 toward_target(const struct GENGHOST_WAYPOINT *target, legacy_f64 target_speed,
							   legacy_u8 input, struct GENGHOST_DRIVER *driver)
{
	const struct CARSTATE *car = &state.playerstate;
	struct VECTOR delta = {
		(legacy_s16)bounded(target->x - car->car_position.lx / 64.0, -32767.0, 32767.0),
		extended_track_active() && target->height_specified
			? (legacy_s16)bounded(target->y - car->car_position.ly / 64.0, -32767.0, 32767.0)
			: 0,
		(legacy_s16)bounded(target->z - car->car_position.lz / 64.0, -32767.0, 32767.0)};
	struct VECTOR local;
	struct MATRIX *rotation = mat_rot_zxy(car->car_rotate.z, car->car_rotate.y, car->car_rotate.x,
										  MATRIX_ROTATION_ORDER_YXZ);
	mat_mul_vector(&delta, rotation, &local);
	legacy_f64 squared = (legacy_f64)local.x * local.x + (legacy_f64)local.z * local.z;
	legacy_f64 wanted =
		squared > 1.0 ? 4.0 * MANEUVER_ANGLE_SCALE * atan(2.0 * wheelbase() * local.x / squared)
					  : 0.0;
	if (local.z < -30) {
		wanted = local.x >= 0 ? 240.0 : -240.0;
		target_speed = fmin(target_speed, 38.0);
	}
	input &= (legacy_u8) ~(INPUT_STEER_LEFT_FLAG | INPUT_STEER_RIGHT_FLAG | INPUT_PEDAL_MASK);
	input |= genghost_steering_key(bounded(wanted, -240.0, 240.0));
	legacy_f64 speed = car->car_actual_speed / 256.0;
	if (car->car_sumSurfRearWheels == CAR_WHEEL_CONTACT_NONE || speed < target_speed - 0.5) {
		input |= INPUT_ACCELERATE_FLAG;
	} else if (speed > target_speed + 1.5) {
		input |= INPUT_BRAKE_FLAG;
	}
	driver->target_speed = target_speed;
	return input;
}

static legacy_int chicane_target(const struct GENGHOST_ROUTE *route,
								 const struct GENGHOST_DRIVER *driver,
								 const struct GENGHOST_MANEUVER *action,
								 struct GENGHOST_WAYPOINT *target, legacy_f64 *target_speed)
{
	const struct CARSTATE *car = &state.playerstate;
	legacy_f64 dx = action->exit.x - action->approach.x;
	legacy_f64 dz = action->exit.z - action->approach.z;
	legacy_f64 length = sqrt(dx * dx + dz * dz);
	if (length < 1.0 || driver->progress > action->feature_end + 140.0) {
		return 0;
	}
	legacy_f64 along = ((car->car_position.lx / 64.0 - action->approach.x) * dx +
						(car->car_position.lz / 64.0 - action->approach.z) * dz) /
					   length;
	if (along > length + 100.0) {
		return 0;
	}
	legacy_f64 lookahead = bounded(160.0 + car->car_actual_speed / 256.0 * 1.8, 180.0, 700.0);
	legacy_f64 next = bounded(along + lookahead, 0.0, length + 120.0);
	legacy_f64 phase = bounded(next / length, 0.0, 1.0);
	/* Keep entry and exit fixed. Only the chord's interior moves sideways, so
	 * alternatives do not extend the fast-grass assumption to adjacent tiles.
	 * Native track_object_chicane assigns the road's surface unconditionally
	 * across its own sampled footprint; there is no artificial grip boost here. */
	legacy_f64 offset = sin(phase * 3.14159265358979323846) *
						bounded(action->gene.lateral_offset, -1.0, 1.0) * 180.0;
	*target = action->exit;
	target->x = action->approach.x + (next * dx + offset * dz) / length;
	target->z = action->approach.z + (next * dz - offset * dx) / length;
	genghost_route_target_height(target);
	*target_speed = action->approach_speed;
	/* Human fast-grass passes can leave toward another shortcut rather than
	 * the spline's next corner. Retain a full-throttle alternative; the beam
	 * evaluates the following section and native collisions before accepting
	 * it. The ordinary diagonal proposal keeps its braking envelope. */
	if (action->hold_throttle) {
		return 1;
	}
	legacy_f64 braking = fmax(0.15, simd_player.braking_eff / 256.0) * 0.80;
	for (size_t i = driver->waypoint; i < route->point_count; ++i) {
		const struct GENGHOST_WAYPOINT *point = &route->points[i];
		if (point->distance <= action->feature_end) {
			continue;
		}
		legacy_f64 ahead = point->distance - action->feature_end + fmax(0.0, length - along);
		if (ahead > 5000.0) {
			break;
		}
		legacy_f64 limit = point->speed * bounded(action->gene.speed_scale, 0.35, 2.5);
		*target_speed =
			fmin(*target_speed, sqrt(limit * limit + 2.0 * braking / MANEUVER_MPH_TRAVEL *
														 fmax(0.0, ahead - 140.0)));
	}
	return 1;
}

legacy_u8 genghost_maneuver_step(const struct GENGHOST_ROUTE *route,
								 const struct GENGHOST_POLICY *policy,
								 struct GENGHOST_DRIVER *driver,
								 const struct GENGHOST_MANEUVER *action, legacy_uint elapsed,
								 legacy_int powergear, legacy_int collision, legacy_int owoot)
{
	if (!action || action->kind == GENGHOST_MANEUVER_FOLLOW) {
		return genghost_driver_step(route, policy, driver, powergear, collision, owoot);
	}
	/* Macro changes are local to this continuation. In particular, no inherited
	 * segment modifications leak into competing nodes or the legacy engine. */
	struct GENGHOST_POLICY local = *policy;
	for (legacy_uint i = 0U; i < GENGHOST_POLICY_SEGMENTS; ++i) {
		local.segment[i] = action->gene;
		if (action->kind == GENGHOST_MANEUVER_SLALOM_CENTER ||
			action->kind == GENGHOST_MANEUVER_SLALOM_JUMP) {
			/* Parameter refinement retains the exact narrow corridor and cannot
			 * turn a preceding ramp into a generic catalog bypass. */
			local.segment[i].stunt_mode = GENGHOST_STUNT_SLALOM_CENTER;
			local.segment[i].lateral_offset = 0.0;
		}
	}
	legacy_u8 input = genghost_driver_step(route, &local, driver, powergear, collision, owoot);
	if (driver->barrel_active || action->kind == GENGHOST_MANEUVER_BARREL_ROLL ||
		(owoot && action->kind != GENGHOST_MANEUVER_POWERGEAR)) {
		return input;
	}
	const struct CARSTATE *car = &state.playerstate;
	struct GENGHOST_WAYPOINT target;
	legacy_f64 target_speed;
	if (!owoot && action->kind == GENGHOST_MANEUVER_CHICANE_GRASS &&
		chicane_target(route, driver, action, &target, &target_speed)) {
		input = toward_target(&target, target_speed, input, driver);
	} else if (!owoot && action->kind == GENGHOST_MANEUVER_SLALOM_JUMP &&
			   car->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE &&
			   (action->exit.x - car->car_position.lx / 64.0) * action->exit_heading_x +
					   (action->exit.z - car->car_position.lz / 64.0) * action->exit_heading_z >
				   0.0) {
		input = toward_target(&action->exit, action->exit_speed, input, driver);
	} else if (!owoot && !action->road_only &&
			   (action->kind == GENGHOST_MANEUVER_LAUNCH ||
				action->kind == GENGHOST_MANEUVER_POWERGEAR ||
				action->kind == GENGHOST_MANEUVER_LANDING) &&
			   car->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE &&
			   (action->kind == GENGHOST_MANEUVER_LANDING ||
				car->car_position.ly / 64.0 > action->approach.y + 25.0)) {
		/* The approach follows the launch geometry; in flight the goal becomes
		 * a later landing/tile contact. After touchdown the normal route driver
		 * takes over. Exact physics decides whether this flight can reach it. */
		input = toward_target(&action->exit, action->exit_speed, input, driver);
	}
	/* Human loop entries can trigger the unsigned-force boost before top gear.
	 * Retain that useful native state instead of immediately shifting away
	 * from it merely because a higher gear is available. */
	if (powergear && action->kind == GENGHOST_MANEUVER_POWERGEAR &&
		genghost_maneuver_powergear_ready()) {
		input &= (legacy_u8) ~(INPUT_SHIFT_UP_FLAG | INPUT_SHIFT_DOWN_FLAG | INPUT_PEDAL_MASK);
		input |= INPUT_ACCELERATE_FLAG;
		return input;
	}
	if (powergear && action->kind == GENGHOST_MANEUVER_POWERGEAR && action->shift_mode &&
		car->car_transmission == TRANSMISSION_MANUAL && !car->car_changing_gear &&
		car->car_current_gear < simd_player.num_gears) {
		legacy_int unloaded = car->car_sumSurfFrontWheels == CAR_WHEEL_CONTACT_NONE ||
							  car->car_sumSurfRearWheels == CAR_WHEEL_CONTACT_NONE;
		legacy_int climbing =
			car->car_pseudoGravity < -12 && car->car_rotate.y > 12 && car->car_rotate.y < 192;
		if (unloaded || (climbing && (action->shift_mode == 1U || elapsed >= 8U))) {
			input &= (legacy_u8)~INPUT_SHIFT_DOWN_FLAG;
			input |= INPUT_SHIFT_UP_FLAG;
		}
	}
	return input;
}
