#include "../opponent/unit_route_graph.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../externs.h"
#include "../track/track_objects.h"
#include "extended_track.h"
#include "extended_opponent.h"

#define UNIT_GRAPH_ROUTE_SUBTYPE_MASK 15U
#define UNIT_GRAPH_MINIMUM_PIECE_COST 1U

static legacy_u32 graph_piece_cost(legacy_s16 piece)
{
	legacy_u8 id = (legacy_u8)track_route_element_ids[piece];
	const struct TRKOBJINFO *info =
		extended_opponent_route_info(piece, id, trkObjectList[id].ss_trkObjInfoPtr);
	if (!info) {
		return 0;
	}
	info += (legacy_u8)track_route_traversal_flags[piece] & UNIT_GRAPH_ROUTE_SUBTYPE_MASK;
	if (!info->route_vectors || info->route_point_count <= 0) {
		return 0;
	}
	struct CARSTATE sample;
	memset(&sample, 0, sizeof(sample));
	get_track_route_point(piece, &sample.car_route_target, 0, NULL);
	struct VECTOR previous = sample.car_route_target;
	legacy_f64 length = 0;
	for (legacy_u16 index = 1; index < (legacy_u8)info->route_point_count; index++) {
		get_track_route_point(piece, &sample.car_route_target, (legacy_s16)index, NULL);
		const struct VECTOR *current = &sample.car_route_target;
		legacy_f64 dx = LEGACY_S16_WRAP_SUB(current->x, previous.x);
		legacy_f64 dz = LEGACY_S16_WRAP_SUB(current->z, previous.z);
		legacy_f64 dy = current->y == ROUTE_POINT_HEIGHT_UNSPECIFIED ||
								previous.y == ROUTE_POINT_HEIGHT_UNSPECIFIED
							? 0
							: (legacy_s32)current->y - previous.y;
		length += sqrt(dx * dx + dy * dy + dz * dz);
		previous = *current;
	}
	return length < UNIT_GRAPH_MINIMUM_PIECE_COST ? UNIT_GRAPH_MINIMUM_PIECE_COST
		   : length >= LEGACY_U32_MAX			  ? LEGACY_U32_MAX
												  : (legacy_u32)ceil(length);
}

legacy_int unit_route_graph_build(legacy_s16 **out_path, legacy_u32 *out_count)
{
	if (!out_path || !out_count || track_pieces_counter <= 0) {
		return 0;
	}
	legacy_u16 node_count = (legacy_u16)track_pieces_counter;
	const struct CSTRK_TRACK *track = extended_track_active() ? extended_track_model() : NULL;
	legacy_u16 gate_count = track ? (legacy_u16)track->object_count : 0U;
	struct UNIT_ROUTE_GRAPH_NODE *nodes = malloc((size_t)node_count * sizeof(*nodes));
	legacy_u8 *colors = calloc(gate_count ? gate_count : 1U, sizeof(*colors));
	if (!nodes || !colors) {
		free(nodes);
		free(colors);
		return 0;
	}
	for (legacy_u16 index = 0; index < gate_count; index++) {
		colors[index] = cstrk_checkpoint_colors(track->objects[index].legacy_id);
	}
	legacy_int valid = 1;
	for (legacy_u16 piece = 0; piece < node_count; piece++) {
		nodes[piece].next[0] = track_primary_route_links[piece];
		nodes[piece].next[1] = track_alternate_route_links[piece];
		nodes[piece].cost = graph_piece_cost((legacy_s16)piece);
		nodes[piece].checkpoint = UNIT_ROUTE_GRAPH_NO_CHECKPOINT;
		if (!nodes[piece].cost) {
			valid = 0;
			break;
		}
		if (track) {
			const struct CSTRK_OBJECT *object = extended_track_object_for_id(
				(legacy_u8)track_route_columns[piece], (legacy_u8)track_route_rows[piece],
				(legacy_u8)track_route_element_ids[piece]);
			if (object && cstrk_checkpoint_colors(object->legacy_id)) {
				nodes[piece].checkpoint = (legacy_s16)(object - track->objects);
			}
		}
	}
	legacy_int result =
		valid && unit_route_graph_find(nodes, node_count, colors, gate_count, out_path, out_count);
	free(nodes);
	free(colors);
	return result;
}
#endif
