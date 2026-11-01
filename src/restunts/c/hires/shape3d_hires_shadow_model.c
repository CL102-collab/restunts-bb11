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

static void shadow_update_volume(struct HIRES_CAR_SHADOW *shadow);

static struct SHADOW_SILHOUETTE_POINT
shadow_project_point(const struct SHADOW_SILHOUETTE *silhouette, legacy_f64 x, legacy_f64 y,
					 legacy_f64 z)
{
	const struct SHADOW_PROJECTION *projection = &silhouette->projection;
	legacy_f64 height = y - projection->ground_height;
	return (struct SHADOW_SILHOUETTE_POINT){x + height * projection->x, z + height * projection->z};
}

/* Union the authored shape3d_hires_state.primitives in the receiving plane. Separate polygons
 * preserve gaps beside open-wheel suspension; the projected mode anchors
 * ground-level vertices while higher geometry travels along the light. */
static void shadow_silhouette_polygon(struct SHADOW_SILHOUETTE *silhouette,
									  const struct SHADOW_SILHOUETTE_POINT *points,
									  legacy_s32 count, legacy_s32 rasterize)
{
	if (!rasterize) {
		for (legacy_s32 index = 0; index < count; index++) {
			if (points[index].x < silhouette->min_x) {
				silhouette->min_x = points[index].x;
			}
			if (points[index].x > silhouette->max_x) {
				silhouette->max_x = points[index].x;
			}
			if (points[index].z < silhouette->min_z) {
				silhouette->min_z = points[index].z;
			}
			if (points[index].z > silhouette->max_z) {
				silhouette->max_z = points[index].z;
			}
		}
		return;
	}
	/* Projected masks change as a showroom car rotates. Scan only rows touched
	 * by this polygon, especially the narrow sides of rounded tire treads. */
	legacy_f64 min_z = points[0].z, max_z = points[0].z;
	for (legacy_s32 index = 1; index < count; index++) {
		min_z = SDL_min(min_z, points[index].z);
		max_z = SDL_max(max_z, points[index].z);
	}
	legacy_s32 first_row =
		SDL_max(1, (legacy_s32)SDL_floor((min_z - silhouette->min_z) * silhouette->scale_z - 0.5));
	legacy_s32 last_row =
		SDL_min(SHADOW_SILHOUETTE_LENGTH - 1,
				(legacy_s32)SDL_ceil((max_z - silhouette->min_z) * silhouette->scale_z + 0.5));
	for (legacy_s32 row = first_row; row < last_row; row++) {
		legacy_f64 z = silhouette->min_z + (row + 0.5) / silhouette->scale_z;
		legacy_f64 crossings[HIRES_ROUND_POINTS];
		legacy_s32 crossing_count = 0;
		for (legacy_s32 index = 0; index < count; index++) {
			const struct SHADOW_SILHOUETTE_POINT *first = &points[index];
			const struct SHADOW_SILHOUETTE_POINT *last = &points[(index + 1) % count];
			if ((first->z > z) != (last->z > z)) {
				legacy_f64 x =
					first->x + (last->x - first->x) * (z - first->z) / (last->z - first->z);
				legacy_s32 insertion = crossing_count++;
				while (insertion > 0 && crossings[insertion - 1] > x) {
					crossings[insertion] = crossings[insertion - 1];
					insertion--;
				}
				crossings[insertion] = x;
			}
		}
		for (legacy_s32 index = 0; index + 1 < crossing_count; index += 2) {
			legacy_s32 left = (legacy_s32)SDL_ceil(
				(crossings[index] - silhouette->min_x) * silhouette->scale_x - 0.5);
			legacy_s32 right = (legacy_s32)SDL_ceil(
				(crossings[index + 1] - silhouette->min_x) * silhouette->scale_x - 0.5);
			if (left < 1) {
				left = 1;
			}
			if (right > SHADOW_SILHOUETTE_WIDTH - 1) {
				right = SHADOW_SILHOUETTE_WIDTH - 1;
			}
			if (left < right) {
				memset(&silhouette->pixels[row * SHADOW_SILHOUETTE_WIDTH + left], LEGACY_U8_MAX,
					   (size_t)(right - left));
			}
		}
	}
}

