#include "amiga.h"
#include "../../c/platform.h"
#include "../../c/keyboard.h"
#include "../../c/restunts.h"
#include "../../c/command_line.h"
#include "../../c/shape2d_internal.h"
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* libnix swaps to this stack when the CLI stack is smaller. */
unsigned long __stack = 20480;
extern void full_data_initialize(void);
#ifdef RESTUNTS_AMIGA_SMOKE
static legacy_u32 selftest_callbacks;
extern legacy_s16 roofbmpheight_copy, dashbmp_y_copy;
extern legacy_s8 replaybar_enabled;

static void selftest_tick(void) { ++selftest_callbacks; }
static int selftest_check(int condition, const char *description)
{
	if (!condition) {
		printf("AMIGA SELFTEST FAIL: %s\n", description);
		fflush(stdout);
	}
	return condition;
}

static int amiga_selftest(void)
{
	legacy_u8 colors[256 * 3], *pixels;
	legacy_u16 segment, handle, i;
	legacy_u16 retrace_states;
	legacy_u32 before, after, callback_count;
	legacy_s16 buttons, x, y;
	char readback[4];
	int ok = 1;
	unsigned long starting_chip = AvailMem(MEMF_CHIP);
	puts("AMIGA SELFTEST: memory and 320x200 display");
	fflush(stdout);
	dos_timer_setup_interrupt();
	kb_init_interrupt();
	/* Bind the same native screen pointers and row tables as the game. */
	full_data_initialize();
	segment = dos_memory_allocate(100);
	ok &= selftest_check(segment != 0, "resource arena allocation");
	if (!ok) return 1;
	pixels = dos_memory_make_pointer(segment, 0);
	pixels[0] = 0x5a;
	ok &= selftest_check(dos_memory_make_pointer(dos_memory_pointer_segment(pixels + 37),
		dos_memory_pointer_offset(pixels + 37)) == pixels + 37, "arena pointer round trip");
	ok &= selftest_check(dos_memory_make_pointer(dos_memory_pointer_segment(&readback[0]),
		dos_memory_pointer_offset(&readback[0])) == &readback[0], "native pointer round trip");
	printf("AMIGA SELFTEST: arena=%lu, planar buffers=%u, free after display=%lu\n",
		(unsigned long)dos_memory_resize(segment, 0xffffU) * 16UL,
		(unsigned)amiga_video_buffer_count(), (unsigned long)AvailMem(MEMF_PUBLIC));
	fflush(stdout);
	dos_video_set_mode_13h();
	for (i = 0; i < 256; ++i) {
		colors[i * 3] = (i & 7U) * 9U;
		colors[i * 3 + 1] = ((i >> 3) & 7U) * 9U;
		colors[i * 3 + 2] = ((i >> 6) & 3U) * 21U;
	}
	dos_video_set_palette(0, 256, colors);
	sprite_select_mcga_backbuffer();
	ok &= selftest_check(drawing_sprite.sprite_bitmapptr == screen_sprite.sprite_bitmapptr &&
		mcga_backbuffer_sprite == NULL, "indexed backbuffer reuse");
	pixels = dos_memory_make_pointer(0xA000U, 0);
	ok &= selftest_check(dos_memory_pointer_segment(pixels) == 0xA000U &&
		dos_memory_pointer_offset(pixels) == 0, "framebuffer segment identity");
	for (i = 0; i < AMIGA_SCREEN_BYTES; ++i) pixels[i] = (legacy_u8)((i % 320U) / 10U);
	sprite_present_mcga_backbuffer();
	/* Change palettes after both pages have been presented: the AROS bitmap
	 * includes private driver metadata that must survive repeated flips. */
	puts("AMIGA SELFTEST: scene palettes and replay Copper");
	fflush(stdout);
	amiga_video_present();
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
	ok &= selftest_check(amiga_video_color(9, 2) == 0x0a0 &&
		amiga_video_color(130, 37) == 0xe82, "world and Countach palette bands");
	replaybar_enabled = 1;
	amiga_video_present();
	ok &= selftest_check(amiga_video_color(150, 37) == 0xe82 &&
		amiga_video_color(151, 37) == 0xa00, "replay palette boundary");
	replaybar_enabled = 0;
	amiga_video_present();
	ok &= selftest_check(amiga_video_color(151, 37) == 0xe82, "replay palette removal");
	amiga_video_palette_race("COUN", 0);
	amiga_video_present();
	puts("AMIGA SELFTEST: input and 100 Hz timing");
	fflush(stdout);
	amiga_timer_pump();
	before = amiga_clock_ticks();
	retrace_states = 0;
	while (retrace_states != 3U && amiga_clock_ticks() - before < 50U) {
		retrace_states |= dos_video_get_status() ? 1U : 2U;
	}
	ok &= selftest_check(retrace_states == 3U, "beam retrace transitions");
	ok &= selftest_check(dos_mouse_init(320, 200) != 0, "mouse initialized");
	dos_mouse_set_position(123, 87);
	dos_mouse_get_state(&buttons, &x, &y);
	ok &= selftest_check(x == 123 && y == 87, "mouse position");
	/* Simulate blocking resource work before a race callback is installed. */
	before = amiga_clock_ticks();
	while (amiga_clock_ticks() - before < 10U) {}
	ok &= selftest_check(dos_timer_register_callback(selftest_tick) != 0, "timer callback registration");
	amiga_timer_pump();
	ok &= selftest_check(selftest_callbacks < 10U, "new callback excludes earlier elapsed ticks");
	before = dos_timer_get_realtime_counter();
	while (dos_timer_get_realtime_counter() - before < 10U) {}
	ok &= selftest_check(dos_timer_get_realtime_counter() - before < 40U,
		"timer polling does not redraw the display");
	ok &= selftest_check(selftest_callbacks >= 10U, "100 Hz timer callbacks");
	dos_timer_set_callbacks_suspended(1);
	callback_count = selftest_callbacks;
	before = dos_timer_get_realtime_counter();
	while (dos_timer_get_realtime_counter() - before < 5U) {}
	after = dos_timer_get_realtime_counter();
	ok &= selftest_check(selftest_callbacks == callback_count && after > before, "suspended callback clock");
	dos_timer_set_callbacks_suspended(0);
	dos_timer_unregister_callback(selftest_tick);
	puts("AMIGA SELFTEST: Paula DMA and AmigaDOS files");
	fflush(stdout);
	ok &= selftest_check(amiga_audio_selftest() != 0, "Paula audio lifecycle");
	ok &= selftest_check(dos_file_find_first((const legacy_s8 *)"SDMAIN.PVS") != NULL,
		"find existing main menu shapes");
	ok &= selftest_check(dos_file_find_first((const legacy_s8 *)"SDTITL.PVS") != NULL,
		"find existing title shapes");
	handle = dos_file_open((const legacy_s8 *)"AMIGATST.TMP", DOS_FILE_CREATE);
	ok &= selftest_check(handle != 0, "create file");
	if (handle != 0) {
		ok &= selftest_check(dos_file_write(handle, "TEST", 4) == 4, "write file");
		ok &= selftest_check(dos_file_seek(handle, 0, DOS_FILE_SEEK_BEGIN) == 0, "seek file");
		ok &= selftest_check(dos_file_read(handle, readback, 4) == 4 && memcmp(readback, "TEST", 4) == 0, "read file");
		ok &= selftest_check(dos_file_tell(handle) == 4, "file position");
		dos_file_close(handle);
		ok &= selftest_check(dos_file_find_first((const legacy_s8 *)"AMIGATST.*") != NULL, "file search");
		dos_file_remove((const legacy_s8 *)"AMIGATST.TMP");
	}
	printf("Amiga Chip RAM: before=%lu remaining=%lu; resource arena=%lu bytes; planar buffers=%u\n",
		starting_chip, (unsigned long)AvailMem(MEMF_CHIP),
		(unsigned long)dos_memory_resize(segment, 0xffffU) * 16UL,
		(unsigned)amiga_video_buffer_count());
	if (ok) puts("AMIGA SELFTEST PASS");
	return ok ? 0 : 1;
}
#endif

