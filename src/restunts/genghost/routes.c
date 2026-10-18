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
#include "../c/track_collision.h"

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
						size_t depth, size_t *bytes, const unsigned char *portals, char *error,
						size_t error_size)
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
		point_count += portals && portals[i] ? 1U : (unsigned char)info[subtype].route_point_count;
	}
	size_t required =
		(depth + 1U) * (sizeof(int) + 1U) + point_count * sizeof(struct GENGHOST_WAYPOINT);
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
	if (portals) {
		route.portals = malloc(depth + 1U);
		if (route.portals) {
			memcpy(route.portals, portals, depth + 1U);
		}
	}
	if (route.pieces == NULL || route.points == NULL || (portals && !route.portals)) {
		free(route.pieces);
		free(route.points);
		free(route.portals);
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
		for (unsigned int j = 0;
			 j < (portals && portals[i] ? 1U : (unsigned char)info->route_point_count); j++) {
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
			point.piece_index = i;
			point.physical_model = trkObjectList[tile].ss_physicalModel;
			if (portals && portals[i]) {
				unsigned int flags = (unsigned char)trkObjectList[tile].ss_multiTileFlag;
				point.x = ((unsigned char)track_route_columns[piece] + 0.5 + !!(flags & 2U) * 0.5) *
						  1024.0;
				point.z =
					(29.5 - (unsigned char)track_route_rows[piece] - !!(flags & 1U) * 0.5) * 1024.0;
				point.height_specified = 0;
				point.edge_x = point.edge_y = point.edge_z = 0.0;
				point.physical_model = PHYSICAL_MODEL_ROAD;
			}
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
		free(route.portals);
		route_error(error, error_size, "Track route is too short to drive.");
		return -1;
	}
	route_speed_limits(&route);
	set->routes[set->count++] = route;
	*bytes += required;
	return 0;
}

/* A switch keeps a genuine prefix on one branch and joins a genuine suffix on
 * the other. Graph nodes alone do not establish that either path was touched:
 * split and merge tiles often have different node IDs for the same footprint. */
struct GENGHOST_DUALWAY {
	size_t split, merge, other, other_split, other_merge;
	unsigned short *sources, *targets;
	size_t source_count, target_count;
	uint64_t pairs, shortest, nearest;
	double shortest_cost, nearest_cost;
};

static int piece_overlap(int a, int b)
{
	unsigned int at = (unsigned char)track_route_element_ids[a];
	unsigned int bt = (unsigned char)track_route_element_ids[b];
	unsigned int af = (unsigned char)trkObjectList[at].ss_multiTileFlag;
	unsigned int bf = (unsigned char)trkObjectList[bt].ss_multiTileFlag;
	int ax = (unsigned char)track_route_columns[a], az = (unsigned char)track_route_rows[a];
	int bx = (unsigned char)track_route_columns[b], bz = (unsigned char)track_route_rows[b];
	return ax <= bx + !!(bf & 2U) && bx <= ax + !!(af & 2U) && az <= bz + !!(bf & 1U) &&
		   bz <= az + !!(af & 1U);
}

static double piece_distance(int a, int b)
{
	unsigned int at = (unsigned char)track_route_element_ids[a];
	unsigned int bt = (unsigned char)track_route_element_ids[b];
	unsigned int af = (unsigned char)trkObjectList[at].ss_multiTileFlag;
	unsigned int bf = (unsigned char)trkObjectList[bt].ss_multiTileFlag;
	double dx =
		((unsigned char)track_route_columns[a] - (unsigned char)track_route_columns[b]) * 1024.0 +
		((int)!!(af & 2U) - (int)!!(bf & 2U)) * 512.0;
	double dz = ((unsigned char)track_route_rows[a] - (unsigned char)track_route_rows[b]) * 1024.0 +
				((int)!!(af & 1U) - (int)!!(bf & 1U)) * 512.0;
	return sqrt(dx * dx + dz * dz);
}

/* Use a separate draw's bits for choosing a recipe. Filtering the selector
 * itself would make some source/target pairs unreachable after modulo mapping. */
uint64_t genghost_dualway_selector(uint32_t gate, uint64_t selector)
{
	return (gate >> 2U) % 4U == 0U ? selector % 3U : selector;
}

double genghost_route_piece_distance(const struct GENGHOST_ROUTE *route, size_t piece)
{
	for (size_t i = 0; i < route->point_count; ++i) {
		if (route->points[i].piece_index == piece) {
			return route->points[i].distance;
		}
	}
	return route->length;
}

static int dualway_build(struct GENGHOST_ROUTES *set, size_t *bytes, char *error, size_t error_size)
{
	for (size_t a = 0; a < set->count; ++a) {
		struct GENGHOST_ROUTE *base = &set->routes[a];
		for (size_t b = 0; b < set->count; ++b) {
			if (a == b) {
				continue;
			}
			const struct GENGHOST_ROUTE *other = &set->routes[b];
			int positions[900];
			for (size_t i = 0; i < 900U; ++i) {
				positions[i] = -1;
			}
			for (size_t i = 0; i + 1U < other->piece_count; ++i) {
				positions[other->pieces[i]] = (int)i;
			}
			for (size_t split = 0; split + 2U < base->piece_count; ++split) {
				int paired = positions[base->pieces[split]];
				if (paired < 0 || base->pieces[split + 1U] == other->pieces[paired + 1U]) {
					continue;
				}
				size_t merge = split + 2U, other_merge = 0U;
				for (; merge < base->piece_count; ++merge) {
					int found = base->pieces[merge] == 0 ? (int)other->piece_count - 1
														 : positions[base->pieces[merge]];
					if (found > paired + 1) {
						other_merge = (size_t)found;
						break;
					}
				}
				if (merge >= base->piece_count) {
					continue;
				}
				/* Routes that differ only after this merge describe the same switch. */
				int duplicate = 0;
				for (size_t i = 0; i < base->dualway_count; ++i) {
					const struct GENGHOST_DUALWAY *old = &base->dualways[i];
					if (old->split == split && old->merge == merge &&
						old->other_merge - old->other_split == other_merge - (size_t)paired &&
						!memcmp(set->routes[old->other].pieces + old->other_split,
								other->pieces + paired,
								(other_merge - (size_t)paired) * sizeof(int))) {
						duplicate = 1;
						break;
					}
				}
				if (duplicate) {
					continue;
				}
				unsigned short sources[900], targets[900];
				size_t ns = 0U, nt = 0U;
				for (size_t i = split + 1U; i < merge; ++i) {
					int shared = 0;
					for (size_t j = (size_t)paired + 1U; j < other_merge; ++j) {
						if (piece_overlap(base->pieces[i], other->pieces[j])) {
							shared = 1;
							break;
						}
					}
					if (!shared) {
						sources[ns++] = (unsigned short)i;
					}
				}
				for (size_t j = (size_t)paired + 1U; j < other_merge; ++j) {
					int shared = 0;
					for (size_t i = split + 1U; i < merge; ++i) {
						if (piece_overlap(base->pieces[i], other->pieces[j])) {
							shared = 1;
							break;
						}
					}
					if (!shared) {
						targets[nt++] = (unsigned short)j;
					}
				}
				if (!ns || !nt) {
					continue;
				}
				size_t required =
					sizeof(struct GENGHOST_DUALWAY) + (ns + nt) * sizeof(unsigned short);
				if (*bytes > GENGHOST_ROUTE_MEMORY_LIMIT - required) {
					route_error(error, error_size,
								"Dual-way alternatives exceed the 512 MiB route-memory limit.");
					return -1;
				}
				void *grown =
					realloc(base->dualways, (base->dualway_count + 1U) * sizeof(*base->dualways));
				unsigned short *indices = malloc((ns + nt) * sizeof(*indices));
				if (grown) {
					base->dualways = grown;
				}
				if (!grown || !indices) {
					free(indices);
					route_error(error, error_size, "Out of memory building dual-way alternatives.");
					return -1;
				}
				struct GENGHOST_DUALWAY *span = &base->dualways[base->dualway_count++];
				memset(span, 0, sizeof(*span));
				span->split = split;
				span->merge = merge;
				span->other = b;
				span->other_split = (size_t)paired;
				span->other_merge = other_merge;
				span->sources = indices;
				span->targets = indices + ns;
				memcpy(span->sources, sources, ns * sizeof(*sources));
				memcpy(span->targets, targets, nt * sizeof(*targets));
				span->source_count = ns;
				span->target_count = nt;
				span->pairs = (uint64_t)ns * nt;
				span->shortest_cost = span->nearest_cost = DBL_MAX;
				double base_merge = genghost_route_piece_distance(base, merge),
					   other_merge_distance = genghost_route_piece_distance(other, other_merge);
				double target_progress[900];
				for (size_t j = 0; j < nt; ++j) {
					target_progress[j] = genghost_route_piece_distance(other, targets[j]);
				}
				for (size_t i = 0; i < ns; ++i) {
					double prefix = genghost_route_piece_distance(base, sources[i]);
					for (size_t j = 0; j < nt; ++j) {
						double crossing =
							piece_distance(base->pieces[sources[i]], other->pieces[targets[j]]);
						double cost = prefix + crossing + other_merge_distance -
									  target_progress[j] - base_merge;
						uint64_t pair = (uint64_t)i * nt + j;
						if (cost < span->shortest_cost) {
							span->shortest_cost = cost;
							span->shortest = pair;
						}
						/* Prefer short grass crossings that save distance; the full engine
						 * still judges speed, terrain, collisions, and deferred penalties. */
						double nearest_cost = crossing + fmax(0.0, cost) * 10.0;
						if (nearest_cost < span->nearest_cost) {
							span->nearest_cost = nearest_cost;
							span->nearest = pair;
						}
					}
				}
				*bytes += required;
				if (UINT64_MAX - set->dualway_pairs < span->pairs) {
					route_error(error, error_size, "Too many dual-way pairs to index.");
					return -1;
				}
				set->dualway_pairs += span->pairs;
			}
		}
	}
	return 0;
}

