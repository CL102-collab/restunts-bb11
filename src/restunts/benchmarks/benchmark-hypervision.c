/* Reuse the production replay setup and state-preservation checks. */
#define stuntsmain benchmark_replay_test_main
#include "../c/video/video_framebuffer.h"
#include "../tests/test-render-replay.c"
#undef stuntsmain
#include "../c/hires/render_workers.h"
#include "../c/renderer/gpu_scene.h"
#include "../c/startup_internal.h"
#include "../c/extended/extended_file.h"
#include "../platform/sdl3/sdl3.h"
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#endif

#define BENCH_DEFAULT_FRAMES 120U
#define BENCH_WARMUP_FRAMES 8U
#define BENCH_LAST_TICK 160U
#define BENCH_TICK_INTERVAL 80U
#define BENCH_MILLISECONDS_PER_SECOND 1000.0
#define BENCH_MICROSECONDS_PER_MILLISECOND 1000.0
#define BENCH_WINDOWS_TICKS_PER_MILLISECOND 10000.0
#define BENCH_PERCENTILE_95 95U
#define BENCH_PERCENTILE_100 100U
#define BENCH_ARGUMENTS_REQUIRED 2
#define BENCH_ARGUMENTS_WITH_COUNT 3
#define BENCH_REPLAY_ARGUMENT 1
#define BENCH_COUNT_ARGUMENT 2
#define BENCH_COUNT_BASE 10
#define BENCH_CAMERA_ELEVATION 64
#define BENCH_CAMERA_AZIMUTH 128
#define BENCH_CAMERA_DISTANCE 450
#define BENCH_PALETTE_CHANNELS 3U
#define BENCH_PALETTE_CHANNEL_MAX 63U
#define BENCH_RED_SHIFT 16U
#define BENCH_GREEN_SHIFT 8U
#define BENCH_OPAQUE_ALPHA 0xFF000000U
#define BENCH_SCREEN_SEGMENT 0xA000U
#define BENCH_HASH_OFFSET 2166136261U
#define BENCH_HASH_PRIME 16777619U
#define BENCH_TRACK_FRAME_RATE 20U
#define BENCH_TRACK_CAR "COUN"
#define BENCH_RENDERER_PREFIX "--renderer:"
#define BENCH_DEVICE_PREFIX "--renderer-device:"

struct BENCHMARK_CPU_TIME {
#ifdef _WIN32
	FILETIME kernel, user;
#else
	struct rusage usage;
#endif
};

static legacy_s32 benchmark_compose;
static legacy_s32 benchmark_present;
static legacy_s32 benchmark_capture;
static legacy_s32 benchmark_classic;
static legacy_s32 benchmark_start_only;
static legacy_s32 benchmark_all_views;
static legacy_u8 benchmark_multi_core = STARTUP_MULTI_CORE_ON;
static legacy_u8 benchmark_workers_specified;
static const legacy_s8 *benchmark_track;
static legacy_s16 benchmark_startup_argc;
static legacy_s8 **benchmark_startup_arguments;
static legacy_u32 benchmark_palette[LEGACY_U8_MAX + 1U];

static void benchmark_free_arguments(void)
{
	free(benchmark_startup_arguments);
	benchmark_startup_arguments = NULL;
}

static void benchmark_parse_options(legacy_s16 *argc, legacy_s8 *argv[])
{
	/* Keep GPU options for production startup validation while removing them
	 * from the benchmark's positional arguments. */
	benchmark_startup_argc = *argc;
	benchmark_startup_arguments = malloc(((size_t)*argc + 1U) * sizeof(*argv));
	if (benchmark_startup_arguments == NULL || atexit(benchmark_free_arguments) != 0) {
		fatal_error("Cannot preserve benchmark startup arguments");
		return;
	}
	memcpy(benchmark_startup_arguments, argv, ((size_t)*argc + 1U) * sizeof(*argv));
	legacy_s16 output = 1;
	for (legacy_s16 input = 1; input < *argc; input++) {
		if (strcmp((const legacy_char *)argv[input], "--compose") == 0) {
			benchmark_compose = 1;
		} else if (strcmp((const legacy_char *)argv[input], "--present") == 0) {
			benchmark_present = 1;
		} else if (strcmp((const legacy_char *)argv[input], "--capture") == 0) {
			benchmark_present = benchmark_capture = 1;
		} else if (strcmp((const legacy_char *)argv[input], "--classic") == 0) {
			benchmark_classic = 1;
		} else if (SDL_strcasecmp((const legacy_char *)argv[input], "--multi-core:on") == 0) {
			benchmark_multi_core = 1;
			benchmark_workers_specified = 1;
		} else if (SDL_strcasecmp((const legacy_char *)argv[input], "--multi-core:off") == 0) {
			benchmark_multi_core = 0;
			benchmark_workers_specified = 1;
		} else if (SDL_strncasecmp((const legacy_char *)argv[input], BENCH_RENDERER_PREFIX,
								   sizeof(BENCH_RENDERER_PREFIX) - 1U) == 0 ||
				   SDL_strncasecmp((const legacy_char *)argv[input], BENCH_DEVICE_PREFIX,
								   sizeof(BENCH_DEVICE_PREFIX) - 1U) == 0) {
			/* Retained for production startup validation and initialization. */
		} else if (strcmp((const legacy_char *)argv[input], "--all-views") == 0) {
			benchmark_all_views = 1;
		} else if (strcmp((const legacy_char *)argv[input], "--start-only") == 0) {
			benchmark_start_only = 1;
		} else if (strcmp((const legacy_char *)argv[input], "--track") == 0 && input + 1 < *argc) {
			benchmark_track = argv[++input];
		} else {
			argv[output++] = argv[input];
		}
	}
	*argc = output;
	argv[output] = NULL;
}

