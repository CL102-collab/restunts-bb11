#ifndef RESTUNTS_GAMESTATE_H
#define RESTUNTS_GAMESTATE_H

#include "math/math.h"

#define GAMESTATE_SERIALIZED_SIZE 1120U
#define CARSTATE_SERIALIZED_SIZE 208U
#define CARSTATE_WHEEL_COUNT 4U
#define GAMESTATE_PARTICLE_SLOT_COUNT 24U

enum GAME_CAR_INDEX { PLAYER_CAR_INDEX = 0, OPPONENT_CAR_INDEX = 1 };

#define GAME_CAR_COUNT 2U
#define GAMESTATE_CAR_VECTOR_COUNT GAME_CAR_COUNT
#define GAMESTATE_PARTICLE_VELOCITY_BYTES (GAMESTATE_PARTICLE_SLOT_COUNT * LEGACY_WORD_BYTES)
#define GAMESTATE_RANDOM_SEED_SIZE 6U
#define GAMESTATE_CAMERA_INDEX_COUNT 2U
#define GAMESTATE_BREAKABLE_OBJECT_COUNT 48U
#define GAMESTATE_CHECKPOINT_COUNT 20U

enum GAME_FRAME_RATE { GAME_FRAME_RATE_LOW = 10, GAME_FRAME_RATE_NORMAL = 20 };

enum GAME_INPUT_MODE {
	GAME_INPUT_MODE_WAITING = 0,
	GAME_INPUT_MODE_ACTIVE = 1,
	GAME_INPUT_MODE_INTRO = 2
};

enum RACE_START_SEQUENCE {
	RACE_START_SEQUENCE_INACTIVE = 0,
	RACE_START_SEQUENCE_FLAG_ANIMATION = 1,
	RACE_START_SEQUENCE_AUTO_DRIVE = 2
};

enum GAMESTATE_INIT_MODE {
	GAMESTATE_INIT_NORMAL = 0,
	GAMESTATE_INIT_RESET_CHECKPOINTS = -1,
	GAMESTATE_INIT_SKIP_ROUTE_SETUP = -2,
	GAMESTATE_INIT_TIMING_ONLY = -3
};

enum TRANSMISSION_MODE { TRANSMISSION_MANUAL = 0, TRANSMISSION_AUTOMATIC = 1 };

enum CAR_GEAR_CHANGE_STATE { CAR_GEAR_CHANGE_INACTIVE = 0, CAR_GEAR_CHANGE_ACTIVE = 1 };

#define GEAR_CHANGE_DELAY_EXPIRED 0

enum CAR_PEDAL_STATE { CAR_PEDAL_RELEASED = 0, CAR_PEDAL_PRESSED = 1 };

#define CAR_SPEED_STOPPED 0
#define CAR_STEERING_CENTERED 0

enum CAR_SURFACE_TYPE {
	CAR_SURFACE_PAVED = 1,
	CAR_SURFACE_DIRT = 2,
	CAR_SURFACE_ICE = 3,
	CAR_SURFACE_GRASS = 4,
	CAR_SURFACE_WATER = 5
};

enum CAR_SLIDING_STATE { CAR_SLIDING_INACTIVE = 0, CAR_SLIDING_ACTIVE = 1 };

#define CAR_WHEEL_CONTACT_NONE 0

enum CAR_COLLISION_LATCH_STATE { CAR_COLLISION_LATCH_CLEAR = 0, CAR_COLLISION_LATCH_SET = 1 };

#define CAR_SOUND_NONE 0U
#define CAR_SOUND_ENGINE_ACTIVE_FLAG 1U
#define CAR_SOUND_SKID_PAVED_FLAG 2U
#define CAR_SOUND_SKID_OFFROAD_FLAG 4U
#define CAR_SOUND_SKID_MASK (CAR_SOUND_SKID_PAVED_FLAG | CAR_SOUND_SKID_OFFROAD_FLAG)

