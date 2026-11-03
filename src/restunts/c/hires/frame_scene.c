#if defined(RESTUNTS_SDL3)
#include "../frame/frame_render_private.h"
#include "render_workers.h"
#include "shape3d_queue.h"

#define HIRES_GEOMETRY_GROUP_TILES 8

struct FRAME_HIRES_GEOMETRY_JOB {
	legacy_s16 first, count, result;
	legacy_u8 dynamic;
};

struct FRAME_HIRES_GEOMETRY_FRAME {
	const struct FRAME_TILE_SELECTION *selection;
	const struct FRAME_CAMERA *camera;
	struct FRAME_CAR_RENDER *cars;
	legacy_s8 flags;
	legacy_s16 pass, result;
};

/* The caller owns this staging data until every synchronous geometry job joins. */
static struct FRAME_TILE hires_geometry_tiles[FRAME_MAXIMUM_TILE_COUNT];
static struct FRAME_HIRES_GEOMETRY_JOB hires_geometry_jobs[FRAME_MAXIMUM_TILE_COUNT];

static legacy_u8 frame_hires_dynamic_tile(const struct FRAME_TILE *tile,
										  const struct FRAME_CAR_RENDER *cars)
{
	for (legacy_s32 car = PLAYER_CAR_INDEX; car <= OPPONENT_CAR_INDEX; car++) {
		if (cars[car].east == tile->east && cars[car].south == tile->south) {
			return 1;
		}
	}
	return (frame_state->game_inputmode == GAME_INPUT_MODE_WAITING &&
			tile->east == start_finish_column && tile->south == start_finish_row) ||
		   frame_has_racing_line(tile);
}

static void frame_hires_build_geometry(void *argument, legacy_s32 index)
{
	const struct FRAME_HIRES_GEOMETRY_FRAME *frame = argument;
	struct FRAME_HIRES_GEOMETRY_JOB *job = &hires_geometry_jobs[index];
	for (legacy_s16 offset = 0; offset < job->count; offset++) {
		struct FRAME_TILE *tile = &hires_geometry_tiles[job->first + offset];
		if (frame_draw_tile_background(tile, frame->selection, frame->camera, frame->flags,
									   FRAME_TILE_GROUND_PASS, frame->pass)) {
			job->result = 1;
			return;
		}
		if (frame->pass == FRAME_TILE_OBJECT_PASS &&
			(extended_frame_front_fences(tile, frame->selection, frame->camera, frame->flags) ||
			 extended_frame_draw_objects(tile, frame->camera, frame->flags))) {
			job->result = 1;
			return;
		}
	}
}

static void frame_hires_join_geometry(void *argument, legacy_s32 index)
{
	struct FRAME_HIRES_GEOMETRY_FRAME *frame = argument;
	const struct FRAME_HIRES_GEOMETRY_JOB *job = &hires_geometry_jobs[index];
	frame->result |= job->result;
	if (frame->result || frame->pass != FRAME_TILE_OBJECT_PASS) {
		return;
	}
	transformedshape_counter = 0;
	curtransshape_ptr = currenttransshape;
	if (!job->dynamic) {
		return;
	}
	/* End a group at each dynamic tile so its serial car, flag and ghost line
	 * remain between the same two pieces of immutable track geometry. */
	const struct FRAME_TILE *tile = &hires_geometry_tiles[job->first + job->count - 1];
	frame_add_tile_cars(tile, frame->camera, frame->cars, frame->flags);
	frame_add_start_flag(tile, frame->camera, frame->flags);
	frame->result |= frame_draw_sorted_shapes(frame->cars, tile, frame->camera, frame->flags);
}

static legacy_s16 frame_hires_draw_parallel_tiles(const struct FRAME_TILE_SELECTION *selection,
												  const struct FRAME_CAMERA *camera,
												  struct FRAME_CAR_RENDER *cars, legacy_s8 flags)
{
	struct FRAME_HIRES_GEOMETRY_FRAME frame = {
		selection, camera, cars, flags, FRAME_TILE_GROUND_PASS, 0};
	for (; frame.pass <= FRAME_TILE_OBJECT_PASS && !frame.result; frame.pass++) {
		legacy_s16 tile_count = 0;
		legacy_s32 job_count = 0;
		legacy_s16 group_first = 0;
		for (legacy_s16 index = selection->first; index < selection->count; index++) {
			if (selection->markers[index] != FRAME_TILE_DRAW_MARKER ||
				!frame_prepare_draw_tile(&hires_geometry_tiles[tile_count], selection, index)) {
				continue;
			}
			struct FRAME_TILE *tile = &hires_geometry_tiles[tile_count++];
			tile->last_east = tile->east;
			tile->last_south = tile->south;
			legacy_u8 dynamic =
				frame.pass == FRAME_TILE_OBJECT_PASS && frame_hires_dynamic_tile(tile, cars);
			if (dynamic || tile_count - group_first == HIRES_GEOMETRY_GROUP_TILES) {
				hires_geometry_jobs[job_count++] = (struct FRAME_HIRES_GEOMETRY_JOB){
					group_first, tile_count - group_first, 0, dynamic};
				group_first = tile_count;
			}
		}
		if (group_first != tile_count) {
			hires_geometry_jobs[job_count++] =
				(struct FRAME_HIRES_GEOMETRY_JOB){group_first, tile_count - group_first, 0, 0};
		}
		shape3d_hires_geometry_run(job_count, frame_hires_build_geometry, frame_hires_join_geometry,
								   &frame);
	}
	return frame.result;
}

void frame_hires_extend_lookahead(struct FRAME_TILE_SELECTION *tiles,
								  const struct FRAME_CAMERA *camera)
{
	hires_extended_frame_extend_lookahead(tiles, camera, frame_hires_adaptive_active);
}

void frame_hires_reset(void)
{
	frame_hires_adaptive_restart(&frame_hires_adaptive);
	frame_hires_adaptive_active = 0;
}

void frame_hires_draw_selected(struct FRAME_TILE_SELECTION *tiles, struct FRAME_CAMERA *camera,
							   struct FRAME_CAR_RENDER *cars, legacy_s8 buffer_index,
							   legacy_s8 redraw_transform_flags, legacy_s8 animated_material)
{
	(void)buffer_index;
	/* Full quality bypasses the adaptive policy; reductions apply only to
	 * isolated presentation snapshots and never to authoritative rendering. */
	tiles->detail_threshold = FRAME_TILE_DETAIL_FULL + 1;
	frame_place_cars(tiles, cars);
	legacy_s16 result;
	if (extended_track_active() && hires_render_workers_count() != 0) {
		result = frame_hires_draw_parallel_tiles(tiles, camera, cars, redraw_transform_flags);
	} else {
		result = frame_draw_tiles(tiles, camera, cars, redraw_transform_flags, animated_material);
	}
	if (result == 0 && frame_hires_adaptive_active != 0) {
		frame_hires_draw_adaptive_particles(camera, cars, redraw_transform_flags);
	}
}

void frame_hires_prepare_tiles(struct FRAME_TILE_SELECTION *tiles,
							   const struct FRAME_CAMERA *camera)
{
	if (frame_hires_adaptive_active != 0) {
		frame_hires_adaptive_prepare(&frame_hires_adaptive, tiles->camera_east, tiles->camera_south,
									 cos_fast(camera->view_heading),
									 sin_fast(camera->view_heading));
	}
	if (hires_active != 0) {
		frame_hires_extend_lookahead(tiles, camera);
	}
}

#endif