static void benchmark_load_palette(void)
{
	legacy_s8 *resource = file_load_shape2d_fatal("sdmain");
	const legacy_u8 *palette =
		(const legacy_u8 *)locate_shape_fatal(resource, "!pal") + SHAPE2D_HEADER_SIZE;
	for (legacy_u32 index = 0; index <= LEGACY_U8_MAX; index++) {
		legacy_u32 channels[BENCH_PALETTE_CHANNELS];
		for (legacy_u32 channel = 0; channel < BENCH_PALETTE_CHANNELS; channel++) {
			channels[channel] =
				(palette[index * BENCH_PALETTE_CHANNELS + channel] & BENCH_PALETTE_CHANNEL_MAX) *
				LEGACY_U8_MAX / BENCH_PALETTE_CHANNEL_MAX;
		}
		benchmark_palette[index] = BENCH_OPAQUE_ALPHA | (channels[0] << BENCH_RED_SHIFT) |
								   (channels[1] << BENCH_GREEN_SHIFT) | channels[2];
	}
	mmgr_free(resource);
}

static legacy_s32 benchmark_width(void)
{
	return hires_enabled() ? hires_render_width() : VIDEO_FRAMEBUFFER_WIDTH;
}

static legacy_s32 benchmark_height(void)
{
	return hires_enabled() ? hires_render_height() : VIDEO_FRAMEBUFFER_HEIGHT;
}

static void benchmark_render_frame(legacy_u32 *composed)
{
	full_redraw_frames_remaining = 1;
	render_and_check(NULL);
	if (composed != NULL) {
		video_copy_framebuffer_argb(dos_memory_make_pointer(BENCH_SCREEN_SEGMENT, 0),
									benchmark_palette, composed,
									benchmark_width() * sizeof(*composed));
	}
}

static void benchmark_usage(const legacy_s8 *program)
{
	fprintf(stderr,
			"Usage: %s [--data-dir DIR] [--compose] [--start-only] REPLAY [FRAMES]\n"
			"       %s [--data-dir DIR] [--compose] --track TRACK [FRAMES]\n"
			"  REPLAY is a replay name without .rpl; FRAMES is a positive integer\n"
			"  (default %u). Measures full-track geometry at 320x200, 640x400\n"
			"  and 1280x800 in cockpit and external views at ticks 0, 80, 160.\n"
			"  --renderer:software|" GRAPHICS_RENDERER_NATIVE_VALUE
			" selects the scene renderer (default software).\n"
			"  --renderer-device:N selects a listed GPU device; use the native GPU renderer.\n"
			"  --multi-core:off disables parallel rendering (on by default).\n"
			"  Headless GPU benchmarking disables CPU rendering workers.\n"
			"  --compose also measures final ARGB framebuffer composition.\n"
			"  --present opens a window and measures native GPU presentation.\n"
			"  Set RESTUNTS_VSYNC=0 for uncapped hardware comparisons.\n"
			"  --capture also saves one SHOTxxxx.PNG after each benchmark scene.\n"
			"  --classic measures the original 320x200 scene rather than HyperVision.\n"
			"  --start-only measures full resolution cockpit view at tick 0.\n"
			"  --track measures that view on a .cstrk track with the Countach.\n"
			"  --all-views includes both cameras and all resolutions with --track.\n",
			program, program, BENCH_DEFAULT_FRAMES);
}

