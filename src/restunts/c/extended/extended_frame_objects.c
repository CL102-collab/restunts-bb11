#include "extended_frame_internal.h"
#include "pine_trees.h"
#include "wheat_field.h"

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

/* Each tree owns its full-detail anchor. A shared grove anchor would lift
 * every crown to the uphill tree and stretch the downhill trunks. */
static legacy_s16 frame_extended_draw_pine_trees(const struct FRAME_EXTENDED_OBJECT *object)
{
	const struct TRACKOBJECT *native = object->definition;
	for (legacy_u8 tree = 0; tree < object->object->pine_count; tree++) {
		struct SHAPE3D full, detail;
		if (!pine_trees_create_mesh(native->ss_shapePtr, &full, object->object, tree)) {
			continue;
		}
		const struct SHAPE3D *source = &full;
		legacy_u8 low_detail = object->detail && native->ss_loShapePtr;
		if (low_detail) {
			if (!pine_trees_create_mesh(native->ss_loShapePtr, &detail, object->object, tree)) {
				extended_geometry_free_shape(&full);
				continue;
			}
			source = &detail;
		}
		struct TRACKOBJECT definition = *native;
		definition.ss_shapePtr = &full;
		definition.ss_rotY = 0;
		legacy_s16 result =
			frame_extended_draw_object_mesh(object, &definition, source, CSTRK_ROAD_PAVED);
		if (low_detail) {
			extended_geometry_free_shape(&detail);
		}
		extended_geometry_free_shape(&full);
		if (result) {
			return result;
		}
	}
	return 0;
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
					   ? hires_frame_extended_hill_edges(track, model->x, model->y)
					   : 0;
	object.detail = tile->detail;
	object.flags = flags;
	if (model->legacy_id == CSTRK_WHEAT_FIELD_ID) {
		struct SHAPE3D source;
		if (!wheat_field_create_mesh(&source)) {
			return 0;
		}
		struct TRACKOBJECT wheat = *definition;
		wheat.ss_rotY = model->rotation * ANGLE_QUARTER_TURN;
		legacy_s16 result =
			frame_extended_draw_object_mesh(&object, &wheat, &source, CSTRK_ROAD_PAVED);
		extended_geometry_free_shape(&source);
		return result;
	}
	if (model->pine_count) {
		return frame_extended_draw_pine_trees(&object);
	}
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

legacy_s16 frame_extended_draw_object_mesh(const struct FRAME_EXTENDED_OBJECT *object,
										   const struct TRACKOBJECT *definition,
										   const struct SHAPE3D *source, legacy_u8 surface)
{
	struct SHAPE3D mesh;
	if (!extended_geometry_transform_shape(source, &mesh, definition->ss_rotY, surface,
										   object->definition->ss_physicalModel)) {
		return 0;
	}
	struct VECTOR anchor;
	const struct VECTOR *upright_anchor =
		hires_track_object_anchor(object, definition, source, &anchor);
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
	if (!hires_track_queue_object(&instance, object, source, upright_anchor, &result)) {
		result = frame_extended_queue_hill_shape(&instance, object->edges);
	}
	extended_geometry_free_shape(&mesh);
	return hires_track_queue_exhausted(result);
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
		return hires_track_queue_exhausted(
			(legacy_s16)frame_extended_queue_hill_shape(&instance, object->edges));
	}
	return frame_extended_draw_object_mesh(object, definition, source, surface);
}

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

legacy_u16 frame_extended_queue_hill_shape(struct TRANSFORMEDSHAPE3D *instance, legacy_u8 edges)
{
	legacy_u16 result;
	if (hires_track_queue_hill_shape(instance, edges, &result)) {
		return result;
	}
	return shape3d_transform_and_queue(instance);
}
