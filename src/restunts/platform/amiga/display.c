#include "amiga.h"
#include <string.h>

/* Select actual VGA palette entries, retaining their identities during fades.
 * The 68000 only quantizes a newly loaded palette, never individual pixels. */
static legacy_u16 color_distance(const legacy_u8 *a, const legacy_u8 *b)
{
	legacy_s16 r = (legacy_s16)a[0] - b[0];
	legacy_s16 g = (legacy_s16)a[1] - b[1];
	legacy_s16 blue = (legacy_s16)a[2] - b[2];
	return (legacy_u16)(r * r + g * g + blue * blue);
}

void amiga_palette_update(struct AMIGA_PALETTE *palette, legacy_u16 first,
						  legacy_u16 count, const legacy_u8 *rgb)
{
	legacy_u16 i, slot;
	legacy_u16 distance[256];
	legacy_u8 peak = 0;
	legacy_u8 select_colors = !palette->initialized;
	if (first >= 256U) {
		return;
	}
	if (count > 256U - first) {
		count = 256U - first;
	}
	for (i = 0; i < count; ++i) {
		legacy_u16 component;
		for (component = 0; component < 3U; ++component) {
			legacy_u8 value = rgb[i * 3U + component];
			palette->rgb[first + i][component] = value > 63U ? 63U : value;
		}
	}
	if (palette->initialized && first == 0 && count == 256U) {
		legacy_u16 component;
		for (i = 0; i < 256U; ++i) for (component = 0; component < 3U; ++component) {
			if (palette->rgb[i][component] > peak) peak = palette->rgb[i][component];
		}
		/* Compare normalized colors with the last selected palette. Uniform
		 * fades keep their map and avoid 8192 distance calculations per step. */
		if (peak >= 8U) {
			for (i = 0; i < 256U && !select_colors; ++i) for (component = 0; component < 3U; ++component) {
				legacy_s16 difference = (legacy_s16)(palette->rgb[i][component] * palette->reference_peak) -
					(legacy_s16)(palette->reference[i][component] * peak);
				legacy_s16 tolerance = palette->reference_peak + peak;
				if (difference < -tolerance || difference > tolerance) select_colors = 1;
			}
		}
	}
	/* A complete non-black palette starts a new scene; partial updates retain
	 * the chosen register identities so palette cycling and fades stay stable. */
	if (select_colors) {
		legacy_u16 brightest = 0;
		legacy_u16 brightness = 0;
		for (i = 0; i < 256U; ++i) {
			legacy_u16 value = palette->rgb[i][0] + palette->rgb[i][1] + palette->rgb[i][2];
			if (value > brightness) {
				brightness = value;
				brightest = i;
			}
			distance[i] = 65535U;
		}
		if (brightness != 0) {
			palette->reference_peak = 0;
			memcpy(palette->reference, palette->rgb, sizeof(palette->reference));
			for (i = 0; i < 256U; ++i) {
				legacy_u16 component;
				for (component = 0; component < 3U; ++component) {
					if (palette->rgb[i][component] > palette->reference_peak)
						palette->reference_peak = palette->rgb[i][component];
				}
			}
			palette->selected[0] = 0;
			palette->selected[1] = (legacy_u8)brightest;
			for (slot = 0; slot < AMIGA_COLORS; ++slot) {
				legacy_u16 farthest = 0, largest = 0;
				for (i = 0; i < 256U; ++i) {
					legacy_u16 d = color_distance(palette->rgb[i],
												 palette->rgb[palette->selected[slot]]);
					if (d < distance[i]) {
						distance[i] = d;
						palette->map[i] = (legacy_u8)slot;
					}
					if (distance[i] > largest) {
						largest = distance[i];
						farthest = i;
					}
				}
				if (slot + 1U < AMIGA_COLORS && slot != 0) {
					palette->selected[slot + 1U] = (legacy_u8)farthest;
				}
			}
			palette->initialized = 1;
		}
	}
	for (slot = 0; slot < AMIGA_COLORS; ++slot) {
		const legacy_u8 *color = palette->rgb[palette->selected[slot]];
		palette->colors[slot] = (legacy_u16)(((color[0] >> 2) << 8) |
											 ((color[1] >> 2) << 4) | (color[2] >> 2));
	}
}

/* Five four-bit lanes let one 68000 longword gather four pixels across all
 * five planes at once. The conversion uses a 1 KiB lookup table and does not
 * access source or destination pixels as potentially unaligned words. */
static legacy_u32 planar_nibbles[256];

#if defined(__GNUC__) && defined(__mc68000__)
/* Keep all five plane pointers in address registers on the original 68000.
 * The kernel implements the same nibble exchange as the portable path below,
 * uses only byte pixel accesses, and preserves the Amiga C ABI registers.
 * Arguments are explicitly 32-bit: source, lookup, plane array, row pitch. */
#if AMIGA_WIDTH != 320U || AMIGA_HEIGHT != 200U || AMIGA_DEPTH != 5U
#error Update the native converter when changing the Amiga display geometry
#endif
extern void amiga_c2p_native(const legacy_u8 *, const legacy_u32 *,
                             legacy_u8 **, unsigned long);
__asm__(
".text\n"
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
"move.w #199,d6\n"
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
"rts\n"
);
#endif

void amiga_chunky_to_planar(const legacy_u8 *pixels, const legacy_u8 *map,
                           legacy_u8 *planes[AMIGA_DEPTH], legacy_u16 bytes_per_row)
{
    legacy_u16 i;
#if !defined(__GNUC__) || !defined(__mc68000__)
    legacy_u16 y, x;
    legacy_u32 row = 0;
#endif
    for (i = 0; i < 256U; ++i) {
        legacy_u32 c = map[i];
        planar_nibbles[i] = ((c & 1U) << 16) | ((c & 2U) << 11) |
            ((c & 4U) << 6) | ((c & 8U) << 1) | ((c & 16U) >> 4);
    }
#if defined(__GNUC__) && defined(__mc68000__)
    amiga_c2p_native(pixels, planar_nibbles, planes, (unsigned long)bytes_per_row);
#else
    for (y = 0; y < AMIGA_HEIGHT; ++y, row += bytes_per_row) {
        for (x = 0; x < AMIGA_ROW_BYTES; ++x) {
            legacy_u32 left = planar_nibbles[pixels[0]];
            legacy_u32 right = planar_nibbles[pixels[4]];
            legacy_u32 exchange;
            left = (left << 1) | planar_nibbles[pixels[1]];
            left = (left << 1) | planar_nibbles[pixels[2]];
            left = (left << 1) | planar_nibbles[pixels[3]];
            right = (right << 1) | planar_nibbles[pixels[5]];
            right = (right << 1) | planar_nibbles[pixels[6]];
            right = (right << 1) | planar_nibbles[pixels[7]];
            /* Alternate nibbles become complete bytes for planes 4/2/0
             * and 3/1. Explicit shifts preserve Amiga's leftmost-MSB order
             * on both big-endian targets and little-endian test hosts. */
            exchange = ((left << 4) ^ right) & 0x00f0f0f0UL;
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