static legacy_s32 benchmark_parse_count(const legacy_s8 *argument, legacy_u32 *count)
{
	if (argument[0] < '0' || argument[0] > '9') {
		return 0;
	}
	legacy_char *end;
	errno = 0;
	legacy_u64 value = strtoull((const legacy_char *)argument, &end, BENCH_COUNT_BASE);
	if (errno == ERANGE || *end != 0 || value == 0 || value > LEGACY_U32_MAX ||
		value > (size_t)-1 / sizeof(legacy_f64)) {
		return 0;
	}
	*count = (legacy_u32)value;
	return 1;
}

static legacy_int benchmark_compare_times(const void *first, const void *second)
{
	legacy_f64 a = *(const legacy_f64 *)first;
	legacy_f64 b = *(const legacy_f64 *)second;
	return (a > b) - (a < b);
}

static legacy_s32 benchmark_read_cpu_time(struct BENCHMARK_CPU_TIME *time)
{
#ifdef _WIN32
	FILETIME created, exited;
	if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &time->kernel, &time->user)) {
		fprintf(stderr, "Cannot read process CPU time (Windows error %" LEGACY_PRIu32 ")\n",
				(legacy_u32)GetLastError());
		return 0;
	}
#else
	if (getrusage(RUSAGE_SELF, &time->usage) != 0) {
		perror("Cannot read process CPU time");
		return 0;
	}
#endif
	return 1;
}

#ifdef _WIN32
static legacy_u64 benchmark_filetime_ticks(const FILETIME *time)
{
	return ((legacy_u64)time->dwHighDateTime << LEGACY_DWORD_BITS) | time->dwLowDateTime;
}
#endif

static legacy_f64 benchmark_cpu_milliseconds(const struct BENCHMARK_CPU_TIME *start,
											 const struct BENCHMARK_CPU_TIME *end)
{
#ifdef _WIN32
	return ((benchmark_filetime_ticks(&end->user) - benchmark_filetime_ticks(&start->user)) +
			(benchmark_filetime_ticks(&end->kernel) - benchmark_filetime_ticks(&start->kernel))) /
		   BENCH_WINDOWS_TICKS_PER_MILLISECOND;
#else
	return (end->usage.ru_utime.tv_sec - start->usage.ru_utime.tv_sec + end->usage.ru_stime.tv_sec -
			start->usage.ru_stime.tv_sec) *
			   BENCH_MILLISECONDS_PER_SECOND +
		   (end->usage.ru_utime.tv_usec - start->usage.ru_utime.tv_usec +
			end->usage.ru_stime.tv_usec - start->usage.ru_stime.tv_usec) /
			   BENCH_MICROSECONDS_PER_MILLISECOND;
#endif
}

static const legacy_char *benchmark_backend(void)
{
	if (graphics_renderer() == GRAPHICS_RENDERER_SOFTWARE) {
		return "software";
	}
	return gpu_scene_active()			  ? "graphics"
		   : gpu_scene_software_preview() ? "software-preview"
										  : "compute-readback";
}

static legacy_u32 benchmark_hash(const legacy_u32 *composed)
{
	legacy_s32 width;
	legacy_s32 height;
	const legacy_u8 *pixels =
		video_framebuffer(dos_memory_make_pointer(BENCH_SCREEN_SEGMENT, 0), &width, &height);
	size_t size = (size_t)width * height;
	if (composed != NULL) {
		pixels = (const legacy_u8 *)composed;
		size *= sizeof(*composed);
	}
	legacy_u32 hash = BENCH_HASH_OFFSET;
	for (size_t index = 0; index < size; index++) {
		hash = (hash ^ pixels[index]) * BENCH_HASH_PRIME;
	}
	return hash;
}

static void benchmark_check_parallel_pixels(legacy_u32 *composed)
{
	if (!benchmark_multi_core) {
		return;
	}
	hires_render_workers_set_enabled(0);
	benchmark_render_frame(composed);
	legacy_s32 width;
	legacy_s32 height;
	const legacy_u8 *indexed =
		video_framebuffer(dos_memory_make_pointer(BENCH_SCREEN_SEGMENT, 0), &width, &height);
	size_t size =
		(size_t)width * height * (composed != NULL ? sizeof(*composed) : sizeof(*indexed));
	legacy_u8 *expected = malloc(size);
	assert(expected != NULL);
	memcpy(expected, composed != NULL ? (const void *)composed : (const void *)indexed, size);
	hires_render_workers_set_enabled(1);
	benchmark_render_frame(composed);
	indexed = video_framebuffer(dos_memory_make_pointer(BENCH_SCREEN_SEGMENT, 0), &width, &height);
	assert(memcmp(expected, composed != NULL ? (const void *)composed : (const void *)indexed,
				  size) == 0);
	free(expected);
}

