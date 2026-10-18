#include "amiga.h"
#include "../../c/platform.h"
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/copper.h>
#include <graphics/gfxmacros.h>
#include <hardware/custom.h>
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
static struct BitMap original_bitmap, second_bitmap;
static struct BitMap *visible_bitmap, *display_bitmap;
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

/* The original switches its sixteen gameplay colors at the roof, windshield,
 * dashboard and replay bar. Two banks allow the first three regions to share
 * our five-plane screen; only the replay bar needs a Copper palette change. */
static struct AMIGA_PALETTE dashboard_palette, replay_palette;
static legacy_u8 race_palette_active;
static struct UCopList *replay_copper;
static legacy_s16 copper_bar = -1;
extern legacy_s16 roofbmpheight_copy, dashbmp_y_copy;
extern legacy_s8 replaybar_enabled;

static legacy_u16 replay_boundary(void)
{
	return replaybar_enabled ? 151 : AMIGA_HEIGHT;
}

static legacy_u16 palette_boundary(legacy_s16 value)
{
	return value < 0 ? 0 : (legacy_u16)value > AMIGA_HEIGHT ? AMIGA_HEIGHT : (legacy_u16)value;
}

static const struct AMIGA_PALETTE *palette_at_row(legacy_u16 y)
{
	if (!race_palette_active) {
		return &palette;
	}
	if (y >= replay_boundary()) {
		return &replay_palette;
	}
	if (y < palette_boundary(roofbmpheight_copy) || y >= palette_boundary(dashbmp_y_copy)) {
		return &dashboard_palette;
	}
	return &palette;
}

legacy_u16 amiga_video_color(legacy_u16 y, legacy_u8 pixel)
{
	const struct AMIGA_PALETTE *p = palette_at_row(y);
	return p->colors[p->map[pixel] - p->first_register];
}

static void palette_remove_copper(void)
{
	if (replay_copper == NULL) {
		return;
	}
	screen->ViewPort.UCopIns = NULL;
	if (RethinkDisplay() != 0) {
		video_fail("cannot rebuild display Copper lists");
	}
	WaitTOF();
	FreeCopList(replay_copper->FirstCopList);
	FreeMem(replay_copper, sizeof(*replay_copper));
	replay_copper = NULL;
}

static void palette_apply(legacy_u8 force)
{
	legacy_u16 colors[AMIGA_COLORS], i;
	legacy_s16 bar = race_palette_active ? replay_boundary() : AMIGA_HEIGHT;
	struct Custom *custom = (struct Custom *)0xdff000UL;
	if (screen == NULL) {
		return;
	}
#ifdef RESTUNTS_AMIGA_TRACE
	if (force) {
		printf("Amiga palette apply: race=%u copper=%p bar=%d previous=%d beforeRGB\n",
			   (unsigned)race_palette_active, (void *)replay_copper, (int)bar, (int)copper_bar);
		fflush(stdout);
	}
#endif
	if (force) {
		memcpy(colors, palette.colors, sizeof(colors));
		if (race_palette_active) {
			memcpy(colors + 16, dashboard_palette.colors, 16 * sizeof(colors[0]));
		}
		LoadRGB4(&screen->ViewPort, colors, AMIGA_COLORS);
#ifdef RESTUNTS_AMIGA_TRACE
		puts("Amiga palette apply: afterRGB");
		fflush(stdout);
#endif
	}
	if (!force && bar == copper_bar) {
		return;
	}
	palette_remove_copper();
	copper_bar = bar;
	if (bar >= (legacy_s16)AMIGA_HEIGHT) {
		return;
	}
	replay_copper = AllocMem(sizeof(*replay_copper), MEMF_PUBLIC | MEMF_CLEAR);
	if (replay_copper == NULL) {
		video_fail("cannot allocate replay palette list");
	}
	if (UCopperListInit(replay_copper, 36) == NULL) {
		video_fail("cannot initialize replay palette list");
	}
	CWAIT(replay_copper, 0, 0);
	for (i = 0; i < 16; ++i) {
		CMOVE(replay_copper, custom->color[16 + i], dashboard_palette.colors[i]);
	}
	CWAIT(replay_copper, bar, 0);
	for (i = 0; i < 16; ++i) {
		CMOVE(replay_copper, custom->color[16 + i], replay_palette.colors[i]);
	}
	CEND(replay_copper);
	screen->ViewPort.UCopIns = replay_copper;
	if (RethinkDisplay() != 0) {
		video_fail("cannot rebuild display Copper lists");
	}
}

