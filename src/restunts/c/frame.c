#include "frame_render_private.h"

const struct GAMESTATE *frame_state = &state;

/*
 * In the original dseg, terrain_scene_objects immediately follows trkObjectList.
 * Some track objects store overlay indices into that combined legacy table,
 * so indices beyond trkObjectList intentionally address terrain_scene_objects.
 */
struct TRACKOBJECT *frame_track_object_from_legacy_index(legacy_u8 index)
{
	if (index < TRACK_OBJECT_COUNT) {
		return &trkObjectList[index];
	}
	return &terrain_scene_objects[(legacy_u16)index - TRACK_OBJECT_COUNT];
}

void transformed_shape_add_for_sort(legacy_s16 z_adjust, legacy_s16 type)
{
	legacy_s16 index = LEGACY_S8_FROM_BITS((legacy_u8)transformedshape_counter);
	if (frame_hires_sort_depth()) {
		struct VECTOR transformed_position;
		mat_mul_vector(&curtransshape_ptr->pos, &mat_temp, &transformed_position);
		transformedshape_zarray[index] = LEGACY_S16_WRAP_ADD(transformed_position.z, z_adjust);
	}
	transformed_shape_sort_types[index] = (legacy_s8)(legacy_u8)type;
	transformedshape_indices[index] = index;
	transformedshape_counter = LEGACY_S8_WRAP_ADD(transformedshape_counter, 1);
	curtransshape_ptr++;
}

legacy_s16 frame_relative_position(legacy_s32 position, legacy_s16 camera_position)
{
	return LEGACY_S16_WRAP_SUB(position_to_word(position), camera_position);
}

legacy_s16 frame_relative_position_sum(legacy_s32 first, legacy_s32 second,
									   legacy_s16 camera_position)
{
	return frame_relative_position(LEGACY_S32_WRAP_ADD(first, second), camera_position);
}

legacy_s16 frame_relative_track_position(legacy_s32 offset, legacy_s16 track_position,
										 legacy_s16 camera_position)
{
	return LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(position_to_word(offset), track_position),
							   camera_position);
}

legacy_s8 frame_tile_from_world(legacy_s32 position)
{
	return LEGACY_S8_FROM_BITS((legacy_u8)((legacy_u32)position >> TRACK_WORLD_TILE_SHIFT));
}

legacy_s8 frame_south_tile_from_world(legacy_s32 position)
{
	return LEGACY_S8_WRAP_SUB(TRACK_GRID_LAST_COORDINATE, frame_tile_from_world(position));
}

legacy_s8 frame_tile_from_world_offset(legacy_s32 position, legacy_s16 offset)
{
	return frame_tile_from_world(LEGACY_S32_WRAP_ADD_S16(position, offset));
}

legacy_s8 frame_south_tile_from_world_offset(legacy_s32 position, legacy_s16 offset)
{
	return LEGACY_S8_WRAP_SUB(TRACK_GRID_LAST_COORDINATE,
							  frame_tile_from_world_offset(position, offset));
}

void frame_prepare_flat_track_shape(struct TRANSFORMEDSHAPE3D *shape, legacy_s8 tile_east,
									legacy_s8 tile_south, const struct VECTOR *camera_position,
									legacy_s16 flags, legacy_s16 rotation)
{
	shape->pos.x = LEGACY_S16_WRAP_SUB(track_column_centers[tile_east], camera_position->x);
	shape->pos.y = LEGACY_S16_WRAP_NEGATE(camera_position->y);
	shape->pos.z = LEGACY_S16_WRAP_SUB(track_row_centers[tile_south], camera_position->z);
	shape->rectptr = &frame_unsorted_shapes_rect;
	shape->ts_flags = flags;
	shape->rotvec.x = 0;
	shape->rotvec.y = 0;
	shape->rotvec.z = rotation;
	shape->culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
	shape->material = 0;
}

