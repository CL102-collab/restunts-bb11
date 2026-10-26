#ifndef RESTUNTS_SHAPE2D_H
#define RESTUNTS_SHAPE2D_H

#include <stddef.h>
#include "legacy.h"

#pragma pack(push, 1)

/* Header of a VGA/EGA bitmap resource, followed directly by the pixel data.
 * The layout is the on-disk resource format, so it is fixed. centre_x and
 * centre_y are the anchor a shape is drawn around; the plane_flags bytes carry
 * the channel mapping and the transposed/interlaced flags. */
struct SHAPE2D {
	legacy_u16 width;
	legacy_u16 height;
	legacy_u16 centre_x;
	legacy_u16 centre_y;
	legacy_u16 position_x;
	legacy_u16 position_y;
	legacy_u8 plane_flags[4];
};

#pragma pack(pop)

#define SHAPE2D_HEADER_SIZE (sizeof(struct SHAPE2D))
#define SHAPE2D_TRANSPARENT_COLOR LEGACY_U8_MAX

enum SHAPE2D_RASTER_OPERATION {
	SHAPE2D_RASTER_AND = 0,
	SHAPE2D_RASTER_OR = 1,
	SHAPE2D_RASTER_COPY = 2,
	SHAPE2D_RASTER_MAP = 3
};

typedef char shape2d_header_must_be_16_bytes[(sizeof(struct SHAPE2D) == 16) ? 1 : -1];
typedef char shape2d_centre_x_must_be_at_04[(offsetof(struct SHAPE2D, centre_x) == 4) ? 1 : -1];
typedef char shape2d_position_x_must_be_at_08[(offsetof(struct SHAPE2D, position_x) == 8) ? 1 : -1];
typedef char
	shape2d_plane_flags_must_be_at_0C[(offsetof(struct SHAPE2D, plane_flags) == 12) ? 1 : -1];

#pragma pack(push, 1)
struct SPRITE {
	struct SHAPE2D far *sprite_bitmapptr;
	legacy_u16 sprite_reserved_word1;
	legacy_u16 sprite_reserved_word2;
	legacy_u16 sprite_reserved_word3;
	legacy_u8 *sprite_lineofs;
	legacy_u16 sprite_left;
	legacy_u16 sprite_right;
	legacy_u16 sprite_top;
	legacy_u16 sprite_bottom;
	legacy_u16 sprite_pitch;
	legacy_u16 sprite_reserved_word4;
	legacy_u16 sprite_buffer_width;
	legacy_u16 sprite_raster_left;
	legacy_u16 sprite_raster_right;
};
#pragma pack(pop)

legacy_u16 shape2d_get_width(const struct SHAPE2D far *shape);
legacy_u16 shape2d_get_height(const struct SHAPE2D far *shape);
legacy_u16 shape2d_get_anchor_x(const struct SHAPE2D far *shape);
legacy_u16 shape2d_get_anchor_y(const struct SHAPE2D far *shape);
legacy_u16 shape2d_get_pos_x(const struct SHAPE2D far *shape);
legacy_u16 shape2d_get_pos_y(const struct SHAPE2D far *shape);
legacy_u16 shape2d_anchored_x(const struct SHAPE2D far *shape, legacy_s16 x);
legacy_u16 shape2d_anchored_y(const struct SHAPE2D far *shape, legacy_s16 y);

/* SPRITE contains both 16-bit near and far pointers. */
#if defined(RESTUNTS_DOS16)
typedef char legacy_sprite_must_be_30_bytes[(sizeof(struct SPRITE) == 30) ? 1 : -1];
#endif

struct SPRITE far *sprite_make_wnd(legacy_u16 width, legacy_u16 height, legacy_u16);
void sprite_free_wnd(struct SPRITE far *wndsprite);

void sprite_select_target(struct SPRITE far *target_sprite);

void sprite_select_screen(void);
void sprite_select_screen_compat(void);
void sprite_select_screen_and_clear(void);
void sprite_select_render_window(void);
void sprite_select_render_window_and_clear(void);

/* A context includes both drawing and screen sprites. Callers allocate both. */
#define SPRITE_STATE_COUNT 2U
void sprite_save_context(struct SPRITE saved_context[SPRITE_STATE_COUNT]);
void sprite_restore_context(struct SPRITE saved_context[SPRITE_STATE_COUNT]);

legacy_s16 sprite_push_background(legacy_s16 left, legacy_s16 right, legacy_s16 top,
								  legacy_s16 bottom);

void sprite_clear_target(legacy_u8 color);
void sprite_xor_rect_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
							 legacy_s16 color);
void sprite_fill_rect(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
					  legacy_s16 color);
void sprite_fill_rect_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
							  legacy_s16 color);
void sprite_draw_rect_outline(legacy_s16 x1, legacy_s16 y1, legacy_s16 x2, legacy_s16 y2,
							  legacy_s16 color);
void sprite_draw_dissolve_phase(struct SHAPE2D far *shape, legacy_u16 phase);
void sprite_draw_palette_mapped(struct SHAPE2D far *shape);

/* EGA conversion tables can change between shapes in the same resource. */
#ifdef RESTUNTS_TEMPLEOS
void shape2d_select_ega_palette(void far *resource, legacy_u8 index);
#else
#define shape2d_select_ega_palette(resource, index) ((void)0)
#endif

