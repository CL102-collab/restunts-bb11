#include "amiga.h"
#include "palette_data.h"
#include <string.h>

/* These are the original release's RGB4 registers, not a quantization of
 * whichever VGA colors happen to be farthest apart. Scene hooks choose the
 * artwork palette; the race uses independent world/dashboard/replay maps. */
static legacy_u8 palette_name_equal(const legacy_char *a, const legacy_char *b)
{
	while (*a && *b) {
		legacy_char ca = *a++, cb = *b++;
		if (ca >= 'a' && ca <= 'z') {
			ca -= 'a' - 'A';
		}
		if (cb >= 'a' && cb <= 'z') {
			cb -= 'a' - 'A';
		}
		if (ca != cb) {
			return 0;
		}
	}
	return *a == *b;
}

const legacy_u16 *amiga_palette_original(const legacy_char *resource, const legacy_char *chunk)
{
	for (legacy_u16 i = 0; i < sizeof(amiga_original_palettes) / sizeof(amiga_original_palettes[0]);
		 ++i) {
		if (palette_name_equal(resource, amiga_original_palettes[i].resource) &&
			palette_name_equal(chunk, amiga_original_palettes[i].chunk)) {
			return amiga_original_palettes[i].colors;
		}
	}
	return NULL;
}

static void palette_map(struct AMIGA_PALETTE *palette)
{
	for (legacy_u16 i = 0; i < AMIGA_DOS_COLORS; ++i) {
		legacy_u32 closest = LEGACY_U32_MAX;
		palette->map[i] = palette->first_register;
		for (legacy_u16 slot = 0; slot < palette->count; ++slot) {
			legacy_u16 color = palette->base_colors[slot];
			legacy_s32 r =
				(legacy_s32)amiga_dos_reference_palette[i][0] * AMIGA_RGB4_COMPONENT_MAX -
				(legacy_s32)((color >> AMIGA_RGB4_RED_SHIFT) & AMIGA_RGB4_COMPONENT_MAX) *
					AMIGA_VGA_COMPONENT_MAX;
			legacy_s32 g =
				(legacy_s32)amiga_dos_reference_palette[i][1] * AMIGA_RGB4_COMPONENT_MAX -
				(legacy_s32)((color >> AMIGA_RGB4_GREEN_SHIFT) & AMIGA_RGB4_COMPONENT_MAX) *
					AMIGA_VGA_COMPONENT_MAX;
			legacy_s32 b =
				(legacy_s32)amiga_dos_reference_palette[i][2] * AMIGA_RGB4_COMPONENT_MAX -
				(legacy_s32)(color & AMIGA_RGB4_COMPONENT_MAX) * AMIGA_VGA_COMPONENT_MAX;
			legacy_u32 distance = r * r + g * g + b * b;
			if (distance < closest) {
				closest = distance;
				palette->map[i] = slot + palette->first_register;
			}
		}
	}
	if (palette->original_map != NULL) {
		for (legacy_u16 i = 0; i < AMIGA_DOS_COLORS; ++i) {
			if (palette->original_map[i] < palette->count) {
				palette->map[i] = palette->original_map[i] + palette->first_register;
			}
		}
	}
	/* Original material colors are literal EGA indexes, including brown. */
	if (palette->world) {
		for (legacy_u16 i = 0; i < AMIGA_PALETTE_BANK_COLORS; ++i) {
			palette->map[i] = i;
		}
	}
}

static void palette_fade(struct AMIGA_PALETTE *palette)
{
	legacy_u16 peak = 0;
	for (legacy_u16 i = 0; i < AMIGA_DOS_COLORS; ++i) {
		for (legacy_u16 component = 0; component < AMIGA_RGB_COMPONENTS; ++component) {
			if (palette->rgb[i][component] > peak) {
				peak = palette->rgb[i][component];
			}
		}
	}
	if (peak > palette->reference_peak) {
		peak = palette->reference_peak;
	}
	for (legacy_u16 slot = 0; slot < AMIGA_COLORS; ++slot) {
		legacy_u16 color = palette->base_colors[slot], divisor = palette->reference_peak;
		palette->colors[slot] =
			divisor == 0
				? 0
				: ((((color >> AMIGA_RGB4_RED_SHIFT) & AMIGA_RGB4_COMPONENT_MAX) * peak / divisor)
				   << AMIGA_RGB4_RED_SHIFT) |
					  ((((color >> AMIGA_RGB4_GREEN_SHIFT) & AMIGA_RGB4_COMPONENT_MAX) * peak /
						divisor)
					   << AMIGA_RGB4_GREEN_SHIFT) |
					  ((color & AMIGA_RGB4_COMPONENT_MAX) * peak / divisor);
	}
}

