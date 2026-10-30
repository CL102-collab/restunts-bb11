#include "extended_frame.h"
#include "game_input.h"
#include "trackdata_layout.h"
#include "frame_render_internal.h"
#include "extended_track.h"
#include "extended_geometry.h"
#include "shape3d_internal.h"
#include "shape3d_hires.h"
#include "track_objects.h"
#include "track_collision.h"
#include "scene_resources.h"
#include "owoot_road.h"
#include "externs.h"
#ifndef RESTUNTS_HEADLESS
#include "extended_replay_runtime.h"
#endif

#define FRAME_EXTENDED_GEOMETRY_MAX_DISTANCE 28672L
#define FRAME_EXTENDED_POSITION_SHIFT 6U

enum FRAME_EXTENDED_TERRAIN_MATERIAL {
	FRAME_EXTENDED_GROUND_GRASS_MATERIAL = 16,
	FRAME_EXTENDED_ICE_MATERIAL = 28,
	FRAME_EXTENDED_DIRT_MATERIAL = 25,
	FRAME_EXTENDED_WATER_MATERIAL = 100,
	FRAME_EXTENDED_HILL_GRASS_MATERIAL = 101,
	FRAME_EXTENDED_HILL_GRASS_MATERIAL_LAST = 105
};

#define FRAME_EXTENDED_TERRAIN_HALF_TILE (CSTRK_TILE_WORLD_SIZE / 2)
#define FRAME_EXTENDED_HILL_EDGE_OVERLAP 2
#define FRAME_EXTENDED_HILL_AUTHORED_EDGE                                                          \
	(FRAME_EXTENDED_TERRAIN_HALF_TILE + FRAME_EXTENDED_HILL_EDGE_OVERLAP)
#define FRAME_EXTENDED_TERRAIN_ALWAYS_VISIBLE 1U
#define FRAME_EXTENDED_OUTER_CORNER_FIRST 11U
#define FRAME_EXTENDED_OUTER_CORNER_LAST 14U
#define FRAME_EXTENDED_TERRAIN_CORNER_COUNT 4U
#define FRAME_EXTENDED_OBJECT_PART_COUNT 2U
#define FRAME_EXTENDED_GROUND_FRAGMENT_INITIAL_CAPACITY 16U
#define FRAME_EXTENDED_CLASSIC_GROUND_FRAGMENT_MAX 256U
#define FRAME_EXTENDED_TERRAIN_NEIGHBOR_COUNT 9U
#define FRAME_EXTENDED_TERRAIN_OBJECT_MAX                                                          \
	(FRAME_EXTENDED_TERRAIN_NEIGHBOR_COUNT * EXTENDED_TRACK_CELL_OBJECT_MAX)
#define FRAME_EXTENDED_ROAD_OVERSCAN 8
#define FRAME_EXTENDED_GROUND_CLIPPED_VERTEX_MAX                                                   \
	(SHAPE3D_POLYGON_MAX_VERTICES + OWOOT_ROAD_TRIANGLE_VERTEX_COUNT)

/* Use the same authored edge correction for the graded mesh and its cutout. */
static void frame_extended_join_object_grade_vertex(struct VECTOR *point,
													const struct CSTRK_OBJECT *object)
{
	legacy_s16 *along = object->slope_x ? &point->x : &point->z;
	legacy_s16 half_span =
		(object->slope_x ? object->width : object->height) * FRAME_EXTENDED_TERRAIN_HALF_TILE;
	if (*along > half_span && *along <= half_span + FRAME_EXTENDED_HILL_EDGE_OVERLAP) {
		*along = half_span;
	} else if (*along < -half_span && *along >= -half_span - FRAME_EXTENDED_HILL_EDGE_OVERLAP) {
		*along = -half_span;
	}
}

static void frame_extended_rotate_vertex(struct VECTOR *point, legacy_s16 yaw)
{
	legacy_s16 x = point->x, z = point->z;
	switch (yaw & ANGLE_MASK) {
		case ANGLE_QUARTER_TURN:
			point->x = z;
			point->z = LEGACY_S16_WRAP_NEGATE(x);
			break;
		case ANGLE_HALF_TURN:
			point->x = LEGACY_S16_WRAP_NEGATE(x);
			point->z = LEGACY_S16_WRAP_NEGATE(z);
			break;
		case ANGLE_THREE_QUARTER_TURN:
			point->x = LEGACY_S16_WRAP_NEGATE(z);
			point->z = x;
			break;
	}
}

enum FRAME_EXTENDED_HILL_EDGE {
	FRAME_EXTENDED_HILL_NORTH = 1U,
	FRAME_EXTENDED_HILL_EAST = 2U,
	FRAME_EXTENDED_HILL_SOUTH = 4U,
	FRAME_EXTENDED_HILL_WEST = 8U
};

enum FRAME_EXTENDED_FENCE_SIDE {
	FRAME_EXTENDED_FENCE_NORTH,
	FRAME_EXTENDED_FENCE_EAST,
	FRAME_EXTENDED_FENCE_SOUTH,
	FRAME_EXTENDED_FENCE_WEST,
	FRAME_EXTENDED_FENCE_SIDE_COUNT
};

#define FRAME_EXTENDED_FENCE_STRAIGHT_INDEX_STEP 2U

static legacy_s16 frame_extended_border_index(legacy_s16 coordinate, legacy_u16 dimension)
{
	if (coordinate == 0) {
		return FRAME_FENCE_EDGE_LOW;
	}
	if (coordinate == dimension - 1) {
		return FRAME_FENCE_EDGE_HIGH;
	}
	return FRAME_FENCE_EDGE_INTERIOR;
}

/* The original fence posts sit on tile edges. Interpolate shared terrain
 * corners so adjacent spans meet and each post keeps its original height. */
static legacy_s16 frame_extended_fence_height(const struct CSTRK_TRACK *track,
											  const struct FRAME_TILE *tile,
											  const struct VECTOR *point)
{
	legacy_u32 stride = track->width + 1U;
	legacy_u32 corner = (legacy_u32)tile->south * stride + tile->east;
	const legacy_u8 *heights = &track->corner_heights[corner];
	legacy_s32 u = (legacy_s32)point->x + FRAME_EXTENDED_TERRAIN_HALF_TILE;
	legacy_s32 v = FRAME_EXTENDED_TERRAIN_HALF_TILE - (legacy_s32)point->z;
	legacy_s32 north = heights[0] * (CSTRK_TILE_WORLD_SIZE - u) + heights[1] * u;
	legacy_s32 south = heights[stride] * (CSTRK_TILE_WORLD_SIZE - u) + heights[stride + 1U] * u;
	legacy_s64 height = ((legacy_s64)north * (CSTRK_TILE_WORLD_SIZE - v) + (legacy_s64)south * v) *
						CSTRK_LEVEL_WORLD_HEIGHT;
	return (legacy_s16)(height / ((legacy_s64)CSTRK_TILE_WORLD_SIZE * CSTRK_TILE_WORLD_SIZE));
}

static legacy_s16 frame_draw_extended_fence_span(const struct FRAME_TILE *tile,
												 const struct FRAME_CAMERA *camera,
												 legacy_s8 redraw_transform_flags,
												 legacy_u16 fence_index)
{
	const struct TRACKOBJECT *object =
		frame_track_object_from_legacy_index(fence_TrkObjCodes[fence_index]);
	const struct SHAPE3D *source = frame_track_shape(object, tile->detail);
	struct SHAPE3D mesh;
	if (!extended_geometry_transform_shape(source, &mesh, fence_rotations[fence_index],
										   CSTRK_ROAD_PAVED, FRAME_SCENERY_PHYSICAL_MODEL_FIRST)) {
		return 0;
	}
	const struct CSTRK_TRACK *track = extended_track_model();
	for (legacy_u16 index = 0; index < mesh.shape3d_numverts; index++) {
		struct VECTOR point;
		shape3d_vertex_read(&mesh, index, &point);
		point.y = LEGACY_S16_WRAP_ADD(point.y, frame_extended_fence_height(track, tile, &point));
		shape3d_vertex_write(&mesh, index, &point);
	}
	struct TRANSFORMEDSHAPE3D instance = {0};
	frame_prepare_flat_track_shape(&instance, tile->east, tile->south, &camera->position,
								   redraw_transform_flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT, 0);
	instance.shapeptr = &mesh;
	legacy_s16 exhausted = (legacy_s16)shape3d_transform_and_queue(&instance) > 0;
	extended_geometry_free_shape(&mesh);
	return exhausted;
}

legacy_s16 extended_frame_draw_fences(const struct FRAME_TILE *tile,
									  const struct FRAME_TILE_SELECTION *tiles,
									  const struct FRAME_CAMERA *camera,
									  legacy_s8 redraw_transform_flags)
{
	if (detail_level != FRAME_DETAIL_FULL &&
		(tile->east != tiles->player_east || tile->south != tiles->player_south)) {
		return 0;
	}
	const struct CSTRK_TRACK *track = extended_track_model();
	if (track->width > 1U && track->height > 1U) {
		legacy_s16 east = frame_extended_border_index(tile->east, track->width);
		legacy_s16 south = frame_extended_border_index(tile->south, track->height);
		legacy_s16 fence_index = fence_by_edge[east][south];
		return fence_index == FRAME_FENCE_NONE
				   ? 0
				   : frame_draw_extended_fence_span(tile, camera, redraw_transform_flags,
													(legacy_u16)fence_index);
	}
	/* A one-cell-wide map needs opposite sides in the same cell, which the
	 * original corner lookup cannot represent. Queue each boundary once. */
	legacy_u8 sides[FRAME_EXTENDED_FENCE_SIDE_COUNT];
	sides[FRAME_EXTENDED_FENCE_NORTH] = tile->south == 0;
	sides[FRAME_EXTENDED_FENCE_EAST] = tile->east == track->width - 1;
	sides[FRAME_EXTENDED_FENCE_SOUTH] = tile->south == track->height - 1;
	sides[FRAME_EXTENDED_FENCE_WEST] = tile->east == 0;
	for (legacy_u16 side = 0; side < FRAME_EXTENDED_FENCE_SIDE_COUNT; side++) {
		if (sides[side] &&
			frame_draw_extended_fence_span(tile, camera, redraw_transform_flags,
										   side * FRAME_EXTENDED_FENCE_STRAIGHT_INDEX_STEP)) {
			return 1;
		}
	}
	return 0;
}

