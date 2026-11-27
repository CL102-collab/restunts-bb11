#ifdef RESTUNTS_MULTI_REPLAY
#include "frame_render_private.h"

#include <stdlib.h>
#include <string.h>
#include "../hires/shape3d_car_ground.h"
#include "../hires/shape3d_mesh.h"

struct FRAME_REPLAY_MODEL {
	struct REPLAY_CAR_SHAPES shapes;
	struct TRACKOBJECT track_object;
	legacy_s8 id[REPLAY_CAR_ID_SIZE];
	legacy_s16 ground_offset;
};

struct FRAME_REPLAY_CAR {
	const struct MULTI_REPLAY_CAR *car;
	struct FRAME_REPLAY_MODEL *model;
	struct FRAME_CAR_RENDER placement;
	legacy_u8 drawn;
};

struct FRAME_REPLAY_QUEUE_ENTRY {
	struct FRAME_REPLAY_CAR *render;
	struct VECTOR position;
	legacy_s16 depth;
	legacy_s8 detail;
	legacy_u8 flags;
};

static struct FRAME_REPLAY_MODEL *models;
static size_t model_count;
static struct FRAME_REPLAY_CAR *render_cars;
static size_t render_car_count;
static struct FRAME_REPLAY_QUEUE_ENTRY *queue;
static size_t queue_count, queue_cursor;
static struct MULTI_REPLAY_CAR *interpolated_cars;
static legacy_u8 interpolating;

void frame_multi_replay_begin_interpolation(legacy_u32 lag)
{
	multi_replay_interpolate(lag, interpolated_cars);
	interpolating = 1;
}

void frame_multi_replay_end_interpolation(void)
{
	interpolating = 0;
}

static const struct MULTI_REPLAY_CAR *frame_multi_replay_car(size_t index)
{
	return interpolating ? &interpolated_cars[index] : multi_replay_car(index);
}

const struct MULTI_REPLAY_CAR *frame_viewed_multi_replay_car(void)
{
	return multi_replay_is_active() ? frame_multi_replay_car(multi_replay_target_index()) : NULL;
}

void frame_multi_replay_free(void)
{
	hires_shape3d_free_replay_car_meshes();
	for (size_t index = 0; index < model_count; index++) {
		free(models[index].shapes.resource);
	}
	free(models);
	free(render_cars);
	free(queue);
	free(interpolated_cars);
	interpolated_cars = NULL;
	interpolating = 0;
	models = NULL;
	render_cars = NULL;
	queue = NULL;
	model_count = render_car_count = queue_count = queue_cursor = 0;
}

static struct FRAME_REPLAY_MODEL *frame_multi_replay_load_model(const legacy_s8 *id)
{
	for (size_t index = 0; index < model_count; index++) {
		if (memcmp(models[index].id, id, REPLAY_CAR_ID_SIZE) == 0) {
			return &models[index];
		}
	}
	struct FRAME_REPLAY_MODEL *model = &models[model_count];
	if (!shape3d_load_replay_car_shapes(&model->shapes, id)) {
		return NULL;
	}
	memcpy(model->id, id, REPLAY_CAR_ID_SIZE);
	model->track_object.ss_shapePtr = &model->shapes.wheel;
	model->track_object.ss_loShapePtr = &model->shapes.low;
	const struct SHAPE3D *meshes[HIRES_MESH_CAR_SHAPE_COUNT] = {
		&model->shapes.low, &model->shapes.wheel, &model->shapes.high};
	hires_shape3d_load_replay_car_meshes(model_count, id, meshes);
	model->ground_offset = shape3d_hires_neutral_car_ground_offset(&model->shapes.wheel);
	model_count++;
	return model;
}