static void shadow_silhouette_line(struct SHADOW_SILHOUETTE *silhouette, const struct VECTOR *first,
								   const struct VECTOR *last, legacy_s32 rasterize)
{
	struct SHADOW_SILHOUETTE_POINT start =
		shadow_project_point(silhouette, first->x, first->y, first->z);
	struct SHADOW_SILHOUETTE_POINT end =
		shadow_project_point(silhouette, last->x, last->y, last->z);
	legacy_f64 dx = end.x - start.x;
	legacy_f64 dz = end.z - start.z;
	legacy_f64 length = SDL_sqrt(dx * dx + dz * dz);
	if (length == 0) {
		return;
	}
	legacy_f64 radius = SHADOW_SILHOUETTE_LINE_RADIUS;
	if (rasterize) {
		legacy_f64 texel = 0.5 / silhouette->scale_x + 0.5 / silhouette->scale_z;
		if (radius < texel) {
			radius = texel;
		}
	}
	legacy_f64 x = dz * radius / length;
	legacy_f64 z = -dx * radius / length;
	struct SHADOW_SILHOUETTE_POINT points[4] = {{start.x - x, start.z - z},
												{end.x - x, end.z - z},
												{end.x + x, end.z + z},
												{start.x + x, start.z + z}};
	shadow_silhouette_polygon(silhouette, points, 4, rasterize);
}

static void shadow_projected_wheel(struct SHADOW_SILHOUETTE *silhouette,
								   const struct VECTOR *vertices, legacy_s32 rasterize)
{
	for (legacy_u32 rim = 0; rim <= SHAPE3D_WHEEL_RIM_VERTEX_COUNT;
		 rim += SHAPE3D_WHEEL_RIM_VERTEX_COUNT) {
		const struct VECTOR *center = &vertices[rim];
		struct SHAPE3D_HIRES_VECTOR first = {vertices[rim + SHAPE3D_WHEEL_FIRST_AXIS].x - center->x,
											 vertices[rim + SHAPE3D_WHEEL_FIRST_AXIS].y - center->y,
											 vertices[rim + SHAPE3D_WHEEL_FIRST_AXIS].z -
												 center->z};
		struct SHAPE3D_HIRES_VECTOR second = {
			vertices[rim + SHAPE3D_WHEEL_SECOND_AXIS].x - center->x,
			vertices[rim + SHAPE3D_WHEEL_SECOND_AXIS].y - center->y,
			vertices[rim + SHAPE3D_WHEEL_SECOND_AXIS].z - center->z};
		legacy_f64 vertical_radius = SDL_sqrt(first.y * first.y + second.y * second.y);
		/* Start at the exact lowest point, including tilted/custom radial axes. */
		legacy_f64 bottom_cosine = vertical_radius > 0 ? -first.y / vertical_radius : 1;
		legacy_f64 bottom_sine = vertical_radius > 0 ? -second.y / vertical_radius : 0;
		struct SHADOW_SILHOUETTE_POINT faces[2][HIRES_ROUND_POINTS];
		for (legacy_s32 vertex = 0; vertex < HIRES_ROUND_POINTS; vertex++) {
			legacy_s16 angle = (legacy_s16)(vertex * ANGLE_FULL_TURN / HIRES_ROUND_POINTS);
			legacy_f64 cosine = cos_fast(angle) / (legacy_f64)TRIG_FIXED_ONE;
			legacy_f64 sine = sin_fast(angle) / (legacy_f64)TRIG_FIXED_ONE;
			legacy_f64 along_first = bottom_cosine * cosine - bottom_sine * sine;
			legacy_f64 along_second = bottom_sine * cosine + bottom_cosine * sine;
			for (legacy_u32 face = 0; face < SDL_arraysize(faces); face++) {
				const struct VECTOR *origin = &vertices[face * SHAPE3D_WHEEL_RIM_VERTEX_COUNT];
				faces[face][vertex] = shadow_project_point(
					silhouette, origin->x + first.x * along_first + second.x * along_second,
					origin->y + first.y * along_first + second.y * along_second,
					origin->z + first.z * along_first + second.z * along_second);
			}
		}
		for (legacy_u32 face = 0; face < SDL_arraysize(faces); face++) {
			shadow_silhouette_polygon(silhouette, faces[face], HIRES_ROUND_POINTS, rasterize);
		}
		for (legacy_s32 vertex = 0; vertex < HIRES_ROUND_POINTS; vertex++) {
			legacy_s32 next = (vertex + 1) % HIRES_ROUND_POINTS;
			struct SHADOW_SILHOUETTE_POINT side[] = {faces[0][vertex], faces[0][next],
													 faces[1][next], faces[1][vertex]};
			shadow_silhouette_polygon(silhouette, side, SDL_arraysize(side), rasterize);
		}
	}
}

