#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "../c/legacy.h"
#include "../c/shape2d.h"
#include "../c/hires.h"

#define TEST_PATH_SIZE 1024
#define TEST_BUFFER_SIZE 16384
#define TEST_FIXTURE_SIZE 16
#define TEST_FRAME_NS 250000000ULL
#define TEST_CLIP_NS (3ULL * TEST_FRAME_NS)
#define TEST_START_NS 5000000000ULL
#define TEST_LONG_PAUSE_LOOPS 100U
#define TEST_RELOAD_COUNT 16U
#define TEST_CLIP_OPPONENT 1U
#define TEST_OTHER_OPPONENT 2U
#define TEST_DRAW_X 3
#define TEST_DRAW_Y 5
#define TEST_DRAW_WIDTH 7
#define TEST_DRAW_HEIGHT 9
#define TEST_CHANNEL_MIN 200U
#define TEST_CHANNEL_MAX 30U
#define TEST_COLOR_RED 16U
#define TEST_COLOR_GREEN 8U
#define TEST_COLOR_BLUE 0U
#define TEST_ALPHA_MASK 0xFF000000U
#define TEST_KEYFRAME_PREFIX_SIZE 3U
#define TEST_TRUNCATED_SIZE 32U
#define TEST_ODD_WIDTH 17
#define TEST_ODD_HEIGHT 19
#define TEST_DURATION_PREFIX_SIZE 3U
#define TEST_EBML_VOID_ID 0xECU
#define TEST_EXPECTED_ARGUMENTS 2
#define TEST_FIXTURE_ARGUMENT 1

static legacy_u64 clock_ns;
static legacy_u8 hypervision;
static legacy_s32 render_scale = HIRES_SCALE;
static legacy_u32 drawn_color;
static legacy_u32 drawn_pixels;
static legacy_s32 draw_left, draw_right, draw_top, draw_bottom;
static legacy_u8 drawing;
static struct SPRITE target;
static const legacy_char *fixture_directory;

static legacy_u64 animation_test_ticks(void)
{
	return clock_ns;
}

/* Decode the real container/bitstream with a deterministic presentation clock. */
#define SDL_GetTicksNS animation_test_ticks
#define asset_path_base test_asset_path_base
#undef RESTUNTS_OPPONENT_DIRECTORY
#define RESTUNTS_OPPONENT_DIRECTORY "source"
#include "../c/opponent_animation.c"
#undef SDL_GetTicksNS
#undef asset_path_base

const legacy_char *test_asset_path_base(void)
{
	return "installed/";
}

legacy_s32 hires_enabled(void)
{
	return hypervision;
}

legacy_s32 hires_render_scale(void)
{
	return render_scale;
}

legacy_s32 hires_begin_argb(const struct SPRITE *sprite)
{
	assert(sprite == &target && !drawing);
	drawing = 1;
	drawn_pixels = 0;
	return 1;
}

void hires_argb_pixel(legacy_s32 x, legacy_s32 y, legacy_u32 color)
{
	assert(drawing);
	assert(x >= draw_left && x < draw_right && y >= draw_top && y < draw_bottom);
	if (drawn_pixels == 0) {
		drawn_color = color;
	}
	drawn_pixels++;
}

void hires_end(void)
{
	assert(drawing);
	drawing = 0;
}

static legacy_s32 draw(void)
{
	draw_left = TEST_DRAW_X * render_scale;
	draw_right = draw_left + TEST_DRAW_WIDTH * render_scale;
	draw_top = TEST_DRAW_Y * render_scale;
	draw_bottom = draw_top + TEST_DRAW_HEIGHT * render_scale;
	drawn_pixels = 0;
	return opponent_animation_draw(&target, TEST_DRAW_X, TEST_DRAW_Y, TEST_DRAW_WIDTH,
								   TEST_DRAW_HEIGHT);
}

static void assert_color(legacy_u32 channel)
{
	assert(draw());
	assert(!drawing);
	assert(drawn_pixels == (legacy_u32)((draw_right - draw_left) * (draw_bottom - draw_top)));
	assert((drawn_color & TEST_ALPHA_MASK) == TEST_ALPHA_MASK);
	for (legacy_u32 shift = TEST_COLOR_BLUE; shift <= TEST_COLOR_RED; shift += LEGACY_BYTE_BITS) {
		legacy_u32 value = (drawn_color >> shift) & LEGACY_U8_MAX;
		assert(shift == channel ? value >= TEST_CHANNEL_MIN : value <= TEST_CHANNEL_MAX);
	}
}

