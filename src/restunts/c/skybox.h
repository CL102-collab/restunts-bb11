#ifndef RESTUNTS_SKYBOX_H
#define RESTUNTS_SKYBOX_H

#include "math/math.h"

#define SKYBOX_IMAGE_COUNT 4
#define SKYBOX_IMAGE_WIDTH 320U
#define SKYBOX_IMAGE_HALF_WRAP 512U
#define SKYBOX_IMAGE_ONE_AND_HALF_WIDTH 832U
#define SKYBOX_IMAGE_FULL_WRAP 1024U
#define SKYBOX_SCREEN_WIDTH 320
#define SKYBOX_SCREEN_BOTTOM 200
#define SKYBOX_LOWEST_DETAIL_LEVEL 4
#define SKYBOX_ROLL_VECTOR_Z 15000U

/* World materials are independent of bitmap palette-map color indexes. */
enum SKYBOX_MATERIAL {
	SKYBOX_MATERIAL_GROUND = 16,
	SKYBOX_MATERIAL_SKY = 17,
	SKYBOX_MATERIAL_WATER = 100
};

struct SHAPE2D;

/* The four horizon band images and the colours drawn around them. heights
 * is indexed the same way as the skyboxes[] resource array. */
struct SKYBOX {
	legacy_u16 heights[SKYBOX_IMAGE_COUNT];
	legacy_u16 minimum_height;
	legacy_u16 maximum_height;
	legacy_s16 sky_color;
	legacy_s16 ground_color;
	legacy_s16 water_color;
};
extern struct SKYBOX skybox;
extern legacy_s8 skybox_resources_loaded;
extern legacy_s8 far *skybox_res_ofs;
extern legacy_s8 loaded_skybox_index;
extern struct SHAPE2D far *skyboxes[];

void load_skybox(legacy_s8 skybox_index);

void unload_skybox(void);

/* Draw a horizon image to the current sprite, retaining SuperSight detail when enabled. */
void skybox_copy_image(legacy_s16 image, legacy_s16 x, legacy_s16 y);

void skybox_render_level_rect(struct RECTANGLE *rect, legacy_s16 angle, legacy_s16 horizon);

legacy_s16 skybox_render(legacy_s16 view_index, struct RECTANGLE *clip, legacy_s16 direction,
						 struct MATRIX *rotation, legacy_s16 roll, legacy_s16 angle,
						 legacy_s16 camera_y);

#endif
