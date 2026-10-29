#include <stddef.h>
#include <limits.h>
#if defined(RESTUNTS_SDL3)
#include <stdlib.h>
#include <string.h>
#endif
#include "externs.h"
#include "fileio.h"
#include "legacy.h"
#include "memmgr.h"
#include "shape3d.h"
#include "shape2d_internal.h"
#include "shape3d_internal.h"
#include "shape2d.h"
#include "math_internal.h"
#include "car_model.h"
#include "projection.h"
#include "residue.h"
#if defined(RESTUNTS_SDL3)
#undef memcpy
#include "fatal.h"
#include "hires.h"
#include "shape3d_hires.h"
#endif

/*

TODO:

 * lines function (calls)
- ----- --------- -------
X   159 mat_rot_zxy (16)
X    60 mat_multiply (0)
X   122 mat_mul_vector (0)
X    48 mat_invert (0)
X   126 vector_direction_sector (14)
X    97 vector_to_point (0)
X    32 rect_compare_point (0)
X    40 vector_interpolate_at_z (0)
X    86 is_facing_camera (4)
X    33 rect_adjust_from_point(0)
X    47 polarRadius2D (6)
X    13 polarRadius3D (4)
X     7 projection_scale_x_wrapped (0)
X    74 polygon_insert_newest (0)
X     - polarAngle (0)
X     - mat_rot_z (4)
X     - mat_rot_x (4)
X     - mat_rot_y (4)
X     - set_projection (10)
X     - select_cliprect_rotate (10)

*/

void shape3d_vertex_read(const struct SHAPE3D *shape, legacy_u16 index, struct VECTOR *destination)
{
	const legacy_u8 far *source =
		shape->shape3d_vertex_bytes + LEGACY_U16_WRAP_MUL(index, SHAPE3D_VERTEX_SIZE);
	destination->x = LEGACY_READ_S16_LE(source + SHAPE3D_VERTEX_X_OFFSET);
	destination->y = LEGACY_READ_S16_LE(source + SHAPE3D_VERTEX_Y_OFFSET);
	destination->z = LEGACY_READ_S16_LE(source + SHAPE3D_VERTEX_Z_OFFSET);
}

void shape3d_vertex_write(struct SHAPE3D *shape, legacy_u16 index, const struct VECTOR *source)
{
	legacy_u8 far *destination =
		shape->shape3d_vertex_bytes + LEGACY_U16_WRAP_MUL(index, SHAPE3D_VERTEX_SIZE);
	LEGACY_WRITE_U16_LE(destination + SHAPE3D_VERTEX_X_OFFSET, (legacy_u16)source->x);
	LEGACY_WRITE_U16_LE(destination + SHAPE3D_VERTEX_Y_OFFSET, (legacy_u16)source->y);
	LEGACY_WRITE_U16_LE(destination + SHAPE3D_VERTEX_Z_OFFSET, (legacy_u16)source->z);
}

extern legacy_s8 is_facing_camera(struct POINT2D far *);
extern legacy_u16 polygon_insert_newest(legacy_u16, legacy_u16);
extern legacy_u16 projection_scale_x_wrapped(legacy_u16, legacy_s16);

extern legacy_u16 polygon_buffer_full;
extern legacy_u16 transshapenumverts;
extern legacy_u8 far *transshapeprimitives;
extern legacy_u16 transshapenumpaints;
extern legacy_u8 transshapematerial;
extern legacy_u8 transshapeflags;
extern struct RECTANGLE *transshaperectptr;
extern legacy_s32 invpow2tbl[32];
extern legacy_u8 shape_view_direction_sector;

// Track the current shape's portion of the depth-sorted polygon list.
// Polygon immediately preceding this shape's first polygon.
extern polyinfo_index shape_polygon_predecessor;
// Last polygon in the full list, or its sentinel when empty.
extern polyinfo_index polygon_list_tail;
// Number of polygons queued for the current shape.
extern polyinfo_index shape_polygon_count;
// After insertion, contains the index of the newly inserted primitive
extern polyinfo_index polygon_insertion_cursor;

extern legacy_u8 transshapenumvertscopy;
extern struct POINT2D *polyvertpointptrtab[];
extern legacy_u16 shape_half_scale;
extern legacy_u8 primidxcounttab[];
extern legacy_u8 primtypetab[];
extern legacy_u8 far *transshapeprimptr;
extern legacy_u8 far *transshapepolyinfo;
extern legacy_s8 transprimitivepaintjob;
extern legacy_u8 far *transshapeprimindexptr;

/* 14-bit fixed point: the inner radius is 37/64 of the outer radius. */
#define WHEEL_INNER_RADIUS_SCALE 9472U

static polyinfo_index queued_ghost_primitives;

#define SHAPE3D_VERTEX_CAPACITY 255U
#define SHAPE3D_VERTEX_FLAG_CAPACITY 256U
#define SHAPE3D_VERTEX_UNTRANSFORMED LEGACY_U8_MAX
#define SHAPE3D_SHADOW_PRIMITIVE_CAPACITY (LEGACY_U8_MAX + 1U)
#define SHAPE3D_FORWARD_VECTOR_SCALE 4096
#define SHAPE3D_NEAR_CLIP_Z 12
#define SHAPE3D_USE_BOUNDING_RECT_FLAG 8U
#define SHAPE3D_PRETRANSFORMED_FLAG 2U
#define SHAPE3D_NO_DEPTH_SORT_FLAG 1U
#define SHAPE3D_PRIMITIVE_ALWAYS_VISIBLE_FLAG 1U

#define POLYINFO_LIST_SENTINEL (-1)
#define POLYINFO_MATERIAL_OFFSET 2U
#define POLYINFO_VERTEX_COUNT_OFFSET 3U
#define POLYINFO_TYPE_OFFSET 4U
#define POLYINFO_HEADER_WORD_COUNT 3U
#define POLYINFO_POINT_WORD_COUNT 2U
#define POLYINFO_START_X_WORD POLYINFO_HEADER_WORD_COUNT
#define POLYINFO_START_Y_WORD (POLYINFO_START_X_WORD + 1U)
#define POLYINFO_END_X_WORD (POLYINFO_START_X_WORD + POLYINFO_POINT_WORD_COUNT)
#define POLYINFO_END_Y_WORD (POLYINFO_END_X_WORD + 1U)
#define POLYINFO_SPHERE_SIZE_WORD (POLYINFO_HEADER_WORD_COUNT + POLYINFO_POINT_WORD_COUNT)
#define POLYINFO_LEGACY_LAST_VALID_OFFSET 10354U
/* A ten-vertex polygon can gain five vertices at the near plane. Reserve
 * a full record before beginning another primitive, including clipped ones. */
#define POLYINFO_MAX_RENDER_POINTS 15U
#define POLYINFO_MAX_RECORD_SIZE (6U + 4U * POLYINFO_MAX_RENDER_POINTS)
#define POLYINFO_SUPERSIGHT_LAST_VALID_OFFSET                                                      \
	(POLYINFO_SUPERSIGHT_DATA_SIZE - POLYINFO_MAX_RECORD_SIZE)

enum SHAPE3D_PATTERN_TYPE {
	SHAPE3D_PATTERN_SOLID = 0,
	SHAPE3D_PATTERN_MASKED = 1,
	SHAPE3D_PATTERN_TWO_COLOR = 2
};

static polyinfo_index polyinfo_primitive_capacity = POLYINFO_LEGACY_PRIMITIVE_CAPACITY;
static polyinfo_offset polyinfo_last_valid_offset = POLYINFO_LEGACY_LAST_VALID_OFFSET;

#if defined(RESTUNTS_SDL3)
static legacy_u8 polyinfo_supersight_enabled;
static polyinfo_index native_primitive_capacity = POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY;
static polyinfo_offset native_data_capacity;
static polyinfo_link *native_polygon_links;
static polyinfo_offset *native_polygon_offsets;
static legacy_u8 *native_polyinfo;

/* Enhanced geometry is held by the projection adapter. Its scene metadata is
 * independent of the packed 16-bit records and links used by classic drawing. */
struct HYPERVISION_SCENE_PRIMITIVE {
	legacy_u16 material;
	legacy_u8 type;
};

static struct HYPERVISION_SCENE_PRIMITIVE *hypervision_scene;
static legacy_u32 hypervision_scene_count;
static legacy_u32 hypervision_scene_capacity;

static void *polyinfo_allocate(size_t count, size_t size)
{
	if (count > (size_t)-1 / size) {
		fatal_error("SuperSight scene exceeds addressable memory");
		return NULL;
	}
	void *result = malloc(count * size);
	if (result == NULL) {
		fatal_error("Cannot allocate SuperSight scene queue");
	}
	return result;
}

static void polyinfo_reserve(void)
{
	if (!polyinfo_supersight_enabled) {
		return;
	}
	if (polyinfonumpolys >= polyinfo_primitive_capacity) {
		polyinfo_index previous_capacity = polyinfo_primitive_capacity;
		/* Links use a signed index so that -1 remains the end marker. */
		if (previous_capacity > LEGACY_S32_MAX / 2U) {
			fatal_error("SuperSight scene has too many primitives");
			return;
		}
		polyinfo_index capacity = previous_capacity * 2U;
		polyinfo_link *links = polyinfo_allocate((size_t)capacity + 1U, sizeof(*links));
		polyinfo_offset *offsets = polyinfo_allocate(capacity, sizeof(*offsets));
		memcpy(links, polygon_next_index, ((size_t)previous_capacity + 1U) * sizeof(*links));
		memcpy(offsets, polygon_record_offsets, (size_t)previous_capacity * sizeof(*offsets));
		/* The head sentinel lives after all primitive slots. Move it before
		 * its former slot becomes the next primitive, including mid-shape growth. */
		links[capacity] = links[previous_capacity];
		if (shape_polygon_predecessor == previous_capacity) {
			shape_polygon_predecessor = capacity;
		}
		if (polygon_insertion_cursor == previous_capacity) {
			polygon_insertion_cursor = capacity;
		}
		if (polygon_list_tail == previous_capacity) {
			polygon_list_tail = capacity;
		}
		free(native_polygon_links);
		free(native_polygon_offsets);
		polygon_next_index = native_polygon_links = links;
		polygon_record_offsets = native_polygon_offsets = offsets;
		polyinfo_primitive_capacity = native_primitive_capacity = capacity;
	}
	if (polyinfoptrnext > LEGACY_U32_MAX - POLYINFO_MAX_RECORD_SIZE) {
		fatal_error("SuperSight scene data exceeds addressable memory");
		return;
	}
	polyinfo_offset required = polyinfoptrnext + POLYINFO_MAX_RECORD_SIZE;
	if (required > native_data_capacity) {
		polyinfo_offset capacity = native_data_capacity;
		if (capacity == 0) {
			capacity = POLYINFO_SUPERSIGHT_DATA_SIZE;
		}
		while (capacity < required) {
			capacity = capacity <= LEGACY_U32_MAX / 2U ? capacity * 2U : required;
		}
		legacy_u8 *data = polyinfo_allocate(capacity, sizeof(*data));
		if (polyinfoptrnext != 0) {
			memcpy(data, polyinfoptr, polyinfoptrnext);
		}
		free(native_polyinfo);
		polyinfoptr = native_polyinfo = data;
		native_data_capacity = capacity;
	} else if (polyinfoptr != native_polyinfo) {
		/* init_polyinfo and isolated render targets can supply a fresh legacy
		 * buffer. Reuse native storage without taking ownership of that buffer. */
		if (polyinfoptrnext != 0) {
			memcpy(native_polyinfo, polyinfoptr, polyinfoptrnext);
		}
		polyinfoptr = native_polyinfo;
	}
	polyinfo_last_valid_offset = native_data_capacity - POLYINFO_MAX_RECORD_SIZE;
}
#endif

#define PROJECTION_EXTENT_SCALE 2048L
#define PROJECTION_EXTENT_DIVISOR 360L
#define PROJECTION_VIEW_VECTOR_LENGTH 10000
#define PROJECTION_YAW_MASK 1023U
#define PROJECTION_SINCOS_ANGLE 128

static legacy_u16 shape3d_average_depth(legacy_s32 sum, legacy_u16 vertex_count)
{
	switch (vertex_count) {
		case 1U:
			return (legacy_u16)sum;
		case 2U:
			return (legacy_u16)LEGACY_S32_SAR(sum, 1U);
		case 4U:
			return (legacy_u16)LEGACY_S32_SAR(sum, 2U);
		case 8U:
			return (legacy_u16)LEGACY_S32_SAR(sum, 3U);
		default:
			/* The original calls __aFuldiv even for negative depth sums.
			 * Preserve that unsigned division; powers of two use signed shifts. */
			return (legacy_u16)LEGACY_U32_DIV_OR_ZERO((legacy_u32)sum, vertex_count);
	}
}

