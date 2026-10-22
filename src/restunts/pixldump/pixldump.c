#include <externs.h>
#include <restunts.h>
#include <fileio.h>
#include <memmgr.h>
#include <platform.h>
#include <shape2d.h>
#include <shape3d.h>
#include <trackdata_layout.h>

#include "murmur3.h"
#ifndef RESTUNTS_ORIGINAL
#include "legacy_context.h"
#endif
#include "../c/fatal.h"
#include "../c/race_resources.h"
#include "../c/residue.h"
#include "../c/shape3d.h"
#include "../c/keyboard.h"
#include "../c/game_input.h"
#include "../c/track_objects.h"
#include "../c/camera.h"
#include "../c/audio_control.h"
#ifdef RESTUNTS_ORIGINAL
#include "../platform/dos/dump_timer.h"
#endif

#define PIXLDUMP_FRAMEBUFFER_SIZE 64000U
#define PIXLDUMP_SAMPLE_INTERVAL 1U
#define PIXLDUMP_DUMP_HEADER "PIXLDUMP 2\r\n"
#define PIXLDUMP_SCREEN_WIDTH 320U
#define PIXLDUMP_SCREEN_HEIGHT 200U
#define PIXLDUMP_BMP_HEADER_SIZE 54U
#define PIXLDUMP_BMP_PALETTE_SIZE 1024U
#define PIXLDUMP_BMP_PIXEL_OFFSET 1078UL
#define PIXLDUMP_BMP_FILE_SIZE 65078UL
#define PIXLDUMP_OUTPUT_NAME_SIZE 128U
#define PIXLDUMP_STDOUT_HANDLE 1U
#define PIXLDUMP_DECIMAL_BASE 10UL
#define PIXLDUMP_FRAME_NUMBER_DIGITS 5U
#define PIXLDUMP_BMP_NAME_PREFIX_SIZE 5U
#define PIXLDUMP_SAMPLE_LINE_SIZE                                                                  \
	(PIXLDUMP_FRAME_NUMBER_DIGITS + 1U + PIXLDUMP_MURMUR3_HEX_DIGITS + 2U)
#define PIXLDUMP_HASH_HIGH_NIBBLE_SHIFT 28U
#define PIXLDUMP_HEX_NIBBLE_SHIFT 4U
#define PIXLDUMP_PALETTE_COLOR_COUNT 256U
#define PIXLDUMP_PALETTE_SOURCE_STRIDE 3U
#define PIXLDUMP_PALETTE_DESTINATION_STRIDE 4U
#define PIXLDUMP_PALETTE_EXPAND_LEFT_SHIFT 2U
#define PIXLDUMP_PALETTE_EXPAND_RIGHT_SHIFT 4U
#define PIXLDUMP_BMP_FILE_SIZE_OFFSET 2U
#define PIXLDUMP_BMP_PIXEL_OFFSET_FIELD 10U
#define PIXLDUMP_BMP_DIB_HEADER_OFFSET 14U
#define PIXLDUMP_BMP_DIB_HEADER_SIZE 40UL
#define PIXLDUMP_BMP_WIDTH_OFFSET 18U
#define PIXLDUMP_BMP_HEIGHT_OFFSET 22U
#define PIXLDUMP_BMP_PLANES_OFFSET 26U
#define PIXLDUMP_BMP_PLANES 1U
#define PIXLDUMP_BMP_BITS_PER_PIXEL_OFFSET 28U
#define PIXLDUMP_BMP_BITS_PER_PIXEL 8U
#define PIXLDUMP_BMP_IMAGE_SIZE_OFFSET 34U
#define PIXLDUMP_BMP_COLOR_COUNT_OFFSET 46U
#define PIXLDUMP_BMP_COLOR_COUNT 256UL
#define PIXLDUMP_VGA_SEGMENT 40960U
#define PIXLDUMP_CVX_RESOURCE_SIZE 22400U
#define PIXLDUMP_RANDOM_SHIFT 3U
#define PIXLDUMP_FRAME_TIMER_VALUE 500
#define PIXLDUMP_PROJECTION_DISTANCE 35
#define PIXLDUMP_PROJECTION_DIVISOR 6U

