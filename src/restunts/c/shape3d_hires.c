#include "shape3d_hires.h"

#if defined(RESTUNTS_SDL3)

#include <SDL3/SDL_stdinc.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include "fatal.h"
#include "hires.h"
#include "hypervision.h"
#include "projection.h"
#include "render_workers.h"
#include "shape3d_internal.h"

#define HIRES_NEAR_CLIP_Z 12
#define HIRES_CLIP_ROUNDING_MARGIN 8
/* Keep the original displayed pixel width through medium-close views,
 * then let perspective narrow the stroke at greater distances. */
#define HIRES_LINE_DIAMETER 1.5
/* Subpixel decals can miss pixel centers at some positions and orientations. */
#define HIRES_MIN_DECAL_WIDTH 0.75
#define HIRES_DECAL_WIDTH_REDUCTION 2
#define HIRES_MAX_POLYGON_POINTS 20
#define HIRES_ROUND_POINTS 64
#define HIRES_ROUND_SMALL_POINTS 8U
#define HIRES_ROUND_MEDIUM_POINTS 16U
#define HIRES_ROUND_LARGE_POINTS 32U
#define HIRES_ROUND_SMALL_RADIUS 3.0
#define HIRES_ROUND_MEDIUM_RADIUS 12.0
#define HIRES_ROUND_LARGE_RADIUS 48.0
#define HIRES_SPHERE_HORIZONTAL_SCALE 0.5
#define HIRES_SPHERE_VERTICAL_SCALE (13.0 / 32.0)
#define HIRES_WHEEL_INNER_SCALE (9472.0 / TRIG_FIXED_ONE)

struct HIRES_PRIMITIVE {
	struct SHAPE3D_HIRES_POINT points[HIRES_MAX_POLYGON_POINTS];
	legacy_u32 count;
	legacy_u32 wheel_face;
	legacy_u32 round_points;
	legacy_u32 shape;
	legacy_u32 family;
	legacy_u32 shadow_surface;
	legacy_s32 attached;
	legacy_f64 size;
};

struct HIRES_PAINT {
	struct HYPERVISION_MATERIAL material;
	legacy_s32 scale;
};

struct HIRES_SHAPE {
	legacy_s32 depth_mode;
	legacy_s32 shadow_receiver;
};

#define HIRES_CAR_SHADOW_COUNT 2
#define HIRES_CAR_SHADOW_REACH 256.0
#define HIRES_CAR_SHADOW_HEIGHT_SCALE 0.6
/* Cotangent of the 70-degree sun elevation; preserve the authored approximation. */
#define HIRES_CAR_SHADOW_SUN_COTANGENT 0.363970
#define HIRES_CAR_SHADOW_OFFSET_LIMIT_SCALE 0.7
#define HIRES_CAR_SHADOW_RECEIVER_TOLERANCE 0.5
#define HIRES_CAR_SHADOW_SHRINK_HEIGHT 128
#define HIRES_CAR_SHADOW_FALLBACK_HEIGHT_SCALE 0.5
#define HIRES_CAR_SHADOW_FALLBACK_DIAGONAL 1.7
#define HIRES_CAR_SHADOW_FALLBACK_EDGE_SCALE 8
#define HIRES_CAR_SHADOW_FADE_HEIGHT 64
#define HIRES_CAR_SHADOW_MAX_OPACITY 96
#define HIRES_CAR_SHADOW_BOUNDS_CORNERS 8
#define HIRES_CAR_SHADOW_BOUNDS_EDGES 12
#define HIRES_CAR_SHADOW_CORNER_MAX_X 1
#define HIRES_CAR_SHADOW_CORNER_MAX_Z 2
#define HIRES_CAR_SHADOW_CORNER_BOTTOM 4
#define HIRES_SHADOW_SURFACE_INITIAL_CAPACITY 32U
#define HIRES_SHADOW_SURFACE_MAX_POINTS SHAPE3D_POLYGON_MAX_VERTICES
#define HIRES_SHADOW_GRILLE_TRANSMISSION 0.5
#define HIRES_SHADOW_SURFACE_MAX_HITS 4U
#define HIRES_SHADOW_INTERSECTION_EPSILON 1e-9

struct HIRES_CAR_SHADOW {
	struct VECTOR position;
	legacy_f64 cosine, sine;
	legacy_f64 half_width, half_length;
	legacy_f64 min_x, max_x, min_z, max_z, height;
	legacy_s32 projected;
	const struct SHADOW_SILHOUETTE *silhouette;
	struct SHAPE3D_HIRES_VECTOR minimum, maximum;
	legacy_f64 surface_top, grille_top;
};

static struct HIRES_CAR_SHADOW car_shadows[HIRES_CAR_SHADOW_COUNT];
static legacy_s32 car_shadow_count;
static legacy_s32 shadow_model_pending;
static struct MATRIX shadow_view;
static legacy_f64 shadow_inverse[3][3];
static legacy_f64 shadow_ground_y;
static legacy_f64 shadow_light_x, shadow_light_z = 1;
static legacy_f64 shadow_light_cotangent = HIRES_CAR_SHADOW_SUN_COTANGENT;

struct SHADOW_SURFACE {
	struct SHAPE3D_HIRES_VECTOR vertices[HIRES_SHADOW_SURFACE_MAX_POINTS];
	struct SHAPE3D_HIRES_VECTOR minimum, maximum, normal;
	legacy_f64 distance;
	legacy_u32 count, cars, rendered_index;
	legacy_s32 grille, axis;
};

static struct SHADOW_SURFACE *shadow_surfaces;
static legacy_f64 *shadow_grille_hits;
static size_t shadow_surface_count, shadow_surface_capacity;
static void shadow_update_volume(struct HIRES_CAR_SHADOW *shadow);

static struct HIRES_PRIMITIVE *primitives;
static struct HIRES_SHAPE *shapes;
static legacy_u32 primitive_capacity;
static legacy_u32 primitive_count;
static legacy_u32 current_shape;
static legacy_u32 current_family;
static legacy_s32 rendered_depth_valid;
static legacy_u32 rendered_generation;
static legacy_f64 model_scale = 1;
static struct HIRES_DEPTH_PLANE ground_plane;
static legacy_s32 ground_enabled;

static legacy_s32 batching;

