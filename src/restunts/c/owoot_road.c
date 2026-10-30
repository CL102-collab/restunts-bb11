#include "owoot_road.h"
#include "externs.h"
#include "track_objects.h"
#include "track_collision.h"
#include "trackdata_layout.h"
#include "extended_track.h"
#ifdef RESTUNTS_SDL3
#include "extended_geometry.h"
#endif

#define OWOOT_ROAD_CONTACT_TOLERANCE 12
#define OWOOT_PROJECTION_SCALE 256L
#define OWOOT_CLIPPED_VERTEX_MAX (OWOOT_WHEEL_RING_MAX * 5U)
#define OWOOT_HILL_ROAD_MODEL 36U
#define OWOOT_ROAD_TILE_OVERHANG 8
#define OWOOT_SLALOM_BARRIER_VERTEX_COUNT 4U
#define OWOOT_ROAD_TRIANGLE_VERTEX_COUNT 3U

struct OWOOT_ROAD_TRIANGLE {
	struct VECTOR vertex[3];
};

struct OWOOT_ROAD_MODEL {
	legacy_u16 first_triangle;
	legacy_u16 triangle_count;
};

struct OWOOT_PROJECTED_POINT {
	legacy_s32 x;
	legacy_s32 z;
};

/* Products in the height clipping step exceed 32 bits even though every
 * resource/world coordinate is a signed word. Watcom implements these C99
 * integer operations on 8086 as well as native host compilers. */

#include "owoot_road_data.h"

static legacy_s64 road_cross(const struct OWOOT_PROJECTED_POINT *a,
							 const struct OWOOT_PROJECTED_POINT *b,
							 const struct OWOOT_PROJECTED_POINT *c)
{
	return (legacy_s64)(b->x - a->x) * (c->z - a->z) - (legacy_s64)(b->z - a->z) * (c->x - a->x);
}

legacy_s16 track_road_contains_local_point(legacy_s16 physical_model, const struct VECTOR *position)
{
	if (physical_model < 0 ||
		(legacy_u16)physical_model >= sizeof(owoot_road_models) / sizeof(owoot_road_models[0])) {
		return 0;
	}
	const struct OWOOT_ROAD_MODEL *geometry = &owoot_road_models[physical_model];
	struct OWOOT_PROJECTED_POINT point;
	point.x = position->x;
	point.z = position->z;
	for (legacy_u16 index = 0; index < geometry->triangle_count; index++) {
		const struct OWOOT_ROAD_TRIANGLE far *triangle =
			&owoot_road_triangles[geometry->first_triangle + index];
		struct OWOOT_PROJECTED_POINT projected[OWOOT_ROAD_TRIANGLE_VERTEX_COUNT];
		for (legacy_u16 vertex = 0; vertex < OWOOT_ROAD_TRIANGLE_VERTEX_COUNT; vertex++) {
			projected[vertex].x = triangle->vertex[vertex].x;
			projected[vertex].z = triangle->vertex[vertex].z;
		}
		if (road_cross(&projected[0], &projected[1], &projected[2]) == 0) {
			continue;
		}
		legacy_s16 negative = 0;
		legacy_s16 positive = 0;
		for (legacy_u16 vertex = 0; vertex < OWOOT_ROAD_TRIANGLE_VERTEX_COUNT; vertex++) {
			legacy_s64 side =
				road_cross(&projected[vertex],
						   &projected[(vertex + 1U) % OWOOT_ROAD_TRIANGLE_VERTEX_COUNT], &point);
			negative |= side < 0;
			positive |= side > 0;
		}
		if (!(negative && positive)) {
			return 1;
		}
	}
	return 0;
}

static legacy_s16 point_precedes(const struct OWOOT_PROJECTED_POINT *a,
								 const struct OWOOT_PROJECTED_POINT *b)
{
	return a->x < b->x || (a->x == b->x && a->z < b->z);
}

