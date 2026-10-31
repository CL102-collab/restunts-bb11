#ifndef RESTUNTS_SHAPE3D_PRERENDER_INTERNAL_H
#define RESTUNTS_SHAPE3D_PRERENDER_INTERNAL_H
#include "shape3d_internal.h"
#define PRERENDER_EDGE_ROW_CAPACITY 480
#define PRERENDER_EDGE_TABLE_COUNT 2U
#define PRERENDER_LINE_BUFFER_COUNT 2U
#define PRERENDER_FIXED_CARRY_LIMIT ((legacy_u32)LEGACY_U16_MAX)
enum PRERENDER_EDGE_MODE { PRERENDER_EDGE_CLIPPED_MODE = 0, PRERENDER_EDGE_UNCLIPPED_MODE = 1 };
void polygon_rasterize(legacy_u16 color, legacy_u16 vertex_count, const struct POINT2D *vertices,
					   legacy_u16 choose_edge_per_row);
void generate_poly_edges(legacy_s16 *edges, const legacy_u16 *line_setup, legacy_s16 clipping_mode);
void polygon_merge_second_edge(const legacy_u16 *line_setup, legacy_u16 choose_edge_per_row,
							   legacy_u16 needs_clipping, legacy_s16 *edge_tables);
#endif