static void shadow_silhouette_primitives(struct SHADOW_SILHOUETTE *silhouette,
										 const struct SHAPE3D *shape, legacy_s32 rasterize)
{
	static const legacy_u8 vertex_counts[SHAPE3D_PRIMITIVE_TYPE_COUNT] = {0, 1, 2,	3, 4, 5, 6, 7,
																		  8, 9, 10, 2, 6, 3, 0, 0};
	const legacy_u8 *primitive = shape->shape3d_primitives;
	for (legacy_u16 index = 0; index < shape->shape3d_numprimitives; index++) {
		legacy_u8 type = primitive[0];
		if (type >= SHAPE3D_PRIMITIVE_TYPE_COUNT) {
			return;
		}
		legacy_u8 count = vertex_counts[type];
		const legacy_u8 *indices =
			primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + shape->shape3d_numpaints;
		primitive = indices + count;
		if (silhouette->projection.enabled && shape->shape3d_visibility_masks != NULL &&
			LEGACY_READ_U32_LE(shape->shape3d_visibility_masks +
							   index * SHAPE3D_VISIBILITY_MASK_SIZE) == 0) {
			continue;
		}
		struct VECTOR vertices[SHAPE3D_POLYGON_MAX_VERTICES];
		legacy_s32 valid = 1;
		for (legacy_u8 vertex = 0; vertex < count; vertex++) {
			if (indices[vertex] >= shape->shape3d_numverts) {
				valid = 0;
				break;
			}
			shape3d_vertex_read(shape, indices[vertex], &vertices[vertex]);
			if (!rasterize && vertices[vertex].y > silhouette->height) {
				silhouette->height = vertices[vertex].y;
			}
		}
		if (!valid) {
			continue;
		}
		struct SHADOW_SILHOUETTE_POINT points[HIRES_ROUND_POINTS];
		if (type >= SHAPE3D_PRIMITIVE_POLYGON_FIRST && type <= SHAPE3D_PRIMITIVE_POLYGON_LAST) {
			for (legacy_u8 vertex = 0; vertex < count; vertex++) {
				points[vertex] = shadow_project_point(silhouette, vertices[vertex].x,
													  vertices[vertex].y, vertices[vertex].z);
			}
			shadow_silhouette_polygon(silhouette, points, count, rasterize);
		} else if (type == SHAPE3D_PRIMITIVE_LINE) {
			shadow_silhouette_line(silhouette, &vertices[0], &vertices[1], rasterize);
		} else if (type == SHAPE3D_PRIMITIVE_WHEEL) {
			if (silhouette->projection.enabled) {
				shadow_projected_wheel(silhouette, vertices, rasterize);
				continue;
			}
			/* A wheel is an ellipse extruded from vertex 0 to vertex 3.
			 * Its two authored radial axes also support steered/custom wheels. */
			struct SHADOW_SILHOUETTE_POINT other[SHADOW_SILHOUETTE_ROUND_POINTS];
			for (legacy_s32 vertex = 0; vertex < SHADOW_SILHOUETTE_ROUND_POINTS; vertex++) {
				legacy_s16 angle =
					(legacy_s16)(vertex * ANGLE_FULL_TURN / SHADOW_SILHOUETTE_ROUND_POINTS);
				legacy_f64 cosine = cos_fast(angle) / (legacy_f64)TRIG_FIXED_ONE;
				legacy_f64 sine = sin_fast(angle) / (legacy_f64)TRIG_FIXED_ONE;
				points[vertex].x = vertices[0].x +
								   (vertices[SHAPE3D_WHEEL_FIRST_AXIS].x - vertices[0].x) * cosine +
								   (vertices[SHAPE3D_WHEEL_SECOND_AXIS].x - vertices[0].x) * sine;
				points[vertex].z = vertices[0].z +
								   (vertices[SHAPE3D_WHEEL_FIRST_AXIS].z - vertices[0].z) * cosine +
								   (vertices[SHAPE3D_WHEEL_SECOND_AXIS].z - vertices[0].z) * sine;
				other[vertex].x =
					points[vertex].x + vertices[SHAPE3D_WHEEL_RIM_VERTEX_COUNT].x - vertices[0].x;
				other[vertex].z =
					points[vertex].z + vertices[SHAPE3D_WHEEL_RIM_VERTEX_COUNT].z - vertices[0].z;
			}
			shadow_silhouette_polygon(silhouette, points, SHADOW_SILHOUETTE_ROUND_POINTS,
									  rasterize);
			shadow_silhouette_polygon(silhouette, other, SHADOW_SILHOUETTE_ROUND_POINTS, rasterize);
			for (legacy_s32 vertex = 0; vertex < SHADOW_SILHOUETTE_ROUND_POINTS; vertex++) {
				legacy_s32 next = (vertex + 1) % SHADOW_SILHOUETTE_ROUND_POINTS;
				struct SHADOW_SILHOUETTE_POINT side[4] = {points[vertex], points[next], other[next],
														  other[vertex]};
				shadow_silhouette_polygon(silhouette, side, 4, rasterize);
			}
		} else if (type == SHAPE3D_PRIMITIVE_SPHERE) {
			legacy_f64 dx = vertices[1].x - vertices[0].x;
			legacy_f64 dy = vertices[1].y - vertices[0].y;
			legacy_f64 dz = vertices[1].z - vertices[0].z;
			legacy_f64 radius = SDL_sqrt(dx * dx + dy * dy + dz * dz);
			if (!rasterize && vertices[0].y + radius > silhouette->height) {
				silhouette->height = vertices[0].y + radius;
			}
			struct SHADOW_SILHOUETTE_POINT center =
				shadow_project_point(silhouette, vertices[0].x, vertices[0].y, vertices[0].z);
			legacy_f64 axis_x = radius, axis_z = radius, skew = 0;
			legacy_s32 count = SHADOW_SILHOUETTE_ROUND_POINTS;
			if (silhouette->projection.enabled) {
				/* The rendered sphere's endpoint encodes its diameter. Project its
				 * vertical radius as well, producing an ellipse that reaches contact. */
				legacy_f64 horizontal = radius * HIRES_SPHERE_HORIZONTAL_SCALE;
				legacy_f64 vertical = radius * silhouette->projection.sphere_vertical;
				legacy_f64 x = vertical * silhouette->projection.x;
				legacy_f64 z = vertical * silhouette->projection.z;
				axis_x = SDL_sqrt(horizontal * horizontal + x * x);
				skew = axis_x > 0 ? x * z / axis_x : 0;
				axis_z = SDL_sqrt(SDL_max(0, horizontal * horizontal + z * z - skew * skew));
				count = HIRES_ROUND_POINTS;
			}
			for (legacy_s32 vertex = 0; vertex < count; vertex++) {
				legacy_s16 angle = (legacy_s16)(vertex * ANGLE_FULL_TURN / count);
				legacy_f64 cosine = cos_fast(angle) / (legacy_f64)TRIG_FIXED_ONE;
				legacy_f64 sine = sin_fast(angle) / (legacy_f64)TRIG_FIXED_ONE;
				points[vertex].x = center.x + axis_x * cosine;
				points[vertex].z = center.z + skew * cosine + axis_z * sine;
			}
			shadow_silhouette_polygon(silhouette, points, count, rasterize);
		}
	}
}

