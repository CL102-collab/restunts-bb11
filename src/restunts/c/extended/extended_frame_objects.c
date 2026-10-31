#include "extended_frame_internal.h"

/* Use the same authored edge correction for the graded mesh and its cutout. */
void frame_extended_join_object_grade_vertex(struct VECTOR *point,
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

void frame_extended_rotate_vertex(struct VECTOR *point, legacy_s16 yaw)
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
void frame_extended_join_hill_vertex(struct VECTOR *point, legacy_u8 edges)
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

legacy_u8 frame_extended_grade_edges(const struct CSTRK_OBJECT *object)
{
	return object->slope_x ? FRAME_EXTENDED_HILL_EAST | FRAME_EXTENDED_HILL_WEST
						   : FRAME_EXTENDED_HILL_NORTH | FRAME_EXTENDED_HILL_SOUTH;
}

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

legacy_u16 frame_extended_queue_hill_shape(struct TRANSFORMEDSHAPE3D *instance, legacy_u8 edges)
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

/* Hill-road resources contain their original grass embankments. The terrain
 * pass supplies that same native surface with this track's independent material
 * and Shore paint, so retaining these faces would paint grass over dirt or ice. */
void frame_extended_remove_hill_grass(struct SHAPE3D *shape)
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

struct TRANSFORMEDSHAPE3D frame_extended_object_instance(const struct FRAME_EXTENDED_OBJECT *object,
														 struct SHAPE3D *shape)
{
	struct TRANSFORMEDSHAPE3D instance = {0};
	instance.pos = object->position;
	instance.shapeptr = shape;
	instance.rectptr = &frame_sorted_shapes_rect;
	instance.culling_distance = FRAME_SINGLE_TILE_TRANSFORM_DISTANCE;
	instance.ts_flags = object->flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
	return instance;
}

#if defined(RESTUNTS_SDL3)
legacy_u8 frame_extended_upright_scenery(legacy_s16 physical_model)
{
	return supersight_enabled &&
		   (physical_model == PHYSICAL_MODEL_PALM_TREE || physical_model == PHYSICAL_MODEL_CACTUS ||
			physical_model == PHYSICAL_MODEL_PINE_TREE);
}
#endif

#if defined(RESTUNTS_SDL3)
struct VECTOR frame_extended_scenery_anchor(const struct SHAPE3D *source,
											const struct TRACKOBJECT *definition,
											const struct CSTRK_OBJECT *object)
{
	/* Both detail levels use the uphill extent of the full mesh, including its
	 * enclosing box. This keeps the crown rigid, clear of the terrain, and at
	 * the same height when the renderer changes detail. */
	const struct SHAPE3D *full = definition->ss_shapePtr ? definition->ss_shapePtr : source;
	struct VECTOR anchor = {0};
	legacy_s32 maximum = 0;
	for (legacy_u16 vertex = 0; vertex < full->shape3d_numverts; vertex++) {
		struct VECTOR point;
		shape3d_vertex_read(full, vertex, &point);
		frame_extended_rotate_vertex(&point, definition->ss_rotY);
		legacy_s32 distance =
			(legacy_s32)object->slope_x * point.x - (legacy_s32)object->slope_y * point.z;
		if (vertex == 0 || distance > maximum) {
			maximum = distance;
			anchor = point;
		}
	}
	return anchor;
}
#endif

void frame_extended_apply_object_grade(struct SHAPE3D *mesh, const struct CSTRK_OBJECT *object,
									   const struct VECTOR *upright_anchor)
{
	/* Raise the structure and overlay along the incline, retaining their
	 * authored height above the road. */
	for (legacy_u16 vertex = 0; vertex < mesh->shape3d_numverts; vertex++) {
		struct VECTOR point;
		shape3d_vertex_read(mesh, vertex, &point);
		frame_extended_join_object_grade_vertex(&point, object);
		/* These plants author their roots at y=0. Only the roots follow the
		 * incline; a common lift preserves the crown and extends the trunk. */
		const struct VECTOR *support = upright_anchor && point.y ? upright_anchor : &point;
		point.y = LEGACY_S16_WRAP_ADD(point.y,
									  frame_extended_object_grade(object, support->x, support->z));
		shape3d_vertex_write(mesh, vertex, &point);
	}
}

#if defined(RESTUNTS_SDL3)
legacy_u16 frame_extended_queue_precise_object(struct TRANSFORMEDSHAPE3D *instance,
											   const struct CSTRK_OBJECT *object,
											   const struct SHAPE3D *source,
											   const struct VECTOR *upright_anchor)
{
	const struct SHAPE3D *mesh = instance->shapeptr;
	if (mesh->shape3d_numverts > LEGACY_U8_MAX) {
		return shape3d_transform_and_queue(instance);
	}
	struct SHAPE3D_HIRES_VECTOR vertices[LEGACY_U8_MAX];
	for (legacy_u16 vertex = 0; vertex < mesh->shape3d_numverts; vertex++) {
		struct VECTOR point;
		shape3d_vertex_read(mesh, vertex, &point);
		const struct VECTOR *support = &point;
		if (upright_anchor) {
			struct VECTOR authored;
			shape3d_vertex_read(source, vertex, &authored);
			if (authored.y) {
				support = upright_anchor;
			}
		}
		legacy_f64 x = object->width * FRAME_EXTENDED_TERRAIN_HALF_TILE + support->x;
		legacy_f64 y = object->height * FRAME_EXTENDED_TERRAIN_HALF_TILE - support->z;
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
			point.x, point.y - frame_extended_object_grade(object, support->x, support->z) + grade,
			point.z};
	}
	return shape3d_hires_transform_and_queue(instance, vertices);
}
#endif