/* Humans often spend their speed budget on the long cross-country connector:
 * a few private departure tiles establish the chosen path, and a late private
 * arrival avoids the remaining branch. These are initial proposals only; every
 * source/target pair remains available to mutation and native simulation. */
void genghost_dualway_seed_policy(const struct GENGHOST_ROUTES *set, size_t index,
								  struct GENGHOST_POLICY *policy, unsigned int seed)
{
	if (seed < 2U || seed >= 6U) {
		return;
	}
	static const unsigned int departures[4] = {0U, 1U, 2U, 2U};
	static const unsigned int arrivals[4] = {2U, 1U, 1U, 0U};
	const struct GENGHOST_ROUTE *base = &set->routes[index];
	for (size_t split = 0; split + 1U < base->piece_count; ++split) {
		double best = DBL_MAX;
		uint64_t preceding = 0U, selected = 0U;
		for (size_t s = 0; s < base->dualway_count; ++s) {
			const struct GENGHOST_DUALWAY *span = &base->dualways[s];
			if (span->split != split) {
				continue;
			}
			size_t source_rank = departures[seed - 2U];
			size_t back = arrivals[seed - 2U];
			if (source_rank >= span->source_count) {
				source_rank = span->source_count - 1U;
			}
			if (back >= span->target_count) {
				back = span->target_count - 1U;
			}
			size_t target_rank = span->target_count - 1U - back;
			size_t source = span->sources[source_rank], target = span->targets[target_rank];
			const struct GENGHOST_ROUTE *other = &set->routes[span->other];
			double cost = genghost_route_piece_distance(base, source) +
						  piece_distance(base->pieces[source], other->pieces[target]) +
						  genghost_route_piece_distance(other, span->other_merge) -
						  genghost_route_piece_distance(other, target) -
						  genghost_route_piece_distance(base, span->merge);
			if (cost < best) {
				best = cost;
				selected =
					3U + preceding + (uint64_t)source_rank * span->target_count + target_rank;
			}
			preceding += span->pairs;
		}
		if (selected) {
			unsigned int segment = (unsigned int)(genghost_route_piece_distance(base, split) /
												  base->length * GENGHOST_POLICY_SEGMENTS);
			if (segment >= GENGHOST_POLICY_SEGMENTS) {
				segment = GENGHOST_POLICY_SEGMENTS - 1U;
			}
			policy->segment[segment].dualway_choice = selected;
		}
	}
}

void genghost_route_release(struct GENGHOST_ROUTE *route)
{
	free(route->pieces);
	free(route->points);
	free(route->portals);
	for (size_t i = 0; i < route->dualway_count; ++i) {
		free(route->dualways[i].sources);
	}
	free(route->dualways);
	memset(route, 0, sizeof(*route));
}

int genghost_route_policy(const struct GENGHOST_ROUTES *set, size_t index,
						  const struct GENGHOST_POLICY *policy, struct GENGHOST_ROUTE *result,
						  char *error, size_t error_size)
{
	const struct GENGHOST_ROUTE *base = &set->routes[index];
	int path[901];
	unsigned char portals[901] = {0}, visited[900] = {0};
	size_t count = 0U, next = 0U, switches = 0U;
	unsigned int confirmation_remaining = 0U;
	memset(result, 0, sizeof(*result));
	if (!base->dualway_count) {
		return 0;
	}
	int enabled = 0;
	for (size_t i = 0; i < GENGHOST_POLICY_SEGMENTS; ++i) {
		if (policy->segment[i].dualway_choice) {
			enabled = 1;
			break;
		}
	}
	if (!enabled) {
		return 0;
	}
	for (size_t split = 0; split + 1U < base->piece_count; ++split) {
		if (split < next) {
			continue;
		}
		unsigned int segment = (unsigned int)(genghost_route_piece_distance(base, split) /
											  base->length * GENGHOST_POLICY_SEGMENTS);
		if (segment >= GENGHOST_POLICY_SEGMENTS) {
			segment = GENGHOST_POLICY_SEGMENTS - 1U;
		}
		uint64_t choice = policy->segment[segment].dualway_choice;
		if (!choice) {
			continue;
		}
		uint64_t total = 0U, selected_pair = 0U;
		const struct GENGHOST_DUALWAY *selected = NULL;
		double best = DBL_MAX;
		for (size_t s = 0; s < base->dualway_count; ++s) {
			const struct GENGHOST_DUALWAY *span = &base->dualways[s];
			if (span->split != split) {
				continue;
			}
			if (UINT64_MAX - total < span->pairs) {
				route_error(error, error_size, "Too many dual-way choices to index.");
				return -1;
			}
			total += span->pairs;
			if (choice <= 2U) {
				double cost = choice == 1U ? span->shortest_cost : span->nearest_cost;
				if (cost < best) {
					best = cost;
					selected = span;
					selected_pair = choice == 1U ? span->shortest : span->nearest;
				}
			}
		}
		if (!total) {
			continue;
		}
		if (choice >= 3U) {
			uint64_t ordinal = (choice - 3U) % total;
			for (size_t s = 0; s < base->dualway_count; ++s) {
				const struct GENGHOST_DUALWAY *span = &base->dualways[s];
				if (span->split != split) {
					continue;
				}
				if (ordinal < span->pairs) {
					selected = span;
					selected_pair = ordinal;
					break;
				}
				ordinal -= span->pairs;
			}
		}
		if (!selected) {
			continue;
		}
		size_t source = selected->sources[selected_pair / selected->target_count];
		size_t target = selected->targets[selected_pair % selected->target_count];
		const struct GENGHOST_ROUTE *other = &set->routes[selected->other];
		for (size_t i = next; i <= source; ++i) {
			if (count >= 900U || visited[base->pieces[i]]) {
				goto invalid;
			}
			visited[base->pieces[i]] = 1U;
			if (confirmation_remaining) {
				portals[count] = 1U;
				--confirmation_remaining;
			}
			path[count++] = base->pieces[i];
		}
		portals[count - 1U] = 1U;
		for (size_t i = target; i < selected->other_merge; ++i) {
			if (count >= 900U || visited[other->pieces[i]]) {
				goto invalid;
			}
			visited[other->pieces[i]] = 1U;
			path[count] = other->pieces[i];
			portals[count++] = i == target ? 2U : i < target + 3U;
		}
		confirmation_remaining = selected->other_merge - target < 3U
									 ? 3U - (unsigned int)(selected->other_merge - target)
									 : 0U;
		next = selected->merge;
		++switches;
	}
	if (!switches) {
		return 0;
	}
	for (size_t i = next; i + 1U < base->piece_count; ++i) {
		if (count >= 900U || visited[base->pieces[i]]) {
			goto invalid;
		}
		visited[base->pieces[i]] = 1U;
		if (confirmation_remaining) {
			portals[count] = 1U;
			--confirmation_remaining;
		}
		path[count++] = base->pieces[i];
	}
	struct GENGHOST_ROUTES built = {0};
	size_t capacity = 0U, bytes = 0U;
	if (append_route(&built, &capacity, path, count, &bytes, portals, error, error_size)) {
		free(built.routes);
		return -1;
	}
	*result = built.routes[0];
	free(built.routes);
	result->switch_count = switches;
	return 1;
invalid:
	/* A nested/cyclic splice that revisits an earlier node is not a simple lap.
	 * Retain the original route; other selectors remain available. */
	return 0;
}

