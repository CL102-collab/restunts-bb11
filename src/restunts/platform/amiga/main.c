#include "amiga.h"
#include "../../c/platform.h"
#include "../../c/keyboard.h"
#include "../../c/restunts.h"
#include "../../c/command_line.h"
#include "../../c/fileio.h"
#include "../../c/shape2d_internal.h"
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AMIGA_STACK_BYTES (20UL * 1024UL)
#define AMIGA_ARGUMENT_DATA_DIRECTORY 1
#define AMIGA_DATA_DIRECTORY_ARGUMENTS 2

/* libnix swaps to this stack when the CLI stack is smaller. */
legacy_u32 __stack = AMIGA_STACK_BYTES;
extern void full_data_initialize(void);
#ifdef RESTUNTS_AMIGA_SMOKE
extern struct GfxBase *GfxBase;
#define AMIGA_SELFTEST_ARENA_PARAGRAPHS 100U
static legacy_u32 selftest_callbacks;
extern legacy_s16 roofbmpheight_copy, dashbmp_y_copy;
extern legacy_s8 replaybar_enabled;

static void selftest_tick(void)
{
	++selftest_callbacks;
}
static legacy_int selftest_check(legacy_int condition, const legacy_char *description)
{
	if (!condition) {
		printf("AMIGA SELFTEST FAIL: %s\n", description);
		fflush(stdout);
	}
	return condition;
}

static legacy_u8 selftest_visible_pixel(const struct BitMap *bitmap, legacy_u16 x, legacy_u16 y)
{
	legacy_u8 pixel = 0;
	for (legacy_u16 plane = 0; plane < AMIGA_DEPTH; ++plane) {
		legacy_u8 byte = bitmap->Planes[plane][(legacy_u32)y * bitmap->BytesPerRow + (x >> 3)];
		pixel |= ((byte >> (7U - (x & 7U))) & 1U) << plane;
	}
	return pixel;
}

static legacy_int selftest_scaled_image(const struct BitMap *bitmap, legacy_u16 height)
{
	static const legacy_u16 columns[] = {0, 159, 319};
	for (legacy_u16 y = 0; y < height; ++y) {
		legacy_u16 source = (legacy_u32)y * AMIGA_HEIGHT / height;
		for (legacy_u16 column = 0; column < sizeof(columns) / sizeof(columns[0]); ++column) {
			legacy_u16 x = columns[column];
			if (selftest_visible_pixel(bitmap, x, y) != ((x / 10U + source) & 15U)) {
				return 0;
			}
		}
	}
	return 1;
}

static legacy_int selftest_visible_row(const struct BitMap *bitmap, const legacy_u8 *pixels,
									   legacy_u16 height, legacy_u16 y, const legacy_u8 *map)
{
	legacy_u16 source = (legacy_u32)y * AMIGA_HEIGHT / height;
	for (legacy_u16 x = 0; x < AMIGA_WIDTH; ++x) {
		if (selftest_visible_pixel(bitmap, x, y) !=
			map[pixels[(legacy_u32)source * AMIGA_WIDTH + x]]) {
			return 0;
		}
	}
	return 1;
}