void init_rect_arrays(void)
{
	if (slow_video_mgmt_copy == 0) {
		return;
	}

	frame_rects_page0[0] = full_screen_rect;
	frame_rects_page1[0] = full_screen_rect;
	for (legacy_s16 i = 1; i < FRAME_DIRTY_RECT_COUNT; i++) {
		frame_rects_page0[i] = empty_rect;
		frame_rects_page1[i] = empty_rect;
	}
}

void font_set_fontdef2(void far *data)
{
	set_fontdefseg(data);
	font_glyph_height =
		LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE((legacy_u8 far *)data + FRAME_FONT_HEIGHT_OFFSET));
}

void font_set_fontdef(void)
{
	font_set_fontdef2(fontdefptr);
}

void frame_mark_changed_rects(void)
{
	for (legacy_s16 i = 0; i < FRAME_DIRTY_RECT_COUNT; i++) {
		frame_rect_change_flags[i] = FRAME_DIRTY_RECT_CHANGED;
	}
	if (detail_level == FRAME_DETAIL_FASTEST) {
		frame_buffer_camera_headings[1] = last_rendered_camera_heading;
	}
	if (frame_buffer_camera_headings[1] == last_rendered_camera_heading &&
		frame_rects_page0[FRAME_SKYBOX_RECT_INDEX].left ==
			frame_rects_page1[FRAME_SKYBOX_RECT_INDEX].left &&
		frame_rects_page0[FRAME_SKYBOX_RECT_INDEX].right ==
			frame_rects_page1[FRAME_SKYBOX_RECT_INDEX].right &&
		frame_rects_page0[FRAME_SKYBOX_RECT_INDEX].top ==
			frame_rects_page1[FRAME_SKYBOX_RECT_INDEX].top &&
		frame_rects_page0[FRAME_SKYBOX_RECT_INDEX].bottom ==
			frame_rects_page1[FRAME_SKYBOX_RECT_INDEX].bottom) {
		frame_rect_change_flags[FRAME_SKYBOX_RECT_INDEX] = 0;
	}
}

void frame_present(struct RECTANGLE *cliprect)
{
#ifdef RESTUNTS_AMIGA
	if (render_window_sprite == &screen_sprite) {
		/* Native races draw into the indexed screen, already hidden behind
		 * the displayed planar page. Presentation follows dashboard updates. */
		sprite_select_screen_compat();
		mouse_draw_transparent_check();
		if (slow_video_mgmt_copy != 0) {
			frame_buffer_camera_headings[1] = last_rendered_camera_heading;
			for (legacy_s16 i = 0; i < FRAME_DIRTY_RECT_COUNT; i++) {
				frame_rects_page1[i] = frame_rects_page0[i];
			}
		}
		return;
	}
#endif
	if (video_uses_page_flipping != 0) {
		return;
	}

	sprite_select_screen_compat();
	struct RECTANGLE *dirty_rect;
	if (full_redraw_frames_remaining != 0) {
		mouse_draw_opaque_check();
		sprite_putimage(render_window_sprite->sprite_bitmapptr);
	} else if (slow_video_mgmt_copy == 0) {
		sprite_set_target_clip_bounds(cliprect->left, cliprect->right, cliprect->top,
									  cliprect->bottom);
		mouse_draw_opaque_check();
		sprite_putimage(render_window_sprite->sprite_bitmapptr);
	} else {
		frame_mark_changed_rects();

		redraw_rect_count = 0;
		rectlist_add_rects(FRAME_DIRTY_RECT_COUNT, frame_rect_change_flags, frame_rects_page0,
						   frame_rects_page1, cliprect, &redraw_rect_count, merged_redraw_rects);
		if (redraw_rect_count != 0) {
			rect_array_sort_by_top(redraw_rect_count, merged_redraw_rects,
								   redraw_rect_sort_indices);
			mouse_draw_opaque_check();
			for (legacy_s16 i = 0; i < redraw_rect_count; i++) {
				dirty_rect = &merged_redraw_rects[redraw_rect_sort_indices[i]];
				sprite_set_target_clip_bounds(dirty_rect->left, dirty_rect->right, dirty_rect->top,
											  dirty_rect->bottom);
				sprite_putimage(render_window_sprite->sprite_bitmapptr);
			}
		} else {
			sprite_set_target_clip_bounds(0, FRAME_SCREEN_WIDTH, cliprect->top, cliprect->bottom);
			mouse_draw_opaque_check();
			sprite_putimage(render_window_sprite->sprite_bitmapptr);
		}
	}

	mouse_draw_transparent_check();
	if (slow_video_mgmt_copy != 0) {
		frame_buffer_camera_headings[1] = last_rendered_camera_heading;
		for (legacy_s16 i = 0; i < FRAME_DIRTY_RECT_COUNT; i++) {
			frame_rects_page1[i] = frame_rects_page0[i];
		}
	}
}