static void reserve_primitives(legacy_u32 index)
{
	if (index < primitive_capacity) {
		return;
	}
	size_t capacity =
		primitive_capacity != 0 ? primitive_capacity : POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY;
	while (capacity <= index) {
		if (capacity > LEGACY_S32_MAX / 2U) {
			fatal_error("HyperVision scene has too many primitives");
			return;
		}
		capacity *= 2U;
	}
	if (capacity > (size_t)-1 / sizeof(*primitives) || capacity > (size_t)-1 / sizeof(*shapes)) {
		fatal_error("HyperVision scene exceeds addressable memory");
		return;
	}
	struct HIRES_PRIMITIVE *new_primitives = calloc(capacity, sizeof(*new_primitives));
	struct HIRES_SHAPE *new_shapes = calloc(capacity, sizeof(*new_shapes));
	if (new_primitives == NULL || new_shapes == NULL) {
		fatal_error("Cannot allocate HyperVision scene geometry");
		return;
	}
	if (primitive_capacity != 0) {
		memcpy(new_primitives, primitives, (size_t)primitive_capacity * sizeof(*new_primitives));
		memcpy(new_shapes, shapes, (size_t)primitive_capacity * sizeof(*new_shapes));
	}
	free(primitives);
	free(shapes);
	primitives = new_primitives;
	shapes = new_shapes;
	primitive_capacity = (legacy_u32)capacity;
}

static void project_coordinates(legacy_f64 x, legacy_f64 y, legacy_f64 z,
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
	project_coordinates(vector->x, vector->y, vector->z > 0 ? vector->z : 1, point);
}

static legacy_u8 point_clip_flags(const struct SHAPE3D_HIRES_POINT *point, legacy_f64 padding)
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

static legacy_s32 polygon_faces_camera(const struct SHAPE3D_HIRES_POINT *points, legacy_u32 count)
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

static legacy_f64 decal_width(legacy_f64 inverse_z)
{
	legacy_s32 scale = hires_render_scale();
	legacy_f64 minimum_width = SDL_min(HIRES_MIN_DECAL_WIDTH, scale);
	legacy_f64 width =
		HIRES_LINE_DIAMETER * model_scale * projection_focal_length_x * scale * inverse_z;
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

/* Measure the complete projected polygon across each edge normal. The coverage
 * target follows perspective, with a subpixel minimum width for distant decals. */
static legacy_f64 polygon_padding(const struct SHAPE3D_HIRES_POINT *points, legacy_u32 count)
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
	if (minimum_width >= scale) {
		return 0;
	}
	legacy_f64 expansion = decal_width(nearest) - minimum_width;
	/* A sloping dash can taper below a sample at its far end even when its
	 * near end is wide enough. Account for that narrowing before adding a border. */
	legacy_f64 far_expansion = decal_width(farthest) - minimum_width * farthest / nearest;
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
	if (index >= primitive_capacity) {
		return 0;
	}
	const struct HIRES_PRIMITIVE *primitive = &primitives[index];
	if (primitive->count < 3) {
		return 0;
	}
	legacy_u8 flags = SHAPE3D_ALL_RECT_CLIP_FLAGS;
	for (legacy_u32 point = 0; point < primitive->count; point++) {
		flags &= point_clip_flags(&primitive->points[point], primitive->size);
	}
	return flags == 0 &&
		   (!cull_backface || polygon_faces_camera(primitive->points, primitive->count));
}

legacy_u32 shape3d_hires_wheel_face(legacy_u32 index)
{
	return index < primitive_capacity ? primitives[index].wheel_face : 0;
}

void shape3d_hires_begin_shape(legacy_u32 index, legacy_s32 depth_mode)
{
	reserve_primitives(index);
	current_shape = index;
	current_family = index + 1;
	shapes[index].depth_mode = depth_mode;
	shapes[index].shadow_receiver = 1;
}

void shape3d_hires_set_shadow_receiver(legacy_s32 enabled)
{
	shapes[current_shape].shadow_receiver = enabled;
}

void shape3d_hires_set_model_scale(legacy_f64 scale)
{
	model_scale = scale;
}

void shape3d_hires_reset(void)
{
	ground_enabled = 0;
	car_shadow_count = 0;
	shadow_model_pending = 0;
	shadow_surface_count = 0;
	model_scale = 1;
	shape3d_hires_begin_shape(0, SHAPE3D_HIRES_DEPTH_SORTED);
	rendered_depth_valid = 0;
	for (legacy_u32 index = 0; index < primitive_count; index++) {
		primitives[index].count = 0;
	}
	primitive_count = 0;
}

static void project_intersection(const struct SHAPE3D_HIRES_VECTOR *first,
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
	project_coordinates(first->x * depth_difference + (second->x - first->x) * near_difference,
						first->y * depth_difference + (second->y - first->y) * near_difference,
						HIRES_NEAR_CLIP_Z * depth_difference, point);
	point->inverse_z = 1.0 / HIRES_NEAR_CLIP_Z;
}

static void queue_polygon(struct HIRES_PRIMITIVE *primitive, legacy_u32 count,
						  const legacy_u8 *indices, const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	const struct SHAPE3D_HIRES_VECTOR *previous = &vertices[indices[count - 1]];
	for (legacy_u32 index = 0; index < count; index++) {
		const struct SHAPE3D_HIRES_VECTOR *current = &vertices[indices[index]];
		if ((previous->z < HIRES_NEAR_CLIP_Z) != (current->z < HIRES_NEAR_CLIP_Z)) {
			project_intersection(previous, current, &primitive->points[primitive->count++]);
		}
		if (current->z >= HIRES_NEAR_CLIP_Z) {
			shape3d_hires_project(current, &primitive->points[primitive->count++]);
		}
		previous = current;
	}
}

