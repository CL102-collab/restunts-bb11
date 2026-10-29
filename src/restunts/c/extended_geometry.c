#include "extended_geometry.h"
#include "trackdata_layout.h"
#include <stdlib.h>
#include <string.h>

static legacy_s32 context_x, context_z;

static double geometry_sqrt(double value)
{
	if (value <= 0.0) {
		return 0.0;
	}
	double root = value > 1.0 ? value : 1.0;
	for (unsigned int iteration = 0; iteration < 16; iteration++) {
		root = (root + value / root) * 0.5;
	}
	return root;
}

static legacy_s16 geometry_word(double value)
{
	legacy_s32 rounded = (legacy_s32)(value < 0.0 ? value - 0.5 : value + 0.5);
	return LEGACY_S16_FROM_BITS((legacy_u16)rounded);
}

legacy_s32 extended_geometry_unwrap(legacy_s16 coordinate, legacy_s32 reference)
{
	/* The difference is intentionally a wrapped word: it describes a local
	 * displacement, while the reference identifies the correct 65536-unit page. */
	legacy_s16 reference_word = LEGACY_S16_FROM_BITS((legacy_u16)reference);
	return reference + LEGACY_S16_WRAP_SUB(coordinate, reference_word);
}

void extended_geometry_set_context(legacy_s32 fixed_x, legacy_s32 fixed_z)
{
	context_x = LEGACY_S32_SAR(fixed_x, 6U);
	context_z = LEGACY_S32_SAR(fixed_z, 6U);
}

void extended_geometry_get_context(legacy_s32 *world_x, legacy_s32 *world_z)
{
	*world_x = context_x;
	*world_z = context_z;
}

void extended_geometry_world_position(const struct VECTOR *wrapped, legacy_s32 *x, legacy_s32 *z)
{
	*x = extended_geometry_unwrap(wrapped->x, context_x);
	*z = extended_geometry_unwrap(wrapped->z, context_z);
}

static void geometry_basis(double slope_x, double slope_z, struct EXTENDED_GEOMETRY_BASIS *basis)
{
	basis->slope_x = slope_x;
	basis->slope_z = slope_z;
	basis->normal_length = geometry_sqrt(1.0 + slope_x * slope_x + slope_z * slope_z);
	basis->normal_x = -slope_x / basis->normal_length;
	basis->normal_y = 1.0 / basis->normal_length;
	basis->normal_z = -slope_z / basis->normal_length;
	double x_length = geometry_sqrt(1.0 + slope_x * slope_x);
	basis->tangent_xx = 1.0 / x_length;
	basis->tangent_xy = slope_x / x_length;
	basis->tangent_xz = 0.0;
	basis->tangent_zx = basis->tangent_xy * basis->normal_z;
	basis->tangent_zy = -basis->tangent_xx * basis->normal_z;
	basis->tangent_zz = basis->tangent_xx * basis->normal_y - basis->tangent_xy * basis->normal_x;
}

void extended_geometry_object_basis(const struct CSTRK_OBJECT *object,
									struct EXTENDED_GEOMETRY_BASIS *basis)
{
	geometry_basis(object->slope_x * (450.0 / 1024.0), -object->slope_y * (450.0 / 1024.0), basis);
}

void extended_geometry_object_origin(const struct CSTRK_TRACK *track,
									 const struct CSTRK_OBJECT *object, struct VECTOR *origin)
{
	origin->x = LEGACY_S16_FROM_BITS((legacy_u16)(object->x * 1024U + object->width * 512U));
	origin->z = LEGACY_S16_FROM_BITS(
		(legacy_u16)(track->height * 1024U - object->y * 1024U - object->height * 512U));
	origin->y =
		(legacy_s16)(object->base_height * 450 +
					 (object->slope_x * object->width + object->slope_y * object->height) * 225);
}

/* One orthonormal placement rotation applies to an entire object. Mesh and
 * collision use the same axes. Connector approaches, where needed, are separate
 * road geometry: original stunt geometry never stretches to fill map cells. */
void extended_geometry_forward(const struct EXTENDED_GEOMETRY_BASIS *basis,
							   const struct VECTOR *local, struct VECTOR *world)
{
	world->x = geometry_word(basis->tangent_xx * local->x + basis->normal_x * local->y +
							 basis->tangent_zx * local->z);
	world->y = geometry_word(basis->tangent_xy * local->x + basis->normal_y * local->y +
							 basis->tangent_zy * local->z);
	world->z = geometry_word(basis->tangent_xz * local->x + basis->normal_z * local->y +
							 basis->tangent_zz * local->z);
}