static legacy_s32 shadow_silhouette_build(struct SHADOW_SILHOUETTE *silhouette,
										  const struct SHAPE3D *shape,
										  const struct SHADOW_PROJECTION *projection)
{
	memset(silhouette, 0, sizeof(*silhouette));
	silhouette->projection = *projection;
	if (shape == NULL || shape->shape3d_numverts == 0 || shape->shape3d_numprimitives == 0) {
		return 0;
	}
	silhouette->min_x = silhouette->min_z = SHADOW_SILHOUETTE_INITIAL_BOUND;
	silhouette->max_x = silhouette->max_z = -SHADOW_SILHOUETTE_INITIAL_BOUND;
	shadow_silhouette_primitives(silhouette, shape, 0);
	legacy_f64 width = silhouette->max_x - silhouette->min_x;
	legacy_f64 length = silhouette->max_z - silhouette->min_z;
	if (width <= 0 || length <= 0) {
		return 0;
	}
	/* Leave clear texels around the model so bilinear filtering softens only
	 * the silhouette edge and cannot wrap onto the opposite side. */
	silhouette->scale_x = (SHADOW_SILHOUETTE_WIDTH - 2 * SHADOW_SILHOUETTE_BORDER_TEXELS) / width;
	silhouette->scale_z = (SHADOW_SILHOUETTE_LENGTH - 2 * SHADOW_SILHOUETTE_BORDER_TEXELS) / length;
	silhouette->min_x -= SHADOW_SILHOUETTE_BORDER_TEXELS / silhouette->scale_x;
	silhouette->max_x += SHADOW_SILHOUETTE_BORDER_TEXELS / silhouette->scale_x;
	silhouette->min_z -= SHADOW_SILHOUETTE_BORDER_TEXELS / silhouette->scale_z;
	silhouette->max_z += SHADOW_SILHOUETTE_BORDER_TEXELS / silhouette->scale_z;
	shadow_silhouette_primitives(silhouette, shape, 1);
	silhouette->shape = shape;
	silhouette->vertex_bytes = shape->shape3d_vertex_bytes;
	return 1;
}

