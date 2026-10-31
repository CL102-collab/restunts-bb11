#ifndef RESTUNTS_GHOST_INTERNAL_H
#define RESTUNTS_GHOST_INTERNAL_H
#include "ghost.h"
struct GHOST_POSE {
	struct CARSTATE car;
	struct GHOST_CAMERA_STATE camera;
};

#define GHOST_TEMP_NAME_SIZE 13U
extern struct GAMEINFO ghost_config;
extern legacy_u16 ghost_file;
extern legacy_s8 ghost_temp_name[GHOST_TEMP_NAME_SIZE];
extern legacy_s16 ghost_cleanup_registered;
legacy_u16 ghost_create_file(legacy_s8 *name, legacy_s8 kind);
legacy_s16 ghost_valid_config(const struct GAMEINFO *config);
void far ghost_cleanup(void);
legacy_u32 ghost_recorded_frames(void);
#ifdef RESTUNTS_SDL3
legacy_s16 ghost_copy_live_pose(legacy_u32 frame, struct GHOST_POSE *pose);
legacy_s16 ghost_read_pose(legacy_u32 frame, struct GHOST_POSE *pose);
#endif
#endif