static legacy_u16 road_projected_hull(struct OWOOT_PROJECTED_POINT *points, legacy_u16 count,
									  struct OWOOT_PROJECTED_POINT *hull)
{
	for (legacy_u16 i = 1; i < count; i++) {
		struct OWOOT_PROJECTED_POINT point = points[i];
		legacy_u16 j = i;
		while (j > 0 && point_precedes(&point, &points[j - 1])) {
			points[j] = points[j - 1];
			j--;
		}
		points[j] = point;
	}
	legacy_u16 unique = 0;
	for (legacy_u16 i = 0; i < count; i++) {
		if (unique == 0 || points[i].x != points[unique - 1].x ||
			points[i].z != points[unique - 1].z) {
			points[unique++] = points[i];
		}
	}
	if (unique < 2) {
		if (unique != 0) {
			hull[0] = points[0];
		}
		return unique;
	}
	legacy_u16 size = 0;
	for (legacy_u16 i = 0; i < unique; i++) {
		while (size >= 2 && road_cross(&hull[size - 2], &hull[size - 1], &points[i]) <= 0) {
			size--;
		}
		hull[size++] = points[i];
	}
	legacy_u16 lower_size = size;
	for (legacy_s16 i = (legacy_s16)unique - 2; i >= 0; i--) {
		while (size > lower_size && road_cross(&hull[size - 2], &hull[size - 1], &points[i]) <= 0) {
			size--;
		}
		hull[size++] = points[i];
	}
	return size - 1;
}

static legacy_s16 road_separating_edges(const struct OWOOT_PROJECTED_POINT *first,
										legacy_u16 first_count,
										const struct OWOOT_PROJECTED_POINT *second,
										legacy_u16 second_count, legacy_s16 boundary_overlap)
{
	for (legacy_u16 edge = 0; edge < first_count; edge++) {
		const struct OWOOT_PROJECTED_POINT *a = &first[edge];
		const struct OWOOT_PROJECTED_POINT *b = &first[(edge + 1) % first_count];
		if (a->x == b->x && a->z == b->z) {
			continue;
		}
		legacy_s64 first_min = 0;
		legacy_s64 first_max = 0;
		for (legacy_u16 i = 0; i < first_count; i++) {
			legacy_s64 side = road_cross(a, b, &first[i]);
			if (side < first_min) {
				first_min = side;
			}
			if (side > first_max) {
				first_max = side;
			}
		}
		legacy_s64 second_min = road_cross(a, b, &second[0]);
		legacy_s64 second_max = second_min;
		for (legacy_u16 i = 1; i < second_count; i++) {
			legacy_s64 side = road_cross(a, b, &second[i]);
			if (side < second_min) {
				second_min = side;
			}
			if (side > second_max) {
				second_max = side;
			}
		}
		if (boundary_overlap ? first_max < second_min || second_max < first_min
							 : first_max <= second_min || second_max <= first_min) {
			return 1;
		}
	}
	return 0;
}

static legacy_s16 road_projected_overlap(const struct OWOOT_PROJECTED_POINT *road,
										 const struct OWOOT_PROJECTED_POINT *wheel,
										 legacy_u16 wheel_count)
{
	if (wheel_count == 0) {
		return 0;
	}
	legacy_s32 road_min_x = road[0].x;
	legacy_s32 road_max_x = road[0].x;
	legacy_s32 road_min_z = road[0].z;
	legacy_s32 road_max_z = road[0].z;
	for (legacy_u16 i = 1; i < 3; i++) {
		if (road[i].x < road_min_x) {
			road_min_x = road[i].x;
		}
		if (road[i].x > road_max_x) {
			road_max_x = road[i].x;
		}
		if (road[i].z < road_min_z) {
			road_min_z = road[i].z;
		}
		if (road[i].z > road_max_z) {
			road_max_z = road[i].z;
		}
	}
	legacy_s32 wheel_min_x = wheel[0].x;
	legacy_s32 wheel_max_x = wheel[0].x;
	legacy_s32 wheel_min_z = wheel[0].z;
	legacy_s32 wheel_max_z = wheel[0].z;
	for (legacy_u16 i = 1; i < wheel_count; i++) {
		if (wheel[i].x < wheel_min_x) {
			wheel_min_x = wheel[i].x;
		}
		if (wheel[i].x > wheel_max_x) {
			wheel_max_x = wheel[i].x;
		}
		if (wheel[i].z < wheel_min_z) {
			wheel_min_z = wheel[i].z;
		}
		if (wheel[i].z > wheel_max_z) {
			wheel_max_z = wheel[i].z;
		}
	}
	if (wheel_max_x < road_min_x || road_max_x < wheel_min_x || wheel_max_z < road_min_z ||
		road_max_z < wheel_min_z) {
		return 0;
	}
	return !road_separating_edges(road, 3, wheel, wheel_count, 1) &&
		   !road_separating_edges(wheel, wheel_count, road, 3, 1);
}