enum PIXLDUMP_PALETTE_COMPONENT_OFFSET {
	PIXLDUMP_PALETTE_BLUE_OFFSET = 0,
	PIXLDUMP_PALETTE_GREEN_OFFSET = 1,
	PIXLDUMP_PALETTE_RED_OFFSET = 2,
	PIXLDUMP_PALETTE_ALPHA_OFFSET = 3
};

#ifdef RESTUNTS_ORIGINAL
#define PIXLDUMP_DUMP_EXTENSION ".PDO"
#define PIXLDUMP_PROGRAM_NAME "PIXLDUMO"
#else
#define PIXLDUMP_DUMP_EXTENSION ".PDD"
#define PIXLDUMP_PROGRAM_NAME "PIXLDUMP"
#endif

typedef legacy_u16 PIXLDUMP_OUTPUT;

/* The original update_grip saves this caller register in a stack slot which
 * player physics can later consume as the fourth wheel's contact distance.
 * Sampling inherits the output handle; BMP mode inherits the CRT argv offset. */
static legacy_s16 pixldump_caller_si;

#ifndef RESTUNTS_ORIGINAL
static legacy_s16 pixldump_argv_si;
static legacy_u16 pixldump_polyinfo_segment;
/* Original DSEG offset of the active material color table. */
#define PIXLDUMP_LEGACY_MATERIAL_COLOR_OFFSET 20628U
#endif

#ifdef RESTUNTS_ORIGINAL
#define PIXLDUMP_LEGACY_FRAME far
static legacy_u16 pixldump_caller_di;
static legacy_u16 pixldump_saved_si;
static legacy_u16 pixldump_saved_di;

/* Keep the original engine's caller registers without changing its stack.
 * The paired inline operations restore the registers expected by Watcom. */
static void pixldump_enter_original_call(void);
#pragma aux pixldump_enter_original_call = "mov pixldump_saved_si,si"                              \
										   "mov pixldump_saved_di,di"                              \
										   "mov si,pixldump_caller_si"                             \
										   "mov di,pixldump_caller_di" modify exact[];
static void pixldump_leave_original_call(void);
#pragma aux pixldump_leave_original_call = "mov si,pixldump_saved_si"                              \
										   "mov di,pixldump_saved_di" modify exact[];
#else
#define PIXLDUMP_LEGACY_FRAME
#endif

static PIXLDUMP_OUTPUT pixldump_output_open(const legacy_s8 *path)
{
	return dos_file_open(path, DOS_FILE_CREATE);
}

static legacy_u16 pixldump_output_write(PIXLDUMP_OUTPUT output, const void far *source,
										legacy_u16 length)
{
	return dos_file_write(output, source, length);
}

static void pixldump_output_close(PIXLDUMP_OUTPUT output)
{
	(void)dos_file_close(output);
}

static void pixldump_write_stdout(const legacy_s8 *message)
{
	(void)pixldump_output_write(PIXLDUMP_STDOUT_HANDLE, message, strlen(message));
}

static legacy_s16 pixldump_parse_camera(const legacy_s8 *argument)
{
	if (argument[0] < '1' || argument[0] > '4' || argument[1] != 0) {
		return 0;
	}
	return (legacy_s16)(argument[0] - '0');
}

static legacy_s16 pixldump_parse_target(const legacy_s8 *argument)
{
	if ((argument[0] != '0' && argument[0] != '1') || argument[1] != 0) {
		return -1;
	}
	return (legacy_s16)(argument[0] - '0');
}

static legacy_s16 pixldump_parse_frame(const legacy_s8 *argument, legacy_u16 *frame)
{
	if (argument[0] == 0) {
		return 0;
	}
	legacy_u32 value = 0;
	for (legacy_u16 index = 0; argument[index] != 0; index++) {
		if (argument[index] < '0' || argument[index] > '9') {
			return 0;
		}
		value = value * PIXLDUMP_DECIMAL_BASE + (legacy_u32)(argument[index] - '0');
		if (value > LEGACY_U16_MAX) {
			return 0;
		}
	}
	*frame = (legacy_u16)value;
	return 1;
}

static void pixldump_write_usage(void)
{
	pixldump_write_stdout("Usage:\r\n");
	pixldump_write_stdout("  " PIXLDUMP_PROGRAM_NAME " REPLNAME CAMERA TARGET\r\n");
	pixldump_write_stdout("  " PIXLDUMP_PROGRAM_NAME " REPLNAME CAMERA TARGET FRAME\r\n");
	pixldump_write_stdout("CAMERA: 1=F1, 2=F2, 3=F3, 4=F4\r\n");
	pixldump_write_stdout("TARGET: 0=player, 1=opponent\r\n");
}