static void queue_primitive(legacy_u32 index, legacy_u8 type, legacy_u16 vertex_count,
							const legacy_u8 *indices, const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	legacy_s32 scale = hires_render_scale();
	reserve_primitives(index);
	if (primitive_count <= index) {
		primitive_count = index + 1U;
	}
	struct HIRES_PRIMITIVE *primitive = &primitives[index];
	primitive->count = 0;
	primitive->size = 0;
	primitive->shadow_surface = 0;
	primitive->round_points = HIRES_ROUND_POINTS;
	if (!hires_enabled() || vertex_count == 0 || vertex_count > HIRES_MAX_POLYGON_POINTS / 2) {
		return;
	}
	if (type == RENDER_PRIMITIVE_POLYGON) {
		queue_polygon(primitive, vertex_count, indices, vertices);
		return;
	}
	if (type == RENDER_PRIMITIVE_WHEEL) {
		struct SHAPE3D_HIRES_POINT face[SHAPE3D_WHEEL_RIM_VERTEX_COUNT];
		for (legacy_u32 vertex = 0; vertex < SHAPE3D_WHEEL_RIM_VERTEX_COUNT; vertex++) {
			shape3d_hires_project(&vertices[indices[vertex]], &face[vertex]);
		}
		legacy_u32 start = polygon_faces_camera(face, SHAPE3D_WHEEL_RIM_VERTEX_COUNT)
							   ? 0
							   : SHAPE3D_WHEEL_RIM_VERTEX_COUNT;
		primitive->wheel_face = start;
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
			project_intersection(current, &vertices[indices[1 - vertex]],
								 &primitive->points[vertex]);
		} else {
			shape3d_hires_project(current, &primitive->points[vertex]);
		}
	}
	primitive->count = vertex_count;
	if (type == RENDER_PRIMITIVE_LINE) {
		/* Capture projection and authored model units with the queued geometry. */
		primitive->size = HIRES_LINE_DIAMETER * model_scale * projection_focal_length_x * scale;
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

static legacy_u32 round_point_count(legacy_f64 radius_squared)
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
	queue_primitive(index, type, vertex_count, indices, vertices);
	if (index >= primitive_capacity || primitives[index].count == 0) {
		return;
	}
	struct HIRES_PRIMITIVE *primitive = &primitives[index];
	primitive->shape = current_shape;
	if (shapes[current_shape].depth_mode != SHAPE3D_HIRES_DEPTH_BACKGROUND) {
		if (type == RENDER_PRIMITIVE_WHEEL) {
			legacy_f64 first_x = primitive->points[1].x - primitive->points[0].x;
			legacy_f64 first_y = primitive->points[1].y - primitive->points[0].y;
			legacy_f64 second_x = primitive->points[2].x - primitive->points[0].x;
			legacy_f64 second_y = primitive->points[2].y - primitive->points[0].y;
			/* The inner rim is smaller; the tread's far perimeter is translated. */
			primitive->round_points = round_point_count(first_x * first_x + first_y * first_y +
														second_x * second_x + second_y * second_y);
		} else if (type == RENDER_PRIMITIVE_SPHERE) {
			legacy_f64 horizontal = primitive->size * HIRES_SPHERE_HORIZONTAL_SCALE;
			legacy_f64 vertical = primitive->size * HIRES_SPHERE_VERTICAL_SCALE;
			primitive->round_points =
				round_point_count(horizontal * horizontal + vertical * vertical);
		}
	}
	primitive->attached = (flags & SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) != 0;
	if (type == RENDER_PRIMITIVE_POLYGON && primitive->attached) {
		primitive->size = polygon_padding(primitive->points, primitive->count);
	}
	if (!primitive->attached) {
		current_family = index + 1;
	}
	primitive->family = current_family;
}

static legacy_s32 ceil_coordinate(legacy_f64 coordinate)
{
	legacy_s32 result = (legacy_s32)coordinate;
	return result < coordinate ? result + 1 : result;
}

static legacy_f64 absolute_coordinate(legacy_f64 value)
{
	return value < 0 ? -value : value;
}

void shape3d_hires_update_bounds(legacy_u32 index, legacy_u8 type, struct RECTANGLE *rectangle)
{
	legacy_s32 scale = hires_render_scale();
	legacy_s32 width = hires_render_width();
	legacy_s32 height = hires_render_height();
	if (index >= primitive_capacity || primitives[index].count == 0) {
		return;
	}
	const struct HIRES_PRIMITIVE *primitive = &primitives[index];
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
		legacy_f64 width = absolute_coordinate(primitive->points[1].x - minimum_x) +
						   absolute_coordinate(primitive->points[2].x - minimum_x);
		legacy_f64 height = absolute_coordinate(primitive->points[1].y - minimum_y) +
							absolute_coordinate(primitive->points[2].y - minimum_y);
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
	legacy_s32 right =
		maximum_x >= width - scale ? width / scale : ceil_coordinate(maximum_x / scale) + 1;
	legacy_s32 bottom =
		maximum_y >= height - scale ? height / scale : ceil_coordinate(maximum_y / scale) + 1;
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
	if (index >= primitive_capacity || primitives[index].count == 0) {
		return;
	}
	const struct HIRES_PRIMITIVE *primitive = &primitives[index];
	const struct HIRES_SHAPE *shape = &shapes[primitive->shape];
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
	batching = 1;
	hypervision_begin(ground_enabled ? &ground_plane : NULL);
}

static legacy_s32 finish_commands(legacy_s32 incremental)
{
	if (!rendered_depth_valid || rendered_generation != hires_generation()) {
		hires_depth_begin(0, hires_render_width(), 0, hires_render_height());
		rendered_depth_valid = 1;
		rendered_generation = hires_generation();
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
	if (!batching) {
		hypervision_begin(ground_enabled ? &ground_plane : NULL);
	}
	render_primitive(index, type, color, second_color, third_color, pattern_type, pattern);
	if (!batching) {
		finish_commands(1);
	}
}

legacy_s32 shape3d_hires_batch_end(void)
{
	batching = 0;
	return finish_commands(0);
}

#define SHADOW_SILHOUETTE_WIDTH 128
#define SHADOW_SILHOUETTE_LENGTH 256
#define SHADOW_SILHOUETTE_ROUND_POINTS 12
#define SHADOW_SILHOUETTE_LINE_RADIUS 0.75
#define SHADOW_SILHOUETTE_BORDER_TEXELS 2
#define SHADOW_SILHOUETTE_INITIAL_BOUND (LEGACY_U16_MAX + 1.0)

struct SHADOW_PROJECTION {
	legacy_f64 x, z, sphere_vertical;
	legacy_s16 ground_height;
	legacy_s32 enabled;
};

struct SHADOW_SILHOUETTE {
	struct SHADOW_PROJECTION projection;
	const struct SHAPE3D *shape;
	const legacy_u8 *vertex_bytes;
	legacy_f64 min_x, min_z, max_x, max_z, height, scale_x, scale_z;
	legacy_u8 pixels[SHADOW_SILHOUETTE_WIDTH * SHADOW_SILHOUETTE_LENGTH];
};

struct SHADOW_SILHOUETTE_POINT {
	legacy_f64 x, z;
};

static struct SHADOW_SILHOUETTE_POINT
shadow_project_point(const struct SHADOW_SILHOUETTE *silhouette, legacy_f64 x, legacy_f64 y,
					 legacy_f64 z)
{
	const struct SHADOW_PROJECTION *projection = &silhouette->projection;
	legacy_f64 height = y - projection->ground_height;
	return (struct SHADOW_SILHOUETTE_POINT){x + height * projection->x, z + height * projection->z};
}

/* Union the authored primitives in the receiving plane. Separate polygons
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

static legacy_f64 shadow_silhouette_sample(const struct SHADOW_SILHOUETTE *silhouette, legacy_f64 x,
										   legacy_f64 z)
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
	ground_enabled = 0;
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
	ground_plane.x_step = (f * g - d * i) * inverse_scale / (projection_focal_length_x * scale);
	ground_plane.y_step = -(a * i - c * g) * inverse_scale / (projection_focal_length_y * scale);
	ground_plane.origin = (c * d - a * f) * inverse_scale +
						  (HIRES_SAMPLE_CENTER_OFFSET - (legacy_s16)projection_center_x * scale) *
							  ground_plane.x_step +
						  (HIRES_SAMPLE_CENTER_OFFSET - (legacy_s16)projection_center_y * scale) *
							  ground_plane.y_step;
	ground_enabled = 1;
}

void shape3d_hires_set_shadow_light(legacy_s16 heading, legacy_f64 cotangent)
{
	shadow_light_x = sin_fast((legacy_u16)heading) / (legacy_f64)TRIG_FIXED_ONE;
	shadow_light_z = cos_fast((legacy_u16)heading) / (legacy_f64)TRIG_FIXED_ONE;
	shadow_light_cotangent = SDL_max(0, cotangent);
}

void shape3d_hires_shadows_begin(const struct VECTOR *camera_position)
{
	/* Each scene starts with the racing sun, including after leaving a menu. */
	shadow_light_x = 0;
	shadow_light_z = 1;
	shadow_light_cotangent = HIRES_CAR_SHADOW_SUN_COTANGENT;
	car_shadow_count = 0;
	shadow_model_pending = 0;
	shadow_surface_count = 0;
	if (!hires_enabled() || hires_render_scale() == HIRES_MINIMUM_SCALE) {
		return;
	}
	shadow_ground_y = -camera_position->y;
	shadow_view = mat_temp;
	/* Invert the fixed-point view exactly: transposing its rounded rotation
	 * can move a small distant footprint away from its supporting surface. */
	legacy_f64 a = shadow_view.m._11, b = shadow_view.m._12, c = shadow_view.m._13;
	legacy_f64 d = shadow_view.m._21, e = shadow_view.m._22, f = shadow_view.m._23;
	legacy_f64 g = shadow_view.m._31, h = shadow_view.m._32, i = shadow_view.m._33;
	legacy_f64 determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	legacy_f64 scale = determinant != 0 ? TRIG_FIXED_ONE / determinant : 0;
	shadow_inverse[0][0] = (e * i - f * h) * scale;
	shadow_inverse[0][1] = (c * h - b * i) * scale;
	shadow_inverse[0][2] = (b * f - c * e) * scale;
	shadow_inverse[1][0] = (f * g - d * i) * scale;
	shadow_inverse[1][1] = (a * i - c * g) * scale;
	shadow_inverse[1][2] = (c * d - a * f) * scale;
	shadow_inverse[2][0] = (d * h - e * g) * scale;
	shadow_inverse[2][1] = (b * g - a * h) * scale;
	shadow_inverse[2][2] = (a * e - b * d) * scale;
}