static legacy_u16 polyinfo_read_word(const legacy_u8 far *record, legacy_u16 word_index)
{
	return LEGACY_READ_U16_LE(record + LEGACY_U16_WRAP_MUL(word_index, LEGACY_WORD_BYTES));
}

static void polyinfo_write_word(legacy_u8 far *record, legacy_u16 word_index, legacy_u16 value)
{
	LEGACY_WRITE_U16_LE(record + LEGACY_U16_WRAP_MUL(word_index, LEGACY_WORD_BYTES), value);
}

static void polyinfo_read_point(const legacy_u8 far *record, legacy_u16 point_index,
								struct POINT2D *point)
{
	legacy_u16 word_index = LEGACY_U16_WRAP_ADD(
		POLYINFO_HEADER_WORD_COUNT, LEGACY_U16_WRAP_MUL(point_index, POLYINFO_POINT_WORD_COUNT));
	point->px = LEGACY_S16_FROM_BITS(polyinfo_read_word(record, word_index));
	point->py =
		LEGACY_S16_FROM_BITS(polyinfo_read_word(record, LEGACY_U16_WRAP_ADD(word_index, 1U)));
}

static void polyinfo_read_points(const legacy_u8 far *record, struct POINT2D *points,
								 legacy_u16 point_count)
{
	for (legacy_u16 index = 0; index < point_count; index++) {
		polyinfo_read_point(record, index, &points[index]);
	}
}

static void polyinfo_write_point(legacy_u8 far *record, legacy_u16 point_index,
								 const struct POINT2D *point)
{
	legacy_u16 word_index = LEGACY_U16_WRAP_ADD(
		POLYINFO_HEADER_WORD_COUNT, LEGACY_U16_WRAP_MUL(point_index, POLYINFO_POINT_WORD_COUNT));
	polyinfo_write_word(record, word_index, (legacy_u16)point->px);
	polyinfo_write_word(record, LEGACY_U16_WRAP_ADD(word_index, 1U), (legacy_u16)point->py);
}

#if defined(RESTUNTS_TEMPLEOS)
#define SHAPE3D_NATIVE_SCALE 2
/* Bound cross products and packed wheel deltas as well as projected points. */
#define SHAPE3D_NATIVE_COORD_LIMIT ((legacy_s32)LEGACY_S16_MAX / SHAPE3D_NATIVE_SCALE)

extern void TempleRasterNative(legacy_u8 enabled);

static legacy_s16 shape3d_native_coordinate(legacy_s64 value)
{
	if (value < -SHAPE3D_NATIVE_COORD_LIMIT) {
		return -SHAPE3D_NATIVE_COORD_LIMIT;
	}
	if (value > SHAPE3D_NATIVE_COORD_LIMIT) {
		return SHAPE3D_NATIVE_COORD_LIMIT;
	}
	return (legacy_s16)value;
}

static void shape3d_project_coordinates(legacy_s64 x, legacy_s64 y, legacy_s64 z,
										struct POINT2D *point)
{
	/* Scale before division: half a DOS pixel is a complete native pixel. */
	point->px = shape3d_native_coordinate((legacy_s64)(legacy_s16)projection_center_x *
											  SHAPE3D_NATIVE_SCALE +
										  x * projection_focal_length_x * SHAPE3D_NATIVE_SCALE / z);
	point->py = shape3d_native_coordinate((legacy_s64)(legacy_s16)projection_center_y *
											  SHAPE3D_NATIVE_SCALE -
										  y * projection_focal_length_y * SHAPE3D_NATIVE_SCALE / z);
}

static void shape3d_project_point(struct VECTOR *vector, struct POINT2D *point)
{
	shape3d_project_coordinates(vector->x, vector->y, vector->z, point);
}

static legacy_s16 shape3d_logical_coordinate(legacy_s16 value)
{
	/* Floor negative coordinates so clipping and saved backgrounds agree. */
	if (value < 0) {
		return (
			legacy_s16)(-((-(legacy_s32)value + SHAPE3D_NATIVE_SCALE - 1) / SHAPE3D_NATIVE_SCALE));
	}
	return value / SHAPE3D_NATIVE_SCALE;
}

static legacy_u16 shape3d_geometry_clip_flags(struct POINT2D *point)
{
	struct POINT2D logical;
	logical.px = shape3d_logical_coordinate(point->px);
	logical.py = shape3d_logical_coordinate(point->py);
	return rect_compare_point(&logical);
}

static void shape3d_adjust_geometry_bounds(struct POINT2D *point, struct RECTANGLE *rectangle)
{
	struct POINT2D logical;
	logical.px = shape3d_logical_coordinate(point->px);
	logical.py = shape3d_logical_coordinate(point->py);
	rect_adjust_from_point(&logical, rectangle);
}
#else
#define shape3d_project_point vector_to_point
#define shape3d_geometry_clip_flags rect_compare_point
#define shape3d_adjust_geometry_bounds rect_adjust_from_point
#endif

/* Emitting a polygon point always appends it to the polyinfo record and
 * narrows the clip flags by the same test. */
static void polyinfo_emit_point(legacy_u16 *point_index, legacy_u8 *rect_flags,
								struct POINT2D *point)
{
	polyinfo_write_point(transshapepolyinfo, *point_index, point);
	if (*rect_flags != 0) {
		*rect_flags &= shape3d_geometry_clip_flags(point);
	}
	*point_index = LEGACY_U16_WRAP_ADD(*point_index, 1U);
}

static legacy_s8 polyinfo_is_facing_camera(const legacy_u8 far *record)
{
	struct POINT2D points[3];
	for (legacy_u16 index = 0; index < 3U; index++) {
		polyinfo_read_point(record, index, &points[index]);
	}
	return is_facing_camera(points);
}

static void shape3d_transform_vertex(const struct SHAPE3D *shape, legacy_u16 index,
									 legacy_s16 half_scale, struct MATRIX *matrix,
									 struct VECTOR *translation, struct VECTOR *transformed)
{
	struct VECTOR source;

	shape3d_vertex_read(shape, index, &source);
	if (half_scale != 0) {
		/* Arithmetic shifts preserve the original flooring for negatives. */
		source.x = LEGACY_S16_SAR(source.x, 1U);
		source.y = LEGACY_S16_SAR(source.y, 1U);
		source.z = LEGACY_S16_SAR(source.z, 1U);
	}
	mat_mul_vector(&source, matrix, transformed);
	transformed->x = LEGACY_S16_WRAP_ADD(transformed->x, translation->x);
	transformed->y = LEGACY_S16_WRAP_ADD(transformed->y, translation->y);
	transformed->z = LEGACY_S16_WRAP_ADD(transformed->z, translation->z);
}

/* Per-shape cache stays on the caller's stack, as in the original transform loop. */
struct SHAPE3D_TRANSFORM_CONTEXT {
	legacy_u8 vertex_clip_flags[SHAPE3D_VERTEX_FLAG_CAPACITY];
	struct MATRIX object_to_view_rotation;
	struct VECTOR view_translation;
	struct VECTOR view_vertices[SHAPE3D_VERTEX_CAPACITY];
	struct POINT2D projected_vertices[SHAPE3D_VERTEX_CAPACITY];
#if defined(RESTUNTS_SDL3)
	struct MATRIX object_to_world_rotation;
	struct VECTOR object_translation;
	struct SHAPE3D_HIRES_VECTOR view_origin;
	struct SHAPE3D_HIRES_VECTOR hires_vertices[SHAPE3D_VERTEX_CAPACITY];
	legacy_u32 shadow_surfaces[SHAPE3D_SHADOW_PRIMITIVE_CAPACITY];
	legacy_u32 shadow_surface_count;
#endif
	legacy_s32 visibility_mask;
	legacy_s32 front_facing_mask;
};

#if defined(RESTUNTS_SDL3)
static void shape3d_hires_rotate(const struct SHAPE3D_HIRES_VECTOR *source,
								 const struct MATRIX *matrix, struct SHAPE3D_HIRES_VECTOR *result)
{
	result->x =
		(source->x * matrix->m._11 + source->y * matrix->m._12 + source->z * matrix->m._13) /
		TRIG_FIXED_ONE;
	result->y =
		(source->x * matrix->m._21 + source->y * matrix->m._22 + source->z * matrix->m._23) /
		TRIG_FIXED_ONE;
	result->z =
		(source->x * matrix->m._31 + source->y * matrix->m._32 + source->z * matrix->m._33) /
		TRIG_FIXED_ONE;
}

static void shape3d_hires_transform_vertex(const struct SHAPE3D *shape, legacy_u16 index,
										   struct SHAPE3D_TRANSFORM_CONTEXT *context)
{
	struct VECTOR vertex;
	shape3d_vertex_read(shape, index, &vertex);
	legacy_f64 scale = shape_half_scale != 0 ? 0.5 : 1.0;
	struct SHAPE3D_HIRES_VECTOR source = {vertex.x * scale, vertex.y * scale, vertex.z * scale};
	struct SHAPE3D_HIRES_VECTOR world;
	shape3d_hires_rotate(&source, &context->object_to_world_rotation, &world);
	struct SHAPE3D_HIRES_VECTOR *view = &context->hires_vertices[index];
	if ((transshapeflags & SHAPE3D_PRETRANSFORMED_FLAG) != 0) {
		shape3d_hires_rotate(&world, &mat_temp, view);
		view->x += context->object_translation.x;
		view->y += context->object_translation.y;
		view->z += context->object_translation.z;
	} else {
		/* Adjacent shapes can express one world vertex using different local
		 * coordinates. Combine position before the camera transform so both
		 * reach exactly the same screen position, without per-shape rounding. */
		world.x += context->object_translation.x;
		world.y += context->object_translation.y;
		world.z += context->object_translation.z;
		shape3d_hires_rotate(&world, &mat_temp, view);
	}
}
#endif

static void shape3d_prepare_instance(struct TRANSFORMEDSHAPE3D *instance,
									 struct SHAPE3D_TRANSFORM_CONTEXT *context)
{
	transshapeprimitives = instance->shapeptr->shape3d_primitives;
	transshapenumpaints = instance->shapeptr->shape3d_numpaints;
	transshapematerial = instance->material;
	if (transshapematerial >= transshapenumpaints) {
		transshapematerial = 0;
	}
	transshapeflags = instance->ts_flags;
	if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
		transshaperectptr = instance->rectptr;
	}
	for (legacy_u16 i = 0; i < transshapenumverts; i++) {
		context->vertex_clip_flags[i] = SHAPE3D_VERTEX_UNTRANSFORMED;
	}

	context->visibility_mask = -1;
	context->front_facing_mask = 0;
	struct MATRIX *object_rotation = mat_rot_zxy(instance->rotvec.x, instance->rotvec.y,
												 instance->rotvec.z, MATRIX_ROTATION_ORDER_ZXY);
#if defined(RESTUNTS_SDL3)
	context->shadow_surface_count = 0;
	context->object_to_world_rotation = *object_rotation;
	context->object_translation = instance->pos;
#endif
	if ((transshapeflags & SHAPE3D_PRETRANSFORMED_FLAG) != 0) {
		mat_multiply(object_rotation, &mat_temp, &context->object_to_view_rotation);
		context->view_translation = instance->pos;
#if defined(RESTUNTS_SDL3)
		context->view_origin =
			(struct SHAPE3D_HIRES_VECTOR){instance->pos.x, instance->pos.y, instance->pos.z};
#endif
		return;
	}
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		struct SHAPE3D_HIRES_VECTOR origin = {instance->pos.x, instance->pos.y, instance->pos.z};
		shape3d_hires_rotate(&origin, &mat_temp, &context->view_origin);
	} else
#endif
	{
		mat_mul_vector(&instance->pos, &mat_temp, &context->view_translation);
	}
	mat_multiply(object_rotation, &mat_temp, &context->object_to_view_rotation);
}

/* Directional visibility cannot affect the bounds test. Prepare it only for
 * surviving instances, and only when their existing distance gate needs it. */
static void shape3d_prepare_visibility(struct TRANSFORMEDSHAPE3D *instance,
									   struct SHAPE3D_TRANSFORM_CONTEXT *context)
{
	if ((transshapeflags & SHAPE3D_PRETRANSFORMED_FLAG) != 0) {
		return;
	}
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		legacy_f64 reach = (legacy_f64)instance->culling_distance * 2;
		if (context->view_origin.x > -reach && context->view_origin.x < reach &&
			context->view_origin.z > -reach && context->view_origin.z < reach) {
			return;
		}
	} else
#endif
	{
		if (LEGACY_S16_SHL(instance->culling_distance, 1U) >
				absolute_word(context->view_translation.x) &&
			LEGACY_S16_SHL(instance->culling_distance, 1U) >
				absolute_word(context->view_translation.z)) {
			return;
		}
	}
	struct MATRIX inverse_view_rotation;
	mat_invert(&context->object_to_view_rotation, &inverse_view_rotation);
	struct VECTOR forward_vector;
	forward_vector.x = 0;
	forward_vector.y = 0;
	forward_vector.z = SHAPE3D_FORWARD_VECTOR_SCALE;
	struct VECTOR view_direction;
	mat_mul_vector(&forward_vector, &inverse_view_rotation, &view_direction);
	if (view_direction.y <= 0 || instance->pos.y >= 0) {
		shape_view_direction_sector = vector_direction_sector(&view_direction);
		context->visibility_mask = invpow2tbl[shape_view_direction_sector];
		context->front_facing_mask = invpow2tbl[shape_view_direction_sector];
	}
}