#ifdef RESTUNTS_ORIGINAL
void init_row_tables(void)
{
	for (legacy_s16 index = 0; index < TRACK_GRID_SIZE; index++) {
		trackrows[index] = TRACK_GRID_SIZE * (TRACK_GRID_LAST_INDEX - index);
		terrainrows[index] = TRACK_GRID_SIZE * index;
		track_row_positions[index] = (TRACK_GRID_LAST_INDEX - index) << TRACK_TILE_POSITION_SHIFT;
		track_row_centers[index] =
			((TRACK_GRID_LAST_INDEX - index) << TRACK_TILE_POSITION_SHIFT) + TRACK_TILE_HALF_SIZE;
		terrainpos[index] = index << TRACK_TILE_POSITION_SHIFT;
		terraincenterpos[index] = (index << TRACK_TILE_POSITION_SHIFT) + TRACK_TILE_HALF_SIZE;
	}

	for (legacy_s16 index = 0; index < TRACK_GRID_SIZE; index++) {
		track_column_positions[index] = index << TRACK_TILE_POSITION_SHIFT;
		track_column_centers[index] = (index << TRACK_TILE_POSITION_SHIFT) + TRACK_TILE_HALF_SIZE;
	}
}

void init_trackdata(void)
{
	legacy_s8 far *track_pointer = mmgr_alloc_resbytes("trakdata", TRACKDATA_ALLOCATION_SIZE);
	track_primary_route_links = (legacy_s16 far *)track_pointer;
	track_pointer += TRACKDATA_LINK_TABLE_SIZE;
	track_alternate_route_links = (legacy_s16 far *)track_pointer;
	track_pointer += TRACKDATA_LINK_TABLE_SIZE;
	opponent_route_track_indices = track_pointer;
	track_pointer += TRACKDATA_LINK_TABLE_SIZE;
	player_aero_resistance_table = (legacy_s16 far *)track_pointer;
	track_pointer += TRACKDATA_AERO_TABLE_SIZE;
	opponent_aero_resistance_table = (legacy_s16 far *)track_pointer;
	track_pointer += TRACKDATA_AERO_TABLE_SIZE;
	reserved_trackside_camera_words = (legacy_s16 far *)track_pointer;
	track_pointer += TRACKDATA_AERO_TABLE_SIZE;
	trackside_camera_ground_heights = (legacy_s16 far *)track_pointer;
	track_pointer += TRACKDATA_AERO_TABLE_SIZE;
	roadside_sign_headings = (legacy_s16 far *)track_pointer;
	track_pointer += TRACKDATA_DIRECTION_TABLE_SIZE;
	trackside_camera_positions = (struct VECTOR far *)track_pointer;
	track_pointer += TRACKDATA_CAMERA_VECTOR_SIZE;
	roadside_sign_positions = (struct VECTOR far *)track_pointer;
	track_pointer += TRACKDATA_CHECK_VECTOR_SIZE;
	track_highscore_table = track_pointer;
	track_pointer += TRACKDATA_HIGHSCORE_SIZE;
	sprite_background_state_stack = track_pointer;
	track_pointer += TRACKDATA_SPRITE_STATE_STACK_SIZE;
	replay_header_buffer = track_pointer;
	track_pointer += TRACKDATA_REPLAY_HEADER_SIZE;
	track_element_map = (legacy_u8 far *)track_pointer;
	track_pointer += TRACKDATA_MAP_SIZE;
	track_terrain_map = (legacy_u8 far *)track_pointer;
	track_pointer += TRACKDATA_MAP_SIZE;
	replay_input_buffer = track_pointer;
	track_pointer += TRACKDATA_REPLAY_INPUT_BUFFER_SIZE;
	track_route_element_ids = track_pointer;
	track_pointer += TRACKDATA_MAP_SIZE;
	track_route_traversal_flags = track_pointer;
	track_pointer += TRACKDATA_MAP_SIZE;
	roadside_sign_indices_by_tile = (legacy_u8 far *)track_pointer;
	track_pointer += TRACKDATA_MAP_SIZE;
	track_and_directory_backup = track_pointer;
	track_pointer += TRACKDATA_TRACK_FILE_APPEND_SIZE;
	track_route_columns = track_pointer;
	track_pointer += TRACKDATA_MAP_SIZE;
	track_route_rows = track_pointer;
	track_pointer += TRACKDATA_MAP_SIZE;
	roadside_sign_shape_indices = (legacy_u8 far *)track_pointer;
}
#endif

