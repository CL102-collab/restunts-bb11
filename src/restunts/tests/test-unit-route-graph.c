#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/opponent/unit_route_graph.h"

#define TEST_NODES 9U
#define TEST_GATES 4U
#define TEST_RANDOM_CASES 2000U
#define TEST_LONG_ROUTE_NODES 16000U
#define TEST_NO_COST ((legacy_u64) ~(legacy_u64)0U)
#define TEST_SEED 78129U
#define TEST_RANDOM_MULTIPLIER 1664525U
#define TEST_RANDOM_INCREMENT 1013904223U

static legacy_u32 random_state = TEST_SEED;

static legacy_u32 next_random(void)
{
	random_state = random_state * TEST_RANDOM_MULTIPLIER + TEST_RANDOM_INCREMENT;
	return random_state;
}

static struct UNIT_ROUTE_GRAPH_NODE node(legacy_s16 first, legacy_s16 second, legacy_u32 cost,
										 legacy_s16 gate)
{
	struct UNIT_ROUTE_GRAPH_NODE value;
	value.next[0] = first;
	value.next[1] = second;
	value.cost = cost;
	value.checkpoint = gate;
	return value;
}

static legacy_int complete(const legacy_u8 *colors, legacy_u16 gates, legacy_u32 passed)
{
	legacy_u32 blue = 0, red = 0;
	for (legacy_u16 gate = 0; gate < gates; gate++) {
		if (colors[gate] & UNIT_ROUTE_GRAPH_BLUE) {
			blue |= 1U << gate;
		}
		if (colors[gate] & UNIT_ROUTE_GRAPH_RED) {
			red |= 1U << gate;
		}
	}
	return (!blue && !red) || (blue && (passed & blue) == blue) || (red && (passed & red) == red);
}

/* Deliberately exhaustive tiny-graph reference, independent of the production
 * reverse-edge heap, lower bounds and path-local gate counters. */
static legacy_u64 oracle(const struct UNIT_ROUTE_GRAPH_NODE *nodes, const legacy_u8 *colors,
						 legacy_u16 gates, legacy_u16 current, legacy_u32 visited,
						 legacy_u32 passed, legacy_u64 cost)
{
	visited |= 1U << current;
	cost += nodes[current].cost;
	if (nodes[current].checkpoint >= 0) {
		passed |= 1U << nodes[current].checkpoint;
	}
	legacy_u64 best = TEST_NO_COST;
	for (legacy_u16 branch = 0; branch < UNIT_ROUTE_GRAPH_BRANCHES; branch++) {
		legacy_s16 next = nodes[current].next[branch];
		if (next < 0) {
			continue;
		}
		if (!next && current && complete(colors, gates, passed)) {
			if (cost < best) {
				best = cost;
			}
		} else if (next && !(visited & (1U << next))) {
			legacy_u64 candidate =
				oracle(nodes, colors, gates, (legacy_u16)next, visited, passed, cost);
			if (candidate < best) {
				best = candidate;
			}
		}
	}
	return best;
}

static void compare(const struct UNIT_ROUTE_GRAPH_NODE *nodes, legacy_u16 count,
					const legacy_u8 *colors, legacy_u16 gates)
{
	legacy_u64 expected = oracle(nodes, colors, gates, 0, 0, 0, 0);
	legacy_s16 sentinel = -1;
	legacy_s16 *path = &sentinel;
	legacy_u32 path_count = TEST_NODES + 2U;
	legacy_int result = unit_route_graph_find(nodes, count, colors, gates, &path, &path_count);
	assert(result == (expected != TEST_NO_COST));
	if (!result) {
		assert(path == &sentinel && path_count == TEST_NODES + 2U);
		return;
	}
	assert(path_count >= 3U && path_count <= (legacy_u32)count + 1U);
	assert(path[0] == 0 && path[path_count - 1U] == 0);
	legacy_u64 cost = 0;
	legacy_u32 visited = 0, passed = 0;
	for (legacy_u32 index = 0; index + 1U < path_count; index++) {
		legacy_s16 current = path[index], next = path[index + 1U];
		assert(current >= 0 && current < count);
		assert(!(visited & (1U << current)));
		visited |= 1U << current;
		assert(nodes[current].next[0] == next || nodes[current].next[1] == next);
		cost += nodes[current].cost;
		if (nodes[current].checkpoint >= 0) {
			passed |= 1U << nodes[current].checkpoint;
		}
	}
	assert(cost == expected);
	assert(complete(colors, gates, passed));
	/* Preparation and replay must select the same path, including ties. */
	legacy_s16 *again = NULL;
	legacy_u32 again_count = 0;
	assert(unit_route_graph_find(nodes, count, colors, gates, &again, &again_count));
	assert(again_count == path_count && !memcmp(again, path, path_count * sizeof(*path)));
	free(again);
	free(path);
}