static legacy_int amiga_selftest(void)
{
	legacy_u32 starting_chip = AvailMem(MEMF_CHIP);
	puts("AMIGA SELFTEST: memory and 320x200 display");
	fflush(stdout);
	dos_timer_setup_interrupt();
	kb_init_interrupt();
	/* Bind the same native screen pointers and row tables as the game. */
	full_data_initialize();
	legacy_u16 segment = dos_memory_allocate(AMIGA_SELFTEST_ARENA_PARAGRAPHS);
	legacy_int ok = 1;
	ok &= selftest_check(segment != 0, "resource arena allocation");
	if (!ok) {
		return 1;
	}
	legacy_u8 *pixels = dos_memory_make_pointer(segment, 0);
	legacy_char readback[4];
	pixels[0] = 0x5a;
	ok &= selftest_check(dos_memory_make_pointer(dos_memory_pointer_segment(pixels + 37),
												 dos_memory_pointer_offset(pixels + 37)) ==
							 pixels + 37,
						 "arena pointer round trip");
	ok &= selftest_check(dos_memory_make_pointer(dos_memory_pointer_segment(&readback[0]),
												 dos_memory_pointer_offset(&readback[0])) ==
							 &readback[0],
						 "native pointer round trip");
	printf("AMIGA SELFTEST: arena=%" LEGACY_PRIu32
		   ", planar buffers=%u, free after display=%" LEGACY_PRIu32 "\n",
		   (legacy_u32)(dos_memory_resize(segment, LEGACY_U16_MAX) * AMIGA_PARAGRAPH_BYTES),
		   (legacy_uint)amiga_video_buffer_count(), (legacy_u32)AvailMem(MEMF_PUBLIC));
	fflush(stdout);
	dos_video_set_mode_13h();
	struct Window *display_window = amiga_video_window();
	legacy_u16 expected_height =
		(GfxBase->DisplayFlags & PAL) ? AMIGA_PAL_HEIGHT : AMIGA_NTSC_HEIGHT;
	ok &= selftest_check(display_window != NULL, "display window initialized");
	if (!ok) {
		return 1;
	}
	struct Screen *display_screen = display_window->WScreen;
	struct BitMap *display_bitmap = display_screen->ViewPort.RasInfo->BitMap;
	ok &= selftest_check(amiga_video_display_height() == expected_height &&
							 display_window->Height == expected_height &&
							 display_screen->Height == expected_height &&
							 display_bitmap->Rows == expected_height &&
							 display_bitmap->Depth == AMIGA_DEPTH,
						 "native 4:3 raster geometry");
	printf("AMIGA SELFTEST: 320x%u %s raster for 320x200 logical image\n",
		   (legacy_uint)expected_height, expected_height == AMIGA_PAL_HEIGHT ? "PAL" : "NTSC");
	fflush(stdout);
	legacy_u8 colors[AMIGA_DOS_COLORS * AMIGA_RGB_COMPONENTS];
	for (legacy_u16 i = 0; i < AMIGA_DOS_COLORS; ++i) {
		colors[i * 3] = (i & 7U) * 9U;
		colors[i * 3 + 1] = ((i >> 3) & 7U) * 9U;
		colors[i * 3 + 2] = ((i >> 6) & 3U) * 21U;
	}
	dos_video_set_palette(0, AMIGA_DOS_COLORS, colors);
	sprite_select_mcga_backbuffer();
	ok &= selftest_check(drawing_sprite.sprite_bitmapptr == screen_sprite.sprite_bitmapptr &&
							 mcga_backbuffer_sprite == NULL,
						 "indexed backbuffer reuse");
	pixels = dos_memory_make_pointer(AMIGA_VIDEO_SEGMENT, 0);
	ok &= selftest_check(dos_memory_pointer_segment(pixels) == AMIGA_VIDEO_SEGMENT &&
							 dos_memory_pointer_offset(pixels) == 0,
						 "framebuffer segment identity");
	for (legacy_u16 i = 0; i < AMIGA_SCREEN_BYTES; ++i) {
		pixels[i] = (legacy_u8)(((i % AMIGA_WIDTH) / 10U + i / AMIGA_WIDTH) & 15U);
	}
	sprite_present_mcga_backbuffer();
	/* Change palettes after both pages have been presented: the AROS bitmap
	 * includes private driver metadata that must survive repeated flips. */
	puts("AMIGA SELFTEST: scene palettes and replay Copper");
	fflush(stdout);
	amiga_video_present();
	ok &= selftest_check(selftest_scaled_image(display_bitmap, expected_height),
						 "visible scaled scanlines after page flips");
	amiga_video_palette_scene("SDMSEL", "!pal");
	amiga_video_present();
	ok &= selftest_check(amiga_video_color(0, 108) == 0x470, "original main palette");
	amiga_video_palette_scene("SDCSEL", "!pal");
	amiga_video_present();
	ok &= selftest_check(amiga_video_color(0, 108) == 0x555, "original car palette");
	roofbmpheight_copy = 9;
	dashbmp_y_copy = 130;
	replaybar_enabled = 0;
	amiga_video_palette_race("COUN", 1);
	amiga_video_present();
	ok &= selftest_check(amiga_video_color(9, 2) == 0x0a0 && amiga_video_color(130, 37) == 0xe82,
						 "world and Countach palette bands");
	replaybar_enabled = 1;
	amiga_video_present();
	ok &= selftest_check(amiga_video_color(150, 37) == 0xe82 && amiga_video_color(151, 37) == 0xa00,
						 "replay palette boundary");
	struct AMIGA_PALETTE row_palette;
	memset(&row_palette, 0, sizeof(row_palette));
	legacy_u16 physical_bar = amiga_display_row(AMIGA_REPLAY_TOP, expected_height);
	amiga_palette_select(&row_palette, "STDBCOUN", "!pal", AMIGA_PALETTE_BANK_COLORS,
						 AMIGA_PALETTE_BANK_COLORS);
	ok &= selftest_check(selftest_visible_row(display_bitmap, pixels, expected_height,
											  physical_bar - 1U, row_palette.map),
						 "visible dashboard before scaled replay boundary");
	amiga_palette_select(&row_palette, "SDGAME", "!pal", AMIGA_PALETTE_BANK_COLORS,
						 AMIGA_PALETTE_BANK_COLORS);
	ok &= selftest_check(selftest_visible_row(display_bitmap, pixels, expected_height, physical_bar,
											  row_palette.map),
						 "visible replay at scaled boundary");
	replaybar_enabled = 0;
	amiga_video_present();
	ok &= selftest_check(amiga_video_color(151, 37) == 0xe82, "replay palette removal");
	amiga_video_palette_race("COUN", 0);
	amiga_video_present();
	puts("AMIGA SELFTEST: input and 100 Hz timing");
	fflush(stdout);
	amiga_timer_pump();
	legacy_u32 before = amiga_clock_ticks();
	legacy_u16 retrace_states = 0;
	while (retrace_states != 3U && amiga_clock_ticks() - before < 50U) {
		retrace_states |= dos_video_get_status() ? 1U : 2U;
	}
	ok &= selftest_check(retrace_states == 3U, "beam retrace transitions");
	ok &= selftest_check(dos_mouse_init(AMIGA_WIDTH, AMIGA_HEIGHT) != 0, "mouse initialized");
	dos_mouse_set_position(123, 87);
	legacy_s16 buttons, x, y;
	dos_mouse_get_state(&buttons, &x, &y);
	ok &= selftest_check(x == 123 && y == 87, "mouse position");
	/* Simulate blocking resource work before a race callback is installed. */
	before = amiga_clock_ticks();
	while (amiga_clock_ticks() - before < 10U) {
	}
	ok &= selftest_check(dos_timer_register_callback(selftest_tick) != 0,
						 "timer callback registration");
	amiga_timer_pump();
	ok &= selftest_check(selftest_callbacks < 10U, "new callback excludes earlier elapsed ticks");
	before = dos_timer_get_realtime_counter();
	while (dos_timer_get_realtime_counter() - before < 10U) {
	}
	ok &= selftest_check(dos_timer_get_realtime_counter() - before < 40U,
						 "timer polling does not redraw the display");
	ok &= selftest_check(selftest_callbacks >= 10U, "100 Hz timer callbacks");
	dos_timer_set_callbacks_suspended(1);
	legacy_u32 callback_count = selftest_callbacks;
	before = dos_timer_get_realtime_counter();
	while (dos_timer_get_realtime_counter() - before < 5U) {
	}
	legacy_u32 after = dos_timer_get_realtime_counter();
	ok &= selftest_check(selftest_callbacks == callback_count && after > before,
						 "suspended callback clock");
	dos_timer_set_callbacks_suspended(0);
	dos_timer_unregister_callback(selftest_tick);
	puts("AMIGA SELFTEST: Paula DMA and AmigaDOS files");
	fflush(stdout);
	ok &= selftest_check(amiga_audio_selftest() != 0, "Paula audio lifecycle");
	ok &= selftest_check(dos_file_find_first((const legacy_s8 *)"SDMAIN.PVS") != NULL,
						 "find existing main menu shapes");
	ok &= selftest_check(dos_file_find_first((const legacy_s8 *)"SDTITL.PVS") != NULL,
						 "find existing title shapes");
	legacy_u16 handle = dos_file_open((const legacy_s8 *)"AMIGATST.TMP", DOS_FILE_CREATE);
	ok &= selftest_check(handle != 0, "create file");
	if (handle != 0) {
		ok &= selftest_check(dos_file_write(handle, "TEST", 4) == 4, "write file");
		ok &= selftest_check(dos_file_seek(handle, 0, DOS_FILE_SEEK_BEGIN) == 0, "seek file");
		ok &= selftest_check(dos_file_read(handle, readback, 4) == 4 &&
								 memcmp(readback, "TEST", 4) == 0,
							 "read file");
		ok &= selftest_check(dos_file_tell(handle) == 4, "file position");
		dos_file_close(handle);
		ok &= selftest_check(dos_file_find_first((const legacy_s8 *)"AMIGATST.*") != NULL,
							 "file search");
		dos_file_remove((const legacy_s8 *)"AMIGATST.TMP");
	}
	printf("Amiga Chip RAM: before=%" LEGACY_PRIu32 " remaining=%" LEGACY_PRIu32
		   "; resource arena=%" LEGACY_PRIu32 " bytes; planar buffers=%u\n",
		   starting_chip, (legacy_u32)AvailMem(MEMF_CHIP),
		   (legacy_u32)(dos_memory_resize(segment, LEGACY_U16_MAX) * AMIGA_PARAGRAPH_BYTES),
		   (legacy_uint)amiga_video_buffer_count());
	if (ok) {
		puts("AMIGA SELFTEST PASS");
	}
	return ok ? 0 : 1;
}
#endif

