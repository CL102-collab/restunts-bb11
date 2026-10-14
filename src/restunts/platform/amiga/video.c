#include "amiga.h"
#include "../../c/platform.h"
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;
static struct Screen *screen;
static struct Window *window;
static struct BitMap second_bitmap;
static struct BitMap *visible_bitmap;
static legacy_u8 *pixels;
static UWORD *empty_pointer;
static struct AMIGA_PALETTE palette;
static legacy_u32 last_present;
static legacy_u8 presenting;
static legacy_u8 drawing_frame;
static legacy_u8 auto_refresh = 1;
static legacy_u16 buffer_count;

static void video_fail(const char *message)
{
	fprintf(stderr, "Amiga video: %s\n", message);
	fflush(stderr);
#ifdef RESTUNTS_AMIGA_TRACE
	printf("Amiga video: %s; free public=%lu, largest=%lu\n", message,
		(unsigned long)AvailMem(MEMF_PUBLIC),
		(unsigned long)AvailMem(MEMF_PUBLIC | MEMF_LARGEST));
	fflush(stdout);
#endif
	dos_process_exit(1);
}

legacy_u8 *amiga_video_pixels(void)
{
	if (pixels == NULL) {
		pixels = calloc(1, AMIGA_SCREEN_BYTES);
		if (pixels == NULL) {
			video_fail("cannot allocate 64000-byte chunky framebuffer");
		}
	}
	return pixels;
}

legacy_u8 *amiga_video_memory_base(void) { return pixels; }

legacy_u16 amiga_video_buffer_count(void)
{
	if (buffer_count == 0) dos_video_set_mode_13h();
	return buffer_count;
}

struct Window *amiga_video_window(void)
{
	return window;
}

void dos_video_set_mode_13h(void)
{
	struct NewScreen settings;
	struct NewWindow window_settings;
	legacy_u16 plane;
	/* Leave room for the largest stock car/opponent and Alpine scenery. */
	legacy_u32 minimum_arena = 272UL * 1024UL;
	if (screen != NULL) {
		return;
	}
	GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 37);
	IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
	if (GfxBase == NULL || IntuitionBase == NULL) {
		video_fail("Kickstart 2.0 or newer is required");
	}
	(void)amiga_video_pixels();
	memset(&settings, 0, sizeof(settings));
	settings.Width = AMIGA_WIDTH;
	settings.Height = AMIGA_HEIGHT;
	settings.Depth = AMIGA_DEPTH;
	settings.BlockPen = 1;
	settings.Type = CUSTOMSCREEN | SCREENQUIET;
	screen = OpenScreen(&settings);
	if (screen == NULL) {
		video_fail("cannot open 320x200 32-color Chip RAM screen");
	}
	ShowTitle(screen, FALSE);
	visible_bitmap = &screen->BitMap;
	memset(&window_settings, 0, sizeof(window_settings));
	window_settings.Width = AMIGA_WIDTH;
	window_settings.Height = AMIGA_HEIGHT;
	window_settings.DetailPen = 0;
	window_settings.BlockPen = 1;
	window_settings.IDCMPFlags = IDCMP_RAWKEY | IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS | IDCMP_DELTAMOVE |
		IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW;
	window_settings.Flags = WFLG_BORDERLESS | WFLG_ACTIVATE | WFLG_RMBTRAP | WFLG_REPORTMOUSE;
	window_settings.Screen = screen;
	window_settings.Type = CUSTOMSCREEN;
	window = OpenWindow(&window_settings);
	if (window == NULL) {
		video_fail("cannot create input window");
	}
	empty_pointer = AllocMem(16, MEMF_CHIP | MEMF_CLEAR);
	if (empty_pointer == NULL) {
		video_fail("cannot allocate mouse pointer");
	}
	SetPointer(window, empty_pointer, 1, 16, 0, 0);
	/* First measure the real screen and Intuition allocation cost. Closing a
	 * boot CLI changes that cost, so a fixed reserve can exhaust Chip RAM. */
	buffer_count = 1;
	if (AvailMem(MEMF_PUBLIC) >= minimum_arena + AMIGA_SYSTEM_HEADROOM +
		(legacy_u32)AMIGA_PLANE_BYTES * AMIGA_DEPTH &&
		AvailMem(MEMF_PUBLIC | MEMF_LARGEST) >= minimum_arena) {
		InitBitMap(&second_bitmap, AMIGA_DEPTH, AMIGA_WIDTH, AMIGA_HEIGHT);
		for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
			second_bitmap.Planes[plane] = AllocRaster(AMIGA_WIDTH, AMIGA_HEIGHT);
			if (second_bitmap.Planes[plane] == NULL) break;
			memset(second_bitmap.Planes[plane], 0, AMIGA_PLANE_BYTES);
		}
		if (plane == AMIGA_DEPTH &&
			AvailMem(MEMF_PUBLIC | MEMF_LARGEST) >= minimum_arena) buffer_count = 2;
		else {
			while (plane != 0) {
				--plane;
				FreeRaster(second_bitmap.Planes[plane], AMIGA_WIDTH, AMIGA_HEIGHT);
				second_bitmap.Planes[plane] = NULL;
			}
		}
	}
	LoadRGB4(&screen->ViewPort, palette.colors, AMIGA_COLORS);
	ScreenToFront(screen);
}