static legacy_u8 frame_extended_hill_edges(const struct CSTRK_TRACK *track, legacy_s16 x,
										   legacy_s16 y)
{
	struct CSTRK_TERRAIN_PROFILE profile;
	if (supersight_enabled == 0 ||
		!cstrk_terrain_profile(track, (legacy_u16)x, (legacy_u16)y, &profile) ||
		profile.terrain_id < CSTRK_TERRAIN_PROFILE_NORTH) {
		return 0;
	}
	static const struct {
		legacy_s8 dx, dy;
		legacy_u8 edge;
	} neighbors[] = {{0, -1, FRAME_EXTENDED_HILL_NORTH},
					 {1, 0, FRAME_EXTENDED_HILL_EAST},
					 {0, 1, FRAME_EXTENDED_HILL_SOUTH},
					 {-1, 0, FRAME_EXTENDED_HILL_WEST}};
	legacy_u8 edges = 0;
	for (legacy_u8 index = 0; index < sizeof(neighbors) / sizeof(neighbors[0]); index++) {
		legacy_s16 nx = x + neighbors[index].dx, ny = y + neighbors[index].dy;
		struct CSTRK_TERRAIN_PROFILE neighbor;
		if (nx >= 0 && ny >= 0 && nx < track->width && ny < track->height &&
			cstrk_terrain_profile(track, (legacy_u16)nx, (legacy_u16)ny, &neighbor) &&
			neighbor.terrain_id >= CSTRK_TERRAIN_PROFILE_NORTH &&
			neighbor.base_height != profile.base_height) {
			edges |= neighbors[index].edge;
		}
	}
	return edges;
}

/* Native hills overlap a tile by two units. At consecutive elevations that
 * overlap separates the two slope planes vertically. HyperVision joins both
 * sides at the shared grid edge; plateau joins retain their authored overlap. */
static void frame_extended_join_hill_vertex(struct VECTOR *point, legacy_u8 edges)
{
	if ((edges & FRAME_EXTENDED_HILL_EAST) && point->x > FRAME_EXTENDED_TERRAIN_HALF_TILE &&
		point->x <= FRAME_EXTENDED_HILL_AUTHORED_EDGE) {
		point->x = FRAME_EXTENDED_TERRAIN_HALF_TILE;
	}
	if ((edges & FRAME_EXTENDED_HILL_WEST) && point->x < -FRAME_EXTENDED_TERRAIN_HALF_TILE &&
		point->x >= -FRAME_EXTENDED_HILL_AUTHORED_EDGE) {
		point->x = -FRAME_EXTENDED_TERRAIN_HALF_TILE;
	}
	if ((edges & FRAME_EXTENDED_HILL_NORTH) && point->z > FRAME_EXTENDED_TERRAIN_HALF_TILE &&
		point->z <= FRAME_EXTENDED_HILL_AUTHORED_EDGE) {
		point->z = FRAME_EXTENDED_TERRAIN_HALF_TILE;
	}
	if ((edges & FRAME_EXTENDED_HILL_SOUTH) && point->z < -FRAME_EXTENDED_TERRAIN_HALF_TILE &&
		point->z >= -FRAME_EXTENDED_HILL_AUTHORED_EDGE) {
		point->z = -FRAME_EXTENDED_TERRAIN_HALF_TILE;
	}
}

static legacy_u8 frame_extended_grade_edges(const struct CSTRK_OBJECT *object)
{
	return object->slope_x ? FRAME_EXTENDED_HILL_EAST | FRAME_EXTENDED_HILL_WEST
						   : FRAME_EXTENDED_HILL_NORTH | FRAME_EXTENDED_HILL_SOUTH;
}

/* Attached markings are layers of the preceding face. Native hill dashes
 * have independently rounded (and occasionally incorrect) heights, visible as
 * dips in HyperVision. Drape them onto their corrected road instead of retaining
 * those offsets. Curbs and horizontal spans keep their authored heights. */
static void frame_extended_align_hill_markings(struct SHAPE3D *mesh)
{
	const legacy_u8 *primitive = mesh->shape3d_primitives;
	struct VECTOR origin = {0};
	legacy_s64 nx = 0, ny = 0, nz = 0;
	for (legacy_u16 index = 0; index < mesh->shape3d_numprimitives; index++) {
		legacy_u16 count = primitive[0];
		if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST || count > SHAPE3D_PRIMITIVE_POLYGON_LAST) {
			break;
		}
		const legacy_u8 *indices =
			primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + mesh->shape3d_numpaints;
		if ((primitive[1] & SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) == 0) {
			shape3d_vertex_read(mesh, indices[0], &origin);
			nx = ny = nz = 0;
			for (legacy_u16 vertex = 1; vertex + 1U < count && ny == 0; vertex++) {
				struct VECTOR first, last;
				shape3d_vertex_read(mesh, indices[vertex], &first);
				shape3d_vertex_read(mesh, indices[vertex + 1U], &last);
				legacy_s32 ax = first.x - origin.x, ay = first.y - origin.y,
						   az = first.z - origin.z;
				legacy_s32 bx = last.x - origin.x, by = last.y - origin.y, bz = last.z - origin.z;
				nx = (legacy_s64)ay * bz - (legacy_s64)az * by;
				ny = (legacy_s64)az * bx - (legacy_s64)ax * bz;
				nz = (legacy_s64)ax * by - (legacy_s64)ay * bx;
			}
		} else if (ny != 0 && (nx != 0 || nz != 0)) {
			for (legacy_u16 vertex = 0; vertex < count; vertex++) {
				struct VECTOR point;
				shape3d_vertex_read(mesh, indices[vertex], &point);
				legacy_s64 numerator =
					ny * origin.y - nx * (point.x - origin.x) - nz * (point.z - origin.z);
				legacy_s64 denominator = ny;
				if (denominator < 0) {
					numerator = -numerator;
					denominator = -denominator;
				}
				point.y = (legacy_s16)((numerator +
										(numerator < 0 ? -denominator / 2 : denominator / 2)) /
									   denominator);
				shape3d_vertex_write(mesh, indices[vertex], &point);
			}
		}
		primitive = indices + count;
	}
}

/* The narrow, inclined first face of a native hill-road mesh is its road.
 * Give the pavement and curbs a uniform grid-sized run, while retaining the
 * surrounding terrain boundary's overlap at unrelated hills and corners. */
static legacy_u8 frame_extended_road_grade_edges(const struct SHAPE3D *source, legacy_s16 yaw,
												 legacy_u8 edges)
{
	const legacy_u8 *primitive = source->shape3d_primitives;
	legacy_u16 count = primitive[0];
	if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST || count > SHAPE3D_PRIMITIVE_POLYGON_LAST) {
		return 0;
	}
	struct VECTOR low = {0}, high = {0};
	for (legacy_u16 vertex = 0; vertex < count; vertex++) {
		struct VECTOR point;
		shape3d_vertex_read(
			source, primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE + source->shape3d_numpaints + vertex],
			&point);
		frame_extended_rotate_vertex(&point, yaw);
		if (vertex == 0) {
			low = high = point;
		}
		if (point.x < low.x) {
			low.x = point.x;
		}
		if (point.y < low.y) {
			low.y = point.y;
		}
		if (point.z < low.z) {
			low.z = point.z;
		}
		if (point.x > high.x) {
			high.x = point.x;
		}
		if (point.y > high.y) {
			high.y = point.y;
		}
		if (point.z > high.z) {
			high.z = point.z;
		}
	}
	if (high.y - low.y != CSTRK_LEVEL_WORLD_HEIGHT) {
		return 0;
	}
	legacy_u8 ends = 0;
	if (high.z - low.z >= CSTRK_TILE_WORLD_SIZE && high.x - low.x < CSTRK_TILE_WORLD_SIZE) {
		ends = FRAME_EXTENDED_HILL_NORTH | FRAME_EXTENDED_HILL_SOUTH;
	} else if (high.x - low.x >= CSTRK_TILE_WORLD_SIZE && high.z - low.z < CSTRK_TILE_WORLD_SIZE) {
		ends = FRAME_EXTENDED_HILL_EAST | FRAME_EXTENDED_HILL_WEST;
	}
	return (edges & ends) ? ends : 0;
}

static legacy_u16 frame_extended_queue_hill_shape(struct TRANSFORMEDSHAPE3D *instance,
												  legacy_u8 edges)
{
	const struct SHAPE3D *source = instance->shapeptr;
	if (edges == 0 || source->shape3d_numverts > LEGACY_U8_MAX) {
		return shape3d_transform_and_queue(instance);
	}
	legacy_u8 grade_edges = frame_extended_road_grade_edges(source, instance->rotvec.z, edges);
	struct TRANSFORMEDSHAPE3D adjusted = *instance;
	/* Resource vertex counts occupy one byte. Queueing copies transformed
	 * vertices, so the original resource remains intact. */
	legacy_u8 vertices[LEGACY_U8_MAX * SHAPE3D_VERTEX_SIZE];
	struct SHAPE3D mesh = *source;
	mesh.shape3d_vertex_bytes = vertices;
	for (legacy_u16 index = 0; index < source->shape3d_numverts; index++) {
		struct VECTOR point;
		shape3d_vertex_read(source, index, &point);
		frame_extended_rotate_vertex(&point, instance->rotvec.z);
		frame_extended_join_hill_vertex(&point, edges);
		if (((grade_edges & (FRAME_EXTENDED_HILL_NORTH | FRAME_EXTENDED_HILL_SOUTH)) &&
			 point.x > -FRAME_EXTENDED_TERRAIN_HALF_TILE &&
			 point.x < FRAME_EXTENDED_TERRAIN_HALF_TILE) ||
			((grade_edges & (FRAME_EXTENDED_HILL_EAST | FRAME_EXTENDED_HILL_WEST)) &&
			 point.z > -FRAME_EXTENDED_TERRAIN_HALF_TILE &&
			 point.z < FRAME_EXTENDED_TERRAIN_HALF_TILE)) {
			frame_extended_join_hill_vertex(&point, grade_edges);
		}
		frame_extended_rotate_vertex(&point, LEGACY_S16_WRAP_NEGATE(instance->rotvec.z));
		shape3d_vertex_write(&mesh, index, &point);
	}
	if (grade_edges != 0) {
		frame_extended_align_hill_markings(&mesh);
	}
	adjusted.shapeptr = &mesh;
	return shape3d_transform_and_queue(&adjusted);
}