static void shape3d_cache_vertex(const struct SHAPE3D *shape,
								 struct SHAPE3D_TRANSFORM_CONTEXT *context, legacy_u16 index)
{
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		shape3d_hires_transform_vertex(shape, index, context);
		context->vertex_clip_flags[index] = context->hires_vertices[index].z < SHAPE3D_NEAR_CLIP_Z;
		return;
	}
#endif
	struct VECTOR transformed;
	shape3d_transform_vertex(shape, index, shape_half_scale, &context->object_to_view_rotation,
							 &context->view_translation, &transformed);
	context->view_vertices[index] = transformed;
	if (transformed.z < SHAPE3D_NEAR_CLIP_Z) {
		context->vertex_clip_flags[index] = 1;
	} else {
		context->vertex_clip_flags[index] = 0;
		shape3d_project_point(&transformed, &context->projected_vertices[index]);
	}
}

static legacy_u8 shape3d_vertex_rect_flags(struct SHAPE3D_TRANSFORM_CONTEXT *context,
										   legacy_u16 index)
{
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		return shape3d_hires_clip_flags(&context->hires_vertices[index]);
	}
#endif
	return (legacy_u8)shape3d_geometry_clip_flags(&context->projected_vertices[index]);
}

static legacy_u16 shape3d_bounds_are_clipped(struct TRANSFORMEDSHAPE3D *instance,
											 struct SHAPE3D_TRANSFORM_CONTEXT *context)
{
	if (transshapenumverts <= 8) {
		transshapenumvertscopy = transshapenumverts;
	} else {
		transshapenumvertscopy = 8;
	}
	struct VECTOR fifth_vertex;
	struct VECTOR first_vertex;
	if (transshapenumvertscopy > 4) {
		shape3d_vertex_read(instance->shapeptr, 0U, &first_vertex);
		shape3d_vertex_read(instance->shapeptr, 4U, &fifth_vertex);
		if (first_vertex.y == fifth_vertex.y) {
			transshapenumvertscopy = 4;
		}
	}

	legacy_u8 common_clip_flags = SHAPE3D_ALL_RECT_CLIP_FLAGS;
	legacy_u16 i;
	legacy_u16 all_vertices_behind = 1;
	legacy_u16 any_vertex_behind = 0;
	for (i = 0; i < transshapenumvertscopy; i = LEGACY_U16_WRAP_ADD(i, 1U)) {
		polyvertpointptrtab[i] = &context->projected_vertices[i];
		shape3d_cache_vertex(instance->shapeptr, context, i);
		if (context->vertex_clip_flags[i] != 0) {
			any_vertex_behind = 1;
			continue;
		}
		all_vertices_behind = 0;
		if (common_clip_flags != 0) {
			common_clip_flags &= shape3d_vertex_rect_flags(context, i);
		}
		if (common_clip_flags == 0) {
			break;
		}
	}
	return i == transshapenumvertscopy && (all_vertices_behind != 0 || any_vertex_behind == 0 ||
										   LEGACY_S16_FROM_BITS(instance->culling_distance) <
											   absolute_word(context->view_translation.x));
}

#if defined(RESTUNTS_SDL3)
#define SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT 8U
#define SHAPE3D_SHADOW_PRIMITIVE_FLAGS_OFFSET 1U

static legacy_u16 shape3d_material_value(const legacy_s16 *table, legacy_u16 index);

static void shape3d_collect_shadow_polygons(const struct TRANSFORMEDSHAPE3D *instance,
											struct SHAPE3D_TRANSFORM_CONTEXT *context)
{
	if (!shape3d_hires_shadows_active() || !hires_enabled() ||
		hires_render_scale() == HIRES_MINIMUM_SCALE ||
		(transshapeflags &
		 (SHAPE3D_NO_SHADOW_RECEIVE_FLAG | SHAPE3D_GHOST_FLAG | SHAPE3D_BACKGROUND_FLAG)) != 0) {
		return;
	}
	const struct SHAPE3D *shape = instance->shapeptr;
	legacy_u32 bounds_count = transshapenumverts < SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT
								  ? transshapenumverts
								  : SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT;
	/* The camera bounds test can stop early or use only four flat corners. */
	for (legacy_u32 vertex = 0; vertex < bounds_count; vertex++) {
		if (context->vertex_clip_flags[vertex] == SHAPE3D_VERTEX_UNTRANSFORMED) {
			shape3d_cache_vertex(shape, context, (legacy_u16)vertex);
		}
	}
	if (!shape3d_hires_shadow_overlap(context->hires_vertices, bounds_count)) {
		return;
	}
	/* Shadow blockers retain their geometry even when camera visibility rejects them. */
	const legacy_u8 *primitive = shape->shape3d_primitives;
	while (primitive[0] != 0 && context->shadow_surface_count < SHAPE3D_SHADOW_PRIMITIVE_CAPACITY) {
		legacy_u32 surface = 0;
		legacy_u32 count = primidxcounttab[primitive[0]];
		const legacy_u8 *indices = primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + transshapenumpaints;
		if (primtypetab[primitive[0]] == RENDER_PRIMITIVE_POLYGON &&
			count <= SHAPE3D_POLYGON_MAX_VERTICES &&
			(primitive[SHAPE3D_SHADOW_PRIMITIVE_FLAGS_OFFSET] &
			 SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) == 0) {
			legacy_u16 material = primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE + transshapematerial];
			legacy_u16 pattern_type = shape3d_material_value(material_patlist_ptr_cpy, material);
			if (pattern_type <= SHAPE3D_PATTERN_TWO_COLOR &&
				(pattern_type != SHAPE3D_PATTERN_MASKED ||
				 shape3d_material_value(material_patlist2_ptr_cpy, material) != 0U)) {
				struct SHAPE3D_HIRES_VECTOR vertices[SHAPE3D_POLYGON_MAX_VERTICES];
				legacy_u32 vertex;
				for (vertex = 0; vertex < count; vertex++) {
					legacy_u16 index = indices[vertex];
					if (index >= transshapenumverts) {
						break;
					}
					if (context->vertex_clip_flags[index] == SHAPE3D_VERTEX_UNTRANSFORMED) {
						shape3d_cache_vertex(shape, context, index);
					}
					vertices[vertex] = context->hires_vertices[index];
				}
				if (vertex == count) {
					surface = shape3d_hires_shadow_polygon(vertices, count,
														   pattern_type == SHAPE3D_PATTERN_MASKED);
				}
			}
		}
		context->shadow_surfaces[context->shadow_surface_count++] = surface;
		primitive = indices + count;
	}
}
#endif

static legacy_u16 shape3d_prepare_primitive_vertices(const struct SHAPE3D *shape,
													 struct SHAPE3D_TRANSFORM_CONTEXT *context,
													 legacy_u16 *any_vertex_behind)
{
	legacy_u8 common_clip_flags = SHAPE3D_ALL_RECT_CLIP_FLAGS;
	*any_vertex_behind = 0;
	transshapeprimindexptr = transshapeprimitives;
	legacy_u16 all_vertices_behind = 1;
	for (legacy_u16 vertex_count = 0; vertex_count < transshapenumvertscopy;
		 vertex_count = LEGACY_U16_WRAP_ADD(vertex_count, 1U)) {
		legacy_u16 vertex_index = transshapeprimindexptr[0];
		transshapeprimindexptr++;
		polyvertpointptrtab[vertex_count] = &context->projected_vertices[vertex_index];
		if (context->vertex_clip_flags[vertex_index] == SHAPE3D_VERTEX_UNTRANSFORMED) {
			shape3d_cache_vertex(shape, context, vertex_index);
		}
		if (context->vertex_clip_flags[vertex_index] == 0) {
			all_vertices_behind = 0;
			if (common_clip_flags != 0) {
				common_clip_flags &= shape3d_vertex_rect_flags(context, vertex_index);
			}
		} else if (context->vertex_clip_flags[vertex_index] == 1) {
			*any_vertex_behind = 1;
		}
	}
	return all_vertices_behind == 0 && (common_clip_flags == 0 || *any_vertex_behind != 0);
}

static void shape3d_project_near_intersection(struct SHAPE3D_TRANSFORM_CONTEXT *context,
											  legacy_u16 front_index, legacy_u16 behind_index,
											  struct POINT2D *point)
{
#if defined(RESTUNTS_TEMPLEOS)
	const struct VECTOR *front = &context->view_vertices[front_index];
	const struct VECTOR *behind = &context->view_vertices[behind_index];
	legacy_s64 depth_span = (legacy_s64)front->z - behind->z;
	legacy_s64 near_offset = SHAPE3D_NEAR_CLIP_Z - (legacy_s64)behind->z;
	/* Preserve the intersection's fraction until the native projection divides. */
	shape3d_project_coordinates(
		behind->x * depth_span + ((legacy_s64)front->x - behind->x) * near_offset,
		behind->y * depth_span + ((legacy_s64)front->y - behind->y) * near_offset,
		SHAPE3D_NEAR_CLIP_Z * depth_span, point);
#else
	struct VECTOR intersection;

	vector_interpolate_at_z(&context->view_vertices[front_index],
							&context->view_vertices[behind_index], &intersection,
							SHAPE3D_NEAR_CLIP_Z);
	shape3d_project_point(&intersection, point);
#endif
}

static void shape3d_emit_polygon_intersection(struct SHAPE3D_TRANSFORM_CONTEXT *context,
											  legacy_u16 front_index, legacy_u16 behind_index,
											  legacy_u16 *point_count, legacy_u8 *clip_flags)
{
	struct POINT2D point;

	shape3d_project_near_intersection(context, front_index, behind_index, &point);
	if (point.px != context->projected_vertices[front_index].px ||
		point.py != context->projected_vertices[front_index].py) {
		polyinfo_emit_point(point_count, clip_flags, &point);
	}
}

static void shape3d_emit_clipped_polygon_edge(struct SHAPE3D_TRANSFORM_CONTEXT *context,
											  legacy_u16 previous_index, legacy_u16 vertex_index,
											  legacy_u16 *point_count, legacy_u8 *clip_flags,
											  struct POINT2D *projected_point)
{
	if (context->vertex_clip_flags[vertex_index] != 0) {
		if (context->vertex_clip_flags[previous_index] == 0) {
			shape3d_emit_polygon_intersection(context, previous_index, vertex_index, point_count,
											  clip_flags);
		}
	} else {
		if (context->vertex_clip_flags[previous_index] != 0) {
			shape3d_emit_polygon_intersection(context, vertex_index, previous_index, point_count,
											  clip_flags);
		}
		polyinfo_emit_point(point_count, clip_flags, projected_point);
	}
}

static legacy_u8 shape3d_emit_polygon(struct SHAPE3D_TRANSFORM_CONTEXT *context,
									  legacy_u16 any_vertex_behind, legacy_s32 *depth_sum)
{
	transshapeprimindexptr = transshapeprimitives;
	*depth_sum = 0;
	legacy_u8 common_clip_flags = SHAPE3D_ALL_RECT_CLIP_FLAGS;
	legacy_u16 previous_vertex_index = 0;
	if (any_vertex_behind != 0) {
		previous_vertex_index = transshapeprimitives[transshapenumvertscopy - 1];
	}
	legacy_u16 output_point_index = 0U;
	for (legacy_u16 i = 0; i < transshapenumvertscopy; i = LEGACY_U16_WRAP_ADD(i, 1U)) {
		legacy_u16 vertex_index = transshapeprimindexptr[0];
		transshapeprimindexptr++;
		*depth_sum = LEGACY_S32_WRAP_ADD_S16(*depth_sum, context->view_vertices[vertex_index].z);
		if (any_vertex_behind == 0) {
			polyinfo_emit_point(&output_point_index, &common_clip_flags, polyvertpointptrtab[i]);
		} else {
			shape3d_emit_clipped_polygon_edge(context, previous_vertex_index, vertex_index,
											  &output_point_index, &common_clip_flags,
											  polyvertpointptrtab[i]);
			previous_vertex_index = vertex_index;
		}
	}
	if (any_vertex_behind != 0) {
		transshapenumvertscopy = output_point_index;
	}
	return common_clip_flags;
}

