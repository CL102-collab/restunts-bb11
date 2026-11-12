#include "shape3d_mesh.h"

#ifdef RESTUNTS_SDL3
#include <stdlib.h>
#include <string.h>
#include "../fileio.h"
#include "../platform.h"
#include "shape3d_hires.h"

#define HIRES_MESH_MATERIAL_COUNT 129U
#define HIRES_MESH_ALLOWED_FLAGS 3U
#define HIRES_MESH_MAX_FILE_BYTES (64UL * 1024UL * 1024UL)
#define HIRES_MESH_FIRST_WHEEL_VERTEX 8U
#define HIRES_MESH_WHEEL_VERTEX_COUNT 24U
#define HIRES_MESH_WHEEL_CONTROL_COUNT 6U
#define HIRES_MESH_WHEEL_COUNT 4U
#define HIRES_MESH_FRONT_WHEEL_COUNT 2U
#define HIRES_MESH_OPPOSITE_RIM_OFFSET 3U
#define HIRES_MESH_CAR_MIN_VERTICES (HIRES_MESH_FIRST_WHEEL_VERTEX + HIRES_MESH_WHEEL_VERTEX_COUNT)
#define HIRES_MESH_CAR_NAME_SIZE 4U
#define HIRES_MESH_FILENAME_SIZE 11U
#define HIRES_MESH_OFFSET_FIELD 4U
#define HIRES_MESH_LENGTH_FIELD 8U
#define HIRES_MESH_FRACTION_BITS_FIELD (3U * sizeof(legacy_u16))

static const legacy_u8 hires_mesh_vertex_counts[SHAPE3D_PRIMITIVE_WHEEL + 1U] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 2, 6};
static const legacy_s8 hires_mesh_names[HIRES_MESH_CAR_SHAPE_COUNT][HIRES_MESH_CAR_NAME_SIZE] = {
	{'c', 'a', 'r', '0'}, {'c', 'a', 'r', '1'}, {'c', 'a', 'r', '2'}};

struct HIRES_CAR_MESHES {
	legacy_u8 *data;
	const struct SHAPE3D *classics[HIRES_MESH_CAR_SHAPE_COUNT];
	struct SHAPE3D meshes[HIRES_MESH_CAR_SHAPE_COUNT];
	struct SHAPE3D_HIRES_VECTOR *vertices[HIRES_MESH_CAR_SHAPE_COUNT];
};
static struct HIRES_CAR_MESHES hires_car_meshes[HIRES_MESH_CAR_COUNT];

legacy_u8 hires_shape3d_mesh_decode(const legacy_u8 *data, size_t size, struct SHAPE3D *shape)
{
	if (shape == NULL) {
		return 0;
	}
	memset(shape, 0, sizeof(*shape));
	if (data == NULL || size < HIRES_MESH_SHAPE_HEADER_SIZE) {
		return 0;
	}
	struct SHAPE3D decoded = {0};
	decoded.shape3d_numverts = LEGACY_READ_U16_LE(data);
	decoded.shape3d_numprimitives = LEGACY_READ_U16_LE(data + sizeof(legacy_u16));
	decoded.shape3d_numpaints = LEGACY_READ_U16_LE(data + 2U * sizeof(legacy_u16));
	legacy_u16 fraction_bits = LEGACY_READ_U16_LE(data + HIRES_MESH_FRACTION_BITS_FIELD);
	if (decoded.shape3d_numverts == 0 || decoded.shape3d_numprimitives == 0 ||
		decoded.shape3d_numpaints == 0 || decoded.shape3d_numpaints > LEGACY_U8_MAX ||
		fraction_bits > HIRES_MESH_MAX_FRACTION_BITS) {
		return 0;
	}
	size_t vertex_bytes = (size_t)decoded.shape3d_numverts * SHAPE3D_VERTEX_SIZE;
	size_t mask_bytes = (size_t)decoded.shape3d_numprimitives * SHAPE3D_VISIBILITY_MASK_SIZE;
	size_t offset = HIRES_MESH_SHAPE_HEADER_SIZE + vertex_bytes + 2U * mask_bytes;
	if (offset > size) {
		return 0;
	}
	decoded.shape3d_vertex_bytes = (legacy_u8 *)data + HIRES_MESH_SHAPE_HEADER_SIZE;
	decoded.shape3d_visibility_masks = decoded.shape3d_vertex_bytes + vertex_bytes;
	decoded.shape3d_front_facing_masks = decoded.shape3d_visibility_masks + mask_bytes;
	decoded.shape3d_primitives = (legacy_u8 *)data + offset;
	for (legacy_u32 primitive = 0; primitive < decoded.shape3d_numprimitives; primitive++) {
		if (size - offset < SHAPE3D_PRIMITIVE_HEADER_SIZE + decoded.shape3d_numpaints) {
			return 0;
		}
		legacy_u8 type = data[offset];
		if (type == SHAPE3D_PRIMITIVE_EMPTY || type > SHAPE3D_PRIMITIVE_WHEEL ||
			(data[offset + 1U] & ~HIRES_MESH_ALLOWED_FLAGS) != 0) {
			return 0;
		}
		offset += SHAPE3D_PRIMITIVE_HEADER_SIZE;
		for (legacy_u16 paint = 0; paint < decoded.shape3d_numpaints; paint++) {
			if (data[offset + paint] >= HIRES_MESH_MATERIAL_COUNT) {
				return 0;
			}
		}
		offset += decoded.shape3d_numpaints;
		legacy_u16 count = hires_mesh_vertex_counts[type];
		size_t index_bytes = (size_t)count * hires_shape3d_index_size(&decoded);
		if (index_bytes > size - offset) {
			return 0;
		}
		for (legacy_u16 vertex = 0; vertex < count; vertex++) {
			if (hires_shape3d_vertex_index(&decoded, data + offset, vertex) >=
				decoded.shape3d_numverts) {
				return 0;
			}
		}
		offset += index_bytes;
	}
	if (offset != size) {
		return 0;
	}
	*shape = decoded;
	return 1;
}