static void checkpoint_cases(void)
{
	struct UNIT_ROUTE_GRAPH_NODE nodes[TEST_NODES];
	for (legacy_u16 index = 0; index < TEST_NODES; index++) {
		nodes[index] = node(-1, -1, 1, -1);
	}
	/* The cheapest lap omits both required blue gates. */
	nodes[0] = node(1, 2, 1, -1);
	nodes[1] = node(0, -1, 1, -1);
	nodes[2] = node(3, -1, 10, 0);
	nodes[3] = node(0, -1, 10, 1);
	legacy_u8 colors[TEST_GATES] = {UNIT_ROUTE_GRAPH_BLUE, UNIT_ROUTE_GRAPH_BLUE, 0, 0};
	compare(nodes, TEST_NODES, colors, 2);
	/* A complete red alternative is sufficient and can be shorter. */
	nodes[1].checkpoint = 2;
	colors[2] = UNIT_ROUTE_GRAPH_RED;
	compare(nodes, TEST_NODES, colors, 3);
	/* A shared gate counts in both color totals. */
	colors[0] = UNIT_ROUTE_GRAPH_COLOR_MASK;
	compare(nodes, TEST_NODES, colors, 3);
	/* Opposite traversals of the same gate cannot satisfy another gate. */
	nodes[1].checkpoint = -1;
	nodes[3].checkpoint = 0;
	colors[0] = UNIT_ROUTE_GRAPH_BLUE;
	compare(nodes, TEST_NODES, colors, 2);
	/* Missing gates produce no route instead of a knowingly invalid lap. */
	nodes[2].checkpoint = -1;
	compare(nodes, TEST_NODES, colors, 2);
	/* No checkpoints restores ordinary shortest-lap selection. */
	nodes[1].checkpoint = nodes[3].checkpoint = -1;
	compare(nodes, TEST_NODES, NULL, 0);
}

static void randomized_cases(void)
{
	for (legacy_u32 iteration = 0; iteration < TEST_RANDOM_CASES; iteration++) {
		struct UNIT_ROUTE_GRAPH_NODE nodes[TEST_NODES], original[TEST_NODES];
		legacy_u8 colors[TEST_GATES];
		for (legacy_u16 index = 0; index < TEST_GATES; index++) {
			colors[index] = (legacy_u8)((next_random() >> 16U) & UNIT_ROUTE_GRAPH_COLOR_MASK);
		}
		for (legacy_u16 index = 0; index < TEST_NODES; index++) {
			legacy_s16 first = (legacy_s16)((next_random() >> 16U) % (TEST_NODES + 1U)) - 1;
			legacy_s16 second = (legacy_s16)((next_random() >> 16U) % (TEST_NODES + 1U)) - 1;
			legacy_u32 cost = (next_random() >> 16U) + 1U;
			legacy_s16 gate = (legacy_s16)((next_random() >> 16U) % (TEST_GATES + 1U)) - 1;
			nodes[index] = node(first, second, cost, gate);
		}
		memcpy(original, nodes, sizeof(nodes));
		compare(nodes, TEST_NODES, colors, TEST_GATES);
		assert(!memcmp(original, nodes, sizeof(nodes)));
	}
}

static void long_route_case(void)
{
	struct UNIT_ROUTE_GRAPH_NODE *nodes = malloc(TEST_LONG_ROUTE_NODES * sizeof(*nodes));
	assert(nodes);
	for (legacy_u16 index = 0; index < TEST_LONG_ROUTE_NODES; index++) {
		nodes[index] =
			node((legacy_s16)((index + 1U) % TEST_LONG_ROUTE_NODES), -1, LEGACY_U32_MAX, -1);
	}
	/* Opposite graph traversals share one physical gate; another gate is near
	 * the finish. Deep native maps must not consume recursive call stack. */
	nodes[1].checkpoint = 0;
	nodes[2].checkpoint = 0;
	nodes[TEST_LONG_ROUTE_NODES - 1U].checkpoint = 1;
	const legacy_u8 colors[2] = {UNIT_ROUTE_GRAPH_BLUE, UNIT_ROUTE_GRAPH_BLUE};
	legacy_s16 *path = NULL;
	legacy_u32 count = 0;
	assert(unit_route_graph_find(nodes, TEST_LONG_ROUTE_NODES, colors, 2, &path, &count));
	assert(count == TEST_LONG_ROUTE_NODES + 1U);
	for (legacy_u32 index = 0; index < count; index++) {
		assert(path[index] == (legacy_s16)(index % TEST_LONG_ROUTE_NODES));
	}
	free(path);
	free(nodes);
}

static void invalid_cases(void)
{
	struct UNIT_ROUTE_GRAPH_NODE nodes[2];
	nodes[0] = node(1, -1, LEGACY_U32_MAX, -1);
	nodes[1] = node(0, -1, LEGACY_U32_MAX, -1);
	compare(nodes, 2, NULL, 0);
	legacy_s16 *path = NULL;
	legacy_u32 count = 0;
	assert(!unit_route_graph_find(NULL, 2, NULL, 0, &path, &count));
	assert(!unit_route_graph_find(nodes, 0, NULL, 0, &path, &count));
	assert(!unit_route_graph_find(nodes, 2, NULL, 1, &path, &count));
	assert(!unit_route_graph_find(nodes, 2, NULL, 0, NULL, &count));
	assert(!unit_route_graph_find(nodes, 2, NULL, 0, &path, NULL));
	nodes[0].cost = 0;
	assert(!unit_route_graph_find(nodes, 2, NULL, 0, &path, &count));
	nodes[0].cost = 1;
	nodes[0].next[0] = 2;
	assert(!unit_route_graph_find(nodes, 2, NULL, 0, &path, &count));
	nodes[0].next[0] = -2;
	assert(!unit_route_graph_find(nodes, 2, NULL, 0, &path, &count));
	nodes[0].next[0] = 1;
	nodes[0].checkpoint = 0;
	assert(!unit_route_graph_find(nodes, 2, NULL, 0, &path, &count));
	legacy_u8 invalid_color = UNIT_ROUTE_GRAPH_COLOR_MASK + 1U;
	assert(!unit_route_graph_find(nodes, 2, &invalid_color, 1, &path, &count));
	assert(path == NULL && count == 0);
}

legacy_int main(void)
{
	checkpoint_cases();
	randomized_cases();
	long_route_case();
	invalid_cases();
	puts("UNIT route graph tests passed");
	return 0;
}