static legacy_s32 frame_extended_shore_distance(const struct VECTOR *point, legacy_u8 corner)
{
	switch (corner) {
		case CSTRK_WATER_NORTHWEST:
			return (legacy_s32)point->z - point->x;
		case CSTRK_WATER_NORTHEAST:
			return (legacy_s32)point->x + point->z;
		case CSTRK_WATER_SOUTHEAST:
			return (legacy_s32)point->x - point->z;
		default:
			return -(legacy_s32)point->x - point->z;
	}
}

static void frame_extended_append_terrain_vertex(struct VECTOR *output, legacy_u16 *count,
												 const struct VECTOR *point)
{
	if (*count == 0 || output[*count - 1U].x != point->x || output[*count - 1U].y != point->y ||
		output[*count - 1U].z != point->z) {
		output[(*count)++] = *point;
	}
}

/* Clip paint boundaries after choosing the native hill mesh. The same vertices
 * and collision surface are retained regardless of the selected Shore corner. */
static legacy_u16 frame_extended_clip_terrain(const struct VECTOR *source, legacy_u16 count,
											  struct VECTOR *output, legacy_u8 corner,
											  legacy_u8 secondary)
{
	legacy_u16 written = 0;
	struct VECTOR previous = source[count - 1U];
	legacy_s32 previous_distance = frame_extended_shore_distance(&previous, corner);
	if (!secondary) {
		previous_distance = -previous_distance;
	}
	for (legacy_u16 index = 0; index < count; index++) {
		struct VECTOR current = source[index];
		legacy_s32 distance = frame_extended_shore_distance(&current, corner);
		if (!secondary) {
			distance = -distance;
		}
		if ((distance >= 0) != (previous_distance >= 0)) {
			legacy_s32 divisor = previous_distance - distance;
			/* A symmetric rational expression rounds both painted halves to
			 * exactly the same word-sized point, even on a half-unit crossing. */
			struct VECTOR crossing = {(legacy_s16)(((legacy_s32)current.x * previous_distance -
													(legacy_s32)previous.x * distance) /
												   divisor),
									  (legacy_s16)(((legacy_s32)current.y * previous_distance -
													(legacy_s32)previous.y * distance) /
												   divisor),
									  (legacy_s16)(((legacy_s32)current.z * previous_distance -
													(legacy_s32)previous.z * distance) /
												   divisor)};
			frame_extended_append_terrain_vertex(output, &written, &crossing);
		}
		if (distance >= 0) {
			frame_extended_append_terrain_vertex(output, &written, &current);
		}
		previous = current;
		previous_distance = distance;
	}
	if (written > 1U && output[0].x == output[written - 1U].x &&
		output[0].y == output[written - 1U].y && output[0].z == output[written - 1U].z) {
		written--;
	}
	return written;
}

static legacy_s16 frame_extended_queue_terrain_polygon(const struct VECTOR *points,
													   legacy_u16 count, legacy_u8 primitive_flags,
													   legacy_u8 material,
													   const struct VECTOR *position,
													   legacy_s8 flags)
{
	if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST) {
		return 0;
	}
	if (count > SHAPE3D_POLYGON_MAX_VERTICES) {
		for (legacy_u16 index = 1; index + 1U < count; index++) {
			struct VECTOR triangle[] = {points[0], points[index], points[index + 1U]};
			if (frame_extended_queue_terrain_polygon(triangle, SHAPE3D_PRIMITIVE_POLYGON_FIRST,
													 primitive_flags, material, position, flags)) {
				return 1;
			}
		}
		return 0;
	}
	legacy_u8 bytes[SHAPE3D_POLYGON_MAX_VERTICES * SHAPE3D_VERTEX_SIZE];
	legacy_u8 primitive[SHAPE3D_POLYGON_MAX_VERTICES + 5U] = {0};
	legacy_u8 masks[SHAPE3D_VISIBILITY_MASK_SIZE] = {255, 255, 255, 255};
	legacy_u8 front_masks[SHAPE3D_VISIBILITY_MASK_SIZE] = {0};
	struct SHAPE3D shape = {count, bytes, 1, 1, primitive, masks, front_masks};
	primitive[0] = (legacy_u8)count;
	primitive[1] = primitive_flags;
	primitive[2] = material;
	for (legacy_u16 index = 0; index < count; index++) {
		shape3d_vertex_write(&shape, index, &points[index]);
		primitive[index + 3U] = (legacy_u8)index;
	}
	primitive[count + 3U] = 0;
	struct TRANSFORMEDSHAPE3D instance = {0};
	instance.pos = *position;
	instance.shapeptr = &shape;
	instance.rectptr = &frame_sorted_shapes_rect;
	instance.culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
	instance.ts_flags = flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
	return (legacy_s16)shape3d_transform_and_queue(&instance) > 0 && supersight_enabled != 0;
}

static legacy_s16 frame_extended_paint_terrain_polygon(const struct VECTOR *points,
													   legacy_u16 count, legacy_u8 primitive_flags,
													   legacy_u8 grass_material,
													   const struct CSTRK_CELL *cell,
													   const struct VECTOR *position,
													   legacy_s8 flags)
{
	static const legacy_u8 materials[] = {FRAME_EXTENDED_GROUND_GRASS_MATERIAL,
										  FRAME_EXTENDED_ICE_MATERIAL, FRAME_EXTENDED_DIRT_MATERIAL,
										  FRAME_EXTENDED_WATER_MATERIAL};
	for (legacy_u8 half = 0; half <= (cell->half_water_corner != CSTRK_WATER_NONE); half++) {
		struct VECTOR clipped[SHAPE3D_POLYGON_MAX_VERTICES + 1U];
		const struct VECTOR *polygon = points;
		legacy_u16 polygon_count = count;
		legacy_u8 surface = half ? cell->corner_surface : cell->surface;
		legacy_u8 material = surface == CSTRK_TERRAIN_GRASS ? grass_material : materials[surface];
		if (cell->half_water_corner != CSTRK_WATER_NONE) {
			polygon_count =
				frame_extended_clip_terrain(points, count, clipped, cell->half_water_corner, half);
			polygon = clipped;
		}
		if (frame_extended_queue_terrain_polygon(polygon, polygon_count, primitive_flags, material,
												 position, flags)) {
			return 1;
		}
	}
	return 0;
}

struct FRAME_EXTENDED_TERRAIN {
	struct CSTRK_TERRAIN_PROFILE profile;
	const struct CSTRK_CELL *cell;
	const struct CSTRK_OBJECT *object;
	const struct CSTRK_OBJECT *objects[FRAME_EXTENDED_TERRAIN_OBJECT_MAX];
	legacy_u16 object_count;
	struct VECTOR position;
	legacy_u8 edges, graded;
	legacy_s16 column, row;
	legacy_s8 flags;
};

/* The corrected native-road classification belongs to HyperVision rendering.
 * Classic frames retain their historical treatment of the two road directions
 * which have no separate catalog spelling. Physics uses the common predicate. */
static legacy_u8 frame_extended_uses_grade(const struct CSTRK_OBJECT *object)
{
	if (!supersight_enabled && object && (object->slope_x || object->slope_y) &&
		object->width == 1U && object->height == 1U &&
		trkObjectList[object->legacy_id].ss_physicalModel == PHYSICAL_MODEL_ROAD) {
		legacy_u16 angle = ((legacy_u16)trkObjectList[object->legacy_id].ss_rotY +
							object->rotation * ANGLE_QUARTER_TURN) &
						   ANGLE_MASK;
		if (angle & ANGLE_HALF_TURN) {
			return 1;
		}
	}
	return extended_geometry_uses_grade(object);
}

static legacy_s16 frame_extended_object_grade(const struct CSTRK_OBJECT *object, legacy_s16 local_x,
											  legacy_s16 local_z)
{
	if (supersight_enabled) {
		return extended_geometry_object_grade(object, local_x, local_z);
	}
	return frame_extended_uses_grade(object)
			   ? extended_geometry_support_grade(object, local_x, local_z)
			   : 0;
}