static legacy_s16 road_aperture_overlaps_wheel(const struct OWOOT_PROJECTED_POINT *aperture,
											   legacy_u16 aperture_count,
											   const struct VECTOR *vertices, legacy_u16 count)
{
	if (count == 0 || count > OWOOT_WHEEL_VERTEX_MAX) {
		return 0;
	}
	struct OWOOT_PROJECTED_POINT points[OWOOT_WHEEL_VERTEX_MAX];
	struct OWOOT_PROJECTED_POINT hull[OWOOT_WHEEL_VERTEX_MAX * 2U];
	for (legacy_u16 index = 0; index < count; index++) {
		points[index].x = vertices[index].x;
		points[index].z = vertices[index].y;
	}
	legacy_u16 hull_count = road_projected_hull(points, count, hull);
	return !road_separating_edges(aperture, aperture_count, hull, hull_count, 0) &&
		   !road_separating_edges(hull, hull_count, aperture, aperture_count, 0);
}

/* These are the inner rim vertices of GAME1's pipe + pip2 models, also
 * shared by the half-pipe. A rectangle admits paths outside the rounded
 * upper corners. Requiring interior overlap also excludes riding on the roof. */
legacy_s16 track_pipe_aperture_overlaps_wheel(const struct VECTOR *vertices, legacy_u16 count)
{
	static const struct OWOOT_PROJECTED_POINT aperture[] = {
		{-31, 0},  {31, 0},	   {84, 35},   {115, 88},	{115, 151}, {84, 204},
		{31, 235}, {-31, 235}, {-84, 204}, {-115, 151}, {-115, 88}, {-84, 35}};
	return road_aperture_overlaps_wheel(aperture, sizeof(aperture) / sizeof(aperture[0]), vertices,
										count);
}

legacy_s16 track_tunnel_aperture_overlaps_wheel(const struct VECTOR *vertices, legacy_u16 count)
{
	/* The tunn model's opening is 240 wide and 144 high. Both polygon edge
	 * crossings and enclosure count, but touching only its outside does not. */
	static const struct OWOOT_PROJECTED_POINT aperture[] = {{-ROAD_HALF_WIDTH, 0},
															{ROAD_HALF_WIDTH, 0},
															{ROAD_HALF_WIDTH, TUNNEL_HEIGHT},
															{-ROAD_HALF_WIDTH, TUNNEL_HEIGHT}};
	return road_aperture_overlaps_wheel(aperture, sizeof(aperture) / sizeof(aperture[0]), vertices,
										count);
}

static struct OWOOT_PROJECTED_POINT road_local_projection(legacy_s32 x, legacy_s32 z,
														  legacy_s16 rotation)
{
	struct OWOOT_PROJECTED_POINT point;
	point.x = x;
	point.z = z;
	switch ((legacy_u16)rotation) {
		case ANGLE_QUARTER_TURN:
			point.x = -z;
			point.z = x;
			break;
		case ANGLE_HALF_TURN:
			point.x = -x;
			point.z = -z;
			break;
		case ANGLE_THREE_QUARTER_TURN:
			point.x = z;
			point.z = -x;
			break;
	}
	return point;
}

/* The swept hull is the current convex envelope plus the translation segment.
 * Projecting that segment onto each separating axis avoids doubling the tire
 * vertex buffers on the small DOS stack. */
static legacy_s16 road_swept_axis_separates(const struct OWOOT_PROJECTED_POINT *a,
											const struct OWOOT_PROJECTED_POINT *b,
											const struct OWOOT_PROJECTED_POINT *barrier,
											const struct OWOOT_PROJECTED_POINT *hull,
											legacy_u16 count,
											const struct OWOOT_PROJECTED_POINT *motion)
{
	if (a->x == b->x && a->z == b->z) {
		return 0;
	}
	legacy_s64 barrier_min = road_cross(a, b, &barrier[0]);
	legacy_s64 barrier_max = barrier_min;
	for (legacy_u16 index = 1; index < OWOOT_SLALOM_BARRIER_VERTEX_COUNT; index++) {
		legacy_s64 value = road_cross(a, b, &barrier[index]);
		if (value < barrier_min) {
			barrier_min = value;
		}
		if (value > barrier_max) {
			barrier_max = value;
		}
	}
	legacy_s64 tire_min = road_cross(a, b, &hull[0]);
	legacy_s64 tire_max = tire_min;
	for (legacy_u16 index = 1; index < count; index++) {
		legacy_s64 value = road_cross(a, b, &hull[index]);
		if (value < tire_min) {
			tire_min = value;
		}
		if (value > tire_max) {
			tire_max = value;
		}
	}
	legacy_s64 sweep =
		(legacy_s64)(b->x - a->x) * motion->z - (legacy_s64)(b->z - a->z) * motion->x;
	if (sweep > 0) {
		tire_max += sweep;
	} else {
		tire_min += sweep;
	}
	return tire_max <= barrier_min || barrier_max <= tire_min;
}

