#include "unit_route_graph.h"

#include <stdlib.h>
#include <string.h>

#define UNIT_GRAPH_NO_EDGE (-1)
#define UNIT_GRAPH_HEAP_ABSENT (-1)
#define UNIT_GRAPH_HEAP_SETTLED (-2)
#define UNIT_GRAPH_DISTANCE_MAX ((legacy_u64) ~(legacy_u64)0U)
#define UNIT_GRAPH_SEARCH_LIMIT 1048576U
#define UNIT_GRAPH_COLOR_COUNT 2U

struct UNIT_GRAPH_VERTEX {
	legacy_u64 distance;
	legacy_s32 incoming, heap_position;
	legacy_u16 heap_node;
	legacy_s16 successor[UNIT_ROUTE_GRAPH_BRANCHES];
	legacy_u8 visited, choice;
};

struct UNIT_GRAPH_EDGE {
	legacy_s32 next;
	legacy_u16 source;
};

static void graph_heap_update(struct UNIT_GRAPH_VERTEX *vertices, legacy_u16 *count,
							  legacy_u16 node)
{
	legacy_u16 position =
		vertices[node].heap_position < 0 ? (*count)++ : (legacy_u16)vertices[node].heap_position;
	while (position) {
		legacy_u16 parent = (position - 1U) / 2U;
		legacy_u16 parent_node = vertices[parent].heap_node;
		if (vertices[parent_node].distance <= vertices[node].distance) {
			break;
		}
		vertices[position].heap_node = parent_node;
		vertices[parent_node].heap_position = position;
		position = parent;
	}
	vertices[position].heap_node = node;
	vertices[node].heap_position = position;
}

static legacy_u16 graph_heap_pop(struct UNIT_GRAPH_VERTEX *vertices, legacy_u16 *count)
{
	legacy_u16 node = vertices[0].heap_node;
	legacy_u16 replacement = vertices[--(*count)].heap_node;
	vertices[node].heap_position = UNIT_GRAPH_HEAP_SETTLED;
	if (!*count) {
		return node;
	}
	legacy_u16 position = 0;
	while ((legacy_u32)position * 2U + 1U < *count) {
		legacy_u16 child = position * 2U + 1U;
		if (child + 1U < *count && vertices[vertices[child + 1U].heap_node].distance <
									   vertices[vertices[child].heap_node].distance) {
			child++;
		}
		legacy_u16 child_node = vertices[child].heap_node;
		if (vertices[replacement].distance <= vertices[child_node].distance) {
			break;
		}
		vertices[position].heap_node = child_node;
		vertices[child_node].heap_position = position;
		position = child;
	}
	vertices[position].heap_node = replacement;
	vertices[replacement].heap_position = position;
	return node;
}

static legacy_int graph_initialize(const struct UNIT_ROUTE_GRAPH_NODE *nodes, legacy_u16 count,
								   legacy_u16 checkpoint_count, struct UNIT_GRAPH_VERTEX *vertices,
								   struct UNIT_GRAPH_EDGE *edges)
{
	for (legacy_u16 node = 0; node < count; node++) {
		if (!nodes[node].cost || nodes[node].checkpoint < UNIT_ROUTE_GRAPH_NO_CHECKPOINT ||
			(nodes[node].checkpoint >= 0 && nodes[node].checkpoint >= checkpoint_count)) {
			return 0;
		}
		vertices[node].distance = UNIT_GRAPH_DISTANCE_MAX;
		vertices[node].incoming = UNIT_GRAPH_NO_EDGE;
		vertices[node].heap_position = UNIT_GRAPH_HEAP_ABSENT;
	}
	legacy_u32 edge_count = 0;
	for (legacy_u16 node = 0; node < count; node++) {
		for (legacy_u16 branch = 0; branch < UNIT_ROUTE_GRAPH_BRANCHES; branch++) {
			legacy_s16 destination = nodes[node].next[branch];
			vertices[node].successor[branch] = destination;
			if (destination == UNIT_GRAPH_NO_EDGE) {
				continue;
			}
			if (destination < 0 || destination >= count) {
				return 0;
			}
			edges[edge_count].source = node;
			edges[edge_count].next = vertices[destination].incoming;
			vertices[destination].incoming = (legacy_s32)edge_count++;
		}
	}
	/* Exact shortest-return costs are an admissible lower bound even when a
	 * checkpoint-constrained route must take a longer branch. */
	legacy_u16 heap_count = 0;
	vertices[0].distance = 0;
	graph_heap_update(vertices, &heap_count, 0);
	while (heap_count) {
		legacy_u16 destination = graph_heap_pop(vertices, &heap_count);
		for (legacy_s32 edge = vertices[destination].incoming; edge != UNIT_GRAPH_NO_EDGE;
			 edge = edges[edge].next) {
			legacy_u16 node = edges[edge].source;
			if (node == 0 || vertices[node].heap_position == UNIT_GRAPH_HEAP_SETTLED) {
				continue;
			}
			legacy_u64 distance = vertices[destination].distance + nodes[node].cost;
			if (distance < vertices[node].distance) {
				vertices[node].distance = distance;
				graph_heap_update(vertices, &heap_count, node);
			}
		}
	}
	for (legacy_u16 node = 0; node < count; node++) {
		legacy_s16 first = vertices[node].successor[0];
		legacy_s16 second = vertices[node].successor[1];
		if (second >= 0 && (first < 0 || vertices[second].distance < vertices[first].distance)) {
			vertices[node].successor[0] = second;
			vertices[node].successor[1] = first;
		}
		if (first == second) {
			vertices[node].successor[1] = UNIT_GRAPH_NO_EDGE;
		}
	}
	return 1;
}