static void frame_extended_collect_terrain_objects(struct FRAME_EXTENDED_TERRAIN *terrain)
{
	terrain->object_count = extended_track_objects_in_cell(
		terrain->column, terrain->row, terrain->objects, EXTENDED_TRACK_CELL_OBJECT_MAX);
	if (!supersight_enabled) {
		return;
	}
	/* Authored roads overhang their grid rectangle by a few world units.
	 * Cut those faces from neighboring terrain too, using the same bounded
	 * origin lookup rather than scanning every object in the track. */
	legacy_s32 left = terrain->column * CSTRK_TILE_WORLD_SIZE;
	legacy_s32 north = terrain->row * CSTRK_TILE_WORLD_SIZE;
	for (legacy_s16 row = terrain->row - 1; row <= terrain->row + 1; row++) {
		for (legacy_s16 column = terrain->column - 1; column <= terrain->column + 1; column++) {
			if (column == terrain->column && row == terrain->row) {
				continue;
			}
			const struct CSTRK_OBJECT *candidates[EXTENDED_TRACK_CELL_OBJECT_MAX];
			legacy_u16 count = extended_track_objects_in_cell(column, row, candidates,
															  EXTENDED_TRACK_CELL_OBJECT_MAX);
			for (legacy_u16 candidate = 0; candidate < count; candidate++) {
				const struct CSTRK_OBJECT *object = candidates[candidate];
				legacy_s32 object_left = object->x * CSTRK_TILE_WORLD_SIZE;
				legacy_s32 object_north = object->y * CSTRK_TILE_WORLD_SIZE;
				if (left >= object_left + object->width * CSTRK_TILE_WORLD_SIZE +
								FRAME_EXTENDED_ROAD_OVERSCAN ||
					left + CSTRK_TILE_WORLD_SIZE <= object_left - FRAME_EXTENDED_ROAD_OVERSCAN ||
					north >= object_north + object->height * CSTRK_TILE_WORLD_SIZE +
								 FRAME_EXTENDED_ROAD_OVERSCAN ||
					north + CSTRK_TILE_WORLD_SIZE <= object_north - FRAME_EXTENDED_ROAD_OVERSCAN) {
					continue;
				}
				legacy_u16 existing = 0;
				while (existing < terrain->object_count && terrain->objects[existing] != object) {
					existing++;
				}
				if (existing == terrain->object_count &&
					terrain->object_count < FRAME_EXTENDED_TERRAIN_OBJECT_MAX) {
					terrain->objects[terrain->object_count++] = object;
				}
			}
		}
	}
}

static legacy_u8 frame_extended_native_grass(const struct CSTRK_CELL *cell)
{
	return cell->surface == CSTRK_TERRAIN_GRASS && cell->half_water_corner == CSTRK_WATER_NONE;
}

static legacy_u8 frame_extended_object_sloped(const struct CSTRK_OBJECT *object)
{
	return object && (object->slope_x || object->slope_y);
}

static legacy_s16
frame_extended_draw_supporting_ground(const struct FRAME_EXTENDED_TERRAIN *terrain)
{
	const struct CSTRK_TERRAIN_PROFILE *profile = &terrain->profile;
	if (profile->terrain_id != 0 && (profile->terrain_id < FRAME_EXTENDED_OUTER_CORNER_FIRST ||
									 profile->terrain_id > FRAME_EXTENDED_OUTER_CORNER_LAST)) {
		return 0;
	}
	if (profile->base_height == 0 && frame_extended_native_grass(terrain->cell)) {
		return 0;
	}
	/* Only the low half of an outer corner needs a supporting fill.
	 * A full quad beneath a slope can overpaint it in the classic painter. */
	const struct VECTOR flat[] = {
		{-FRAME_EXTENDED_TERRAIN_HALF_TILE, 0, FRAME_EXTENDED_TERRAIN_HALF_TILE},
		{FRAME_EXTENDED_TERRAIN_HALF_TILE, 0, FRAME_EXTENDED_TERRAIN_HALF_TILE},
		{FRAME_EXTENDED_TERRAIN_HALF_TILE, 0, -FRAME_EXTENDED_TERRAIN_HALF_TILE},
		{-FRAME_EXTENDED_TERRAIN_HALF_TILE, 0, -FRAME_EXTENDED_TERRAIN_HALF_TILE}};
	struct VECTOR low_half[SHAPE3D_PRIMITIVE_POLYGON_FIRST];
	const struct VECTOR *points = flat;
	legacy_u16 count = sizeof(flat) / sizeof(flat[0]);
	if (profile->terrain_id != 0) {
		legacy_u8 high_corner = 0;
		while (!(profile->high_mask & (1U << high_corner))) {
			high_corner++;
		}
		count = sizeof(low_half) / sizeof(low_half[0]);
		for (legacy_u16 vertex = 0; vertex < count; vertex++) {
			low_half[vertex] =
				flat[(high_corner + vertex + 1U) % FRAME_EXTENDED_TERRAIN_CORNER_COUNT];
		}
		points = low_half;
	}
	legacy_u8 material = profile->base_height == 0 ? FRAME_EXTENDED_GROUND_GRASS_MATERIAL
												   : FRAME_EXTENDED_HILL_GRASS_MATERIAL;
	return frame_extended_paint_terrain_polygon(points, count,
												FRAME_EXTENDED_TERRAIN_ALWAYS_VISIBLE, material,
												terrain->cell, &terrain->position, terrain->flags);
}

struct FRAME_EXTENDED_GROUND_FRAGMENT {
	legacy_u16 count;
	struct VECTOR points[SHAPE3D_POLYGON_MAX_VERTICES];
};

static legacy_s64 frame_extended_ground_side(const struct VECTOR *a, const struct VECTOR *b,
											 const struct VECTOR *point)
{
	return (legacy_s64)(b->x - a->x) * (point->z - a->z) -
		   (legacy_s64)(b->z - a->z) * (point->x - a->x);
}

static legacy_u16 frame_extended_clip_ground(const struct VECTOR *points, legacy_u16 count,
											 struct VECTOR *output, const struct VECTOR *a,
											 const struct VECTOR *b, legacy_s16 orientation)
{
	if (!count) {
		return 0;
	}
	legacy_u16 written = 0;
	struct VECTOR previous = points[count - 1U];
	legacy_s64 previous_side = frame_extended_ground_side(a, b, &previous) * orientation;
	for (legacy_u16 vertex = 0; vertex < count; vertex++) {
		struct VECTOR current = points[vertex];
		legacy_s64 side = frame_extended_ground_side(a, b, &current) * orientation;
		if ((side >= 0) != (previous_side >= 0)) {
			legacy_s64 divisor = previous_side - side;
			struct VECTOR crossing = {(legacy_s16)(((legacy_s64)current.x * previous_side -
													(legacy_s64)previous.x * side) /
												   divisor),
									  (legacy_s16)(((legacy_s64)current.y * previous_side -
													(legacy_s64)previous.y * side) /
												   divisor),
									  (legacy_s16)(((legacy_s64)current.z * previous_side -
													(legacy_s64)previous.z * side) /
												   divisor)};
			frame_extended_append_terrain_vertex(output, &written, &crossing);
		}
		if (side >= 0) {
			frame_extended_append_terrain_vertex(output, &written, &current);
		}
		previous = current;
		previous_side = side;
	}
	if (written > 1U && memcmp(&output[0], &output[written - 1U], sizeof(output[0])) == 0) {
		written--;
	}
	return written;
}

static legacy_s16
frame_extended_append_ground_fragments(struct FRAME_EXTENDED_GROUND_FRAGMENT **fragments,
									   legacy_u16 *written, legacy_u16 *capacity,
									   const struct VECTOR *points, legacy_u16 count)
{
	if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST) {
		return 1;
	}
	if (count > SHAPE3D_POLYGON_MAX_VERTICES ||
		(!supersight_enabled && count > OWOOT_ROAD_TRIANGLE_VERTEX_COUNT)) {
		for (legacy_u16 vertex = 1; vertex + 1U < count; vertex++) {
			struct VECTOR triangle[] = {points[0], points[vertex], points[vertex + 1U]};
			if (!frame_extended_append_ground_fragments(fragments, written, capacity, triangle,
														OWOOT_ROAD_TRIANGLE_VERTEX_COUNT)) {
				return 0;
			}
		}
		return 1;
	}
	legacy_s64 area = 0;
	for (legacy_u16 vertex = 1; vertex + 1U < count; vertex++) {
		area += frame_extended_ground_side(&points[0], &points[vertex], &points[vertex + 1U]);
	}
	if (!area) {
		return 1;
	}
	if (!supersight_enabled && *written == FRAME_EXTENDED_CLASSIC_GROUND_FRAGMENT_MAX) {
		return 0;
	}
	if (*written == *capacity) {
		if (*capacity == LEGACY_U16_MAX) {
			return 0;
		}
		legacy_u32 next_capacity = *capacity ? (legacy_u32)*capacity * 2U
											 : FRAME_EXTENDED_GROUND_FRAGMENT_INITIAL_CAPACITY;
		if (next_capacity > LEGACY_U16_MAX) {
			next_capacity = LEGACY_U16_MAX;
		}
		struct FRAME_EXTENDED_GROUND_FRAGMENT *grown =
			realloc(*fragments, next_capacity * sizeof(**fragments));
		if (!grown) {
			return 0;
		}
		*fragments = grown;
		*capacity = (legacy_u16)next_capacity;
	}
	/* HyperVision keeps convex remainders intact until final submission.
	 * Premature fan triangulation cuts new internal edges repeatedly on curves.
	 * The classic path retains its original triangulation and fragment budget. */
	(*fragments)[*written].count = count;
	memcpy((*fragments)[*written].points, points, count * sizeof(points[0]));
	(*written)++;
	return 1;
}

#if defined(RESTUNTS_SDL3)
/* Curved-road cuts create T-junctions in the remaining ground. Keep their
 * coordinates fractional through projection: rounding even only the final
 * vertices moves a subdivided edge away from its unsplit neighbor. */
#define FRAME_EXTENDED_GROUND_AREA_EPSILON 0.00000001

static void frame_extended_append_precise_vertex(struct SHAPE3D_HIRES_VECTOR *output,
												 legacy_u16 *count,
												 const struct SHAPE3D_HIRES_VECTOR *point)
{
	if (!*count || output[*count - 1U].x != point->x || output[*count - 1U].y != point->y ||
		output[*count - 1U].z != point->z) {
		output[(*count)++] = *point;
	}
}

static legacy_f64 frame_extended_precise_shore_distance(const struct SHAPE3D_HIRES_VECTOR *point,
														legacy_u8 corner)
{
	switch (corner) {
		case CSTRK_WATER_NORTHWEST:
			return point->z - point->x;
		case CSTRK_WATER_NORTHEAST:
			return point->x + point->z;
		case CSTRK_WATER_SOUTHEAST:
			return point->x - point->z;
		default:
			return -point->x - point->z;
	}
}