static legacy_s16 road_swept_barrier_overlap(const struct OWOOT_PROJECTED_POINT *barrier,
											 const struct OWOOT_PROJECTED_POINT *hull,
											 legacy_u16 count,
											 const struct OWOOT_PROJECTED_POINT *motion)
{
	for (legacy_u16 edge = 0; edge < OWOOT_SLALOM_BARRIER_VERTEX_COUNT; edge++) {
		if (road_swept_axis_separates(&barrier[edge],
									  &barrier[(edge + 1U) % OWOOT_SLALOM_BARRIER_VERTEX_COUNT],
									  barrier, hull, count, motion)) {
			return 0;
		}
	}
	for (legacy_u16 edge = 0; edge < count; edge++) {
		if (road_swept_axis_separates(&hull[edge], &hull[(edge + 1U) % count], barrier, hull, count,
									  motion)) {
			return 0;
		}
	}
	struct OWOOT_PROJECTED_POINT origin = {0, 0};
	return !road_swept_axis_separates(&origin, motion, barrier, hull, count, motion);
}

legacy_s16 track_slalom_wheel_envelope_crosses_barrier(
	const struct VECTOR vertices[CARSTATE_WHEEL_COUNT][OWOOT_WHEEL_VERTEX_MAX],
	const legacy_u16 counts[CARSTATE_WHEEL_COUNT], const struct VECTOR body[CARSTATE_WHEEL_COUNT],
	const struct VECTOR *center, legacy_s16 rotation, const struct VECTOR *motion)
{
	/* Match the two physical slalom boxes in trackobj.c. Combine tires with
	 * the normal body-plane contact corners; no preferred opponent lane is
	 * imposed. Ignoring barrier height prevents jumping over the obstacles. */
	static const struct OWOOT_PROJECTED_POINT barriers[][OWOOT_SLALOM_BARRIER_VERTEX_COUNT] = {
		{{SLALOM_POLE_INNER_X, -SLALOM_POLE_FAR_Z},
		 {SLALOM_POLE_OUTER_X, -SLALOM_POLE_FAR_Z},
		 {SLALOM_POLE_OUTER_X, -SLALOM_POLE_NEAR_Z},
		 {SLALOM_POLE_INNER_X, -SLALOM_POLE_NEAR_Z}},
		{{-SLALOM_POLE_OUTER_X, SLALOM_POLE_NEAR_Z},
		 {-SLALOM_POLE_INNER_X, SLALOM_POLE_NEAR_Z},
		 {-SLALOM_POLE_INNER_X, SLALOM_POLE_FAR_Z},
		 {-SLALOM_POLE_OUTER_X, SLALOM_POLE_FAR_Z}}};
	struct OWOOT_PROJECTED_POINT points[CARSTATE_WHEEL_COUNT * (OWOOT_WHEEL_VERTEX_MAX + 1U)];
	legacy_u16 count = 0;
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		if (counts[wheel] > OWOOT_WHEEL_VERTEX_MAX) {
			return 0;
		}
		for (legacy_u16 index = 0; index < counts[wheel]; index++) {
			points[count++] =
				road_local_projection((legacy_s32)vertices[wheel][index].x - center->x,
									  (legacy_s32)vertices[wheel][index].z - center->z, rotation);
		}
	}
	if (body != 0) {
		for (legacy_u16 corner = 0; corner < CARSTATE_WHEEL_COUNT; corner++) {
			points[count++] =
				road_local_projection((legacy_s32)body[corner].x - center->x,
									  (legacy_s32)body[corner].z - center->z, rotation);
		}
	}
	if (count == 0) {
		return 0;
	}
	struct OWOOT_PROJECTED_POINT sweep =
		road_local_projection(-(legacy_s32)motion->x, -(legacy_s32)motion->z, rotation);
	legacy_s32 min_x = points[0].x, max_x = points[0].x;
	legacy_s32 min_z = points[0].z, max_z = points[0].z;
	for (legacy_u16 index = 1; index < count; index++) {
		if (points[index].x < min_x) {
			min_x = points[index].x;
		}
		if (points[index].x > max_x) {
			max_x = points[index].x;
		}
		if (points[index].z < min_z) {
			min_z = points[index].z;
		}
		if (points[index].z > max_z) {
			max_z = points[index].z;
		}
	}
	min_x += sweep.x < 0 ? sweep.x : 0;
	max_x += sweep.x > 0 ? sweep.x : 0;
	min_z += sweep.z < 0 ? sweep.z : 0;
	max_z += sweep.z > 0 ? sweep.z : 0;
	legacy_s16 first = max_x > SLALOM_POLE_INNER_X && min_x < SLALOM_POLE_OUTER_X &&
					   max_z > -SLALOM_POLE_FAR_Z && min_z < -SLALOM_POLE_NEAR_Z;
	legacy_s16 second = max_x > -SLALOM_POLE_OUTER_X && min_x < -SLALOM_POLE_INNER_X &&
						max_z > SLALOM_POLE_NEAR_Z && min_z < SLALOM_POLE_FAR_Z;
	if (!first && !second) {
		return 0;
	}
	struct OWOOT_PROJECTED_POINT hull[2U * CARSTATE_WHEEL_COUNT * (OWOOT_WHEEL_VERTEX_MAX + 1U)];
	legacy_u16 hull_count = road_projected_hull(points, count, hull);
	return (first && road_swept_barrier_overlap(barriers[0], hull, hull_count, &sweep)) ||
		   (second && road_swept_barrier_overlap(barriers[1], hull, hull_count, &sweep));
}

