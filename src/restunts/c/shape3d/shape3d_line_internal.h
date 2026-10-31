#ifndef RESTUNTS_SHAPE3D_LINE_INTERNAL_H
#define RESTUNTS_SHAPE3D_LINE_INTERNAL_H
#include "shape3d_internal.h"
legacy_u16 line_prepare(legacy_u16 start_x, legacy_u16 start_y, legacy_u16 end_x, legacy_u16 end_y,
						legacy_u16 *line, legacy_u16 skip_clipping);
legacy_u16 draw_line_reject(legacy_u16 *line, legacy_u16 reject);
legacy_u16 draw_line_horizontal(legacy_u16 y, legacy_u16 start_x, legacy_u16 end_x,
								legacy_u16 *line, legacy_u16 unclipped);
legacy_u16 draw_line_step(legacy_u16 minor, legacy_u16 major);
void draw_line_subdivide(legacy_u16 *line);
legacy_u8 draw_line_clip_top(legacy_u16 *line);
legacy_u8 draw_line_clip_bottom(legacy_u16 *line);
legacy_s16 draw_line_clip_left(legacy_u16 *line);
legacy_u16 draw_line_clip_right(legacy_u16 *line);
#endif