void amiga_video_present(void)
{
	struct BitMap *target;
	drawing_frame = 0;
	if (screen == NULL || presenting) {
		return;
	}
	presenting = 1;
	if (second_bitmap.Planes[0] == NULL) {
		/* With little free RAM, conversion writes the bitmap and can visibly
		 * tear. Start at vertical blank to make the best use of that interval. */
		WaitTOF();
		WaitBlit();
		amiga_chunky_to_planar(pixels, palette.map, (legacy_u8 **)screen->BitMap.Planes,
							   screen->BitMap.BytesPerRow);
		last_present = amiga_clock_ticks();
		presenting = 0;
		return;
	}
	target = visible_bitmap == &screen->BitMap ? &second_bitmap : &screen->BitMap;
	WaitBlit();
	amiga_chunky_to_planar(pixels, palette.map, (legacy_u8 **)target->Planes,
						   target->BytesPerRow);
	WaitTOF();
	screen->ViewPort.RasInfo->BitMap = target;
	ScrollVPort(&screen->ViewPort);
	/* Let the Copper latch the new pointers before the old page is writable. */
	WaitTOF();
	visible_bitmap = target;
	last_present = amiga_clock_ticks();
	presenting = 0;
}

void amiga_video_begin_frame(void) { drawing_frame = 1; }

legacy_u8 amiga_video_set_auto_refresh(legacy_u8 enabled)
{
	legacy_u8 previous = auto_refresh;
	auto_refresh = enabled != 0;
	return previous;
}

void amiga_video_refresh(void)
{
	/* Legacy menus draw directly into VGA memory between input polls. */
	if (screen != NULL && auto_refresh && !drawing_frame && amiga_clock_ticks() - last_present >= 4U) {
		amiga_video_present();
	}
}

void dos_video_set_palette(legacy_u16 first, legacy_u16 count, legacy_u8 *rgb)
{
	amiga_palette_update(&palette, first, count, rgb);
	if (screen != NULL) {
		LoadRGB4(&screen->ViewPort, palette.colors, AMIGA_COLORS);
		amiga_video_present();
	}
}

legacy_s16 dos_video_get_status(void)
{
	legacy_u16 vertical;
	/* Retrace polling must not present a frame: WaitTOF would sample the same
	 * beam phase every time and trap the DOS startup calibration loop. */
	amiga_input_pump();
	amiga_timer_pump();
	vertical = (legacy_u16)(((*(volatile UWORD *)0xDFF004UL & 1U) << 8) |
		(*(volatile UWORD *)0xDFF006UL >> 8));
	/* The non-interlaced 200-line picture occupies beam lines 44 through243. */
	return vertical < 44U || vertical >= 244U ? DOS_VIDEO_RETRACE_STATUS_BIT : 0;
}

legacy_s16 video_get_status(void) { return dos_video_get_status(); }
legacy_u8 dos_video_enable_planar_pages(void) { return 0; }
void dos_video_set_write_planes(legacy_u8 mask) { (void)mask; }
void dos_video_set_read_plane(legacy_u8 plane) { (void)plane; }
void dos_video_show_page(legacy_u16 address) { (void)address; amiga_video_present(); }
void dos_video_set_mode4(void) { dos_video_set_mode_13h(); }
void dos_video_set_mode7(void) { amiga_video_shutdown(); }

void amiga_video_shutdown(void)
{
	legacy_u16 plane;
	if (window != NULL) {
		ClearPointer(window);
		CloseWindow(window);
		window = NULL;
	}
	if (empty_pointer != NULL) {
		FreeMem(empty_pointer, 16);
		empty_pointer = NULL;
	}
	if (screen != NULL) {
		screen->ViewPort.RasInfo->BitMap = &screen->BitMap;
		ScrollVPort(&screen->ViewPort);
		WaitTOF();
		WaitTOF();
		CloseScreen(screen);
		screen = NULL;
	}
	for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
		if (second_bitmap.Planes[plane] != NULL) {
			FreeRaster(second_bitmap.Planes[plane], AMIGA_WIDTH, AMIGA_HEIGHT);
			second_bitmap.Planes[plane] = NULL;
		}
	}
	free(pixels);
	pixels = NULL;
	buffer_count = 0;
	if (IntuitionBase != NULL) {
		CloseLibrary((struct Library *)IntuitionBase);
		IntuitionBase = NULL;
	}
	if (GfxBase != NULL) {
		CloseLibrary((struct Library *)GfxBase);
		GfxBase = NULL;
	}
}