static legacy_u16 pixldump_append_frame_number(legacy_s8 *line, legacy_u16 frame)
{
	legacy_s8 reversed[PIXLDUMP_FRAME_NUMBER_DIGITS];
	legacy_u16 count = 0;
	do {
		reversed[count++] = (legacy_s8)('0' + frame % PIXLDUMP_DECIMAL_BASE);
		frame = (legacy_u16)(frame / PIXLDUMP_DECIMAL_BASE);
	} while (frame != 0U);

	for (legacy_u16 index = 0; index < count; index++) {
		line[index] = reversed[count - index - 1U];
	}
	return count;
}

static legacy_u16 pixldump_frame_number_length(legacy_u16 frame)
{
	legacy_u16 length = 1;
	while (frame >= PIXLDUMP_DECIMAL_BASE) {
		frame = (legacy_u16)(frame / PIXLDUMP_DECIMAL_BASE);
		length++;
	}
	return length;
}

static legacy_s16 pixldump_build_output_name(legacy_s8 *output_name, const legacy_s8 *replay_name,
											 legacy_s16 camera_number, legacy_s16 target,
											 legacy_s16 bmp_mode, legacy_u16 frame)
{
	legacy_u16 length = strlen(replay_name);
	legacy_u16 required;
	if (bmp_mode != 0) {
		required =
			(legacy_u16)(PIXLDUMP_BMP_NAME_PREFIX_SIZE + pixldump_frame_number_length(frame) +
						 strlen(PIXLDUMP_DUMP_EXTENSION ".bmp"));
	} else {
		required = strlen(PIXLDUMP_DUMP_EXTENSION);
	}
	if (length + required >= PIXLDUMP_OUTPUT_NAME_SIZE) {
		return 0;
	}

	strcpy(output_name, replay_name);
	if (bmp_mode != 0) {
		output_name[length++] = '.';
		output_name[length++] = (legacy_s8)('0' + camera_number);
		output_name[length++] = '.';
		output_name[length++] = (legacy_s8)('0' + target);
		output_name[length++] = '.';
		length += pixldump_append_frame_number(output_name + length, frame);
		strcpy(output_name + length, PIXLDUMP_DUMP_EXTENSION ".bmp");
	} else {
		strcat(output_name, PIXLDUMP_DUMP_EXTENSION);
	}
	return 1;
}

static legacy_s16 pixldump_write_sample(PIXLDUMP_OUTPUT output, legacy_u16 frame,
										const legacy_u8 far *framebuffer)
{
	legacy_u32 hash = pixldump_murmur3(framebuffer, PIXLDUMP_FRAMEBUFFER_SIZE);
	legacy_s8 line[PIXLDUMP_SAMPLE_LINE_SIZE];
	legacy_u16 line_length = pixldump_append_frame_number(line, frame);
	line[line_length++] = ' ';
	static const legacy_s8 hex_digits[] = "0123456789abcdef";
	for (legacy_u16 index = 0; index < PIXLDUMP_MURMUR3_HEX_DIGITS; index++) {
		line[line_length++] = hex_digits[(legacy_u16)(hash >> PIXLDUMP_HASH_HIGH_NIBBLE_SHIFT)];
		hash <<= PIXLDUMP_HEX_NIBBLE_SHIFT;
	}
	line[line_length++] = '\r';
	line[line_length++] = '\n';
	return pixldump_output_write(output, line, line_length) == line_length;
}

static void pixldump_store_u16(legacy_u8 *output, legacy_u16 value)
{
	output[0] = (legacy_u8)value;
	output[1] = (legacy_u8)(value >> LEGACY_BYTE_BITS);
}