static void assert_unloaded(void)
{
	static const struct OPPONENT_ANIMATION empty;
	assert(memcmp(&animation, &empty, sizeof(animation)) == 0);
	assert(!draw() && drawn_pixels == 0);
}

static size_t read_fixture(const legacy_char *name, legacy_u8 *bytes)
{
	legacy_char path[TEST_PATH_SIZE];
	legacy_s32 length = snprintf(path, sizeof(path), "%s/%s", fixture_directory, name);
	assert(length > 0 && (size_t)length < sizeof(path));
	FILE *file = fopen(path, "rb");
	assert(file != NULL);
	size_t size = fread(bytes, 1, TEST_BUFFER_SIZE, file);
	assert(size > 0 && size < TEST_BUFFER_SIZE && !ferror(file));
	assert(fclose(file) == 0);
	return size;
}

static void write_bytes(const legacy_char *path, const legacy_u8 *bytes, size_t size)
{
	FILE *file = fopen(path, "wb");
	assert(file != NULL && fwrite(bytes, 1, size, file) == size);
	assert(fclose(file) == 0);
}

static void copy_fixture(const legacy_char *name, const legacy_char *path)
{
	legacy_u8 bytes[TEST_BUFFER_SIZE];
	size_t size = read_fixture(name, bytes);
	write_bytes(path, bytes, size);
}

static void test_timing(void)
{
	copy_fixture("colors.webm", "opponents/animations/opp1win.webm");
	hypervision = 0;
	clock_ns = 0;
	opponent_animation_load(TEST_CLIP_OPPONENT, 1);
	assert(!draw() && drawn_pixels == 0);
	assert(animation.frame != NULL && animation.frame->w == TEST_FIXTURE_SIZE &&
		   animation.frame->h == TEST_FIXTURE_SIZE);
	/* The second packet is an interframe, exercising VP8 reference frames. */
	legacy_u8 *packet;
	size_t packet_size;
	assert(nestegg_packet_data(animation.packet, 0, &packet, &packet_size) == 0);
	assert(packet_size > 0 && (packet[0] & ANIMATION_VP8_INTERFRAME_FLAG));
	clock_ns = TEST_START_NS;
	hypervision = 1;
	assert_color(TEST_COLOR_RED);
	clock_ns += TEST_FRAME_NS - 1;
	assert_color(TEST_COLOR_RED);
	clock_ns++;
	assert_color(TEST_COLOR_GREEN);
	clock_ns += TEST_FRAME_NS;
	assert_color(TEST_COLOR_BLUE);
	clock_ns = TEST_START_NS + TEST_CLIP_NS - 1;
	assert_color(TEST_COLOR_BLUE);
	clock_ns++;
	assert_color(TEST_COLOR_RED);
	clock_ns = TEST_START_NS + TEST_CLIP_NS * TEST_LONG_PAUSE_LOOPS + TEST_FRAME_NS;
	assert_color(TEST_COLOR_GREEN);
	/* Disabling enhancement does not draw or replace the classic fallback. */
	hypervision = 0;
	clock_ns += TEST_FRAME_NS;
	assert(!draw() && drawn_pixels == 0);
	hypervision = 1;
	assert_color(TEST_COLOR_BLUE);
	for (render_scale = HIRES_MINIMUM_SCALE; render_scale <= HIRES_SCALE; render_scale *= 2) {
		assert_color(TEST_COLOR_BLUE);
	}
	render_scale = HIRES_SCALE;
	opponent_animation_unload();
	assert_unloaded();
	opponent_animation_unload();
	assert_unloaded();
	assert(remove("opponents/animations/opp1win.webm") == 0);
}