static void hires_shape3d_clear_car_meshes(struct HIRES_CAR_MESHES *car)
{
	for (legacy_u16 index = 0; index < HIRES_MESH_CAR_SHAPE_COUNT; index++) {
		free(car->vertices[index]);
	}
	free(car->data);
	memset(car, 0, sizeof(*car));
}

static void hires_shape3d_copy_native_vertices(const struct SHAPE3D *shape,
											   struct SHAPE3D_HIRES_VECTOR *vertices)
{
	for (legacy_u16 index = 0; index < HIRES_MESH_NATIVE_VERTEX_COUNT; index++) {
		struct VECTOR point;
		shape3d_vertex_read(shape, index, &point);
		vertices[index] = (struct SHAPE3D_HIRES_VECTOR){point.x, point.y, point.z};
	}
}

static legacy_u8 hires_shape3d_decode_fractional_vertices(struct HIRES_CAR_MESHES *car,
														  legacy_u16 index,
														  legacy_u16 fraction_bits)
{
	if (fraction_bits == 0) {
		return 1;
	}
	struct SHAPE3D *shape = &car->meshes[index];
	struct SHAPE3D_HIRES_VECTOR *vertices =
		malloc((size_t)shape->shape3d_numverts * sizeof(*vertices));
	if (vertices == NULL) {
		return 0;
	}
	legacy_f64 divisor = (legacy_u32)1U << fraction_bits;
	hires_shape3d_copy_native_vertices(shape, vertices);
	for (legacy_u16 vertex = HIRES_MESH_NATIVE_VERTEX_COUNT; vertex < shape->shape3d_numverts;
		 vertex++) {
		struct VECTOR point;
		shape3d_vertex_read(shape, vertex, &point);
		vertices[vertex] =
			(struct SHAPE3D_HIRES_VECTOR){point.x / divisor, point.y / divisor, point.z / divisor};
	}
	car->vertices[index] = vertices;
	return 1;
}

static legacy_u8 *hires_shape3d_read_car_meshes(const legacy_s8 *id, size_t *size)
{
	legacy_s8 filename[HIRES_MESH_FILENAME_SIZE] = "ST0000.H3D";
	for (legacy_u16 index = 0; index < FILE_CAR_ID_LENGTH; index++) {
		legacy_s8 value = id[index];
		if (value == 0 || value == '.' || value == '/' || value == '\\' || value == ':' ||
			value == '*' || value == '?') {
			return NULL;
		}
		filename[2U + index] = value;
	}
	legacy_u16 handle = file_open_resource(filename);
	if (handle == 0) {
		return NULL;
	}
	legacy_u8 *data = NULL;
	if (dos_file_seek(handle, 0, DOS_FILE_SEEK_END) == 0) {
		legacy_s32 length = dos_file_tell(handle);
		if (length >= (legacy_s32)HIRES_MESH_CONTAINER_HEADER_SIZE &&
			(legacy_u32)length <= HIRES_MESH_MAX_FILE_BYTES &&
			dos_file_seek(handle, 0, DOS_FILE_SEEK_BEGIN) == 0) {
			data = malloc((size_t)length);
			if (data != NULL) {
				size_t read = 0;
				while (read < (size_t)length) {
					size_t remaining = (size_t)length - read;
					legacy_u16 chunk = remaining > LEGACY_U16_MAX ? LEGACY_U16_MAX : remaining;
					if (dos_file_read(handle, data + read, chunk) != chunk) {
						free(data);
						data = NULL;
						break;
					}
					read += chunk;
				}
				if (data != NULL) {
					*size = (size_t)length;
				}
			}
		}
	}
	dos_file_close(handle);
	return data;
}