void amiga_palette_select(struct AMIGA_PALETTE *palette, const legacy_char *resource,
						  const legacy_char *chunk, legacy_u8 count, legacy_u8 first_register)
{
	const legacy_u16 *colors = amiga_palette_original(resource, chunk);
	if (colors == NULL || count == 0 || count > AMIGA_COLORS ||
		first_register + count > AMIGA_COLORS) {
		return;
	}
	memcpy(palette->base_colors, colors, sizeof(palette->base_colors));
	palette->original_map = NULL;
	for (legacy_u16 i = 0; i < AMIGA_ORIGINAL_COLOR_MAP_COUNT; ++i) {
		if (palette_name_equal(resource, amiga_original_color_maps[i].resource)) {
			palette->original_map = amiga_original_color_maps[i].indexes;
		}
	}
	palette->count = count;
	palette->first_register = first_register;
	palette->world = first_register == 0 && palette_name_equal(resource, "SDTITL") &&
					 palette_name_equal(chunk, "!def");
	palette_map(palette);
	palette_fade(palette);
}

void amiga_palette_update(struct AMIGA_PALETTE *palette, legacy_u16 first, legacy_u16 count,
						  const legacy_u8 *rgb)
{
	if (first >= AMIGA_DOS_COLORS) {
		return;
	}
	if (count > AMIGA_DOS_COLORS - first) {
		count = AMIGA_DOS_COLORS - first;
	}
	for (legacy_u16 i = 0; i < count; ++i) {
		for (legacy_u16 component = 0; component < AMIGA_RGB_COMPONENTS; ++component) {
			legacy_u8 value = rgb[i * AMIGA_RGB_COMPONENTS + component];
			palette->rgb[first + i][component] =
				value > AMIGA_VGA_COMPONENT_MAX ? AMIGA_VGA_COMPONENT_MAX : value;
		}
	}
	legacy_u16 peak = 0;
	for (legacy_u16 i = 0; i < AMIGA_DOS_COLORS; ++i) {
		for (legacy_u16 component = 0; component < AMIGA_RGB_COMPONENTS; ++component) {
			if (palette->rgb[i][component] > peak) {
				peak = palette->rgb[i][component];
			}
		}
	}
	/* Keep index identities stable through fades. The game installs its VGA
	 * source palette once; a brighter complete palette establishes the map. */
	if (peak && (!palette->initialized || peak > palette->reference_peak)) {
		palette->reference_peak = peak;
		palette->initialized = 1;
		if (palette->count == 0) {
			amiga_palette_select(palette, "SDTITL", "!def", AMIGA_PALETTE_BANK_COLORS, 0);
		} else {
			palette_map(palette);
		}
	}
	palette_fade(palette);
}

/* Five four-bit lanes let one 68000 longword gather four pixels across all
 * five planes at once. The conversion uses a 1 KiB lookup table and does not
 * access source or destination pixels as potentially unaligned words. */
static legacy_u32 planar_nibbles[AMIGA_DOS_COLORS];

#if defined(__GNUC__) && defined(__mc68000__)
/* Keep all five plane pointers in address registers on the original 68000.
 * The kernel implements the same nibble exchange as the portable path below,
 * uses only byte pixel accesses, and preserves the Amiga C ABI registers.
 * Arguments are explicitly 32-bit: source, lookup, plane array, row pitch,
 * row count. The public entry point checks and clamps the row count. */
#if AMIGA_WIDTH != 320U || AMIGA_HEIGHT != 200U || AMIGA_DEPTH != 5U
#error Update the native converter when changing the Amiga display geometry
#endif
extern void amiga_c2p_native(const legacy_u8 *, const legacy_u32 *, legacy_u8 **, legacy_u32,
							 legacy_u32);