static void benchmark_initialize_track(void)
{
	assert(extended_file_load_track(benchmark_track) == 0);
	memset(&gameconfig, 0, sizeof(gameconfig));
	memcpy(gameconfig.game_playercarid, BENCH_TRACK_CAR, REPLAY_CAR_ID_SIZE);
	gameconfig.game_opponentcarid[0] = -1;
	gameconfig.game_framespersec = BENCH_TRACK_FRAME_RATE;
	initialize_loaded_race();
}

static legacy_s32 benchmark_frames(legacy_u16 tick, legacy_u32 count)
{
	legacy_f64 *times = malloc((size_t)count * sizeof(*times));
	legacy_u32 *composed =
		benchmark_compose
			? malloc((size_t)benchmark_width() * benchmark_height() * sizeof(*composed))
			: NULL;
	if (times == NULL || (benchmark_compose && composed == NULL)) {
		free(composed);
		free(times);
		fputs("Cannot allocate benchmark frame samples\n", stderr);
		return 0;
	}
	benchmark_check_parallel_pixels(composed);
	for (legacy_u32 frame = 0; frame < BENCH_WARMUP_FRAMES; frame++) {
		benchmark_render_frame(composed);
	}
	struct BENCHMARK_CPU_TIME cpu_start, cpu_end;
	if (!benchmark_read_cpu_time(&cpu_start)) {
		free(composed);
		free(times);
		return 0;
	}
	legacy_u64 frequency = SDL_GetPerformanceFrequency();
	legacy_u64 started = SDL_GetPerformanceCounter();
	for (legacy_u32 frame = 0; frame < count; frame++) {
		legacy_u64 before = SDL_GetPerformanceCounter();
		benchmark_render_frame(composed);
		times[frame] =
			(SDL_GetPerformanceCounter() - before) * BENCH_MILLISECONDS_PER_SECOND / frequency;
	}
	legacy_f64 total =
		(SDL_GetPerformanceCounter() - started) * BENCH_MILLISECONDS_PER_SECOND / frequency;
	if (!benchmark_read_cpu_time(&cpu_end)) {
		free(composed);
		free(times);
		return 0;
	}
	legacy_f64 cpu = benchmark_cpu_milliseconds(&cpu_start, &cpu_end) / count;
	qsort(times, count, sizeof(*times), benchmark_compare_times);
	legacy_u32 percentile =
		(legacy_u32)((legacy_u64)count * BENCH_PERCENTILE_95 / BENCH_PERCENTILE_100);
	printf("tick=%u camera=%d resolution=%dx%d renderer=%s backend=%s workers=%d n=%u "
		   "cpu=%.3f mean=%.3f p50=%.3f p95=%.3f ms hash=%08x parity=%s\n",
		   tick, cameramode, benchmark_width(), benchmark_height(),
		   graphics_renderer() == GRAPHICS_RENDERER_SOFTWARE ? "software"
															 : GRAPHICS_RENDERER_NATIVE_VALUE,
		   benchmark_backend(), hires_render_workers_count(), count, cpu, total / count,
		   times[count / 2U], times[percentile], benchmark_hash(composed),
		   benchmark_multi_core ? "matched" : "serial");
	fflush(stdout);
	if (benchmark_capture) {
		sdl3_video_screenshot();
	}
	free(composed);
	free(times);
	return 1;
}

legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[])
{
	benchmark_parse_options(&argc, argv);
	/* Keep the normal positional parser for track-only startup measurements. */
	if (benchmark_track != NULL) {
		for (legacy_s16 index = argc; index > BENCH_REPLAY_ARGUMENT; index--) {
			argv[index] = argv[index - 1];
		}
		argv[BENCH_REPLAY_ARGUMENT] = (legacy_s8 *)benchmark_track;
		argc++;
		argv[argc] = NULL;
		benchmark_start_only = !benchmark_all_views;
	}
	legacy_u32 count = BENCH_DEFAULT_FRAMES;
	if (argc == BENCH_ARGUMENTS_REQUIRED &&
		SDL_strcmp((const legacy_char *)argv[BENCH_REPLAY_ARGUMENT], "--help") == 0) {
		benchmark_usage(argv[0]);
		return EXIT_SUCCESS;
	}
	if ((argc != BENCH_ARGUMENTS_REQUIRED && argc != BENCH_ARGUMENTS_WITH_COUNT) ||
		argv[BENCH_REPLAY_ARGUMENT][0] == 0 ||
		(argc == BENCH_ARGUMENTS_WITH_COUNT &&
		 !benchmark_parse_count(argv[BENCH_COUNT_ARGUMENT], &count))) {
		benchmark_usage(argv[0]);
		return EXIT_FAILURE;
	}
	if (benchmark_present) {
		sdl3_batch_mode = 0;
	}
	init_main(benchmark_startup_argc, benchmark_startup_arguments);
	if (graphics_renderer() != GRAPHICS_RENDERER_SOFTWARE) {
		/* Native presentation can parallelize geometry and artwork while
		 * triangle coverage and depth testing stay on the GPU. */
		if (!benchmark_present || !benchmark_workers_specified) {
			benchmark_multi_core = benchmark_present;
		}
	}
	hires_render_workers_set_enabled(benchmark_multi_core);
	init_div0();
	init_row_tables();
	mainresptr = file_load_resfile("main");
	fontdefptr = file_load_resource(0, "fontdef.fnt");
	fontnptr = file_load_resource(0, "fontn.fnt");
	font_set_fontdef();
	init_polyinfo();
	init_trackdata();
	reset_race_loop_state();
	init_kevinrandom("kevin");
	if (benchmark_track != NULL) {
		benchmark_initialize_track();
	} else {
		initialize_replay(argv[BENCH_REPLAY_ARGUMENT]);
	}
	if (benchmark_compose) {
		benchmark_load_palette();
	}
	/* Keep transient HUD coverage identical across fast and slow runs. */
	visual_clock_ticks = dos_timer_get_realtime_counter();
	visual_clock_frozen = 1;
	if ((renderer_active != 0) == (benchmark_classic != 0)) {
		assert(handle_ingame_kb_shortcuts(KEY_F12) != 0);
	}
	frame_hires_adaptive_set_preset(&frame_hires_adaptive, FRAME_HIRES_ADAPTIVE_PRESET_FULL);
	custom_camera.elevation_angle = BENCH_CAMERA_ELEVATION;
	custom_camera.azimuth_angle = BENCH_CAMERA_AZIMUTH;
	custom_camera.distance = BENCH_CAMERA_DISTANCE;
	static const legacy_s8 cameras[] = {CAMERA_MODE_COCKPIT, CAMERA_MODE_CUSTOM};
	static const legacy_s32 scales[] = {HIRES_MINIMUM_SCALE, HIRES_MEDIUM_SCALE, HIRES_SCALE};
	legacy_u16 limit = gameconfig.game_recordedframes < BENCH_LAST_TICK
						   ? gameconfig.game_recordedframes
						   : BENCH_LAST_TICK;
	if (benchmark_start_only) {
		limit = 0;
	}
	printf("%s renderer benchmark: process CPU and elapsed milliseconds/frame.\n",
		   benchmark_classic ? "Classic" : "HyperVision full-track");
	puts("Includes render/state checks, shadows, skybox and framebuffer copies; "
		 "excludes pacing and physics.");
	printf("Standalone ARGB composition: %s.\n", benchmark_compose ? "included" : "excluded");
	printf("Window presentation: %s.\n", benchmark_present ? "included" : "excluded (headless)");
	if (benchmark_present) {
		puts("Window presentation includes production ARGB composition, upload and display.");
	}
	puts("Parity and hash cover the CPU framebuffer; native GPU scene pixels are separate.");
	for (legacy_u16 tick = 0; tick <= limit; tick++) {
		if (tick % BENCH_TICK_INTERVAL == 0 || tick == limit) {
			legacy_u32 camera_count = benchmark_start_only ? 1U : SDL_arraysize(cameras);
			for (legacy_u32 camera = 0; camera < camera_count; camera++) {
				cameramode = cameras[camera];
				legacy_u32 first_scale =
					!benchmark_classic && benchmark_start_only ? SDL_arraysize(scales) - 1U : 0U;
				legacy_u32 last_scale = benchmark_classic ? 1U : SDL_arraysize(scales);
				for (legacy_u32 scale = first_scale; scale < last_scale; scale++) {
					hires_set_render_scale(scales[scale]);
					if (!benchmark_frames(tick, count)) {
						call_exitlist();
						return EXIT_FAILURE;
					}
				}
			}
		}
		if (tick < limit) {
			update_gamestate();
		}
	}
	call_exitlist();
	return EXIT_SUCCESS;
}
