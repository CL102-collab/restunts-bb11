#ifndef RESTUNTS_UNIT_ROUTE_GRAPH_H
#define RESTUNTS_UNIT_ROUTE_GRAPH_H

#include "../legacy.h"

#define UNIT_ROUTE_GRAPH_BRANCHES 2U
#define UNIT_ROUTE_GRAPH_NO_CHECKPOINT (-1)
#define UNIT_ROUTE_GRAPH_BLUE 1U
#define UNIT_ROUTE_GRAPH_RED 2U
#define UNIT_ROUTE_GRAPH_COLOR_MASK (UNIT_ROUTE_GRAPH_BLUE | UNIT_ROUTE_GRAPH_RED)

struct UNIT_ROUTE_GRAPH_NODE {
	legacy_s16 next[UNIT_ROUTE_GRAPH_BRANCHES];
	legacy_u32 cost;
	legacy_s16 checkpoint;
};

/* Select a low-cost simple lap from route node zero back to zero, visiting
 * every authored checkpoint of at least one present color. Physical gate IDs
 * prevent opposite traversals of one gate from counting as different gates.
 * Reverse shortest-path bounds find the minimum-cost valid lap when the search
 * completes; a fixed work budget returns the best proven valid lap found so far
 * on unusually branch-heavy maps. Positive costs exclude arbitrary extra laps.
 * Success transfers a malloc-owned path including both zero endpoints; failure
 * leaves outputs unchanged. No route is returned without complete gate coverage. */
legacy_int unit_route_graph_find(const struct UNIT_ROUTE_GRAPH_NODE *nodes, legacy_u16 node_count,
								 const legacy_u8 *checkpoint_colors, legacy_u16 checkpoint_count,
								 legacy_s16 **out_path, legacy_u32 *out_count);

/* Build the immutable search graph from the currently compiled native track.
 * Only UNIT calls this; the six original opponents retain their route costs. */
legacy_int unit_route_graph_build(legacy_s16 **out_path, legacy_u32 *out_count);

#endif
