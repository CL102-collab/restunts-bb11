#include <assert.h>
#include <string.h>

#include "../pixldump/legacy_context.h"
#include "../c/platform.h"
#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/shape2d.h"
#include "../c/shape2d_internal.h"

#define TEST_ENVIRONMENT_SEGMENT 4660U
#define TEST_ENVIRONMENT_SIZE 65536UL
#define TEST_PSP_SEGMENT 654U

static legacy_u8 test_psp[256];
static legacy_u8 test_environment[TEST_ENVIRONMENT_SIZE];
static legacy_s32 test_psp_available;
static legacy_u16 test_psp_segment;
static legacy_u8 test_psp_reference;
static struct SHAPE2D test_shapes[3];
static struct SPRITE test_sprites[3];
static legacy_u8 test_main_resource;
static legacy_u8 test_default_font;
static legacy_u8 test_secondary_font;
static legacy_u16 test_chunk_sizes[6];
static legacy_u16 test_driver_paragraphs;

struct SPRITE far *mouse_small_sprite = &test_sprites[0];
struct SPRITE far *mouse_medium_sprite = &test_sprites[1];
struct SPRITE far *mouse_background_sprite = &test_sprites[2];
void far *mainresptr = &test_main_resource;
void far *fontdefptr = &test_default_font;
void far *fontnptr = &test_secondary_font;

legacy_u16 file_paras_fatal(const legacy_s8 *filename)
{
	assert(strcmp((const legacy_char *)filename, "pc15.drv") == 0);
	return test_driver_paragraphs;
}

legacy_u16 mmgr_get_chunk_size(legacy_s8 far *pointer)
{
	for (legacy_u16 index = 0; index < 3U; index++) {
		if ((void *)pointer == &test_shapes[index]) {
			return test_chunk_sizes[index];
		}
	}
	if ((void *)pointer == mainresptr) {
		return test_chunk_sizes[3];
	}
	if ((void *)pointer == fontdefptr) {
		return test_chunk_sizes[4];
	}
	assert((void *)pointer == fontnptr);
	return test_chunk_sizes[5];
}

void far *dos_memory_get_psp(void)
{
	return test_psp_available ? &test_psp_reference : 0;
}

legacy_u16 dos_memory_pointer_offset(const void far *pointer)
{
	assert(pointer == &test_psp_reference);
	return test_psp_segment;
}

void far *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	assert(offset == 0);
	if (segment == test_psp_segment) {
		return test_psp;
	}
	assert(segment == TEST_ENVIRONMENT_SEGMENT);
	return test_environment;
}

static void prepare_environment(const char *program_path)
{
	memset(test_psp, 0, sizeof(test_psp));
	memset(test_environment, 0, sizeof(test_environment));
	test_psp_available = 1;
	test_psp_segment = TEST_PSP_SEGMENT;
	test_psp[0] = 0xcd;
	test_psp[1] = 0x20;
	test_psp[0x2c] = (legacy_u8)TEST_ENVIRONMENT_SEGMENT;
	test_psp[0x2d] = (legacy_u8)(TEST_ENVIRONMENT_SEGMENT >> LEGACY_BYTE_BITS);
	static const char variables[] = "PATH=C:\\DOS\0TEMP=C:\\TEMP\0";
	memcpy(test_environment, variables, sizeof(variables));
	test_environment[sizeof(variables)] = 1;
	strcpy((legacy_char *)test_environment + sizeof(variables) + 2U, program_path);
}

static void assert_context(legacy_u16 expected, legacy_s16 argc, legacy_s8 *argv[])
{
	assert((legacy_u16)pixldump_legacy_argv_si(argc, argv) == expected);
}