static legacy_u8 hires_shape3d_car_wheels_match(const struct SHAPE3D *shape,
												const struct SHAPE3D *classic)
{
	for (legacy_u16 wheel = 0; wheel < HIRES_MESH_WHEEL_COUNT; wheel++) {
		legacy_u16 first = HIRES_MESH_FIRST_WHEEL_VERTEX + wheel * HIRES_MESH_WHEEL_CONTROL_COUNT;
		size_t offset = (size_t)first * SHAPE3D_VERTEX_SIZE;
		if (memcmp(shape->shape3d_vertex_bytes + offset, classic->shape3d_vertex_bytes + offset,
				   HIRES_MESH_WHEEL_CONTROL_COUNT * SHAPE3D_VERTEX_SIZE) == 0) {
			continue;
		}
		if (wheel >= HIRES_MESH_FRONT_WHEEL_COUNT) {
			return 0;
		}
		/* Freeing a steered car restores the original engine's neutral front
		 * wheels in its cached classic resource. That operation reverses both
		 * horizontal axes, swapping rims without changing the wheel geometry. */
		struct VECTOR center, opposite;
		shape3d_vertex_read(shape, first, &center);
		shape3d_vertex_read(shape, first + HIRES_MESH_OPPOSITE_RIM_OFFSET, &opposite);
		center.x = LEGACY_S16_SAR(LEGACY_S16_WRAP_ADD(center.x, opposite.x), 1U);
		for (legacy_u16 vertex = 0; vertex < HIRES_MESH_WHEEL_CONTROL_COUNT; vertex++) {
			struct VECTOR expected, actual;
			shape3d_vertex_read(shape, first + vertex, &expected);
			shape3d_vertex_read(classic, first + vertex, &actual);
			expected.x = LEGACY_S16_WRAP_ADD(center.x, LEGACY_S16_WRAP_SUB(center.x, expected.x));
			expected.z = LEGACY_S16_WRAP_ADD(center.z, LEGACY_S16_WRAP_SUB(center.z, expected.z));
			if (expected.x != actual.x || expected.y != actual.y || expected.z != actual.z) {
				return 0;
			}
		}
	}
	return 1;
}

static legacy_u8
hires_shape3d_decode_car_meshes(struct HIRES_CAR_MESHES *car, size_t size,
								const struct SHAPE3D *const classics[HIRES_MESH_CAR_SHAPE_COUNT])
{
	const legacy_u8 *data = car->data;
	legacy_u16 version = LEGACY_READ_U16_LE(data + sizeof("H3D"));
	if (memcmp(data, "H3D", sizeof("H3D")) != 0 || version != HIRES_MESH_VERSION) {
		return 0;
	}
	size_t header_size = HIRES_MESH_CONTAINER_HEADER_SIZE;
	legacy_u16 count = LEGACY_READ_U16_LE(data + sizeof("H3D") + sizeof(legacy_u16));
	size_t directory_end = header_size + (size_t)count * HIRES_MESH_DIRECTORY_ENTRY_SIZE;
	if (count == 0 || count > HIRES_MESH_CAR_SHAPE_COUNT || directory_end > size) {
		return 0;
	}
	legacy_u32 offsets[HIRES_MESH_CAR_SHAPE_COUNT] = {0};
	legacy_u32 lengths[HIRES_MESH_CAR_SHAPE_COUNT] = {0};
	for (legacy_u16 entry = 0; entry < count; entry++) {
		const legacy_u8 *record =
			data + header_size + (size_t)entry * HIRES_MESH_DIRECTORY_ENTRY_SIZE;
		legacy_u16 index = 0;
		while (index < HIRES_MESH_CAR_SHAPE_COUNT &&
			   memcmp(record, hires_mesh_names[index], HIRES_MESH_CAR_NAME_SIZE) != 0) {
			index++;
		}
		legacy_u32 offset = LEGACY_READ_U32_LE(record + HIRES_MESH_OFFSET_FIELD);
		legacy_u32 length = LEGACY_READ_U32_LE(record + HIRES_MESH_LENGTH_FIELD);
		if (index == HIRES_MESH_CAR_SHAPE_COUNT || car->classics[index] != NULL ||
			offset < directory_end || offset > size || length > size - offset ||
			classics[index] == NULL) {
			return 0;
		}
		for (legacy_u16 previous = 0; previous < entry; previous++) {
			if (offset < offsets[previous] + lengths[previous] &&
				offsets[previous] < offset + length) {
				return 0;
			}
		}
		struct SHAPE3D *shape = &car->meshes[index];
		const struct SHAPE3D *classic = classics[index];
		if (!hires_shape3d_mesh_decode(data + offset, length, shape) ||
			shape->shape3d_numverts < HIRES_MESH_CAR_MIN_VERTICES ||
			classic->shape3d_numverts < HIRES_MESH_CAR_MIN_VERTICES ||
			classic->shape3d_vertex_bytes == NULL ||
			shape->shape3d_numpaints != classic->shape3d_numpaints ||
			!hires_shape3d_car_wheels_match(shape, classic)) {
			return 0;
		}
		memcpy(shape->shape3d_vertex_bytes + HIRES_MESH_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
			   classic->shape3d_vertex_bytes + HIRES_MESH_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
			   HIRES_MESH_WHEEL_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE);
		legacy_u16 fraction_bits =
			LEGACY_READ_U16_LE(data + offset + HIRES_MESH_FRACTION_BITS_FIELD);
		if (!hires_shape3d_decode_fractional_vertices(car, index, fraction_bits)) {
			return 0;
		}
		car->classics[index] = classic;
		offsets[entry] = offset;
		lengths[entry] = length;
	}
	return 1;
}