static void pixldump_store_u32(legacy_u8 *output, legacy_u32 value)
{
	output[0] = (legacy_u8)value;
	output[1] = (legacy_u8)(value >> LEGACY_BYTE_BITS);
	output[2] = (legacy_u8)(value >> LEGACY_WORD_BITS);
	output[3] = (legacy_u8)(value >> LEGACY_THREE_BYTE_BITS);
}

static legacy_u8 pixldump_expand_palette_channel(legacy_u8 value)
{
	return (legacy_u8)((value << PIXLDUMP_PALETTE_EXPAND_LEFT_SHIFT) |
					   (value >> PIXLDUMP_PALETTE_EXPAND_RIGHT_SHIFT));
}

static void pixldump_load_bmp_palette(legacy_u8 *bmp_palette)
{
	legacy_s8 far *resource = (legacy_s8 far *)file_load_shape2d_fatal("sdmain");
	legacy_u8 far *source =
		(legacy_u8 far *)locate_shape_fatal(resource, "!pal") + SHAPE2D_HEADER_SIZE;
	for (legacy_u16 index = 0; index < PIXLDUMP_PALETTE_COLOR_COUNT; index++) {
		legacy_u16 source_offset = (legacy_u16)(index * PIXLDUMP_PALETTE_SOURCE_STRIDE);
		legacy_u16 destination_offset = (legacy_u16)(index * PIXLDUMP_PALETTE_DESTINATION_STRIDE);
		bmp_palette[destination_offset + PIXLDUMP_PALETTE_BLUE_OFFSET] =
			pixldump_expand_palette_channel(source[source_offset + PIXLDUMP_PALETTE_RED_OFFSET]);
		bmp_palette[destination_offset + PIXLDUMP_PALETTE_GREEN_OFFSET] =
			pixldump_expand_palette_channel(source[source_offset + PIXLDUMP_PALETTE_GREEN_OFFSET]);
		bmp_palette[destination_offset + PIXLDUMP_PALETTE_RED_OFFSET] =
			pixldump_expand_palette_channel(source[source_offset + PIXLDUMP_PALETTE_BLUE_OFFSET]);
		bmp_palette[destination_offset + PIXLDUMP_PALETTE_ALPHA_OFFSET] = 0;
	}
	(void)mmgr_free(resource);
}

static legacy_s16 pixldump_write_bmp(const legacy_s8 *output_name, const legacy_u8 far *framebuffer)
{
	legacy_u8 header[PIXLDUMP_BMP_HEADER_SIZE];
	for (legacy_u16 index = 0; index < PIXLDUMP_BMP_HEADER_SIZE; index++) {
		header[index] = 0;
	}
	header[0] = 'B';
	header[1] = 'M';
	pixldump_store_u32(header + PIXLDUMP_BMP_FILE_SIZE_OFFSET, PIXLDUMP_BMP_FILE_SIZE);
	pixldump_store_u32(header + PIXLDUMP_BMP_PIXEL_OFFSET_FIELD, PIXLDUMP_BMP_PIXEL_OFFSET);
	pixldump_store_u32(header + PIXLDUMP_BMP_DIB_HEADER_OFFSET, PIXLDUMP_BMP_DIB_HEADER_SIZE);
	pixldump_store_u32(header + PIXLDUMP_BMP_WIDTH_OFFSET, PIXLDUMP_SCREEN_WIDTH);
	pixldump_store_u32(header + PIXLDUMP_BMP_HEIGHT_OFFSET, PIXLDUMP_SCREEN_HEIGHT);
	pixldump_store_u16(header + PIXLDUMP_BMP_PLANES_OFFSET, PIXLDUMP_BMP_PLANES);
	pixldump_store_u16(header + PIXLDUMP_BMP_BITS_PER_PIXEL_OFFSET, PIXLDUMP_BMP_BITS_PER_PIXEL);
	pixldump_store_u32(header + PIXLDUMP_BMP_IMAGE_SIZE_OFFSET, PIXLDUMP_FRAMEBUFFER_SIZE);
	pixldump_store_u32(header + PIXLDUMP_BMP_COLOR_COUNT_OFFSET, PIXLDUMP_BMP_COLOR_COUNT);

	static legacy_u8 bmp_palette[PIXLDUMP_BMP_PALETTE_SIZE];
	pixldump_load_bmp_palette(bmp_palette);
	PIXLDUMP_OUTPUT output = pixldump_output_open(output_name);
	if (output == 0) {
		return 1;
	}

	legacy_s16 result =
		pixldump_output_write(output, header, PIXLDUMP_BMP_HEADER_SIZE) == PIXLDUMP_BMP_HEADER_SIZE;
	if (result != 0) {
		result = pixldump_output_write(output, bmp_palette, PIXLDUMP_BMP_PALETTE_SIZE) ==
				 PIXLDUMP_BMP_PALETTE_SIZE;
	}
	for (legacy_u16 row = 0; result != 0 && row < PIXLDUMP_SCREEN_HEIGHT; row++) {
		const legacy_u8 far *source_row =
			framebuffer + (legacy_u16)(PIXLDUMP_SCREEN_WIDTH * (PIXLDUMP_SCREEN_HEIGHT - row - 1U));
		result = pixldump_output_write(output, source_row, PIXLDUMP_SCREEN_WIDTH) ==
				 PIXLDUMP_SCREEN_WIDTH;
	}

	pixldump_output_close(output);
	return result == 0;
}

