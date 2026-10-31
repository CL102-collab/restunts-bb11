#include "extended_opponent.h"
#include "externs.h"
#include "extended_track.h"
#include "memmgr.h"
#include "trackdata_layout.h"
#include "fatal.h"

#ifdef RESTUNTS_SDL3
#include <stdint.h>
#include <stdlib.h>
#include "track_objects.h"
#include "extended_geometry.h"

struct EXTENDED_ROUTE_VERTEX {
	uint32_t distance;
	int32_t first_incoming;
	int32_t heap_position;
	legacy_s16 next_route;
	legacy_u16 heap_node;
};

struct EXTENDED_ROUTE_EDGE {
	int32_t next;
	legacy_u16 source;
};

static void route_heap_update(struct EXTENDED_ROUTE_VERTEX *vertices, unsigned *count,
							  unsigned node)
{
	unsigned position;
	if (vertices[node].heap_position < 0) {
		position = (*count)++;
	} else {
		position = (unsigned)vertices[node].heap_position;
	}
	while (position) {
		unsigned parent = (position - 1U) / 2U;
		unsigned parent_node = vertices[parent].heap_node;
		if (vertices[parent_node].distance <= vertices[node].distance) {
			break;
		}
		vertices[position].heap_node = (legacy_u16)parent_node;
		vertices[parent_node].heap_position = (int32_t)position;
		position = parent;
	}
	vertices[position].heap_node = (legacy_u16)node;
	vertices[node].heap_position = (int32_t)position;
}

static unsigned route_heap_pop(struct EXTENDED_ROUTE_VERTEX *vertices, unsigned *count)
{
	unsigned result = vertices[0].heap_node;
	unsigned replacement = vertices[--(*count)].heap_node;
	vertices[result].heap_position = -2;
	if (!*count) {
		return result;
	}
	unsigned position = 0;
	while (position * 2U + 1U < *count) {
		unsigned child = position * 2U + 1U;
		if (child + 1U < *count && vertices[vertices[child + 1U].heap_node].distance <
									   vertices[vertices[child].heap_node].distance) {
			child++;
		}
		unsigned child_node = vertices[child].heap_node;
		if (vertices[replacement].distance <= vertices[child_node].distance) {
			break;
		}
		vertices[position].heap_node = (legacy_u16)child_node;
		vertices[child_node].heap_position = (int32_t)position;
		position = child;
	}
	vertices[position].heap_node = (legacy_u16)replacement;
	vertices[replacement].heap_position = (int32_t)position;
	return result;
}

int extended_opponent_build_route(const legacy_u8 costs[256])
{
	if (!extended_track_active() || !costs || track_pieces_counter <= 0 ||
		(unsigned)track_pieces_counter >= extended_track_route_capacity()) {
		return 0;
	}
	unsigned count = (unsigned)track_pieces_counter;
	struct EXTENDED_ROUTE_VERTEX *vertices = calloc(count, sizeof(*vertices));
	struct EXTENDED_ROUTE_EDGE *edges = malloc(count * 2U * sizeof(*edges));
	if (!vertices || !edges) {
		free(vertices);
		free(edges);
		return 0;
	}
	for (unsigned node = 0; node < count; node++) {
		vertices[node].distance = UINT32_MAX;
		vertices[node].first_incoming = -1;
		vertices[node].heap_position = -1;
		vertices[node].next_route = -1;
	}
	unsigned edge_count = 0;
	for (unsigned source = 0; source < count; source++) {
		legacy_s16 targets[2] = {track_primary_route_links[source],
								 track_alternate_route_links[source]};
		for (unsigned branch = 0; branch < 2; branch++) {
			int destination = targets[branch];
			if (destination == TRACK_ROUTE_LINK_NONE) {
				continue;
			}
			if (destination < 0 || (unsigned)destination >= count) {
				free(vertices);
				free(edges);
				return 0;
			}
			edges[edge_count].source = (legacy_u16)source;
			edges[edge_count].next = vertices[destination].first_incoming;
			vertices[destination].first_incoming = (int32_t)edge_count++;
		}
	}
	/* Starting at the finish in the reversed graph finds the cheapest return
 * path for every branch. Positive costs prevent cycles in the chosen path. */
	unsigned heap_count = 0;
	vertices[0].distance = 0;
	route_heap_update(vertices, &heap_count, 0);
	while (heap_count) {
		unsigned destination = route_heap_pop(vertices, &heap_count);
		for (int32_t edge = vertices[destination].first_incoming; edge >= 0;
			 edge = edges[edge].next) {
			unsigned source = edges[edge].source;
			if (!source || vertices[source].heap_position == -2) {
				continue;
			}
			uint32_t cost = (uint32_t)costs[(legacy_u8)track_route_element_ids[source]] + 1U;
			uint32_t distance = vertices[destination].distance + cost;
			if (distance >= vertices[source].distance) {
				continue;
			}
			vertices[source].distance = distance;
			vertices[source].next_route = (legacy_s16)destination;
			route_heap_update(vertices, &heap_count, source);
		}
	}
	int first = -1;
	legacy_s16 targets[2] = {track_primary_route_links[0], track_alternate_route_links[0]};
	for (unsigned branch = 0; branch < 2; branch++) {
		int candidate = targets[branch];
		if (candidate < 0 || (unsigned)candidate >= count ||
			vertices[candidate].distance == UINT32_MAX) {
			continue;
		}
		if (first < 0 || vertices[candidate].distance < vertices[first].distance) {
			first = candidate;
		}
	}
	if (first < 0) {
		free(vertices);
		free(edges);
		return 0;
	}
	/* Validate the whole sequence before replacing any usable route. */
	unsigned length = 1;
	for (int node = first; node; node = vertices[node].next_route) {
		if (node < 0 || (unsigned)node >= count || ++length > count) {
			free(vertices);
			free(edges);
			return 0;
		}
	}
	legacy_u8 *output = (legacy_u8 *)opponent_route_track_indices;
	LEGACY_WRITE_U16_LE(output, 0U);
	unsigned index = 1;
	for (int node = first; node; node = vertices[node].next_route) {
		LEGACY_WRITE_U16_LE(output + 2U * index, (legacy_u16)node);
		index++;
	}
	LEGACY_WRITE_U16_LE(output + 2U * index, 0U);
	index++;
	LEGACY_WRITE_U16_LE(output + 2U * index, 0U);
	index++;
	LEGACY_WRITE_U16_LE(output + 2U * index, 1U);
	free(vertices);
	free(edges);
	return 1;
}
#endif