void hires_shape3d_load_car_meshes(legacy_u16 car, const legacy_s8 *id,
								   const struct SHAPE3D *const classics[HIRES_MESH_CAR_SHAPE_COUNT])
{
	if (car >= HIRES_MESH_CAR_COUNT) {
		return;
	}
	struct HIRES_CAR_MESHES *meshes = &hires_car_meshes[car];
	hires_shape3d_clear_car_meshes(meshes);
	if (id == NULL || classics == NULL) {
		return;
	}
	size_t size = 0;
	meshes->data = hires_shape3d_read_car_meshes(id, &size);
	if (meshes->data != NULL && !hires_shape3d_decode_car_meshes(meshes, size, classics)) {
		hires_shape3d_clear_car_meshes(meshes);
	}
}

void hires_shape3d_free_car_meshes(void)
{
	for (legacy_u16 car = 0; car < HIRES_MESH_CAR_COUNT; car++) {
		hires_shape3d_clear_car_meshes(&hires_car_meshes[car]);
	}
	memset(hires_car_meshes, 0, sizeof(hires_car_meshes));
}

struct SHAPE3D *hires_shape3d_car_mesh(const struct SHAPE3D *classic)
{
	if (classic != NULL) {
		for (legacy_u16 car = 0; car < HIRES_MESH_CAR_COUNT; car++) {
			for (legacy_u16 index = 0; index < HIRES_MESH_CAR_SHAPE_COUNT; index++) {
				if (hires_car_meshes[car].classics[index] == classic) {
					return &hires_car_meshes[car].meshes[index];
				}
			}
		}
	}
	return (struct SHAPE3D *)classic;
}

const struct SHAPE3D_HIRES_VECTOR *hires_shape3d_car_vertices(const struct SHAPE3D *shape)
{
	if (shape != NULL) {
		for (legacy_u16 car = 0; car < HIRES_MESH_CAR_COUNT; car++) {
			for (legacy_u16 index = 0; index < HIRES_MESH_CAR_SHAPE_COUNT; index++) {
				const struct HIRES_CAR_MESHES *meshes = &hires_car_meshes[car];
				if (meshes->classics[index] != NULL &&
					(shape == meshes->classics[index] || shape == &meshes->meshes[index])) {
					return meshes->vertices[index];
				}
			}
		}
	}
	return NULL;
}

void hires_shape3d_update_car_mesh(const struct SHAPE3D *classic)
{
	struct SHAPE3D *mesh = hires_shape3d_car_mesh(classic);
	if (mesh != classic) {
		memcpy(mesh->shape3d_vertex_bytes + HIRES_MESH_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
			   classic->shape3d_vertex_bytes + HIRES_MESH_FIRST_WHEEL_VERTEX * SHAPE3D_VERTEX_SIZE,
			   HIRES_MESH_WHEEL_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE);
		for (legacy_u16 car = 0; car < HIRES_MESH_CAR_COUNT; car++) {
			for (legacy_u16 index = 0; index < HIRES_MESH_CAR_SHAPE_COUNT; index++) {
				if (&hires_car_meshes[car].meshes[index] == mesh &&
					hires_car_meshes[car].vertices[index] != NULL) {
					hires_shape3d_copy_native_vertices(mesh, hires_car_meshes[car].vertices[index]);
					return;
				}
			}
		}
	}
}
#endif