enum ROUTE_INDICATOR {
	ROUTE_INDICATOR_NONE = 0,
	ROUTE_INDICATOR_LEFT = 1,
	ROUTE_INDICATOR_RIGHT = 2,
	ROUTE_INDICATOR_WRONG_WAY = 3
};

enum ROUTE_TRACKING_STATE {
	ROUTE_TRACKING_NORMAL = 0,
	ROUTE_TRACKING_OUTSIDE_TRACK = 1,
	ROUTE_TRACKING_WRONG_WAY = 2
};

enum ROUTE_CONFIRMATION_STATE { ROUTE_CONFIRMATION_NONE = 0, ROUTE_CONFIRMATION_INITIAL = 1 };

#define ROUTE_CONFIRMATION_STEP 1
#define ROUTE_INDEX_NONE (-1)

enum ROUTE_POINT_INDEX { ROUTE_POINT_FIRST = 0, ROUTE_POINT_SECOND = 1 };

#define ROUTE_POINT_STEP 1
#define ROUTE_POINT_HEIGHT_UNSPECIFIED (-1)

#pragma pack(push, 1)

struct CARSTATE {
	struct VECTORLONG car_position;
	struct VECTORLONG car_previous_position;
	struct VECTOR car_rotate; /* Rotation angles, despite the vector notation. */
	legacy_s16 car_pseudoGravity;
	legacy_s16 car_steeringAngle;
	legacy_s16 car_currpm;
	legacy_s16 car_lastrpm;
	legacy_s16 car_initial_rpm;
	legacy_s16 car_speeddiff;	 /* Formerly called gripdiff. */
	legacy_u16 car_rev_speed;	 /* Rev-coupled speed, scaled by 2^8. */
	legacy_u16 car_actual_speed; /* Actual car speed, scaled by 2^8. */
	legacy_u16 car_lastspeed;
	legacy_u16 car_gearratio;
	legacy_u16 car_gearratioshr8;
	legacy_s16 car_knob_x;
	legacy_s16 car_velocity_heading_offset;
	legacy_s16 car_knob_y;
	legacy_s16 car_knob_x2;
	legacy_s16 car_knob_y2;
	legacy_s16 car_slide_yaw_delta;
	legacy_s16 car_front_wheel_response_angle;
	legacy_s16 car_slip_angle;
	legacy_s16 car_demandedGrip;
	legacy_s16 car_surfacegrip_sum;
	legacy_s16 car_route_heading_error;
	legacy_s16 car_route_index;
	legacy_s16 car_wheel_vertical_speed[CARSTATE_WHEEL_COUNT];
	legacy_s16 car_suspension_deflection[CARSTATE_WHEEL_COUNT];
	legacy_s16 car_reserved_wheel_state[CARSTATE_WHEEL_COUNT];
	legacy_s16 car_reserved_contact_state[CARSTATE_WHEEL_COUNT];
	legacy_s16 car_suspension_target[CARSTATE_WHEEL_COUNT];
	struct VECTOR car_wheel_contact_positions[CARSTATE_WHEEL_COUNT];
	struct VECTOR car_body_corner_positions[CARSTATE_WHEEL_COUNT];
	struct VECTOR car_route_target;
	struct VECTOR car_route_first_edge;
	struct VECTOR car_route_second_edge;
	legacy_s16 car_route_has_reverse_path;
	legacy_s16 car_reserved_route_word1;
	legacy_s16 car_reserved_route_word2;
	legacy_s8 car_is_braking;
	legacy_s8 car_is_accelerating;
	legacy_s8 car_current_gear;
	legacy_s8 car_sumSurfFrontWheels;
	legacy_s8 car_sumSurfRearWheels;
	legacy_s8 car_sumSurfAllWheels; /* Also used as the jump flag. */
	legacy_s8 car_surfaceWhl[CARSTATE_WHEEL_COUNT];
	legacy_s8 car_engineLimiterTimer;
	legacy_s8 car_slidingFlag;
	legacy_s8 car_collision_latch;
	legacy_s8 car_crashBmpFlag;
	legacy_s8 car_changing_gear;
	legacy_s8 car_gear_change_delay;
	legacy_s8 car_transmission;
	legacy_s8 car_lap_count;
	legacy_s8 car_route_point_index;
	legacy_s8 car_sound_flags;
};