static legacy_u16 frame_extended_clip_precise_terrain(const struct SHAPE3D_HIRES_VECTOR *source,
													  legacy_u16 count,
													  struct SHAPE3D_HIRES_VECTOR *output,
													  legacy_u8 corner, legacy_u8 secondary)
{
	legacy_u16 written = 0;
	struct SHAPE3D_HIRES_VECTOR previous = source[count - 1U];
	legacy_f64 previous_distance = frame_extended_precise_shore_distance(&previous, corner);
	if (!secondary) {
		previous_distance = -previous_distance;
	}
	for (legacy_u16 index = 0; index < count; index++) {
		struct SHAPE3D_HIRES_VECTOR current = source[index];
		legacy_f64 distance = frame_extended_precise_shore_distance(&current, corner);
		if (!secondary) {
			distance = -distance;
		}
		if ((distance >= 0) != (previous_distance >= 0)) {
			legacy_f64 divisor = previous_distance - distance;
			/* Use the same intersection for the two painted halves. */
			struct SHAPE3D_HIRES_VECTOR crossing = {
				((current.x * previous_distance - previous.x * distance) / divisor),
				((current.y * previous_distance - previous.y * distance) / divisor),
				((current.z * previous_distance - previous.z * distance) / divisor)};
			frame_extended_append_precise_vertex(output, &written, &crossing);
		}
		if (distance >= 0) {
			frame_extended_append_precise_vertex(output, &written, &current);
		}
		previous = current;
		previous_distance = distance;
	}
	if (written > 1U && output[0].x == output[written - 1U].x &&
		output[0].y == output[written - 1U].y && output[0].z == output[written - 1U].z) {
		written--;
	}
	return written;
}

static legacy_s16 frame_extended_queue_precise_terrain(const struct SHAPE3D_HIRES_VECTOR *points,
													   legacy_u16 count, legacy_u8 primitive_flags,
													   legacy_u8 material,
													   const struct VECTOR *position,
													   legacy_s8 flags)
{
	if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST) {
		return 0;
	}
	if (count > SHAPE3D_POLYGON_MAX_VERTICES) {
		for (legacy_u16 index = 1; index + 1U < count; index++) {
			struct SHAPE3D_HIRES_VECTOR triangle[] = {points[0], points[index], points[index + 1U]};
			if (frame_extended_queue_precise_terrain(triangle, SHAPE3D_PRIMITIVE_POLYGON_FIRST,
													 primitive_flags, material, position, flags)) {
				return 1;
			}
		}
		return 0;
	}
	legacy_u8 bytes[SHAPE3D_POLYGON_MAX_VERTICES * SHAPE3D_VERTEX_SIZE];
	legacy_u8 primitive[SHAPE3D_POLYGON_MAX_VERTICES + 5U] = {0};
	legacy_u8 masks[SHAPE3D_VISIBILITY_MASK_SIZE] = {255, 255, 255, 255};
	legacy_u8 front_masks[SHAPE3D_VISIBILITY_MASK_SIZE] = {0};
	struct SHAPE3D shape = {count, bytes, 1, 1, primitive, masks, front_masks};
	primitive[0] = (legacy_u8)count;
	primitive[1] = primitive_flags;
	primitive[2] = material;
	for (legacy_u16 index = 0; index < count; index++) {
		struct VECTOR fallback = {(legacy_s16)points[index].x, (legacy_s16)points[index].y,
								  (legacy_s16)points[index].z};
		shape3d_vertex_write(&shape, index, &fallback);
		primitive[index + 3U] = (legacy_u8)index;
	}
	primitive[count + 3U] = 0;
	struct TRANSFORMEDSHAPE3D instance = {0};
	instance.pos = *position;
	instance.shapeptr = &shape;
	instance.rectptr = &frame_sorted_shapes_rect;
	instance.culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
	instance.ts_flags = flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
	return (legacy_s16)shape3d_hires_transform_and_queue(&instance, points) > 0 &&
		   supersight_enabled != 0;
}

static legacy_s16 frame_extended_paint_precise_terrain(const struct SHAPE3D_HIRES_VECTOR *points,
													   legacy_u16 count, legacy_u8 primitive_flags,
													   legacy_u8 grass_material,
													   const struct CSTRK_CELL *cell,
													   const struct VECTOR *position,
													   legacy_s8 flags)
{
	static const legacy_u8 materials[] = {FRAME_EXTENDED_GROUND_GRASS_MATERIAL,
										  FRAME_EXTENDED_ICE_MATERIAL, FRAME_EXTENDED_DIRT_MATERIAL,
										  FRAME_EXTENDED_WATER_MATERIAL};
	for (legacy_u8 half = 0; half <= (cell->half_water_corner != CSTRK_WATER_NONE); half++) {
		struct SHAPE3D_HIRES_VECTOR clipped[SHAPE3D_POLYGON_MAX_VERTICES + 1U];
		const struct SHAPE3D_HIRES_VECTOR *polygon = points;
		legacy_u16 polygon_count = count;
		legacy_u8 surface = half ? cell->corner_surface : cell->surface;
		legacy_u8 material = surface == CSTRK_TERRAIN_GRASS ? grass_material : materials[surface];
		if (cell->half_water_corner != CSTRK_WATER_NONE) {
			polygon_count = frame_extended_clip_precise_terrain(points, count, clipped,
																cell->half_water_corner, half);
			polygon = clipped;
		}
		if (frame_extended_queue_precise_terrain(polygon, polygon_count, primitive_flags, material,
												 position, flags)) {
			return 1;
		}
	}
	return 0;
}

struct FRAME_EXTENDED_PRECISE_GROUND_FRAGMENT {
	legacy_u16 count;
	struct SHAPE3D_HIRES_VECTOR points[SHAPE3D_POLYGON_MAX_VERTICES];
};

static legacy_f64 frame_extended_precise_ground_side(const struct SHAPE3D_HIRES_VECTOR *a,
													 const struct SHAPE3D_HIRES_VECTOR *b,
													 const struct SHAPE3D_HIRES_VECTOR *point)
{
	return (legacy_f64)(b->x - a->x) * (point->z - a->z) -
		   (legacy_f64)(b->z - a->z) * (point->x - a->x);
}

static legacy_u16 frame_extended_clip_precise_ground(const struct SHAPE3D_HIRES_VECTOR *points,
													 legacy_u16 count,
													 struct SHAPE3D_HIRES_VECTOR *output,
													 const struct SHAPE3D_HIRES_VECTOR *a,
													 const struct SHAPE3D_HIRES_VECTOR *b,
													 legacy_s16 orientation)
{
	if (!count) {
		return 0;
	}
	legacy_u16 written = 0;
	struct SHAPE3D_HIRES_VECTOR previous = points[count - 1U];
	legacy_f64 previous_side = frame_extended_precise_ground_side(a, b, &previous) * orientation;
	for (legacy_u16 vertex = 0; vertex < count; vertex++) {
		struct SHAPE3D_HIRES_VECTOR current = points[vertex];
		legacy_f64 side = frame_extended_precise_ground_side(a, b, &current) * orientation;
		if ((side >= 0) != (previous_side >= 0)) {
			legacy_f64 divisor = previous_side - side;
			struct SHAPE3D_HIRES_VECTOR crossing = {
				(((legacy_f64)current.x * previous_side - (legacy_f64)previous.x * side) / divisor),
				(((legacy_f64)current.y * previous_side - (legacy_f64)previous.y * side) / divisor),
				(((legacy_f64)current.z * previous_side - (legacy_f64)previous.z * side) /
				 divisor)};
			frame_extended_append_precise_vertex(output, &written, &crossing);
		}
		if (side >= 0) {
			frame_extended_append_precise_vertex(output, &written, &current);
		}
		previous = current;
		previous_side = side;
	}
	if (written > 1U && memcmp(&output[0], &output[written - 1U], sizeof(output[0])) == 0) {
		written--;
	}
	return written;
}

static legacy_s16
frame_extended_append_precise_fragments(struct FRAME_EXTENDED_PRECISE_GROUND_FRAGMENT **fragments,
										legacy_u16 *written, legacy_u16 *capacity,
										const struct SHAPE3D_HIRES_VECTOR *points, legacy_u16 count)
{
	if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST) {
		return 1;
	}
	if (count > SHAPE3D_POLYGON_MAX_VERTICES) {
		for (legacy_u16 vertex = 1; vertex + 1U < count; vertex++) {
			struct SHAPE3D_HIRES_VECTOR triangle[] = {points[0], points[vertex],
													  points[vertex + 1U]};
			if (!frame_extended_append_precise_fragments(fragments, written, capacity, triangle,
														 OWOOT_ROAD_TRIANGLE_VERTEX_COUNT)) {
				return 0;
			}
		}
		return 1;
	}
	legacy_f64 area = 0;
	for (legacy_u16 vertex = 1; vertex + 1U < count; vertex++) {
		area +=
			frame_extended_precise_ground_side(&points[0], &points[vertex], &points[vertex + 1U]);
	}
	if (area > -FRAME_EXTENDED_GROUND_AREA_EPSILON && area < FRAME_EXTENDED_GROUND_AREA_EPSILON) {
		return 1;
	}
	if (*written == *capacity) {
		if (*capacity == LEGACY_U16_MAX) {
			return 0;
		}
		legacy_u32 next_capacity = *capacity ? (legacy_u32)*capacity * 2U
											 : FRAME_EXTENDED_GROUND_FRAGMENT_INITIAL_CAPACITY;
		if (next_capacity > LEGACY_U16_MAX) {
			next_capacity = LEGACY_U16_MAX;
		}
		struct FRAME_EXTENDED_PRECISE_GROUND_FRAGMENT *grown =
			realloc(*fragments, next_capacity * sizeof(**fragments));
		if (!grown) {
			return 0;
		}
		*fragments = grown;
		*capacity = (legacy_u16)next_capacity;
	}
	(*fragments)[*written].count = count;
	memcpy((*fragments)[*written].points, points, count * sizeof(points[0]));
	(*written)++;
	return 1;
}