void shape3d_hires_shadow_car(const struct VECTOR *relative_position, legacy_s16 heading,
							  legacy_s16 half_width, legacy_s16 half_length)
{
	shadow_model_pending = 0;
	if (!hires_enabled() || hires_render_scale() == HIRES_MINIMUM_SCALE ||
		car_shadow_count == HIRES_CAR_SHADOW_COUNT || half_width <= 0 || half_length <= 0) {
		return;
	}
	struct HIRES_CAR_SHADOW *shadow = &car_shadows[car_shadow_count++];
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
	shadow_model_pending = 1;
}

void shape3d_hires_shadow_models_reset(void)
{
	car_shadow_count = 0;
	shadow_model_pending = 0;
	shadow_surface_count = 0;
	for (legacy_s32 index = 0; index < HIRES_CAR_SHADOW_COUNT; index++) {
		shadow_silhouettes[index].shape = NULL;
	}
}

static void shadow_attach_model(const struct SHAPE3D *shape, legacy_s16 ground_height,
								legacy_s32 projected)
{
	if (!shadow_model_pending || shape == NULL) {
		return;
	}
	shadow_model_pending = 0;
	if (!hires_enabled() || hires_render_scale() == HIRES_MINIMUM_SCALE) {
		return;
	}
	struct HIRES_CAR_SHADOW *shadow = &car_shadows[car_shadow_count - 1];
	struct SHADOW_SILHOUETTE *silhouette = &shadow_silhouettes[car_shadow_count - 1];
	struct SHADOW_PROJECTION projection = {0};
	if (projected) {
		projection.x = (shadow_light_x * shadow->cosine - shadow_light_z * shadow->sine) *
					   shadow_light_cotangent;
		projection.z = (shadow_light_x * shadow->sine + shadow_light_z * shadow->cosine) *
					   shadow_light_cotangent;
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

static legacy_f64 shadow_light_offset(const struct HIRES_CAR_SHADOW *shadow, legacy_f64 gap)
{
	if (shadow->projected) {
		return 0;
	}
	/* Leave a visible lean even on the ground, limiting travel as the car rises. */
	legacy_f64 offset =
		(gap + shadow->height * HIRES_CAR_SHADOW_HEIGHT_SCALE) * shadow_light_cotangent;
	legacy_f64 limit =
		SDL_min(shadow->half_width, shadow->half_length) * HIRES_CAR_SHADOW_OFFSET_LIMIT_SCALE;
	return offset < limit ? offset : limit;
}

static void shadow_update_volume(struct HIRES_CAR_SHADOW *shadow)
{
	legacy_f64 extent_x = SDL_max(-shadow->min_x, shadow->max_x);
	legacy_f64 extent_z = SDL_max(-shadow->min_z, shadow->max_z);
	legacy_f64 radius_x = absolute_coordinate(shadow->cosine) * extent_x +
						  absolute_coordinate(shadow->sine) * extent_z;
	legacy_f64 radius_z = absolute_coordinate(shadow->sine) * extent_x +
						  absolute_coordinate(shadow->cosine) * extent_z;
	legacy_f64 extension = shadow_light_offset(shadow, HIRES_CAR_SHADOW_REACH);
	legacy_f64 extend_x = extension * shadow_light_x;
	legacy_f64 extend_z = extension * shadow_light_z;
	shadow->minimum = (struct SHAPE3D_HIRES_VECTOR){
		shadow->position.x - radius_x + SDL_min(0, extend_x),
		shadow->projected ? shadow->position.y - HIRES_CAR_SHADOW_RECEIVER_TOLERANCE
						  : SDL_max(shadow_ground_y, shadow->position.y - HIRES_CAR_SHADOW_REACH),
		shadow->position.z - radius_z + SDL_min(0, extend_z)};
	shadow->maximum =
		(struct SHAPE3D_HIRES_VECTOR){shadow->position.x + radius_x + SDL_max(0, extend_x),
									  shadow->position.y + HIRES_CAR_SHADOW_RECEIVER_TOLERANCE,
									  shadow->position.z + radius_z + SDL_max(0, extend_z)};
}

static void shadow_world_vertices(const struct SHAPE3D_HIRES_VECTOR *vertices, legacy_u32 count,
								  struct SHADOW_SURFACE *surface)
{
	for (legacy_u32 index = 0; index < count; index++) {
		const struct SHAPE3D_HIRES_VECTOR *view = &vertices[index];
		struct SHAPE3D_HIRES_VECTOR world = {
			shadow_inverse[0][0] * view->x + shadow_inverse[0][1] * view->y +
				shadow_inverse[0][2] * view->z,
			shadow_inverse[1][0] * view->x + shadow_inverse[1][1] * view->y +
				shadow_inverse[1][2] * view->z,
			shadow_inverse[2][0] * view->x + shadow_inverse[2][1] * view->y +
				shadow_inverse[2][2] * view->z};
		surface->vertices[index] = world;
		if (index == 0) {
			surface->minimum = surface->maximum = world;
		} else {
			surface->minimum.x = SDL_min(surface->minimum.x, world.x);
			surface->minimum.y = SDL_min(surface->minimum.y, world.y);
			surface->minimum.z = SDL_min(surface->minimum.z, world.z);
			surface->maximum.x = SDL_max(surface->maximum.x, world.x);
			surface->maximum.y = SDL_max(surface->maximum.y, world.y);
			surface->maximum.z = SDL_max(surface->maximum.z, world.z);
		}
	}
	surface->cars = 0;
	for (legacy_s32 car = 0; car < car_shadow_count; car++) {
		const struct HIRES_CAR_SHADOW *shadow = &car_shadows[car];
		if (surface->maximum.x >= shadow->minimum.x && surface->minimum.x <= shadow->maximum.x &&
			surface->maximum.y >= shadow->minimum.y && surface->minimum.y <= shadow->maximum.y &&
			surface->maximum.z >= shadow->minimum.z && surface->minimum.z <= shadow->maximum.z) {
			surface->cars |= 1U << car;
		}
	}
}

legacy_s32 shape3d_hires_shadows_active(void)
{
	return car_shadow_count != 0;
}

legacy_s32 shape3d_hires_shadow_overlap(const struct SHAPE3D_HIRES_VECTOR *vertices,
										legacy_u32 count)
{
	if (car_shadow_count == 0 || count == 0 || count > HIRES_SHADOW_SURFACE_MAX_POINTS) {
		return 0;
	}
	struct SHADOW_SURFACE surface;
	shadow_world_vertices(vertices, count, &surface);
	return surface.cars != 0;
}

legacy_u32 shape3d_hires_shadow_polygon(const struct SHAPE3D_HIRES_VECTOR *vertices,
										legacy_u32 count, legacy_s32 grille)
{
	if (car_shadow_count == 0 || count < 3 || count > HIRES_SHADOW_SURFACE_MAX_POINTS) {
		return 0;
	}
	struct SHADOW_SURFACE surface;
	shadow_world_vertices(vertices, count, &surface);
	if (surface.cars == 0) {
		return 0;
	}
	/* Newell's normal also handles polygons with collinear leading vertices. */
	surface.normal = (struct SHAPE3D_HIRES_VECTOR){0, 0, 0};
	for (legacy_u32 index = 0; index < count; index++) {
		const struct SHAPE3D_HIRES_VECTOR *first = &surface.vertices[index];
		const struct SHAPE3D_HIRES_VECTOR *last = &surface.vertices[(index + 1) % count];
		surface.normal.x += (first->y - last->y) * (first->z + last->z);
		surface.normal.y += (first->z - last->z) * (first->x + last->x);
		surface.normal.z += (first->x - last->x) * (first->y + last->y);
	}
	legacy_f64 length =
		SDL_sqrt(surface.normal.x * surface.normal.x + surface.normal.y * surface.normal.y +
				 surface.normal.z * surface.normal.z);
	if (length < HIRES_SHADOW_INTERSECTION_EPSILON) {
		return 0;
	}
	surface.normal.x /= length;
	surface.normal.y /= length;
	surface.normal.z /= length;
	surface.distance = 0;
	for (legacy_u32 index = 0; index < count; index++) {
		surface.distance += surface.normal.x * surface.vertices[index].x +
							surface.normal.y * surface.vertices[index].y +
							surface.normal.z * surface.vertices[index].z;
	}
	surface.distance /= count;
	/* Drop the dominant normal coordinate for the point-in-polygon test. */
	surface.axis =
		absolute_coordinate(surface.normal.x) > absolute_coordinate(surface.normal.y) ? 0 : 1;
	if (absolute_coordinate(surface.normal.z) > (surface.axis == 0
													 ? absolute_coordinate(surface.normal.x)
													 : absolute_coordinate(surface.normal.y))) {
		surface.axis = 2;
	}
	if (surface.axis == 1) {
		/* A warped quad's fitted plane can extend outside its authored Y
		 * bounds. Include its projected corners when clipping intersections. */
		for (legacy_u32 index = 0; index < count; index++) {
			legacy_f64 y = (surface.distance - surface.normal.x * surface.vertices[index].x -
							surface.normal.z * surface.vertices[index].z) /
						   surface.normal.y;
			surface.minimum.y = SDL_min(surface.minimum.y, y);
			surface.maximum.y = SDL_max(surface.maximum.y, y);
		}
	}
	surface.count = count;
	surface.rendered_index = LEGACY_U32_MAX;
	surface.grille = grille;
	if (shadow_surface_count == shadow_surface_capacity) {
		size_t capacity = shadow_surface_capacity != 0 ? shadow_surface_capacity * 2
													   : HIRES_SHADOW_SURFACE_INITIAL_CAPACITY;
		/* Each plane intersects each of the two projection pieces at most twice. */
		if (capacity < shadow_surface_capacity || capacity > LEGACY_U32_MAX ||
			capacity > (size_t)-1 / sizeof(*shadow_surfaces) ||
			capacity > (size_t)-1 / (HIRES_SHADOW_SURFACE_MAX_HITS * sizeof(*shadow_grille_hits))) {
			fatal_error("HyperVision shadow surfaces exceed addressable memory");
			return 0;
		}
		struct SHADOW_SURFACE *surfaces = realloc(shadow_surfaces, capacity * sizeof(*surfaces));
		legacy_f64 *hits =
			realloc(shadow_grille_hits, capacity * HIRES_SHADOW_SURFACE_MAX_HITS * sizeof(*hits));
		if (surfaces == NULL || hits == NULL) {
			fatal_error("Cannot allocate HyperVision shadow surfaces");
			return 0;
		}
		shadow_surfaces = surfaces;
		shadow_grille_hits = hits;
		shadow_surface_capacity = capacity;
	}
	shadow_surfaces[shadow_surface_count++] = surface;
	for (legacy_s32 car = 0; car < car_shadow_count; car++) {
		if ((surface.cars & (1U << car)) != 0) {
			struct HIRES_CAR_SHADOW *shadow = &car_shadows[car];
			shadow->surface_top = SDL_max(shadow->surface_top, surface.maximum.y);
			if (grille) {
				shadow->grille_top = SDL_max(shadow->grille_top, surface.maximum.y);
			}
		}
	}
	return (legacy_u32)shadow_surface_count;
}

void shape3d_hires_set_shadow_surface(legacy_u32 index, legacy_u32 surface)
{
	if (index < primitive_count && surface != 0 && surface <= shadow_surface_count) {
		primitives[index].shadow_surface = surface;
		shadow_surfaces[surface - 1].rendered_index = index;
	}
}

/* Sorted polygons identify their support directly. An ordered shape shares one
 * depth family, so resolve its support only among the few collected surfaces,
 * using the same scanline depth interpolation as the scene rasterizer. */
static legacy_u32 shadow_receiver_surface(legacy_u32 family)
{
	return family != 0U && family <= primitive_count ? primitives[family - 1U].shadow_surface : 0U;
}

static legacy_s32 shadow_surface_contains(const struct SHADOW_SURFACE *surface, legacy_f64 x,
										  legacy_f64 y, legacy_f64 z)
{
	if (x < surface->minimum.x - HIRES_SHADOW_INTERSECTION_EPSILON ||
		x > surface->maximum.x + HIRES_SHADOW_INTERSECTION_EPSILON ||
		z < surface->minimum.z - HIRES_SHADOW_INTERSECTION_EPSILON ||
		z > surface->maximum.z + HIRES_SHADOW_INTERSECTION_EPSILON) {
		return 0;
	}
	legacy_f64 u = surface->axis == 0 ? y : x;
	legacy_f64 v = surface->axis == 2 ? y : z;
	legacy_s32 inside = 0;
	for (legacy_u32 index = 0; index < surface->count; index++) {
		const struct SHAPE3D_HIRES_VECTOR *first = &surface->vertices[index];
		const struct SHAPE3D_HIRES_VECTOR *last = &surface->vertices[(index + 1) % surface->count];
		legacy_f64 first_u = surface->axis == 0 ? first->y : first->x;
		legacy_f64 first_v = surface->axis == 2 ? first->y : first->z;
		legacy_f64 last_u = surface->axis == 0 ? last->y : last->x;
		legacy_f64 last_v = surface->axis == 2 ? last->y : last->z;
		legacy_f64 cross = (last_u - first_u) * (v - first_v) - (last_v - first_v) * (u - first_u);
		if (absolute_coordinate(cross) < HIRES_SHADOW_INTERSECTION_EPSILON &&
			u >= SDL_min(first_u, last_u) && u <= SDL_max(first_u, last_u) &&
			v >= SDL_min(first_v, last_v) && v <= SDL_max(first_v, last_v)) {
			return 1;
		}
		if ((first_v > v) != (last_v > v) && ((cross > 0) == (last_v > first_v))) {
			inside = !inside;
		}
	}
	return inside;
}

struct SHADOW_TRACE {
	const struct HIRES_CAR_SHADOW *shadow;
	legacy_f64 source_x, source_z, gap, transmission;
	size_t grille_count;
	legacy_s32 solid_receiver, grille_receiver;
};

static legacy_s32 shadow_surface_hit(const struct SHADOW_SURFACE *surface,
									 struct SHADOW_TRACE *trace, legacy_f64 gap, legacy_f64 first,
									 legacy_f64 last)
{
	if (gap < first - HIRES_SHADOW_INTERSECTION_EPSILON ||
		gap > last + HIRES_SHADOW_INTERSECTION_EPSILON) {
		return 0;
	}
	const struct HIRES_CAR_SHADOW *shadow = trace->shadow;
	legacy_f64 shrink = 1 + gap / HIRES_CAR_SHADOW_SHRINK_HEIGHT;
	legacy_f64 offset = shadow_light_offset(shadow, gap);
	legacy_f64 x = shadow->position.x + offset * shadow_light_x + trace->source_x / shrink;
	legacy_f64 z = shadow->position.z + offset * shadow_light_z + trace->source_z / shrink;
	if (!shadow_surface_contains(surface, x, shadow->position.y - gap, z)) {
		return 0;
	}
	if (gap >= trace->gap - HIRES_CAR_SHADOW_RECEIVER_TOLERANCE) {
		if (surface->grille) {
			trace->grille_receiver = 1;
		} else {
			trace->solid_receiver = 1;
		}
		return 0;
	}
	if (!surface->grille) {
		return 1;
	}
	/* Mesh triangles, reversed faces and coplanar panels form one layer. */
	for (size_t index = 0; index < trace->grille_count; index++) {
		if (absolute_coordinate(shadow_grille_hits[index] - gap) <=
			HIRES_CAR_SHADOW_RECEIVER_TOLERANCE) {
			return 0;
		}
	}
	shadow_grille_hits[trace->grille_count++] = gap;
	trace->transmission *= HIRES_SHADOW_GRILLE_TRANSMISSION;
	return 0;
}

static legacy_s32 shadow_surface_segment(const struct SHADOW_SURFACE *surface,
										 struct SHADOW_TRACE *trace, legacy_f64 first,
										 legacy_f64 last, legacy_f64 offset, legacy_f64 slope)
{
	const struct HIRES_CAR_SHADOW *shadow = trace->shadow;
	const struct SHAPE3D_HIRES_VECTOR *normal = &surface->normal;
	legacy_f64 light_normal = normal->x * shadow_light_x + normal->z * shadow_light_z;
	legacy_f64 constant = normal->x * (shadow->position.x + shadow_light_x * offset) +
						  normal->y * shadow->position.y +
						  normal->z * (shadow->position.z + shadow_light_z * offset) -
						  surface->distance;
	legacy_f64 linear = light_normal * slope - normal->y;
	/* Follow the existing shrinking footprint, including the directional offset
	 * clamp. Multiplying the plane equation by (1 + gap / height) yields a
	 * quadratic, so ramps and road edges need no marching or shadow map. */
	legacy_f64 a = linear / HIRES_CAR_SHADOW_SHRINK_HEIGHT;
	legacy_f64 b = linear + constant / HIRES_CAR_SHADOW_SHRINK_HEIGHT;
	legacy_f64 c = constant + normal->x * trace->source_x + normal->z * trace->source_z;
	if (absolute_coordinate(a) < HIRES_SHADOW_INTERSECTION_EPSILON) {
		return absolute_coordinate(b) >= HIRES_SHADOW_INTERSECTION_EPSILON &&
			   shadow_surface_hit(surface, trace, -c / b, first, last);
	}
	legacy_f64 discriminant = b * b - 4 * a * c;
	if (discriminant < 0) {
		return 0;
	}
	legacy_f64 root = SDL_sqrt(discriminant);
	legacy_f64 q = -0.5 * (b + (b < 0 ? -root : root));
	if (q == 0) {
		return shadow_surface_hit(surface, trace, -b / (2 * a), first, last);
	}
	return shadow_surface_hit(surface, trace, q / a, first, last) ||
		   shadow_surface_hit(surface, trace, c / q, first, last);
}

static legacy_s32 shadow_occlusion_needed(const struct HIRES_CAR_SHADOW *shadow, legacy_f64 y)
{
	/* Normal driving on an opaque surface needs no intersection work. */
	return shadow_surface_count != 0 &&
		   (y < shadow->surface_top - HIRES_CAR_SHADOW_RECEIVER_TOLERANCE ||
			y <= shadow->grille_top + HIRES_CAR_SHADOW_RECEIVER_TOLERANCE);
}

static legacy_f64 shadow_transmission(const struct HIRES_CAR_SHADOW *shadow, legacy_f64 x,
									  legacy_f64 y, legacy_f64 z, legacy_u32 receiver)
{

	legacy_f64 gap = SDL_max(0, shadow->position.y - y);
	legacy_f64 shrink = 1 + gap / HIRES_CAR_SHADOW_SHRINK_HEIGHT;
	legacy_f64 displacement = shadow_light_offset(shadow, gap);
	struct SHADOW_TRACE trace = {shadow,
								 (x - shadow->position.x - displacement * shadow_light_x) * shrink,
								 (z - shadow->position.z - displacement * shadow_light_z) * shrink,
								 gap,
								 1,
								 0,
								 0,
								 0};
	legacy_f64 limit =
		SDL_min(shadow->half_width, shadow->half_length) * HIRES_CAR_SHADOW_OFFSET_LIMIT_SCALE;
	legacy_f64 offset = shadow->height * HIRES_CAR_SHADOW_HEIGHT_SCALE * shadow_light_cotangent;
	legacy_f64 turn = shadow_light_cotangent > 0
						  ? SDL_max(0, (limit - offset) / shadow_light_cotangent)
						  : DBL_MAX;
	legacy_u32 car = 1U << (shadow - car_shadows);
	for (size_t index = 0; index < shadow_surface_count; index++) {
		const struct SHADOW_SURFACE *surface = &shadow_surfaces[index];
		/* A warped road quad's rasterized depth need not lie on its Newell
		 * plane. Its identity prevents that approximation from self-blocking. */
		if (index + 1 == receiver) {
			trace.grille_receiver |= surface->grille;
			trace.solid_receiver |= !surface->grille;
			continue;
		}
		if ((surface->cars & car) == 0 ||
			surface->maximum.y < y - HIRES_CAR_SHADOW_RECEIVER_TOLERANCE) {
			continue;
		}
		legacy_f64 first =
			SDL_max(-HIRES_CAR_SHADOW_RECEIVER_TOLERANCE, shadow->position.y - surface->maximum.y);
		legacy_f64 last = SDL_min(gap + HIRES_CAR_SHADOW_RECEIVER_TOLERANCE,
								  shadow->position.y - surface->minimum.y);
		if (first > last) {
			continue;
		}
		/* Flat decks dominate normal driving; their intersection needs no roots. */
		if (absolute_coordinate(surface->normal.x) < HIRES_SHADOW_INTERSECTION_EPSILON &&
			absolute_coordinate(surface->normal.z) < HIRES_SHADOW_INTERSECTION_EPSILON) {
			if (shadow_surface_hit(surface, &trace, shadow->position.y - surface->vertices[0].y,
								   first, last)) {
				return 0;
			}
		} else if ((first < turn &&
					shadow_surface_segment(surface, &trace, first, SDL_min(last, turn), offset,
										   shadow_light_cotangent)) ||
				   (last >= turn && shadow_surface_segment(surface, &trace, SDL_max(first, turn),
														   last, limit, 0))) {
			return 0;
		}
	}
	return trace.transmission *
		   (trace.grille_receiver && !trace.solid_receiver ? HIRES_SHADOW_GRILLE_TRANSMISSION : 1);
}

static struct RECTANGLE shadow_bounds(const struct HIRES_CAR_SHADOW *shadow)
{
	legacy_s32 width = hires_render_width();
	legacy_s32 height = hires_render_height();
	struct RECTANGLE bounds = {width, 0, height, 0};
	/* The flat ground bounds the bottom of the small receiving volume. */
	legacy_f64 bottom =
		shadow->projected ? shadow->position.y : shadow->position.y - HIRES_CAR_SHADOW_REACH;
	if (bottom < shadow_ground_y) {
		bottom = shadow_ground_y;
	}
	legacy_f64 extension = shadow_light_offset(shadow, HIRES_CAR_SHADOW_REACH);
	legacy_f64 extend_x =
		extension * (shadow_light_x * shadow->cosine - shadow_light_z * shadow->sine);
	legacy_f64 extend_z =
		extension * (shadow_light_x * shadow->sine + shadow_light_z * shadow->cosine);
	struct SHAPE3D_HIRES_VECTOR vertices[HIRES_CAR_SHADOW_BOUNDS_CORNERS];
	for (legacy_s32 corner = 0; corner < HIRES_CAR_SHADOW_BOUNDS_CORNERS; corner++) {
		legacy_f64 x = (corner & HIRES_CAR_SHADOW_CORNER_MAX_X)
						   ? shadow->max_x + SDL_max(0, extend_x)
						   : shadow->min_x + SDL_min(0, extend_x);
		legacy_f64 z = (corner & HIRES_CAR_SHADOW_CORNER_MAX_Z)
						   ? shadow->max_z + SDL_max(0, extend_z)
						   : shadow->min_z + SDL_min(0, extend_z);
		legacy_f64 world_x = shadow->position.x + x * shadow->cosine + z * shadow->sine;
		legacy_f64 world_y =
			(corner & HIRES_CAR_SHADOW_CORNER_BOTTOM) ? bottom : shadow->position.y;
		legacy_f64 world_z = shadow->position.z - x * shadow->sine + z * shadow->cosine;
		struct SHAPE3D_HIRES_VECTOR view = {
			(world_x * shadow_view.m._11 + world_y * shadow_view.m._12 +
			 world_z * shadow_view.m._13) /
				TRIG_FIXED_ONE,
			(world_x * shadow_view.m._21 + world_y * shadow_view.m._22 +
			 world_z * shadow_view.m._23) /
				TRIG_FIXED_ONE,
			(world_x * shadow_view.m._31 + world_y * shadow_view.m._32 +
			 world_z * shadow_view.m._33) /
				TRIG_FIXED_ONE};
		vertices[corner] = view;
	}
	/* Clip the twelve prism edges, including near-camera cars, rather than
	 * turning a single behind-camera corner into a full-screen scan. */
	struct SHAPE3D_HIRES_POINT
		points[HIRES_CAR_SHADOW_BOUNDS_CORNERS + HIRES_CAR_SHADOW_BOUNDS_EDGES];
	legacy_s32 count = 0;
	for (legacy_s32 corner = 0; corner < HIRES_CAR_SHADOW_BOUNDS_CORNERS; corner++) {
		if (vertices[corner].z >= HIRES_NEAR_CLIP_Z) {
			shape3d_hires_project(&vertices[corner], &points[count++]);
		}
		for (legacy_s32 edge = HIRES_CAR_SHADOW_CORNER_MAX_X;
			 edge <= HIRES_CAR_SHADOW_CORNER_BOTTOM; edge *= 2) {
			if ((corner & edge) == 0 && ((vertices[corner].z < HIRES_NEAR_CLIP_Z) !=
										 (vertices[corner | edge].z < HIRES_NEAR_CLIP_Z))) {
				project_intersection(&vertices[corner], &vertices[corner | edge], &points[count++]);
			}
		}
	}
	for (legacy_s32 index = 0; index < count; index++) {
		struct SHAPE3D_HIRES_POINT point = points[index];
		legacy_s16 px = (legacy_s16)(point.x < 0 ? 0 : point.x > width ? width : point.x);
		legacy_s16 py = (legacy_s16)(point.y < 0 ? 0 : point.y > height ? height : point.y);
		if (px < bounds.left) {
			bounds.left = px;
		}
		if (px + 1 > bounds.right) {
			bounds.right = px + 1;
		}
		if (py < bounds.top) {
			bounds.top = py;
		}
		if (py + 1 > bounds.bottom) {
			bounds.bottom = py + 1;
		}
	}
	return bounds;
}

static legacy_u8 shadow_opacity(const struct HIRES_CAR_SHADOW *shadow, legacy_f64 x, legacy_f64 y,
								legacy_f64 z)
{
	legacy_f64 gap = shadow->position.y - y;
	if (shadow->projected) {
		/* This mask is already cast onto the registered flat contact plane. */
		if (absolute_coordinate(gap) > HIRES_CAR_SHADOW_RECEIVER_TOLERANCE) {
			return 0;
		}
		gap = 0;
	}
	if (gap < -HIRES_CAR_SHADOW_RECEIVER_TOLERANCE || gap >= HIRES_CAR_SHADOW_REACH) {
		return 0;
	}
	if (gap < 0) {
		gap = 0;
	}
	legacy_f64 offset = shadow_light_offset(shadow, gap);
	x -= shadow->position.x + offset * shadow_light_x;
	z -= shadow->position.z + offset * shadow_light_z;
	legacy_f64 shrink = 1 + gap / HIRES_CAR_SHADOW_SHRINK_HEIGHT;
	legacy_f64 u = (x * shadow->cosine - z * shadow->sine) * shrink;
	legacy_f64 v = (x * shadow->sine + z * shadow->cosine) * shrink;
	legacy_f64 coverage;
	if (shadow->silhouette != NULL) {
		coverage = shadow_silhouette_sample(shadow->silhouette, u, v);
	} else {
		/* Retain a modest footprint if a custom model has no usable geometry. */
		u = absolute_coordinate(u / shadow->half_width);
		v = absolute_coordinate(v / shadow->half_length);
		legacy_f64 contour = SDL_max(SDL_max(u, v), (u + v) / HIRES_CAR_SHADOW_FALLBACK_DIAGONAL);
		coverage = SDL_min(1, (1 - contour) * HIRES_CAR_SHADOW_FALLBACK_EDGE_SCALE);
	}
	if (coverage <= 0) {
		return 0;
	}
	/* Fade the last part of the short reach so a high jump has no cutoff. */
	legacy_f64 fade = (HIRES_CAR_SHADOW_REACH - gap) / HIRES_CAR_SHADOW_FADE_HEIGHT;
	if (fade > 1) {
		fade = 1;
	}
	return (legacy_u8)(HIRES_CAR_SHADOW_MAX_OPACITY * coverage * fade);
}

void shape3d_hires_draw_shadows(void)
{
	legacy_s32 scale = hires_render_scale();
	legacy_s32 width = hires_render_width();
	if (!hires_enabled() || scale == HIRES_MINIMUM_SCALE || car_shadow_count == 0 ||
		projection_focal_length_x == 0 || projection_focal_length_y == 0) {
		return;
	}
	struct HIRES_RASTER_TARGET target;
	if (!hires_raster_prepare(&target)) {
		return;
	}
	legacy_f64 focal_x = projection_focal_length_x * scale;
	legacy_f64 focal_y = projection_focal_length_y * scale;
	legacy_f64 ray_step[3];
	for (legacy_s32 axis = 0; axis < 3; axis++) {
		ray_step[axis] = shadow_inverse[axis][0] / focal_x;
	}
	legacy_s32 started = 0;
	for (legacy_s32 car = 0; car < car_shadow_count; car++) {
		const struct HIRES_CAR_SHADOW *shadow = &car_shadows[car];
		struct RECTANGLE bounds = shadow_bounds(shadow);
		if (bounds.left < target.left) {
			bounds.left = (legacy_s16)target.left;
		}
		if (bounds.right > target.right) {
			bounds.right = (legacy_s16)target.right;
		}
		if (bounds.top < target.top) {
			bounds.top = (legacy_s16)target.top;
		}
		if (bounds.bottom > target.bottom) {
			bounds.bottom = (legacy_s16)target.bottom;
		}
		for (legacy_s32 y = bounds.top; y < bounds.bottom; y++) {
			legacy_f64 view_x =
				(bounds.left + 0.5 - (legacy_s16)projection_center_x * scale) / focal_x;
			legacy_f64 view_y = ((legacy_s16)projection_center_y * scale - y - 0.5) / focal_y;
			legacy_f64 ray[3];
			for (legacy_s32 axis = 0; axis < 3; axis++) {
				ray[axis] = shadow_inverse[axis][0] * view_x + shadow_inverse[axis][1] * view_y +
							shadow_inverse[axis][2];
			}
			for (legacy_s32 x = bounds.left; x < bounds.right; x++) {
				legacy_u32 family = 0;
				size_t pixel = (size_t)y * width + x;
				if (x >= target.depth_left && x < target.depth_right && y >= target.depth_top &&
					y < target.depth_bottom) {
					family = target.depth_family[pixel];
				}
				legacy_f64 depth = 0;
				if (family != 0) {
					if (family <= primitive_count &&
						shapes[primitives[family - 1].shape].shadow_receiver) {
						depth = 1.0 / target.inverse_depth[pixel];
					}
				} else if (ray[1] < 0 && shadow_ground_y < 0) {
					/* Flat grass is the skybox's ground fill, not queued geometry. */
					depth = shadow_ground_y / ray[1];
				}
				if (depth >= HIRES_NEAR_CLIP_Z) {
					legacy_u8 opacity =
						shadow_opacity(shadow, ray[0] * depth, ray[1] * depth, ray[2] * depth);
					if (opacity != 0 && shadow_occlusion_needed(shadow, ray[1] * depth)) {
						opacity = (legacy_u8)(opacity *
											  shadow_transmission(shadow, ray[0] * depth,
																  ray[1] * depth, ray[2] * depth,
																  shadow_receiver_surface(family)));
					}
					if (opacity != 0) {
						if (!started) {
							if (!hires_shadow_begin()) {
								return;
							}
							started = 1;
						}
						hires_shadow_pixel(x, y, opacity);
					}
				}
				for (legacy_s32 axis = 0; axis < 3; axis++) {
					ray[axis] += ray_step[axis];
				}
			}
		}
	}
}

#endif
