#include "shape3d_hires.h"

#if defined(RESTUNTS_SDL3)

#include <SDL3/SDL_stdinc.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include "../fatal.h"
#include "hires.h"
#include "hypervision.h"
#include "../math/projection.h"
#include "render_workers.h"
#include "../shape3d/shape3d_internal.h"

#include "shape3d_hires_internal.h"

/* Lines become narrow quads once, instead of repeatedly covering the same
 * samples with a Bresenham spine and overlapping circles. */
static void draw_line(const struct SHAPE3D_HIRES_POINT *first,
					  const struct SHAPE3D_HIRES_POINT *last, legacy_f64 projected_width,
					  const struct HIRES_PAINT *paint)
{
	legacy_f64 dx = last->x - first->x;
	legacy_f64 dy = last->y - first->y;
	legacy_f64 length = SDL_sqrt(dx * dx + dy * dy);
	if (length == 0) {
		legacy_f64 x = SDL_floor(first->x + HIRES_SAMPLE_CENTER_OFFSET);
		legacy_f64 y = SDL_floor(first->y + HIRES_SAMPLE_CENTER_OFFSET);
		struct HYPERVISION_VERTEX point[4] = {{x, y, first->inverse_z},
											  {x + 1, y, first->inverse_z},
											  {x + 1, y + 1, first->inverse_z},
											  {x, y + 1, first->inverse_z}};
		struct HYPERVISION_MATERIAL material = paint->material;
		material.flags |= HYPERVISION_GROUND_CLIP;
		hypervision_polygon(point, 4, &material);
		return;
	}
	legacy_f64 first_radius = projected_width * first->inverse_z / 2;
	legacy_f64 last_radius = projected_width * last->inverse_z / 2;
	legacy_f64 limit = paint->scale / 2.0;
	if (first_radius < HIRES_SAMPLE_CENTER_OFFSET) {
		first_radius = HIRES_SAMPLE_CENTER_OFFSET;
	} else if (first_radius > limit) {
		first_radius = limit;
	}
	if (last_radius < HIRES_SAMPLE_CENTER_OFFSET) {
		last_radius = HIRES_SAMPLE_CENTER_OFFSET;
	} else if (last_radius > limit) {
		last_radius = limit;
	}
	legacy_f64 nx = length > 0 ? -dy / length : 1;
	legacy_f64 ny = length > 0 ? dx / length : 0;
	legacy_f64 end_x = length > 0 ? dx / length * HIRES_SAMPLE_CENTER_OFFSET : 0;
	legacy_f64 end_y =
		length > 0 ? dy / length * HIRES_SAMPLE_CENTER_OFFSET : HIRES_SAMPLE_CENTER_OFFSET;
	struct HYPERVISION_VERTEX points[4] = {
		{first->x - end_x + nx * first_radius, first->y - end_y + ny * first_radius,
		 first->inverse_z},
		{last->x + end_x + nx * last_radius, last->y + end_y + ny * last_radius, last->inverse_z},
		{last->x + end_x - nx * last_radius, last->y + end_y - ny * last_radius, last->inverse_z},
		{first->x - end_x - nx * first_radius, first->y - end_y - ny * first_radius,
		 first->inverse_z}};
	struct HYPERVISION_MATERIAL material = paint->material;
	material.flags |= HYPERVISION_GROUND_CLIP;
	hypervision_polygon(points, 4, &material);
}

static void draw_polygon(const struct SHAPE3D_HIRES_POINT *points, legacy_u32 count,
						 legacy_f64 padding, const struct HIRES_PAINT *paint)
{
	if (count < 3U) {
		if (count != 0U) {
			draw_line(&points[0], &points[count - 1U], 0, paint);
		}
		return;
	}
	struct HYPERVISION_VERTEX vertices[HYPERVISION_MAX_VERTICES];
	for (legacy_u32 index = 0; index < count; index++) {
		vertices[index] =
			(struct HYPERVISION_VERTEX){points[index].x, points[index].y, points[index].inverse_z};
	}
	hypervision_polygon(vertices, count, &paint->material);
	/* Thin authored decals retain a visible stroke without the former per-pixel
	 * distance/coverage search over every polygon edge. */
	if (padding > 0) {
		for (legacy_u32 index = 0; index < count; index++) {
			legacy_u32 next = index + 1U == count ? 0U : index + 1U;
			draw_line(&points[index], &points[next], 0, paint);
		}
	}
}