static void shape3d_adjust_polygon_bounds(void)
{
	struct POINT2D point;
	for (legacy_u16 i = 0; i < transshapenumvertscopy; i = LEGACY_U16_WRAP_ADD(i, 1U)) {
		polyinfo_read_point(transshapepolyinfo, i, &point);
#if defined(RESTUNTS_TEMPLEOS)
		point.px = shape3d_logical_coordinate(point.px);
		point.py = shape3d_logical_coordinate(point.py);
#endif
		if (point.px < transshaperectptr->left) {
			transshaperectptr->left = point.px;
		}
		if (transshaperectptr->right < point.px + 1) {
			transshaperectptr->right = point.px + 1;
		}
		if (transshaperectptr->top > point.py) {
			transshaperectptr->top = point.py;
		}
		if (transshaperectptr->bottom < point.py + 1) {
			transshaperectptr->bottom = point.py + 1;
		}
	}
}

static legacy_u16 shape3d_prepare_polygon(struct SHAPE3D_TRANSFORM_CONTEXT *context,
										  legacy_u16 any_vertex_behind, legacy_u16 primitive_flags,
										  const legacy_u8 far *front_facing_masks,
										  legacy_s32 *depth_sum)
{
	legacy_u8 common_clip_flags = shape3d_emit_polygon(context, any_vertex_behind, depth_sum);
	legacy_u16 cull_backface =
		(primitive_flags & SHAPE3D_PRIMITIVE_ALWAYS_VISIBLE_FLAG) == 0 &&
		((legacy_u32)context->front_facing_mask & LEGACY_READ_U32_LE(front_facing_masks)) == 0UL;
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		if (!shape3d_hires_polygon_visible(polyinfonumpolys, cull_backface)) {
			return 0;
		}
	} else
#endif
	{
		if (transshapenumvertscopy == 0 || common_clip_flags != 0 ||
			(cull_backface != 0 && polyinfo_is_facing_camera(transshapepolyinfo) == 0)) {
			return 0;
		}
	}
	if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
		shape3d_adjust_polygon_bounds();
	}
	return 1;
}

static legacy_u16 shape3d_prepare_line(struct SHAPE3D_TRANSFORM_CONTEXT *context,
									   legacy_s32 *depth_sum)
{
	legacy_u16 first_index = transshapeprimitives[0];
	legacy_u16 second_index = transshapeprimitives[1];
	if (context->vertex_clip_flags[first_index] + context->vertex_clip_flags[second_index] == 2) {
		return 0;
	}
	if (context->vertex_clip_flags[first_index] != 0) {
		shape3d_project_near_intersection(context, second_index, first_index,
										  &context->projected_vertices[first_index]);
	} else if (context->vertex_clip_flags[second_index] != 0) {
		shape3d_project_near_intersection(context, first_index, second_index,
										  &context->projected_vertices[second_index]);
	}

	/* Preserve the signed 16-bit sum used to sort lines, including car wheels. */
	*depth_sum = (legacy_s32)LEGACY_S16_WRAP_ADD(context->view_vertices[first_index].z,
												 context->view_vertices[second_index].z);
	polyinfo_write_point(transshapepolyinfo, 0U, polyvertpointptrtab[0]);
	polyinfo_write_point(transshapepolyinfo, 1U, polyvertpointptrtab[1]);
	if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
		shape3d_adjust_geometry_bounds(polyvertpointptrtab[0], transshaperectptr);
		shape3d_adjust_geometry_bounds(polyvertpointptrtab[1], transshaperectptr);
	}
	transshapenumvertscopy = 2;
	return 1;
}

static void shape3d_adjust_round_bounds(const struct POINT2D *center, legacy_u16 radius,
										legacy_s16 padding)
{
	struct POINT2D point;

#if defined(RESTUNTS_TEMPLEOS)
	/* Large offscreen wheels must not wrap their saved-background bounds. */
	legacy_s64 extent = (legacy_s64)radius + padding * SHAPE3D_NATIVE_SCALE;
	point.px = shape3d_native_coordinate(center->px - extent);
	point.py = shape3d_native_coordinate(center->py - extent);
	shape3d_adjust_geometry_bounds(&point, transshaperectptr);
	point.px = shape3d_native_coordinate(center->px + extent);
	point.py = shape3d_native_coordinate(center->py + extent);
#else
	point.px = LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_SUB(center->px, radius), padding);
	point.py = LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_SUB(center->py, radius), padding);
	shape3d_adjust_geometry_bounds(&point, transshaperectptr);
	point.px = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(center->px, radius), padding);
	point.py = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(center->py, radius), padding);
#endif
	shape3d_adjust_geometry_bounds(&point, transshaperectptr);
}

static legacy_u16 shape3d_prepare_wheel(struct SHAPE3D_TRANSFORM_CONTEXT *context,
										legacy_u16 any_vertex_behind, legacy_s32 *depth_sum)
{
	if (any_vertex_behind != 0) {
		return 0;
	}
	struct POINT2D points[4];
	for (legacy_u16 i = 0; i < 4; i++) {
		points[i] = *polyvertpointptrtab[i];
		polyinfo_write_point(transshapepolyinfo, i, &points[i]);
	}
	legacy_s16 first_face = is_facing_camera(points);
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		first_face = shape3d_hires_wheel_face(polyinfonumpolys) == 0;
	}
#endif
	if (first_face != 0) {
		*depth_sum =
			LEGACY_S32_SHL((legacy_s32)context->view_vertices[transshapeprimitives[0]].z, 2U);
	} else {
		points[0] = *polyvertpointptrtab[3];
		points[1] = *polyvertpointptrtab[4];
		points[2] = *polyvertpointptrtab[5];
		points[3] = *polyvertpointptrtab[0];
		for (legacy_u16 i = 0; i < 4; i++) {
			polyinfo_write_point(transshapepolyinfo, i, &points[i]);
		}
		*depth_sum =
			LEGACY_S32_SHL((legacy_s32)context->view_vertices[transshapeprimitives[3]].z, 2U);
	}

	legacy_u16 radius = polarRadius2D(LEGACY_S16_WRAP_SUB(points[0].px, points[1].px),
									  LEGACY_S16_WRAP_SUB(points[0].py, points[1].py));
	legacy_u16 other_radius = polarRadius2D(LEGACY_S16_WRAP_SUB(points[0].px, points[2].px),
											LEGACY_S16_WRAP_SUB(points[0].py, points[2].py));
	if (other_radius > radius) {
		radius = other_radius;
	}
	if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
		shape3d_adjust_round_bounds(&points[0], radius, 1);
		shape3d_adjust_round_bounds(&points[3], radius, 1);
	}
	transshapenumvertscopy = 4;
	return 1;
}

static void shape3d_adjust_sphere_bounds(const struct POINT2D *center, legacy_u16 radius)
{
	struct POINT2D point;

	point.px = LEGACY_S16_WRAP_SUB(center->px, radius);
	point.py = LEGACY_S16_WRAP_SUB(center->py, radius);
	shape3d_adjust_geometry_bounds(&point, transshaperectptr);
	/* Original SEG006 sphere code overwrites the second point's X with Y + radius;
	 * its Y stays at Y - radius. Crash explosions scale from these bounds. */
	point.px = LEGACY_S16_WRAP_ADD(center->py, radius);
	shape3d_adjust_geometry_bounds(&point, transshaperectptr);
}

static legacy_u16 shape3d_prepare_sphere(struct SHAPE3D_TRANSFORM_CONTEXT *context,
										 legacy_s32 *depth_sum)
{
	legacy_u16 center_index = transshapeprimitives[0];
	legacy_u16 radius_index = transshapeprimitives[1];
	*depth_sum = (legacy_s32)LEGACY_S16_WRAP_ADD(context->view_vertices[center_index].z,
												 context->view_vertices[radius_index].z);
	if (context->vertex_clip_flags[center_index] + context->vertex_clip_flags[radius_index] != 0) {
		return 0;
	}
	polyinfo_write_point(transshapepolyinfo, 0U, polyvertpointptrtab[0]);
	struct VECTOR center = context->view_vertices[center_index];
	struct VECTOR endpoint = context->view_vertices[radius_index];
	struct VECTOR radius_vector;
	radius_vector.x = LEGACY_S16_WRAP_SUB(center.x, endpoint.x);
	radius_vector.y = LEGACY_S16_WRAP_SUB(center.y, endpoint.y);
	radius_vector.z = LEGACY_S16_WRAP_SUB(center.z, endpoint.z);
#if defined(RESTUNTS_TEMPLEOS)
	legacy_u16 screen_radius = shape3d_native_coordinate((legacy_s64)projection_focal_length_x *
														 (legacy_u16)polarRadius3D(&radius_vector) *
														 SHAPE3D_NATIVE_SCALE / center.z);
#else
	legacy_u16 screen_radius = projection_scale_x_wrapped(polarRadius3D(&radius_vector), center.z);
#endif
	polyinfo_write_word(transshapepolyinfo, 5U, screen_radius);
	if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
		shape3d_adjust_sphere_bounds(polyvertpointptrtab[0], screen_radius);
	}
	transshapenumvertscopy = 2;
	return 1;
}

static legacy_u16 shape3d_prepare_point(struct SHAPE3D_TRANSFORM_CONTEXT *context,
										legacy_s32 *depth_sum)
{
	legacy_u16 vertex_index = transshapeprimitives[0];
	if (context->vertex_clip_flags[vertex_index] != 0) {
		return 0;
	}
	*depth_sum = context->view_vertices[vertex_index].z;
	polyinfo_write_point(transshapepolyinfo, 0U, polyvertpointptrtab[0]);
	if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
		shape3d_adjust_geometry_bounds(polyvertpointptrtab[0], transshaperectptr);
	}
	transshapenumvertscopy = 1;
	return 1;
}

static legacy_u16 shape3d_prepare_primitive(struct SHAPE3D_TRANSFORM_CONTEXT *context,
											legacy_u8 primitive_type, legacy_u16 any_vertex_behind,
											legacy_u16 primitive_flags,
											const legacy_u8 far *front_facing_masks,
											legacy_s32 *depth_sum)
{
	switch (primitive_type) {
		case RENDER_PRIMITIVE_POLYGON:
			return shape3d_prepare_polygon(context, any_vertex_behind, primitive_flags,
										   front_facing_masks, depth_sum);
		case RENDER_PRIMITIVE_LINE:
			return shape3d_prepare_line(context, depth_sum);
		case RENDER_PRIMITIVE_WHEEL:
			return shape3d_prepare_wheel(context, any_vertex_behind, depth_sum);
		case RENDER_PRIMITIVE_SPHERE:
			return shape3d_prepare_sphere(context, depth_sum);
		case RENDER_PRIMITIVE_POINT:
			return shape3d_prepare_point(context, depth_sum);
		default:
			return 0;
	}
}

static legacy_u16 shape3d_insert_primitive(legacy_u8 primitive_type, legacy_u16 primitive_flags,
										   legacy_s32 depth_sum)
{
	transshapepolyinfo[POLYINFO_VERTEX_COUNT_OFFSET] = transshapenumvertscopy;
	transshapepolyinfo[POLYINFO_TYPE_OFFSET] = primitive_type;
	if ((transshapeflags & SHAPE3D_GHOST_FLAG) != 0U) {
		transshapepolyinfo[POLYINFO_TYPE_OFFSET] |= RENDER_PRIMITIVE_GHOST_FLAG;
		queued_ghost_primitives++;
	}
	if (transprimitivepaintjob == BACKLIGHT_PAINT_DEFAULT) {
		transshapepolyinfo[POLYINFO_MATERIAL_OFFSET] = backlights_paint_override;
	} else {
		transshapepolyinfo[POLYINFO_MATERIAL_OFFSET] = transprimitivepaintjob;
	}
	legacy_u16 depth = shape3d_average_depth(depth_sum, transshapenumvertscopy);
	polyinfo_write_word(transshapepolyinfo, 0U, depth);
	legacy_u16 sort_by_depth;
	if ((transshapeflags & SHAPE3D_NO_DEPTH_SORT_FLAG) != 0 ||
		(primitive_flags & SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) != 0) {
		sort_by_depth = 0;
	} else {
		sort_by_depth = 1;
	}
	polygon_buffer_full = polygon_insert_newest(depth, sort_by_depth);
	return polygon_buffer_full;
}

#if defined(RESTUNTS_SDL3)
static void hypervision_scene_reserve(void)
{
	if (hypervision_scene_count < hypervision_scene_capacity) {
		return;
	}
	if (hypervision_scene_capacity > LEGACY_U32_MAX / 2U) {
		fatal_error("HyperVision scene has too many primitives");
		return;
	}
	size_t capacity = hypervision_scene_capacity != 0 ? (size_t)hypervision_scene_capacity * 2U
													  : POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY;
	if (capacity > LEGACY_U32_MAX || capacity > (size_t)-1 / sizeof(*hypervision_scene)) {
		fatal_error("HyperVision scene exceeds addressable memory");
		return;
	}
	struct HYPERVISION_SCENE_PRIMITIVE *scene =
		realloc(hypervision_scene, capacity * sizeof(*hypervision_scene));
	if (scene == NULL) {
		fatal_error("Cannot allocate HyperVision scene metadata");
		return;
	}
	hypervision_scene = scene;
	hypervision_scene_capacity = (legacy_u32)capacity;
}

