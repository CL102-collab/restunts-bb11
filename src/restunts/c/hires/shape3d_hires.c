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

struct SHAPE3D_HIRES_STATE shape3d_hires_main_state = {
	.shadow_light_z = 1,
	.shadow_light_cotangent = HIRES_CAR_SHADOW_SUN_COTANGENT,
	.model_scale = 1,
};

legacy_s32 shape3d_hires_reserve_primitives(legacy_u32 index)
{
	if (index < shape3d_hires_state.primitive_capacity) {
		return 1;
	}
	size_t capacity = shape3d_hires_state.primitive_capacity != 0
						  ? shape3d_hires_state.primitive_capacity
					  : shape3d_hires_thread_state != NULL ? HIRES_GEOMETRY_INITIAL_PRIMITIVES
														   : HIRES_INITIAL_PRIMITIVE_CAPACITY;
	while (capacity <= index) {
		if (capacity > LEGACY_S32_MAX / 2U) {
			shape3d_hires_geometry_error(
				"HyperVision scene has too many shape3d_hires_state.primitives");
			return 0;
		}
		capacity *= 2U;
	}
	if (capacity > (size_t)-1 / sizeof(*shape3d_hires_state.primitives) ||
		capacity > (size_t)-1 / sizeof(*shape3d_hires_state.shapes)) {
		shape3d_hires_geometry_error("HyperVision scene exceeds addressable memory");
		return 0;
	}
	struct HIRES_PRIMITIVE *new_primitives = calloc(capacity, sizeof(*new_primitives));
	struct HIRES_SHAPE *new_shapes = calloc(capacity, sizeof(*new_shapes));
	if (new_primitives == NULL || new_shapes == NULL) {
		free(new_primitives);
		free(new_shapes);
		shape3d_hires_geometry_error("Cannot allocate HyperVision scene geometry");
		return 0;
	}
	if (shape3d_hires_state.primitive_capacity != 0) {
		memcpy(new_primitives, shape3d_hires_state.primitives,
			   (size_t)shape3d_hires_state.primitive_capacity * sizeof(*new_primitives));
		memcpy(new_shapes, shape3d_hires_state.shapes,
			   (size_t)shape3d_hires_state.primitive_capacity * sizeof(*new_shapes));
	}
	free(shape3d_hires_state.primitives);
	free(shape3d_hires_state.shapes);
	shape3d_hires_state.primitives = new_primitives;
	shape3d_hires_state.shapes = new_shapes;
	shape3d_hires_state.primitive_capacity = (legacy_u32)capacity;
	return 1;
}

static void hires_project_coordinates(legacy_f64 x, legacy_f64 y, legacy_f64 z,
									  struct SHAPE3D_HIRES_POINT *point)
{
	legacy_s32 scale = hires_render_scale();
	point->x = (legacy_s16)projection_center_x * scale + x * projection_focal_length_x * scale / z;
	point->y = (legacy_s16)projection_center_y * scale - y * projection_focal_length_y * scale / z;
	point->inverse_z = 1.0 / z;
}

void shape3d_hires_project(const struct SHAPE3D_HIRES_VECTOR *vector,
						   struct SHAPE3D_HIRES_POINT *point)
{
	hires_project_coordinates(vector->x, vector->y, vector->z > 0 ? vector->z : 1, point);
}

static legacy_u8 hires_point_clip_flags(const struct SHAPE3D_HIRES_POINT *point, legacy_f64 padding)
{
	legacy_s32 scale = hires_render_scale();
	legacy_u8 flags = 0;
	if (point->y < select_rect_rc.top * scale - padding) {
		flags |= SHAPE3D_RECT_CLIP_TOP;
	} else if (point->y >= (select_rect_rc.bottom + 1) * scale + padding) {
		flags |= SHAPE3D_RECT_CLIP_BOTTOM;
	}
	if (point->x < select_rect_rc.left * scale - padding) {
		flags |= SHAPE3D_RECT_CLIP_LEFT;
	} else if (point->x >= (select_rect_rc.right + 1) * scale + padding) {
		flags |= SHAPE3D_RECT_CLIP_RIGHT;
	}
	return flags;
}