void genghost_routes_free(struct GENGHOST_ROUTES *set)
{
	if (set == NULL) {
		return;
	}
	for (size_t i = 0; i < set->count; i++) {
		genghost_route_release(&set->routes[i]);
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
			if (append_route(set, &capacity, path, depth, &bytes, NULL, error, error_size) != 0) {
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
	if (result == 0) {
		result = dualway_build(set, &bytes, error, error_size);
	}
	if (result != 0) {
		genghost_routes_free(set);
	}
	return result;
}

void genghost_policy_default(struct GENGHOST_POLICY *policy)
{
	for (size_t i = 0; i < GENGHOST_POLICY_SEGMENTS; i++) {
		policy->segment[i].dualway_choice = 0U;
		policy->segment[i].speed_scale = 1.0;
		policy->segment[i].lateral_offset = 0.0;
		policy->segment[i].lookahead_scale = 1.0;
		policy->segment[i].shift_rpm_scale = 1.0;
		policy->segment[i].shortcut_skip = 0U;
		policy->segment[i].line_mode = 0U;
		policy->segment[i].stunt_mode = 0U;
		policy->segment[i].corner_scale = 1.0;
		policy->segment[i].powergear_bias = 0.5;
		policy->segment[i].slide_brake = 1.0;
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
	if (route->portals) {
		while (driver->next_portal < route->piece_count) {
			size_t n = driver->next_portal;
			int piece = route->pieces[n];
			unsigned int tile = (unsigned char)track_route_element_ids[piece];
			unsigned int flags = (unsigned char)trkObjectList[tile].ss_multiTileFlag;
			double min_x = (unsigned char)track_route_columns[piece] * 1024.0;
			double max_z = (30 - (unsigned char)track_route_rows[piece]) * 1024.0;
			if (route->portals[n] && !(x >= min_x && x < min_x + (1U + !!(flags & 2U)) * 1024.0 &&
									   z < max_z && z >= max_z - (1U + !!(flags & 1U)) * 1024.0)) {
				break;
			}
			++driver->next_portal;
		}
	}
	double best = DBL_MAX, best_progress = driver->progress, best_distance = DBL_MAX;
	size_t first = driver->waypoint > 2U ? driver->waypoint - 2U : 0U;
	size_t best_index = driver->waypoint;
	for (size_t i = first; i + 1U < route->point_count; i++) {
		const struct GENGHOST_WAYPOINT *a = &route->points[i], *b = &route->points[i + 1U];
		if (route->portals && b->piece_index > driver->next_portal) {
			break;
		}
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

/* These pieces offer flat-road alternatives. Elevated geometry and stunts
 * retain the original driving line so useful launches remain available. */
static int open_piece(int piece)
{
	unsigned int tile = (unsigned char)track_route_element_ids[piece];
	int model = trkObjectList[tile].ss_physicalModel;
	return model >= PHYSICAL_MODEL_ROAD && model <= PHYSICAL_MODEL_CROSSROAD;
}

/* Penalty recognition checks the car center against the complete tile
 * footprint, including both rows/columns of multi-tile pieces. */
static void tile_portal(int piece, double margin, struct GENGHOST_REGION *portal)
{
	unsigned int tile = (unsigned char)track_route_element_ids[piece];
	unsigned int flags = (unsigned char)trkObjectList[tile].ss_multiTileFlag;
	unsigned int column = (unsigned char)track_route_columns[piece];
	unsigned int row = (unsigned char)track_route_rows[piece];
	portal->min_x = column * 1024.0 + margin;
	portal->max_x = (column + 1U + ((flags & 2U) != 0U)) * 1024.0 - margin;
	portal->min_z = (29.0 - row - ((flags & 1U) != 0U)) * 1024.0 + margin;
	portal->max_z = (30U - row) * 1024.0 - margin;
}

static double portal_cost(double x, double z, double ax, double az, double bx, double bz)
{
	return sqrt((x - ax) * (x - ax) + (z - az) * (z - az)) +
		   sqrt((x - bx) * (x - bx) + (z - bz) * (z - bz));
}

/* Intersect the direct segment first. Otherwise the shortest broken line
 * meets a rectangle edge: minimizing the sum of its two distances gives
 * a weighted interpolation along that edge, clamped at its corners. */
void genghost_region_crossing(const struct GENGHOST_REGION *portal, double ax, double az, double bx,
							  double bz, double *px, double *pz)
{
	double enter = 0.0, leave = 1.0;
	double delta[2] = {bx - ax, bz - az}, start[2] = {ax, az};
	double minimum[2] = {portal->min_x, portal->min_z}, maximum[2] = {portal->max_x, portal->max_z};
	int crosses = 1;
	for (unsigned int axis = 0U; axis < 2U; axis++) {
		if (delta[axis] == 0.0) {
			if (start[axis] < minimum[axis] || start[axis] > maximum[axis]) {
				crosses = 0;
			}
		} else {
			double a = (minimum[axis] - start[axis]) / delta[axis],
				   b = (maximum[axis] - start[axis]) / delta[axis];
			if (a > b) {
				double swap = a;
				a = b;
				b = swap;
			}
			enter = fmax(enter, a);
			leave = fmin(leave, b);
		}
	}
	if (crosses && enter <= leave && leave >= 0.0 && enter <= 1.0) {
		*px = ax + delta[0] * leave;
		*pz = az + delta[1] * leave;
		return;
	}
	double best = DBL_MAX, furthest = -1.0;
	for (unsigned int edge = 0U; edge < 4U; edge++) {
		double x, z;
		if (edge < 2U) {
			x = edge ? portal->max_x : portal->min_x;
			double da = fabs(x - ax), db = fabs(x - bx), denominator = da + db;
			z = bounded(denominator > 0.0 ? (db * az + da * bz) / denominator : bz, portal->min_z,
						portal->max_z);
		} else {
			z = edge == 3U ? portal->max_z : portal->min_z;
			double da = fabs(z - az), db = fabs(z - bz), denominator = da + db;
			x = bounded(denominator > 0.0 ? (db * ax + da * bx) / denominator : bx, portal->min_x,
						portal->max_x);
		}
		double cost = portal_cost(x, z, ax, az, bx, bz);
		double distance = (x - ax) * (x - ax) + (z - az) * (z - az);
		if (cost < best - 0.000001 || (fabs(cost - best) <= 0.000001 && distance > furthest)) {
			best = cost;
			furthest = distance;
			*px = x;
			*pz = z;
		}
	}
}

static int shortcut_target(const struct GENGHOST_ROUTE *route, const struct GENGHOST_DRIVER *driver,
						   unsigned int advance, double speed, struct GENGHOST_WAYPOINT *target)
{
	int confirmed = state.game_player_confirmed_route;
	size_t current = 0U;
	for (; current + 1U < route->piece_count; current++) {
		if (route->pieces[current] == confirmed) {
			break;
		}
	}
	if (current + 1U >= route->piece_count) {
		return 0;
	}
	/* The finish tile must be crossed through its actual finish plane. */
	if (confirmed == 0 && driver->progress > route->length * 0.5) {
		return 0;
	}
	size_t next = current + 1U;
	if (!open_piece(route->pieces[current]) && confirmed != 0) {
		return 0;
	}
	/* On a simple branch the native penalty state normally confirms the
	 * third successor without charging for the two intervening pieces.
	 * Branches and finish handling can differ; simulation decides the score. */
	if (advance > 3U) {
		advance = 3U;
	}
	for (unsigned int n = 1U; n < advance && next + 1U < route->piece_count; n++) {
		if (!open_piece(route->pieces[next]) || !open_piece(route->pieces[next + 1U])) {
			break;
		}
		next++;
	}
	if (!open_piece(route->pieces[next])) {
		return 0;
	}
	size_t future = next + 1U;
	if (future >= route->piece_count) {
		return 0;
	}
	if (!open_piece(route->pieces[future])) {
		return 0;
	}
	for (unsigned int n = 1U; n < advance && future + 1U < route->piece_count; n++) {
		if (!open_piece(route->pieces[future + 1U])) {
			break;
		}
		future++;
	}
	/* One input tick travels about speed*0.367 units. Leave enough inset
	 * to give discrete 20 Hz sampling a chance to register a corner touch. */
	double margin = bounded(40.0 + speed * 0.4, 48.0, 160.0);
	struct GENGHOST_REGION first, second;
	tile_portal(route->pieces[next], margin, &first);
	tile_portal(route->pieces[future], margin, &second);
	double x = state.playerstate.car_position.lx / 64.0,
		   z = state.playerstate.car_position.lz / 64.0;
	struct GENGHOST_REGION origin;
	tile_portal(route->pieces[current], 0.0, &origin);
	/* A grass crossing can be far from the painted road while still following
	 * its chosen tile corridor. Outside that corridor use normal recovery. */
	if (x < fmin(origin.min_x, first.min_x) - 512.0 ||
		x > fmax(origin.max_x, first.max_x) + 512.0 ||
		z < fmin(origin.min_z, first.min_z) - 512.0 ||
		z > fmax(origin.max_z, first.max_z) + 512.0) {
		return 0;
	}
	double bx = (second.min_x + second.max_x) * 0.5, bz = (second.min_z + second.max_z) * 0.5;
	double tx = bx, tz = bz;
	genghost_region_crossing(&first, x, z, bx, bz, &tx, &tz);
	target->x = tx;
	target->z = tz;
	target->height_specified = 0;
	target->distance = driver->progress;
	for (size_t i = driver->waypoint; i < route->point_count; i++) {
		if (route->points[i].piece == route->pieces[next]) {
			target->distance = route->points[i].distance;
			break;
		}
	}
	struct GENGHOST_WAYPOINT from = *target, to = *target;
	from.x = x;
	from.z = z;
	to.x = bx;
	to.z = bz;
	double curvature = point_curvature(&from, target, &to);
	double angle = 4.0 * atan(wheelbase() * curvature) * GENGHOST_ANGLE_SCALE;
	double grip = (double)simd_player.grip * 2.0 * simd_player.surface_grip[3] / 256.0;
	if (grip < 1.0) {
		grip = 1200.0;
	}
	target->speed = bounded(sqrt(512.0 * grip / (angle + 8.0)) * 0.88, 30.0, 250.0);
	return 1;
}

/* Geometry proposals, never alternate physics: every resulting replay still
 * runs through native contacts, route confirmation and penalty calculation. */
static void turn_quarter(double x, double z, int angle, double *tx, double *tz)
{
	switch ((angle & 1023) / 256) {
		case 1:
			*tx = z;
			*tz = -x;
			break;
		case 2:
			*tx = -x;
			*tz = -z;
			break;
		case 3:
			*tx = -z;
			*tz = x;
			break;
		default:
			*tx = x;
			*tz = z;
			break;
	}
}

/* The two columns deliberately describe rule sets rather than making up a
 * collision exploit for every obstacle. Clear geometric cuts remain useful
 * with either rule set. This catalog contains every drivable stunt model;
 * plain roads, intersections and scenery are not stunt recipes. */
struct STUNT_CATALOG_ENTRY {
	int model;
	struct GENGHOST_STUNT_RECIPE legacy, fixed;
	enum GENGHOST_STUNT_RECIPE_KIND alternate;
};
static const struct STUNT_CATALOG_ENTRY stunt_catalog[] = {
	{PHYSICAL_MODEL_CHICANE_RIGHT_LEFT,
	 {GENGHOST_RECIPE_CHORD, "chicane chord"},
	 {GENGHOST_RECIPE_CHORD, "chicane chord"},
	 GENGHOST_RECIPE_CHORD},
	{PHYSICAL_MODEL_CHICANE_LEFT_RIGHT,
	 {GENGHOST_RECIPE_CHORD, "chicane chord"},
	 {GENGHOST_RECIPE_CHORD, "chicane chord"},
	 GENGHOST_RECIPE_CHORD},
	{PHYSICAL_MODEL_HIGHWAY_ENTRANCE,
	 {GENGHOST_RECIPE_MEDIAN, "median approach"},
	 {GENGHOST_RECIPE_CLEAR_LANE, "clear inner lane"},
	 GENGHOST_RECIPE_OUTSIDE},
	{PHYSICAL_MODEL_HIGHWAY,
	 {GENGHOST_RECIPE_MEDIAN, "two-wheel median ride"},
	 {GENGHOST_RECIPE_MEDIAN, "two-wheel median ride"},
	 GENGHOST_RECIPE_OUTSIDE},
	{PHYSICAL_MODEL_RAMP,
	 {GENGHOST_RECIPE_OUTSIDE, "outside ramp bypass"},
	 {GENGHOST_RECIPE_OUTSIDE, "outside ramp bypass"},
	 GENGHOST_RECIPE_LAUNCH},
	{PHYSICAL_MODEL_SOLID_RAMP,
	 {GENGHOST_RECIPE_OUTSIDE, "outside solid ramp bypass"},
	 {GENGHOST_RECIPE_OUTSIDE, "outside solid ramp bypass"},
	 GENGHOST_RECIPE_LAUNCH},
	{PHYSICAL_MODEL_ELEVATED_ROAD,
	 {GENGHOST_RECIPE_UNDERPASS, "low underpass or deck exit"},
	 {GENGHOST_RECIPE_UNDERPASS, "low underpass or deck exit"},
	 GENGHOST_RECIPE_LAUNCH},
	{PHYSICAL_MODEL_ELEVATED_SPAN,
	 {GENGHOST_RECIPE_UNDERPASS, "low underpass or deck exit"},
	 {GENGHOST_RECIPE_UNDERPASS, "low underpass or deck exit"},
	 GENGHOST_RECIPE_LAUNCH},
	{PHYSICAL_MODEL_SOLID_ROAD,
	 {GENGHOST_RECIPE_LOW_EDGE, "solid deck edge or outside bypass"},
	 {GENGHOST_RECIPE_LOW_EDGE, "solid deck edge or outside bypass"},
	 GENGHOST_RECIPE_OUTSIDE},
	{PHYSICAL_MODEL_ELEVATED_CORNER,
	 {GENGHOST_RECIPE_CHORD, "low chord or inside deck line"},
	 {GENGHOST_RECIPE_CHORD, "low chord or inside deck line"},
	 GENGHOST_RECIPE_LAUNCH},
	{PHYSICAL_MODEL_OVERPASS,
	 {GENGHOST_RECIPE_UNDERPASS, "low crossing or deck exit"},
	 {GENGHOST_RECIPE_UNDERPASS, "low crossing or deck exit"},
	 GENGHOST_RECIPE_OUTSIDE},
	{PHYSICAL_MODEL_BANKED_ENTRANCE_B,
	 {GENGHOST_RECIPE_LOW_EDGE, "low bank entry"},
	 {GENGHOST_RECIPE_LOW_EDGE, "low bank entry"},
	 GENGHOST_RECIPE_OUTSIDE},
	{PHYSICAL_MODEL_BANKED_ENTRANCE_A,
	 {GENGHOST_RECIPE_LOW_EDGE, "low bank entry"},
	 {GENGHOST_RECIPE_LOW_EDGE, "low bank entry"},
	 GENGHOST_RECIPE_OUTSIDE},
	{PHYSICAL_MODEL_BANKED_ROAD,
	 {GENGHOST_RECIPE_LOW_EDGE, "low bank edge"},
	 {GENGHOST_RECIPE_LOW_EDGE, "low bank edge"},
	 GENGHOST_RECIPE_OUTSIDE},
	{PHYSICAL_MODEL_BANKED_CORNER,
	 {GENGHOST_RECIPE_CHORD, "inside bank chord"},
	 {GENGHOST_RECIPE_CHORD, "inside bank chord"},
	 GENGHOST_RECIPE_LOW_EDGE},
	{PHYSICAL_MODEL_LOOP,
	 {GENGHOST_RECIPE_GROUND_CROSS, "loop ground crossing"},
	 {GENGHOST_RECIPE_OUTSIDE, "outside loop bypass"},
	 GENGHOST_RECIPE_LAUNCH},
	{PHYSICAL_MODEL_TUNNEL,
	 {GENGHOST_RECIPE_FAST_PASS, "straight tunnel passage"},
	 {GENGHOST_RECIPE_FAST_PASS, "straight tunnel passage"},
	 GENGHOST_RECIPE_OUTSIDE},
	{PHYSICAL_MODEL_PIPE_ENTRANCE,
	 {GENGHOST_RECIPE_FAST_PASS, "pipe floor entry"},
	 {GENGHOST_RECIPE_FAST_PASS, "pipe floor entry"},
	 GENGHOST_RECIPE_OUTSIDE},
	{PHYSICAL_MODEL_PIPE,
	 {GENGHOST_RECIPE_FAST_PASS, "pipe floor chord"},
	 {GENGHOST_RECIPE_FAST_PASS, "pipe floor chord"},
	 GENGHOST_RECIPE_OUTSIDE},
	{PHYSICAL_MODEL_HALF_PIPE,
	 {GENGHOST_RECIPE_OUTSIDE, "outside half-pipe bypass"},
	 {GENGHOST_RECIPE_OUTSIDE, "outside half-pipe bypass"},
	 GENGHOST_RECIPE_FAST_PASS},
	{PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A,
	 {GENGHOST_RECIPE_GROUND_CROSS, "corkscrew wall crossing"},
	 {GENGHOST_RECIPE_OUTSIDE, "outside vertical corkscrew"},
	 GENGHOST_RECIPE_LAUNCH},
	{PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B,
	 {GENGHOST_RECIPE_GROUND_CROSS, "corkscrew wall crossing"},
	 {GENGHOST_RECIPE_OUTSIDE, "outside vertical corkscrew"},
	 GENGHOST_RECIPE_LAUNCH},
	{PHYSICAL_MODEL_SLALOM,
	 {GENGHOST_RECIPE_SLALOM_CENTER, "slalom center passage"},
	 {GENGHOST_RECIPE_SLALOM_CENTER, "slalom center passage"},
	 GENGHOST_RECIPE_OUTSIDE},
	{PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT,
	 {GENGHOST_RECIPE_CORK_CUT, "right of red wall"},
	 {GENGHOST_RECIPE_CORK_CUT, "right of red wall"},
	 GENGHOST_RECIPE_OUTSIDE}};

static const struct STUNT_CATALOG_ENTRY *catalog_entry(int model)
{
	for (size_t i = 0; i < sizeof(stunt_catalog) / sizeof(stunt_catalog[0]); ++i) {
		if (stunt_catalog[i].model == model) {
			return &stunt_catalog[i];
		}
	}
	return NULL;
}

const struct GENGHOST_STUNT_RECIPE *genghost_stunt_recipe(int model, int collision_enabled)
{
	const struct STUNT_CATALOG_ENTRY *entry = catalog_entry(model);
	return entry ? (collision_enabled ? &entry->legacy : &entry->fixed) : NULL;
}

int genghost_stunt_catalog_model(int model)
{
	return catalog_entry(model) != NULL;
}

static double car_half_width(void)
{
	double half = 0.0;
	for (unsigned int point = 0; point < SIMD_COLLISION_POINT_COUNT; ++point) {
		half = fmax(half, fabs((double)simd_player.collide_points[point].px));
	}
	for (unsigned int wheel = 0; wheel < 4U; ++wheel) {
		half = fmax(half, fabs(simd_player.wheel_coords[wheel].x / 64.0));
	}
	return half;
}

/* Collision point zero contains body extents. Point one is a broad-phase
 * radius, not another body corner: using it makes even COUN appear wider than
 * the real 46-unit gap. Include the wheel centers in the alignment envelope. */
static double slalom_half_width(void)
{
	double half = fabs((double)simd_player.collide_points[0].px);
	for (unsigned int wheel = 0U; wheel < 4U; ++wheel) {
		half = fmax(half, fabs(simd_player.wheel_coords[wheel].x / 64.0));
	}
	return half;
}

int genghost_slalom_center_fits(void)
{
	return slalom_half_width() < SLALOM_POLE_INNER_X - 1.0;
}

static double slalom_half_length(void)
{
	/* Native box: point0.x is width, point1.x is length; their y fields
	 * describe height and the broad-phase radius, respectively. */
	double half = fabs((double)simd_player.collide_points[1].px);
	for (unsigned int wheel = 0U; wheel < 4U; ++wheel) {
		half = fmax(half, fabs(simd_player.wheel_coords[wheel].z / 64.0));
	}
	return half;
}

static int slalom_center_available(int collision)
{
	/* Original collision behavior may admit a wider car as an exploit. The
	 * fixed collision proposal must fit the actual narrow center corridor. */
	return collision || genghost_slalom_center_fits();
}

static double car_half_length(void)
{
	double half = 0.0;
	for (unsigned int point = 0; point < SIMD_COLLISION_POINT_COUNT; ++point) {
		half = fmax(half, fabs((double)simd_player.collide_points[point].py));
	}
	for (unsigned int wheel = 0; wheel < 4U; ++wheel) {
		half = fmax(half, fabs(simd_player.wheel_coords[wheel].z / 64.0));
	}
	return half;
}

static double catalog_corner_speed(const struct GENGHOST_WAYPOINT *target,
								   const struct GENGHOST_WAYPOINT *after)
{
	struct GENGHOST_WAYPOINT from = *target;
	from.x = state.playerstate.car_position.lx / 64.0;
	from.z = state.playerstate.car_position.lz / 64.0;
	double curvature = point_curvature(&from, target, after);
	double angle = 4.0 * atan(wheelbase() * curvature) * GENGHOST_ANGLE_SCALE;
	double grip = (double)simd_player.grip * 2.0 * simd_player.surface_grip[3] / 256.0;
	if (grip < 1.0) {
		grip = 1200.0;
	}
	return bounded(sqrt(512.0 * grip / (angle + 8.0)) * 0.88, 35.0, 250.0);
}

/* A chord removes unnecessary steering, including the alternating chicane
 * spline. Projection is geometric, so all object rotations and reverse routes
 * use the same rule. Small lateral variations refine the entry/exit angle. */
static void catalog_chord(double cx, double cz, double ax, double az, double bx, double bz,
						  double lookahead, double offset, double *px, double *pz)
{
	double dx = bx - ax, dz = bz - az;
	double length = sqrt(dx * dx + dz * dz);
	if (length < 1.0) {
		*px = bx;
		*pz = bz;
		return;
	}
	double along =
		bounded(((cx - ax) * dx + (cz - az) * dz) / length + lookahead, 0.0, length + 120.0);
	*px = ax + (along * dx + offset * dz) / length;
	*pz = az + (along * dz - offset * dx) / length;
}

/* Straight outside corridors include the approach, not just an offset target
 * inside a wall. If already committed inside a solid's longitudinal span,
 * leave the normal controller in charge instead of steering through its side. */
static int catalog_outside(double cx, double cz, double direction, double entry, double exit,
						   double left, double right, double side, double half, double lookahead,
						   double *px, double *pz)
{
	double margin = half + 16.0;
	double boundary = side > 0.0 ? right : left;
	double corridor = boundary + side * margin;
	double longitudinal = car_half_length() + 20.0;
	double before = direction * entry - longitudinal;
	double after = direction * exit + longitudinal;
	double forward = direction * cz;
	if (forward >= before && forward <= after && side * (cx - boundary) < half + 8.0) {
		return 0;
	}
	*px = corridor;
	*pz = direction * fmin(forward + lookahead, after + 120.0);
	if (forward < before && side * (cx - boundary) < half + 8.0) {
		*pz = direction * fmin(direction * *pz, before);
	}
	return 1;
}

static int catalog_target(const struct GENGHOST_ROUTE *route, const struct GENGHOST_DRIVER *driver,
						  const struct GENGHOST_GENE *gene, double lookahead, int collision,
						  size_t begin, size_t end, const struct TRACKOBJECT *object, double ox,
						  double oz, double cx, double cz, double ax, double az, double bx,
						  double bz, struct GENGHOST_WAYPOINT *target, double *protected_end)
{
	int model = route->points[begin].physical_model;
	const struct STUNT_CATALOG_ENTRY *entry = catalog_entry(model);
	if (!entry) {
		return 0;
	}
	int alternate = gene->stunt_mode == GENGHOST_STUNT_HUMAN_ALT;
	enum GENGHOST_STUNT_RECIPE_KIND kind =
		alternate ? entry->alternate : (collision ? entry->legacy.kind : entry->fixed.kind);
	double direction = bz >= az ? 1.0 : -1.0;
	double side = direction * (alternate ? -1.0 : 1.0);
	double half = car_half_width();
	double phase = bounded(gene->lateral_offset, -1.0, 1.0);
	double px = ax, pz = cz + direction * lookahead;
	const struct GENGHOST_WAYPOINT *first = &route->points[begin], *last = &route->points[end];
	const struct CARSTATE *car = &state.playerstate;
	int elevated = model == PHYSICAL_MODEL_ELEVATED_ROAD || model == PHYSICAL_MODEL_ELEVATED_SPAN ||
				   model == PHYSICAL_MODEL_SOLID_ROAD || model == PHYSICAL_MODEL_ELEVATED_CORNER ||
				   model == PHYSICAL_MODEL_OVERPASS;
	/* The route carries terrain-adjusted heights. An already elevated car
	 * cannot take the low underpass; keep its deck line until a real exit. */
	int on_deck =
		elevated && first->height_specified && car->car_position.ly / 64.0 > first->y - 90.0;
	if (kind == GENGHOST_RECIPE_SLALOM_CENTER && !slalom_center_available(collision)) {
		kind = GENGHOST_RECIPE_OUTSIDE;
	}
	if ((kind == GENGHOST_RECIPE_CHORD || kind == GENGHOST_RECIPE_UNDERPASS) && on_deck) {
		kind = GENGHOST_RECIPE_LOW_EDGE;
	}
	if (kind == GENGHOST_RECIPE_LOW_EDGE && model == PHYSICAL_MODEL_SOLID_ROAD && !on_deck) {
		/* A filled bridge has no ground-level underpass. Approach its outside
		 * corridor from before the entrance rather than aiming at the deck. */
		kind = GENGHOST_RECIPE_OUTSIDE;
	}
	if (kind == GENGHOST_RECIPE_SLALOM_CENTER) {
		/* Stones alternate sides but leave the physical middle open. Aim at
		 * exactly x=0: COUN has only three units of nominal body clearance.
		 * A short pursuit horizon settles lateral/yaw error before the stones;
		 * the normal long road lookahead can drift into their inner corners. */
		double forward = direction * cz;
		double before = -SLALOM_POLE_FAR_Z - slalom_half_length() - 16.0;
		double after = SLALOM_POLE_FAR_Z + slalom_half_length() + 16.0;
		legacy_u16 yaw = (legacy_u16)(car->car_rotate.x - object->ss_rotY);
		double projected = slalom_half_width() * fabs(cos_fast(yaw) / 16384.0) +
						   slalom_half_length() * fabs(sin_fast(yaw) / 16384.0);
		double opening = fmax(0.0, SLALOM_POLE_INNER_X - projected - 1.0);
		int unaligned =
			fabs(cx) > opening || (!collision && projected >= SLALOM_POLE_INNER_X - 1.0);
		if (unaligned && forward >= before && forward <= after &&
			car->car_sumSurfAllWheels != CAR_WHEEL_CONTACT_NONE) {
			/* Do not acquire a narrow corridor by steering through a stone's
			 * side after its entrance. A native airborne continuation is scored
			 * separately; zero contacts alone never proves height clearance. */
			return 0;
		}
		px = 0.0;
		pz = direction * fmin(forward + 150.0, after + 120.0);
		if (unaligned && forward < before) {
			pz = direction * fmin(direction * pz, before);
		}
	} else if (kind == GENGHOST_RECIPE_CHORD || kind == GENGHOST_RECIPE_UNDERPASS) {
		catalog_chord(cx, cz, ax, az, bx, bz, lookahead, phase * (alternate ? 140.0 : 55.0), &px,
					  &pz);
	} else if (kind == GENGHOST_RECIPE_OUTSIDE) {
		double width = 120.0, in = az, out = bz;
		if (model == PHYSICAL_MODEL_TUNNEL) {
			width = 270.0;
		} else if (model == PHYSICAL_MODEL_PIPE_ENTRANCE || model == PHYSICAL_MODEL_PIPE ||
				   model == PHYSICAL_MODEL_HALF_PIPE) {
			width = 164.0;
		} else if (model == PHYSICAL_MODEL_SLALOM) {
			width = SLALOM_POLE_OUTER_X;
			in = -direction * SLALOM_POLE_FAR_Z;
			out = direction * SLALOM_POLE_FAR_Z;
		} else if (model == PHYSICAL_MODEL_HIGHWAY || model == PHYSICAL_MODEL_HIGHWAY_ENTRANCE) {
			width = 360.0;
		} else if (model == PHYSICAL_MODEL_LOOP) {
			width = 440.0;
		} else if (model == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT) {
			width = 150.0;
		} else if (model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A ||
				   model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B) {
			width = 700.0;
		}
		/* Keep a clear side that the car already reached. Before entry, test
		 * the shorter approach first; the alternate can try the other side. */
		if (fabs(cx) > width + half + 8.0) {
			side = cx > 0.0 ? 1.0 : -1.0;
		} else if (fabs(cx) > 30.0 || fabs(ax) > 30.0) {
			double approach = fabs(cx) > 30.0 ? cx : ax;
			side = (approach > 0.0 ? 1.0 : -1.0) * (alternate ? -1.0 : 1.0);
		}
		if (!catalog_outside(cx, cz, direction, in, out, -width, width, side,
							 half + fabs(phase) * 45.0, lookahead, &px, &pz)) {
			return 0;
		}
	} else if (kind == GENGHOST_RECIPE_FAST_PASS) {
		/* The tunnel/pipe center is a straight floor, not their banking or
		 * roof. Keep the whole car inside the smaller entrance aperture. */
		px = phase * fmax(0.0, 31.0 - half - 4.0);
		double opening = (model == PHYSICAL_MODEL_TUNNEL ? 120.0 : 115.0) - half - 12.0;
		double before = direction * az - car_half_length() - 20.0;
		double after = direction * bz + car_half_length() + 20.0;
		if (fabs(cx) > opening && direction * cz >= before && direction * cz <= after) {
			return 0;
		}
		if (fabs(cx) > opening && direction * cz < before) {
			pz = direction * fmin(direction * pz, before);
		}
	} else if (kind == GENGHOST_RECIPE_CLEAR_LANE || kind == GENGHOST_RECIPE_MEDIAN) {
		if (fabs(ax) > 20.0) {
			side = ax > 0.0 ? 1.0 : -1.0;
		}
		double lane = kind == GENGHOST_RECIPE_MEDIAN ? 120.0 : 120.0 + half + 16.0;
		lane += fabs(phase) * 40.0;
		if (model == PHYSICAL_MODEL_HIGHWAY_ENTRANCE) {
			/* The median grows only in the second half. Approach it from the
			 * actual entering lane instead of cutting across its front face. */
			double blend = bounded((pz + 512.0) / 512.0, 0.0, 1.0);
			px = (1.0 - blend) * ax + blend * side * lane;
		} else {
			px = side * lane;
		}
	} else if (kind == GENGHOST_RECIPE_LOW_EDGE) {
		point_at(route, driver->progress + lookahead, 0.0, target);
		/* Choose the lower of the real two edges; for a flat elevated corner
		 * use its inner edge, keeping a car-width margin inside the guardrail. */
		double edge = sqrt(target->edge_x * target->edge_x + target->edge_z * target->edge_z);
		double amount = edge > half + 18.0 ? (edge - half - 18.0) / edge : 0.0;
		double chosen = target->edge_y > 0.0 ? -1.0 : 1.0;
		if (model == PHYSICAL_MODEL_ELEVATED_CORNER || model == PHYSICAL_MODEL_BANKED_CORNER) {
			double tx, tz, ex, ez;
			turn_quarter(target->x - ox, target->z - oz, -object->ss_rotY, &tx, &tz);
			turn_quarter(target->edge_x, target->edge_z, -object->ss_rotY, &ex, &ez);
			chosen = (tx + 1024.0) * ex + (tz + 1024.0) * ez > 0.0 ? -1.0 : 1.0;
		}
		amount *= bounded(0.9 + phase * 0.1, 0.6, 1.0);
		target->x += chosen * amount * target->edge_x;
		target->y += chosen * amount * target->edge_y;
		target->z += chosen * amount * target->edge_z;
		target->speed = fmin(180.0, catalog_corner_speed(target, last));
		target->distance = last->distance;
		*protected_end = last->distance;
		return 1;
	} else if (kind == GENGHOST_RECIPE_LAUNCH && elevated) {
		if (car->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE && end + 1U < route->point_count) {
			size_t landing = end + 1U;
			unsigned int remaining = 1U + gene->shortcut_skip % 3U;
			for (size_t k = landing + 1U; k < route->point_count; ++k) {
				if (route->points[k].piece != route->points[landing].piece) {
					if (!remaining--) {
						break;
					}
					landing = k;
				}
			}
			*target = route->points[landing];
			target->height_specified = 0;
			*protected_end = target->distance;
		} else {
			/* A low car must not aim into the side of a solid deck. A car on
			 * its deck uses a legal edge approach before native physics releases it. */
			if (!on_deck) {
				return 0;
			}
			point_at(route, driver->progress + lookahead, bounded(0.7 + phase * 0.2, 0.4, 0.9),
					 target);
			target->distance = last->distance;
			*protected_end = last->distance;
		}
		target->speed = 250.0;
		return 1;
	} else {
		/* Reuse the previously native-tested crossing, launch and red-wall
		 * planners. Their parameter space remains available independently. */
		struct GENGHOST_GENE legacy = *gene;
		legacy.stunt_mode = kind == GENGHOST_RECIPE_CORK_CUT ? GENGHOST_STUNT_CORKSCREW_CUT
							: kind == GENGHOST_RECIPE_LAUNCH ? 4U
															 : 1U;
		return genghost_stunt_target(route, driver, &legacy, lookahead, collision, target,
									 protected_end);
	}
	double wx, wz;
	turn_quarter(px, pz, object->ss_rotY, &wx, &wz);
	*target = *first;
	target->x = ox + wx;
	target->z = oz + wz;
	target->height_specified = 0;
	target->speed =
		kind == GENGHOST_RECIPE_SLALOM_CENTER ? 250.0 : catalog_corner_speed(target, last);
	target->distance = last->distance;
	*protected_end = last->distance;
	return 1;
}

static int tactic_model(int model, unsigned int mode, int collision)
{
	if (mode == GENGHOST_STUNT_SLALOM_CENTER) {
		return model == PHYSICAL_MODEL_SLALOM;
	}
	if (mode == GENGHOST_STUNT_HUMAN || mode == GENGHOST_STUNT_HUMAN_ALT) {
		return genghost_stunt_catalog_model(model);
	}
	if (mode == GENGHOST_STUNT_CORKSCREW_CUT) {
		return model == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT;
	}
	if (mode == 1U) {
		return model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A ||
			   model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B ||
			   (collision && (stunt_model(model) || model == PHYSICAL_MODEL_SLALOM));
	}
	if (mode == 2U || mode == 3U) {
		return stunt_model(model) || model == PHYSICAL_MODEL_SLALOM ||
			   model == PHYSICAL_MODEL_HIGHWAY || model == PHYSICAL_MODEL_ELEVATED_ROAD ||
			   model == PHYSICAL_MODEL_ELEVATED_SPAN || model == PHYSICAL_MODEL_BANKED_ROAD;
	}
	return mode == 4U && (stunt_model(model) || model == PHYSICAL_MODEL_RAMP ||
						  model == PHYSICAL_MODEL_SOLID_RAMP);
}

static int stunt_target(const struct GENGHOST_ROUTE *route, const struct GENGHOST_DRIVER *driver,
						const struct GENGHOST_GENE *gene, double lookahead, int collision,
						struct GENGHOST_WAYPOINT *target, double *protected_end, int slalom_only)
{
	unsigned int mode = gene->stunt_mode;
	if (!mode) {
		return 0;
	}
	size_t begin = driver->waypoint;
	while (begin && route->points[begin - 1U].piece == route->points[begin].piece) {
		--begin;
	}
	while (begin < route->point_count &&
		   route->points[begin].distance <= driver->progress + fmax(lookahead, 900.0)) {
		size_t end = begin;
		while (end + 1U < route->point_count &&
			   route->points[end + 1U].piece == route->points[begin].piece) {
			++end;
		}
		int model = route->points[begin].physical_model;
		if (end > begin && route->points[end].distance > driver->progress - 100.0 &&
			(!slalom_only || model == PHYSICAL_MODEL_SLALOM) &&
			tactic_model(model, mode, collision)) {
			const struct GENGHOST_WAYPOINT *first = &route->points[begin],
										   *last = &route->points[end];
			int piece = first->piece;
			unsigned int tile = (unsigned char)track_route_element_ids[piece];
			const struct TRACKOBJECT *object = &trkObjectList[tile];
			double ox = track_object_base_x(object, (unsigned char)track_route_columns[piece]);
			double oz = track_object_base_z(object, (unsigned char)track_route_rows[piece]);
			double cx, cz, ax, az, bx, bz;
			turn_quarter(state.playerstate.car_position.lx / 64.0 - ox,
						 state.playerstate.car_position.lz / 64.0 - oz, -object->ss_rotY, &cx, &cz);
			turn_quarter(first->x - ox, first->z - oz, -object->ss_rotY, &ax, &az);
			turn_quarter(last->x - ox, last->z - oz, -object->ss_rotY, &bx, &bz);
			double direction = bz >= az ? 1.0 : -1.0;
			/* Once beyond this piece's exit, do not turn back toward its special line. */
			double exit_progress = (cz - bz) * direction;
			if (mode == GENGHOST_STUNT_HUMAN || mode == GENGHOST_STUNT_HUMAN_ALT ||
				mode == GENGHOST_STUNT_SLALOM_CENTER) {
				double dx = bx - ax, dz = bz - az;
				double length = sqrt(dx * dx + dz * dz);
				if (length > 1.0) {
					exit_progress = ((cx - bx) * dx + (cz - bz) * dz) / length;
				}
			}
			if (exit_progress > 140.0) {
				begin = end + 1U;
				continue;
			}
			if (mode == GENGHOST_STUNT_HUMAN || mode == GENGHOST_STUNT_HUMAN_ALT ||
				mode == GENGHOST_STUNT_SLALOM_CENTER) {
				return catalog_target(route, driver, gene, lookahead, collision, begin, end, object,
									  ox, oz, cx, cz, ax, az, bx, bz, target, protected_end);
			}
			double px = ax,
				   pz = direction > 0.0 ? fmax(az, cz + lookahead) : fmin(az, cz - lookahead);
			int flat = 1;
			if (mode == GENGHOST_STUNT_CORKSCREW_CUT) {
				/* Enter near the road center, then pass on the driving-right of
				 * the diagonal red wall (local x=-60..60). Turning onto its rising
				 * side too early can crash; the gradual offset follows the clear
				 * approach used by human cuts. Native contacts decide clearance. */
				double half_width = fabs((double)simd_player.collide_points[0].px);
				for (unsigned int wheel = 0; wheel < 4U; ++wheel) {
					half_width = fmax(half_width, fabs(simd_player.wheel_coords[wheel].x / 64.0));
				}
				double clearance = 75.0 + half_width + 12.0 + gene->lateral_offset * 70.0;
				double phase = bounded((direction * pz + 1000.0) / 1000.0, 0.0, 1.0);
				px = direction * fmax(60.0 + half_width + 12.0, clearance) * phase;
			} else if (mode == 1U) {
				if (model == PHYSICAL_MODEL_LOOP) {
					px = direction * (30.0 + gene->lateral_offset * 100.0);
				} else if (model == PHYSICAL_MODEL_SLALOM) {
					px = 60.0 + gene->lateral_offset * 140.0;
				} else if (model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A ||
						   model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B) {
					px = gene->lateral_offset * 150.0;
				} else {
					px = (ax + bx) * 0.5 + gene->lateral_offset * 150.0;
				}
			} else if (mode == 2U || mode == 3U) {
				double side = mode == 2U ? -1.0 : 1.0;
				if (model == PHYSICAL_MODEL_HIGHWAY) {
					px = side * 120.0 + gene->lateral_offset * 40.0;
				} else if (model == PHYSICAL_MODEL_LOOP) {
					px = side * 440.0 + gene->lateral_offset * 100.0;
				} else if (model == PHYSICAL_MODEL_SLALOM) {
					px = side * 220.0 + gene->lateral_offset * 100.0;
				} else {
					point_at(route, driver->progress + lookahead,
							 side * 1.0 + bounded(gene->lateral_offset, -0.6, 0.6), target);
					flat = 0;
				}
			} else {
				const struct CARSTATE *car = &state.playerstate;
				double base_y = first->height_specified ? first->y : 0.0;
				double car_y = car->car_position.ly / 64.0;
				int launched = cz * direction > 0.0 && car_y > base_y + 70.0 &&
							   car->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE;
				if (launched) {
					/* Flight cannot follow a loop's descending spline. Aim into later tile
					 * footprints; touching their centers in the air uses native recognition. */
					if (end + 1U >= route->point_count) {
						return 0;
					}
					size_t landing = end + 1U;
					int pieces = 0, last_piece = route->points[landing].piece;
					for (size_t k = landing + 1U; k < route->point_count; ++k) {
						if (route->points[k].piece != last_piece) {
							last_piece = route->points[k].piece;
							if (++pieces >= 1 + (int)(gene->shortcut_skip % 3U)) {
								break;
							}
							landing = k;
						}
					}
					*target = route->points[landing];
					target->height_specified = 0;
					target->speed = 250.0;
					*protected_end = target->distance;
					return 1;
				}
				double phase = bounded(gene->lateral_offset, -1.0, 1.0);
				if (model == PHYSICAL_MODEL_LOOP) {
					/* Human outer-edge launches hold a nearly straight heading near x=-350
					 * into the rising half. The road narrows inward as it rises, naturally
					 * releasing the car with forward pitch instead of following the crown. */
					px = -direction * (350.0 + phase * 100.0);
				} else {
					double side = model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A ? -1.0 : 1.0;
					/* A descending cork has no higher launch point: follow its exit/drop
					 * instead of targeting its entry behind the car. */
					double offset = side * 0.9 + phase * 0.5;
					point_at(route, driver->progress + lookahead, offset, target);
					flat = 0;
				}
			}
			if (flat) {
				double wx, wz;
				turn_quarter(px, pz, object->ss_rotY, &wx, &wz);
				*target = *first;
				target->x = ox + wx;
				target->z = oz + wz;
				target->height_specified = 0;
			}
			target->speed = 250.0;
			target->distance = last->distance;
			*protected_end = last->distance;
			return 1;
		}
		begin = end + 1U;
	}
	return 0;
}

int genghost_stunt_target(const struct GENGHOST_ROUTE *route, const struct GENGHOST_DRIVER *driver,
						  const struct GENGHOST_GENE *gene, double lookahead, int collision,
						  struct GENGHOST_WAYPOINT *target, double *protected_end)
{
	return stunt_target(route, driver, gene, lookahead, collision, target, protected_end, 0);
}

static int protected_target(const struct GENGHOST_ROUTE *route,
							const struct GENGHOST_DRIVER *driver, double lookahead, double speed,
							struct GENGHOST_WAYPOINT *target)
{
	if (!route->portals || driver->next_portal >= route->piece_count) {
		return 0;
	}
	size_t n = driver->next_portal, point = 0U;
	while (point < route->point_count && route->points[point].piece_index != n) {
		++point;
	}
	if (point >= route->point_count) {
		return 0;
	}
	if (route->portals[n] != 2U &&
		route->points[point].distance > driver->progress + lookahead + 1024.0) {
		return 0;
	}
	size_t future = n + 1U;
	while (future + 1U < route->piece_count && !route->portals[future]) {
		++future;
	}
	if (future >= route->piece_count) {
		future = n;
	}
	struct GENGHOST_REGION first, second;
	double margin = bounded(40.0 + speed * 0.4, 48.0, 160.0);
	tile_portal(route->pieces[n], margin, &first);
	tile_portal(route->pieces[future], margin, &second);
	double x = state.playerstate.car_position.lx / 64.0,
		   z = state.playerstate.car_position.lz / 64.0;
	double bx = (second.min_x + second.max_x) * 0.5, bz = (second.min_z + second.max_z) * 0.5;
	*target = route->points[point];
	genghost_region_crossing(&first, x, z, bx, bz, &target->x, &target->z);
	target->height_specified = 0;
	struct GENGHOST_WAYPOINT from = *target, to = *target;
	from.x = x;
	from.z = z;
	to.x = bx;
	to.z = bz;
	double angle =
		4.0 * atan(wheelbase() * point_curvature(&from, target, &to)) * GENGHOST_ANGLE_SCALE;
	double grip = (double)simd_player.grip * 2.0 * simd_player.surface_grip[3] / 256.0;
	if (grip < 1.0) {
		grip = 1200.0;
	}
	target->speed = bounded(sqrt(512.0 * grip / (angle + 8.0)) * 0.88, 30.0, 250.0);
	return 1;
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

/* A higher gear can deliberately make uphill engine force negative while
 * the rear wheels still drive. Test the actual top ratio, torque curve, drag,
 * gravity and native unsigned mass divide; no car/track name or fixed mph
 * power-gear threshold is assumed. Shifting clears the limiter, so this is
 * settled top-gear potential rather than a claim about the current tick. */
static int uphill_powergear_available(void)
{
	const struct CARSTATE *car = &state.playerstate;
	int gear = simd_player.num_gears;
	int pitch = car->car_rotate.y;
	if (car->car_sumSurfRearWheels == CAR_WHEEL_CONTACT_NONE || pitch < 12 || pitch > 192 ||
		car->car_pseudoGravity >= -12 || car->car_actual_speed < 50U * 256U ||
		simd_player.car_mass <= 0 || gear <= 0 || gear >= (int)SIMD_GEAR_RATIO_COUNT) {
		return 0;
	}
	/* The knob must physically reach each next gear. Forecast the drag and
	 * uphill speed lost while the clutch is disengaged. A force sign only at
	 * current speed can shift too early and miss the narrow boost window. */
	if (car->car_current_gear < 1 || car->car_current_gear > gear) {
		return 0;
	}
	unsigned int ticks = 0U;
	int x = car->car_knob_x, y = car->car_knob_y;
	for (int g = car->car_current_gear + 1; g <= gear; ++g) {
		int nx = simd_player.knob_points[g].px, ny = simd_player.knob_points[g].py;
		int neutral = simd_player.knob_points[0].py;
		if (x == nx) {
			ticks += (unsigned int)(abs(ny - y) + 5) / 6U;
		} else {
			ticks += (unsigned int)(abs(neutral - y) + 5) / 6U +
					 (unsigned int)(abs(nx - x) + 5) / 6U +
					 (unsigned int)(abs(ny - neutral) + 5) / 6U;
		}
		x = nx;
		y = ny;
	}
	if (ticks > 128U) {
		return 0;
	}
	legacy_u16 coast_speed = car->car_rev_speed;
	for (unsigned int tick = 0U; tick < ticks; ++tick) {
		int next = (int)coast_speed + car->car_pseudoGravity -
				   simd_player.aerorestable[coast_speed >> 10U];
		coast_speed = (legacy_u16)bounded(next, 0.0, 65535.0);
	}
	legacy_u16 ratio = simd_player.gear_ratios[gear];
	unsigned int rpm = (legacy_u32)coast_speed * ratio >> 16U;
	if (rpm < (unsigned int)simd_player.idle_rpm) {
		rpm = simd_player.idle_rpm;
	}
	if (rpm >= (unsigned int)simd_player.max_rpm ||
		(rpm >> 7U) >= sizeof(simd_player.torque_curve)) {
		return 0;
	}
	unsigned int torque = (unsigned char)simd_player.torque_curve[rpm >> 7U];
	legacy_s16 force = LEGACY_S16_WRAP_ADD(
		LEGACY_S16_WRAP_SUB(car->car_pseudoGravity, simd_player.aerorestable[coast_speed >> 10U]),
		LEGACY_S16_FROM_BITS((legacy_u16)(LEGACY_U16_WRAP_MUL(ratio >> 8U, torque) >> 4U)));
	if (force >= 0) {
		return 0;
	}
	legacy_s32 product = (legacy_s32)force * 25;
	legacy_s16 original =
		LEGACY_S16_FROM_BITS((legacy_u16)((legacy_u32)product / (legacy_u16)simd_player.car_mass));
	legacy_s16 corrected = LEGACY_S16_FROM_BITS((legacy_u16)(product / simd_player.car_mass));
	return LEGACY_S16_SAR(original, 1) > 0 && LEGACY_S16_SAR(corrected, 1) < 0;
}

unsigned char genghost_steering_key(double wanted)
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
								   struct GENGHOST_DRIVER *driver, int powergear_enabled,
								   int collision_enabled, int owoot)
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
	if (!owoot && gene->line_mode == 0U && gene->shortcut_skip != 0U) {
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
	int planned_shortcut = !owoot && gene->line_mode != 0U &&
						   shortcut_target(route, driver, gene->shortcut_skip + 1U, speed, &target);
	double tactic_end = 0.0;
	int road_center = owoot &&
					  (gene->stunt_mode == GENGHOST_STUNT_HUMAN ||
					   gene->stunt_mode == GENGHOST_STUNT_SLALOM_CENTER) &&
					  genghost_slalom_center_fits();
	int planned_tactic =
		(!owoot || road_center) &&
		stunt_target(route, driver, gene, lookahead, road_center ? 0 : collision_enabled, &target,
					 &tactic_end, road_center);
	if (planned_tactic) {
		planned_shortcut = 0;
	}
	if (!owoot && protected_target(route, driver, lookahead, speed, &target)) {
		planned_shortcut = 1;
		planned_tactic = 0;
	}
	/* Count only a target the controller actually uses. */
	if (planned_tactic) {
		++driver->tactic_ticks;
	}
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
	unsigned char input = genghost_steering_key(wanted);

	double target_speed = 250.0;
	/* Integrating dv/dframe against native distance/frame yields this exact
	 * units conversion for a conservative braking envelope. Aero drag only
	 * improves braking, so it need not be guessed here. */
	double braking = fmax(0.15, simd_player.braking_eff / 256.0) * 0.80;
	double braking_coefficient = 2.0 * braking / GENGHOST_MPH_TRAVEL;
	if (planned_shortcut ||
		(planned_tactic && (gene->stunt_mode == GENGHOST_STUNT_HUMAN ||
							gene->stunt_mode == GENGHOST_STUNT_HUMAN_ALT ||
							gene->stunt_mode == GENGHOST_STUNT_SLALOM_CENTER))) {
		double available = fmax(0.0, sqrt(world_x * world_x + world_z * world_z) - 140.0);
		double limit = target.speed * bounded(gene->corner_scale, 0.35, 2.5);
		target_speed = fmin(target_speed, sqrt(limit * limit + braking_coefficient * available));
	}
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
		/* Fund the real approach, not just the last 900 units of a stunt.
		 * Braking for the ordinary slalom spline can otherwise kill a native
		 * ramp jump before the center tactic becomes active. Exempt only the
		 * selected slalom points; the next corner retains its braking envelope. */
		if (point->physical_model == PHYSICAL_MODEL_SLALOM &&
			(future->stunt_mode == GENGHOST_STUNT_HUMAN ||
			 future->stunt_mode == GENGHOST_STUNT_SLALOM_CENTER) &&
			(!owoot || genghost_slalom_center_fits()) &&
			slalom_center_available(collision_enabled)) {
			limit = 250.0;
		}
		if ((planned_tactic && point->distance <= tactic_end) ||
			(planned_shortcut && point->distance < target.distance && open_piece(point->piece))) {
			limit = 250.0;
		}
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
	if (local.z < -30 || (driver->off_route > 500.0 && !planned_shortcut && !planned_tactic)) {
		target_speed = fmin(target_speed, 38.0);
	}
	/* Human laps sustain controlled slides under throttle. Keep the safer
	 * brake response as a segment gene instead of forcing it on every line. */
	if (gene->slide_brake > 0.5 && car->car_slidingFlag && fabs(wanted) > 60.0) {
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
		/* Preserve acceleration on approach. Some cars reach power gear while
		 * still climbing: shift only when top-gear force remains negative
		 * after the clutch's predicted speed loss. Retain unloaded attempts
		 * for other ramps and stunts. */
		int seek_pg = useful_pg && gene->powergear_bias > 0.05 &&
					  (uphill_powergear_available() ||
					   (pg_feature_ahead && car->car_sumSurfRearWheels == CAR_WHEEL_CONTACT_NONE &&
						speed > bounded(85.0 - 65.0 * gene->powergear_bias, 20.0, 90.0)));
		if (car->car_current_gear < simd_player.num_gears &&
			(car->car_currpm >= shift_rpm || seek_pg)) {
			input |= INPUT_SHIFT_UP_FLAG;
		} else if (!seek_pg &&
				   (!useful_pg || car->car_sumSurfRearWheels != CAR_WHEEL_CONTACT_NONE) &&
				   car->car_current_gear > 1 && car->car_currpm < simd_player.downshift_rpm &&
				   car->car_currpm * (double)simd_player.gear_ratios[car->car_current_gear - 1] /
						   fmax(1.0, simd_player.gear_ratios[car->car_current_gear]) <
					   shift_rpm - 300.0) {
			input |= INPUT_SHIFT_DOWN_FLAG;
		}
	}
	return input;
}