legacy_s16 frame_multi_replay_load(void)
{
	size_t count = multi_replay_car_count();
	models = calloc(count, sizeof(*models));
	render_cars = calloc(count, sizeof(*render_cars));
	queue = calloc(count, sizeof(*queue));
	interpolated_cars = calloc(count, sizeof(*interpolated_cars));
	if (models == NULL || render_cars == NULL || queue == NULL || interpolated_cars == NULL ||
		!hires_shape3d_allocate_replay_car_meshes(count)) {
		frame_multi_replay_free();
		return 0;
	}
	for (size_t index = 0; index < count; index++) {
		const struct MULTI_REPLAY_CAR *car = multi_replay_car(index);
		struct FRAME_REPLAY_MODEL *model = frame_multi_replay_load_model(car->id);
		if (model == NULL) {
			frame_multi_replay_free();
			return 0;
		}
		render_cars[index].model = model;
	}
	render_car_count = count;
	return 1;
}

void frame_multi_replay_place_cars(const struct FRAME_TILE_SELECTION *tiles)
{
	for (size_t index = 0; index < render_car_count; index++) {
		struct FRAME_REPLAY_CAR *render = &render_cars[index];
		render->car = frame_multi_replay_car(index);
		render->placement.east = -1;
		render->drawn = 0;
		render->placement.explosion_visible = 0;
		const struct MULTI_REPLAY_CAR *car = render->car;
		if (car == NULL || car->state.car_crashBmpFlag == CRASH_EVENT_WATER ||
			(cameramode == CAMERA_MODE_COCKPIT && index == multi_replay_target_index())) {
			continue;
		}
		render->placement.depth_adjustment = frame_find_car_wheel(
			&car->state, &car->simd, tiles, &render->placement.east, &render->placement.south);
	}
}

static legacy_s16 frame_multi_replay_in_tile(const struct FRAME_TILE *tile,
											 const struct FRAME_REPLAY_CAR *render)
{
	const struct FRAME_CAR_RENDER *placement = &render->placement;
	return !render->drawn && placement->east >= 0 &&
		   (placement->east == tile->east || placement->east == tile->last_east) &&
		   (placement->south == tile->south || placement->south == tile->last_south);
}

legacy_s16 frame_multi_replay_tile_has_car(const struct FRAME_TILE *tile)
{
	for (size_t index = 0; index < render_car_count; index++) {
		if (frame_multi_replay_in_tile(tile, &render_cars[index])) {
			return 1;
		}
	}
	return 0;
}

static legacy_int frame_multi_replay_compare(const void *left, const void *right)
{
	const struct FRAME_REPLAY_QUEUE_ENTRY *first = left;
	const struct FRAME_REPLAY_QUEUE_ENTRY *second = right;
	if (first->depth != second->depth) {
		return (legacy_int)second->depth - first->depth;
	}
	return first->render < second->render ? -1 : first->render != second->render;
}

void frame_multi_replay_prepare_tile(const struct FRAME_TILE *tile,
									 const struct FRAME_CAMERA *camera, legacy_s8 flags)
{
	queue_count = queue_cursor = 0;
	for (size_t index = 0; index < render_car_count; index++) {
		struct FRAME_REPLAY_CAR *render = &render_cars[index];
		if (!frame_multi_replay_in_tile(tile, render)) {
			continue;
		}
		render->drawn = 1;
		const struct MULTI_REPLAY_CAR *car = render->car;
		legacy_s8 detail = tile->detail;
		if (frame_hires_adaptive_active != 0) {
			legacy_u8 adaptive_flags = frame_hires_adaptive_car_flags(&car->state, &car->simd);
			if (adaptive_flags == FRAME_HIRES_ADAPTIVE_HIDE) {
				continue;
			}
			detail = adaptive_flags == FRAME_HIRES_ADAPTIVE_LOW_GEOMETRY;
		}
		struct FRAME_REPLAY_QUEUE_ENTRY *entry = &queue[queue_count++];
		entry->render = render;
		entry->detail = detail;
		entry->flags = (legacy_u8)flags;
		entry->position.x = frame_relative_position(car->state.car_position.lx, camera->position.x);
		entry->position.y = frame_relative_position(car->state.car_position.ly, camera->position.y);
		entry->position.z = frame_relative_position(car->state.car_position.lz, camera->position.z);
		if (frame_renderer_uses_screen_depth()) {
			struct VECTOR offset = frame_hires_car_render_offset_with_ground(
				&car->state, render->model->ground_offset);
			entry->position.x = LEGACY_S16_WRAP_ADD(entry->position.x, offset.x);
			entry->position.y = LEGACY_S16_WRAP_ADD(entry->position.y, offset.y);
			entry->position.z = LEGACY_S16_WRAP_ADD(entry->position.z, offset.z);
			entry->flags |= SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
			entry->depth = 0;
		} else {
			struct VECTOR view;
			mat_mul_vector(&entry->position, &mat_temp, &view);
			entry->depth =
				LEGACY_S16_WRAP_ADD(view.z, render->placement.depth_adjustment & tile->depth_mask);
		}
	}
	if (queue_count > 1U && !frame_renderer_uses_screen_depth()) {
		qsort(queue, queue_count, sizeof(*queue), frame_multi_replay_compare);
	}
}