static void road_append_crossing(struct OWOOT_PROJECTED_POINT *points, legacy_u16 *count,
								 const struct VECTOR *vertices, const legacy_s64 *distances,
								 legacy_u16 first, legacy_u16 second)
{
	legacy_s64 start = distances[first];
	legacy_s64 end = distances[second];
	if ((start < 0 && end > 0) || (start > 0 && end < 0)) {
		struct OWOOT_PROJECTED_POINT *point = &points[(*count)++];
		point->x = (legacy_s32)vertices[first].x * OWOOT_PROJECTION_SCALE +
				   (legacy_s32)((legacy_s64)(vertices[second].x - vertices[first].x) *
								OWOOT_PROJECTION_SCALE * start / (start - end));
		point->z = (legacy_s32)vertices[first].z * OWOOT_PROJECTION_SCALE +
				   (legacy_s32)((legacy_s64)(vertices[second].z - vertices[first].z) *
								OWOOT_PROJECTION_SCALE * start / (start - end));
	}
}

static legacy_s16 road_triangle_overlaps_wheel(const struct OWOOT_ROAD_TRIANGLE far *triangle,
											   const struct VECTOR *vertices, legacy_u16 ring_count,
											   legacy_s16 contact_tolerance)
{
	legacy_s16 min_x = triangle->vertex[0].x;
	legacy_s16 max_x = min_x;
	legacy_s16 min_z = triangle->vertex[0].z;
	legacy_s16 max_z = min_z;
	for (legacy_u16 i = 1; i < 3; i++) {
		if (triangle->vertex[i].x < min_x) {
			min_x = triangle->vertex[i].x;
		}
		if (triangle->vertex[i].x > max_x) {
			max_x = triangle->vertex[i].x;
		}
		if (triangle->vertex[i].z < min_z) {
			min_z = triangle->vertex[i].z;
		}
		if (triangle->vertex[i].z > max_z) {
			max_z = triangle->vertex[i].z;
		}
	}
	legacy_s16 left = 1;
	legacy_s16 right = 1;
	legacy_s16 before = 1;
	legacy_s16 after = 1;
	for (legacy_u16 i = 0; i < ring_count * OWOOT_WHEEL_RIM_COUNT; i++) {
		left &= vertices[i].x < min_x;
		right &= vertices[i].x > max_x;
		before &= vertices[i].z < min_z;
		after &= vertices[i].z > max_z;
	}
	if (left || right || before || after) {
		return 0;
	}
	struct OWOOT_PROJECTED_POINT road[3];
	struct VECTOR a = triangle->vertex[0];
	struct VECTOR b = triangle->vertex[1];
	struct VECTOR c = triangle->vertex[2];
	legacy_s32 normal_x =
		(legacy_s32)(b.y - a.y) * (c.z - a.z) - (legacy_s32)(b.z - a.z) * (c.y - a.y);
	legacy_s32 normal_y =
		(legacy_s32)(b.z - a.z) * (c.x - a.x) - (legacy_s32)(b.x - a.x) * (c.z - a.z);
	legacy_s32 normal_z =
		(legacy_s32)(b.x - a.x) * (c.y - a.y) - (legacy_s32)(b.y - a.y) * (c.x - a.x);
	if (normal_y < 0) {
		normal_x = -normal_x;
		normal_y = -normal_y;
		normal_z = -normal_z;
	} else if (normal_y == 0) {
		/* A vertical driving surface projects to a line. Its lowest vertex
		 * is the first height at which a wheel can lie on or above it. */
		normal_x = 0;
		normal_y = 1;
		normal_z = 0;
		if (b.y < a.y) {
			a.y = b.y;
		}
		if (c.y < a.y) {
			a.y = c.y;
		}
	}
	for (legacy_u16 i = 0; i < 3; i++) {
		road[i].x = (legacy_s32)triangle->vertex[i].x * OWOOT_PROJECTION_SCALE;
		road[i].z = (legacy_s32)triangle->vertex[i].z * OWOOT_PROJECTION_SCALE;
	}
	struct OWOOT_PROJECTED_POINT points[OWOOT_CLIPPED_VERTEX_MAX];
	legacy_s64 distances[OWOOT_WHEEL_VERTEX_MAX];
	legacy_u16 count = 0;
	for (legacy_u16 i = 0; i < ring_count * OWOOT_WHEEL_RIM_COUNT; i++) {
		distances[i] =
			(legacy_s64)normal_x * (vertices[i].x - a.x) +
			(legacy_s64)normal_y * ((legacy_s32)vertices[i].y - a.y + contact_tolerance) +
			(legacy_s64)normal_z * (vertices[i].z - a.z);
		if (distances[i] >= 0) {
			points[count].x = (legacy_s32)vertices[i].x * OWOOT_PROJECTION_SCALE;
			points[count++].z = (legacy_s32)vertices[i].z * OWOOT_PROJECTION_SCALE;
		}
	}
	if (count == 0) {
		return 0;
	}
	if (count != ring_count * OWOOT_WHEEL_RIM_COUNT) {
		for (legacy_u16 i = 0; i < ring_count; i++) {
			legacy_u16 next = (i + 1) % ring_count;
			road_append_crossing(points, &count, vertices, distances, i, next);
			road_append_crossing(points, &count, vertices, distances, i + ring_count,
								 next + ring_count);
			road_append_crossing(points, &count, vertices, distances, i, i + ring_count);
		}
	}
	struct OWOOT_PROJECTED_POINT hull[OWOOT_CLIPPED_VERTEX_MAX * 2U];
	legacy_u16 hull_count = road_projected_hull(points, count, hull);
	return road_projected_overlap(road, hull, hull_count);
}

