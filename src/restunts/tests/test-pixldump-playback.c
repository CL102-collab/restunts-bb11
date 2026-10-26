#include <assert.h>
#include <string.h>

#include "../pixldump/pixldump.c"

#undef memcpy
#undef strcmp

#define TEST_REPLAY_FRAMES 3U
#define TEST_RATE_HEADER_HIGH_BITS 0x1200U

struct LEGACY_EXECUTION_RESIDUE legacy_execution_residue;

static legacy_u8 framebuffer[64000];
static legacy_u8 output_bytes[65535];
static legacy_u8 palette_resource[SHAPE2D_HEADER_SIZE + 768];
static legacy_u32 output_length, render_count, present_count, close_count;
static legacy_s32 render_stack_enabled;
static legacy_s16 expected_bmp_mode;
static legacy_s16 expected_frame_rate;
static legacy_u16 stored_frame_rate, initialization_count;
static legacy_u8 replay_track[TRACKDATA_LINK_TABLE_SIZE];
static legacy_s8 replay_backup[TRACKDATA_TRACK_FILE_APPEND_SIZE];
static legacy_u8 checkpoint_bytes[PIXLDUMP_CVX_RESOURCE_SIZE];

void *_memcpy(void *destination, const void *source, legacy_u16 length)
{
	return memcpy(destination, source, length);
}

static void assert_frame_rate(void)
{
	if (expected_frame_rate != 0) {
		assert(framespersec == (legacy_u16)expected_frame_rate);
	}
}

legacy_s16 file_load_replay(const legacy_s8 *directory, const legacy_s8 *name)
{
	assert(directory[0] == 0 && name[0] != 0);
	gameconfig.game_framespersec = stored_frame_rate;
	gameconfig.game_recordedframes = TEST_REPLAY_FRAMES;
	return 0;
}

legacy_s16 track_setup(void)
{
	return 0;
}

void far *mmgr_alloc_resbytes(const legacy_s8 *name, legacy_s32 size)
{
	assert(name[0] != 0 && size == sizeof(checkpoint_bytes));
	return checkpoint_bytes;
}

void init_game_state(legacy_s16 mode)
{
	assert(mode == GAMESTATE_INIT_RESET_CHECKPOINTS);
	assert_frame_rate();
	initialization_count++;
}

legacy_s16 get_kevinrandom(void)
{
	return 0;
}

legacy_s16 setup_player_cars_without_dashboard(void)
{
	assert(initialization_count == 1);
	assert_frame_rate();
	return 0;
}

void set_projection(legacy_s16 horizontal_fov, legacy_s16 vertical_fov, legacy_s16 width,
					legacy_s16 height)
{
	(void)horizontal_fov;
	(void)vertical_fov;
	assert(width == PIXLDUMP_SCREEN_WIDTH && height == PIXLDUMP_SCREEN_HEIGHT);
}

void restore_gamestate(legacy_u16 frame)
{
	assert(frame == 0 || frame == TEST_REPLAY_FRAMES);
	assert_frame_rate();
}

void init_rect_arrays(void)
{
}

void far *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	assert(segment == 40960 && offset == 0);
	return framebuffer;
}

legacy_u16 dos_file_open(const legacy_s8 *path, legacy_s16 create)
{
	(void)path;
	assert(create == DOS_FILE_CREATE);
	return 5;
}

legacy_u16 dos_file_write(legacy_u16 handle, const void far *source, legacy_u16 length)
{
	assert(handle == 5);
	assert(output_length + length <= sizeof(output_bytes));
	memcpy(output_bytes + output_length, source, length);
	output_length += length;
	return length;
}

legacy_s16 dos_file_close(legacy_u16 handle)
{
	assert(handle == 5);
	close_count++;
	return 0;
}

void sprite_select_render_window(void)
{
}

void update_frame(legacy_s8 index, struct RECTANGLE *clip)
{
	assert_frame_rate();
	assert(index == 0 && clip == &rect_windshield);
	assert(render_stack_enabled);
	assert(render_count == (legacy_u32)state.game_frame);
	assert(full_redraw_frames_remaining == (render_count == 0));
	/* Each frame changes one pixel using its previous contents. Skipping an
	 * intermediate render therefore changes the eventual hash and BMP too. */
	framebuffer[0] += (legacy_u8)(state.game_frame + 1);
	render_count++;
}

void frame_present(struct RECTANGLE *clip)
{
	assert(clip == &rect_windshield);
	assert(render_count == ++present_count);
}

void mouse_draw_opaque_check(void)
{
}

void update_gamestate_with_legacy_si(legacy_s16 caller_si)
{
	assert_frame_rate();
	assert(caller_si == pixldump_caller_si);
	assert(render_count == (legacy_u32)state.game_frame + 1);
	state.game_frame++;
}