legacy_u8 shape3d_hires_clip_flags(const struct SHAPE3D_HIRES_VECTOR *vector)
{
	/* Compare the camera-space vertex with the padded frustum planes. This
	 * avoids projecting the same vertex again merely to reject an instance.
	 * Keep a floating-point margin so rearranging the equation cannot discard
	 * a line or polygon lying exactly on a viewport boundary. */
	legacy_s32 scale = hires_render_scale();
	legacy_f64 padding = 0.5 + 0.5 / scale;
	legacy_f64 z = vector->z > 0 ? vector->z : 1;
	legacy_f64 x = vector->x * projection_focal_length_x;
	legacy_f64 y = vector->y * projection_focal_length_y;
	legacy_f64 center_x = (legacy_s16)projection_center_x;
	legacy_f64 center_y = (legacy_s16)projection_center_y;
	legacy_f64 left = (select_rect_rc.left - padding - center_x) * z;
	legacy_f64 right = (select_rect_rc.right + 1 + padding - center_x) * z;
	legacy_f64 top = (center_y - select_rect_rc.top + padding) * z;
	legacy_f64 bottom = (center_y - select_rect_rc.bottom - 1 - padding) * z;
	legacy_f64 margin_x =
		HIRES_CLIP_ROUNDING_MARGIN * DBL_EPSILON * (SDL_fabs(x) + SDL_fabs(left) + SDL_fabs(right));
	legacy_f64 margin_y =
		HIRES_CLIP_ROUNDING_MARGIN * DBL_EPSILON * (SDL_fabs(y) + SDL_fabs(top) + SDL_fabs(bottom));
	legacy_u8 flags = 0;
	if (y > top + margin_y) {
		flags |= SHAPE3D_RECT_CLIP_TOP;
	} else if (y < bottom - margin_y) {
		flags |= SHAPE3D_RECT_CLIP_BOTTOM;
	}
	if (x < left - margin_x) {
		flags |= SHAPE3D_RECT_CLIP_LEFT;
	} else if (x > right + margin_x) {
		flags |= SHAPE3D_RECT_CLIP_RIGHT;
	}
	return flags;
}

static legacy_s32 hires_polygon_faces_camera(const struct SHAPE3D_HIRES_POINT *points,
											 legacy_u32 count)
{
	/* Use every edge: the first three vertices can be collinear even when
	 * the complete polygon covers visible pixels at the higher resolution. */
	legacy_f64 area = 0;
	for (legacy_u32 index = 1; index + 1 < count; index++) {
		area += (points[index].x - points[0].x) * (points[index + 1].y - points[0].y) -
				(points[index].y - points[0].y) * (points[index + 1].x - points[0].x);
	}
	return area > 0;
}

static legacy_f64 hires_decal_width(legacy_f64 inverse_z)
{
	legacy_s32 scale = hires_render_scale();
	legacy_f64 minimum_width = SDL_min(HIRES_MIN_DECAL_WIDTH, scale);
	legacy_f64 width = HIRES_LINE_DIAMETER * shape3d_hires_state.model_scale *
					   projection_focal_length_x * scale * inverse_z;
	if (width >= scale) {
		return scale;
	}
	if (width <= minimum_width) {
		return minimum_width;
	}
	/* Reduce mid-range coverage while preserving the near cap and distant width floor. */
	legacy_f64 excess = width - minimum_width;
	legacy_f64 reduction = excess - excess * excess / (scale - minimum_width);
	width -= HIRES_DECAL_WIDTH_REDUCTION * reduction;
	return width > minimum_width ? width : minimum_width;
}

/* Measure the complete projected polygon across each edge normal. Resolvable
 * decals retain their nearby weight; tiny details receive no artificial border. */