int main(int argc, char **argv)
{
	legacy_s16 info;
	BPTR data_lock = 0, previous_directory = 0;
	int result;
	if (atexit(amiga_platform_shutdown) != 0) return 1;
#ifdef RESTUNTS_AMIGA_SMOKE
	if (argc == 2 && strcmp(argv[1], "--amiga-selftest") == 0) return amiga_selftest();
#endif
	info = command_line_print_info(argc, (legacy_s8 **)argv);
	if (info != COMMAND_LINE_CONTINUE) return info;
	if (argc > 1 && strcmp(argv[1], "--data-dir") == 0) {
		if (argc < 3 || (data_lock = Lock(argv[2], ACCESS_READ)) == 0) {
			fputs("Cannot open --data-dir directory\n", stderr); return 1;
		}
		previous_directory = CurrentDir(data_lock);
		memmove(argv + 1, argv + 3, (argc - 2) * sizeof(*argv));
		argc -= 2;
	}
	if (argc > 32767) { fputs("Too many arguments\n", stderr); return 1; }
	full_data_initialize();
#ifdef RESTUNTS_AMIGA_SMOKE
	if (argc == 2 && strcmp(argv[1], "--amiga-smoke") == 0)
		result = amiga_game_smoke();
	else
#endif
		result = run_main_menu_loop((legacy_s16)argc, (legacy_s8 **)argv);
	if (data_lock != 0) { CurrentDir(previous_directory); UnLock(data_lock); }
	return result;
}