struct GAMESTATE {
	legacy_s32 game_particle_x[GAMESTATE_PARTICLE_SLOT_COUNT]; /* x */
	legacy_s32 game_particle_y[GAMESTATE_PARTICLE_SLOT_COUNT]; /* y */
	legacy_s32 game_particle_z[GAMESTATE_PARTICLE_SLOT_COUNT]; /* z */
	struct VECTOR game_follow_camera_position[GAMESTATE_CAR_VECTOR_COUNT];
	struct VECTOR game_player_camera_previous;
	struct VECTOR game_opponent_camera_previous;
	legacy_s16 game_frame_in_sec;
	legacy_s16 game_frames_per_sec;
	legacy_s32 game_travDist;
	legacy_s16 game_frame;
	legacy_s16 game_total_finish; /* Finish time plus penalty. */
	legacy_s16 game_opponent_finish_time;
	legacy_s16 game_pEndFrame;
	legacy_s16 game_oEndFrame;
	legacy_s16 game_penalty;
	legacy_u16 game_impactSpeed;
	legacy_u16 game_topSpeed;
	legacy_s16 game_jumpCount;
	struct CARSTATE playerstate;
	struct CARSTATE opponentstate;
	legacy_s16 game_player_confirmed_route;
	legacy_s16 game_player_previous_route;
	legacy_s16 game_startcol;
	legacy_s16 game_startcol2;
	legacy_s16 game_startrow;
	legacy_s16 game_startrow2;
	legacy_s16 game_particle_rotation_x[GAMESTATE_PARTICLE_SLOT_COUNT];
	legacy_s16 game_particle_rotation_y[GAMESTATE_PARTICLE_SLOT_COUNT];
	legacy_s16 game_particle_heading[GAMESTATE_PARTICLE_SLOT_COUNT];
	legacy_s16 game_particle_forward_speed[GAMESTATE_PARTICLE_SLOT_COUNT];
	legacy_s8 game_particle_vertical_speed[GAMESTATE_PARTICLE_VELOCITY_BYTES];
	legacy_s8 kevinseed[GAMESTATE_RANDOM_SEED_SIZE];
	legacy_s8 game_checkpoint_valid;
	legacy_s8 game_inputmode;
	legacy_s8 game_end_event;
	legacy_s8 game_trackside_camera_index[GAMESTATE_CAMERA_INDEX_COUNT];
	legacy_s8 game_opponent_target_speed;
	legacy_s8 game_object_destroyed[GAMESTATE_BREAKABLE_OBJECT_COUNT];
	legacy_s8 game_particles_active;
	legacy_s8 game_particle_shape_index[GAMESTATE_PARTICLE_SLOT_COUNT];
	legacy_s8 game_particle_owner[GAMESTATE_PARTICLE_SLOT_COUNT];
	legacy_s8 game_player_route_status;
	legacy_s8 game_route_confirmation_count;
	legacy_s8 game_player_route_indicator;
	legacy_s8 game_opponent_route_indicator;
	legacy_s8 game_reserved_trailing_byte;
};

#pragma pack(pop)

typedef char legacy_carstate_must_be_208_bytes[(sizeof(struct CARSTATE) == CARSTATE_SERIALIZED_SIZE)
												   ? 1
												   : -1];
typedef char legacy_gamestate_must_be_1120_bytes
	[(sizeof(struct GAMESTATE) == GAMESTATE_SERIALIZED_SIZE) ? 1 : -1];

legacy_u16 gamestate_serialize(legacy_u8 far *destination, const struct GAMESTATE *source);

#endif