void shape3d_set_legacy_render_stack(legacy_s16 *headings, legacy_u16 frame_pointer,
									 legacy_u16 code_segment,
									 const struct SHAPE3D_LEGACY_OPPONENT_RENDER_CONTEXT *opponent)
{
	(void)frame_pointer;
	(void)code_segment;
	assert((headings != 0) == (opponent != 0));
	render_stack_enabled = headings != 0;
}

legacy_u16 pixldump_legacy_polygon_frame_pointer(legacy_s16 argv_si, legacy_s16 bmp_mode)
{
	assert(bmp_mode == expected_bmp_mode);
	return (legacy_u16)argv_si;
}

legacy_u16 pixldump_legacy_polygon_code_segment(void)
{
	return 0;
}

legacy_u32 pixldump_murmur3(const legacy_u8 far *source, legacy_u16 length)
{
	assert(source == framebuffer && length == sizeof(framebuffer));
	return source[0];
}

void far *file_load_shape2d_fatal(const legacy_s8 *name)
{
	assert(strcmp((const legacy_char *)name, "sdmain") == 0);
	return palette_resource;
}

legacy_s8 far *locate_shape_fatal(legacy_s8 far *data, const legacy_s8 *name)
{
	assert((void *)data == palette_resource);
	assert(strcmp((const legacy_char *)name, "!pal") == 0);
	return data;
}

void far *mmgr_free(legacy_s8 far *pointer)
{
	assert((void *)pointer == palette_resource);
	return 0;
}

static void reset_capture(void)
{
	expected_frame_rate = 0;
	initialization_count = 0;
	memset(framebuffer, 0, sizeof(framebuffer));
	memset(output_bytes, 0, sizeof(output_bytes));
	state.game_frame = 0;
	full_redraw_frames_remaining = 1;
	output_length = render_count = present_count = close_count = 0;
	render_stack_enabled = 0;
}

static void test_hash_capture(void)
{
	reset_capture();
	expected_bmp_mode = 0;
	gameconfig.game_recordedframes = 12;
	assert(pixldump_write_frames((const legacy_s8 *)"test.PDD") == 0);
	static const char expected[] = "PIXLDUMP 2\r\n"
								   "0 00000001\r\n"
								   "1 00000003\r\n"
								   "2 00000006\r\n"
								   "3 0000000a\r\n"
								   "4 0000000f\r\n"
								   "5 00000015\r\n"
								   "6 0000001c\r\n"
								   "7 00000024\r\n"
								   "8 0000002d\r\n"
								   "9 00000037\r\n"
								   "10 00000042\r\n"
								   "11 0000004e\r\n"
								   "12 0000005b\r\n";
	assert(output_length == sizeof(expected) - 1);
	assert(memcmp(output_bytes, expected, sizeof(expected) - 1) == 0);
	assert(render_count == 13 && present_count == 13);
	assert(close_count == 1 && !render_stack_enabled);
	assert(full_redraw_frames_remaining == 0);
}

static void test_bmp_capture(void)
{
	for (legacy_u16 frame = 0; frame <= 6; frame++) {
		reset_capture();
		expected_bmp_mode = 1;
		assert(pixldump_write_requested_frame((const legacy_s8 *)"test.bmp", frame) == 0);
		assert(output_length == 65078);
		assert(output_bytes[0] == 'B' && output_bytes[1] == 'M');
		/* The BMP stores the top scanline last. */
		assert(output_bytes[1078 + 199 * 320] == (frame + 1) * (frame + 2) / 2);
		assert(render_count == (legacy_u32)frame + 1 && present_count == render_count);
		assert(close_count == 1 && !render_stack_enabled);
		assert(full_redraw_frames_remaining == 0);
	}
}

static void test_recorded_frame_rate(void)
{
	static const struct {
		legacy_u16 stored;
		legacy_s16 expected;
	} cases[] = {
		{GAME_FRAME_RATE_LOW, GAME_FRAME_RATE_LOW},
		{GAME_FRAME_RATE_NORMAL, GAME_FRAME_RATE_NORMAL},
		{TEST_RATE_HEADER_HIGH_BITS | GAME_FRAME_RATE_LOW, GAME_FRAME_RATE_LOW},
		{TEST_RATE_HEADER_HIGH_BITS | (legacy_u8)-GAME_FRAME_RATE_LOW, -GAME_FRAME_RATE_LOW}};
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		reset_capture();
		expected_bmp_mode = 0;
		expected_frame_rate = cases[index].expected;
		stored_frame_rate = cases[index].stored;
		framespersec = 0;
		track_element_map = replay_track;
		track_and_directory_backup = replay_backup;
		assert(pixldump_process_replay((const legacy_s8 *)"rate", (const legacy_s8 *)"rate.PDD", 1,
									   0, 0, 0) == 0);
		assert(initialization_count == 1);
		assert(render_count == TEST_REPLAY_FRAMES + 1U);
		assert_frame_rate();
	}
}

int main(void)
{
	test_hash_capture();
	test_bmp_capture();
	test_recorded_frame_rate();
	return 0;
}