legacy_f64 shape3d_hires_shadow_silhouette_sample(const struct SHADOW_SILHOUETTE *silhouette,
												  legacy_f64 x, legacy_f64 z)
{
	x = (x - silhouette->min_x) * silhouette->scale_x - 0.5;
	z = (z - silhouette->min_z) * silhouette->scale_z - 0.5;
	if (x < 0 || z < 0 || x >= SHADOW_SILHOUETTE_WIDTH - 1 || z >= SHADOW_SILHOUETTE_LENGTH - 1) {
		return 0;
	}
	legacy_s32 column = (legacy_s32)x;
	legacy_s32 row = (legacy_s32)z;
	x -= column;
	z -= row;
	const legacy_u8 *pixels = &silhouette->pixels[row * SHADOW_SILHOUETTE_WIDTH + column];
	legacy_f64 first = pixels[0] + (pixels[1] - pixels[0]) * x;
	legacy_f64 last = pixels[SHADOW_SILHOUETTE_WIDTH] +
					  (pixels[SHADOW_SILHOUETTE_WIDTH + 1] - pixels[SHADOW_SILHOUETTE_WIDTH]) * x;
	return (first + (last - first) * z) / LEGACY_U8_MAX;
}

static struct SHADOW_SILHOUETTE shadow_silhouettes[HIRES_CAR_SHADOW_COUNT];