static void test_search_order(void)
{
	copy_fixture("colors.webm", "opponents/animations/opp1win.webm");
	copy_fixture("reverse.webm", "installed/opponents/animations/opp1win.webm");
	copy_fixture("colors.webm", "source/animations/opp1win.webm");
	copy_fixture("reverse.webm", "opponents/animations/opp1lose.webm");
	opponent_animation_load(TEST_CLIP_OPPONENT, 1);
	assert_color(TEST_COLOR_RED);
	opponent_animation_load(TEST_CLIP_OPPONENT, 0);
	assert_color(TEST_COLOR_BLUE);
	assert(remove("opponents/animations/opp1win.webm") == 0);
	opponent_animation_load(TEST_CLIP_OPPONENT, 1);
	assert_color(TEST_COLOR_BLUE);
	/* A malformed local override must not prevent a usable installed clip. */
	static const legacy_u8 invalid[] = "not a WebM container";
	write_bytes("opponents/animations/opp1win.webm", invalid, sizeof(invalid));
	opponent_animation_load(TEST_CLIP_OPPONENT, 1);
	assert_color(TEST_COLOR_BLUE);
	assert(remove("opponents/animations/opp1win.webm") == 0);
	assert(remove("installed/opponents/animations/opp1win.webm") == 0);
	opponent_animation_load(TEST_CLIP_OPPONENT, 1);
	assert_color(TEST_COLOR_RED);
	assert(remove("source/animations/opp1win.webm") == 0);
	opponent_animation_load(TEST_CLIP_OPPONENT, 1);
	assert_unloaded();
	/* A missing result/opponent never reuses the previously loaded clip. */
	opponent_animation_load(TEST_CLIP_OPPONENT, 0);
	assert_color(TEST_COLOR_BLUE);
	opponent_animation_load(TEST_OTHER_OPPONENT, 0);
	assert_unloaded();
	assert(remove("opponents/animations/opp1lose.webm") == 0);
}

static void test_inferred_duration(void)
{
	legacy_u8 bytes[TEST_BUFFER_SIZE];
	size_t size = read_fixture("colors.webm", bytes);
	static const legacy_u8 duration_prefix[TEST_DURATION_PREFIX_SIZE] = {0x44, 0x89, 0x88};
	legacy_u8 changed = 0;
	for (size_t offset = 0; offset + sizeof(duration_prefix) <= size; offset++) {
		if (memcmp(bytes + offset, duration_prefix, sizeof(duration_prefix)) == 0) {
			/* Replace Duration with an equal-length EBML Void, retaining the clip. */
			bytes[offset] = TEST_EBML_VOID_ID;
			changed = 1;
			break;
		}
	}
	assert(changed);
	write_bytes("opponents/animations/opp1win.webm", bytes, size);
	opponent_animation_load(TEST_CLIP_OPPONENT, 1);
	assert(animation.duration == 0);
	assert_color(TEST_COLOR_RED);
	clock_ns += TEST_CLIP_NS - 1;
	assert_color(TEST_COLOR_BLUE);
	clock_ns++;
	assert_color(TEST_COLOR_RED);
	opponent_animation_unload();
	assert_unloaded();
	assert(remove("opponents/animations/opp1win.webm") == 0);
}