static void PIXLDUMP_LEGACY_FRAME pixldump_render_frame(void)
{
#ifdef RESTUNTS_ORIGINAL
	pixldump_enter_original_call();
#endif
	sprite_select_render_window();
	update_frame(0, &rect_windshield);
	frame_present(&rect_windshield);
	/* The normal presentation path draws the software mouse cursor last. */
	mouse_draw_opaque_check();
#ifdef RESTUNTS_ORIGINAL
	pixldump_leave_original_call();
#endif
	if (full_redraw_frames_remaining != 0) {
		full_redraw_frames_remaining--;
	}
}

#ifdef RESTUNTS_ORIGINAL
extern void kb_shift_checking1(void);
#endif

static void PIXLDUMP_LEGACY_FRAME pixldump_update_gamestate(void)
{
#ifdef RESTUNTS_ORIGINAL
	pixldump_enter_original_call();
	input_do_checking(1);
	update_gamestate();
	pixldump_leave_original_call();
#else
	update_gamestate_with_legacy_si(pixldump_caller_si);
#endif
}

#ifndef RESTUNTS_ORIGINAL
static void pixldump_enable_legacy_render_stack(legacy_s16 bmp_mode)
{
	struct SHAPE3D_LEGACY_OPPONENT_RENDER_CONTEXT opponent;

	opponent.wheel_headings = legacy_execution_residue.wheel_angle_stack_words;
	opponent.polyinfo_offset = 0;
	opponent.polyinfo_segment = pixldump_polyinfo_segment;
	opponent.material_color_offset = PIXLDUMP_LEGACY_MATERIAL_COLOR_OFFSET;
	shape3d_set_legacy_render_stack(
		legacy_execution_residue.wheel_plane_angles,
		pixldump_legacy_polygon_frame_pointer(pixldump_argv_si, bmp_mode),
		pixldump_legacy_polygon_code_segment(), &opponent);
}
#endif

static legacy_s16 PIXLDUMP_LEGACY_FRAME pixldump_write_frames(const legacy_s8 *output_name)
{
	PIXLDUMP_OUTPUT output = pixldump_output_open(output_name);
	if (output == 0) {
		return 1;
	}

	pixldump_caller_si = LEGACY_S16_FROM_BITS(output);
#ifdef RESTUNTS_ORIGINAL
	/* The directory-copy loop leaves DI here until the first sample result. */
	pixldump_caller_di = TRACKDATA_CHECKPOINT_DATA_SIZE;
#endif
#ifndef RESTUNTS_ORIGINAL
	pixldump_enable_legacy_render_stack(0);
#endif
	legacy_u8 far *framebuffer = (legacy_u8 far *)dos_memory_make_pointer(PIXLDUMP_VGA_SEGMENT, 0);
	pixldump_render_frame();
	legacy_s16 result =
		pixldump_output_write(output, PIXLDUMP_DUMP_HEADER, sizeof(PIXLDUMP_DUMP_HEADER) - 1U) !=
			sizeof(PIXLDUMP_DUMP_HEADER) - 1U ||
		!pixldump_write_sample(output, 0U, framebuffer);
#ifdef RESTUNTS_ORIGINAL
	pixldump_caller_di = 0;
#endif

	while (result == 0 && gameconfig.game_recordedframes > (legacy_u16)state.game_frame) {
		pixldump_update_gamestate();
		/* Dirty regions describe consecutive rendered frames. */
		pixldump_render_frame();
		if ((legacy_u16)state.game_frame % PIXLDUMP_SAMPLE_INTERVAL == 0U) {
			if (!pixldump_write_sample(output, (legacy_u16)state.game_frame, framebuffer)) {
				result = 1;
			}
		}
	}

#ifndef RESTUNTS_ORIGINAL
	shape3d_set_legacy_render_stack(0, 0, 0, 0);
#endif
	pixldump_output_close(output);
	return result;
}