static legacy_s16
frame_extended_paint_precise_terrain_face(const struct VECTOR *source, legacy_u16 count,
										  const legacy_u8 *primitive,
										  const struct FRAME_EXTENDED_TERRAIN *terrain)
{
	struct SHAPE3D_HIRES_VECTOR points[SHAPE3D_POLYGON_MAX_VERTICES];
	for (legacy_u16 vertex = 0; vertex < count; vertex++) {
		points[vertex] =
			(struct SHAPE3D_HIRES_VECTOR){source[vertex].x, source[vertex].y, source[vertex].z};
	}
	struct FRAME_EXTENDED_PRECISE_GROUND_FRAGMENT *current = NULL, *next = NULL;
	legacy_u16 remaining = 0, current_capacity = 0, next_capacity = 0;
	if (!frame_extended_append_precise_fragments(&current, &remaining, &current_capacity, points,
												 count)) {
		free(current);
		return frame_extended_paint_precise_terrain(points, count, primitive[1], primitive[2],
													terrain->cell, &terrain->position,
													terrain->flags);
	}
	legacy_s16 complete = 1;
	for (legacy_u16 object_index = 0; object_index < terrain->object_count && complete;
		 object_index++) {
		const struct CSTRK_OBJECT *object = terrain->objects[object_index];
		if (!frame_extended_uses_grade(object)) {
			continue;
		}
		const struct TRACKOBJECT *definition = &trkObjectList[extended_track_object_id(object)];
		legacy_u16 triangle_count = track_road_triangle_count(definition->ss_physicalModel);
		for (legacy_u16 triangle_index = 0; triangle_index < triangle_count && remaining;
			 triangle_index++) {
			struct SHAPE3D_HIRES_VECTOR triangle[OWOOT_ROAD_TRIANGLE_VERTEX_COUNT];
			struct VECTOR authored[OWOOT_ROAD_TRIANGLE_VERTEX_COUNT];
			track_road_triangle(definition->ss_physicalModel, triangle_index, authored);
			for (legacy_u16 vertex = 0; vertex < OWOOT_ROAD_TRIANGLE_VERTEX_COUNT; vertex++) {
				frame_extended_rotate_vertex(&authored[vertex], definition->ss_rotY);
				frame_extended_join_object_grade_vertex(&authored[vertex], object);
				triangle[vertex] = (struct SHAPE3D_HIRES_VECTOR){
					authored[vertex].x, authored[vertex].y, authored[vertex].z};
			}
			if (triangle[0].y || triangle[1].y || triangle[2].y) {
				continue;
			}
			for (legacy_u16 vertex = 0; vertex < OWOOT_ROAD_TRIANGLE_VERTEX_COUNT; vertex++) {
				triangle[vertex].x +=
					((legacy_s16)object->x - terrain->column) * CSTRK_TILE_WORLD_SIZE +
					((legacy_s16)object->width - 1) * FRAME_EXTENDED_TERRAIN_HALF_TILE;
				triangle[vertex].z +=
					(terrain->row - (legacy_s16)object->y) * CSTRK_TILE_WORLD_SIZE -
					((legacy_s16)object->height - 1) * FRAME_EXTENDED_TERRAIN_HALF_TILE;
			}
			legacy_f64 winding =
				frame_extended_precise_ground_side(&triangle[0], &triangle[1], &triangle[2]);
			if (!winding) {
				continue;
			}
			legacy_s16 orientation = winding > 0 ? 1 : -1;
			legacy_u16 written = 0;
			for (legacy_u16 fragment = 0; fragment < remaining && complete; fragment++) {
				struct SHAPE3D_HIRES_VECTOR inside[FRAME_EXTENDED_GROUND_CLIPPED_VERTEX_MAX];
				legacy_u16 inside_count = current[fragment].count;
				memcpy(inside, current[fragment].points, inside_count * sizeof(inside[0]));
				for (legacy_u16 edge = 0; edge < OWOOT_ROAD_TRIANGLE_VERTEX_COUNT && inside_count;
					 edge++) {
					struct SHAPE3D_HIRES_VECTOR outside[FRAME_EXTENDED_GROUND_CLIPPED_VERTEX_MAX];
					struct SHAPE3D_HIRES_VECTOR clipped[FRAME_EXTENDED_GROUND_CLIPPED_VERTEX_MAX];
					const struct SHAPE3D_HIRES_VECTOR *a = &triangle[edge];
					const struct SHAPE3D_HIRES_VECTOR *b =
						&triangle[(edge + 1U) % OWOOT_ROAD_TRIANGLE_VERTEX_COUNT];
					legacy_u16 outside_count = frame_extended_clip_precise_ground(
						inside, inside_count, outside, a, b, -orientation);
					if (!frame_extended_append_precise_fragments(&next, &written, &next_capacity,
																 outside, outside_count)) {
						complete = 0;
						break;
					}
					inside_count = frame_extended_clip_precise_ground(inside, inside_count, clipped,
																	  a, b, orientation);
					memcpy(inside, clipped, inside_count * sizeof(inside[0]));
				}
			}
			if (!complete) {
				break;
			}
			struct FRAME_EXTENDED_PRECISE_GROUND_FRAGMENT *swap = current;
			current = next;
			next = swap;
			legacy_u16 swap_capacity = current_capacity;
			current_capacity = next_capacity;
			next_capacity = swap_capacity;
			remaining = written;
		}
	}
	legacy_s16 result = 0;
	if (complete) {
		for (legacy_u16 fragment = 0; fragment < remaining; fragment++) {
			if (frame_extended_paint_precise_terrain(
					current[fragment].points, current[fragment].count, primitive[1], primitive[2],
					terrain->cell, &terrain->position, terrain->flags)) {
				result = 1;
				break;
			}
		}
	} else {
		result =
			frame_extended_paint_precise_terrain(points, count, primitive[1], primitive[2],
												 terrain->cell, &terrain->position, terrain->flags);
	}
	free(current);
	free(next);
	return result;
}

#endif

/* Remove only ground-height driving faces. The same authored road triangles
 * define flat turns, branches, straight roads and stunt approaches; elevated
 * faces and scenery leave the supporting terrain intact. */
static legacy_s16 frame_extended_paint_terrain_face(const struct VECTOR *points, legacy_u16 count,
													const legacy_u8 *primitive,
													const struct FRAME_EXTENDED_TERRAIN *terrain)
{
#if defined(RESTUNTS_SDL3)
	if (supersight_enabled && terrain->graded) {
		return frame_extended_paint_precise_terrain_face(points, count, primitive, terrain);
	}
#endif
	if (!terrain->graded) {
		return frame_extended_paint_terrain_polygon(points, count, primitive[1], primitive[2],
													terrain->cell, &terrain->position,
													terrain->flags);
	}
	struct FRAME_EXTENDED_GROUND_FRAGMENT *current = NULL, *next = NULL;
	legacy_u16 remaining = 0, current_capacity = 0, next_capacity = 0;
	if (!frame_extended_append_ground_fragments(&current, &remaining, &current_capacity, points,
												count)) {
		free(current);
		return frame_extended_paint_terrain_polygon(points, count, primitive[1], primitive[2],
													terrain->cell, &terrain->position,
													terrain->flags);
	}
	legacy_s16 complete = 1;
	for (legacy_u16 object_index = 0; object_index < terrain->object_count && complete;
		 object_index++) {
		const struct CSTRK_OBJECT *object = terrain->objects[object_index];
		if (!frame_extended_uses_grade(object)) {
			continue;
		}
		const struct TRACKOBJECT *definition = &trkObjectList[extended_track_object_id(object)];
		legacy_u16 triangle_count = track_road_triangle_count(definition->ss_physicalModel);
		for (legacy_u16 triangle_index = 0; triangle_index < triangle_count && remaining;
			 triangle_index++) {
			struct VECTOR triangle[OWOOT_ROAD_TRIANGLE_VERTEX_COUNT];
			track_road_triangle(definition->ss_physicalModel, triangle_index, triangle);
			if (triangle[0].y || triangle[1].y || triangle[2].y) {
				continue;
			}
			for (legacy_u16 vertex = 0; vertex < OWOOT_ROAD_TRIANGLE_VERTEX_COUNT; vertex++) {
				frame_extended_rotate_vertex(&triangle[vertex], definition->ss_rotY);
				triangle[vertex].x +=
					((legacy_s16)object->x - terrain->column) * CSTRK_TILE_WORLD_SIZE +
					(object->width - 1U) * FRAME_EXTENDED_TERRAIN_HALF_TILE;
				triangle[vertex].z +=
					(terrain->row - (legacy_s16)object->y) * CSTRK_TILE_WORLD_SIZE -
					(object->height - 1U) * FRAME_EXTENDED_TERRAIN_HALF_TILE;
			}
			legacy_s64 winding =
				frame_extended_ground_side(&triangle[0], &triangle[1], &triangle[2]);
			if (!winding) {
				continue;
			}
			legacy_s16 orientation = winding > 0 ? 1 : -1;
			legacy_u16 written = 0;
			for (legacy_u16 fragment = 0; fragment < remaining && complete; fragment++) {
				struct VECTOR inside[FRAME_EXTENDED_GROUND_CLIPPED_VERTEX_MAX];
				legacy_u16 inside_count = current[fragment].count;
				memcpy(inside, current[fragment].points, inside_count * sizeof(inside[0]));
				for (legacy_u16 edge = 0; edge < OWOOT_ROAD_TRIANGLE_VERTEX_COUNT && inside_count;
					 edge++) {
					struct VECTOR outside[FRAME_EXTENDED_GROUND_CLIPPED_VERTEX_MAX];
					struct VECTOR clipped[FRAME_EXTENDED_GROUND_CLIPPED_VERTEX_MAX];
					const struct VECTOR *a = &triangle[edge];
					const struct VECTOR *b =
						&triangle[(edge + 1U) % OWOOT_ROAD_TRIANGLE_VERTEX_COUNT];
					legacy_u16 outside_count = frame_extended_clip_ground(
						inside, inside_count, outside, a, b, -orientation);
					if (!frame_extended_append_ground_fragments(&next, &written, &next_capacity,
																outside, outside_count)) {
						complete = 0;
						break;
					}
					inside_count = frame_extended_clip_ground(inside, inside_count, clipped, a, b,
															  orientation);
					memcpy(inside, clipped, inside_count * sizeof(inside[0]));
				}
			}
			if (!complete) {
				break;
			}
			struct FRAME_EXTENDED_GROUND_FRAGMENT *swap = current;
			current = next;
			next = swap;
			legacy_u16 swap_capacity = current_capacity;
			current_capacity = next_capacity;
			next_capacity = swap_capacity;
			remaining = written;
		}
	}
	legacy_s16 result = 0;
	if (complete) {
		for (legacy_u16 fragment = 0; fragment < remaining; fragment++) {
			if (frame_extended_paint_terrain_polygon(
					current[fragment].points, current[fragment].count, primitive[1], primitive[2],
					terrain->cell, &terrain->position, terrain->flags)) {
				result = 1;
				break;
			}
		}
	} else {
		result =
			frame_extended_paint_terrain_polygon(points, count, primitive[1], primitive[2],
												 terrain->cell, &terrain->position, terrain->flags);
	}
	free(current);
	free(next);
	return result;
}

