#ifndef RESTUNTS_HIRES_SHAPE3D_MESH_H
#define RESTUNTS_HIRES_SHAPE3D_MESH_H

#include <stddef.h>
#include "../shape3d/shape3d.h"

#ifdef RESTUNTS_SDL3
#define HIRES_MESH_LEGACY_CONTAINER_HEADER_SIZE 8U
#define HIRES_MESH_CONTAINER_HEADER_SIZE 12U
#define HIRES_MESH_DIRECTORY_ENTRY_SIZE 12U
#define HIRES_MESH_SHAPE_HEADER_SIZE 8U
#define HIRES_MESH_LEGACY_VERSION 2U
#define HIRES_MESH_VERSION 3U
#define HIRES_MESH_MAX_FRACTION_BITS 8U
#define HIRES_MESH_NATIVE_VERTEX_COUNT 32U
#define HIRES_MESH_CAR_COUNT 2U
#define HIRES_MESH_CAR_SHAPE_COUNT 3U
#define HIRES_MESH_WORD_INDEX_SIZE 2U
#define HIRES_MESH_BYTE_INDEX_SIZE 1U

/* Wide descriptors use word indices; stock descriptors retain byte indices.
 * Keeping this invariant avoids changing the packed, shared SHAPE3D layout. */
static inline legacy_u16 hires_shape3d_index_size(const struct SHAPE3D *shape)
{
	return shape->shape3d_numverts > LEGACY_U8_MAX ? HIRES_MESH_WORD_INDEX_SIZE
												   : HIRES_MESH_BYTE_INDEX_SIZE;
}

static inline legacy_u16 hires_shape3d_vertex_index(const struct SHAPE3D *shape,
													const legacy_u8 *indices, legacy_u16 index)
{
	return shape->shape3d_numverts > LEGACY_U8_MAX
			   ? LEGACY_READ_U16_LE(indices + (size_t)index * HIRES_MESH_WORD_INDEX_SIZE)
			   : indices[index];
}

/* Decode a bounded H3D shape with native controls and fixed-point body vertices.
 * The non-owning descriptor exposes encoded bytes. Failure clears it. */
legacy_u8 hires_shape3d_mesh_decode(const legacy_u8 *data, size_t size, struct SHAPE3D *shape);
void hires_shape3d_load_car_meshes(
	legacy_u16 car, const legacy_s8 *id,
	const struct SHAPE3D *const classics[HIRES_MESH_CAR_SHAPE_COUNT]);
void hires_shape3d_free_car_meshes(void);
/* Lookup is read-only so concurrent geometry workers can share loaded meshes. */
struct SHAPE3D *hires_shape3d_car_mesh(const struct SHAPE3D *classic);
struct SHAPE3D_HIRES_VECTOR;
/* Fractional or uniformly scaled positions are owned by the loaded car;
 * NULL retains native vertices. Scaling preserves each model's ground plane.
 * Accept either its classic or enhanced descriptor. Reads are worker-safe. */
const struct SHAPE3D_HIRES_VECTOR *hires_shape3d_car_vertices(const struct SHAPE3D *shape);
/* Called after the classic wheel controls change, before geometry workers run. */
void hires_shape3d_update_car_mesh(const struct SHAPE3D *classic);
#endif
#endif
