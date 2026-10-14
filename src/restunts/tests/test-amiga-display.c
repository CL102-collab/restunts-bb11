#include "../platform/amiga/amiga.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Report failures to the redirected Amiga shell output instead of opening
 * libnix's interactive assertion requester during unattended emulator runs. */
static void display_test_failure(unsigned line)
{
    printf("AMIGA DISPLAY TEST FAIL at line %u\n", line);
    fflush(stdout);
    exit(1);
}
#undef assert
#define assert(condition) ((condition) ? (void)0 : display_test_failure(__LINE__))

#define TEST_PITCH 47U
#define TEST_GUARD 16U
static legacy_u8 pixels[AMIGA_SCREEN_BYTES];
static legacy_u8 storage[AMIGA_DEPTH][TEST_GUARD + TEST_PITCH * AMIGA_HEIGHT + TEST_GUARD];

static legacy_u32 random_state = 0x12345678UL;
static legacy_u8 next_byte(void)
{
    random_state = random_state * 1664525UL + 1013904223UL;
    return (legacy_u8)(random_state >> 24);
}

static void check_conversion(legacy_u16 pitch, legacy_u16 pattern)
{
    legacy_u8 map[256], *planes[AMIGA_DEPTH];
    legacy_u32 i;
    legacy_u16 plane, y, x;
    for (i = 0; i < 256U; ++i) map[i] = next_byte() & 31U;
    for (i = 0; i < AMIGA_SCREEN_BYTES; ++i) {
        legacy_u16 fill = pattern == 3 ? (legacy_u16)((i / AMIGA_WIDTH) & 3U) : pattern;
        pixels[i] = fill == 0 ? 0 : fill == 1 ? 255 :
            fill == 2 ? (legacy_u8)i : next_byte();
    }
    memset(storage, 0xa5, sizeof(storage));
    for (plane = 0; plane < AMIGA_DEPTH; ++plane) planes[plane] = storage[plane] + TEST_GUARD;
    amiga_chunky_to_planar(pixels, map, planes, pitch);
    /* Reconstruct each pixel independently from the five bitplanes. This
     * checks plane order, pixel order, palette lookup, row pitch and padding. */
    for (y = 0; y < AMIGA_HEIGHT; ++y) {
        for (x = 0; x < AMIGA_WIDTH; ++x) {
            legacy_u8 result = 0;
            for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
                legacy_u8 byte = planes[plane][(legacy_u32)y * pitch + (x >> 3)];
                result |= ((byte >> (7U - (x & 7U))) & 1U) << plane;
            }
            assert(result == map[pixels[(legacy_u32)y * AMIGA_WIDTH + x]]);
        }
        for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
            for (x = AMIGA_ROW_BYTES; x < pitch; ++x)
                assert(planes[plane][(legacy_u32)y * pitch + x] == 0xa5);
        }
    }
    for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
        for (i = 0; i < TEST_GUARD; ++i) assert(storage[plane][i] == 0xa5);
        for (i = TEST_GUARD + (legacy_u32)pitch * AMIGA_HEIGHT; i < sizeof(storage[plane]); ++i)
            assert(storage[plane][i] == 0xa5);
    }
}

static legacy_u16 distance(const legacy_u8 *a, const legacy_u8 *b)
{
    legacy_s16 r = a[0] - b[0], g = a[1] - b[1], blue = a[2] - b[2];
    return (legacy_u16)(r * r + g * g + blue * blue);
}

static void check_palette(void)
{
    struct AMIGA_PALETTE palette;
    legacy_u8 colors[256 * 3], faded[256 * 3], saved_map[256], selected[32];
    legacy_u16 i, slot;
    memset(&palette, 0, sizeof(palette));
    memset(colors, 0, sizeof(colors));
    amiga_palette_update(&palette, 0, 256, colors);
    for (i = 0; i < 32; ++i) assert(palette.colors[i] == 0);
    for (i = 0; i < sizeof(colors); ++i) colors[i] = next_byte() & 63U;
    colors[0] = colors[1] = colors[2] = 0;
    colors[3] = colors[4] = colors[5] = 63;
    amiga_palette_update(&palette, 0, 256, colors);
    assert(palette.initialized);
    for (i = 0; i < 256; ++i) {
        legacy_u16 nearest;
        assert(palette.map[i] < 32);
        nearest = distance(palette.rgb[i], palette.rgb[palette.selected[palette.map[i]]]);
        for (slot = 0; slot < 32; ++slot)
            assert(nearest <= distance(palette.rgb[i], palette.rgb[palette.selected[slot]]));
    }
    for (slot = 0; slot < 32; ++slot) assert(palette.colors[slot] <= 0x0fffU);
    memcpy(saved_map, palette.map, sizeof(saved_map));
    memcpy(selected, palette.selected, sizeof(selected));
    for (i = 0; i < sizeof(colors); ++i) faded[i] = colors[i] / 2;
    amiga_palette_update(&palette, 0, 256, faded);
    assert(memcmp(saved_map, palette.map, sizeof(saved_map)) == 0);
    assert(memcmp(selected, palette.selected, sizeof(selected)) == 0);
    memset(faded, 0, sizeof(faded));
    amiga_palette_update(&palette, 0, 256, faded);
    assert(memcmp(saved_map, palette.map, sizeof(saved_map)) == 0);
    for (slot = 0; slot < 32; ++slot) assert(palette.colors[slot] == 0);
    amiga_palette_update(&palette, 0, 256, colors);
    assert(memcmp(saved_map, palette.map, sizeof(saved_map)) == 0);
    /* Palette cycling keeps register identities and clamps VGA components. */
    faded[0] = 255; faded[1] = 127; faded[2] = 64;
    amiga_palette_update(&palette, selected[1], 1, faded);
    assert(palette.colors[1] == 0x0fffU);
    assert(memcmp(saved_map, palette.map, sizeof(saved_map)) == 0);
}

int main(void)
{
    legacy_u16 pattern;
    check_palette();
    puts("Amiga palette checks passed");
    fflush(stdout);
#ifdef __mc68000__
    /* Every four rows cover all fill patterns. Two complete screens keep the
     * independent bit-by-bit verification practical on a 7 MHz processor. */
    pattern = 3;
    check_conversion(AMIGA_ROW_BYTES, pattern);
    puts("Amiga standard-pitch conversion passed");
    fflush(stdout);
    check_conversion(TEST_PITCH, pattern);
#else
    for (pattern = 0; pattern < 4; ++pattern) {
        check_conversion(AMIGA_ROW_BYTES, pattern);
        check_conversion(TEST_PITCH, pattern);
    }
#endif
    puts("AMIGA DISPLAY TEST PASS");
    return 0;
}