static void build_perimeter(const struct SHAPE3D_HIRES_POINT *center,
							const struct SHAPE3D_HIRES_POINT *first_axis,
							const struct SHAPE3D_HIRES_POINT *second_axis, legacy_f64 scale,
							legacy_u32 count, struct SHAPE3D_HIRES_POINT *points)
{
	legacy_u32 angle_step = ANGLE_FULL_TURN / count;
	for (legacy_u32 index = 0; index < count; index++) {
		legacy_s16 angle = (legacy_s16)(index * angle_step);
		legacy_f64 cosine = cos_fast(angle) * scale / TRIG_FIXED_ONE;
		legacy_f64 sine = sin_fast(angle) * scale / TRIG_FIXED_ONE;
		points[index].x =
			center->x + (first_axis->x - center->x) * cosine + (second_axis->x - center->x) * sine;
		points[index].y =
			center->y + (first_axis->y - center->y) * cosine + (second_axis->y - center->y) * sine;
		points[index].inverse_z = center->inverse_z +
								  (first_axis->inverse_z - center->inverse_z) * cosine +
								  (second_axis->inverse_z - center->inverse_z) * sine;
	}
}

static void draw_sphere(const struct HIRES_PRIMITIVE *primitive, const struct HIRES_PAINT *paint)
{
	struct SHAPE3D_HIRES_POINT points[HIRES_ROUND_POINTS];
	struct SHAPE3D_HIRES_POINT horizontal = primitive->points[0];
	struct SHAPE3D_HIRES_POINT vertical = horizontal;
	horizontal.x += primitive->size * HIRES_SPHERE_HORIZONTAL_SCALE;
	vertical.y += primitive->size * HIRES_SPHERE_VERTICAL_SCALE;
	build_perimeter(&primitive->points[0], &horizontal, &vertical, 1, primitive->round_points,
					points);
	draw_polygon(points, primitive->round_points, 0, paint);
}

static void draw_wheel(const struct HIRES_PRIMITIVE *primitive, struct HIRES_PAINT paint,
					   legacy_u16 side_color, legacy_u16 inner_color)
{
	legacy_u32 count = primitive->round_points;
	struct SHAPE3D_HIRES_POINT outer[HIRES_ROUND_POINTS];
	struct SHAPE3D_HIRES_POINT inner[HIRES_ROUND_POINTS];
	build_perimeter(&primitive->points[0], &primitive->points[1], &primitive->points[2], 1, count,
					outer);
	build_perimeter(&primitive->points[0], &primitive->points[1], &primitive->points[2],
					HIRES_WHEEL_INNER_SCALE, count, inner);
	legacy_f64 depth_x = primitive->points[3].x - primitive->points[0].x;
	legacy_f64 depth_y = primitive->points[3].y - primitive->points[0].y;
	legacy_f64 depth_z = primitive->points[3].inverse_z - primitive->points[0].inverse_z;
	for (legacy_u32 index = 0; index < count; index++) {
		legacy_u32 next = index + 1 == count ? 0 : index + 1;
		struct SHAPE3D_HIRES_POINT side[4] = {outer[index], outer[next], outer[next], outer[index]};
		side[2].x += depth_x;
		side[2].y += depth_y;
		side[2].inverse_z += depth_z;
		side[3].x += depth_x;
		side[3].y += depth_y;
		side[3].inverse_z += depth_z;
		draw_polygon(side, 4, 0, &paint);
	}
	paint.material.color = side_color;
	for (legacy_u32 index = 0; index < count; index++) {
		legacy_u32 next = index + 1 == count ? 0 : index + 1;
		struct SHAPE3D_HIRES_POINT rim[4] = {outer[index], outer[next], inner[next], inner[index]};
		draw_polygon(rim, 4, 0, &paint);
	}
	paint.material.color = inner_color;
	draw_polygon(inner, count, 0, &paint);
}