legacy_int main(legacy_int argc, legacy_char **argv)
{
	/* AmigaDOS resolves PROGDIR: beside the executable, even after CurrentDir. */
	file_car_set_base((const legacy_s8 *)"PROGDIR:");
	if (atexit(amiga_platform_shutdown) != 0) {
		return 1;
	}
#ifdef RESTUNTS_AMIGA_SMOKE
	if (argc == 2 && strcmp(argv[1], "--amiga-selftest") == 0) {
		return amiga_selftest();
	}
#endif
	legacy_s16 info = command_line_print_info(argc, (legacy_s8 **)argv);
	if (info != COMMAND_LINE_CONTINUE) {
		return info;
	}
	BPTR data_lock = 0, previous_directory = 0;
	if (argc > AMIGA_ARGUMENT_DATA_DIRECTORY &&
		strcmp(argv[AMIGA_ARGUMENT_DATA_DIRECTORY], "--data-dir") == 0) {
		if (argc <= AMIGA_DATA_DIRECTORY_ARGUMENTS ||
			(data_lock = Lock(argv[AMIGA_DATA_DIRECTORY_ARGUMENTS], ACCESS_READ)) == 0) {
			fputs("Cannot open --data-dir directory\n", stderr);
			return 1;
		}
		previous_directory = CurrentDir(data_lock);
		memmove(argv + AMIGA_ARGUMENT_DATA_DIRECTORY,
				argv + AMIGA_ARGUMENT_DATA_DIRECTORY + AMIGA_DATA_DIRECTORY_ARGUMENTS,
				(argc - AMIGA_DATA_DIRECTORY_ARGUMENTS) * sizeof(*argv));
		argc -= AMIGA_DATA_DIRECTORY_ARGUMENTS;
	}
	if (argc > (legacy_int)LEGACY_S16_MAX) {
		fputs("Too many arguments\n", stderr);
		return 1;
	}
	full_data_initialize();
	legacy_s16 result;
#ifdef RESTUNTS_AMIGA_SMOKE
	if (argc == 2 && strcmp(argv[1], "--amiga-smoke") == 0) {
		result = amiga_game_smoke();
	} else {
		result = run_main_menu_loop((legacy_s16)argc, (legacy_s8 **)argv);
	}
#else
	result = run_main_menu_loop((legacy_s16)argc, (legacy_s8 **)argv);
#endif
	if (data_lock != 0) {
		CurrentDir(previous_directory);
		UnLock(data_lock);
	}
	return result;
}