static legacy_s16 frame_extended_draw_terrain_mesh(const struct FRAME_EXTENDED_TERRAIN *terrain,
												   legacy_s8 detail)
{
	const struct TRACKOBJECT *definition = &terrain_scene_objects[terrain->profile.terrain_id];
	const struct SHAPE3D *source =
		supersight_enabled != 0 ? frame_track_shape(definition, detail) : definition->ss_shapePtr;
	if (!terrain->graded && frame_extended_native_grass(terrain->cell)) {
		struct TRANSFORMEDSHAPE3D instance = {0};
		instance.pos = terrain->position;
		instance.shapeptr = (struct SHAPE3D *)source;
		instance.rectptr = terrain->profile.base_height == 0 ? &frame_unsorted_shapes_rect
															 : &frame_sorted_shapes_rect;
		instance.rotvec.z = definition->ss_rotY;
		instance.culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
		instance.ts_flags = terrain->flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT;
		return (legacy_s16)frame_extended_queue_hill_shape(&instance, terrain->edges) > 0 &&
			   supersight_enabled != 0;
	}
	const legacy_u8 *primitive = source->shape3d_primitives;
	for (legacy_u16 index = 0; index < source->shape3d_numprimitives; index++) {
		legacy_u16 count = primitive[0];
		if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST || count > SHAPE3D_PRIMITIVE_POLYGON_LAST) {
			break;
		}
		struct VECTOR points[SHAPE3D_POLYGON_MAX_VERTICES];
		for (legacy_u16 vertex = 0; vertex < count; vertex++) {
			shape3d_vertex_read(source, primitive[2U + source->shape3d_numpaints + vertex],
								&points[vertex]);
			frame_extended_rotate_vertex(&points[vertex], definition->ss_rotY);
			frame_extended_join_hill_vertex(&points[vertex], terrain->edges);
		}
		if (frame_extended_paint_terrain_face(points, count, primitive, terrain)) {
			return 1;
		}
		primitive += 2U + source->shape3d_numpaints + count;
	}
	return 0;
}

/* Original hill meshes retain their full tile span, corner diagonals, and
 * authored shading. Higher levels translate those meshes without tilting them. */
legacy_s16 extended_frame_draw_terrain(const struct FRAME_TILE *tile,
									   const struct FRAME_CAMERA *camera, legacy_s8 flags)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	struct FRAME_EXTENDED_TERRAIN terrain;
	if (!cstrk_terrain_profile(track, (legacy_u16)tile->east, (legacy_u16)tile->south,
							   &terrain.profile)) {
		return 0;
	}
	terrain.cell = &track->cells[(size_t)tile->south * track->width + tile->east];
	terrain.edges = frame_extended_hill_edges(track, tile->east, tile->south);
	terrain.position = (struct VECTOR){
		LEGACY_S16_WRAP_SUB(track_column_centers[tile->east], camera->position.x),
		LEGACY_S16_WRAP_SUB((legacy_s16)(terrain.profile.base_height * CSTRK_LEVEL_WORLD_HEIGHT),
							camera->position.y),
		LEGACY_S16_WRAP_SUB(track_row_centers[tile->south], camera->position.z)};
	terrain.column = tile->east;
	terrain.row = tile->south;
	terrain.object = extended_track_object_at(tile->east, tile->south);
	terrain.graded = 0;
	frame_extended_collect_terrain_objects(&terrain);
	for (legacy_u16 candidate = 0; candidate < terrain.object_count; candidate++) {
		if (frame_extended_uses_grade(terrain.objects[candidate])) {
			terrain.graded = 1;
			terrain.edges |= frame_extended_grade_edges(terrain.objects[candidate]);
		}
	}
	terrain.flags = flags;
	if (frame_extended_object_sloped(terrain.object) &&
		(!terrain.graded || (supersight_enabled && !frame_extended_uses_grade(terrain.object))) &&
		frame_extended_native_grass(terrain.cell)) {
		/* Native hill roads and spans already include their supporting grass. */
		return 0;
	}
	if (frame_extended_draw_supporting_ground(&terrain)) {
		return 1;
	}
	if (terrain.profile.terrain_id == 0) {
		return 0;
	}
	return frame_extended_draw_terrain_mesh(&terrain, tile->detail);
}

/* Hill-road resources contain their original grass embankments. The terrain
 * pass supplies that same native surface with this track's independent material
 * and Shore paint, so retaining these faces would paint grass over dirt or ice. */
static void frame_extended_remove_hill_grass(struct SHAPE3D *shape)
{
	legacy_u8 *read = shape->shape3d_primitives, *write = read;
	legacy_u16 kept = 0;
	for (legacy_u16 index = 0; index < shape->shape3d_numprimitives; index++) {
		legacy_u16 size = 2U + shape->shape3d_numpaints + read[0];
		legacy_u8 material = read[2];
		if (material < FRAME_EXTENDED_HILL_GRASS_MATERIAL ||
			material > FRAME_EXTENDED_HILL_GRASS_MATERIAL_LAST) {
			memmove(write, read, size);
			memmove(shape->shape3d_visibility_masks + kept * SHAPE3D_VISIBILITY_MASK_SIZE,
					shape->shape3d_visibility_masks + index * SHAPE3D_VISIBILITY_MASK_SIZE,
					SHAPE3D_VISIBILITY_MASK_SIZE);
			memmove(shape->shape3d_front_facing_masks + kept * SHAPE3D_VISIBILITY_MASK_SIZE,
					shape->shape3d_front_facing_masks + index * SHAPE3D_VISIBILITY_MASK_SIZE,
					SHAPE3D_VISIBILITY_MASK_SIZE);
			write += size;
			kept++;
		}
		read += size;
	}
	write[0] = 0;
	write[1] = 0;
	shape->shape3d_numprimitives = kept;
}

struct FRAME_EXTENDED_OBJECT {
	const struct CSTRK_OBJECT *object;
	const struct TRACKOBJECT *definition;
	struct VECTOR position;
	legacy_u8 native_grass, graded, edges;
	legacy_s8 detail, flags;
};

static struct TRANSFORMEDSHAPE3D
frame_extended_object_instance(const struct FRAME_EXTENDED_OBJECT *object, struct SHAPE3D *shape)
{
	struct TRANSFORMEDSHAPE3D instance = {0};
	instance.pos = object->position;
	instance.shapeptr = shape;
	instance.rectptr = &frame_sorted_shapes_rect;
	instance.culling_distance = FRAME_SINGLE_TILE_TRANSFORM_DISTANCE;
	instance.ts_flags = object->flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
	return instance;
}

static void frame_extended_apply_object_grade(struct SHAPE3D *mesh,
											  const struct CSTRK_OBJECT *object)
{
	/* Raise the structure and overlay along the incline, retaining their
	 * authored height above the road. */
	for (legacy_u16 vertex = 0; vertex < mesh->shape3d_numverts; vertex++) {
		struct VECTOR point;
		shape3d_vertex_read(mesh, vertex, &point);
		frame_extended_join_object_grade_vertex(&point, object);
		point.y =
			LEGACY_S16_WRAP_ADD(point.y, frame_extended_object_grade(object, point.x, point.z));
		shape3d_vertex_write(mesh, vertex, &point);
	}
}

#if defined(RESTUNTS_SDL3)
static legacy_u16 frame_extended_queue_precise_object(struct TRANSFORMEDSHAPE3D *instance,
													  const struct CSTRK_OBJECT *object)
{
	const struct SHAPE3D *mesh = instance->shapeptr;
	if (mesh->shape3d_numverts > LEGACY_U8_MAX) {
		return shape3d_transform_and_queue(instance);
	}
	struct SHAPE3D_HIRES_VECTOR vertices[LEGACY_U8_MAX];
	for (legacy_u16 vertex = 0; vertex < mesh->shape3d_numverts; vertex++) {
		struct VECTOR point;
		shape3d_vertex_read(mesh, vertex, &point);
		legacy_f64 x = object->width * FRAME_EXTENDED_TERRAIN_HALF_TILE + point.x;
		legacy_f64 y = object->height * FRAME_EXTENDED_TERRAIN_HALF_TILE - point.z;
		if (object->slope_x < 0) {
			x -= object->width * CSTRK_TILE_WORLD_SIZE;
		}
		if (object->slope_y < 0) {
			y -= object->height * CSTRK_TILE_WORLD_SIZE;
		}
		legacy_f64 grade = (object->slope_x * x + object->slope_y * y) * CSTRK_LEVEL_WORLD_HEIGHT /
						   CSTRK_TILE_WORLD_SIZE;
		/* The packed fallback retains the original rounded grade. Restore the
		 * authored height before applying the exact affine grade for rendering. */
		vertices[vertex] = (struct SHAPE3D_HIRES_VECTOR){
			point.x, point.y - frame_extended_object_grade(object, point.x, point.z) + grade,
			point.z};
	}
	return shape3d_hires_transform_and_queue(instance, vertices);
}
#endif