static legacy_f64 hires_polygon_padding(const struct SHAPE3D_HIRES_POINT *points, legacy_u32 count,
										legacy_f64 *projected_width)
{
	legacy_s32 scale = hires_render_scale();
	legacy_f64 minimum_width = 0;
	legacy_s32 has_edge = 0;
	legacy_f64 nearest = points[0].inverse_z;
	legacy_f64 farthest = nearest;
	for (legacy_u32 edge = 0; edge < count; edge++) {
		const struct SHAPE3D_HIRES_POINT *first = &points[edge];
		const struct SHAPE3D_HIRES_POINT *last = &points[(edge + 1) % count];
		if (first->inverse_z > nearest) {
			nearest = first->inverse_z;
		}
		if (first->inverse_z < farthest) {
			farthest = first->inverse_z;
		}
		legacy_f64 dx = last->x - first->x;
		legacy_f64 dy = last->y - first->y;
		legacy_f64 length = SDL_sqrt(dx * dx + dy * dy);
		if (length == 0) {
			continue;
		}
		legacy_f64 minimum = 0;
		legacy_f64 maximum = 0;
		for (legacy_u32 point = 0; point < count; point++) {
			legacy_f64 distance =
				(points[point].x - first->x) * dy - (points[point].y - first->y) * dx;
			if (distance < minimum) {
				minimum = distance;
			}
			if (distance > maximum) {
				maximum = distance;
			}
		}
		legacy_f64 width = (maximum - minimum) / length;
		if (!has_edge || width < minimum_width) {
			minimum_width = width;
		}
		has_edge = 1;
	}
	*projected_width = minimum_width;
	if (minimum_width <= HIRES_DECAL_BORDER_LOGICAL_WIDTH * scale || minimum_width >= scale) {
		return 0;
	}
	legacy_f64 expansion = hires_decal_width(nearest) - minimum_width;
	/* A sloping dash can taper below a sample at its far end even when its
	 * near end is wide enough. Account for that narrowing before adding a border. */
	legacy_f64 far_expansion = hires_decal_width(farthest) - minimum_width * farthest / nearest;
	if (far_expansion > expansion) {
		expansion = far_expansion;
	}
	/* Keep the original nearby weight and a smooth transition to unexpanded surfaces. */
	if (expansion > scale - minimum_width) {
		expansion = scale - minimum_width;
	}
	return expansion > 0 ? expansion * 0.5 : 0;
}

legacy_s32 shape3d_hires_polygon_visible(legacy_u32 index, legacy_s32 cull_backface)
{
	if (index >= shape3d_hires_state.primitive_capacity) {
		return 0;
	}
	const struct HIRES_PRIMITIVE *primitive = &shape3d_hires_state.primitives[index];
	if (primitive->count < 3) {
		return 0;
	}
	legacy_u8 flags = SHAPE3D_ALL_RECT_CLIP_FLAGS;
	for (legacy_u32 point = 0; point < primitive->count; point++) {
		flags &= hires_point_clip_flags(&primitive->points[point], primitive->size);
	}
	return flags == 0 &&
		   (!cull_backface || hires_polygon_faces_camera(primitive->points, primitive->count));
}

void shape3d_hires_begin_shape(legacy_u32 index, legacy_s32 depth_mode)
{
	if (!shape3d_hires_reserve_primitives(index)) {
		return;
	}
	shape3d_hires_state.current_shape = index;
	shape3d_hires_state.current_family = index + 1;
	shape3d_hires_state.shapes[index].depth_mode = depth_mode;
	shape3d_hires_state.shapes[index].shadow_receiver = 1;
}

void shape3d_hires_set_shadow_receiver(legacy_s32 enabled)
{
	if (shape3d_hires_state.current_shape < shape3d_hires_state.primitive_capacity) {
		shape3d_hires_state.shapes[shape3d_hires_state.current_shape].shadow_receiver = enabled;
	}
}

void shape3d_hires_set_model_scale(legacy_f64 scale)
{
	shape3d_hires_state.model_scale = scale;
}

void shape3d_hires_reset(void)
{
	shape3d_hires_state.ground_enabled = 0;
	shape3d_hires_state.car_shadow_count = 0;
	shape3d_hires_state.shadow_model_pending = 0;
	shape3d_hires_state.shadow_surface_count = 0;
	shape3d_hires_state.model_scale = 1;
	shape3d_hires_begin_shape(0, SHAPE3D_HIRES_DEPTH_SORTED);
	shape3d_hires_state.rendered_depth_valid = 0;
	for (legacy_u32 index = 0; index < shape3d_hires_state.primitive_count; index++) {
		shape3d_hires_state.primitives[index].count = 0;
	}
	shape3d_hires_state.primitive_count = 0;
}