legacy_s16 frame_extended_draw_object_mesh(const struct FRAME_EXTENDED_OBJECT *object,
										   const struct TRACKOBJECT *definition,
										   const struct SHAPE3D *source, legacy_u8 surface)
{
	struct SHAPE3D mesh;
	if (!extended_geometry_transform_shape(source, &mesh, definition->ss_rotY, surface,
										   object->definition->ss_physicalModel)) {
		return 0;
	}
	const struct VECTOR *upright_anchor = NULL;
#if defined(RESTUNTS_SDL3)
	struct VECTOR anchor;
	if (object->graded && frame_extended_upright_scenery(object->definition->ss_physicalModel)) {
		anchor = frame_extended_scenery_anchor(source, definition, object->object);
		upright_anchor = &anchor;
	}
#endif
	if (object->graded) {
		frame_extended_apply_object_grade(&mesh, object->object, upright_anchor);
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
		result = (legacy_s16)frame_extended_queue_precise_object(&instance, object->object, source,
																 upright_anchor);
	} else
#endif
	{
		result = frame_extended_queue_hill_shape(&instance, object->edges);
	}
	extended_geometry_free_shape(&mesh);
	return result > 0 && supersight_enabled != 0;
}

legacy_s16 frame_extended_draw_object_part(const struct FRAME_EXTENDED_OBJECT *object,
										   const struct TRACKOBJECT *definition, legacy_u8 overlay)
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

legacy_s16 frame_extended_draw_object(const struct CSTRK_TRACK *track,
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
	if (model->roll_ramp) {
		object.flags |= SHAPE3D_BLACK_GRILLE_FLAG;
		struct SHAPE3D source;
		if (!roll_ramp_create_mesh(&source, model->surface)) {
			return 0;
		}
		struct TRACKOBJECT launch = *definition;
		launch.ss_rotY = roll_ramp_orientation(model);
		/* The generated mesh already selects its approach surface. Keep its
		 * grille and yellow markings out of the native corkscrew repaint map. */
		legacy_s16 result =
			frame_extended_draw_object_mesh(&object, &launch, &source, CSTRK_ROAD_PAVED);
		roll_ramp_free_mesh(&source);
		return result;
	}
	if (model->tunnel_slalom) {
		/* Reuse the tunnel shell and shaded floor, adding only the slalom's
		 * barriers. Its ordinary road overlay would duplicate the floor. */
		struct TRACKOBJECT tunnel = trkObjectList[CSTRK_TUNNEL_FIRST_ID];
		struct TRACKOBJECT floor = *frame_track_object_from_legacy_index(tunnel.ss_ssOvelay);
		tunnel.ss_rotY = definition->ss_rotY;
		floor.ss_rotY = definition->ss_rotY;
		/* The interior floor has no authored low-detail alternative. */
		if (!floor.ss_loShapePtr) {
			floor.ss_loShapePtr = floor.ss_shapePtr;
		}
		if (frame_extended_draw_object_part(&object, &tunnel, 0) ||
			frame_extended_draw_object_part(&object, definition, 0) ||
			frame_extended_draw_object_part(&object, &floor, 1)) {
			return 1;
		}
		return 0;
	}
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

void extended_frame_draw_preview_tile(legacy_s16 column, legacy_s16 row)
{
	struct FRAME_TILE tile = {0};
	tile.east = (legacy_s8)column;
	tile.south = (legacy_s8)row;
	tile.detail = FRAME_TILE_DETAIL_FULL;
	struct FRAME_CAMERA camera = {0};
	camera.position.x = track_column_centers[column];
	camera.position.z = track_row_centers[row];
	extended_frame_draw_terrain(&tile, &camera, 0);

	/* Look up nearby origins instead of scanning the entire object list for
	 * every overview cell. Free footprint corners can have another owner. */
	const struct CSTRK_TRACK *track = extended_track_model();
	const struct CSTRK_OBJECT *objects[EXTENDED_TRACK_CELL_OBJECT_MAX];
	legacy_u16 count =
		extended_track_objects_in_cell(column, row, objects, EXTENDED_TRACK_CELL_OBJECT_MAX);
	for (legacy_u16 index = 0; index < count; index++) {
		const struct CSTRK_OBJECT *object = objects[index];
		if (object->x == column && object->y == row) {
			legacy_u32 id = extended_track_object_id(object);
			if (id < TRACK_OBJECT_COUNT) {
				frame_extended_draw_object(track, object, &trkObjectList[id], &tile, &camera, 0);
			}
		}
	}
}