legacy_s8 frame_begin(legacy_s8 buffer_index)
{
	if (video_uses_page_flipping == 0 || buffer_index == 0) {
		active_frame_rects = frame_rects_page0;
		alternate_frame_rects = frame_rects_page1;
	} else {
		alternate_frame_rects = frame_rects_page0;
		active_frame_rects = frame_rects_page1;
	}

	struct RECTANGLE *redraw_rect;
	legacy_s8 redraw_transform_flags;
	if (slow_video_mgmt_copy != 0) {
		redraw_transform_flags = FRAME_SLOW_VIDEO_TRANSFORM_FLAG;
		redraw_rect = frame_layer_rects;
		for (legacy_s16 rect_index = 0; rect_index < FRAME_DIRTY_RECT_COUNT; rect_index++) {
			*redraw_rect = empty_rect;
			redraw_rect++;
		}
	} else {
		redraw_transform_flags = 0;
	}

	return redraw_transform_flags;
}

legacy_s8 frame_animated_material(void)
{
	legacy_s8 animated_material;

	if (frame_state->game_frame == 0) {
		animated_material =
			track_material_animation[frame_callback_count & FRAME_ANIMATION_PHASE_MASK];
	} else {
		animated_material =
			track_material_animation[frame_state->game_frame & FRAME_ANIMATION_PHASE_MASK];
	}

	return animated_material;
}

void frame_draw_explosions(struct FRAME_CAR_RENDER *cars, struct RECTANGLE *cliprect)
{
	// Draw the three explosion images in successive four-frame phases.
	struct VECTOR offset_vector;
	struct RECTANGLE *redraw_rect;
	for (legacy_s16 car_index = 0; car_index < FRAME_EXPLOSION_CAR_COUNT; car_index++) {
		if (cars[car_index].explosion_visible == 0) {
			continue;
		}
		if (slow_video_mgmt_copy == 0) {
			if (car_index == PLAYER_CAR_INDEX) {
				redraw_rect = &cars[PLAYER_CAR_INDEX].crash_rect;
			} else {
				redraw_rect = &cars[OPPONENT_CAR_INDEX].crash_rect;
			}
		} else {
			if (car_index == PLAYER_CAR_INDEX) {
				redraw_rect = &frame_player_car_rect;
			} else {
				redraw_rect = &frame_opponent_car_rect;
			}
		}

		if (rect_intersect(redraw_rect, cliprect) == 0) {
			sprite_set_target_clip_bounds(redraw_rect->left, redraw_rect->right, redraw_rect->top,
										  redraw_rect->bottom);
			offset_vector.x =
				LEGACY_S16_SAR(LEGACY_S16_WRAP_ADD(redraw_rect->right, redraw_rect->left),
							   FRAME_RECT_CENTER_SHIFT);
			offset_vector.y =
				LEGACY_S16_SAR(LEGACY_S16_WRAP_ADD(redraw_rect->top, redraw_rect->bottom),
							   FRAME_RECT_CENTER_SHIFT);
			legacy_s16 extent = LEGACY_S16_WRAP_SUB(redraw_rect->right, redraw_rect->left);
			legacy_s16 height_or_scale = LEGACY_S16_WRAP_SUB(redraw_rect->bottom, redraw_rect->top);
			if (height_or_scale > extent) {
				extent = height_or_scale;
			}

			legacy_s16 explosion_index =
				LEGACY_S16_SAR(frame_state->game_frame, FRAME_EXPLOSION_FRAME_SHIFT) %
				FRAME_EXPLOSION_VARIANT_COUNT;
			height_or_scale = LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_DIV_OR_ZERO(
				LEGACY_S32_WRAP_MUL((legacy_s32)extent, FRAME_EXPLOSION_FIXED_SCALE),
				(legacy_s32)sdgame2_widths[explosion_index]));
			shape2d_draw_scaled_transparent_clipped(height_or_scale, sdgame2shapes[explosion_index],
													offset_vector.x, offset_vector.y);
		}
	}
}