static legacy_s32 hypervision_bounds_are_clipped(const struct SHAPE3D *shape,
												 struct SHAPE3D_TRANSFORM_CONTEXT *context)
{
	/* Resource models begin with eight enclosing box corners. Tiny shapes can
	 * consist only of a sphere center/radius or wheel axes, which do not bound
	 * their coverage; let their projected primitive extents handle rejection. */
	if (shape->shape3d_numverts <= SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT) {
		return 0;
	}
	legacy_u8 common_clip_flags = SHAPE3D_ALL_RECT_CLIP_FLAGS;
	legacy_s32 any_front = 0;
	legacy_s32 any_behind = 0;
	for (legacy_u16 index = 0; index < SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT; index++) {
		shape3d_cache_vertex(shape, context, index);
		if (context->vertex_clip_flags[index] != 0) {
			any_behind = 1;
		} else {
			any_front = 1;
			common_clip_flags &= shape3d_hires_clip_flags(&context->hires_vertices[index]);
			if (common_clip_flags == 0) {
				return 0;
			}
		}
	}
	/* A box crossing the near plane may expand into the view after clipping,
	 * even when every already-projected corner lies on one side of it. */
	return !any_front || (!any_behind && common_clip_flags != 0);
}

static legacy_s32 hypervision_primitive_vertices(const struct SHAPE3D *shape,
												 struct SHAPE3D_TRANSFORM_CONTEXT *context,
												 const legacy_u8 *indices, legacy_u16 count,
												 legacy_u8 type)
{
	legacy_u8 common_clip_flags = SHAPE3D_ALL_RECT_CLIP_FLAGS;
	legacy_s32 any_front = 0;
	legacy_s32 any_behind = 0;
	for (legacy_u16 vertex = 0; vertex < count; vertex++) {
		legacy_u16 index = indices[vertex];
		if (index >= shape->shape3d_numverts) {
			return 0;
		}
		if (context->vertex_clip_flags[index] == SHAPE3D_VERTEX_UNTRANSFORMED) {
			shape3d_cache_vertex(shape, context, index);
		}
		if (context->vertex_clip_flags[index] != 0) {
			any_behind = 1;
		} else {
			any_front = 1;
			if (common_clip_flags != 0) {
				common_clip_flags &= shape3d_hires_clip_flags(&context->hires_vertices[index]);
			}
		}
	}
	/* Polygon and line adapters clip intersections; round primitives need
	 * their complete defining axes in front of the near plane. */
	if (any_behind != 0 && type != RENDER_PRIMITIVE_POLYGON && type != RENDER_PRIMITIVE_LINE) {
		return 0;
	}
	if (type == RENDER_PRIMITIVE_SPHERE || type == RENDER_PRIMITIVE_WHEEL) {
		return any_front;
	}
	return any_front != 0 && (any_behind != 0 || common_clip_flags == 0);
}

static legacy_u16 hypervision_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance)
{
	const struct SHAPE3D *shape = instance->shapeptr;
	transshapenumverts = shape->shape3d_numverts;
	if (transshapenumverts > SHAPE3D_VERTEX_CAPACITY) {
		return 1;
	}
	struct SHAPE3D_TRANSFORM_CONTEXT context;
	shape3d_prepare_instance(instance, &context);
	legacy_s32 bounds_clipped = hypervision_bounds_are_clipped(shape, &context);
	shape3d_collect_shadow_polygons(instance, &context);
	if (bounds_clipped != 0) {
		return (legacy_u16)-1;
	}
	shape3d_prepare_visibility(instance, &context);
	legacy_s32 depth_mode = (transshapeflags & SHAPE3D_BACKGROUND_FLAG) != 0
								? SHAPE3D_HIRES_DEPTH_BACKGROUND
								: SHAPE3D_HIRES_DEPTH_SORTED;
	shape3d_hires_begin_shape(hypervision_scene_count, depth_mode);
	shape3d_hires_set_shadow_receiver(
		(transshapeflags & (SHAPE3D_NO_SHADOW_RECEIVE_FLAG | SHAPE3D_GHOST_FLAG)) == 0);
	const legacy_u8 *primitive = shape->shape3d_primitives;
	legacy_u32 initial_count = hypervision_scene_count;
	legacy_s32 parent_visible = 1;
	for (legacy_u16 primitive_index = 0; primitive_index < shape->shape3d_numprimitives;
		 primitive_index++) {
		legacy_u8 resource_type = primitive[0];
		if (resource_type == SHAPE3D_PRIMITIVE_EMPTY ||
			resource_type >= SHAPE3D_PRIMITIVE_TYPE_COUNT) {
			break;
		}
		legacy_u16 count = primidxcounttab[resource_type];
		legacy_u8 type = primtypetab[resource_type];
		legacy_u16 flags = primitive[1];
		const legacy_u8 *indices = primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + transshapenumpaints;
		legacy_s32 attached = (flags & SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) != 0;
		legacy_s32 visible =
			type != RENDER_PRIMITIVE_UNSUPPORTED && count != 0 && (!attached || parent_visible) &&
			(LEGACY_READ_U32_LE(shape->shape3d_visibility_masks +
								(size_t)primitive_index * SHAPE3D_VISIBILITY_MASK_SIZE) &
			 (legacy_u32)context.visibility_mask) != 0 &&
			hypervision_primitive_vertices(shape, &context, indices, count, type);
		if (visible != 0) {
			shape3d_hires_queue(hypervision_scene_count, type, count, indices,
								context.hires_vertices, flags);
			if (type == RENDER_PRIMITIVE_POLYGON) {
				legacy_s32 cull_backface =
					(flags & SHAPE3D_PRIMITIVE_ALWAYS_VISIBLE_FLAG) == 0 &&
					((legacy_u32)context.front_facing_mask &
					 LEGACY_READ_U32_LE(shape->shape3d_front_facing_masks +
										(size_t)primitive_index * SHAPE3D_VISIBILITY_MASK_SIZE)) ==
						0;
				visible = shape3d_hires_polygon_visible(hypervision_scene_count, cull_backface);
			} else if (type == RENDER_PRIMITIVE_SPHERE || type == RENDER_PRIMITIVE_WHEEL) {
				struct RECTANGLE bounds = {HIRES_WIDTH / HIRES_SCALE, 0, HIRES_HEIGHT / HIRES_SCALE,
										   0};
				shape3d_hires_update_bounds(hypervision_scene_count, type, &bounds);
				visible = bounds.left < bounds.right && bounds.top < bounds.bottom;
			}
		}
		if (visible != 0) {
			if (primitive_index < context.shadow_surface_count) {
				shape3d_hires_set_shadow_surface(hypervision_scene_count,
												 context.shadow_surfaces[primitive_index]);
			}
			if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
				shape3d_hires_update_bounds(hypervision_scene_count, type, transshaperectptr);
			}
			legacy_u16 material = primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE + transshapematerial];
			if (material == BACKLIGHT_PAINT_DEFAULT) {
				material = backlights_paint_override;
			}
			if ((transshapeflags & SHAPE3D_GHOST_FLAG) != 0) {
				type |= RENDER_PRIMITIVE_GHOST_FLAG;
			}
			hypervision_scene_reserve();
			hypervision_scene[hypervision_scene_count++] =
				(struct HYPERVISION_SCENE_PRIMITIVE){material, type};
		}
		if (!attached) {
			parent_visible = visible;
		}
		primitive = indices + count;
	}
	return hypervision_scene_count != initial_count ? 0 : (legacy_u16)-1;
}
#endif

legacy_u16 shape3d_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance)
{
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		return hypervision_transform_and_queue(instance);
	}
#endif
	if (polygon_buffer_full != 0) {
		return 1;
	}
#if defined(RESTUNTS_SDL3)
	polyinfo_reserve();
#endif
	transshapenumverts = instance->shapeptr->shape3d_numverts;
	/* Shape files store this count in one byte. Reject a damaged descriptor
	 * before it can overrun the fixed-size transformation work arrays. */
	if (transshapenumverts > SHAPE3D_VERTEX_CAPACITY) {
		return 1;
	}
	legacy_u8 far *visibility_masks = instance->shapeptr->shape3d_visibility_masks;
	legacy_u8 far *front_facing_masks = instance->shapeptr->shape3d_front_facing_masks;
	struct SHAPE3D_TRANSFORM_CONTEXT context;
	shape3d_prepare_instance(instance, &context);
	shape_polygon_predecessor = polygon_list_tail;
	polygon_insertion_cursor = polygon_list_tail;
	shape_polygon_count = 0;
	legacy_u16 bounds_clipped = shape3d_bounds_are_clipped(instance, &context);
#if defined(RESTUNTS_SDL3)
	shape3d_collect_shadow_polygons(instance, &context);
#endif
	if (bounds_clipped != 0) {
		return (legacy_u16)-1;
	}
	shape3d_prepare_visibility(instance, &context);
	transshapeprimitives = instance->shapeptr->shape3d_primitives;
#if defined(RESTUNTS_SDL3)
	legacy_u32 shadow_primitive = 0;
	legacy_s32 depth_mode = SHAPE3D_HIRES_DEPTH_SORTED;
	if ((transshapeflags & SHAPE3D_BACKGROUND_FLAG) != 0) {
		depth_mode = SHAPE3D_HIRES_DEPTH_BACKGROUND;
	} else if ((transshapeflags & SHAPE3D_NO_DEPTH_SORT_FLAG) != 0) {
		depth_mode = SHAPE3D_HIRES_DEPTH_ORDERED;
	}
	shape3d_hires_begin_shape(polyinfonumpolys, depth_mode);
	shape3d_hires_set_shadow_receiver(
		(transshapeflags & (SHAPE3D_NO_SHADOW_RECEIVE_FLAG | SHAPE3D_GHOST_FLAG)) == 0);
#endif

	legacy_s32 depth_sum;
	legacy_u16 queued_primitive_count = 0;
	legacy_u8 primitive_type;
	legacy_u16 any_vertex_behind;
	for (;;) {
		transshapeprimptr = transshapeprimitives + primidxcounttab[transshapeprimitives[0]] +
							transshapenumpaints + 2;
		legacy_u16 primitive_flags = transshapeprimitives[1];
		legacy_u16 primitive_visible = 0;
		if ((LEGACY_READ_U32_LE(visibility_masks) & (legacy_u32)context.visibility_mask) != 0UL) {
			transshapenumvertscopy = primidxcounttab[transshapeprimitives[0]];
			primitive_type = primtypetab[transshapeprimitives[0]];
#if defined(RESTUNTS_SDL3)
			polyinfo_reserve();
#endif
			transshapepolyinfo = polyinfoptr + polyinfoptrnext;
			polygon_record_offsets[polyinfonumpolys] = polyinfoptrnext;
			transprimitivepaintjob = transshapeprimitives[2 + transshapematerial];
			transshapeprimitives += 2 + transshapenumpaints;
			if (shape3d_prepare_primitive_vertices(instance->shapeptr, &context,
												   &any_vertex_behind) != 0) {
#if defined(RESTUNTS_SDL3)
				/* Visibility must use the same fractional projection as rasterization. */
				shape3d_hires_queue(polyinfonumpolys, primitive_type, transshapenumvertscopy,
									transshapeprimitives, context.hires_vertices, primitive_flags);
#endif
				primitive_visible =
					shape3d_prepare_primitive(&context, primitive_type, any_vertex_behind,
											  primitive_flags, front_facing_masks, &depth_sum);
#if defined(RESTUNTS_SDL3)
				if (primitive_visible != 0) {
					if (shadow_primitive < context.shadow_surface_count) {
						shape3d_hires_set_shadow_surface(polyinfonumpolys,
														 context.shadow_surfaces[shadow_primitive]);
					}
					if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
						shape3d_hires_update_bounds(polyinfonumpolys, primitive_type,
													transshaperectptr);
					}
				}
#endif
			}
		}

		transshapeprimitives = transshapeprimptr;
#if defined(RESTUNTS_SDL3)
		shadow_primitive++;
#endif
		visibility_masks += 4U;
		front_facing_masks += 4U;
		if (primitive_visible != 0) {
			queued_primitive_count = LEGACY_U16_WRAP_ADD(queued_primitive_count, 1U);
			if (shape3d_insert_primitive(primitive_type, primitive_flags, depth_sum) != 0) {
				return 1;
			}
		} else if ((primitive_flags & SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) == 0) {
			while (transshapeprimitives[0] != 0 &&
				   (transshapeprimitives[1] & SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) != 0) {
				transshapeprimitives +=
					primidxcounttab[transshapeprimitives[0]] + transshapenumpaints + 2;
#if defined(RESTUNTS_SDL3)
				shadow_primitive++;
#endif
				visibility_masks += 4U;
				front_facing_masks += 4U;
			}
		}
		if (transshapeprimitives[0] == 0) {
			return queued_primitive_count != 0 ? 0 : (legacy_u16)-1;
		}
	}
}