void shape3d_hires_project_intersection(const struct SHAPE3D_HIRES_VECTOR *first,
										const struct SHAPE3D_HIRES_VECTOR *second,
										struct SHAPE3D_HIRES_POINT *point)
{
	/* Evaluate shared edges in the same direction so clipping neighboring
	 * polygons cannot round the same intersection to opposite pixel sides. */
	if (first->z > second->z) {
		const struct SHAPE3D_HIRES_VECTOR *temporary = first;
		first = second;
		second = temporary;
	}
	/* Near-plane intersections need the same subpixel precision as ordinary
	 * vertices; rounding back into a legacy VECTOR would discard it. */
	/* Keep the interpolation numerator until projection. Rounding an intermediate
	 * world coordinate to binary64 can move an exact pixel-center intersection. */
	legacy_f64 depth_difference = second->z - first->z;
	legacy_f64 near_difference = HIRES_NEAR_CLIP_Z - first->z;
	hires_project_coordinates(
		first->x * depth_difference + (second->x - first->x) * near_difference,
		first->y * depth_difference + (second->y - first->y) * near_difference,
		HIRES_NEAR_CLIP_Z * depth_difference, point);
	point->inverse_z = 1.0 / HIRES_NEAR_CLIP_Z;
}

static void hires_queue_polygon(struct HIRES_PRIMITIVE *primitive, legacy_u32 count,
								const legacy_u8 *indices,
								const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	const struct SHAPE3D_HIRES_VECTOR *previous = &vertices[indices[count - 1]];
	for (legacy_u32 index = 0; index < count; index++) {
		const struct SHAPE3D_HIRES_VECTOR *current = &vertices[indices[index]];
		if ((previous->z < HIRES_NEAR_CLIP_Z) != (current->z < HIRES_NEAR_CLIP_Z)) {
			shape3d_hires_project_intersection(previous, current,
											   &primitive->points[primitive->count++]);
		}
		if (current->z >= HIRES_NEAR_CLIP_Z) {
			shape3d_hires_project(current, &primitive->points[primitive->count++]);
		}
		previous = current;
	}
}

static void hires_queue_primitive(legacy_u32 index, legacy_u8 type, legacy_u16 vertex_count,
								  const legacy_u8 *indices,
								  const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	legacy_s32 scale = hires_render_scale();
	if (!shape3d_hires_reserve_primitives(index)) {
		return;
	}
	if (shape3d_hires_state.primitive_count <= index) {
		shape3d_hires_state.primitive_count = index + 1U;
	}
	struct HIRES_PRIMITIVE *primitive = &shape3d_hires_state.primitives[index];
	primitive->count = 0;
	primitive->size = 0;
	primitive->shadow_surface = 0;
	primitive->round_points = HIRES_ROUND_POINTS;
	if (!hires_enabled() || vertex_count == 0 || vertex_count > HIRES_MAX_POLYGON_POINTS / 2) {
		return;
	}
	if (type == RENDER_PRIMITIVE_POLYGON) {
		hires_queue_polygon(primitive, vertex_count, indices, vertices);
		return;
	}
	if (type == RENDER_PRIMITIVE_WHEEL) {
		struct SHAPE3D_HIRES_POINT face[SHAPE3D_WHEEL_RIM_VERTEX_COUNT];
		for (legacy_u32 vertex = 0; vertex < SHAPE3D_WHEEL_RIM_VERTEX_COUNT; vertex++) {
			shape3d_hires_project(&vertices[indices[vertex]], &face[vertex]);
		}
		legacy_u32 start = hires_polygon_faces_camera(face, SHAPE3D_WHEEL_RIM_VERTEX_COUNT)
							   ? 0
							   : SHAPE3D_WHEEL_RIM_VERTEX_COUNT;
		for (legacy_u32 vertex = 0; vertex < 4; vertex++) {
			shape3d_hires_project(&vertices[indices[(start + vertex) % 6]],
								  &primitive->points[vertex]);
		}
		primitive->count = 4;
		return;
	}
	for (legacy_u32 vertex = 0; vertex < vertex_count; vertex++) {
		const struct SHAPE3D_HIRES_VECTOR *current = &vertices[indices[vertex]];
		if (type == RENDER_PRIMITIVE_LINE && current->z < HIRES_NEAR_CLIP_Z) {
			shape3d_hires_project_intersection(current, &vertices[indices[1 - vertex]],
											   &primitive->points[vertex]);
		} else {
			shape3d_hires_project(current, &primitive->points[vertex]);
		}
	}
	primitive->count = vertex_count;
	if (type == RENDER_PRIMITIVE_LINE) {
		/* Capture projection and authored model units with the queued geometry. */
		primitive->size = HIRES_LINE_DIAMETER * shape3d_hires_state.model_scale *
						  projection_focal_length_x * scale;
	}
	if (type == RENDER_PRIMITIVE_SPHERE) {
		const struct SHAPE3D_HIRES_VECTOR *center = &vertices[indices[0]];
		const struct SHAPE3D_HIRES_VECTOR *endpoint = &vertices[indices[1]];
		legacy_f64 radius_x = center->x - endpoint->x;
		legacy_f64 radius_y = center->y - endpoint->y;
		legacy_f64 radius_z = center->z - endpoint->z;
		legacy_f64 radius =
			SDL_sqrt(radius_x * radius_x + radius_y * radius_y + radius_z * radius_z);
		primitive->size = projection_focal_length_x * radius * scale / center->z;
	}
}

