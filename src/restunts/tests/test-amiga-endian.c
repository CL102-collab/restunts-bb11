#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/audio.h"
#include "../c/highscore.h"
#include "../c/platform.h"
#include "../c/shape2d.h"
#include "../c/shape3d.h"

/* These globals deliberately do not depend on the linker preserving DOS data
 * segment adjacency. Save/restore must copy the two sprites independently. */
struct SPRITE drawing_sprite;
struct SPRITE screen_sprite;

static legacy_u8 audio_bytes[256];

void far *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	assert(segment == 0x1234U && offset < sizeof(audio_bytes));
	return audio_bytes + offset;
}

legacy_u16 dos_memory_pointer_segment(const void far *pointer)
{
	assert((const legacy_u8 *)pointer >= audio_bytes &&
		   (const legacy_u8 *)pointer < audio_bytes + sizeof(audio_bytes));
	return 0x1234U;
}

legacy_u16 dos_memory_pointer_offset(const void far *pointer)
{
	return (legacy_u16)((const legacy_u8 *)pointer - audio_bytes);
}

extern void far *audio_read_far_pointer(const legacy_u8 far *source);
extern void audio_write_far_pointer(legacy_u8 far *destination, const void far *value);

static void test_unaligned_bitmap_header(void)
{
	/* A resource payload can start at an odd address, with little-endian words. */
	legacy_u8 bytes[17] = {0,	 0x40, 0x01, 0xc8, 0, 0x34, 0x12, 0x78, 0x56,
						   0xbc, 0x9a, 0xf0, 0xde, 1, 2,	3,	  4};
	const struct SHAPE2D *shape = (const struct SHAPE2D *)(bytes + 1);
	assert(shape2d_get_width(shape) == 320);
	assert(shape2d_get_height(shape) == 200);
	assert(shape2d_get_anchor_x(shape) == 0x1234U);
	assert(shape2d_get_anchor_y(shape) == 0x5678U);
	assert(shape2d_get_pos_x(shape) == 0x9abcU);
	assert(shape2d_get_pos_y(shape) == 0xdef0U);
}

static void test_highscore_file_order(void)
{
	legacy_u8 bytes[HIGHSCORE_ENTRY_SIZE_BYTES + 1U] = {0};
	struct HIGHSCORE_ENTRY *entry = (struct HIGHSCORE_ENTRY *)(bytes + 1);
	bytes[1U + HIGHSCORE_TIME_OFFSET] = 0x34;
	bytes[2U + HIGHSCORE_TIME_OFFSET] = 0x12;
	assert(highscore_entry_time(entry) == 0x1234U);
	highscore_entry_set_time(entry, 0x5678U);
	assert(bytes[1U + HIGHSCORE_TIME_OFFSET] == 0x78);
	assert(bytes[2U + HIGHSCORE_TIME_OFFSET] == 0x56);
}

static void test_vertex_file_order(void)
{
	legacy_u8 bytes[7] = {0, 0x34, 0x12, 0xfe, 0xff, 0, 0x80};
	struct SHAPE3D shape;
	struct VECTOR vertex;
	memset(&shape, 0, sizeof(shape));
	shape.shape3d_vertex_bytes = bytes + 1;
	shape3d_vertex_read(&shape, 0, &vertex);
	assert(vertex.x == 0x1234 && vertex.y == -2 && vertex.z == -32768);
	vertex.x = -3;
	vertex.y = 0x2345;
	vertex.z = 0x6789;
	shape3d_vertex_write(&shape, 0, &vertex);
	assert(bytes[1] == 0xfd && bytes[2] == 0xff);
	assert(bytes[3] == 0x45 && bytes[4] == 0x23);
	assert(bytes[5] == 0x89 && bytes[6] == 0x67);
}

static void test_audio_pointer_file_order(void)
{
	legacy_u8 bytes[5] = {0, 0x56, 0, 0x34, 0x12};
	assert(audio_read_far_pointer(bytes + 1) == audio_bytes + 0x56);
	audio_write_far_pointer(bytes + 1, audio_bytes + 0x78);
	assert(bytes[1] == 0x78 && bytes[2] == 0);
	assert(bytes[3] == 0x34 && bytes[4] == 0x12);
}

static void test_native_sprite_context(void)
{
	struct SPRITE saved[SPRITE_STATE_COUNT];
	drawing_sprite.sprite_left = 123;
	drawing_sprite.sprite_right = 456;
	screen_sprite.sprite_left = 789;
	screen_sprite.sprite_right = 999;
	sprite_save_context(saved);
	assert(saved[0].sprite_left == 123 && saved[0].sprite_right == 456);
	assert(saved[1].sprite_left == 789 && saved[1].sprite_right == 999);
	drawing_sprite.sprite_left = 0;
	screen_sprite.sprite_left = 0;
	sprite_restore_context(saved);
	assert(drawing_sprite.sprite_left == 123 && screen_sprite.sprite_left == 789);
}

int main(void)
{
	test_unaligned_bitmap_header();
	test_highscore_file_order();
	test_vertex_file_order();
	test_audio_pointer_file_order();
	test_native_sprite_context();
	puts("Amiga endian and native sprite tests passed.");
	return 0;
}
