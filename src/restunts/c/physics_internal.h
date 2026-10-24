#ifndef RESTUNTS_PHYSICS_INTERNAL_H
#define RESTUNTS_PHYSICS_INTERNAL_H

#include "math.h"

struct CARSTATE;
struct SIMD;

extern legacy_s32 car_working_x;
extern legacy_s32 car_working_y;
extern legacy_s32 car_working_z;
extern legacy_s16 car_working_roll;
extern legacy_s16 car_working_yaw;
extern legacy_s16 car_working_pitch;
extern legacy_s16 nextPosAndNormalIP;

extern struct POINT2D start_finish_pole_bounds[2];
extern struct POINT2D breakable_object_bounds[2];
extern struct POINT2D track_auxiliary_obstacle_bounds[2];
extern legacy_s16 wheel_gravity_steps[4];
extern struct VECTOR scenery_collision_points[];
extern struct VECTOR elevated_road_collision_points[];
extern struct VECTOR corkscrew_lr_collision_points[];
extern struct VECTOR corkscrew_up_collision_points[];
extern struct VECTOR corkscrew_down_collision_points[];
extern struct VECTOR slalom_collision_points[];

#define PHYSICS_HEIGHT_ONLY_PLANE_COUNT 4
#define PHYSICS_PLANE_CONTACT_TOLERANCE 12
#define PHYSICS_UNDERSIDE_CLEARANCE 24

legacy_s16 sweep_track_underside(struct VECTOR *previous, struct VECTOR *current,
								 legacy_s16 *fraction);

legacy_s16 sweep_track_solid_obstacle(struct VECTORLONG *previous, struct VECTORLONG *current,
									  legacy_s16 *fraction);

legacy_s16 sweep_track_wall_span(struct VECTORLONG *previous_first,
								 struct VECTORLONG *previous_second,
								 struct VECTORLONG *current_first,
								 struct VECTORLONG *current_second, legacy_s16 *fraction);

void configure_left_corner_bias(legacy_s16 argc, legacy_s8 *argv[]);
void configure_amiga_grass(legacy_s16 argc, legacy_s8 *argv[]);
void apply_amiga_grass(struct SIMD *simd, const legacy_s8 car_id[4]);
legacy_s16 scale_cornering_angle(legacy_s16 angle, legacy_u8 shift);
void configure_legacy_collision(legacy_s16 argc, legacy_s8 *argv[]);
/* Validate a body sign-change crash between reusable track plane templates. */
legacy_s16 body_plane_crossing_is_collision(struct VECTOR *previous, struct VECTOR *current);
legacy_s16 damp_collision_angle(legacy_s16 angle);
void interpolate_collision_at_z(struct VECTOR *first, struct VECTOR *second, struct VECTOR *result,
								legacy_s16 depth);

legacy_s16 scale_position_delta(legacy_s32 current, legacy_s32 previous, legacy_s16 factor,
								legacy_s16 divisor);
legacy_s16 scale_speed_to_travel(legacy_u16 speed, legacy_u16 divisor);
legacy_s16 physics_difference_word(legacy_s32 left, legacy_s32 right);
legacy_s16 wheel_pair_delta(legacy_s16 first, legacy_s16 second, legacy_s16 third,
							legacy_s16 fourth);

legacy_s16 get_track_collision_points(legacy_s16 column, legacy_s16 row, struct VECTOR *output);
legacy_s16 update_wheel_suspension(struct CARSTATE *carstate, legacy_s16 contact_delta,
								   legacy_s16 wheel_index);
legacy_s16 resolve_car_collision_speeds(struct CARSTATE *first_state,
										struct CARSTATE *second_state);
legacy_s16 car_collision_boxes_overlap(struct POINT2D *first_collision_points,
									   struct VECTOR *first_world_coordinates,
									   struct POINT2D *second_collision_points,
									   struct VECTOR *second_world_coordinates);

#endif