void shape3d_hires_ground_begin(const struct VECTOR *camera_position)
{
	shape3d_hires_state.ground_enabled = 0;
	if (!hires_enabled() || camera_position->y <= 0 || projection_focal_length_x == 0 ||
		projection_focal_length_y == 0) {
		return;
	}
	/* Only the world-Y row of the inverse view is needed. Use the actual
	 * fixed-point matrix rather than assuming its rounded rows are orthogonal. */
	legacy_f64 a = mat_temp.m._11, b = mat_temp.m._12, c = mat_temp.m._13;
	legacy_f64 d = mat_temp.m._21, e = mat_temp.m._22, f = mat_temp.m._23;
	legacy_f64 g = mat_temp.m._31, h = mat_temp.m._32, i = mat_temp.m._33;
	legacy_f64 determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	if (determinant == 0) {
		return;
	}
	legacy_f64 inverse_scale = -TRIG_FIXED_ONE / (determinant * camera_position->y);
	legacy_f64 scale = hires_render_scale();
	shape3d_hires_state.ground_plane.x_step =
		(f * g - d * i) * inverse_scale / (projection_focal_length_x * scale);
	shape3d_hires_state.ground_plane.y_step =
		-(a * i - c * g) * inverse_scale / (projection_focal_length_y * scale);
	shape3d_hires_state.ground_plane.origin =
		(c * d - a * f) * inverse_scale +
		(HIRES_SAMPLE_CENTER_OFFSET - (legacy_s16)projection_center_x * scale) *
			shape3d_hires_state.ground_plane.x_step +
		(HIRES_SAMPLE_CENTER_OFFSET - (legacy_s16)projection_center_y * scale) *
			shape3d_hires_state.ground_plane.y_step;
	shape3d_hires_state.ground_enabled = 1;
}

void shape3d_hires_set_shadow_light(legacy_s16 heading, legacy_f64 cotangent)
{
	shape3d_hires_state.shadow_light_x = sin_fast((legacy_u16)heading) / (legacy_f64)TRIG_FIXED_ONE;
	shape3d_hires_state.shadow_light_z = cos_fast((legacy_u16)heading) / (legacy_f64)TRIG_FIXED_ONE;
	shape3d_hires_state.shadow_light_cotangent = SDL_max(0, cotangent);
}

void shape3d_hires_shadows_begin(const struct VECTOR *camera_position)
{
	/* Each scene starts with the racing sun, including after leaving a menu. */
	shape3d_hires_state.shadow_light_x = 0;
	shape3d_hires_state.shadow_light_z = 1;
	shape3d_hires_state.shadow_light_cotangent = HIRES_CAR_SHADOW_SUN_COTANGENT;
	shape3d_hires_state.car_shadow_count = 0;
	shape3d_hires_state.shadow_model_pending = 0;
	shape3d_hires_state.shadow_surface_count = 0;
	if (!hires_enabled() || hires_render_scale() == HIRES_MINIMUM_SCALE) {
		return;
	}
	shape3d_hires_state.shadow_ground_y = -camera_position->y;
	shape3d_hires_state.shadow_view = mat_temp;
	/* Invert the fixed-point view exactly: transposing its rounded rotation
	 * can move a small distant footprint away from its supporting surface. */
	legacy_f64 a = shape3d_hires_state.shadow_view.m._11, b = shape3d_hires_state.shadow_view.m._12,
			   c = shape3d_hires_state.shadow_view.m._13;
	legacy_f64 d = shape3d_hires_state.shadow_view.m._21, e = shape3d_hires_state.shadow_view.m._22,
			   f = shape3d_hires_state.shadow_view.m._23;
	legacy_f64 g = shape3d_hires_state.shadow_view.m._31, h = shape3d_hires_state.shadow_view.m._32,
			   i = shape3d_hires_state.shadow_view.m._33;
	legacy_f64 determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	legacy_f64 scale = determinant != 0 ? TRIG_FIXED_ONE / determinant : 0;
	shape3d_hires_state.shadow_inverse[0][0] = (e * i - f * h) * scale;
	shape3d_hires_state.shadow_inverse[0][1] = (c * h - b * i) * scale;
	shape3d_hires_state.shadow_inverse[0][2] = (b * f - c * e) * scale;
	shape3d_hires_state.shadow_inverse[1][0] = (f * g - d * i) * scale;
	shape3d_hires_state.shadow_inverse[1][1] = (a * i - c * g) * scale;
	shape3d_hires_state.shadow_inverse[1][2] = (c * d - a * f) * scale;
	shape3d_hires_state.shadow_inverse[2][0] = (d * h - e * g) * scale;
	shape3d_hires_state.shadow_inverse[2][1] = (b * g - a * h) * scale;
	shape3d_hires_state.shadow_inverse[2][2] = (a * e - b * d) * scale;
}