void extended_geometry_inverse(const struct EXTENDED_GEOMETRY_BASIS *basis,
							   const struct VECTOR *world, struct VECTOR *local)
{
	local->x = geometry_word(basis->tangent_xx * world->x + basis->tangent_xy * world->y +
							 basis->tangent_xz * world->z);
	local->y = geometry_word(basis->normal_x * world->x + basis->normal_y * world->y +
							 basis->normal_z * world->z);
	local->z = geometry_word(basis->tangent_zx * world->x + basis->tangent_zy * world->y +
							 basis->tangent_zz * world->z);
}

static void geometry_normalize(double *x, double *y, double *z)
{
	double length = geometry_sqrt(*x * *x + *y * *y + *z * *z);
	if (length == 0.0) {
		*x = 0.0;
		*y = 1.0;
		*z = 0.0;
		return;
	}
	*x /= length;
	*y /= length;
	*z /= length;
}

static void geometry_plane_axes(struct PLANE *plane, double nx, double ny, double nz, double tx,
								double ty, double tz)
{
	geometry_normalize(&nx, &ny, &nz);
	double projection = tx * nx + ty * ny + tz * nz;
	tx -= projection * nx;
	ty -= projection * ny;
	tz -= projection * nz;
	geometry_normalize(&tx, &ty, &tz);
	double zx = ty * nz - tz * ny;
	double zy = tz * nx - tx * nz;
	double zz = tx * ny - ty * nx;
	plane->plane_normal = (struct VECTOR){geometry_word(nx * 8192.0), geometry_word(ny * 8192.0),
										  geometry_word(nz * 8192.0)};
	plane->plane_rotation.m._11 = geometry_word(tx * 16384.0);
	plane->plane_rotation.m._21 = geometry_word(ty * 16384.0);
	plane->plane_rotation.m._31 = geometry_word(tz * 16384.0);
	plane->plane_rotation.m._12 = geometry_word(nx * 16384.0);
	plane->plane_rotation.m._22 = geometry_word(ny * 16384.0);
	plane->plane_rotation.m._32 = geometry_word(nz * 16384.0);
	plane->plane_rotation.m._13 = geometry_word(zx * 16384.0);
	plane->plane_rotation.m._23 = geometry_word(zy * 16384.0);
	plane->plane_rotation.m._33 = geometry_word(zz * 16384.0);
	plane->plane_yz = polarAngle(plane->plane_normal.z, plane->plane_normal.y);
	plane->plane_xy = polarAngle(plane->plane_normal.x, plane->plane_normal.y);
}

void extended_geometry_make_plane(struct PLANE *plane, double slope_x, double slope_z,
								  legacy_s16 height)
{
	plane->plane_origin = (struct VECTOR){0, height, 0};
	geometry_plane_axes(plane, -slope_x, 1.0, -slope_z, 1.0, slope_x, 0.0);
}

void extended_geometry_transform_plane(const struct EXTENDED_GEOMETRY_BASIS *basis,
									   const struct PLANE *source, struct PLANE *destination)
{
	*destination = *source;
	extended_geometry_forward(basis, &source->plane_origin, &destination->plane_origin);
	/* For a rigid orthonormal rotation, vectors and covectors use the same matrix. */
	double x = source->plane_normal.x, y = source->plane_normal.y, z = source->plane_normal.z;
	double nx = basis->tangent_xx * x + basis->normal_x * y + basis->tangent_zx * z;
	double ny = basis->tangent_xy * x + basis->normal_y * y + basis->tangent_zy * z;
	double nz = basis->tangent_xz * x + basis->normal_z * y + basis->tangent_zz * z;
	double tx = source->plane_rotation.m._11;
	double ty = source->plane_rotation.m._21;
	double tz = source->plane_rotation.m._31;
	double transformed_x = basis->tangent_xx * tx + basis->normal_x * ty + basis->tangent_zx * tz;
	double transformed_y = basis->tangent_xy * tx + basis->normal_y * ty + basis->tangent_zy * tz;
	double transformed_z = basis->tangent_xz * tx + basis->normal_z * ty + basis->tangent_zz * tz;
	geometry_plane_axes(destination, nx, ny, nz, transformed_x, transformed_y, transformed_z);
}