#ifdef RESTUNTS_SDL3
static legacy_s16 road_extended_tile_overlaps_wheel(const struct VECTOR *vertices,
													legacy_u16 ring_count, int column,
													int world_row, legacy_s16 contact_tolerance)
{
	const struct CSTRK_OBJECT *placed =
		extended_track_object_at(column, extended_track_height() - 1 - world_row);
	if (!placed) {
		return 0;
	}
	unsigned id = extended_track_object_id(placed);
	const struct TRACKOBJECT *object = &trkObjectList[id];
	int model = object->ss_physicalModel;
	if (model < 0 || model > PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT) {
		return 0;
	}
	if (model == PHYSICAL_MODEL_ROAD && (placed->slope_x || placed->slope_y)) {
		model = OWOOT_HILL_ROAD_MODEL;
	}
	struct VECTOR origin, local[OWOOT_WHEEL_VERTEX_MAX];
	extended_geometry_object_origin(extended_track_model(), placed, &origin);
	for (legacy_u16 index = 0; index < ring_count * OWOOT_WHEEL_RIM_COUNT; index++) {
		local[index] = (struct VECTOR){LEGACY_S16_WRAP_SUB(vertices[index].x, origin.x),
									   LEGACY_S16_WRAP_SUB(vertices[index].y, origin.y),
									   LEGACY_S16_WRAP_SUB(vertices[index].z, origin.z)};
		if (extended_geometry_sloped_stunt(placed)) {
			local[index].y = LEGACY_S16_WRAP_SUB(
				local[index].y,
				extended_geometry_object_grade(placed, local[index].x, local[index].z));
		}
		legacy_s16 x = local[index].x, z = local[index].z;
		switch ((legacy_u16)object->ss_rotY) {
			case ANGLE_QUARTER_TURN:
				local[index].x = -z;
				local[index].z = x;
				break;
			case ANGLE_HALF_TURN:
				local[index].x = -x;
				local[index].z = -z;
				break;
			case ANGLE_THREE_QUARTER_TURN:
				local[index].x = z;
				local[index].z = -x;
				break;
		}
	}
	const struct OWOOT_ROAD_MODEL *geometry = &owoot_road_models[model];
	for (legacy_u16 index = 0; index < geometry->triangle_count; index++) {
		if (road_triangle_overlaps_wheel(&owoot_road_triangles[geometry->first_triangle + index],
										 local, ring_count, contact_tolerance)) {
			return 1;
		}
	}
	return 0;
}