static void render_primitive(legacy_u32 index, legacy_u8 type, legacy_u16 color,
							 legacy_u16 second_color, legacy_u16 third_color,
							 legacy_u16 pattern_type, legacy_u16 pattern)
{
	if (index >= shape3d_hires_state.primitive_capacity ||
		shape3d_hires_state.primitives[index].count == 0) {
		return;
	}
	const struct HIRES_PRIMITIVE *primitive = &shape3d_hires_state.primitives[index];
	const struct HIRES_SHAPE *shape = &shape3d_hires_state.shapes[primitive->shape];
	struct HIRES_PAINT paint = {
		{index + 1U, color, second_color, pattern, (legacy_u8)pattern_type, 0},
		hires_render_scale()};
	if (shape->depth_mode == SHAPE3D_HIRES_DEPTH_BACKGROUND) {
		paint.material.flags |= HYPERVISION_BACKGROUND;
	}
	if (primitive->attached) {
		paint.material.flags |= HYPERVISION_DECAL;
		paint.material.family = primitive->family;
	}
	if (pattern_type == HIRES_PAINT_ALTERNATE) {
		paint.material.color = second_color;
		paint.material.alternate = color;
	}
	if ((type & RENDER_PRIMITIVE_BLACK_GRILLE_FLAG) != 0U) {
		type &= ~RENDER_PRIMITIVE_BLACK_GRILLE_FLAG;
		paint.material.color = 0;
	}
	if ((type & RENDER_PRIMITIVE_GHOST_FLAG) != 0) {
		type &= ~RENDER_PRIMITIVE_GHOST_FLAG;
		paint.material.color = 0;
		paint.material.pattern = PRERENDER_BLACK_GRILLE_PATTERN;
		paint.material.mode = HIRES_PAINT_PATTERN;
		second_color = 0;
		third_color = 0;
	}
	if (type == RENDER_PRIMITIVE_POLYGON) {
		draw_polygon(primitive->points, primitive->count, primitive->size, &paint);
	} else if (type == RENDER_PRIMITIVE_LINE) {
		draw_line(&primitive->points[0], &primitive->points[1], primitive->size, &paint);
	} else if (type == RENDER_PRIMITIVE_POINT) {
		draw_line(&primitive->points[0], &primitive->points[0], 0, &paint);
	} else if (type == RENDER_PRIMITIVE_SPHERE) {
		draw_sphere(primitive, &paint);
	} else if (type == RENDER_PRIMITIVE_WHEEL) {
		draw_wheel(primitive, paint, second_color, third_color);
	}
}

void shape3d_hires_batch_begin(void)
{
	shape3d_hires_state.batching = 1;
	hypervision_begin(shape3d_hires_state.ground_enabled ? &shape3d_hires_state.ground_plane
														 : NULL);
}

static legacy_s32 finish_commands(legacy_s32 incremental)
{
	if (!shape3d_hires_state.rendered_depth_valid ||
		shape3d_hires_state.rendered_generation != hires_generation()) {
		hires_depth_begin(0, hires_render_width(), 0, hires_render_height());
		shape3d_hires_state.rendered_depth_valid = 1;
		shape3d_hires_state.rendered_generation = hires_generation();
	}
	struct HIRES_RASTER_TARGET target;
	if (!hires_raster_prepare(&target)) {
		return 0;
	}
	return hypervision_end(&target, incremental);
}

void shape3d_hires_render(legacy_u32 index, legacy_u8 type, legacy_u16 color,
						  legacy_u16 second_color, legacy_u16 third_color, legacy_u16 pattern_type,
						  legacy_u16 pattern)
{
	if (!shape3d_hires_state.batching) {
		hypervision_begin(shape3d_hires_state.ground_enabled ? &shape3d_hires_state.ground_plane
															 : NULL);
	}
	render_primitive(index, type, color, second_color, third_color, pattern_type, pattern);
	if (!shape3d_hires_state.batching) {
		finish_commands(1);
	}
}

legacy_s32 shape3d_hires_batch_end(void)
{
	shape3d_hires_state.batching = 0;
	return finish_commands(0);
}

#endif
