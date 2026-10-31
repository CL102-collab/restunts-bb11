#ifdef RESTUNTS_SDL3
#include "extended_frame_internal.h"
#include <stdlib.h>
#include <string.h>

legacy_u8 frame_extended_hill_edges(const struct CSTRK_TRACK *track, legacy_s16 x, legacy_s16 y)
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

/* Attached markings are layers of the preceding face. Native hill dashes
 * have independently rounded (and occasionally incorrect) heights, visible as
 * dips in HyperVision. Drape them onto their corrected road instead of retaining
 * those offsets. Curbs and horizontal spans keep their authored heights. */
void frame_extended_align_hill_markings(struct SHAPE3D *mesh)
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
legacy_u8 frame_extended_road_grade_edges(const struct SHAPE3D *source, legacy_s16 yaw,
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

legacy_u8 hires_track_queue_hill_shape(struct TRANSFORMEDSHAPE3D *instance, legacy_u8 edges,
									   legacy_u16 *result)
{
	const struct SHAPE3D *source = instance->shapeptr;
	if (edges == 0 || source->shape3d_numverts > LEGACY_U8_MAX) {
		return 0;
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
	*result = shape3d_transform_and_queue(&adjusted);
	return 1;
}

#endif
