/* Reuse the production replay setup and state-preservation checks. */
#define stuntsmain benchmark_replay_test_main
#include "../c/video/video_framebuffer.h"
#include "../tests/test-render-replay.c"
#undef stuntsmain
#include "../c/hires/render_workers.h"
#include "../c/startup_internal.h"
#include "../c/extended/extended_file.h"
#include "../c/extended/extended_replay_runtime.h"
#include "../c/extended/extended_frame.h"
#include "../c/extended/extended_track_preview_hires.h"
#include "../c/menu/menu_internal.h"
#include "../platform/sdl3/sdl3.h"
#include <errno.h>
#include <sys/resource.h>

#define BENCH_DEFAULT_FRAMES 120U
#define BENCH_WARMUP_FRAMES 8U
#define BENCH_LAST_TICK 160U
#define BENCH_TICK_INTERVAL 80U
#define BENCH_MILLISECONDS_PER_SECOND 1000.0
#define BENCH_MICROSECONDS_PER_MILLISECOND 1000.0
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
#define BENCH_PRESENTATION_RATE 60U
#define BENCH_PRESENTATIONS_PER_TICK (BENCH_PRESENTATION_RATE / BENCH_TRACK_FRAME_RATE)
#define BENCH_DRIVE_MAX_SECONDS (LEGACY_S16_MAX / BENCH_TRACK_FRAME_RATE)
#define BENCH_PREVIEW_HEADINGS 4U
#define BENCH_PREVIEW_SPIN_SECONDS 40UL
#define BENCH_PREVIEW_SPIN_TICKS (BENCH_PREVIEW_SPIN_SECONDS * DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define BENCH_PREVIEW_SCREEN_WIDTH (HIRES_WIDTH / HIRES_SCALE)
#define BENCH_PREVIEW_SCREEN_HEIGHT (HIRES_HEIGHT / HIRES_SCALE)
#define BENCH_PREVIEW_GUARD_COLOR 13U

static legacy_s32 benchmark_compose;
static legacy_s32 benchmark_preview;
static legacy_s32 benchmark_preview_full_detail;
static legacy_s32 benchmark_start_only;
static legacy_s32 benchmark_all_views;
static legacy_s32 benchmark_f3;
static legacy_u8 benchmark_multi_core = STARTUP_MULTI_CORE_ON;
static const legacy_s8 *benchmark_car;
static const legacy_s8 *benchmark_track;
static const legacy_s8 *benchmark_drive_argument;
static legacy_u32 benchmark_drive_seconds;
static legacy_u32 benchmark_palette[LEGACY_U8_MAX + 1U];