// parameter points to a far array of 2d points
legacy_s8 is_facing_camera(struct POINT2D far *pts)
{
	legacy_s32 dx0 = (legacy_s32)pts[0].px - pts[1].px;
	legacy_s32 dx1 = (legacy_s32)pts[2].px - pts[1].px;

	if (dx0 == 0 && dx1 == 0) {
		return 0;
	}

	legacy_s32 dy0 = (legacy_s32)pts[0].py - pts[1].py;
	legacy_s32 dy1 = (legacy_s32)pts[2].py - pts[1].py;

	if (dy0 == 0 && dy1 == 0) {
		return 0;
	}
	legacy_s32 signed_area = (dx1 * dy0) - (dx0 * dy1);
	return signed_area <= 0 ? 0 : 1;
}

extern legacy_u16 projection_horizontal_half_fov;
extern legacy_u16 projection_vertical_half_fov;
extern legacy_u16 projection_half_width;
extern legacy_u16 projection_origin_x;
extern legacy_u16 projection_half_height;
extern legacy_u16 projection_origin_y;

legacy_u16 projection_scale_x_wrapped(legacy_u16 numerator, legacy_s16 divisor)
{
	return LEGACY_U16_DIV_OR_ZERO(LEGACY_U16_WRAP_MUL(projection_focal_length_x, numerator),
								  (legacy_u16)divisor);
}

legacy_u16 divide_u32_to_u16(legacy_u32 dividend, legacy_u16 divisor)
{
	return (legacy_u16)LEGACY_U32_DIV_OR_ZERO(dividend, divisor);
}

legacy_u32 projection_multiply_x(legacy_u16 value)
{
	return (legacy_u32)projection_focal_length_x * value;
}

legacy_u32 projection_multiply_y(legacy_u16 value)
{
	return (legacy_u32)projection_focal_length_y * value;
}

legacy_u16 projection_scale_y(legacy_u16 value, legacy_u16 divisor)
{
	return (legacy_u16)LEGACY_U32_DIV_OR_ZERO(LEGACY_U32_WRAP_MUL(projection_focal_length_y, value),
											  divisor);
}

extern legacy_u16 polygon_insert_newest(legacy_u16 depth, legacy_u16 sort_by_depth)
{
	//return ported_insert_newest_poly_in_poly_linked_list_40ED6_(depth, sort_by_depth);

	polyinfo_link next_polygon;
#if defined(RESTUNTS_SDL3)
	/* HyperVision resolves visibility in its depth buffer. Retain resource
	 * submission order without searching the classic painter queue. */
	if (hires_enabled()) {
		sort_by_depth = 0;
	}
#endif
	if (sort_by_depth == 0) {
		next_polygon = polygon_next_index[polygon_insertion_cursor];
	} else {
		polygon_insertion_cursor = shape_polygon_predecessor;
		next_polygon = polygon_next_index[shape_polygon_predecessor];
		polyinfo_index remaining_polygons = shape_polygon_count;

		while (next_polygon >= 0) {
			polyinfo_index previous_remaining_count = remaining_polygons;
			remaining_polygons--;
			if (previous_remaining_count == 0) {
				break;
			}
			if (LEGACY_READ_S16_LE(polyinfoptr + polygon_record_offsets[next_polygon]) <
				(legacy_s16)depth) {
				break;
			}
			polygon_insertion_cursor = next_polygon;
			next_polygon = polygon_next_index[next_polygon];
		}
	}

	polygon_next_index[polyinfonumpolys] = next_polygon;
	polygon_next_index[polygon_insertion_cursor] = polyinfonumpolys;
	shape_polygon_count++;
	if (next_polygon < 0) {
		polygon_list_tail = polyinfonumpolys;
	}
	polygon_insertion_cursor = polygon_next_index[polygon_insertion_cursor];
	polyinfonumpolys++;
	polyinfoptrnext += transshapenumvertscopy * sizeof(struct POINT2D) + 6U;
#if defined(RESTUNTS_SDL3)
	if (polyinfo_supersight_enabled) {
		return 0;
	}
#endif
	if (polyinfonumpolys == polyinfo_primitive_capacity) {
		return 1;
	}
	if (polyinfoptrnext <= polyinfo_last_valid_offset) {
		return 0;
	}
	return 1;
}

static legacy_u16 projection_angle_from_extent(legacy_s16 extent)
{
	legacy_s32 scaled = LEGACY_S32_WRAP_MUL((legacy_s32)extent, PROJECTION_EXTENT_SCALE);
	legacy_s32 quotient = LEGACY_S32_DIV_OR_ZERO(scaled, PROJECTION_EXTENT_DIVISOR);
	return (legacy_u16)LEGACY_S32_SAR(quotient, 1U);
}

static legacy_u16 projection_scale_for_angle(legacy_u16 angle, legacy_u16 extent)
{
	legacy_s32 product = LEGACY_S32_WRAP_MUL((legacy_s32)cos_fast(angle), (legacy_s32)extent);
	legacy_s32 quotient = LEGACY_S32_DIV_OR_ZERO(product, (legacy_s32)sin_fast(angle));
	return (legacy_u16)quotient;
}

static void projection_update_derived(void)
{
	projection_center_x = LEGACY_U16_WRAP_ADD(projection_half_width, projection_origin_x);
	projection_center_y = LEGACY_U16_WRAP_ADD(projection_half_height, projection_origin_y);
	projection_focal_length_x =
		projection_scale_for_angle(projection_horizontal_half_fov, projection_half_width);
	if (projection_vertical_half_fov != 0) {
		projection_focal_length_y =
			projection_scale_for_angle(projection_vertical_half_fov, projection_half_height);
	} else {
		projection_focal_length_y = LEGACY_U16_WRAP_SUB(
			LEGACY_U16_WRAP_SUB(projection_focal_length_x, projection_focal_length_x >> 3),
			projection_focal_length_x >> 4);
		projection_vertical_half_fov =
			polarAngle(projection_focal_length_y, projection_half_height);
	}
}

void set_projection(legacy_s16 horizontal_fov_degrees, legacy_s16 vertical_fov_degrees,
					legacy_s16 width, legacy_s16 height)
{
	projection_horizontal_half_fov = projection_angle_from_extent(horizontal_fov_degrees);
	projection_vertical_half_fov = projection_angle_from_extent(vertical_fov_degrees);
	projection_half_width = (legacy_u16)LEGACY_S16_SAR(width, 1U);
	projection_half_height = (legacy_u16)LEGACY_S16_SAR(height, 1U);
	projection_update_derived();
}

void projection_set_origin(legacy_u16 x, legacy_u16 y)
{
	projection_origin_x = x;
	projection_center_x = LEGACY_U16_WRAP_ADD(projection_half_width, x);
	projection_origin_y = y;
	projection_center_y = LEGACY_U16_WRAP_ADD(projection_half_height, y);
}

void projection_set_half_fov(legacy_u16 horizontal_half_fov, legacy_u16 vertical_half_fov,
							 legacy_u16 width, legacy_u16 height)
{
	projection_horizontal_half_fov = horizontal_half_fov;
	projection_vertical_half_fov = vertical_half_fov;
	projection_half_width = width >> 1;
	projection_half_height = height >> 1;
	projection_update_derived();
}

//extern unsigned word_411F6;
extern struct MATRIX mat_y0, mat_y100, mat_y200, mat_y300;
extern legacy_s32 direction_sector_sine_copy;
extern legacy_s32 direction_sector_cosine_copy;

static void shape3d_retain_legacy_view_rotation(legacy_s16 angZ, legacy_s16 angX, legacy_s16 angY);

legacy_u16 select_cliprect_rotate(legacy_s16 angZ, legacy_s16 angX, legacy_s16 angY,
								  struct RECTANGLE *cliprect, legacy_s16 half_scale)
{
	//return ported_select_cliprect_rotate_(angX, angY, angZ, cliprect, half_scale);

	mat_temp = *mat_rot_zxy(angZ, angX, angY, MATRIX_ROTATION_ORDER_YXZ);
	polyinfo_reset();
	select_rect_rc = *cliprect;
	shape_half_scale = half_scale;
#if defined(RESTUNTS_SDL3)
	shape3d_hires_set_model_scale(half_scale != 0 ? 0.5 : 1);
#endif
	struct MATRIX *inverse_view_rotation =
		mat_rot_zxy(-angZ, -angX, -angY, MATRIX_ROTATION_ORDER_ZXY);
	shape3d_retain_legacy_view_rotation(angZ, angX, angY);
	struct VECTOR forward_axis;
	forward_axis.z = PROJECTION_VIEW_VECTOR_LENGTH;
	forward_axis.y = 0;
	forward_axis.x = 0;
	struct VECTOR view_direction;
	mat_mul_vector(&forward_axis, inverse_view_rotation, &view_direction);
	return polarAngle(view_direction.x, view_direction.z) & PROJECTION_YAW_MASK;
}

void polyinfo_reset(void)
{
#if defined(RESTUNTS_SDL3)
	hypervision_scene_count = 0;
	shape3d_hires_reset();
#endif
	queued_ghost_primitives = 0;
	polyinfonumpolys = 0;
	polyinfoptrnext = 0;
	polygon_buffer_full = 0;
	polygon_next_index[polyinfo_primitive_capacity] = POLYINFO_LIST_SENTINEL;
	polygon_list_tail = polyinfo_primitive_capacity;
}

void polyinfo_set_supersight(legacy_u8 enabled)
{
	polyinfo_index capacity =
		enabled != 0U ? POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY : POLYINFO_LEGACY_PRIMITIVE_CAPACITY;
#if defined(RESTUNTS_SDL3)
	polyinfo_supersight_enabled = enabled != 0U;
	if (polyinfo_supersight_enabled) {
		capacity = native_primitive_capacity;
	}
#endif
	if (polyinfo_primitive_capacity != capacity) {
		polyinfo_primitive_capacity = capacity;
		polyinfo_last_valid_offset = enabled != 0U ? POLYINFO_SUPERSIGHT_LAST_VALID_OFFSET
												   : POLYINFO_LEGACY_LAST_VALID_OFFSET;
		polyinfo_reset();
	}
}

void init_direction_sector_thresholds(void)
{
	direction_sector_sine = sin_fast(PROJECTION_SINCOS_ANGLE);
	direction_sector_cosine = cos_fast(PROJECTION_SINCOS_ANGLE);
	direction_sector_sine_copy = sin_fast(PROJECTION_SINCOS_ANGLE);
	direction_sector_cosine_copy = cos_fast(PROJECTION_SINCOS_ANGLE);
}

void init_polyinfo(void)
{
	polyinfoptr = mmgr_alloc_resbytes("polyinfo", POLYINFO_SUPERSIGHT_DATA_SIZE);

	mat_rot_y(&mat_y0, 0);
	mat_rot_y(&mat_y100, ANGLE_QUARTER_TURN);
	mat_rot_y(&mat_y200, ANGLE_HALF_TURN);
	mat_rot_y(&mat_y300, ANGLE_THREE_QUARTER_TURN);
	init_direction_sector_thresholds();
}

/* Original get_a_poly_info rasterizer call frames overlap stopped-wheel
 * headings. The caller opts into this virtual stack layout; ordinary game
 * rendering does not borrow the archived sampling caller's stack address.
 * SEG006 return sites: solid131F, patterned1348, two-color13E1, line13FF,
 * wheel145D, sphere1477 and point148D. Argument counts determine whether
 * the four retained words hold two locals, one local, or the first argument
 * alongside the saved BP and far return address. */
#define SHAPE3D_LEGACY_SOLID_POLYGON_RETURN_IP 4895U
#define SHAPE3D_LEGACY_PATTERNED_POLYGON_RETURN_IP 4936U
#define SHAPE3D_LEGACY_TWO_COLOR_POLYGON_RETURN_IP 5089U
#define SHAPE3D_LEGACY_LINE_RETURN_IP 5119U
#define SHAPE3D_LEGACY_WHEEL_RETURN_IP 5213U
#define SHAPE3D_LEGACY_SPHERE_RETURN_IP 5239U
#define SHAPE3D_LEGACY_POINT_RETURN_IP 5261U
#define SHAPE3D_LEGACY_POLYGON_POINTS_STACK_OFFSET 50U
#define SHAPE3D_LEGACY_POLYGON_HEADER_SIZE 6U
#define SHAPE3D_LEGACY_POLYGON_POINT_SIZE 4U

static legacy_s16 *legacy_render_wheel_headings;
static legacy_u16 legacy_render_polygon_frame_pointer;
static legacy_u16 legacy_render_polygon_code_segment;
static struct SHAPE3D_LEGACY_OPPONENT_RENDER_CONTEXT legacy_opponent_render_context;