static legacy_u32 hires_round_point_count(legacy_f64 radius_squared)
{
	/* The squared sum of both projected axes bounds the squared ellipse
	 * radius, including skewed wheels. At radii 3, 12 and 48 the existing
	 * fixed-point perimeter deviates from 8, 16 and 32 chords by less than
	 * one quarter of a high-resolution pixel. All levels share its angles. */
	if (radius_squared <= HIRES_ROUND_SMALL_RADIUS * HIRES_ROUND_SMALL_RADIUS) {
		return HIRES_ROUND_SMALL_POINTS;
	}
	if (radius_squared <= HIRES_ROUND_MEDIUM_RADIUS * HIRES_ROUND_MEDIUM_RADIUS) {
		return HIRES_ROUND_MEDIUM_POINTS;
	}
	if (radius_squared <= HIRES_ROUND_LARGE_RADIUS * HIRES_ROUND_LARGE_RADIUS) {
		return HIRES_ROUND_LARGE_POINTS;
	}
	return HIRES_ROUND_POINTS;
}

void shape3d_hires_queue(legacy_u32 index, legacy_u8 type, legacy_u16 vertex_count,
						 const legacy_u8 *indices, const struct SHAPE3D_HIRES_VECTOR *vertices,
						 legacy_u16 flags)
{
	hires_queue_primitive(index, type, vertex_count, indices, vertices);
	if (index >= shape3d_hires_state.primitive_capacity ||
		shape3d_hires_state.primitives[index].count == 0) {
		return;
	}
	struct HIRES_PRIMITIVE *primitive = &shape3d_hires_state.primitives[index];
	primitive->shape = shape3d_hires_state.current_shape;
	if (shape3d_hires_state.shapes[shape3d_hires_state.current_shape].depth_mode !=
		SHAPE3D_HIRES_DEPTH_BACKGROUND) {
		if (type == RENDER_PRIMITIVE_WHEEL) {
			legacy_f64 first_x = primitive->points[1].x - primitive->points[0].x;
			legacy_f64 first_y = primitive->points[1].y - primitive->points[0].y;
			legacy_f64 second_x = primitive->points[2].x - primitive->points[0].x;
			legacy_f64 second_y = primitive->points[2].y - primitive->points[0].y;
			/* The inner rim is smaller; the tread's far perimeter is translated. */
			primitive->round_points = hires_round_point_count(
				first_x * first_x + first_y * first_y + second_x * second_x + second_y * second_y);
		} else if (type == RENDER_PRIMITIVE_SPHERE) {
			legacy_f64 horizontal = primitive->size * HIRES_SPHERE_HORIZONTAL_SCALE;
			legacy_f64 vertical = primitive->size * HIRES_SPHERE_VERTICAL_SCALE;
			primitive->round_points =
				hires_round_point_count(horizontal * horizontal + vertical * vertical);
		}
	}
	primitive->attached = (flags & SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) != 0;
	if (type == RENDER_PRIMITIVE_POLYGON && primitive->attached) {
		legacy_f64 projected_width;
		primitive->size =
			hires_polygon_padding(primitive->points, primitive->count, &projected_width);
		if (projected_width < HIRES_DECAL_CULL_LOGICAL_WIDTH * hires_render_scale()) {
			primitive->count = 0;
		}
	}
	if (!primitive->attached) {
		shape3d_hires_state.current_family = index + 1;
	}
	primitive->family = shape3d_hires_state.current_family;
}

legacy_s32 shape3d_hires_ceil_coordinate(legacy_f64 coordinate)
{
	legacy_s32 result = (legacy_s32)coordinate;
	return result < coordinate ? result + 1 : result;
}

legacy_f64 shape3d_hires_absolute_coordinate(legacy_f64 value)
{
	return value < 0 ? -value : value;
}