void amiga_video_palette_scene(const char *resource, const char *chunk)
{
#ifdef RESTUNTS_AMIGA_TRACE
	printf("Amiga palette scene: %s %s select\n", resource, chunk);
	fflush(stdout);
#endif
	race_palette_active = 0;
	amiga_palette_select(&palette, resource, chunk, strcmp(chunk, "!def") == 0 ? 16 : AMIGA_COLORS,
						 0);
#ifdef RESTUNTS_AMIGA_TRACE
	puts("Amiga palette scene: apply");
	fflush(stdout);
#endif
	palette_apply(1);
#ifdef RESTUNTS_AMIGA_TRACE
	puts("Amiga palette scene: complete");
	fflush(stdout);
#endif
}

void amiga_video_palette_race(const char *car, legacy_u8 active)
{
	char resource[9] = "STDB";
	if (!active) {
		amiga_video_palette_scene("SDTITL", "!def");
		return;
	}
	memcpy(resource + 4, car, 4);
	memcpy(&dashboard_palette, &palette, sizeof(palette));
	memcpy(&replay_palette, &palette, sizeof(palette));
	if (amiga_palette_original(resource, "!pal") != NULL) {
		amiga_palette_select(&dashboard_palette, resource, "!pal", 16, 16);
	} else {
		amiga_palette_select(&dashboard_palette, "SDTITL", "!def", 16, 16);
	}
	amiga_palette_select(&replay_palette, "SDGAME", "!pal", 16, 16);
	amiga_palette_select(&palette, "SDTITL", "!def", 16, 0);
	race_palette_active = 1;
	palette_apply(1);
}

static void palette_convert(struct BitMap *target)
{
	legacy_u16 top = 0, bottom, plane;
	while (top < AMIGA_HEIGHT) {
		const struct AMIGA_PALETTE *p = palette_at_row(top);
		legacy_u8 *planes[AMIGA_DEPTH];
		for (bottom = top + 1; bottom < AMIGA_HEIGHT && palette_at_row(bottom) == p; ++bottom) {
		}
		for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
			planes[plane] = target->Planes[plane] + (legacy_u32)top * target->BytesPerRow;
		}
		amiga_chunky_to_planar_rows(pixels + (legacy_u32)top * AMIGA_WIDTH, p->map, planes,
									target->BytesPerRow, bottom - top);
		top = bottom;
	}
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

legacy_u8 *amiga_video_memory_base(void)
{
	return pixels;
}