static legacy_s16 road_extended_overlaps_wheel(const struct VECTOR *vertices, legacy_u16 ring_count,
											   legacy_s16 contact_tolerance)
{
	legacy_s32 min_x, min_z;
	extended_geometry_world_position(&vertices[0], &min_x, &min_z);
	legacy_s32 max_x = min_x, max_z = min_z;
	for (legacy_u16 index = 1; index < ring_count * OWOOT_WHEEL_RIM_COUNT; index++) {
		legacy_s32 x, z;
		extended_geometry_world_position(&vertices[index], &x, &z);
		if (x < min_x) {
			min_x = x;
		}
		if (x > max_x) {
			max_x = x;
		}
		if (z < min_z) {
			min_z = z;
		}
		if (z > max_z) {
			max_z = z;
		}
	}
	int first_column = (int)((min_x - OWOOT_ROAD_TILE_OVERHANG) >> 10);
	int last_column = (int)((max_x + OWOOT_ROAD_TILE_OVERHANG) >> 10);
	int first_row = (int)((min_z - OWOOT_ROAD_TILE_OVERHANG) >> 10);
	int last_row = (int)((max_z + OWOOT_ROAD_TILE_OVERHANG) >> 10);
	if (first_column < 0) {
		first_column = 0;
	}
	if (first_row < 0) {
		first_row = 0;
	}
	if (last_column >= extended_track_width()) {
		last_column = extended_track_width() - 1;
	}
	if (last_row >= extended_track_height()) {
		last_row = extended_track_height() - 1;
	}
	for (int row = first_row; row <= last_row; row++) {
		for (int column = first_column; column <= last_column; column++) {
			if (road_extended_tile_overlaps_wheel(vertices, ring_count, column, row,
												  contact_tolerance)) {
				return 1;
			}
		}
	}
	return 0;
}
#endif

static legacy_s16 road_tile_overlaps_wheel(const struct VECTOR *vertices, legacy_u16 ring_count,
										   legacy_s16 column, legacy_s16 row,
										   legacy_s16 contact_tolerance)
{
	legacy_u8 tile = track_element_map[terrainrows[row] + column];
	if (tile == TRACK_TILE_CONTINUATION_SOUTHEAST) {
		row++;
		column--;
	} else if (tile == TRACK_TILE_CONTINUATION_SOUTH) {
		row++;
	} else if (tile == TRACK_TILE_CONTINUATION_EAST) {
		column--;
	}
	if (column < 0 || column >= TRACK_GRID_SIZE || row < 0 || row >= TRACK_GRID_SIZE) {
		return 0;
	}
	tile = track_element_map[terrainrows[row] + column];
	if (tile == 0 || tile >= sizeof(trkObjectList) / sizeof(trkObjectList[0])) {
		return 0;
	}
	legacy_u8 terrain = track_terrain_map[trackrows[row] + column];
	if (terrain >= HILL_TERRAIN_FIRST && terrain < HILL_TERRAIN_END) {
		tile = subst_hillroad_track(terrain, tile);
		if (tile == 0) {
			return 0;
		}
	}
	const struct TRACKOBJECT *object = &trkObjectList[tile];
	legacy_s16 model = object->ss_physicalModel;
	if (model < 0 || model > PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT) {
		return 0;
	}
	if (model == PHYSICAL_MODEL_ROAD && terrain >= HILL_TERRAIN_FIRST &&
		terrain < HILL_TERRAIN_END) {
		model = OWOOT_HILL_ROAD_MODEL;
	}
	legacy_s16 origin_x = (legacy_s16)((column + 1) * TRACK_TILE_SIZE);
	legacy_s16 origin_z = (legacy_s16)(row * TRACK_TILE_SIZE);
	if ((object->ss_multiTileFlag & MULTI_TILE_COLUMN_EDGE_FLAG) == 0) {
		origin_x -= TRACK_TILE_HALF_SIZE;
	}
	if ((object->ss_multiTileFlag & MULTI_TILE_ROW_EDGE_FLAG) == 0) {
		origin_z += TRACK_TILE_HALF_SIZE;
	}
	legacy_s16 elevation = terrain == TERRAIN_RAISED_TILE
							   ? (legacy_s16)hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX]
							   : 0;
	struct VECTOR local[OWOOT_WHEEL_VERTEX_MAX];
	for (legacy_u16 i = 0; i < ring_count * OWOOT_WHEEL_RIM_COUNT; i++) {
		legacy_s16 x = LEGACY_S16_WRAP_SUB(vertices[i].x, origin_x);
		legacy_s16 z = LEGACY_S16_WRAP_SUB(vertices[i].z, origin_z);
		local[i].x = x;
		local[i].y = LEGACY_S16_WRAP_SUB(vertices[i].y, elevation);
		local[i].z = z;
		switch ((legacy_u16)object->ss_rotY) {
			case ANGLE_QUARTER_TURN:
				local[i].x = -z;
				local[i].z = x;
				break;
			case ANGLE_HALF_TURN:
				local[i].x = -x;
				local[i].z = -z;
				break;
			case ANGLE_THREE_QUARTER_TURN:
				local[i].x = z;
				local[i].z = -x;
				break;
		}
	}
	const struct OWOOT_ROAD_MODEL *geometry = &owoot_road_models[model];
	for (legacy_u16 i = 0; i < geometry->triangle_count; i++) {
		if (road_triangle_overlaps_wheel(&owoot_road_triangles[geometry->first_triangle + i], local,
										 ring_count, contact_tolerance)) {
			return 1;
		}
	}
	return 0;
}