void shape3d_set_legacy_render_stack(legacy_s16 *wheel_headings, legacy_u16 polygon_frame_pointer,
									 legacy_u16 polygon_code_segment,
									 const struct SHAPE3D_LEGACY_OPPONENT_RENDER_CONTEXT *opponent)
{
	legacy_render_wheel_headings = wheel_headings;
	legacy_render_player_headings_active =
		wheel_headings == legacy_execution_residue.wheel_plane_angles;
	legacy_render_polygon_frame_pointer = polygon_frame_pointer;
	legacy_render_polygon_code_segment = polygon_code_segment;
	if (opponent != 0) {
		legacy_opponent_render_context = *opponent;
	} else {
		legacy_opponent_render_context.wheel_headings = 0;
	}
}

/* select_cliprect_rotate's inverse mat_rot_zxy call reuses get_a_poly_info
 * BP-64..BP-58, later read as the opponent's stopped-wheel headings. With two
 * or more axes, mat_multiply leaves the axis flags, saved BP and far return
 * address. A single-axis constructor leaves sine, cosine, saved BP and return
 * IP. Incremental sky rendering can leave these words untouched, and a solid
 * polygon does not replace the first or fourth word.
 * The inverse yaw is computed after the forward yaw, so a non-cardinal single
 * yaw always misses the matrix cache; cardinal yaw and identity make no call. */
#define SHAPE3D_LEGACY_ROTATE_Z_RETURN_IP 5306U
#define SHAPE3D_LEGACY_ROTATE_X_RETURN_IP 5331U
#define SHAPE3D_LEGACY_ROTATE_Y_RETURN_IP 5395U
#define SHAPE3D_LEGACY_ROTATE_PAIR_RETURN_IP 5482U
#define SHAPE3D_LEGACY_ROTATE_TRIPLE_RETURN_IP 5626U
#define SHAPE3D_LEGACY_VIEW_ROTATION_STACK_OFFSET 42U
#define SHAPE3D_LEGACY_ROTATE_Y_FLAG 1U
#define SHAPE3D_LEGACY_ROTATE_X_FLAG 2U
#define SHAPE3D_LEGACY_ROTATE_Z_FLAG 4U
#define SHAPE3D_LEGACY_ROTATE_ALL_FLAGS                                                            \
	(SHAPE3D_LEGACY_ROTATE_Z_FLAG | SHAPE3D_LEGACY_ROTATE_X_FLAG | SHAPE3D_LEGACY_ROTATE_Y_FLAG)

static void shape3d_retain_legacy_view_rotation(legacy_s16 angZ, legacy_s16 angX, legacy_s16 angY)
{
	legacy_s16 rotate_z;
	legacy_s16 rotate_x;
	legacy_s16 rotate_y;
	legacy_s16 angle;
	legacy_s16 *headings;
	legacy_u16 frame_pointer;
	legacy_u16 return_ip;
	legacy_u16 axes;

	if (legacy_opponent_render_context.wheel_headings == 0) {
		return;
	}
	rotate_z = (angZ & ANGLE_MASK) != 0;
	rotate_x = (angX & ANGLE_MASK) != 0;
	rotate_y = (angY & ANGLE_MASK) != 0;
	headings = legacy_opponent_render_context.wheel_headings;
	frame_pointer = (legacy_u16)(legacy_render_polygon_frame_pointer -
								 SHAPE3D_LEGACY_VIEW_ROTATION_STACK_OFFSET);
	if ((rotate_z && rotate_x) || (rotate_z && rotate_y) || (rotate_x && rotate_y)) {
		axes = (legacy_u16)((rotate_z ? SHAPE3D_LEGACY_ROTATE_Z_FLAG : 0) |
							(rotate_x ? SHAPE3D_LEGACY_ROTATE_X_FLAG : 0) |
							(rotate_y ? SHAPE3D_LEGACY_ROTATE_Y_FLAG : 0));
		headings[0] = LEGACY_S16_FROM_BITS(axes);
		headings[1] = LEGACY_S16_FROM_BITS(frame_pointer);
		headings[2] = LEGACY_S16_FROM_BITS(axes == SHAPE3D_LEGACY_ROTATE_ALL_FLAGS
											   ? SHAPE3D_LEGACY_ROTATE_TRIPLE_RETURN_IP
											   : SHAPE3D_LEGACY_ROTATE_PAIR_RETURN_IP);
		headings[3] = LEGACY_S16_FROM_BITS(legacy_render_polygon_code_segment);
		return;
	} else if (rotate_z) {
		angle = LEGACY_S16_WRAP_NEGATE(angZ);
		return_ip = SHAPE3D_LEGACY_ROTATE_Z_RETURN_IP;
	} else if (rotate_x) {
		angle = LEGACY_S16_WRAP_NEGATE(angX);
		return_ip = SHAPE3D_LEGACY_ROTATE_X_RETURN_IP;
	} else if ((angY & ANGLE_QUARTER_MASK) != 0) {
		angle = LEGACY_S16_WRAP_NEGATE(angY);
		return_ip = SHAPE3D_LEGACY_ROTATE_Y_RETURN_IP;
	} else {
		return;
	}
	headings[0] = sin_fast(angle);
	headings[1] = cos_fast(angle);
	headings[2] = LEGACY_S16_FROM_BITS(frame_pointer);
	headings[3] = LEGACY_S16_FROM_BITS(return_ip);
}

/* update_frame calls skybox_op with fourteen argument bytes before calling
 * get_a_poly_info without arguments. Its BP is therefore fourteen bytes
 * below the polygon renderer's BP: skybox BP-70..BP-64 overlaps the player's
 * headings, while BP-50..BP-44 overlaps the opponent's headings. */
void shape3d_retain_legacy_skybox_horizon(legacy_s16 horizon)
{
	if (legacy_opponent_render_context.wheel_headings != 0) {
		legacy_opponent_render_context.wheel_headings[0] = horizon;
	}
}

void shape3d_retain_legacy_skybox_rect(const struct RECTANGLE *rect)
{
	if (legacy_opponent_render_context.wheel_headings != 0) {
		legacy_opponent_render_context.wheel_headings[1] = rect->left;
		legacy_opponent_render_context.wheel_headings[2] = rect->right;
		legacy_opponent_render_context.wheel_headings[3] = rect->top;
	}
}

void shape3d_retain_legacy_skybox_points(const struct POINT2D *points)
{
	if (legacy_render_wheel_headings != 0) {
		legacy_render_wheel_headings[0] = points[0].px;
		legacy_render_wheel_headings[1] = points[0].py;
		legacy_render_wheel_headings[2] = points[1].px;
		legacy_render_wheel_headings[3] = points[1].py;
	}
}

/* init_crak's line buffer starts at get_a_poly_info BP-80. The dirty-rectangle
 * path pushes two pending rect_union arguments, shifting the buffer to BP-84.
 * Its clipped coordinates and clip counts replace the stopped-wheel headings
 * after scene rendering, even when the crack line is entirely outside the view. */
#define SHAPE3D_LEGACY_CRACK_LINE_STACK_OFFSET 80U
#define SHAPE3D_LEGACY_LINE_CODE_PARAGRAPH_DELTA 2508U

void shape3d_retain_legacy_crack_line(const legacy_u16 *line, legacy_u16 end_y,
									  legacy_s16 dirty_rects, legacy_s16 drawn)
{
	legacy_u16 i;
	legacy_u16 opponent_index;

	if (legacy_render_wheel_headings != 0) {
		if (dirty_rects != 0) {
			for (i = 0; i < LEGACY_RESIDUE_WORD_COUNT; i++) {
				legacy_render_wheel_headings[i] = LEGACY_S16_FROM_BITS(line[i]);
			}
		} else {
			/* Drawing replaces the setup call's end-Y argument with its return CS. */
			legacy_render_wheel_headings[0] = LEGACY_S16_FROM_BITS(
				drawn != 0 ? LEGACY_U16_WRAP_ADD(legacy_render_polygon_code_segment,
												 SHAPE3D_LEGACY_LINE_CODE_PARAGRAPH_DELTA)
						   : end_y);
			legacy_render_wheel_headings[1] = LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_SUB(
				legacy_render_polygon_frame_pointer, SHAPE3D_LEGACY_CRACK_LINE_STACK_OFFSET));
			legacy_render_wheel_headings[2] =
				LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_FRACTION_INDEX]);
			legacy_render_wheel_headings[3] = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]);
		}
	}
	if (legacy_opponent_render_context.wheel_headings != 0) {
		opponent_index =
			dirty_rects != 0 ? DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX : DRAW_LINE_COLOR_INDEX;
		for (i = 0; i < LEGACY_RESIDUE_WORD_COUNT; i++) {
			legacy_opponent_render_context.wheel_headings[i] =
				LEGACY_S16_FROM_BITS(line[opponent_index + i]);
		}
	}
}

/* rect_adjust_from_point saves init_crak's SI (line index) and DI (normalized
 * crack resource offset) over the last two opponent words after each line. */
void shape3d_retain_legacy_crack_bounds(legacy_s16 line_index, legacy_u16 crack_offset)
{
	if (legacy_opponent_render_context.wheel_headings != 0) {
		legacy_opponent_render_context.wheel_headings[2] = line_index;
		legacy_opponent_render_context.wheel_headings[3] = LEGACY_S16_FROM_BITS(crack_offset);
	}
}

/* Opponent headings overlap get_a_poly_info's BP-64 pattern temporary,
 * BP-62/BP-60 polygon-data far pointer, and the word at BP-58 left by the preceding skybox call.
 * Rasterizer calls are below this window and leave these locals intact. */
static void shape3d_retain_opponent_pattern(legacy_u16 value)
{
	if (legacy_opponent_render_context.wheel_headings != 0) {
		legacy_opponent_render_context.wheel_headings[0] = LEGACY_S16_FROM_BITS(value);
	}
}

static void shape3d_retain_opponent_polygon_pointer(const legacy_u8 far *record,
													legacy_u16 vertex_count)
{
	if (legacy_opponent_render_context.wheel_headings == 0) {
		return;
	}
	legacy_u16 offset = LEGACY_U16_WRAP_ADD(legacy_opponent_render_context.polyinfo_offset,
											(legacy_u16)(record - polyinfoptr));
	offset = LEGACY_U16_WRAP_ADD(offset, SHAPE3D_LEGACY_POLYGON_HEADER_SIZE);
	offset = LEGACY_U16_WRAP_ADD(
		offset, LEGACY_U16_WRAP_MUL(vertex_count, SHAPE3D_LEGACY_POLYGON_POINT_SIZE));
	legacy_opponent_render_context.wheel_headings[1] = LEGACY_S16_FROM_BITS(offset);
	/* The original increments only the pointer's offset, without normalizing it. */
	legacy_opponent_render_context.wheel_headings[2] =
		LEGACY_S16_FROM_BITS(legacy_opponent_render_context.polyinfo_segment);
}

static void shape3d_retain_render_local_pair(legacy_u16 first, legacy_u16 second,
											 legacy_u16 return_ip)
{
	if (legacy_render_wheel_headings == 0) {
		return;
	}
	legacy_render_wheel_headings[0] = LEGACY_S16_FROM_BITS(first);
	legacy_render_wheel_headings[1] = LEGACY_S16_FROM_BITS(second);
	legacy_render_wheel_headings[2] = LEGACY_S16_FROM_BITS(legacy_render_polygon_frame_pointer);
	legacy_render_wheel_headings[3] = LEGACY_S16_FROM_BITS(return_ip);
}

static void shape3d_retain_render_local(legacy_u16 first, legacy_u16 return_ip)
{
	if (legacy_render_wheel_headings == 0) {
		return;
	}
	legacy_render_wheel_headings[0] = LEGACY_S16_FROM_BITS(first);
	legacy_render_wheel_headings[1] = LEGACY_S16_FROM_BITS(legacy_render_polygon_frame_pointer);
	legacy_render_wheel_headings[2] = LEGACY_S16_FROM_BITS(return_ip);
	legacy_render_wheel_headings[3] = LEGACY_S16_FROM_BITS(legacy_render_polygon_code_segment);
}

static void shape3d_retain_render_argument(legacy_u16 return_ip, legacy_u16 argument)
{
	if (legacy_render_wheel_headings == 0) {
		return;
	}
	legacy_render_wheel_headings[0] = LEGACY_S16_FROM_BITS(legacy_render_polygon_frame_pointer);
	legacy_render_wheel_headings[1] = LEGACY_S16_FROM_BITS(return_ip);
	legacy_render_wheel_headings[2] = LEGACY_S16_FROM_BITS(legacy_render_polygon_code_segment);
	legacy_render_wheel_headings[3] = LEGACY_S16_FROM_BITS(argument);
}

