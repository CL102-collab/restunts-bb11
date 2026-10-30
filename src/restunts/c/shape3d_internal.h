#ifndef RESTUNTS_SHAPE3D_INTERNAL_H
#define RESTUNTS_SHAPE3D_INTERNAL_H

#include "shape3d.h"

/* Queued renderer categories, distinct from shape resource primitive types.
 * Their numeric values are stored in the byte-sized polyinfo type field. */
enum RENDER_PRIMITIVE_TYPE {
	RENDER_PRIMITIVE_POLYGON = 0,
	RENDER_PRIMITIVE_LINE = 1,
	RENDER_PRIMITIVE_SPHERE = 2,
	RENDER_PRIMITIVE_WHEEL = 3,
	RENDER_PRIMITIVE_UNSUPPORTED = 4, /* Resource type 13 has no draw handler. */
	RENDER_PRIMITIVE_POINT = 5
};

#define SHAPE3D_RECT_CLIP_TOP 1U
#define SHAPE3D_RECT_CLIP_BOTTOM 2U
#define SHAPE3D_RECT_CLIP_LEFT 4U
#define SHAPE3D_RECT_CLIP_RIGHT 8U
#define SHAPE3D_ALL_RECT_CLIP_FLAGS                                                                \
	(SHAPE3D_RECT_CLIP_TOP | SHAPE3D_RECT_CLIP_BOTTOM | SHAPE3D_RECT_CLIP_LEFT |                   \
	 SHAPE3D_RECT_CLIP_RIGHT)
#define SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG 2U

#define SHAPE3D_MATERIAL_COUNT 129U
extern legacy_s16 material_color_list[SHAPE3D_MATERIAL_COUNT];
extern legacy_s16 material_pattern_list[SHAPE3D_MATERIAL_COUNT];
extern legacy_s16 material_pattern2_list[SHAPE3D_MATERIAL_COUNT];

/* Ghost primitives retain their source material for visibility decisions. */
#define RENDER_PRIMITIVE_GHOST_FLAG 128U
#define RENDER_PRIMITIVE_BLACK_GRILLE_FLAG 64U
#define RENDER_PRIMITIVE_VISUAL_FLAGS                                                              \
	(RENDER_PRIMITIVE_GHOST_FLAG | RENDER_PRIMITIVE_BLACK_GRILLE_FLAG)
#define PRERENDER_GHOST_COLOR 256U
#define WHEEL_SOURCE_POINT_COUNT 4U
#define PRERENDER_BLACK_GRILLE_PATTERN 0xCC33U

#define DRAW_LINE_FIXED_ROUNDING 32768UL
#define DRAW_LINE_DEGENERATE_STEP 4956U
#define DRAW_LINE_MIN_MAJOR_LENGTH 2U
#define DRAW_LINE_MODE_MASK LEGACY_U8_MAX
#define DRAW_LINE_CLIP_SHIFT LEGACY_BYTE_BITS

#define DRAW_LINE_CLIP_RIGHT 1U
#define DRAW_LINE_CLIP_LEFT 2U
#define DRAW_LINE_CLIP_TOP 4U
#define DRAW_LINE_CLIP_BOTTOM 8U
#define DRAW_LINE_CLIP_MASK 15U

enum DRAW_LINE_MODE {
	DRAW_LINE_MODE_HORIZONTAL_REVERSED = 0,
	DRAW_LINE_MODE_HORIZONTAL = 1,
	DRAW_LINE_MODE_VERTICAL = 2,
	DRAW_LINE_MODE_DIAGONAL_LEFT = 3,
	DRAW_LINE_MODE_DIAGONAL_RIGHT = 4,
	DRAW_LINE_MODE_Y_MAJOR_LEFT = 5,
	DRAW_LINE_MODE_Y_MAJOR_RIGHT = 6,
	DRAW_LINE_MODE_X_MAJOR_LEFT = 7,
	DRAW_LINE_MODE_X_MAJOR_RIGHT = 8,
	DRAW_LINE_MODE_POINT = 9,
	DRAW_LINE_MODE_UNSET = LEGACY_U8_MAX
};

#define DRAW_LINE_SUBDIVIDE_MIN (-16000)
#define DRAW_LINE_SUBDIVIDE_MAX 16000

enum DRAW_LINE_BUFFER_INDEX {
	DRAW_LINE_START_X_FRACTION_INDEX = 0,
	DRAW_LINE_START_X_INDEX = 1,
	DRAW_LINE_START_Y_FRACTION_INDEX = 2,
	DRAW_LINE_START_Y_INDEX = 3,
	DRAW_LINE_END_X_INDEX = 4,
	DRAW_LINE_END_Y_INDEX = 5,
	DRAW_LINE_STEP_INDEX = 6,
	DRAW_LINE_PIXEL_COUNT_INDEX = 7,
	DRAW_LINE_COLOR_INDEX = 8,
	DRAW_LINE_MODE_AND_CLIP_INDEX = 9,
	DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX = 10,
	DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX = 11,
	DRAW_LINE_START_RIGHT_CLIP_COUNT_INDEX = 12,
	DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX = 13
};

#define DRAW_LINE_WORD_COUNT 14

extern void (*spritefunc)(legacy_s16 *, legacy_s16 *, legacy_u16, legacy_u16, legacy_u16);
extern void (*imagefunc)(legacy_u16, legacy_u16, legacy_u16, legacy_u16, legacy_u16);
extern legacy_u8 *sphere_radius_rows[];

#define POLYINFO_LEGACY_PRIMITIVE_CAPACITY 400U
#define POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY 592U
#define POLYINFO_SUPERSIGHT_DATA_SIZE 13312U

/* Native SuperSight queues grow with the visible scene. DOS retains its
 * original near-memory arrays and 16-bit record offsets. */
#if defined(RESTUNTS_SDL3)
typedef legacy_u32 polyinfo_index;
typedef legacy_s32 polyinfo_link;
typedef legacy_u32 polyinfo_offset;
extern polyinfo_link *polygon_next_index;
extern polyinfo_offset *polygon_record_offsets;
#else
typedef legacy_u16 polyinfo_index;
typedef legacy_s16 polyinfo_link;
typedef legacy_u16 polyinfo_offset;
extern polyinfo_link polygon_next_index[];
extern polyinfo_offset polygon_record_offsets[];
#endif
extern polyinfo_index polyinfonumpolys;
extern polyinfo_offset polyinfoptrnext;
extern legacy_u8 far *polyinfoptr;

#endif