legacy_u16 amiga_video_buffer_count(void)
{
	if (buffer_count == 0) {
		dos_video_set_mode_13h();
	}
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
	struct Library *aros_library;
	legacy_u8 can_flip;
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
	display_bitmap = screen->ViewPort.RasInfo->BitMap;
	original_bitmap = *display_bitmap;
	visible_bitmap = &original_bitmap;
	memset(&window_settings, 0, sizeof(window_settings));
	window_settings.Width = AMIGA_WIDTH;
	window_settings.Height = AMIGA_HEIGHT;
	window_settings.DetailPen = 0;
	window_settings.BlockPen = 1;
	window_settings.IDCMPFlags = IDCMP_RAWKEY | IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS |
								 IDCMP_DELTAMOVE | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW;
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
	/* AROS caches a separate chipset bitmap inside its display driver.
	 * ScrollVPort does not reread public plane pointers there. Use its actual
	 * visible bitmap; V37 AmigaOS retains the direct Copper flip path below. */
	aros_library = OpenLibrary("aros.library", 0);
	can_flip = aros_library == NULL;
	if (aros_library != NULL) {
		CloseLibrary(aros_library);
	}
	if (can_flip &&
		AvailMem(MEMF_PUBLIC) >=
			minimum_arena + AMIGA_SYSTEM_HEADROOM + (legacy_u32)AMIGA_PLANE_BYTES * AMIGA_DEPTH &&
		AvailMem(MEMF_PUBLIC | MEMF_LARGEST) >= minimum_arena) {
		InitBitMap(&second_bitmap, AMIGA_DEPTH, AMIGA_WIDTH, AMIGA_HEIGHT);
		for (plane = 0; plane < AMIGA_DEPTH; ++plane) {
			second_bitmap.Planes[plane] = AllocRaster(AMIGA_WIDTH, AMIGA_HEIGHT);
			if (second_bitmap.Planes[plane] == NULL) {
				break;
			}
			memset(second_bitmap.Planes[plane], 0, AMIGA_PLANE_BYTES);
		}
		if (plane == AMIGA_DEPTH && AvailMem(MEMF_PUBLIC | MEMF_LARGEST) >= minimum_arena) {
			buffer_count = 2;
		} else {
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
	palette_apply(0);
	if (second_bitmap.Planes[0] == NULL) {
		/* With little free RAM, conversion writes the bitmap and can visibly
		 * tear. Start at vertical blank to make the best use of that interval. */
		WaitTOF();
		WaitBlit();
		palette_convert(display_bitmap);
		last_present = amiga_clock_ticks();
		presenting = 0;
		return;
	}
	target = visible_bitmap == &original_bitmap ? &second_bitmap : &original_bitmap;
	WaitBlit();
	palette_convert(target);
	WaitTOF();
	/* Keep Intuition's bitmap object: AROS stores driver metadata after it.
	 * Replacing RasInfo->BitMap with an InitBitMap object breaks later color
	 * updates. Only the displayed plane addresses need to change. */
	memcpy(display_bitmap->Planes, target->Planes, sizeof(target->Planes));
	ScrollVPort(&screen->ViewPort);
	/* Let the Copper latch the new pointers before the old page is writable. */
	WaitTOF();
	visible_bitmap = target;
	last_present = amiga_clock_ticks();
	presenting = 0;
}

void amiga_video_begin_frame(void)
{
	drawing_frame = 1;
}

legacy_u8 amiga_video_set_auto_refresh(legacy_u8 enabled)
{
	legacy_u8 previous = auto_refresh;
	auto_refresh = enabled != 0;
	return previous;
}

void amiga_video_refresh(void)
{
	/* Legacy menus draw directly into VGA memory between input polls. */
	if (screen != NULL && auto_refresh && !drawing_frame &&
		amiga_clock_ticks() - last_present >= 4U) {
		amiga_video_present();
	}
}

void dos_video_set_palette(legacy_u16 first, legacy_u16 count, legacy_u8 *rgb)
{
	amiga_palette_update(&palette, first, count, rgb);
	if (race_palette_active) {
		amiga_palette_update(&dashboard_palette, first, count, rgb);
		amiga_palette_update(&replay_palette, first, count, rgb);
	}
	if (screen != NULL) {
		palette_apply(1);
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

legacy_s16 video_get_status(void)
{
	return dos_video_get_status();
}
legacy_u8 dos_video_enable_planar_pages(void)
{
	return 0;
}
void dos_video_set_write_planes(legacy_u8 mask)
{
	(void)mask;
}
void dos_video_set_read_plane(legacy_u8 plane)
{
	(void)plane;
}
void dos_video_show_page(legacy_u16 address)
{
	(void)address;
	amiga_video_present();
}
void dos_video_set_mode4(void)
{
	dos_video_set_mode_13h();
}
void dos_video_set_mode7(void)
{
	amiga_video_shutdown();
}

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
		palette_remove_copper();
		memcpy(display_bitmap->Planes, original_bitmap.Planes, sizeof(original_bitmap.Planes));
		ScrollVPort(&screen->ViewPort);
		WaitTOF();
		WaitTOF();
		CloseScreen(screen);
		screen = NULL;
		display_bitmap = NULL;
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
