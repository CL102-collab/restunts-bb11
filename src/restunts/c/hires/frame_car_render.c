#if defined(RESTUNTS_SDL3)
#include "../frame/frame_render_private.h"

struct VECTOR frame_hires_car_render_offset(const struct CARSTATE *carstate,
											const struct SHAPE3D *wheel_shape)
{
	struct VECTOR local = {0, -shape3d_hires_car_ground_offset(wheel_shape), 0};
	struct VECTOR offset;
	struct MATRIX *rotation =
		mat_rot_zxy(LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.z),
					LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.y),
					LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.x), MATRIX_ROTATION_ORDER_ZXY);
	mat_mul_vector(&local, rotation, &offset);
	/* Collision surfaces are raised two world units above the road artwork.
	 * Grass uses a zero/one-unit height variation instead. Remove the lowest
	 * supported-wheel bias to avoid sinking tires at mixed surface edges.
	 * Keep the road allowance in flight, without snapping to a surface. */
	legacy_s16 surface_bias = FRAME_NON_GRASS_HEIGHT_OFFSET;
	for (legacy_s16 wheel = 0; wheel < FRAME_CAR_WHEEL_COUNT; wheel++) {
		if (carstate->car_surfaceWhl[wheel] == CAR_SURFACE_GRASS) {
			const struct VECTOR *contact = &carstate->car_wheel_contact_positions[wheel];
			legacy_s16 grass_bias =
				((legacy_u16)(contact->x ^ contact->z) >> FRAME_GRASS_HEIGHT_HASH_SHIFT) &
				FRAME_GRASS_HEIGHT_VARIATION_MASK;
			if (grass_bias < surface_bias) {
				surface_bias = grass_bias;
			}
		}
	}
	offset.y = LEGACY_S16_WRAP_SUB(offset.y, surface_bias);
	return offset;
}

void frame_hires_add_car_shadow(const struct CARSTATE *carstate, const struct SIMD *simd,
								const struct VECTOR *camera_position, const struct SHAPE3D *shape)
{
	if (!extended_frame_car_visible(carstate)) {
		return;
	}

	if (frame_hires_adaptive_car_flags(carstate, simd) == FRAME_HIRES_ADAPTIVE_HIDE ||
		carstate->car_crashBmpFlag == CRASH_EVENT_WATER ||
		(cameramode == CAMERA_MODE_COCKPIT && carstate == frame_viewed_car_state())) {
		return;
	}
	struct VECTOR relative_position;
	relative_position.x = frame_relative_position(carstate->car_position.lx, camera_position->x);
	relative_position.y = frame_relative_position(carstate->car_position.ly, camera_position->y);
	relative_position.z = frame_relative_position(carstate->car_position.lz, camera_position->z);
	/* The collision box stores half-width and half-length in renderer units. */
	shape3d_hires_shadow_car(&relative_position, LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.x),
							 simd->collide_points[0].px, simd->collide_points[1].px);
	shape3d_hires_shadow_model(shape);
}

void frame_hires_override_car_detail(const struct TRACKOBJECT *track_object, legacy_s8 tile_detail,
									 legacy_s16 *low_detail)
{
	if (hires_active != 0) {
		*low_detail = tile_detail != FRAME_TILE_DETAIL_FULL && track_object->ss_loShapePtr != 0;
	}
}

legacy_s16 frame_hires_prepare_car(const struct CARSTATE *carstate, legacy_s8 debris_owner,
								   const struct SHAPE3D *wheel_shape, legacy_s8 *tile_detail,
								   legacy_s8 *flags, struct VECTOR *render_offset)
{
	if (frame_hires_adaptive_active != 0) {
		const struct SIMD *simd =
			debris_owner == PLAYER_CAR_INDEX
				? &simd_player
				: (gameconfig.game_opponenttype != 0 ? &simd_opponent : ghost_car_simd());
		legacy_u8 adaptive_flags = frame_hires_adaptive_car_flags(carstate, simd);
		if (adaptive_flags == FRAME_HIRES_ADAPTIVE_HIDE) {
			return 0;
		}
		*tile_detail = adaptive_flags == FRAME_HIRES_ADAPTIVE_LOW_GEOMETRY;
	}
	if (hires_active != 0) {
		*flags |= SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
		*render_offset = frame_hires_car_render_offset(carstate, wheel_shape);
	}
	return 1;
}

void frame_hires_car_flags(legacy_s8 flags)
{
	curtransshape_ptr->ts_flags |= flags & SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
}

void frame_hires_offset_car_shape(const struct VECTOR *offset)
{
	curtransshape_ptr->pos.x = LEGACY_S16_WRAP_ADD(curtransshape_ptr->pos.x, offset->x);
	curtransshape_ptr->pos.y = LEGACY_S16_WRAP_ADD(curtransshape_ptr->pos.y, offset->y);
	curtransshape_ptr->pos.z = LEGACY_S16_WRAP_ADD(curtransshape_ptr->pos.z, offset->z);
}

#endif