static void graph_gate_visit(legacy_s16 checkpoint, const legacy_u8 *colors, legacy_u16 *visits,
							 legacy_u16 crossed[UNIT_GRAPH_COLOR_COUNT], legacy_int entering)
{
	if (checkpoint < 0) {
		return;
	}
	legacy_int changed;
	if (entering) {
		changed = visits[checkpoint]++ == 0;
	} else {
		changed = --visits[checkpoint] == 0;
	}
	if (changed) {
		for (legacy_u16 color = 0; color < UNIT_GRAPH_COLOR_COUNT; color++) {
			if (colors[checkpoint] & (1U << color)) {
				if (entering) {
					crossed[color]++;
				} else {
					crossed[color]--;
				}
			}
		}
	}
}

static legacy_int graph_complete(const legacy_u16 totals[UNIT_GRAPH_COLOR_COUNT],
								 const legacy_u16 crossed[UNIT_GRAPH_COLOR_COUNT])
{
	return (!totals[0] && !totals[1]) || (totals[0] && crossed[0] == totals[0]) ||
		   (totals[1] && crossed[1] == totals[1]);
}

legacy_int unit_route_graph_find(const struct UNIT_ROUTE_GRAPH_NODE *nodes, legacy_u16 node_count,
								 const legacy_u8 *checkpoint_colors, legacy_u16 checkpoint_count,
								 legacy_s16 **out_path, legacy_u32 *out_count)
{
	if (!nodes || !node_count || node_count > LEGACY_S16_MAX || checkpoint_count > LEGACY_S16_MAX ||
		(checkpoint_count && !checkpoint_colors) || !out_path || !out_count) {
		return 0;
	}
	legacy_u16 totals[UNIT_GRAPH_COLOR_COUNT] = {0}, crossed[UNIT_GRAPH_COLOR_COUNT] = {0};
	for (legacy_u16 gate = 0; gate < checkpoint_count; gate++) {
		if (checkpoint_colors[gate] & (legacy_u8)~UNIT_ROUTE_GRAPH_COLOR_MASK) {
			return 0;
		}
		for (legacy_u16 color = 0; color < UNIT_GRAPH_COLOR_COUNT; color++) {
			if (checkpoint_colors[gate] & (1U << color)) {
				totals[color]++;
			}
		}
	}
	struct UNIT_GRAPH_VERTEX *vertices = calloc(node_count, sizeof(*vertices));
	struct UNIT_GRAPH_EDGE *edges = malloc(node_count * UNIT_ROUTE_GRAPH_BRANCHES * sizeof(*edges));
	legacy_s16 *path = malloc(((size_t)node_count + 1U) * sizeof(*path));
	legacy_s16 *best = malloc(((size_t)node_count + 1U) * sizeof(*best));
	legacy_u16 *gate_visits =
		calloc(checkpoint_count ? checkpoint_count : 1U, sizeof(*gate_visits));
	legacy_u32 best_count = 0;
	if (!vertices || !edges || !path || !best || !gate_visits ||
		!graph_initialize(nodes, node_count, checkpoint_count, vertices, edges)) {
		goto done;
	}
	legacy_u32 depth = 1U;
	legacy_u64 cost = nodes[0].cost, best_cost = UNIT_GRAPH_DISTANCE_MAX;
	path[0] = 0;
	vertices[0].visited = 1;
	graph_gate_visit(nodes[0].checkpoint, checkpoint_colors, gate_visits, crossed, 1);
	/* Path-local traversal permits branch merges, excludes arbitrary extra
	 * laps, and bounds preparation even on adversarial branching graphs. */
	for (legacy_u32 work = 0; depth && work < UNIT_GRAPH_SEARCH_LIMIT; work++) {
		legacy_u16 node = (legacy_u16)path[depth - 1U];
		legacy_u16 choice = vertices[node].choice++;
		if (choice >= UNIT_ROUTE_GRAPH_BRANCHES) {
			vertices[node].visited = 0;
			vertices[node].choice = 0;
			cost -= nodes[node].cost;
			graph_gate_visit(nodes[node].checkpoint, checkpoint_colors, gate_visits, crossed, 0);
			depth--;
			continue;
		}
		legacy_s16 next = vertices[node].successor[choice];
		if (next < 0) {
			continue;
		}
		if (next == 0) {
			if (depth > 1U && cost < best_cost && graph_complete(totals, crossed)) {
				memcpy(best, path, depth * sizeof(*path));
				best[depth] = 0;
				best_count = depth + 1U;
				best_cost = cost;
			}
		} else if (!vertices[next].visited && vertices[next].distance != UNIT_GRAPH_DISTANCE_MAX &&
				   cost + vertices[next].distance < best_cost) {
			path[depth++] = next;
			vertices[next].visited = 1;
			vertices[next].choice = 0;
			cost += nodes[next].cost;
			graph_gate_visit(nodes[next].checkpoint, checkpoint_colors, gate_visits, crossed, 1);
		}
	}
done:
	free(vertices);
	free(edges);
	free(path);
	free(gate_visits);
	if (!best_count) {
		free(best);
		return 0;
	}
	*out_path = best;
	*out_count = best_count;
	return 1;
}