void shape3d_hires_shadow_car(const struct VECTOR *relative_position, legacy_s16 heading,
							  legacy_s16 half_width, legacy_s16 half_length)
{
	shape3d_hires_state.shadow_model_pending = 0;
	if (!hires_enabled() || hires_render_scale() == HIRES_MINIMUM_SCALE ||
		shape3d_hires_state.car_shadow_count == HIRES_CAR_SHADOW_COUNT || half_width <= 0 ||
		half_length <= 0) {
		return;
	}
	struct HIRES_CAR_SHADOW *shadow =
		&shape3d_hires_state.car_shadows[shape3d_hires_state.car_shadow_count++];
	shadow->position = *relative_position;
	shadow->cosine = cos_fast((legacy_u16)heading) / (legacy_f64)TRIG_FIXED_ONE;
	shadow->sine = sin_fast((legacy_u16)heading) / (legacy_f64)TRIG_FIXED_ONE;
	shadow->half_width = half_width;
	shadow->half_length = half_length;
	shadow->min_x = -half_width;
	shadow->max_x = half_width;
	shadow->min_z = -half_length;
	shadow->max_z = half_length;
	shadow->height = SDL_min(half_width, half_length) * HIRES_CAR_SHADOW_FALLBACK_HEIGHT_SCALE;
	shadow->silhouette = NULL;
	shadow->projected = 0;
	shadow->surface_top = shadow->grille_top = -DBL_MAX;
	shadow_update_volume(shadow);
	shape3d_hires_state.shadow_model_pending = 1;
}

void shape3d_hires_shadow_models_reset(void)
{
	shape3d_hires_state.car_shadow_count = 0;
	shape3d_hires_state.shadow_model_pending = 0;
	shape3d_hires_state.shadow_surface_count = 0;
	for (legacy_s32 index = 0; index < HIRES_CAR_SHADOW_COUNT; index++) {
		shadow_silhouettes[index].shape = NULL;
	}
}

static void shadow_attach_model(const struct SHAPE3D *shape, legacy_s16 ground_height,
								legacy_s32 projected)
{
	if (!shape3d_hires_state.shadow_model_pending || shape == NULL) {
		return;
	}
	shape3d_hires_state.shadow_model_pending = 0;
	if (!hires_enabled() || hires_render_scale() == HIRES_MINIMUM_SCALE) {
		return;
	}
	struct HIRES_CAR_SHADOW *shadow =
		&shape3d_hires_state.car_shadows[shape3d_hires_state.car_shadow_count - 1];
	struct SHADOW_SILHOUETTE *silhouette =
		&shadow_silhouettes[shape3d_hires_state.car_shadow_count - 1];
	struct SHADOW_PROJECTION projection = {0};
	if (projected) {
		projection.x = (shape3d_hires_state.shadow_light_x * shadow->cosine -
						shape3d_hires_state.shadow_light_z * shadow->sine) *
					   shape3d_hires_state.shadow_light_cotangent;
		projection.z = (shape3d_hires_state.shadow_light_x * shadow->sine +
						shape3d_hires_state.shadow_light_z * shadow->cosine) *
					   shape3d_hires_state.shadow_light_cotangent;
		projection.sphere_vertical =
			projection_focal_length_x != 0 && projection_focal_length_y != 0
				? HIRES_SPHERE_VERTICAL_SCALE * projection_focal_length_x /
					  projection_focal_length_y
				: HIRES_SPHERE_HORIZONTAL_SCALE;
		projection.ground_height = ground_height;
		projection.enabled = 1;
	}
	const struct SHADOW_PROJECTION *cached = &silhouette->projection;
	if (silhouette->shape != shape || silhouette->vertex_bytes != shape->shape3d_vertex_bytes ||
		cached->enabled != projection.enabled ||
		cached->ground_height != projection.ground_height || cached->x != projection.x ||
		cached->z != projection.z || cached->sphere_vertical != projection.sphere_vertical) {
		if (!shadow_silhouette_build(silhouette, shape, &projection)) {
			return;
		}
		silhouette->shape = shape;
		silhouette->vertex_bytes = shape->shape3d_vertex_bytes;
	}
	shadow->silhouette = silhouette;
	shadow->projected = projected;
	shadow->min_x = silhouette->min_x;
	shadow->max_x = silhouette->max_x;
	shadow->min_z = silhouette->min_z;
	shadow->max_z = silhouette->max_z;
	shadow->height = silhouette->height;
	shadow->half_width = (silhouette->max_x - silhouette->min_x) * 0.5;
	shadow->half_length = (silhouette->max_z - silhouette->min_z) * 0.5;
	shadow_update_volume(shadow);
}