static void benchmark_parse_options(legacy_s16 *argc, legacy_s8 *argv[])
{
	legacy_s16 output = 1;
	for (legacy_s16 input = 1; input < *argc; input++) {
		if (strcmp((const legacy_char *)argv[input], "--preview") == 0) {
			benchmark_preview = 1;
		} else if (strcmp((const legacy_char *)argv[input], "--preview-full-detail") == 0) {
			benchmark_preview = 1;
			benchmark_preview_full_detail = 1;
		} else if (strcmp((const legacy_char *)argv[input], "--compose") == 0) {
			benchmark_compose = 1;
		} else if (SDL_strcasecmp((const legacy_char *)argv[input], "--multi-core:on") == 0) {
			benchmark_multi_core = 1;
		} else if (SDL_strcasecmp((const legacy_char *)argv[input], "--multi-core:off") == 0) {
			benchmark_multi_core = 0;
		} else if (strcmp((const legacy_char *)argv[input], "--all-views") == 0) {
			benchmark_all_views = 1;
		} else if (strcmp((const legacy_char *)argv[input], "--start-only") == 0) {
			benchmark_start_only = 1;
		} else if (strcmp((const legacy_char *)argv[input], "--f3") == 0) {
			benchmark_f3 = 1;
		} else if (strcmp((const legacy_char *)argv[input], "--car") == 0 && input + 1 < *argc) {
			benchmark_car = argv[++input];
		} else if (strcmp((const legacy_char *)argv[input], "--drive-seconds") == 0 &&
				   input + 1 < *argc) {
			benchmark_drive_argument = argv[++input];
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

static void benchmark_render_frame(legacy_u32 *composed, const struct GAMESTATE *snapshot)
{
	if (benchmark_preview) {
		struct GAMESTATE saved = state;
		struct GAMEINFO saved_config = gameconfig;
		legacy_s8 seed_before[GAMESTATE_RANDOM_SEED_SIZE], seed_after[GAMESTATE_RANDOM_SEED_SIZE];
		get_kevinrandom_seed(seed_before);
		sprite_select_screen_compat();
		draw_track_preview();
		get_kevinrandom_seed(seed_after);
		assert(memcmp(&saved, &state, sizeof(state)) == 0);
		assert(memcmp(&saved_config, &gameconfig, sizeof(gameconfig)) == 0);
		assert(memcmp(seed_before, seed_after, sizeof(seed_before)) == 0);
	} else {
		full_redraw_frames_remaining = 1;
		render_and_check(snapshot);
	}
	if (composed != NULL) {
		video_copy_framebuffer_argb(dos_memory_make_pointer(BENCH_SCREEN_SEGMENT, 0),
									benchmark_palette, composed,
									hires_render_width() * sizeof(*composed));
	}
}

static void benchmark_usage(const legacy_s8 *program)
{
	fprintf(stderr,
			"Usage: %s [--data-dir DIR] [--compose] [--start-only] REPLAY [FRAMES]\n"
			"       %s [--data-dir DIR] [--compose] --track TRACK [FRAMES]\n"
			"       %s [--data-dir DIR] [--compose] --preview [--preview-full-detail]\n"
			"          --track TRACK [FRAMES]\n"
			"  REPLAY is a replay name without .rpl; FRAMES is a positive integer\n"
			"  (default %u). Measures full-track geometry at 320x200, 640x400\n"
			"  and 1280x800 in cockpit and external views at ticks 0, 80, 160.\n"
			"  --multi-core:off disables parallel rendering (on by default).\n"
			"  --compose also measures final ARGB framebuffer composition.\n"
			"  --start-only measures full resolution cockpit view at tick 0.\n"
			"  --track measures that view on a .cstrk track with the Countach.\n"
			"  --car ID overrides the player car with a four-character resource ID.\n"
			"  --f3 measures only the external camera at its initial F3 position;\n"
			"    combine with --start-only for full resolution at tick 0.\n"
			"  --drive-seconds N with --track drives straight for N simulated seconds\n"
			"    at 20 Hz, rendering 60 interpolated frames/second at Full resolution.\n"
			"    Reports throughput including physics; excludes display and pacing.\n"
			"  --all-views includes both cameras and all resolutions with --track.\n"
			"  --preview measures the actual rotating track-menu preview at four fixed\n"
			"  headings; --all-views includes all three resolutions.\n"
			"  --preview-full-detail disables preview detail reductions for comparison.\n",
			program, program, program, BENCH_DEFAULT_FRAMES);
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

static legacy_f64 benchmark_cpu_milliseconds(const struct rusage *start, const struct rusage *end)
{
	return (end->ru_utime.tv_sec - start->ru_utime.tv_sec + end->ru_stime.tv_sec -
			start->ru_stime.tv_sec) *
			   BENCH_MILLISECONDS_PER_SECOND +
		   (end->ru_utime.tv_usec - start->ru_utime.tv_usec + end->ru_stime.tv_usec -
			start->ru_stime.tv_usec) /
			   BENCH_MICROSECONDS_PER_MILLISECOND;
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
	benchmark_render_frame(composed, NULL);
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
	benchmark_render_frame(composed, NULL);
	indexed = video_framebuffer(dos_memory_make_pointer(BENCH_SCREEN_SEGMENT, 0), &width, &height);
	assert(memcmp(expected, composed != NULL ? (const void *)composed : (const void *)indexed,
				  size) == 0);
	free(expected);
}

static void benchmark_override_car(void)
{
	if (benchmark_car != NULL) {
		memcpy(gameconfig.game_playercarid, benchmark_car, REPLAY_CAR_ID_SIZE);
	}
}

static void benchmark_initialize_replay(const legacy_s8 *name)
{
	if (benchmark_car == NULL) {
		initialize_replay(name);
		return;
	}
	assert(file_load_replay("", name) == 0);
	benchmark_override_car();
	initialize_loaded_race();
}

static void benchmark_initialize_track(void)
{
	assert(extended_file_load_track(benchmark_track) == 0);
	memset(&gameconfig, 0, sizeof(gameconfig));
	memcpy(gameconfig.game_playercarid, BENCH_TRACK_CAR, REPLAY_CAR_ID_SIZE);
	benchmark_override_car();
	gameconfig.game_opponentcarid[0] = -1;
	gameconfig.game_framespersec = BENCH_TRACK_FRAME_RATE;
	if (benchmark_drive_seconds != 0U) {
		gameconfig.game_playertransmission = TRANSMISSION_AUTOMATIC;
	}
	initialize_loaded_race();
}

static legacy_s32 benchmark_frames(legacy_u16 tick, legacy_u32 count)
{
	legacy_f64 *times = malloc((size_t)count * sizeof(*times));
	legacy_u32 *composed =
		benchmark_compose
			? malloc((size_t)hires_render_width() * hires_render_height() * sizeof(*composed))
			: NULL;
	if (times == NULL || (benchmark_compose && composed == NULL)) {
		free(composed);
		free(times);
		fputs("Cannot allocate benchmark frame samples\n", stderr);
		return 0;
	}
	benchmark_check_parallel_pixels(composed);
	for (legacy_u32 frame = 0; frame < BENCH_WARMUP_FRAMES; frame++) {
		benchmark_render_frame(composed, NULL);
	}
	struct rusage cpu_start, cpu_end;
	if (getrusage(RUSAGE_SELF, &cpu_start) != 0) {
		perror("Cannot read process CPU time");
		free(composed);
		free(times);
		return 0;
	}
	legacy_u64 frequency = SDL_GetPerformanceFrequency();
	legacy_u64 started = SDL_GetPerformanceCounter();
	for (legacy_u32 frame = 0; frame < count; frame++) {
		legacy_u64 before = SDL_GetPerformanceCounter();
		benchmark_render_frame(composed, NULL);
		times[frame] =
			(SDL_GetPerformanceCounter() - before) * BENCH_MILLISECONDS_PER_SECOND / frequency;
	}
	legacy_f64 total =
		(SDL_GetPerformanceCounter() - started) * BENCH_MILLISECONDS_PER_SECOND / frequency;
	if (getrusage(RUSAGE_SELF, &cpu_end) != 0) {
		perror("Cannot read process CPU time");
		free(composed);
		free(times);
		return 0;
	}
	legacy_f64 cpu = benchmark_cpu_milliseconds(&cpu_start, &cpu_end) / count;
	qsort(times, count, sizeof(*times), benchmark_compare_times);
	legacy_u32 percentile =
		(legacy_u32)((legacy_u64)count * BENCH_PERCENTILE_95 / BENCH_PERCENTILE_100);
	printf("tick=%u camera=%d resolution=%dx%d workers=%d n=%u "
		   "cpu=%.3f mean=%.3f p50=%.3f p95=%.3f ms hash=%08x parity=%s\n",
		   tick, cameramode, hires_render_width(), hires_render_height(),
		   hires_render_workers_count(), count, cpu, total / count, times[count / 2U],
		   times[percentile], benchmark_hash(composed),
		   benchmark_multi_core ? "matched" : "serial");
	if (benchmark_preview) {
		printf("preview_tick=%u primitives=%u detail=%s\n", visual_clock_ticks,
			   extended_track_preview_hires_primitive_count(),
			   benchmark_preview_full_detail ? "full" : "default");
	}
	fflush(stdout);
	free(composed);
	free(times);
	return 1;
}

static legacy_u32 benchmark_state_hash(void)
{
	legacy_u8 serialized[GAMESTATE_SERIALIZED_SIZE];
	legacy_u16 size = gamestate_serialize(serialized, &state);
	legacy_u32 hash = BENCH_HASH_OFFSET;
	for (legacy_u16 index = 0; index < size; index++) {
		hash = (hash ^ serialized[index]) * BENCH_HASH_PRIME;
	}
	return hash;
}

static void benchmark_drive_check(legacy_u32 *composed, legacy_u32 tick)
{
	if (benchmark_multi_core) {
		benchmark_check_parallel_pixels(composed);
	} else {
		benchmark_render_frame(composed, NULL);
	}
	printf("drive_tick=%u frame_hash=%08x state_hash=%08x parity=%s\n", tick,
		   benchmark_hash(composed), benchmark_state_hash(),
		   benchmark_multi_core ? "matched" : "serial");
	fflush(stdout);
}

static legacy_s32 benchmark_drive(void)
{
	hires_set_render_scale(HIRES_SCALE);
	legacy_u32 ticks = benchmark_drive_seconds * BENCH_TRACK_FRAME_RATE;
	legacy_u32 count = benchmark_drive_seconds * BENCH_PRESENTATION_RATE;
	legacy_u32 *composed =
		benchmark_compose
			? malloc((size_t)hires_render_width() * hires_render_height() * sizeof(*composed))
			: NULL;
	legacy_f64 *times = malloc((size_t)count * sizeof(*times));
	if (times == NULL || (benchmark_compose && composed == NULL)) {
		free(composed);
		free(times);
		fputs("Cannot allocate driving benchmark samples\n", stderr);
		return 0;
	}
	extended_replay_runtime_begin();
	for (legacy_u32 tick = 0; tick < ticks; tick++) {
		if (!extended_replay_record_input(INPUT_ACCELERATE_FLAG)) {
			free(composed);
			free(times);
			fputs("Cannot allocate driving benchmark input\n", stderr);
			return 0;
		}
	}
	extended_replay_set_input_frame(0U);
	for (legacy_u32 frame = 0; frame < BENCH_WARMUP_FRAMES; frame++) {
		benchmark_render_frame(composed, NULL);
	}
	benchmark_drive_check(composed, 0U);
	struct VECTORLONG start = state.playerstate.car_position;
	struct rusage cpu_start, cpu_end;
	if (getrusage(RUSAGE_SELF, &cpu_start) != 0) {
		perror("Cannot read process CPU time");
		free(composed);
		free(times);
		return 0;
	}
	legacy_u64 frequency = SDL_GetPerformanceFrequency();
	legacy_u64 started = SDL_GetPerformanceCounter();
	legacy_u64 diagnostic_ticks = 0U;
	legacy_f64 diagnostic_cpu = 0.0;
	for (legacy_u32 tick = 0; tick < ticks; tick++) {
		struct GAMESTATE previous = state;
		assert(extended_replay_advance_input());
		update_gamestate();
		for (legacy_u32 sample = 1; sample <= BENCH_PRESENTATIONS_PER_TICK; sample++) {
			legacy_u32 frame = tick * BENCH_PRESENTATIONS_PER_TICK + sample - 1U;
			visual_clock_ticks =
				(frame + 1U) * DOS_TIMER_REALTIME_TICKS_PER_SECOND / BENCH_PRESENTATION_RATE;
			legacy_u64 before = SDL_GetPerformanceCounter();
			struct GAMESTATE interpolated;
			frame_hires_interpolate_state(&interpolated, &state, &previous,
										  sample * FRAME_INTERPOLATION_ONE /
											  BENCH_PRESENTATIONS_PER_TICK);
			benchmark_render_frame(composed, &interpolated);
			times[frame] =
				(SDL_GetPerformanceCounter() - before) * BENCH_MILLISECONDS_PER_SECOND / frequency;
		}
		if (tick + 1U == ticks / 2U || tick + 1U == ticks) {
			struct rusage check_start, check_end;
			legacy_u64 check_started = SDL_GetPerformanceCounter();
			assert(getrusage(RUSAGE_SELF, &check_start) == 0);
			benchmark_drive_check(composed, tick + 1U);
			assert(getrusage(RUSAGE_SELF, &check_end) == 0);
			diagnostic_cpu += benchmark_cpu_milliseconds(&check_start, &check_end);
			diagnostic_ticks += SDL_GetPerformanceCounter() - check_started;
		}
	}
	legacy_f64 elapsed = (SDL_GetPerformanceCounter() - started - diagnostic_ticks) *
						 BENCH_MILLISECONDS_PER_SECOND / frequency;
	if (getrusage(RUSAGE_SELF, &cpu_end) != 0) {
		perror("Cannot read process CPU time");
		free(composed);
		free(times);
		return 0;
	}
	legacy_f64 cpu = benchmark_cpu_milliseconds(&cpu_start, &cpu_end) - diagnostic_cpu;
	qsort(times, count, sizeof(*times), benchmark_compare_times);
	legacy_u32 percentile =
		(legacy_u32)((legacy_u64)count * BENCH_PERCENTILE_95 / BENCH_PERCENTILE_100);
	struct VECTORLONG end = state.playerstate.car_position;
	assert(extended_replay_frame() == ticks);
	assert(extended_replay_input_frame() == ticks);
	assert(start.lx != end.lx || start.ly != end.ly || start.lz != end.lz);
	printf("drive_seconds=%u physics_hz=%u visual_hz=%u resolution=%dx%d workers=%d "
		   "ticks=%u n=%u elapsed_ms=%.3f cpu_ms=%.3f fps=%.3f cpu_fps=%.3f "
		   "mean=%.3f p50=%.3f p95=%.3f ms state_hash=%08x\n",
		   benchmark_drive_seconds, BENCH_TRACK_FRAME_RATE, BENCH_PRESENTATION_RATE,
		   hires_render_width(), hires_render_height(), hires_render_workers_count(), ticks, count,
		   elapsed, cpu, count * BENCH_MILLISECONDS_PER_SECOND / elapsed,
		   count * BENCH_MILLISECONDS_PER_SECOND / cpu, elapsed / count, times[count / 2U],
		   times[percentile], benchmark_state_hash());
	printf("drive_position_start=%d,%d,%d drive_position_end=%d,%d,%d "
		   "speed=%u crash=%d\n",
		   start.lx, start.ly, start.lz, end.lx, end.ly, end.lz, state.playerstate.car_actual_speed,
		   state.playerstate.car_crashBmpFlag);
	fflush(stdout);
	free(composed);
	free(times);
	return 1;
}

static legacy_s32 benchmark_preview_frames(legacy_u32 count)
{
	static const legacy_s32 scales[] = {HIRES_MINIMUM_SCALE, HIRES_MEDIUM_SCALE, HIRES_SCALE};
	extended_frame_preview_set_detail_enabled(!benchmark_preview_full_detail);
	cameramode = CAMERA_MODE_CUSTOM;
	for (legacy_u32 heading = 0; heading < BENCH_PREVIEW_HEADINGS; heading++) {
		visual_clock_ticks = heading * BENCH_PREVIEW_SPIN_TICKS / BENCH_PREVIEW_HEADINGS;
		legacy_u32 first_scale = benchmark_all_views ? 0U : SDL_arraysize(scales) - 1U;
		for (legacy_u32 scale = first_scale; scale < SDL_arraysize(scales); scale++) {
			hires_set_render_scale(scales[scale]);
			sprite_select_screen_compat();
			sprite_set_target_clip_bounds(0, BENCH_PREVIEW_SCREEN_WIDTH, 0,
										  BENCH_PREVIEW_SCREEN_HEIGHT);
			sprite_clear_target(BENCH_PREVIEW_GUARD_COLOR);
			if (!benchmark_frames((legacy_u16)visual_clock_ticks, count)) {
				extended_frame_preview_set_detail_enabled(1);
				return 0;
			}
		}
	}
	extended_frame_preview_set_detail_enabled(1);
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
	if (benchmark_drive_argument != NULL &&
		(!benchmark_parse_count(benchmark_drive_argument, &benchmark_drive_seconds) ||
		 benchmark_drive_seconds > BENCH_DRIVE_MAX_SECONDS || benchmark_track == NULL ||
		 benchmark_preview || benchmark_all_views || argc != BENCH_ARGUMENTS_REQUIRED)) {
		benchmark_usage(argv[0]);
		return EXIT_FAILURE;
	}
	if ((benchmark_preview && (benchmark_track == NULL || benchmark_f3)) ||
		(benchmark_car != NULL &&
		 SDL_strlen((const legacy_char *)benchmark_car) != REPLAY_CAR_ID_SIZE)) {
		benchmark_usage(argv[0]);
		return EXIT_FAILURE;
	}
	init_main(argc, argv);
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
		benchmark_initialize_replay(argv[BENCH_REPLAY_ARGUMENT]);
	}
	if (benchmark_compose) {
		benchmark_load_palette();
	}
	/* Keep transient HUD coverage identical across fast and slow runs. */
	visual_clock_ticks = benchmark_drive_seconds != 0U ? 0U : dos_timer_get_realtime_counter();
	visual_clock_frozen = 1;
	assert(handle_ingame_kb_shortcuts(KEY_F12) != 0);
	frame_hires_adaptive_set_preset(&frame_hires_adaptive, FRAME_HIRES_ADAPTIVE_PRESET_FULL);
	custom_camera.elevation_angle =
		benchmark_f3 ? CUSTOM_CAMERA_INITIAL_ELEVATION_ANGLE : BENCH_CAMERA_ELEVATION;
	custom_camera.azimuth_angle =
		benchmark_f3 ? CUSTOM_CAMERA_INITIAL_AZIMUTH_ANGLE : BENCH_CAMERA_AZIMUTH;
	custom_camera.distance = benchmark_f3 ? CUSTOM_CAMERA_INITIAL_DISTANCE : BENCH_CAMERA_DISTANCE;
	if (benchmark_f3) {
		cameramode = CAMERA_MODE_CUSTOM;
	}
	static const legacy_s8 cameras[] = {CAMERA_MODE_COCKPIT, CAMERA_MODE_CUSTOM};
	static const legacy_s32 scales[] = {HIRES_MINIMUM_SCALE, HIRES_MEDIUM_SCALE, HIRES_SCALE};
	legacy_u16 limit = gameconfig.game_recordedframes < BENCH_LAST_TICK
						   ? gameconfig.game_recordedframes
						   : BENCH_LAST_TICK;
	if (benchmark_start_only) {
		limit = 0;
	}
	puts(benchmark_preview
			 ? "HyperVision production track-preview benchmark: CPU and elapsed milliseconds/frame."
			 : "HyperVision full-track renderer benchmark: process CPU and elapsed "
			   "milliseconds/frame.");
	puts(benchmark_preview ? "Includes draw_track_preview and state/RNG checks; excludes menu "
							 "overlays, display and pacing."
		 : benchmark_drive_seconds != 0U
			 ? "Includes physics, interpolation, render/state checks and framebuffer "
			   "copies; excludes display and pacing."
			 : "Includes render/state checks, shadows, skybox and framebuffer "
			   "copies; excludes display, pacing and physics.");
	printf("Final ARGB composition: %s.\n", benchmark_compose ? "included" : "excluded");
	printf("Player car: %.*s%s. External camera: distance=%d elevation=%d azimuth=%d%s.\n",
		   (legacy_int)REPLAY_CAR_ID_SIZE, (const legacy_char *)gameconfig.game_playercarid,
		   benchmark_car != NULL ? " (override)" : "", custom_camera.distance,
		   custom_camera.elevation_angle, custom_camera.azimuth_angle,
		   benchmark_f3 ? " (initial F3)" : "");
	if (benchmark_drive_seconds != 0U) {
		puts("Driving straight with acceleration only and automatic transmission. "
			 "Parity/hash diagnostics are outside timing.");
		legacy_s32 result = benchmark_drive();
		call_exitlist();
		return result ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (benchmark_preview) {
		legacy_s32 result = benchmark_preview_frames(count);
		call_exitlist();
		return result ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	for (legacy_u16 tick = 0; tick <= limit; tick++) {
		if (tick % BENCH_TICK_INTERVAL == 0 || tick == limit) {
			legacy_u32 camera_count =
				benchmark_start_only || benchmark_f3 ? 1U : SDL_arraysize(cameras);
			for (legacy_u32 camera = 0; camera < camera_count; camera++) {
				cameramode = benchmark_f3 ? CAMERA_MODE_CUSTOM : cameras[camera];
				legacy_u32 first_scale = benchmark_start_only ? SDL_arraysize(scales) - 1U : 0U;
				for (legacy_u32 scale = first_scale; scale < SDL_arraysize(scales); scale++) {
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