static legacy_s16 PIXLDUMP_LEGACY_FRAME pixldump_write_requested_frame(const legacy_s8 *output_name,
																	   legacy_u16 requested_frame)
{
#ifdef RESTUNTS_ORIGINAL
	/* Retain the archived BMP caller's four-byte local stack slot. */
	legacy_u8 far *volatile framebuffer;
#endif

#ifndef RESTUNTS_ORIGINAL
	pixldump_enable_legacy_render_stack(1);
#endif
	pixldump_render_frame();
	while ((legacy_u16)state.game_frame < requested_frame) {
		pixldump_update_gamestate();
		pixldump_render_frame();
	}
#ifndef RESTUNTS_ORIGINAL
	shape3d_set_legacy_render_stack(0, 0, 0, 0);
	legacy_u8 far *framebuffer;
#endif
	framebuffer = (legacy_u8 far *)dos_memory_make_pointer(PIXLDUMP_VGA_SEGMENT, 0);
	return pixldump_write_bmp(output_name, framebuffer);
}

static legacy_s16 PIXLDUMP_LEGACY_FRAME pixldump_process_replay(
	const legacy_s8 *replay_name, const legacy_s8 *output_name, legacy_s16 camera_number,
	legacy_s16 target, legacy_s16 bmp_mode, legacy_u16 requested_frame)
{
	if (file_load_replay("", replay_name) != 0) {
		return 1;
	}
	/* Playback physics and its rate-dependent setup follow the replay viewer. */
	framespersec =
		(legacy_s16)LEGACY_S8_FROM_BITS(LEGACY_U16_LOW_BYTE(gameconfig.game_framespersec));
#ifndef RESTUNTS_ORIGINAL
	/* Keep the new filename overlay out of renderer oracle comparisons. */
	replay_filename[0] = 0;
#endif
	if (target != 0 && gameconfig.game_opponenttype == 0) {
		pixldump_write_stdout("Replay does not contain an opponent.\r\n");
		return 1;
	}
	if (bmp_mode != 0 && requested_frame > gameconfig.game_recordedframes) {
		pixldump_write_stdout("Requested frame is outside the replay.\r\n");
		return 1;
	}

	_memcpy(&gameconfigcopy, &gameconfig, sizeof(struct GAMEINFO));
	for (legacy_u16 index = 0; index < TRACKDATA_LINK_TABLE_SIZE; index++) {
		track_and_directory_backup[index] = track_element_map[index];
	}
	for (legacy_u16 index = 0; index < TRACKDATA_CHECKPOINT_DATA_SIZE; index++) {
		track_and_directory_backup[index + TRACKDATA_LINK_TABLE_SIZE] = track_directory[index];
		track_and_directory_backup[index + TRACKDATA_CHECKPOINT_SECOND_OFFSET] =
			replay_directory[index];
	}
	if (track_setup() != 0) {
		return 1;
	}

	cvxptr = mmgr_alloc_resbytes("cvx", PIXLDUMP_CVX_RESOURCE_SIZE);
	init_game_state(-1);
	viewport_bottom_cache = -1;
	run_game_random = LEGACY_S16_SHL(get_kevinrandom(), PIXLDUMP_RANDOM_SHIFT);
	replaybar_toggle = 0;
	is_in_replay = 1;
	idle_expired = 0;
	cameramode = (legacy_s8)(camera_number - 1);
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	detail_level = 0;
	slow_video_mgmt = 1;
	slow_video_mgmt_copy = 1;

#ifdef RESTUNTS_ORIGINAL
	if (setup_player_cars() != 0) {
#else
	if (setup_player_cars_without_dashboard() != 0) {
#endif
		return 1;
	}

	kbormouse = 0;
	replay_playback_speed = REPLAY_PLAYBACK_NORMAL;
	race_exit_request = 1;
	game_replay_mode_copy = -1;
	frame_buffer_index = 0;
	dashboard_buffer_index = 0;
	recording_limit_warning_requested = 0;
	dashb_toggle = 0;
	followOpponentFlag = (legacy_u8)target;
	start_flag_animation = PIXLDUMP_FRAME_TIMER_VALUE;
	rect_windshield.left = 0;
	rect_windshield.right = 320;
	rect_windshield.top = 0;
	rect_windshield.bottom = 200;
	set_projection(PIXLDUMP_PROJECTION_DISTANCE,
				   PIXLDUMP_SCREEN_HEIGHT / PIXLDUMP_PROJECTION_DIVISOR, PIXLDUMP_SCREEN_WIDTH,
				   PIXLDUMP_SCREEN_HEIGHT);

	restore_gamestate(0);
	restore_gamestate(gameconfig.game_recordedframes);
	/* The capture uses one render buffer. Initialize it once, then retain
	 * the normal incremental redraw history through the entire replay. */
	init_rect_arrays();
	full_redraw_frames_remaining = 1;
	if (bmp_mode != 0) {
		return pixldump_write_requested_frame(output_name, requested_frame);
	}
	return pixldump_write_frames(output_name);
}

legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[])
{
	if (argc != 4 && argc != 5) {
		pixldump_write_usage();
		return 1;
	}
	legacy_s16 camera_number = pixldump_parse_camera(argv[2]);
	if (camera_number == 0) {
		pixldump_write_stdout("Camera must be 1 (F1), 2 (F2), 3 (F3), or 4 (F4).\r\n");
		return 1;
	}
	legacy_s16 target = pixldump_parse_target(argv[3]);
	if (target < 0) {
		pixldump_write_stdout("Target must be 0 (player) or 1 (opponent).\r\n");
		return 1;
	}
	legacy_s16 bmp_mode = argc == 5;
	legacy_u16 requested_frame = 0;
	if (bmp_mode != 0 && !pixldump_parse_frame(argv[4], &requested_frame)) {
		pixldump_write_stdout("Frame must be an integer from 0 to 65535.\r\n");
		return 1;
	}

#ifdef RESTUNTS_ORIGINAL
	pixldump_caller_si = (legacy_s16)argv;
	pixldump_caller_di = TRACKDATA_CHECKPOINT_DATA_SIZE;
#else
	pixldump_argv_si = pixldump_legacy_argv_si(argc, argv);
	if (bmp_mode != 0) {
		pixldump_caller_si = pixldump_argv_si;
	}
#endif

	legacy_s16 length = (legacy_s16)strlen(argv[1]);
	if (length >= 4 &&
		(strcmp(argv[1] + length - 4, ".rpl") == 0 || strcmp(argv[1] + length - 4, ".RPL") == 0)) {
		argv[1][length - 4] = 0;
		length -= 4;
	}
	legacy_s8 output_name[PIXLDUMP_OUTPUT_NAME_SIZE];
	if (!pixldump_build_output_name(output_name, argv[1], camera_number, target, bmp_mode,
									requested_frame)) {
		pixldump_write_stdout("Output path is too long.\r\n");
		return 1;
	}

	init_main(argc, argv);
#ifdef RESTUNTS_ORIGINAL
	dump_disable_timer_irq();
#endif
	init_div0();
	init_row_tables();
	mainresptr = file_load_resfile("main");
	fontdefptr = file_load_resource(0, "fontdef.fnt");
	fontnptr = file_load_resource(0, "fontn.fnt");
	font_set_fontdef();
	init_polyinfo();
#ifndef RESTUNTS_ORIGINAL
	pixldump_polyinfo_segment = pixldump_legacy_polyinfo_segment();
#endif
	init_trackdata();
	reset_race_loop_state();
	init_kevinrandom("kevin");
	legacy_s16 result = pixldump_process_replay(argv[1], output_name, camera_number, target,
												bmp_mode, requested_frame);

#ifdef RESTUNTS_ORIGINAL
	legacy_timer_shutdown();
	audiodrv_atexit();
	kb_exit_handler();
	kb_shift_checking1();
	video_set_mode7();
#else
	call_exitlist();
#endif
	return result;
}
