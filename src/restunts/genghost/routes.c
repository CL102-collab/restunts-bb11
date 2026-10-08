#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "routes.h"
#include "../c/externs.h"
#include "../c/game_input.h"
#include "../c/track_objects.h"
#include "../c/trackdata_layout.h"

#undef memcpy

/* restunts_options puts the legacy c/math.h ahead of the host's <math.h>.
 * These ISO C declarations keep this tool independent of compiler-specific
 * include_next paths while using the platform math library on both targets. */
extern double fabs(double);
extern double sqrt(double);
extern double atan(double);
extern double fmin(double, double);
extern double fmax(double, double);

#define GENGHOST_ROUTE_MEMORY_LIMIT ((size_t)512U * 1024U * 1024U)
#define GENGHOST_ANGLE_SCALE (1024.0 / 6.28318530717958647693)
/* Native physics: speed*1408/15360 fixed-position units, divided by 64. */
#define GENGHOST_MPH_TRAVEL (11.0 / 30.0)

static double bounded(double value, double minimum, double maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

static void route_error(char *error, size_t error_size, const char *message)
{
	if (error != NULL && error_size != 0U) {
		(void)snprintf(error, error_size, "%s", message);
	}
}

static double wheelbase(void)
{
	double front = ((double)simd_player.wheel_coords[0].z + simd_player.wheel_coords[1].z) / 2.0;
	double rear = ((double)simd_player.wheel_coords[2].z + simd_player.wheel_coords[3].z) / 2.0;
	return bounded(fabs(front - rear) / 64.0, 20.0, 160.0);
}

static int stunt_model(int model)
{
	return model == PHYSICAL_MODEL_LOOP || model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A ||
		   model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B ||
		   model == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT;
}

static double point_curvature(const struct GENGHOST_WAYPOINT *a, const struct GENGHOST_WAYPOINT *b,
							  const struct GENGHOST_WAYPOINT *c)
{
	double abx = b->x - a->x, abz = b->z - a->z;
	double bcx = c->x - b->x, bcz = c->z - b->z;
	double acx = c->x - a->x, acz = c->z - a->z;
	double product =
		sqrt((abx * abx + abz * abz) * (bcx * bcx + bcz * bcz) * (acx * acx + acz * acz));
	return product > 1.0 ? 2.0 * fabs(abx * bcz - abz * bcx) / product : 0.0;
}

static void route_speed_limits(struct GENGHOST_ROUTE *route)
{
	double axle_length = wheelbase();
	for (size_t i = 0; i < route->point_count; i++) {
		struct GENGHOST_WAYPOINT *point = &route->points[i];
		unsigned int tile = (unsigned char)track_route_element_ids[point->piece];
		int surface = trkObjectList[tile].ss_surfaceType;
		/* Resource surface codes are zero-based, CARSTATE contact codes are
		 * one-based. A negative model surface is an alternating paved piece. */
		if (surface < 0 || surface > 2) {
			surface = 0;
		}
		double grip = (double)simd_player.grip * 2.0 * simd_player.surface_grip[surface] / 256.0;
		if (grip < 1.0) {
			grip = 1200.0;
		}
		point->speed = 250.0;
		if (stunt_model(point->physical_model)) {
			/* Loop/corkscrew projection doubles back in x/z. Treating it as
			 * a hairpin would brake until the car falls off the ceiling. */
			point->speed =
				point->physical_model == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT ? 105.0 : 125.0;
		} else if (i > 0U && i + 1U < route->point_count &&
				   !stunt_model(route->points[i - 1U].physical_model) &&
				   !stunt_model(route->points[i + 1U].physical_model)) {
			double curvature =
				point_curvature(&route->points[i - 1U], point, &route->points[i + 1U]);
			double angle = 4.0 * atan(axle_length * curvature) * GENGHOST_ANGLE_SCALE;
			if (angle > 2.0) {
				point->speed = bounded(sqrt(512.0 * grip / (angle + 8.0)) * 0.88, 30.0, 250.0);
			}
		}
		if (point->physical_model == PHYSICAL_MODEL_ELEVATED_CORNER) {
			point->speed = fmin(point->speed, 78.0);
		}
		if (point->physical_model == PHYSICAL_MODEL_SLALOM) {
			point->speed = fmin(point->speed, 65.0);
		}
	}
}

static int append_route(struct GENGHOST_ROUTES *set, size_t *capacity, const int *path,
						size_t depth, size_t *bytes, char *error, size_t error_size)
{
	size_t point_count = 0U;
	for (size_t i = 0; i <= depth; i++) {
		int piece = i == depth ? 0 : path[i];
		unsigned int tile = (unsigned char)track_route_element_ids[piece];
		unsigned int subtype = (unsigned char)track_route_traversal_flags[piece] & 15U;
		const struct TRKOBJINFO *info = trkObjectList[tile].ss_trkObjInfoPtr;
		if (info == NULL || info[subtype].route_point_count <= 0) {
			route_error(error, error_size,
						"Track route contains a piece without usable driving geometry.");
			return -1;
		}
		point_count += (unsigned char)info[subtype].route_point_count;
	}
	size_t required = (depth + 1U) * sizeof(int) + point_count * sizeof(struct GENGHOST_WAYPOINT);
	if (required > GENGHOST_ROUTE_MEMORY_LIMIT || *bytes > GENGHOST_ROUTE_MEMORY_LIMIT - required) {
		route_error(error, error_size,
					"Enumerating every route exceeds the 512 MiB route-memory limit; no "
					"combinations were silently discarded.");
		return -1;
	}
	if (set->count == *capacity) {
		size_t next = *capacity == 0U ? 8U : *capacity * 2U;
		if (next > GENGHOST_ROUTE_MEMORY_LIMIT / sizeof(*set->routes)) {
			route_error(error, error_size, "Too many route combinations to represent safely.");
			return -1;
		}
		void *allocation = realloc(set->routes, next * sizeof(*set->routes));
		if (allocation == NULL) {
			route_error(error, error_size,
						"Out of memory while enumerating all route combinations.");
			return -1;
		}
		set->routes = allocation;
		*capacity = next;
	}
	struct GENGHOST_ROUTE route;
	memset(&route, 0, sizeof(route));
	route.pieces = malloc((depth + 1U) * sizeof(*route.pieces));
	route.points = calloc(point_count, sizeof(*route.points));
	if (route.pieces == NULL || route.points == NULL) {
		free(route.pieces);
		free(route.points);
		route_error(error, error_size, "Out of memory while expanding route driving geometry.");
		return -1;
	}
	memcpy(route.pieces, path, depth * sizeof(*path));
	route.pieces[depth] = 0;
	route.piece_count = depth + 1U;
	for (size_t i = 0; i < route.piece_count; i++) {
		int piece = route.pieces[i];
		unsigned int tile = (unsigned char)track_route_element_ids[piece];
		unsigned int subtype = (unsigned char)track_route_traversal_flags[piece] & 15U;
		const struct TRKOBJINFO *info = &trkObjectList[tile].ss_trkObjInfoPtr[subtype];
		for (unsigned int j = 0; j < (unsigned char)info->route_point_count; j++) {
			/* The native API writes center, two edges, and a word at byte 18. */
			struct VECTOR geometry[ROUTE_GEOMETRY_POINT_COUNT];
			(void)get_track_route_point((legacy_s16)piece, geometry, (legacy_s16)j, NULL);
			struct GENGHOST_WAYPOINT point;
			memset(&point, 0, sizeof(point));
			point.x = geometry[0].x;
			point.y = geometry[0].y;
			point.z = geometry[0].z;
			point.height_specified = geometry[0].y != ROUTE_POINT_HEIGHT_UNSPECIFIED;
			point.edge_x = ((double)geometry[2].x - geometry[1].x) * 0.5;
			point.edge_y =
				point.height_specified ? ((double)geometry[2].y - geometry[1].y) * 0.5 : 0.0;
			point.edge_z = ((double)geometry[2].z - geometry[1].z) * 0.5;
			point.piece = piece;
			point.physical_model = trkObjectList[tile].ss_physicalModel;
			if (route.point_count != 0U) {
				const struct GENGHOST_WAYPOINT *previous = &route.points[route.point_count - 1U];
				double dx = point.x - previous->x, dz = point.z - previous->z;
				double dy = point.height_specified && previous->height_specified
								? point.y - previous->y
								: 0.0;
				double distance = sqrt(dx * dx + dy * dy + dz * dz);
				/* Adjacent pieces can share an endpoint. Keeping a zero-length
				 * segment only makes projection and curvature ambiguous. */
				if (distance < 0.01) {
					continue;
				}
				route.length += distance;
			}
			point.distance = route.length;
			route.points[route.point_count++] = point;
		}
	}
	if (route.point_count < 2U || route.length < 1.0) {
		free(route.pieces);
		free(route.points);
		route_error(error, error_size, "Track route is too short to drive.");
		return -1;
	}
	route_speed_limits(&route);
	set->routes[set->count++] = route;
	*bytes += required;
	return 0;
}

void genghost_routes_free(struct GENGHOST_ROUTES *set)
{
	if (set == NULL) {
		return;
	}
	for (size_t i = 0; i < set->count; i++) {
		free(set->routes[i].pieces);
		free(set->routes[i].points);
	}
	free(set->routes);
	memset(set, 0, sizeof(*set));
}

int genghost_routes_build(struct GENGHOST_ROUTES *set, char *error, size_t error_size)
{
	memset(set, 0, sizeof(*set));
	route_error(error, error_size, "");
	if (track_pieces_counter <= 0 || track_pieces_counter > 900) {
		route_error(error, error_size, "Track setup did not produce a valid route graph.");
		return -1;
	}
	size_t nodes = (size_t)track_pieces_counter;
	int *path = calloc(nodes + 1U, sizeof(*path));
	unsigned char *choices = calloc(nodes + 1U, 1U);
	unsigned char *visited = calloc(nodes, 1U);
	if (path == NULL || choices == NULL || visited == NULL) {
		free(path);
		free(choices);
		free(visited);
		route_error(error, error_size, "Out of memory while creating route traversal.");
		return -1;
	}
	for (size_t i = 0; i < nodes; i++) {
		int primary = track_primary_route_links[i];
		int alternate = track_alternate_route_links[i];
		if (primary < -1 || primary >= (int)nodes || alternate < -1 || alternate >= (int)nodes) {
			free(path);
			free(choices);
			free(visited);
			route_error(error, error_size, "Track setup produced an out-of-range route link.");
			return -1;
		}
		if (alternate >= 0 && alternate != primary) {
			set->branch_count++;
		}
	}
	size_t depth = 1U, capacity = 0U, bytes = 0U;
	visited[0] = 1U;
	int result = 0;
	/* An iterative path-local DFS permits branch merges while preventing an
	 * arbitrary number of laps around internal cycles. Opposite traversals of
	 * one physical tile retain their distinct native graph node identities. */
	while (depth != 0U) {
		int node = path[depth - 1U];
		unsigned int choice = choices[depth - 1U]++;
		if (choice >= 2U) {
			visited[node] = 0U;
			depth--;
			continue;
		}
		int next =
			choice == 0U ? track_primary_route_links[node] : track_alternate_route_links[node];
		if (next < 0 || (choice == 1U && next == track_primary_route_links[node])) {
			continue;
		}
		if (next == 0) {
			if (append_route(set, &capacity, path, depth, &bytes, error, error_size) != 0) {
				result = -1;
				break;
			}
		} else if (!visited[next]) {
			path[depth] = next;
			choices[depth] = 0U;
			visited[next] = 1U;
			depth++;
		}
	}
	free(path);
	free(choices);
	free(visited);
	if (result == 0 && set->count == 0U) {
		route_error(error, error_size, "No start-to-finish route exists in the track graph.");
		result = -1;
	}
	if (result != 0) {
		genghost_routes_free(set);
	}
	return result;
}

void genghost_policy_default(struct GENGHOST_POLICY *policy)
{
	for (size_t i = 0; i < GENGHOST_POLICY_SEGMENTS; i++) {
		policy->segment[i].speed_scale = 1.0;
		policy->segment[i].lateral_offset = 0.0;
		policy->segment[i].lookahead_scale = 1.0;
		policy->segment[i].shift_rpm_scale = 1.0;
		policy->segment[i].shortcut_skip = 0U;
		policy->segment[i].powergear_bias = 0.5;
	}
}

void genghost_driver_reset(struct GENGHOST_DRIVER *driver)
{
	memset(driver, 0, sizeof(*driver));
}

static unsigned int policy_segment(const struct GENGHOST_ROUTE *route, double distance)
{
	unsigned int index =
		(unsigned int)(bounded(distance / route->length, 0.0, 1.0) * GENGHOST_POLICY_SEGMENTS);
	return index < GENGHOST_POLICY_SEGMENTS ? index : GENGHOST_POLICY_SEGMENTS - 1U;
}

void genghost_driver_observe(const struct GENGHOST_ROUTE *route, struct GENGHOST_DRIVER *driver)
{
	if (route->point_count < 2U) {
		return;
	}
	const struct CARSTATE *car = &state.playerstate;
	double x = car->car_position.lx / 64.0;
	double y = car->car_position.ly / 64.0;
	double z = car->car_position.lz / 64.0;
	double best = DBL_MAX, best_progress = driver->progress, best_distance = DBL_MAX;
	size_t first = driver->waypoint > 2U ? driver->waypoint - 2U : 0U;
	size_t best_index = driver->waypoint;
	for (size_t i = first; i + 1U < route->point_count; i++) {
		const struct GENGHOST_WAYPOINT *a = &route->points[i], *b = &route->points[i + 1U];
		if (a->distance > driver->progress + 6144.0) {
			break;
		}
		double dx = b->x - a->x, dz = b->z - a->z;
		int use_height = a->height_specified && b->height_specified;
		double dy = use_height ? b->y - a->y : 0.0;
		double ry = use_height ? y - a->y : 0.0;
		double length_squared = dx * dx + dy * dy + dz * dz;
		double fraction =
			length_squared > 0.0
				? bounded(((x - a->x) * dx + ry * dy + (z - a->z) * dz) / length_squared, 0.0, 1.0)
				: 0.0;
		double px = a->x + fraction * dx, pz = a->z + fraction * dz;
		double py = use_height ? a->y + fraction * dy : y;
		double distance_squared = (x - px) * (x - px) + (z - pz) * (z - pz) + (y - py) * (y - py);
		double progress = a->distance + fraction * (b->distance - a->distance);
		double advance = fmax(0.0, progress - driver->progress - 150.0);
		double score = distance_squared + advance * advance * 0.018;
		if (progress + 500.0 < driver->progress) {
			score += 1000000.0;
		}
		if (score < best) {
			best = score;
			best_progress = progress;
			best_distance = distance_squared;
			best_index = i;
		}
	}
	if (best != DBL_MAX) {
		driver->progress = fmax(driver->progress, best_progress);
		driver->waypoint = best_index;
		driver->off_route = sqrt(best_distance);
		driver->segment = policy_segment(route, driver->progress);
	}
}

static void point_at(const struct GENGHOST_ROUTE *route, double distance, double offset,
					 struct GENGHOST_WAYPOINT *point)
{
	size_t low = 0U, high = route->point_count - 1U;
	while (low + 1U < high) {
		size_t middle = low + (high - low) / 2U;
		if (route->points[middle].distance < distance) {
			low = middle;
		} else {
			high = middle;
		}
	}
	const struct GENGHOST_WAYPOINT *a = &route->points[low], *b = &route->points[high];
	double fraction = bounded((distance - a->distance) / (b->distance - a->distance), 0.0, 1.0);
	*point = *a;
	point->x =
		a->x + fraction * (b->x - a->x) + offset * (a->edge_x + fraction * (b->edge_x - a->edge_x));
	point->z =
		a->z + fraction * (b->z - a->z) + offset * (a->edge_z + fraction * (b->edge_z - a->edge_z));
	point->height_specified = a->height_specified && b->height_specified;
	if (point->height_specified) {
		point->y = a->y + fraction * (b->y - a->y) +
				   offset * (a->edge_y + fraction * (b->edge_y - a->edge_y));
	}
}

int genghost_powergear_kind(void)
{
	int mass = simd_player.car_mass;
	int gears = simd_player.num_gears;
	if (mass <= 0 || gears <= 0 || gears >= (int)SIMD_GEAR_RATIO_COUNT) {
		return 0;
	}
	int residue = 65536 % mass;
	if (residue == 0) {
		return 0;
	}
	if (6 * residue < mass) {
		return 1;
	}
	if (2 * residue < mass && simd_player.gear_ratios[gears] != 0U &&
		256.0 * simd_player.max_rpm / simd_player.gear_ratios[gears] >= 225.0) {
		return 2;
	}
	return 0;
}

static unsigned char steering_key(double wanted)
{
	const struct CARSTATE *car = &state.playerstate;
	unsigned int speed_index = (car->car_actual_speed >> 10U) & 252U;
	double best = DBL_MAX;
	unsigned char key = 0U;
	for (unsigned int choice = 0U; choice < 3U; choice++) {
		int response = steerWhlRespTable_ptr[speed_index + choice];
		int angle = car->car_steeringAngle;
		if ((response > 0 && angle < -1) || (response < 0 && angle > 1)) {
			response *= 4;
		}
		if (response == 0 && car->car_actual_speed != 0U && angle != 0) {
			int maximum = steerWhlRespTable_ptr[speed_index + 1U] * 2;
			response = angle < 0 ? (int)fmin(-angle, maximum) : -(int)fmin(angle, maximum);
		}
		double predicted = bounded(angle + bounded(response, -80.0, 80.0), -240.0, 240.0);
		double cost = fabs(predicted - wanted);
		if (cost < best) {
			best = cost;
			key = choice == 1U ? INPUT_STEER_RIGHT_FLAG : choice == 2U ? INPUT_STEER_LEFT_FLAG : 0U;
		}
	}
	return key;
}

unsigned char genghost_driver_step(const struct GENGHOST_ROUTE *route,
								   const struct GENGHOST_POLICY *policy,
								   struct GENGHOST_DRIVER *driver, int powergear_enabled, int owoot)
{
	genghost_driver_observe(route, driver);
	driver->ticks++;
	const struct CARSTATE *car = &state.playerstate;
	const struct GENGHOST_GENE *gene = &policy->segment[driver->segment];
	double speed = car->car_actual_speed / 256.0;
	double lookahead =
		bounded((160.0 + speed * (owoot ? 1.3 : 1.8)) * gene->lookahead_scale, 100.0, 1500.0);
	int model = route->points[driver->waypoint].physical_model;
	if (stunt_model(model)) {
		lookahead = fmin(lookahead, 260.0);
	}
	double target_distance = driver->progress + lookahead;
	if (!owoot && gene->shortcut_skip != 0U) {
		unsigned int remaining = gene->shortcut_skip;
		int piece = route->points[driver->waypoint].piece;
		for (size_t i = driver->waypoint + 1U; i < route->point_count; i++) {
			if (route->points[i].piece != piece) {
				piece = route->points[i].piece;
				if (remaining-- == 0U) {
					target_distance = fmax(target_distance, route->points[i].distance + lookahead);
					break;
				}
			}
		}
	}
	struct GENGHOST_WAYPOINT target;
	point_at(route, target_distance,
			 bounded(gene->lateral_offset, owoot ? -0.8 : -5.0, owoot ? 0.8 : 5.0), &target);
	double world_x = target.x - car->car_position.lx / 64.0;
	double world_z = target.z - car->car_position.lz / 64.0;
	double world_y = target.height_specified ? target.y - car->car_position.ly / 64.0 : 0.0;
	struct VECTOR delta = {(legacy_s16)bounded(world_x, -32767.0, 32767.0),
						   (legacy_s16)bounded(world_y, -32767.0, 32767.0),
						   (legacy_s16)bounded(world_z, -32767.0, 32767.0)};
	struct VECTOR local;
	struct MATRIX *rotation = mat_rot_zxy(car->car_rotate.z, car->car_rotate.y, car->car_rotate.x,
										  MATRIX_ROTATION_ORDER_YXZ);
	mat_mul_vector(&delta, rotation, &local);
	double distance_squared = (double)local.x * local.x + (double)local.z * local.z;
	double wanted =
		distance_squared > 1.0
			? 4.0 * GENGHOST_ANGLE_SCALE * atan(2.0 * wheelbase() * local.x / distance_squared)
			: 0.0;
	if (local.z < -30) {
		wanted = local.x >= 0 ? 240.0 : -240.0;
	}
	wanted = bounded(wanted, -240.0, 240.0);
	unsigned char input = steering_key(wanted);

	double target_speed = 250.0;
	/* Integrating dv/dframe against native distance/frame yields this exact
	 * units conversion for a conservative braking envelope. Aero drag only
	 * improves braking, so it need not be guessed here. */
	double braking = fmax(0.15, simd_player.braking_eff / 256.0) * 0.80;
	double braking_coefficient = 2.0 * braking / GENGHOST_MPH_TRAVEL;
	int useful_pg = powergear_enabled ? genghost_powergear_kind() : 0;
	int pg_feature_ahead = 0;
	for (size_t i = driver->waypoint; i < route->point_count; i++) {
		const struct GENGHOST_WAYPOINT *point = &route->points[i];
		double ahead = point->distance - driver->progress;
		if (ahead > 6000.0) {
			break;
		}
		if (ahead < -200.0) {
			continue;
		}
		const struct GENGHOST_GENE *future =
			&policy->segment[policy_segment(route, point->distance)];
		double limit = point->speed * bounded(future->speed_scale, 0.35, 2.5);
		/* Keep the first feasible seed fast enough to cross upside-down parts;
		 * later mutations can still explore much faster entries and launches. */
		if (stunt_model(point->physical_model)) {
			limit = fmax(
				limit, point->physical_model == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT ? 70.0 : 90.0);
		}
		if (useful_pg && stunt_model(point->physical_model) && future->powergear_bias > 0.65) {
			/* A normal stunt speed cap would immediately brake away a newly
			 * triggered power gear. Aggressive PG policies test carrying the
			 * boost through the obstacle; native collision checks decide if
			 * the resulting launch or line is actually survivable. */
			limit = fmax(limit,
						 125.0 + 125.0 * bounded((future->powergear_bias - 0.65) / 0.35, 0.0, 1.0));
		}
		double available = fmax(0.0, ahead - 140.0);
		double allowed = sqrt(limit * limit + braking_coefficient * available);
		target_speed = fmin(target_speed, allowed);
		if (ahead < 1600.0 &&
			(stunt_model(point->physical_model) || point->physical_model == PHYSICAL_MODEL_RAMP ||
			 point->physical_model == PHYSICAL_MODEL_SOLID_RAMP)) {
			pg_feature_ahead = 1;
		}
	}
	if (local.z < -30 || driver->off_route > 500.0) {
		target_speed = fmin(target_speed, 38.0);
	}
	if (car->car_slidingFlag && fabs(wanted) > 60.0) {
		target_speed = fmin(target_speed, fmax(35.0, speed - 2.0));
	}
	driver->target_speed = target_speed;
	if (speed > target_speed + 1.5) {
		input |= INPUT_BRAKE_FLAG;
	} else if (speed < target_speed - 0.5 || car->car_sumSurfRearWheels == CAR_WHEEL_CONTACT_NONE) {
		input |= INPUT_ACCELERATE_FLAG;
	}

	if (car->car_transmission == TRANSMISSION_MANUAL && !car->car_changing_gear) {
		double shift_rpm = bounded(simd_player.upshift_rpm * gene->shift_rpm_scale,
								   simd_player.idle_rpm + 1200.0, simd_player.max_rpm - 100.0);
		int seek_pg = useful_pg && gene->powergear_bias > 0.05 && pg_feature_ahead &&
					  speed > bounded(85.0 - 65.0 * gene->powergear_bias, 20.0, 90.0);
		if (car->car_current_gear < simd_player.num_gears &&
			(car->car_currpm >= shift_rpm || seek_pg)) {
			input |= INPUT_SHIFT_UP_FLAG;
		} else if (!seek_pg && car->car_current_gear > 1 &&
				   car->car_currpm < simd_player.downshift_rpm &&
				   car->car_currpm * (double)simd_player.gear_ratios[car->car_current_gear - 1] /
						   fmax(1.0, simd_player.gear_ratios[car->car_current_gear]) <
					   shift_rpm - 300.0) {
			input |= INPUT_SHIFT_DOWN_FLAG;
		}
	}
	return input;
}