void shape3d_hires_update_bounds(legacy_u32 index, legacy_u8 type, struct RECTANGLE *rectangle)
{
	legacy_s32 scale = hires_render_scale();
	legacy_s32 width = hires_render_width();
	legacy_s32 height = hires_render_height();
	if (index >= shape3d_hires_state.primitive_capacity ||
		shape3d_hires_state.primitives[index].count == 0) {
		return;
	}
	const struct HIRES_PRIMITIVE *primitive = &shape3d_hires_state.primitives[index];
	legacy_f64 minimum_x = primitive->points[0].x;
	legacy_f64 maximum_x = minimum_x;
	legacy_f64 minimum_y = primitive->points[0].y;
	legacy_f64 maximum_y = minimum_y;
	if (type == RENDER_PRIMITIVE_SPHERE) {
		minimum_x -= primitive->size * HIRES_SPHERE_HORIZONTAL_SCALE;
		maximum_x += primitive->size * HIRES_SPHERE_HORIZONTAL_SCALE;
		minimum_y -= primitive->size * HIRES_SPHERE_VERTICAL_SCALE;
		maximum_y += primitive->size * HIRES_SPHERE_VERTICAL_SCALE;
	} else if (type == RENDER_PRIMITIVE_WHEEL) {
		/* The sum of the two axis magnitudes bounds every perimeter point,
		 * including tilted ellipses and the far edge of the tread. */
		legacy_f64 width = shape3d_hires_absolute_coordinate(primitive->points[1].x - minimum_x) +
						   shape3d_hires_absolute_coordinate(primitive->points[2].x - minimum_x);
		legacy_f64 height = shape3d_hires_absolute_coordinate(primitive->points[1].y - minimum_y) +
							shape3d_hires_absolute_coordinate(primitive->points[2].y - minimum_y);
		if (primitive->points[3].x < minimum_x) {
			minimum_x = primitive->points[3].x;
		} else {
			maximum_x = primitive->points[3].x;
		}
		if (primitive->points[3].y < minimum_y) {
			minimum_y = primitive->points[3].y;
		} else {
			maximum_y = primitive->points[3].y;
		}
		minimum_x -= width;
		maximum_x += width;
		minimum_y -= height;
		maximum_y += height;
	} else {
		for (legacy_u32 point = 1; point < primitive->count; point++) {
			if (primitive->points[point].x < minimum_x) {
				minimum_x = primitive->points[point].x;
			}
			if (primitive->points[point].x > maximum_x) {
				maximum_x = primitive->points[point].x;
			}
			if (primitive->points[point].y < minimum_y) {
				minimum_y = primitive->points[point].y;
			}
			if (primitive->points[point].y > maximum_y) {
				maximum_y = primitive->points[point].y;
			}
		}
	}
	if (type == RENDER_PRIMITIVE_LINE || type == RENDER_PRIMITIVE_POLYGON) {
		/* Include the maximum stroke and rounding to its nearest sample
		 * in the sprite copy rectangle. */
		legacy_f64 padding = type == RENDER_PRIMITIVE_LINE ? scale / 2.0 + 0.5 : primitive->size;
		minimum_x -= padding;
		maximum_x += padding;
		minimum_y -= padding;
		maximum_y += padding;
	}
	if (maximum_x < 0 || minimum_x >= width || maximum_y < 0 || minimum_y >= height) {
		return;
	}
	legacy_s32 left = minimum_x <= 0 ? 0 : (legacy_s32)(minimum_x / scale);
	legacy_s32 top = minimum_y <= 0 ? 0 : (legacy_s32)(minimum_y / scale);
	legacy_s32 right = maximum_x >= width - scale
						   ? width / scale
						   : shape3d_hires_ceil_coordinate(maximum_x / scale) + 1;
	legacy_s32 bottom = maximum_y >= height - scale
							? height / scale
							: shape3d_hires_ceil_coordinate(maximum_y / scale) + 1;
	if (left < rectangle->left) {
		rectangle->left = (legacy_s16)left;
	}
	if (right > rectangle->right) {
		rectangle->right = (legacy_s16)right;
	}
	if (top < rectangle->top) {
		rectangle->top = (legacy_s16)top;
	}
	if (bottom > rectangle->bottom) {
		rectangle->bottom = (legacy_s16)bottom;
	}
}

#endif