static legacy_s16 frame_multi_replay_draw(struct FRAME_REPLAY_QUEUE_ENTRY *entry)
{
	struct FRAME_REPLAY_CAR *render = entry->render;
	struct FRAME_REPLAY_MODEL *model = render->model;
	struct REPLAY_CAR_SHAPES *shapes = &model->shapes;
	const struct MULTI_REPLAY_CAR *car = render->car;
	struct TRANSFORMEDSHAPE3D instance = {0};
	instance.pos = entry->position;
	instance.material = (legacy_u8)car->material;
	instance.culling_distance = FRAME_CAR_TRANSFORM_DISTANCE;
	instance.rotvec.x = LEGACY_S16_WRAP_NEGATE(car->state.car_rotate.z);
	instance.rotvec.y = LEGACY_S16_WRAP_NEGATE(car->state.car_rotate.y);
	instance.rotvec.z = LEGACY_S16_WRAP_NEGATE(car->state.car_rotate.x);

	/* Share classic/hi-res model selection and wheel animation with other cars. */
	frame_prepare_car_model(&instance, &model->track_object, &car->state, &shapes->wheel,
							shapes->wheel_state, shapes->vertices, shapes->centers, entry->detail);
	render->placement.crash_rect = empty_rect;
	frame_prepare_car_clipping(&instance, &car->state, &render->placement.crash_rect,
							   &render->placement.crash_rect);
	instance.ts_flags |= entry->flags & SHAPE3D_NO_SHADOW_RECEIVE_FLAG;

	legacy_s8 saved_brakes = backlights_paint_override;
	backlights_paint_override =
		car->state.car_is_braking ? BACKLIGHT_PAINT_BRAKING : BACKLIGHT_PAINT_NORMAL;
	legacy_s16 result = shape3d_transform_and_queue(&instance);
	backlights_paint_override = saved_brakes;
	render->placement.explosion_visible =
		result == 0 && car->state.car_crashBmpFlag == CRASH_EVENT_COLLISION;
	if (slow_video_mgmt_copy != 0) {
		rect_union(&frame_player_car_rect, &render->placement.crash_rect, &frame_player_car_rect);
	}
	return result > 0;
}

legacy_s16 frame_multi_replay_draw_before(legacy_s32 depth)
{
	while (queue_cursor < queue_count && queue[queue_cursor].depth > depth) {
		if (frame_multi_replay_draw(&queue[queue_cursor++])) {
			return 1;
		}
	}
	return 0;
}

void frame_multi_replay_draw_explosions(struct RECTANGLE *cliprect)
{
	for (size_t index = 0; index < render_car_count; index++) {
		struct FRAME_REPLAY_CAR *render = &render_cars[index];
		if (render->drawn && render->placement.explosion_visible) {
			legacy_s16 variant = (render->car->camera.frame >> FRAME_EXPLOSION_FRAME_SHIFT) %
								 FRAME_EXPLOSION_VARIANT_COUNT;
			frame_draw_explosion(&render->placement.crash_rect, cliprect, variant);
		}
	}
}

void frame_multi_replay_add_shadows(const struct FRAME_CAMERA *camera)
{
	for (size_t index = 0; index < render_car_count; index++) {
		const struct MULTI_REPLAY_CAR *car = frame_multi_replay_car(index);
		frame_hires_add_car_shadow(&car->state, &car->simd, &camera->position,
								   &render_cars[index].model->shapes.wheel);
	}
}
#endif