__asm__(".text\n"
		".even\n"
		".globl _amiga_c2p_native\n"
		"_amiga_c2p_native:\n"
		"movem.l d2-d7/a2-a6,-(sp)\n"
		"move.l 48(sp),a0\n"
		"move.l 52(sp),a1\n"
		"move.l 56(sp),a2\n"
		"move.l 60(sp),d7\n"
		"sub.l #40,d7\n"
		"movem.l (a2),a2-a6\n"
		"move.l 64(sp),d6\n"
		"subq.w #1,d6\n"
		"1:\n"
		"moveq #39,d5\n"
		"2:\n"
		"moveq #0,d0\n"
		"move.b (a0)+,d0\n"
		"add.w d0,d0\n"
		"add.w d0,d0\n"
		"move.l 0(a1,d0.w),d1\n"
		"moveq #0,d0\n"
		"move.b (a0)+,d0\n"
		"add.w d0,d0\n"
		"add.w d0,d0\n"
		"add.l d1,d1\n"
		"or.l 0(a1,d0.w),d1\n"
		"moveq #0,d0\n"
		"move.b (a0)+,d0\n"
		"add.w d0,d0\n"
		"add.w d0,d0\n"
		"add.l d1,d1\n"
		"or.l 0(a1,d0.w),d1\n"
		"moveq #0,d0\n"
		"move.b (a0)+,d0\n"
		"add.w d0,d0\n"
		"add.w d0,d0\n"
		"add.l d1,d1\n"
		"or.l 0(a1,d0.w),d1\n"
		"moveq #0,d0\n"
		"move.b (a0)+,d0\n"
		"add.w d0,d0\n"
		"add.w d0,d0\n"
		"move.l 0(a1,d0.w),d2\n"
		"moveq #0,d0\n"
		"move.b (a0)+,d0\n"
		"add.w d0,d0\n"
		"add.w d0,d0\n"
		"add.l d2,d2\n"
		"or.l 0(a1,d0.w),d2\n"
		"moveq #0,d0\n"
		"move.b (a0)+,d0\n"
		"add.w d0,d0\n"
		"add.w d0,d0\n"
		"add.l d2,d2\n"
		"or.l 0(a1,d0.w),d2\n"
		"moveq #0,d0\n"
		"move.b (a0)+,d0\n"
		"add.w d0,d0\n"
		"add.w d0,d0\n"
		"add.l d2,d2\n"
		"or.l 0(a1,d0.w),d2\n"
		"move.l d1,d3\n"
		"lsl.l #4,d3\n"
		"eor.l d2,d3\n"
		"and.l #0x00f0f0f0,d3\n"
		"eor.l d3,d2\n"
		"lsr.l #4,d3\n"
		"eor.l d3,d1\n"
		"move.b d2,(a6)+\n"
		"lsr.w #8,d2\n"
		"move.b d2,(a4)+\n"
		"swap d2\n"
		"move.b d2,(a2)+\n"
		"move.b d1,(a5)+\n"
		"lsr.w #8,d1\n"
		"move.b d1,(a3)+\n"
		"dbra d5,2b\n"
		"add.l d7,a2\n"
		"add.l d7,a3\n"
		"add.l d7,a4\n"
		"add.l d7,a5\n"
		"add.l d7,a6\n"
		"dbra d6,1b\n"
		"movem.l (sp)+,d2-d7/a2-a6\n"
		"rts\n");
#endif

void amiga_chunky_to_planar_rows(const legacy_u8 *pixels, const legacy_u8 *map,
								 legacy_u8 *planes[AMIGA_DEPTH], legacy_u16 bytes_per_row,
								 legacy_u16 rows)
{
	if (rows == 0) {
		return;
	}
	if (rows > AMIGA_HEIGHT) {
		rows = AMIGA_HEIGHT;
	}
	for (legacy_u16 i = 0; i < AMIGA_DOS_COLORS; ++i) {
		legacy_u32 c = map[i];
		planar_nibbles[i] = ((c & 1U) << 16) | ((c & 2U) << 11) | ((c & 4U) << 6) |
							((c & 8U) << 1) | ((c & 16U) >> 4);
	}
#if defined(__GNUC__) && defined(__mc68000__)
	amiga_c2p_native(pixels, planar_nibbles, planes, (legacy_u32)bytes_per_row, (legacy_u32)rows);
#else
	legacy_u32 row = 0;
	for (legacy_u16 y = 0; y < rows; ++y, row += bytes_per_row) {
		for (legacy_u16 x = 0; x < AMIGA_ROW_BYTES; ++x) {
			legacy_u32 left = planar_nibbles[pixels[0]];
			legacy_u32 right = planar_nibbles[pixels[4]];
			left = (left << 1) | planar_nibbles[pixels[1]];
			left = (left << 1) | planar_nibbles[pixels[2]];
			left = (left << 1) | planar_nibbles[pixels[3]];
			right = (right << 1) | planar_nibbles[pixels[5]];
			right = (right << 1) | planar_nibbles[pixels[6]];
			right = (right << 1) | planar_nibbles[pixels[7]];
			/* Alternate nibbles become complete bytes for planes 4/2/0
			 * and 3/1. Explicit shifts preserve Amiga's leftmost-MSB order
			 * on both big-endian targets and little-endian test hosts. */
			legacy_u32 exchange = ((left << 4) ^ right) & 0x00f0f0f0UL;
			right ^= exchange;
			left ^= exchange >> 4;
			planes[0][row + x] = (legacy_u8)(right >> 16);
			planes[1][row + x] = (legacy_u8)(left >> 8);
			planes[2][row + x] = (legacy_u8)(right >> 8);
			planes[3][row + x] = (legacy_u8)left;
			planes[4][row + x] = (legacy_u8)right;
			pixels += 8;
		}
	}
#endif
}

void amiga_chunky_to_planar(const legacy_u8 *pixels, const legacy_u8 *map,
							legacy_u8 *planes[AMIGA_DEPTH], legacy_u16 bytes_per_row)
{
	amiga_chunky_to_planar_rows(pixels, map, planes, bytes_per_row, AMIGA_HEIGHT);
}