static void test_unusable_inputs(void)
{
	const legacy_char *path = "opponents/animations/opp1win.webm";
	copy_fixture("vp9.webm", path);
	opponent_animation_load(TEST_CLIP_OPPONENT, 1);
	assert_unloaded();
	legacy_u8 bytes[TEST_BUFFER_SIZE];
	size_t size = read_fixture("colors.webm", bytes);
	write_bytes(path, bytes, TEST_TRUNCATED_SIZE);
	opponent_animation_load(TEST_CLIP_OPPONENT, 1);
	assert_unloaded();
	/* The container is valid but the first VP8 keyframe header is damaged. */
	static const legacy_u8 keyframe_prefix[TEST_KEYFRAME_PREFIX_SIZE] = {0x9D, 0x01, 0x2A};
	legacy_u8 changed = 0;
	for (size_t offset = 0; offset + sizeof(keyframe_prefix) <= size; offset++) {
		if (memcmp(bytes + offset, keyframe_prefix, sizeof(keyframe_prefix)) == 0) {
			bytes[offset] = 0;
			changed = 1;
			break;
		}
	}
	assert(changed);
	write_bytes(path, bytes, size);
	opponent_animation_load(TEST_CLIP_OPPONENT, 1);
	assert_unloaded();
	/* Reject a keyframe whose dimensions disagree with the container before
	 * allocating decoder reference images, even if the container is small. */
	size = read_fixture("colors.webm", bytes);
	changed = 0;
	for (size_t offset = 0; offset + sizeof(keyframe_prefix) + LEGACY_WORD_BYTES <= size;
		 offset++) {
		if (memcmp(bytes + offset, keyframe_prefix, sizeof(keyframe_prefix)) == 0) {
			LEGACY_WRITE_U16_LE(bytes + offset + sizeof(keyframe_prefix),
								ANIMATION_MAX_DIMENSION + 1);
			changed = 1;
			break;
		}
	}
	assert(changed);
	write_bytes(path, bytes, size);
	opponent_animation_load(TEST_CLIP_OPPONENT, 1);
	assert_unloaded();
	copy_fixture("colors.webm", path);
	opponent_animation_load(OPPONENT_FIRST - 1, 1);
	assert_unloaded();
	opponent_animation_load(OPPONENT_LAST + 1, 1);
	assert_unloaded();
	opponent_animation_load(TEST_CLIP_OPPONENT, 1);
	assert(!opponent_animation_draw(NULL, TEST_DRAW_X, TEST_DRAW_Y, TEST_DRAW_WIDTH,
									TEST_DRAW_HEIGHT));
	assert(!opponent_animation_draw(&target, TEST_DRAW_X, TEST_DRAW_Y, 0, TEST_DRAW_HEIGHT));
	assert(!opponent_animation_draw(&target, TEST_DRAW_X, TEST_DRAW_Y, TEST_DRAW_WIDTH, -1));
	assert_color(TEST_COLOR_RED);
	/* A later damaged packet must retire its decoder and return the fallback. */
	legacy_u8 *packet;
	size_t packet_size;
	assert(nestegg_packet_data(animation.packet, 0, &packet, &packet_size) == 0);
	memset(packet, 0, packet_size);
	clock_ns += TEST_FRAME_NS;
	assert(!draw());
	assert_unloaded();
	assert(remove(path) == 0);
}

static void test_odd_dimensions(void)
{
	copy_fixture("odd.webm", "opponents/animations/opp1win.webm");
	opponent_animation_load(TEST_CLIP_OPPONENT, 1);
	assert(animation.frame != NULL && animation.frame->w == TEST_ODD_WIDTH &&
		   animation.frame->h == TEST_ODD_HEIGHT);
	assert_color(TEST_COLOR_RED);
	clock_ns += TEST_FRAME_NS;
	assert_color(TEST_COLOR_GREEN);
	clock_ns += TEST_FRAME_NS;
	assert_color(TEST_COLOR_BLUE);
	opponent_animation_unload();
	assert_unloaded();
	assert(remove("opponents/animations/opp1win.webm") == 0);
}

static void test_audio_is_ignored(void)
{
	copy_fixture("with-audio.webm", "opponents/animations/opp1win.webm");
	for (legacy_u32 iteration = 0; iteration < TEST_RELOAD_COUNT; iteration++) {
		opponent_animation_load(TEST_CLIP_OPPONENT, 1);
		assert_color(TEST_COLOR_BLUE);
		clock_ns += TEST_FRAME_NS;
		assert_color(TEST_COLOR_GREEN);
		clock_ns += TEST_FRAME_NS;
		assert_color(TEST_COLOR_RED);
		assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
		opponent_animation_unload();
		assert_unloaded();
	}
	assert(remove("opponents/animations/opp1win.webm") == 0);
}

legacy_int main(legacy_int argc, legacy_char **argv)
{
	assert(argc == TEST_EXPECTED_ARGUMENTS);
	fixture_directory = argv[TEST_FIXTURE_ARGUMENT];
	assert(SDL_Init(0));
	assert(SDL_CreateDirectory("opponents/animations"));
	assert(SDL_CreateDirectory("installed/opponents/animations"));
	assert(SDL_CreateDirectory("source/animations"));
	test_timing();
	test_search_order();
	test_inferred_duration();
	test_unusable_inputs();
	test_odd_dimensions();
	test_audio_is_ignored();
	assert(rmdir("opponents/animations") == 0 && rmdir("opponents") == 0);
	assert(rmdir("installed/opponents/animations") == 0 && rmdir("installed/opponents") == 0 &&
		   rmdir("installed") == 0);
	assert(rmdir("source/animations") == 0 && rmdir("source") == 0);
	SDL_Quit();
	puts("Opponent VP8 animation loading, fallback, timing, looping and cleanup tests passed.");
	return 0;
}