static legacy_s16 frame_extended_draw_object_mesh(const struct FRAME_EXTENDED_OBJECT *object,
												  const struct TRACKOBJECT *definition,
												  const struct SHAPE3D *source, legacy_u8 surface)
{
	struct SHAPE3D mesh;
	if (!extended_geometry_transform_shape(source, &mesh, definition->ss_rotY, surface,
										   object->definition->ss_physicalModel)) {
		return 0;
	}
	if (object->graded) {
		frame_extended_apply_object_grade(&mesh, object->object);
	} else if (frame_extended_object_sloped(object->object) && !object->native_grass) {
		frame_extended_remove_hill_grass(&mesh);
	}
	if (!mesh.shape3d_numprimitives) {
		extended_geometry_free_shape(&mesh);
		return 0;
	}
	struct TRANSFORMEDSHAPE3D instance = frame_extended_object_instance(object, &mesh);
	legacy_s16 result;
#if defined(RESTUNTS_SDL3)
	if (supersight_enabled && object->graded) {
		result = (legacy_s16)frame_extended_queue_precise_object(&instance, object->object);
	} else
#endif
	{
		result = frame_extended_queue_hill_shape(&instance, object->edges);
	}
	extended_geometry_free_shape(&mesh);
	return result > 0 && supersight_enabled != 0;
}

static legacy_s16 frame_extended_draw_object_part(const struct FRAME_EXTENDED_OBJECT *object,
												  const struct TRACKOBJECT *definition,
												  legacy_u8 overlay)
{
	legacy_u8 surface = overlay && object->definition->ss_physicalModel == PHYSICAL_MODEL_OVERPASS
							? object->object->lower_surface
							: object->object->surface;
	const struct SHAPE3D *source = frame_track_shape(definition, object->detail);
	if (frame_extended_object_sloped(object->object) && !object->graded && object->native_grass &&
		(overlay || surface < source->shape3d_numpaints)) {
		struct TRANSFORMEDSHAPE3D instance =
			frame_extended_object_instance(object, (struct SHAPE3D *)source);
		instance.rotvec.z = definition->ss_rotY;
		instance.ts_flags |= definition->ss_ignoreZBias;
		instance.material = overlay ? 0 : surface;
		return (legacy_s16)frame_extended_queue_hill_shape(&instance, object->edges) > 0 &&
			   supersight_enabled != 0;
	}
	return frame_extended_draw_object_mesh(object, definition, source, surface);
}

static legacy_s16 frame_extended_draw_object(const struct CSTRK_TRACK *track,
											 const struct CSTRK_OBJECT *model,
											 const struct TRACKOBJECT *definition,
											 const struct FRAME_TILE *tile,
											 const struct FRAME_CAMERA *camera, legacy_s8 flags)
{
	struct FRAME_EXTENDED_OBJECT object;
	object.object = model;
	object.definition = definition;
	const struct CSTRK_CELL *cell = &track->cells[(size_t)model->y * track->width + model->x];
	object.native_grass = frame_extended_native_grass(cell);
	object.graded = frame_extended_uses_grade(model);
	struct VECTOR origin;
	extended_geometry_object_origin(track, model, &origin);
	object.position = (struct VECTOR){LEGACY_S16_WRAP_SUB(origin.x, camera->position.x),
									  LEGACY_S16_WRAP_SUB(origin.y, camera->position.y),
									  LEGACY_S16_WRAP_SUB(origin.z, camera->position.z)};
	object.edges = frame_extended_object_sloped(model) && !object.graded
					   ? frame_extended_hill_edges(track, model->x, model->y)
					   : 0;
	object.detail = tile->detail;
	object.flags = flags;
	legacy_u8 hill_overlay =
		frame_extended_object_sloped(model) && !object.graded && definition->ss_ssOvelay;
	for (legacy_u8 part = 0; part < FRAME_EXTENDED_OBJECT_PART_COUNT; part++) {
		legacy_u8 overlay = hill_overlay ? part == 0 : part;
		const struct TRACKOBJECT *render_definition = definition;
		if (overlay) {
			if (!definition->ss_ssOvelay) {
				break;
			}
			render_definition = frame_track_object_from_legacy_index(definition->ss_ssOvelay);
		}
		if (frame_extended_draw_object_part(&object, render_definition, overlay)) {
			return 1;
		}
	}
	return 0;
}

legacy_s16 extended_frame_draw_objects(struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
									   legacy_s8 flags)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	tile->last_east = tile->east;
	tile->last_south = tile->south;
	for (legacy_u32 index = 0; index < track->object_count; index++) {
		const struct CSTRK_OBJECT *object = &track->objects[index];
		if (object->x != tile->east || object->y != tile->south) {
			continue;
		}
		legacy_u32 id = extended_track_object_id(object);
		if (id >= TRACK_OBJECT_COUNT) {
			continue;
		}
		if (frame_extended_draw_object(track, object, &trkObjectList[id], tile, camera, flags)) {
			return 1;
		}
	}
	return 0;
}

legacy_s16 extended_frame_add_start_flag(const struct FRAME_CAMERA *camera,
										 legacy_s8 redraw_transform_flags)
{
	if (!extended_track_active()) {
		return 0;
	}

	const struct CSTRK_OBJECT *object =
		extended_track_object_at(start_finish_column, start_finish_row);
	if (!object) {
		return 1;
	}
	struct VECTOR origin, offset;
	extended_geometry_object_origin(extended_track_model(), object, &origin);
	offset = (struct VECTOR){
		LEGACY_S16_WRAP_ADD(
			multiply_and_scale(sin_fast(track_angle + ANGLE_QUARTER_TURN), FRAME_START_FLAG_RADIUS),
			multiply_and_scale(sin_fast(track_angle + ANGLE_HALF_TURN),
							   FRAME_START_FLAG_FAR_OFFSET)),
		0,
		LEGACY_S16_WRAP_ADD(
			multiply_and_scale(cos_fast(track_angle + ANGLE_QUARTER_TURN), FRAME_START_FLAG_RADIUS),
			multiply_and_scale(cos_fast(track_angle + ANGLE_HALF_TURN),
							   FRAME_START_FLAG_FAR_OFFSET))};
	legacy_u16 paint = (legacy_u16)start_flag_animation >> FRAME_START_FLAG_ANIMATION_SHIFT;
	if (paint > FRAME_START_FLAG_MAX_MATERIAL) {
		paint = FRAME_START_FLAG_MAX_MATERIAL;
	}
	struct SHAPE3D mesh;
	if (extended_geometry_transform_shape(&game3dshapes[FRAME_START_FLAG_SHAPE_INDEX], &mesh,
										  track_angle, paint, -1)) {
		struct TRANSFORMEDSHAPE3D instance = {0};
		instance.pos = (struct VECTOR){
			LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(origin.x, offset.x), camera->position.x),
			LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(origin.y, offset.y), camera->position.y),
			LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(origin.z, offset.z), camera->position.z)};
		instance.shapeptr = &mesh;
		instance.rectptr = &frame_sorted_shapes_rect;
		instance.culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
		instance.ts_flags = redraw_transform_flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
		shape3d_transform_and_queue(&instance);
		extended_geometry_free_shape(&mesh);
	}
	return 1;
}

legacy_s16 extended_frame_car_visible(const struct CARSTATE *carstate)
{
	if (!extended_track_active()) {
		return 1;
	}
	legacy_s32 reference_x, reference_z;
	extended_geometry_get_context(&reference_x, &reference_z);
	legacy_s32 dx =
		LEGACY_S32_SAR(carstate->car_position.lx, FRAME_EXTENDED_POSITION_SHIFT) - reference_x;
	legacy_s32 dz =
		LEGACY_S32_SAR(carstate->car_position.lz, FRAME_EXTENDED_POSITION_SHIFT) - reference_z;
	return dx >= -FRAME_EXTENDED_GEOMETRY_MAX_DISTANCE &&
		   dx <= FRAME_EXTENDED_GEOMETRY_MAX_DISTANCE &&
		   dz >= -FRAME_EXTENDED_GEOMETRY_MAX_DISTANCE &&
		   dz <= FRAME_EXTENDED_GEOMETRY_MAX_DISTANCE;
}

legacy_s16 extended_frame_background_height(legacy_s16 height)
{
	return extended_track_active() ? 0 : height;
}

void extended_frame_position_camera_tiles(struct FRAME_TILE_SELECTION *tiles,
										  const struct FRAME_CAMERA *camera)
{
	legacy_s32 x, z;
	extended_geometry_world_position(&camera->position, &x, &z);
	tiles->camera_east = (legacy_s8)LEGACY_S32_SAR(x, FRAME_CAMERA_TILE_SHIFT);
	tiles->camera_south =
		(legacy_s8)(extended_track_height() - 1 - LEGACY_S32_SAR(z, FRAME_CAMERA_TILE_SHIFT));
}

void extended_frame_set_context(const struct CARSTATE *viewed)
{
	if (extended_track_active()) {
		extended_geometry_set_context(viewed->car_position.lx, viewed->car_position.lz);
	}
}

legacy_s16 extended_frame_first_tile_pass(void)
{
	return supersight_enabled != 0 && extended_track_active() ? FRAME_TILE_GROUND_PASS
															  : FRAME_TILE_OBJECT_PASS;
}

legacy_u8 extended_frame_should_draw_terrain(const struct FRAME_TILE *tile, legacy_s16 first_pass,
											 legacy_s16 pass)
{
	struct CSTRK_TERRAIN_PROFILE ground_profile;
	legacy_u8 ground_first = first_pass == FRAME_TILE_GROUND_PASS &&
							 cstrk_terrain_profile(extended_track_model(), (legacy_u16)tile->east,
												   (legacy_u16)tile->south, &ground_profile) &&
							 ground_profile.terrain_id < CSTRK_TERRAIN_PROFILE_NORTH;
	return (pass == FRAME_TILE_GROUND_PASS) == ground_first;
}

#ifndef RESTUNTS_HEADLESS
legacy_s16 extended_frame_format_elapsed_time(legacy_s8 *out)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	extended_replay_format_runtime_time(out, extended_replay_clocks.input_frame, 0);
	return 1;
}
#endif
