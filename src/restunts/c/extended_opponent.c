#include "extended_opponent.h"

#ifdef RESTUNTS_SDL3
#include <stdint.h>
#include <stdlib.h>
#include "extended_track.h"
#include "externs.h"
#include "track_objects.h"

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