legacy_s16 track_road_overlaps_wheel(const struct VECTOR *vertices, legacy_u16 ring_count,
									 legacy_s16 road_contact)
{
	if (ring_count < 3U || ring_count > OWOOT_WHEEL_RING_MAX) {
		return 0;
	}
	legacy_s16 contact_tolerance = road_contact ? OWOOT_ROAD_CONTACT_TOLERANCE : 0;
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		return road_extended_overlaps_wheel(vertices, ring_count, contact_tolerance);
	}
#endif
	legacy_s16 min_x = vertices[0].x;
	legacy_s16 max_x = vertices[0].x;
	legacy_s16 min_z = vertices[0].z;
	legacy_s16 max_z = vertices[0].z;
	for (legacy_u16 i = 1; i < ring_count * OWOOT_WHEEL_RIM_COUNT; i++) {
		if (vertices[i].x < min_x) {
			min_x = vertices[i].x;
		}
		if (vertices[i].x > max_x) {
			max_x = vertices[i].x;
		}
		if (vertices[i].z < min_z) {
			min_z = vertices[i].z;
		}
		if (vertices[i].z > max_z) {
			max_z = vertices[i].z;
		}
	}
	/* The renderer extends road ends by up to seven world units, so adjacent
	 * elements may own a visible surface just outside their nominal tile. */
	legacy_s16 first_column =
		(legacy_s16)(((legacy_s32)min_x - OWOOT_ROAD_TILE_OVERHANG) >> TRACK_TILE_POSITION_SHIFT);
	legacy_s16 last_column =
		(legacy_s16)(((legacy_s32)max_x + OWOOT_ROAD_TILE_OVERHANG) >> TRACK_TILE_POSITION_SHIFT);
	legacy_s16 first_row =
		(legacy_s16)(((legacy_s32)min_z - OWOOT_ROAD_TILE_OVERHANG) >> TRACK_TILE_POSITION_SHIFT);
	legacy_s16 last_row =
		(legacy_s16)(((legacy_s32)max_z + OWOOT_ROAD_TILE_OVERHANG) >> TRACK_TILE_POSITION_SHIFT);
	if (first_column < 0) {
		first_column = 0;
	}
	if (last_column >= TRACK_GRID_SIZE) {
		last_column = TRACK_GRID_LAST_INDEX;
	}
	if (first_row < 0) {
		first_row = 0;
	}
	if (last_row >= TRACK_GRID_SIZE) {
		last_row = TRACK_GRID_LAST_INDEX;
	}
	for (legacy_s16 row = first_row; row <= last_row; row++) {
		for (legacy_s16 column = first_column; column <= last_column; column++) {
			if (road_tile_overlaps_wheel(vertices, ring_count, column, row, contact_tolerance)) {
				return 1;
			}
		}
	}
	return 0;
}