void frame_draw_cockpit_effects(struct RECTANGLE *cliprect)
{
	// Depict windscreen cracking after a crash
	sprite_set_target_clip_bounds(0, FRAME_SCREEN_WIDTH, cliprect->top, cliprect->bottom);
	if (cameramode == CAMERA_MODE_COCKPIT) {
		const struct CARSTATE *viewed_carstate = frame_viewed_car_state();
		const struct GHOST_CAMERA_STATE *ghost_camera = frame_viewed_ghost_camera();
		legacy_s16 frame = frame_state->game_frame;
		legacy_s16 crash_frame = frame_state->game_pEndFrame;
		if (ghost_camera != 0) {
			frame = ghost_camera->frame;
			crash_frame = ghost_camera->crash_frame;
		} else if (viewed_carstate == &frame_state->opponentstate) {
			crash_frame = frame_state->game_oEndFrame;
		}

		if (viewed_carstate->car_crashBmpFlag == CRASH_EVENT_COLLISION) {
			if (slow_video_mgmt_copy != 0) {
				rect_union(
					init_crak(frame - crash_frame, cliprect->top, cliprect->bottom - cliprect->top),
					frame_layer_rects, frame_layer_rects);
			} else {
				init_crak(frame - crash_frame, cliprect->top, cliprect->bottom - cliprect->top);
			}
		} else if (viewed_carstate->car_crashBmpFlag == CRASH_EVENT_WATER) {
			if (slow_video_mgmt_copy != 0) {
				rect_union(do_sinking(frame - crash_frame, cliprect->top,
									  cliprect->bottom - cliprect->top),
						   frame_layer_rects, frame_layer_rects);
			} else {
				do_sinking(frame - crash_frame, cliprect->top, cliprect->bottom - cliprect->top);
			}
		}
	}
}

void frame_draw_dashboard_top(const struct RECTANGLE *cliprect)
{
	if (dastbmp_y == 0 || dashboard_visible == 0) {
		return;
	}
	if (slow_video_mgmt_copy != 0) {
		struct RECTANGLE mask_bounds;
		mask_bounds.left = 0;
		mask_bounds.right = FRAME_SCREEN_WIDTH;
		mask_bounds.top = dastbmp_y;
		mask_bounds.bottom = dashbmp_y_copy;
		rect_union(frame_layer_rects, &mask_bounds, frame_layer_rects);
	}
	if (!frame_hires_dashboard_top(cliprect)) {
		shape2d_render_bmp_as_mask(dasmshapeptr);
		shape2d_rle_or_far_pointer(dastbmp_y2, dastseg);
	}
}

void frame_draw_elapsed_time(void)
{
	// Show elapsed time
	if (game_replay_mode == REPLAY_MODE_LIVE) {
		if (frame_state->game_inputmode != GAME_INPUT_MODE_WAITING) {
#if defined(RESTUNTS_SDL3) && !defined(RESTUNTS_HEADLESS)
			if (!extended_frame_format_elapsed_time(&resID_byte1)) {
#endif
				format_frame_as_string(&resID_byte1, elapsed_time1 + elapsed_time2, 0);
#if defined(RESTUNTS_SDL3) && !defined(RESTUNTS_HEADLESS)
			}
#endif
			font_set_fontdef2(fontledresptr);
			if (slow_video_mgmt_copy != 0) {
				rect_union(intro_draw_text(&resID_byte1, FRAME_ELAPSED_TIME_X,
										   roofbmpheight + FRAME_ELAPSED_TIME_Y_OFFSET,
										   dialog_fnt_colour, 0),
						   &frame_elapsed_time_rect, &frame_elapsed_time_rect);
			} else {
				intro_draw_text(&resID_byte1, FRAME_ELAPSED_TIME_X,
								roofbmpheight + FRAME_ELAPSED_TIME_Y_OFFSET, dialog_fnt_colour, 0);
			}

			font_set_fontdef();
		}
	}
}