int extended_geometry_terrain(const struct CSTRK_TRACK *track, legacy_s32 world_x,
							  legacy_s32 world_z, struct EXTENDED_TERRAIN_SAMPLE *sample)
{
	legacy_s32 map_y = (legacy_s32)track->height * 1024 - world_z;
	if (world_x < 0 || map_y < 0 || world_x >= (legacy_s32)track->width * 1024 ||
		map_y >= (legacy_s32)track->height * 1024) {
		return 0;
	}
	legacy_s16 column = (legacy_s16)(world_x / 1024);
	legacy_s16 row = (legacy_s16)(map_y / 1024);
	legacy_s16 u = (legacy_s16)(world_x % 1024), v = (legacy_s16)(map_y % 1024);
	size_t stride = track->width + 1U;
	size_t corner = (size_t)row * stride + column;
	legacy_s16 nw = track->corner_heights[corner] * 450;
	legacy_s16 ne = track->corner_heights[corner + 1] * 450;
	legacy_s16 sw = track->corner_heights[corner + stride] * 450;
	legacy_s16 se = track->corner_heights[corner + stride + 1] * 450;
	const struct CSTRK_CELL *cell = &track->cells[(size_t)row * track->width + column];
	legacy_s16 dx, dy, intercept = nw;
	if (cell->half_water_corner == CSTRK_WATER_NORTHWEST ||
		cell->half_water_corner == CSTRK_WATER_SOUTHEAST) {
		if (u + v <= 1024) {
			dx = ne - nw;
			dy = sw - nw;
		} else {
			dx = se - sw;
			dy = se - ne;
			intercept = ne + sw - se;
		}
	} else if (v <= u) {
		dx = ne - nw;
		dy = se - ne;
	} else {
		dx = se - sw;
		dy = sw - nw;
	}
	sample->column = column;
	sample->north_row = row;
	sample->origin.x = LEGACY_S16_FROM_BITS((legacy_u16)(column * 1024 + 512));
	sample->origin.z = LEGACY_S16_FROM_BITS((legacy_u16)(track->height * 1024 - row * 1024 - 512));
	sample->origin.y = 0;
	extended_geometry_make_plane(&sample->plane, dx / 1024.0, -dy / 1024.0,
								 (legacy_s16)(intercept + (dx + dy) / 2));
	static const legacy_s16 surface_types[4] = {4, 3, 2, 5};
	sample->surface = surface_types[cell->surface];
	/* The corner identifies the secondary surface half. Its diagonal is shared
	 * with the terrain's triangular split, including on elevated surfaces. */
	if ((cell->half_water_corner == CSTRK_WATER_NORTHWEST && u + v < 1024) ||
		(cell->half_water_corner == CSTRK_WATER_NORTHEAST && v < u) ||
		(cell->half_water_corner == CSTRK_WATER_SOUTHEAST && u + v >= 1024) ||
		(cell->half_water_corner == CSTRK_WATER_SOUTHWEST && v >= u)) {
		sample->surface = surface_types[cell->corner_surface];
	}
	return 1;
}

int extended_geometry_transform_shape(const struct SHAPE3D *source, struct SHAPE3D *destination,
									  const struct EXTENDED_GEOMETRY_BASIS *basis,
									  legacy_s16 orientation, unsigned int surface,
									  legacy_s16 physical_model)
{
	static const unsigned char counts[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 2, 6, 3, 0, 0};
	memset(destination, 0, sizeof(*destination));
	if (source == 0 || source->shape3d_numverts == 0 || source->shape3d_numpaints == 0) {
		return 0;
	}
	/* Scenery has its own paint and animation palette. A road-surface byte in
	 * an older track must not select a different scenery paint or recolor it. */
	if (physical_model >= 64) {
		surface = CSTRK_ROAD_PAVED;
	}
	size_t primitive_bytes = 1;
	const legacy_u8 *primitive = source->shape3d_primitives;
	for (unsigned i = 0; i < source->shape3d_numprimitives; i++) {
		if (primitive[0] >= 16 || counts[primitive[0]] == 0) {
			return 0;
		}
		primitive_bytes += 3U + counts[primitive[0]];
		primitive += 2U + source->shape3d_numpaints + counts[primitive[0]];
	}
	size_t vertex_bytes = (size_t)source->shape3d_numverts * SHAPE3D_VERTEX_SIZE;
	size_t mask_bytes = (size_t)source->shape3d_numprimitives * 4U;
	legacy_u8 *memory = malloc(vertex_bytes + primitive_bytes + mask_bytes * 2U);
	if (memory == 0) {
		return 0;
	}
	*destination = *source;
	destination->shape3d_vertex_bytes = memory;
	destination->shape3d_primitives = memory + vertex_bytes;
	destination->shape3d_visibility_masks = memory + vertex_bytes + primitive_bytes;
	destination->shape3d_front_facing_masks = memory + vertex_bytes + primitive_bytes + mask_bytes;
	destination->shape3d_numpaints = 1;
	memset(destination->shape3d_visibility_masks, 255, mask_bytes);
	memset(destination->shape3d_front_facing_masks, 0, mask_bytes);
	for (unsigned i = 0; i < source->shape3d_numverts; i++) {
		struct VECTOR local, rotated, transformed;
		const legacy_u8 *vertex = source->shape3d_vertex_bytes + (size_t)i * SHAPE3D_VERTEX_SIZE;
		local.x = LEGACY_READ_S16_LE(vertex);
		local.y = LEGACY_READ_S16_LE(vertex + 2);
		local.z = LEGACY_READ_S16_LE(vertex + 4);
		rotated = local;
		switch (orientation & ANGLE_MASK) {
			case ANGLE_QUARTER_TURN:
				rotated.x = local.z;
				rotated.z = LEGACY_S16_WRAP_NEGATE(local.x);
				break;
			case ANGLE_HALF_TURN:
				rotated.x = LEGACY_S16_WRAP_NEGATE(local.x);
				rotated.z = LEGACY_S16_WRAP_NEGATE(local.z);
				break;
			case ANGLE_THREE_QUARTER_TURN:
				rotated.x = LEGACY_S16_WRAP_NEGATE(local.z);
				rotated.z = local.x;
				break;
		}
		extended_geometry_forward(basis, &rotated, &transformed);
		legacy_u8 *out_vertex = destination->shape3d_vertex_bytes + (size_t)i * SHAPE3D_VERTEX_SIZE;
		LEGACY_WRITE_U16_LE(out_vertex, (legacy_u16)transformed.x);
		LEGACY_WRITE_U16_LE(out_vertex + 2, (legacy_u16)transformed.y);
		LEGACY_WRITE_U16_LE(out_vertex + 4, (legacy_u16)transformed.z);
	}
	primitive = source->shape3d_primitives;
	legacy_u8 *out = destination->shape3d_primitives;
	for (unsigned i = 0; i < source->shape3d_numprimitives; i++) {
		unsigned count = counts[primitive[0]];
		unsigned paint = surface < source->shape3d_numpaints ? surface : 0;
		legacy_u8 material = primitive[2U + paint];
		if (surface != 0 && source->shape3d_numpaints < 3) {
			if (material == 19) {
				material = surface == CSTRK_ROAD_DIRT ? 25 : 28;
			} else if (material == 21) {
				material = surface == CSTRK_ROAD_DIRT ? 27 : 30;
			} else if (physical_model == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT && material >= 22 &&
					   material <= 24) {
				/* The horizontal corkscrew's curved road uses two shaded
				 * pavement materials and separate lane markings. */
				material = (surface == CSTRK_ROAD_DIRT ? 25 : 28) + material - 22;
			} else if ((physical_model == 18 || physical_model == 19 || physical_model == 22) &&
					   (material == 22 || material == 23)) {
				material = surface == CSTRK_ROAD_DIRT ? 25 : 28;
			}
		}
		out[0] = primitive[0];
		out[1] = primitive[1];
		out[2] = material;
		memcpy(out + 3, primitive + 2U + source->shape3d_numpaints, count);
		out += 3U + count;
		primitive += 2U + source->shape3d_numpaints + count;
	}
	*out = 0;
	return 1;
}