#define EXTENDED_OPPONENT_AVERAGE_SHIFT 1U
#define EXTENDED_OPPONENT_PASSING_PAGE_RANGE (4096L * 64L)
#define EXTENDED_OPPONENT_LOWER_OVERPASS_SUBTYPE 1U
legacy_s16 extended_opponent_average(legacy_s16 first, legacy_s16 second, legacy_s16 *average)
{
	if (extended_track_active()) {
		*average = LEGACY_S16_WRAP_ADD(first, LEGACY_S16_SAR(LEGACY_S16_WRAP_SUB(second, first),
															 EXTENDED_OPPONENT_AVERAGE_SHIFT));
		return 1;
	}
	return 0;
}
legacy_s16 extended_opponent_passing_allowed(void)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		legacy_s32 dx = state.playerstate.car_position.lx - state.opponentstate.car_position.lx;
		legacy_s32 dz = state.playerstate.car_position.lz - state.opponentstate.car_position.lz;
		/* Cars a 65536-unit page apart must not share a passing lane. */
		return dx <= EXTENDED_OPPONENT_PASSING_PAGE_RANGE &&
			   dx >= -EXTENDED_OPPONENT_PASSING_PAGE_RANGE &&
			   dz <= EXTENDED_OPPONENT_PASSING_PAGE_RANGE &&
			   dz >= -EXTENDED_OPPONENT_PASSING_PAGE_RANGE;
	}
#endif
	return 1;
}
legacy_s16 extended_opponent_place_points(legacy_s16 track_index, struct VECTOR *first,
										  struct VECTOR *second)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		const struct CSTRK_OBJECT *object = extended_track_object_for_id(
			(legacy_u8)track_route_columns[track_index], (legacy_u8)track_route_rows[track_index],
			(legacy_u8)track_route_element_ids[track_index]);
		if (object) {
			struct VECTOR origin;
			extended_geometry_object_origin(extended_track_model(), object, &origin);
			if (first->y == ROUTE_POINT_HEIGHT_UNSPECIFIED) {
				first->y = 0;
			}
			if (second->y == ROUTE_POINT_HEIGHT_UNSPECIFIED) {
				second->y = 0;
			}
			first->y = LEGACY_S16_WRAP_ADD(
				first->y, extended_geometry_object_grade(object, first->x, first->z));
			second->y = LEGACY_S16_WRAP_ADD(
				second->y, extended_geometry_object_grade(object, second->x, second->z));
			first->x = LEGACY_S16_WRAP_ADD(origin.x, first->x);
			first->y = LEGACY_S16_WRAP_ADD(origin.y, first->y);
			first->z = LEGACY_S16_WRAP_ADD(origin.z, first->z);
			second->x = LEGACY_S16_WRAP_ADD(origin.x, second->x);
			second->y = LEGACY_S16_WRAP_ADD(origin.y, second->y);
			second->z = LEGACY_S16_WRAP_ADD(origin.z, second->z);
			return 1;
		}
	}
#else
	(void)track_index;
	(void)first;
	(void)second;
#endif
	return 0;
}
const struct TRKOBJINFO *extended_opponent_route_info(legacy_s16 track_index, legacy_u16 id,
													  const struct TRKOBJINFO *legacy_info)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		return extended_track_route_info((legacy_u8)track_route_columns[track_index],
										 (legacy_u8)track_route_rows[track_index], id);
	}
#else
	(void)track_index;
	(void)id;
#endif
	return legacy_info;
}
void extended_opponent_route_speed(legacy_s16 track_index, legacy_u16 id, legacy_u8 subtype,
								   const struct TRKOBJINFO *info, const struct TRACKOBJECT *object,
								   legacy_s8 *speed, EXTENDED_OPPONENT_SPEED_LOOKUP lookup)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active() && speed) {
		const struct CSTRK_OBJECT *placed =
			extended_track_object_for_id((legacy_u8)track_route_columns[track_index],
										 (legacy_u8)track_route_rows[track_index], id);
		if (placed) {
			legacy_u8 surface = placed->surface;
			if (object->ss_physicalModel == PHYSICAL_MODEL_OVERPASS &&
				subtype == EXTENDED_OPPONENT_LOWER_OVERPASS_SUBTYPE) {
				surface = placed->lower_surface;
			}
			*speed = LEGACY_S8_FROM_BITS(lookup((legacy_u8)info->opponent_speed_code + surface));
		}
	}
#else
	(void)track_index;
	(void)id;
	(void)subtype;
	(void)info;
	(void)object;
	(void)speed;
	(void)lookup;
#endif
}
legacy_s16 extended_opponent_load_route(const legacy_u8 *costs, void far *resource)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		legacy_s16 success = (legacy_s16)extended_opponent_build_route(costs);
		mmgr_release(resource);
		if (!success) {
			fatal_error((const legacy_s8 *)"Unable to build extended opponent route");
		}
		return 1;
	}
#else
	(void)costs;
	(void)resource;
#endif
	return 0;
}