void shape3d_hires_shadow_model(const struct SHAPE3D *shape)
{
	shadow_attach_model(shape, 0, 0);
}

void shape3d_hires_shadow_projected_model(const struct SHAPE3D *shape, legacy_s16 ground_height)
{
	shadow_attach_model(shape, ground_height, 1);
}

legacy_f64 shape3d_hires_shadow_light_offset(const struct HIRES_CAR_SHADOW *shadow, legacy_f64 gap)
{
	if (shadow->projected) {
		return 0;
	}
	/* Leave a visible lean even on the ground, limiting travel as the car rises. */
	legacy_f64 offset = (gap + shadow->height * HIRES_CAR_SHADOW_HEIGHT_SCALE) *
						shape3d_hires_state.shadow_light_cotangent;
	legacy_f64 limit =
		SDL_min(shadow->half_width, shadow->half_length) * HIRES_CAR_SHADOW_OFFSET_LIMIT_SCALE;
	return offset < limit ? offset : limit;
}

static void shadow_update_volume(struct HIRES_CAR_SHADOW *shadow)
{
	legacy_f64 extent_x = SDL_max(-shadow->min_x, shadow->max_x);
	legacy_f64 extent_z = SDL_max(-shadow->min_z, shadow->max_z);
	legacy_f64 radius_x = shape3d_hires_absolute_coordinate(shadow->cosine) * extent_x +
						  shape3d_hires_absolute_coordinate(shadow->sine) * extent_z;
	legacy_f64 radius_z = shape3d_hires_absolute_coordinate(shadow->sine) * extent_x +
						  shape3d_hires_absolute_coordinate(shadow->cosine) * extent_z;
	legacy_f64 extension = shape3d_hires_shadow_light_offset(shadow, HIRES_CAR_SHADOW_REACH);
	legacy_f64 extend_x = extension * shape3d_hires_state.shadow_light_x;
	legacy_f64 extend_z = extension * shape3d_hires_state.shadow_light_z;
	shadow->minimum = (struct SHAPE3D_HIRES_VECTOR){
		shadow->position.x - radius_x + SDL_min(0, extend_x),
		shadow->projected ? shadow->position.y - HIRES_CAR_SHADOW_RECEIVER_TOLERANCE
						  : SDL_max(shape3d_hires_state.shadow_ground_y,
									shadow->position.y - HIRES_CAR_SHADOW_REACH),
		shadow->position.z - radius_z + SDL_min(0, extend_z)};
	shadow->maximum =
		(struct SHAPE3D_HIRES_VECTOR){shadow->position.x + radius_x + SDL_max(0, extend_x),
									  shadow->position.y + HIRES_CAR_SHADOW_RECEIVER_TOLERANCE,
									  shadow->position.z + radius_z + SDL_max(0, extend_z)};
}

#endif