void extended_geometry_free_shape(struct SHAPE3D *shape)
{
	free(shape->shape3d_vertex_bytes);
	memset(shape, 0, sizeof(*shape));
}

void extended_geometry_initial_pose(const struct CSTRK_OBJECT *object, legacy_s16 track_angle,
									struct VECTOR *rotation, struct VECTOR *normal_clearance)
{
	if (!object->slope_x && !object->slope_y) {
		*rotation = (struct VECTOR){LEGACY_S16_WRAP_NEGATE(track_angle), 0, 0};
		*normal_clearance = (struct VECTOR){0, 8, 0};
		return;
	}
	struct EXTENDED_GEOMETRY_BASIS basis;
	extended_geometry_object_basis(object, &basis);
	struct MATRIX yaw, combined;
	mat_rot_y(&yaw, track_angle);
	struct VECTOR column, transformed;
	column = (struct VECTOR){yaw.m._11, yaw.m._21, yaw.m._31};
	extended_geometry_forward(&basis, &column, &transformed);
	combined.m._11 = transformed.x;
	combined.m._21 = transformed.y;
	combined.m._31 = transformed.z;
	column = (struct VECTOR){yaw.m._12, yaw.m._22, yaw.m._32};
	extended_geometry_forward(&basis, &column, &transformed);
	combined.m._12 = transformed.x;
	combined.m._22 = transformed.y;
	combined.m._32 = transformed.z;
	column = (struct VECTOR){yaw.m._13, yaw.m._23, yaw.m._33};
	extended_geometry_forward(&basis, &column, &transformed);
	combined.m._13 = transformed.x;
	combined.m._23 = transformed.y;
	combined.m._33 = transformed.z;
	rotation->x = LEGACY_S16_WRAP_NEGATE(polarAngle(combined.m._13, combined.m._33));
	rotation->y = LEGACY_S16_WRAP_NEGATE(polarAngle(LEGACY_S16_WRAP_NEGATE(combined.m._23),
													polarRadius2D(combined.m._21, combined.m._22)));
	rotation->z = LEGACY_S16_WRAP_NEGATE(polarAngle(combined.m._21, combined.m._22));
	column = (struct VECTOR){0, 8, 0};
	extended_geometry_forward(&basis, &column, normal_clearance);
}