static void shape3d_retain_sphere_stack(legacy_u16 size)
{
	if (legacy_render_wheel_headings == 0) {
		return;
	}
	legacy_u16 effective_height =
		LEGACY_U16_WRAP_ADD(LEGACY_U16_WRAP_SUB(size, size >> 2), size >> 4);
	/* preRender_sphere writes BP-2 only after rejecting nonpositive heights
	 * and the one-pixel shortcut; its saved BP/return words always change. */
	legacy_u16 first = LEGACY_S16_FROM_BITS(effective_height) >= 2
						   ? LEGACY_U16_WRAP_SUB(drawing_sprite.sprite_raster_right, 1U)
						   : (legacy_u16)legacy_render_wheel_headings[0];
	shape3d_retain_render_local(first, SHAPE3D_LEGACY_SPHERE_RETURN_IP);
}

static legacy_u16 shape3d_material_value(const legacy_s16 *table, legacy_u16 index)
{
	/* Resource materials use a byte, and wheel colors read two entries further.
	 * The original maps are adjacent: color, pattern type, then pattern bits.
	 * Model those reads without relying on the host's global-variable layout.
	 * Extended pattern types never select a secondary-pattern lookup. */
	if (index >= SHAPE3D_MATERIAL_COUNT) {
		if (table == material_color_list) {
			table = material_pattern_list;
			index -= SHAPE3D_MATERIAL_COUNT;
		} else if (table == material_pattern_list) {
			table = material_pattern2_list;
			index -= SHAPE3D_MATERIAL_COUNT;
		}
	}
	return (legacy_u16)table[index];
}

/* Keep ghost drawing out of the legacy renderer stack handoff: its appearance
 * must not become an input to either car's physics. */
static void shape3d_render_ghost(const legacy_u8 far *record, struct POINT2D *points)
{
	legacy_u16 primitive_type = record[POLYINFO_TYPE_OFFSET] & ~RENDER_PRIMITIVE_GHOST_FLAG;
	if (primitive_type == RENDER_PRIMITIVE_POLYGON) {
		legacy_u16 material = record[POLYINFO_MATERIAL_OFFSET];
		legacy_u16 pattern_type = shape3d_material_value(material_patlist_ptr_cpy, material);
		if (pattern_type > SHAPE3D_PATTERN_TWO_COLOR ||
			(pattern_type == SHAPE3D_PATTERN_MASKED &&
			 shape3d_material_value(material_patlist2_ptr_cpy, material) == 0U)) {
			return;
		}
		polyinfo_read_points(record, points, record[POLYINFO_VERTEX_COUNT_OFFSET]);
		preRender_default(PRERENDER_GHOST_COLOR, record[POLYINFO_VERTEX_COUNT_OFFSET], points);
	} else if (primitive_type == RENDER_PRIMITIVE_WHEEL) {
		polyinfo_read_points(record, points, WHEEL_SOURCE_POINT_COUNT);
		preRender_wheel(points, WHEEL_INNER_RADIUS_SCALE, PRERENDER_GHOST_COLOR,
						PRERENDER_GHOST_COLOR, PRERENDER_GHOST_COLOR);
	} else if (primitive_type == RENDER_PRIMITIVE_SPHERE) {
		preRender_sphere(LEGACY_S16_FROM_BITS(polyinfo_read_word(record, POLYINFO_START_X_WORD)),
						 LEGACY_S16_FROM_BITS(polyinfo_read_word(record, POLYINFO_START_Y_WORD)),
						 polyinfo_read_word(record, POLYINFO_SPHERE_SIZE_WORD),
						 PRERENDER_GHOST_COLOR);
	} else if (primitive_type == RENDER_PRIMITIVE_LINE ||
			   primitive_type == RENDER_PRIMITIVE_POINT) {
		legacy_u16 x = polyinfo_read_word(record, POLYINFO_START_X_WORD);
		legacy_u16 y = polyinfo_read_word(record, POLYINFO_START_Y_WORD);
		preRender_line(x, y,
					   primitive_type == RENDER_PRIMITIVE_POINT
						   ? x
						   : polyinfo_read_word(record, POLYINFO_END_X_WORD),
					   primitive_type == RENDER_PRIMITIVE_POINT
						   ? y
						   : polyinfo_read_word(record, POLYINFO_END_Y_WORD),
					   PRERENDER_GHOST_COLOR);
	}
}

static legacy_u16 shape3d_legacy_record_index(polyinfo_index record_index)
{
	polyinfo_index result = record_index;
	if (queued_ghost_primitives != 0U) {
		for (polyinfo_index index = 0; index < record_index; index++) {
			if ((polyinfoptr[polygon_record_offsets[index] + POLYINFO_TYPE_OFFSET] &
				 RENDER_PRIMITIVE_GHOST_FLAG) != 0U) {
				result--;
			}
		}
	}
	return result;
}

#if defined(RESTUNTS_SDL3)
legacy_u32 shape3d_queued_primitive_count(void)
{
	return hires_enabled() ? hypervision_scene_count : polyinfonumpolys;
}

static void shape3d_render_hires_primitive(legacy_u32 index,
										   const struct HYPERVISION_SCENE_PRIMITIVE *primitive)
{
	legacy_u16 material = primitive->material;
	legacy_u16 type = primitive->type & ~RENDER_PRIMITIVE_GHOST_FLAG;
	legacy_u16 pattern_type = SHAPE3D_PATTERN_SOLID;
	legacy_u16 pattern = 0;
	legacy_u16 color = shape3d_material_value(material_clrlist_ptr_cpy, material);
	legacy_u16 second_color = 0;
	legacy_u16 third_color = 0;
	if (type == RENDER_PRIMITIVE_POLYGON) {
		pattern_type = shape3d_material_value(material_patlist_ptr_cpy, material);
		pattern = shape3d_material_value(material_patlist2_ptr_cpy, material);
		if (pattern_type > SHAPE3D_PATTERN_TWO_COLOR ||
			(pattern_type == SHAPE3D_PATTERN_MASKED && pattern == 0U)) {
			return;
		}
		second_color = shape3d_material_value(material_clrlist2_ptr_cpy, material);
	} else if (type == RENDER_PRIMITIVE_WHEEL) {
		second_color = shape3d_material_value(material_clrlist_ptr_cpy, material + 1U);
		third_color = shape3d_material_value(material_clrlist_ptr_cpy, material + 2U);
	}
	shape3d_hires_render(index, primitive->type, color, second_color, third_color, pattern_type,
						 pattern);
}
#endif

void shape3d_render_queued_primitives(void)
{
#if defined(RESTUNTS_TEMPLEOS)
	TempleRasterNative(1);
#endif
#if defined(RESTUNTS_SDL3)
	if (hires_begin(&drawing_sprite) != 0) {
		/* Enhanced presentation has its own depth rasterizer. Drawing the
		 * original 320x200 polygons as well only repeats the complete scene. */
		shape3d_hires_batch_begin();
		for (legacy_u32 index = 0; index < hypervision_scene_count; index++) {
			shape3d_render_hires_primitive(index, &hypervision_scene[index]);
		}
		shape3d_hires_batch_end();
		shape3d_hires_draw_shadows();
		hires_end();
		polyinfo_reset();
		return;
	}
#endif
	polyinfo_index record_index = polyinfo_primitive_capacity;
	polyinfo_index rendered_ghost_primitives = 0;
	struct POINT2D points[POLYINFO_MAX_RENDER_POINTS];
	for (polyinfo_index primitive_index = 0; primitive_index < polyinfonumpolys;
		 primitive_index++) {
		record_index = (polyinfo_index)polygon_next_index[record_index];
		legacy_u8 far *record = polyinfoptr + polygon_record_offsets[record_index];
		if ((record[POLYINFO_TYPE_OFFSET] & RENDER_PRIMITIVE_GHOST_FLAG) != 0U) {
			shape3d_render_ghost(record, points);
			rendered_ghost_primitives++;
			continue;
		}
		legacy_u16 material_type = record[POLYINFO_MATERIAL_OFFSET];
		legacy_u16 material_color = shape3d_material_value(material_clrlist_ptr_cpy, material_type);
		legacy_u16 primitive_type = record[POLYINFO_TYPE_OFFSET];

		if (primitive_type == RENDER_PRIMITIVE_POLYGON) {
			legacy_u16 vertex_count = record[POLYINFO_VERTEX_COUNT_OFFSET];
			polyinfo_read_points(record, points, vertex_count);
			shape3d_retain_opponent_polygon_pointer(record, vertex_count);
			legacy_u16 pattern_type =
				shape3d_material_value(material_patlist_ptr_cpy, material_type);
			if (pattern_type == SHAPE3D_PATTERN_SOLID) {
				shape3d_retain_render_local_pair(
					LEGACY_U16_WRAP_SUB(drawing_sprite.sprite_raster_right, 1U),
					drawing_sprite.sprite_raster_left, SHAPE3D_LEGACY_SOLID_POLYGON_RETURN_IP);
				preRender_default(material_color, vertex_count, points);
			} else if (pattern_type == SHAPE3D_PATTERN_MASKED) {
				pattern_type = shape3d_material_value(material_patlist2_ptr_cpy, material_type);
				shape3d_retain_opponent_pattern(pattern_type);
				if (pattern_type != 0U) {
					shape3d_retain_render_local(drawing_sprite.sprite_raster_left,
												SHAPE3D_LEGACY_PATTERNED_POLYGON_RETURN_IP);
					preRender_patterned(pattern_type, material_color, vertex_count, points);
				}
			} else if (pattern_type == SHAPE3D_PATTERN_TWO_COLOR) {
				shape3d_retain_opponent_pattern(LEGACY_U16_WRAP_MUL(material_type, 2U));
				/* preRender_unk replaces its first argument with the secondary color. */
				shape3d_retain_render_argument(
					SHAPE3D_LEGACY_TWO_COLOR_POLYGON_RETURN_IP,
					shape3d_material_value(material_clrlist2_ptr_cpy, material_type));
				preRender_two_color(
					shape3d_material_value(material_patlist2_ptr_cpy, material_type),
					shape3d_material_value(material_clrlist2_ptr_cpy, material_type),
					material_color, vertex_count, points);
			}
		} else if (primitive_type == RENDER_PRIMITIVE_LINE) {
			shape3d_retain_render_argument(SHAPE3D_LEGACY_LINE_RETURN_IP,
										   polyinfo_read_word(record, POLYINFO_START_X_WORD));
			preRender_line(polyinfo_read_word(record, POLYINFO_START_X_WORD),
						   polyinfo_read_word(record, POLYINFO_START_Y_WORD),
						   polyinfo_read_word(record, POLYINFO_END_X_WORD),
						   polyinfo_read_word(record, POLYINFO_END_Y_WORD), material_color);
		} else if (primitive_type == RENDER_PRIMITIVE_SPHERE) {
			shape3d_retain_sphere_stack(polyinfo_read_word(record, POLYINFO_SPHERE_SIZE_WORD));
			preRender_sphere(
				LEGACY_S16_FROM_BITS(polyinfo_read_word(record, POLYINFO_START_X_WORD)),
				LEGACY_S16_FROM_BITS(polyinfo_read_word(record, POLYINFO_START_Y_WORD)),
				polyinfo_read_word(record, POLYINFO_SPHERE_SIZE_WORD), material_color);
		} else if (primitive_type == RENDER_PRIMITIVE_WHEEL) {
			shape3d_retain_opponent_pattern(
				LEGACY_U16_WRAP_ADD(legacy_opponent_render_context.material_color_offset,
									LEGACY_U16_WRAP_MUL(material_type, 2U)));
			shape3d_retain_render_argument(
				SHAPE3D_LEGACY_WHEEL_RETURN_IP,
				LEGACY_U16_WRAP_SUB(legacy_render_polygon_frame_pointer,
									SHAPE3D_LEGACY_POLYGON_POINTS_STACK_OFFSET));
			polyinfo_read_points(record, points, WHEEL_SOURCE_POINT_COUNT);
			preRender_wheel(points, WHEEL_INNER_RADIUS_SCALE, material_color,
							shape3d_material_value(material_clrlist_ptr_cpy, material_type + 1U),
							shape3d_material_value(material_clrlist_ptr_cpy, material_type + 2U));
		} else if (primitive_type == RENDER_PRIMITIVE_POINT) {
			shape3d_retain_render_local_pair(shape3d_legacy_record_index(record_index),
											 primitive_index - rendered_ghost_primitives,
											 SHAPE3D_LEGACY_POINT_RETURN_IP);
#if defined(RESTUNTS_TEMPLEOS)
			legacy_u16 x = polyinfo_read_word(record, POLYINFO_START_X_WORD);
			legacy_u16 y = polyinfo_read_word(record, POLYINFO_START_Y_WORD);
			preRender_line(x, y, x, y, material_color);
#else
			sprite_putpixel_clipped(
				LEGACY_S16_FROM_BITS(polyinfo_read_word(record, POLYINFO_START_X_WORD)),
				LEGACY_S16_FROM_BITS(polyinfo_read_word(record, POLYINFO_START_Y_WORD)),
				material_color);
#endif
		}
	}
#if defined(RESTUNTS_TEMPLEOS)
	TempleRasterNative(0);
#endif
	polyinfo_reset();
}
