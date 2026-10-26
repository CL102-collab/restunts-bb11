#include "fileio.h"
#include "memmgr.h"
#include "shape3d.h"
#include "scene_resources.h"
#ifdef RESTUNTS_AMIGA_TRACE
#include <stdio.h>
#define AMIGA_SHAPE_TRACE(stage)                                                                   \
	do {                                                                                           \
		puts("Amiga track shapes: " stage);                                                        \
		fflush(stdout);                                                                            \
	} while (0)
#else
#define AMIGA_SHAPE_TRACE(stage) ((void)0)
#endif

#define SHAPE3D_REQUIRED_ARENA_BYTES 65000UL
#define SHAPE3D_BASE_TRACK_SHAPE_COUNT 116
#define SHAPE3D_TRACK_SHAPE_NAME_SIZE 5
#define SHAPE3D_VERTEX_RECORD_SIZE 6U
#define SHAPE3D_PRIMITIVE_CULL_RECORD_SIZE 4U
#define SHAPE3D_PRIMITIVE_RECORD_SIZE 8U

legacy_s16 shape3d_load_all()
{
	game1ptr = 0;
	game2ptr = 0;

	legacy_u32 mmgrofsdiff = mmgr_get_res_ofs_diff_scaled();
#ifdef RESTUNTS_AMIGA_TRACE
	printf("Amiga track shapes: %" LEGACY_PRIu32 " bytes free\n", mmgrofsdiff);
	fflush(stdout);
#endif

	if (mmgrofsdiff < SHAPE3D_REQUIRED_ARENA_BYTES) {
		return 1;
	}

	AMIGA_SHAPE_TRACE("load GAME1");
	game1ptr = file_load_3dres("game1");
	AMIGA_SHAPE_TRACE("load GAME2");
	game2ptr = file_load_3dres("game2");
	AMIGA_SHAPE_TRACE("initialize descriptors");

	for (legacy_s16 i = 0; i < SHAPE3D_BASE_TRACK_SHAPE_COUNT; i++) {
		legacy_s8 *shapename = &game_shape_names[i * SHAPE3D_TRACK_SHAPE_NAME_SIZE];
		curshapeptr = locate_shape_nofatal(game1ptr, shapename);
		if (curshapeptr == 0) {
			curshapeptr = locate_shape_fatal(game2ptr, shapename);
		}
		shape3d_init_shape(curshapeptr, &game3dshapes[i]);
	}
	AMIGA_SHAPE_TRACE("complete");
	return 0;
}

void shape3d_free_all()
{
	if (game1ptr != 0) {
		mmgr_free(game1ptr);
	}
	if (game2ptr != 0) {
		mmgr_free(game2ptr);
	}
}

void shape3d_init_shape(legacy_s8 far *shapeptr, struct SHAPE3D *gameshape)
{
	gameshape->shape3d_numverts = (legacy_u8)shapeptr[SHAPE3D_VERTEX_COUNT_OFFSET];
	legacy_u16 primitive_count = (legacy_u8)shapeptr[SHAPE3D_PRIMITIVE_COUNT_OFFSET];
	gameshape->shape3d_numprimitives = primitive_count;
	// The original stores this one as a byte - `mov byte ptr
	// [bx+SHAPE3D.shape3d_numpaints], al` - leaving the field's high byte
	// alone, where this writes the whole word and zeroes it. The field is
	// only ever read as a count and the shape structs start out zeroed.
	gameshape->shape3d_numpaints = (legacy_u8)shapeptr[SHAPE3D_PAINT_COUNT_OFFSET];
	legacy_u16 vertex_bytes =
		LEGACY_U16_WRAP_MUL(gameshape->shape3d_numverts, SHAPE3D_VERTEX_RECORD_SIZE);
	gameshape->shape3d_vertex_bytes = (legacy_u8 far *)shapeptr + SHAPE3D_HEADER_SIZE;
	gameshape->shape3d_visibility_masks =
		(legacy_u8 far *)shapeptr + vertex_bytes + SHAPE3D_HEADER_SIZE;
	gameshape->shape3d_front_facing_masks =
		(legacy_u8 far *)shapeptr +
		LEGACY_U16_WRAP_MUL(primitive_count, SHAPE3D_PRIMITIVE_CULL_RECORD_SIZE) + vertex_bytes +
		SHAPE3D_HEADER_SIZE;
	gameshape->shape3d_primitives =
		(legacy_u8 far *)shapeptr +
		LEGACY_U16_WRAP_MUL(primitive_count, SHAPE3D_PRIMITIVE_RECORD_SIZE) + vertex_bytes +
		SHAPE3D_HEADER_SIZE;
}