void draw_filled_lines(legacy_s16 *x1arr, legacy_s16 *x2arr, legacy_u16 y, legacy_u16 numlines,
					   legacy_u16 color);
void draw_two_color_lines(legacy_s16 *x1arr, legacy_s16 *x2arr, legacy_u16 y, legacy_u16 numlines,
						  legacy_u16 color);
void draw_patterned_lines(legacy_s16 *x1arr, legacy_s16 *x2arr, legacy_u16 y, legacy_u16 numlines,
						  legacy_u16 color);
void sprite_draw_line_from_setup(const legacy_u16 *line);
void sprite_putpixel_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 color);
void putpixel_iconMask(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y);
void putpixel_iconFillings(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y);

void sprite_putimage(struct SHAPE2D far *shape);
void sprite_shape_to_1(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y);
void sprite_shape_to_1_alt(struct SHAPE2D far *shape);
void sprite_pop_background(void);
void sprite_putimage_and(struct SHAPE2D far *shape, legacy_u16 a, legacy_u16 b);
void sprite_copy_image_at(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y);
void sprite_and_image_at_anchor(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y);
void sprite_putimage_or(struct SHAPE2D far *shape, legacy_u16 a, legacy_u16 b);
void sprite_or_image_at_anchor(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y);
void sprite_putimage_transparent(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y);
/* Inverse affine sampling of a raw sprite. Coordinates are pixels and matrix
 * coefficients are signed Q14. The source rectangle may be smaller than the
 * sprite's physical stride; transparent source pixels leave the target alone. */
#define SHAPE2D_TRANSFORM_FRACTION_BITS 14U
#define SHAPE2D_TRANSFORM_ONE 16384L
struct SHAPE2D_TRANSFORM {
	legacy_s16 inverse_xx, inverse_xy, inverse_yx, inverse_yy;
	legacy_s16 source_pivot_x, source_pivot_y, target_pivot_x, target_pivot_y;
	legacy_u16 source_width, source_height;
	legacy_s16 left, top, right, bottom;
};
void sprite_copy_image_transformed(struct SPRITE far *source,
								   const struct SHAPE2D_TRANSFORM *transform);

void sprite_clear_shape_alt(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y);
void sprite_clear_shape(struct SHAPE2D far *shape);
void font_draw_text_opaque(const legacy_s8 *text, legacy_s16 x, legacy_s16 y);
void shape2d_rle_copy_at_position(struct SHAPE2D far *shape);
void shape2d_rle_copy_clipped(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y);
void shape2d_rle_copy_position_clipped(struct SHAPE2D far *shape);
void shape2d_rle_mask_position_clipped(struct SHAPE2D far *shape);
void shape2d_rle_or_position_clipped(struct SHAPE2D far *shape);
void shape2d_rle_or_far_pointer(legacy_u16 offset, legacy_u16 segment);
void shape2d_rle_copy(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y);
void shape2d_render_bmp_as_mask(struct SHAPE2D far *shape);

void shape2d_draw_scaled_transparent_clipped(legacy_s16 scale, struct SHAPE2D far *shape,
											 legacy_s16 x, legacy_s16 y);
void shape2d_draw_scaled_transparent(legacy_s16 scale, struct SHAPE2D far *shape, legacy_s16 x,
									 legacy_s16 y);

void sprite_present_mcga_backbuffer(void);
void sprite_select_mcga_backbuffer(void);
void sprite_copy_rect_shifted(legacy_s16 source_x, legacy_s16 source_y, legacy_s16 width,
							  legacy_s16 height, legacy_s16 destination_shift);

struct SHAPE2D far *file_get_shape2d(legacy_u8 far *memchunk, legacy_s16 index);

legacy_u16 file_get_res_shape_count(void far *memchunk);

void file_unflip_shape2d(legacy_u8 far *memchunk, legacy_s8 far *mempages);

void file_unflip_shape2d_pes(legacy_u8 far *memchunk, legacy_s8 far *mempages);

void file_load_shape2d_expand(legacy_u8 far *memchunk, legacy_s8 far *mempages);

legacy_u16 file_get_unflip_size(legacy_s8 far *memchunk);

legacy_u16 file_load_shape2d_expandedsize(void far *memchunk);

void file_load_shape2d_palmap_init(legacy_u8 far *pal);
void file_load_shape2d_palmap_apply(legacy_u8 far *memchunk, legacy_u8 palmap[]);

void far *file_load_shape2d_esh(void far *memchunk, const legacy_s8 *str);
void parse_shape2d(void far *memchunk, void far *mempages);
void far *file_load_shape2d(const legacy_s8 *shapename, legacy_s16 fatal);

void far *file_load_shape2d_fatal(const legacy_s8 *shapename);
void far *file_load_shape2d_nofatal(const legacy_s8 *shapename);
void far *file_load_shape2d_nofatal2(const legacy_s8 *shapename);

void far *file_load_shape2d_res(const legacy_s8 *resname, legacy_s16 fatal);
void far *file_load_shape2d_res_fatal(const legacy_s8 *resname);
void far *file_load_shape2d_res_nofatal(const legacy_s8 *resname);

extern struct SPRITE far *render_window_sprite;

#endif