void frame_finish(legacy_s8 buffer_index, struct RECTANGLE *cliprect,
				  legacy_s16 skybox_requires_full_redraw, legacy_s16 camera_yaw)
{
	if (slow_video_mgmt_copy != 0) {
		rect_union(draw_ingame_text(), frame_layer_rects, frame_layer_rects);
		if (skybox_requires_full_redraw != 0) {
			frame_layer_rects[0] = *cliprect;
			for (legacy_s16 rect_index = 1; rect_index < FRAME_DIRTY_RECT_COUNT; rect_index++) {
				frame_layer_rects[rect_index] = empty_rect;
			}
		}

		for (legacy_s16 rect_index = 0; rect_index < FRAME_DIRTY_RECT_COUNT; rect_index++) {
			active_frame_rects[rect_index] = frame_layer_rects[rect_index];
		}
		frame_buffer_camera_headings[buffer_index] = camera_yaw;
		last_rendered_camera_heading = camera_yaw;

	} else {
		draw_ingame_text();
	}
}

void update_frame(legacy_s8 buffer_index, struct RECTANGLE *cliprect)
{
	frame_hires_begin();
	struct FRAME_CAR_RENDER cars[FRAME_EXPLOSION_CAR_COUNT];
	cars[PLAYER_CAR_INDEX].explosion_visible = 0;
	cars[OPPONENT_CAR_INDEX].explosion_visible = 0;
	legacy_s8 redraw_transform_flags = frame_begin(buffer_index);
	struct FRAME_CAMERA camera;
#if defined(RESTUNTS_SDL3)
	extended_frame_set_context(frame_viewed_car_state());
#endif
	frame_setup_camera(&camera);
	legacy_s8 animated_material = frame_animated_material();
	struct FRAME_TILE_SELECTION tiles;
	tiles.lookahead = frame_setup_projection(&camera, cliprect);
	frame_draw_clouds(&camera, redraw_transform_flags);
	frame_select_tiles(&tiles, &camera);
	frame_hires_prepare_ground(&camera);
	if (!frame_hires_draw_scene(&tiles, &camera, cars, buffer_index, redraw_transform_flags,
								animated_material)) {
#if defined(RESTUNTS_SDL3)
		frame_place_cars(&tiles, cars);
		frame_draw_tiles(&tiles, &camera, cars, redraw_transform_flags, animated_material);
#else
		frame_draw_classic(&tiles, &camera, cars, buffer_index, redraw_transform_flags,
						   animated_material);
#endif
	}

	legacy_s16 skybox_requires_full_redraw =
		skybox_render(buffer_index, cliprect, camera.skybox_parameter, &camera.pitch_roll_rotation,
					  camera.roll, camera.yaw, frame_background_camera_height(&camera));
	sprite_set_target_clip_bounds(0, FRAME_SCREEN_WIDTH, cliprect->top, cliprect->bottom);
	shape3d_render_queued_primitives();
	frame_draw_explosions(cars, cliprect);
	frame_draw_cockpit_effects(cliprect);
	/* Cockpit trim covers scene effects, while the timer and race messages
	 * remain above every custom upper-dashboard silhouette. */
	frame_draw_dashboard_top(cliprect);
	frame_draw_elapsed_time();
	frame_finish(buffer_index, cliprect, skybox_requires_full_redraw, camera.yaw);
	frame_hires_end();
}