static void test_archived_argument_addresses(void)
{
	legacy_s8 *sample[] = {(legacy_s8 *)"PIXLDUMP", (legacy_s8 *)"my0000", (legacy_s8 *)"2",
						   (legacy_s8 *)"0"};
	legacy_s8 *bmp[] = {(legacy_s8 *)"PIXLDUMP", (legacy_s8 *)"my0000", (legacy_s8 *)"2",
						(legacy_s8 *)"0", (legacy_s8 *)"170"};

	prepare_environment("C:\\PIXLDUMP.EXE");
	/* CC38 and the complete argv strings were read from the archived DOS trace.
	 * The additional BMP argument moves that same CRT table to CC32. */
	assert_context(0xcc38, 4, sample);
	assert(pixldump_legacy_load_segment() == 0x029e);
	assert(pixldump_legacy_polygon_code_segment() == 0x1774);
	assert(pixldump_legacy_polygon_frame_pointer(pixldump_legacy_argv_si(4, sample), 0) == 0xca0a);
	assert_context(0xcc32, 5, bmp);
	assert(pixldump_legacy_polygon_frame_pointer(pixldump_legacy_argv_si(5, bmp), 1) == 0xca06);
	bmp[1] = (legacy_s8 *)"my0000.rpl";
	assert_context(0xcc2e, 5, bmp);
	test_psp_segment += 16U;
	assert(pixldump_legacy_load_segment() == 0x02ae);
	assert(pixldump_legacy_polygon_code_segment() == 0x1784);
	bmp[1] = (legacy_s8 *)"hill climb";
	/* Quotes have already been removed by the command-line parser. */
	assert_context(0xcc2e, 5, bmp);
	bmp[1] = (legacy_s8 *)"my0000";
	prepare_environment("C:\\GAMES\\STUNTS\\PIXLDUMP.EXE");
	assert_context(0xcc26, 5, bmp);
	prepare_environment("C:\\X\\PIXLDUMP.EXE");
	assert_context(0xcc30, 5, bmp);
	bmp[4] = (legacy_s8 *)"1000";
	assert_context(0xcc30, 5, bmp);
	bmp[4] = (legacy_s8 *)"10000";
	assert_context(0xcc2e, 5, bmp);
}

static void test_archived_polyinfo_allocation(void)
{
	prepare_environment("C:\\PIXLDUMP.EXE");
	test_driver_paragraphs = 140U;
	for (legacy_u16 index = 0; index < 3U; index++) {
		test_sprites[index].sprite_bitmapptr = &test_shapes[index];
		test_chunk_sizes[index] = 12U;
	}
	test_chunk_sizes[3] = 94U;
	test_chunk_sizes[4] = 115U;
	test_chunk_sizes[5] = 91U;
	/* Incremental Borland oracle: load segment 029E and polyinfo segment 3E61. Each allocation
	 * is supplied independently so resource changes must affect the result. */
	assert(pixldump_legacy_polyinfo_segment() == 0x3e61);
	for (legacy_u16 index = 0; index < 6U; index++) {
		test_chunk_sizes[index] += 7U;
		assert(pixldump_legacy_polyinfo_segment() == 0x3e68);
		test_chunk_sizes[index] -= 7U;
	}
	test_driver_paragraphs += 9U;
	assert(pixldump_legacy_polyinfo_segment() == 0x3e6a);
	test_psp_segment += 16U;
	assert(pixldump_legacy_polyinfo_segment() == 0x3e7a);
	test_psp_available = 0;
	assert(pixldump_legacy_load_segment() == 0);
	assert(pixldump_legacy_polygon_code_segment() == 0);
	assert(pixldump_legacy_polyinfo_segment() == 0);
}

static void test_environment_bounds_and_fallback(void)
{
	legacy_s8 *bmp[] = {(legacy_s8 *)"PIXLDUMP", (legacy_s8 *)"my0000", (legacy_s8 *)"2",
						(legacy_s8 *)"0", (legacy_s8 *)"170"};

	prepare_environment("C:\\PIXLDUMP.EXE");
	test_psp_available = 0;
	assert_context(0xcc3a, 5, bmp);
	test_psp_available = 1;
	test_psp[0] = 0;
	assert_context(0xcc3a, 5, bmp);
	test_psp[0] = 0xcd;
	test_psp[0x2c] = 0;
	test_psp[0x2d] = 0;
	assert_context(0xcc3a, 5, bmp);

	prepare_environment("C:\\PIXLDUMP.EXE");
	memset(test_environment, 'X', sizeof(test_environment));
	assert_context(0xcc3a, 5, bmp);
	test_environment[65534] = 0;
	test_environment[65535] = 0;
	assert_context(0xcc3a, 5, bmp);
	memset(test_environment, 'X', sizeof(test_environment));
	test_environment[65530] = 0;
	test_environment[65531] = 0;
	test_environment[65532] = 1;
	test_environment[65533] = 0;
	/* A one-character path ending at the final segment byte is valid. */
	test_environment[65535] = 0;
	assert_context(0xcc40, 5, bmp);
	test_environment[65535] = 'X';
	assert_context(0xcc3a, 5, bmp);

	memset(test_environment, 0, sizeof(test_environment));
	assert_context(0xcc3a, 5, bmp);
	/* Empty variable lists still have a double NUL before the path count. */
	test_environment[2] = 1;
	strcpy((legacy_char *)test_environment + 4U, "C:\\PIXLDUMP.EXE");
	assert_context(0xcc32, 5, bmp);
}

int main(void)
{
	test_archived_argument_addresses();
	test_archived_polyinfo_allocation();
	test_environment_bounds_and_fallback();
	return 0;
}
