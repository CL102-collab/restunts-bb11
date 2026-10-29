/* Keep score-screen decisions and track-preview rendering real, while the
 * .cshig adapter is stubbed. Codec/I/O integrity has separate runtime tests. */
#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "../c/highscore.c"
#include "../c/menu_track.c"
#include "../c/extended_highscore.h"
#undef printf
#undef strlen
#undef strcmp
#undef strcpy
#undef strcat
#undef memcpy

struct GAMEINFO gameconfig;
struct SPRITE *render_window_sprite;
struct RECTANGLE highscore_text_bounds;
struct EXTENDED_REPLAY_CLOCKS extended_race_stats;
legacy_s8 resID_buffer[RESID_BUFFER_SIZE];
legacy_s8 track_directory[81];
legacy_s8 g_path_buf[256];
legacy_s8 highscore_player_name_input[HIGHSCORE_PLAYER_NAME_BYTES];
legacy_s8 gnam_string[64];
legacy_s8 gsna_string[8];
legacy_s8 opponent_highscore_name[8];
legacy_s8 track_file_extension[] = ".trk";
legacy_s8 highscore_track_disk_prompt_id[] = "hstd";
legacy_s8 replay_recording_flags;
legacy_s8 g_is_busy;
legacy_u16 framespersec;
legacy_u16 dialog_border_color;
legacy_s16 dialog_fnt_colour;
legacy_s16 font_glyph_height;
legacy_s16 gState_total_finish_time;
legacy_s16 gState_penalty;
legacy_s8 *track_highscore_table;
legacy_u8 *track_element_map;
void *fontnptr;
void *mainresptr;
legacy_s16 button_top_color;
legacy_s16 button_bottom_color;
legacy_s16 button_fill_color;

#define TEST_GLYPH_WIDTH 5U
#define TEST_GLYPH_BIT_COUNT 8U
#define TEST_NEW_SCORE_FRAMES 123U
#define TEST_FIRST_SCORE_TICKS 300U
#define TEST_SECOND_SCORE_TICKS 500U
#define TEST_PLACEHOLDER_FIELD_COUNT (HIGHSCORE_TEXT_FIELD_COUNT - 1U)

static struct CSTRK_TRACK fixture_model;
static struct CSHIG_ENTRY fixture_scores[CSHIG_ENTRY_COUNT];
static struct HIGHSCORE_ENTRY legacy_scores[HIGHSCORE_ENTRY_COUNT];
static legacy_u8 legacy_track[END_SCREEN_TRACK_VALIDATION_BYTES];
static int fixture_extended, validate_error, load_error, record_error, qualifies_result;
static unsigned validate_calls, load_calls, qualify_calls, record_calls;
static unsigned prompt_calls, input_calls, acknowledgement_calls, preview_labels, preview_fields;
static unsigned legacy_reads, legacy_writes;
static uint32_t qualified_frames;
static uint16_t qualified_rate;
static uint64_t formatted_ticks;
static struct CSHIG_ENTRY captured_entry;
static char preview_time[32];
static char table_time[32];
static legacy_s16 preview_time_x, table_time_x;
static unsigned first_row_fields;
static char captured_dialog[256];
static const legacy_s8 *fixture_error_message;
static legacy_u16 fixture_record_rank;
static legacy_s16 current_font_color;
static legacy_u8 table_pixels[END_SCREEN_TOP_HEIGHT][END_SCREEN_WIDTH];

static void check_adapter_arguments(const legacy_s8 *directory, const legacy_s8 *name,
									const struct CSTRK_TRACK *track)
{
	assert(fixture_extended && directory == track_directory);
	assert(strcmp((const char *)directory, "tracks") == 0);
	assert(strcmp((const char *)name, "TRACK") == 0 && track == &fixture_model);
}

int extended_track_active(void)
{
	return fixture_extended;
}

const struct CSTRK_TRACK *extended_track_model(void)
{
	return &fixture_model;
}

int extended_replay_is_active(void)
{
	return fixture_extended;
}

int extended_highscore_load_or_create(const legacy_s8 *directory, const legacy_s8 *name,
									  const struct CSTRK_TRACK *track, int create_default)
{
	check_adapter_arguments(directory, name, track);
	assert(create_default == 0 || create_default == 1);
	++load_calls;
	return load_error;
}

const struct CSHIG_ENTRY *extended_highscore_entry(unsigned index)
{
	assert(fixture_extended);
	return index < CSHIG_ENTRY_COUNT && !load_error ? &fixture_scores[index] : NULL;
}

int extended_highscore_validate_track(const legacy_s8 *directory, const legacy_s8 *name,
									  const struct CSTRK_TRACK *track)
{
	check_adapter_arguments(directory, name, track);
	++validate_calls;
	return validate_error;
}

uint64_t extended_highscore_ticks(uint32_t frames, uint16_t rate)
{
	assert(rate == 10 || rate == 20);
	return (uint64_t)frames * (20U / rate);
}

int extended_highscore_qualifies(uint32_t frames, uint16_t rate)
{
	assert(fixture_extended);
	++qualify_calls;
	qualified_frames = frames;
	qualified_rate = rate;
	return qualifies_result && frames != 0;
}

int extended_highscore_record(const legacy_s8 *directory, const legacy_s8 *name,
							  const struct CSTRK_TRACK *track, const struct CSHIG_ENTRY *entry,
							  unsigned *rank)
{
	check_adapter_arguments(directory, name, track);
	/* The adapter must never be asked to save the provisional blank name. */
	assert(prompt_calls == 1 && input_calls == 1);
	assert(entry != NULL && rank != NULL);
	captured_entry = *entry;
	++record_calls;
	if (record_error) {
		return 1;
	}
	*rank = fixture_record_rank;
	for (legacy_u16 index = CSHIG_ENTRY_COUNT - 1U; index > fixture_record_rank; index--) {
		fixture_scores[index] = fixture_scores[index - 1U];
	}
	fixture_scores[fixture_record_rank] = *entry;
	return 0;
}

int extended_highscore_format_time(legacy_s8 *out, size_t size, uint64_t ticks)
{
	if (ticks == CSHIG_EMPTY_TICKS) {
		ticks = 0;
	} else {
		formatted_ticks = ticks;
	}
	int count = snprintf((char *)out, size, "%2" PRIu64 ":%02u.%02u", ticks / 1200U,
						 (unsigned)(ticks / 20U % 60U), (unsigned)(ticks % 20U * 5U));
	assert(count >= 0 && (size_t)count < size);
	return 0;
}

const legacy_s8 *extended_highscore_error(void)
{
	return fixture_error_message;
}

legacy_s8 *_strcpy(legacy_s8 *destination, const legacy_s8 *source)
{
	return (legacy_s8 *)strcpy((char *)destination, (const char *)source);
}

legacy_s8 *_strcat(legacy_s8 *destination, const legacy_s8 *source)
{
	return (legacy_s8 *)strcat((char *)destination, (const char *)source);
}

legacy_u16 _strlen(const legacy_s8 *text)
{
	return (legacy_u16)strlen((const char *)text);
}

void *_memcpy(void *destination, const void *source, legacy_u16 size)
{
	return memcpy(destination, source, size);
}

void file_build_path(const legacy_s8 *directory, const legacy_s8 *name, const legacy_s8 *extension,
					 legacy_s8 *out)
{
	(void)directory;
	assert(!fixture_extended);
	snprintf((char *)out, sizeof(g_path_buf), "%s%s", (const char *)name, (const char *)extension);
}

void *file_load_resource(legacy_s16 type, const legacy_s8 *path)
{
	(void)path;
	assert(!fixture_extended && type == FILE_RESOURCE_BINARY_OPTIONAL);
	return legacy_track;
}

void *file_read_nofatal(const legacy_s8 *path, void *destination)
{
	(void)path;
	assert(!fixture_extended && destination == legacy_scores);
	++legacy_reads;
	return destination;
}

legacy_s16 file_write_fatal(const legacy_s8 *path, void *data, legacy_u32 size)
{
	(void)path;
	(void)data;
	assert(!fixture_extended && size == HIGHSCORE_TABLE_SIZE_BYTES);
	++legacy_writes;
	return 0;
}

void mmgr_release(void *resource)
{
	assert(resource == legacy_track);
}

legacy_s16 show_disk_error_dialog(void)
{
	assert(!fixture_extended);
	return HIGHSCORE_READ_RETRY_CANCEL_RESULT;
}

legacy_u16 show_dialog(legacy_s16 type, legacy_s16 save, void *text, legacy_u16 x, legacy_u16 y,
					   legacy_s16 color, legacy_s16 *positions, legacy_s16 selected)
{
	(void)save;
	assert(text != NULL && x == DIALOG_AUTO_POSITION && selected == 0);
	assert(y == DIALOG_AUTO_POSITION && color == dialog_border_color);
	if (type == DIALOG_TYPE_PLACEHOLDERS) {
		assert(record_calls == 0 && positions != NULL);
		assert(fixture_scores[2].ticks == CSHIG_EMPTY_TICKS);
		positions[0] = 20;
		positions[1] = 30;
		++prompt_calls;
	} else {
		assert(type == DIALOG_TYPE_ACKNOWLEDGEMENT);
		size_t length = strlen((const char *)text);
		assert(length > 0 && length < sizeof(captured_dialog));
		assert(((const char *)text)[length - 1U] == ']');
		unsigned line_length = 0;
		for (size_t i = 0; i < length; i++) {
			unsigned character = ((const unsigned char *)text)[i];
			if (character == ']') {
				assert(line_length > 0 && line_length <= 35U);
				line_length = 0;
			} else {
				assert(character >= 32U && character <= 126U);
				++line_length;
			}
		}
		assert(line_length == 0);
		strcpy(captured_dialog, (const char *)text);
		++acknowledgement_calls;
	}
	return 1;
}

legacy_s16 call_read_line(legacy_s8 *text, legacy_s16 maximum, legacy_s16 x, legacy_s16 y,
						  legacy_u32 timeout)
{
	assert(prompt_calls == 1 && record_calls == 0);
	assert(maximum == 16 && x == 20 && y == 30 && timeout != 0);
	strcpy((char *)text, "ABCDEFGHIJKLMNOP");
	++input_calls;
	return 1;
}

void check_input(void)
{
}
void sprite_select_render_window(void)
{
}
legacy_s16 sprite_blit_to_video(struct SPRITE *sprite, legacy_s16 mode)
{
	(void)sprite;
	(void)mode;
	return 0;
}
void font_set_fontdef(void)
{
}
void font_set_fontdef2(void *font)
{
	(void)font;
}
void font_set_colors(legacy_s16 color, legacy_s16 background)
{
	current_font_color = color;
	(void)background;
}
legacy_s16 font_text_width(const legacy_s8 *text)
{
	return (legacy_s16)(_strlen(text) * TEST_GLYPH_WIDTH);
}
legacy_s16 font_centered_text_x(const legacy_s8 *text)
{
	(void)text;
	return 0;
}
void draw_button(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
				 legacy_s16 top_color, legacy_s16 bottom_color, legacy_s16 fill_color,
				 legacy_s16 font_color)
{
	(void)text;
	(void)top_color;
	(void)bottom_color;
	(void)font_color;
	for (legacy_s16 row = y; row < y + height && row < END_SCREEN_TOP_HEIGHT; row++) {
		for (legacy_s16 column = x; column < x + width && column < (legacy_s16)END_SCREEN_WIDTH;
			 column++) {
			if (row >= 0 && column >= 0) {
				table_pixels[row][column] = (legacy_u8)fill_color;
			}
		}
	}
}

void font_draw_text(const legacy_s8 *text, legacy_s16 x, legacy_s16 y)
{
	/* Like the real font renderer, unset glyph bits leave old pixels visible. */
	for (legacy_u16 character = 0; text[character] != 0; character++) {
		if (text[character] == ' ') {
			continue;
		}
		for (legacy_u16 glyph_y = 0; glyph_y < font_glyph_height; glyph_y++) {
			for (legacy_u16 glyph_x = 0; glyph_x < TEST_GLYPH_WIDTH; glyph_x++) {
				legacy_s16 pixel_x = x + character * TEST_GLYPH_WIDTH + glyph_x;
				legacy_s16 pixel_y = y + glyph_y;
				legacy_u16 bit = (glyph_x + glyph_y) % TEST_GLYPH_BIT_COUNT;
				if (pixel_x >= 0 && pixel_x < (legacy_s16)END_SCREEN_WIDTH && pixel_y >= 0 &&
					pixel_y < END_SCREEN_TOP_HEIGHT &&
					((legacy_u8)text[character] & (1U << bit)) != 0) {
					table_pixels[pixel_y][pixel_x] = (legacy_u8)current_font_color;
				}
			}
		}
	}
	if (y == TRACK_MENU_HIGHSCORE_ENTRY_Y) {
		if (++preview_fields % HIGHSCORE_TEXT_FIELD_COUNT == 0) {
			strcpy(preview_time, (const char *)text);
			preview_time_x = x;
		}
	}
	if (y == HIGHSCORE_FIRST_ROW_Y) {
		if (++first_row_fields % HIGHSCORE_TEXT_FIELD_COUNT == 0) {
			strcpy(table_time, (const char *)text);
			table_time_x = x;
		}
	}
}
struct RECTANGLE *intro_draw_text(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 color,
								  legacy_s16 shadow)
{
	(void)text;
	(void)x;
	(void)color;
	(void)shadow;
	assert(y == TRACK_MENU_HIGHSCORE_LABEL_Y);
	++preview_labels;
	return &highscore_text_bounds;
}
void copy_string(legacy_s8 *destination, legacy_s8 *source)
{
	_strcpy(destination, source);
}
legacy_s8 *locate_text_res(legacy_s8 *resource, const legacy_s8 *name)
{
	(void)resource;
	(void)name;
	return (legacy_s8 *)"Score";
}
void format_frame_as_string(legacy_s8 *out, legacy_s16 frames, legacy_s16 fraction)
{
	(void)fraction;
	assert(!fixture_extended);
	snprintf((char *)out, HIGHSCORE_FORMAT_BUFFER_SIZE, "%u", (unsigned)(legacy_u16)frames);
}

static void reset_fixture(legacy_u32 finish, legacy_u16 rate)
{
	memset(fixture_scores, 0, sizeof(fixture_scores));
	for (unsigned i = 0; i < CSHIG_ENTRY_COUNT; i++) {
		fixture_scores[i].ticks = CSHIG_EMPTY_TICKS;
		highscore_entry_set_time(&legacy_scores[i], HIGHSCORE_EMPTY_TIME);
	}
	fixture_extended = 1;
	fixture_record_rank = 2;
	validate_error = load_error = record_error = 0;
	qualifies_result = 1;
	validate_calls = load_calls = qualify_calls = record_calls = 0;
	prompt_calls = input_calls = acknowledgement_calls = preview_labels = preview_fields = 0;
	legacy_reads = legacy_writes = first_row_fields = 0;
	preview_time_x = table_time_x = -1;
	preview_time[0] = table_time[0] = 0;
	formatted_ticks = qualified_frames = qualified_rate = 0;
	captured_dialog[0] = 0;
	fixture_error_message =
		(const legacy_s8
			 *)"The high score file could not be saved because the destination directory changed "
			   "while the name prompt was open. This plain text has no dialog delimiters and "
			   "contains a long token: "
			   "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789. "
			   "Any remaining explanation must be bounded without losing the final delimiter "
			   "needed by the game's dialog renderer.";
	memset(&captured_entry, 0, sizeof(captured_entry));
	memset(&extended_race_stats, 0, sizeof(extended_race_stats));
	extended_race_stats.player_finish = finish;
	extended_race_stats.penalty = finish != 0 ? 25U : 0U;
	extended_race_stats.player_end = finish != 0 ? finish - 25U : 0U;
	gState_total_finish_time = LEGACY_S16_FROM_BITS((legacy_u16)finish);
	gState_penalty = 25;
	framespersec = rate;
	replay_recording_flags = REPLAY_RECORDING_ACTIVE_FLAG;
	gameconfig.game_opponenttype = 0;
	strcpy((char *)gameconfig.game_trackname, "TRACK");
	strcpy((char *)track_directory, "tracks");
	strcpy((char *)gnam_string, "Very long car display name exceeding the field");
	strcpy((char *)opponent_highscore_name, "OP");
	strcpy((char *)gsna_string, "CAR4");
	highscore_player_name_input[0] = 0;
	dialog_border_color = 3;
	button_fill_color = END_SCREEN_COLOR;
	memset(table_pixels, button_fill_color, sizeof(table_pixels));
	font_glyph_height = 8;
	track_element_map = legacy_track;
	track_highscore_table = (legacy_s8 *)legacy_scores;
	ranking_highlight = HIGHSCORE_NO_HIGHLIGHT;
	for (unsigned i = 0; i < HIGHSCORE_ENTRY_COUNT; i++) {
		ranking_entry_order[i] = (legacy_s16)i;
	}
}

static void check_wide_finish(legacy_u32 finish, legacy_u8 opponent)
{
	reset_fixture(finish, GAME_FRAME_RATE_LOW);
	gameconfig.game_opponenttype = opponent;
	struct END_SCREEN_STATE screen = {0};
	end_hiscore_check_score(&screen);
	assert(screen.score_status == 1 && screen.finish_time == finish);
	assert(qualified_frames == finish && qualified_rate == GAME_FRAME_RATE_LOW);
	assert(record_calls == 0 && prompt_calls == 0);
	end_hiscore_enter_score(&screen, (void *)"Name", opponent != 0);
	assert(prompt_calls == 1 && input_calls == 1 && record_calls == 1);
	assert(captured_entry.ticks == (uint64_t)finish * 2U);
	assert(strcmp((const char *)captured_entry.player_name, "ABCDEFGHIJKLMNOP") == 0);
	assert(strlen((const char *)captured_entry.car_name) == CSHIG_CAR_NAME_BYTES - 1U);
	assert(strncmp((const char *)captured_entry.car_name, (const char *)gnam_string,
				   CSHIG_CAR_NAME_BYTES - 1U) == 0);
	assert(captured_entry.car_flag == (opponent != 0));
	assert(strcmp((const char *)captured_entry.opponent, opponent ? "OP/CAR4" : " ") == 0);
	assert(ranking_highlight == 2 && legacy_reads == 0 && legacy_writes == 0);
}

static void check_eligibility(void)
{
	for (unsigned flag = REPLAY_RECORDING_MODIFIED_FLAG;
		 flag <= REPLAY_RECORDING_HIGHSCORE_INELIGIBLE_FLAGS; flag += 2U) {
		reset_fixture(65536U, GAME_FRAME_RATE_LOW);
		replay_recording_flags = (legacy_s8)flag;
		struct END_SCREEN_STATE screen = {0};
		end_hiscore_check_score(&screen);
		assert(screen.score_status == 0 && record_calls == 0 && prompt_calls == 0);
	}
	reset_fixture(0, GAME_FRAME_RATE_LOW);
	gState_total_finish_time = 123;
	struct END_SCREEN_STATE screen = {0};
	end_hiscore_check_score(&screen);
	assert(screen.score_status == 0 && screen.finish_time == 0 && qualify_calls == 0);
	reset_fixture(70001U, GAME_FRAME_RATE_LOW);
	qualifies_result = 0;
	end_hiscore_check_score(&screen);
	assert(screen.score_status == 0 && qualified_frames == 70001U && record_calls == 0);
	reset_fixture(70001U, GAME_FRAME_RATE_LOW);
	load_error = 1;
	end_hiscore_check_score(&screen);
	assert(screen.score_status == -1 && qualify_calls == 0 && record_calls == 0);
	reset_fixture(70001U, GAME_FRAME_RATE_LOW);
	validate_error = 1;
	end_hiscore_check_score(&screen);
	assert(screen.score_status == -1 && qualify_calls == 0 && record_calls == 0);
	reset_fixture(70001U, GAME_FRAME_RATE_LOW);
	end_hiscore_check_score(&screen);
	assert(screen.score_status == 1);
	validate_error = 1; /* Track changed after evaluation, before name entry. */
	end_hiscore_enter_score(&screen, (void *)"Name", 0);
	assert(prompt_calls == 0 && input_calls == 0 && record_calls == 0);
}

static void check_save_failure(void)
{
	reset_fixture(70001U, GAME_FRAME_RATE_LOW);
	struct END_SCREEN_STATE screen = {0};
	end_hiscore_check_score(&screen);
	record_error = 1; /* Also models the adapter detecting a change during entry. */
	end_hiscore_enter_score(&screen, (void *)"Name", 0);
	assert(prompt_calls == 1 && input_calls == 1 && record_calls == 1);
	assert(acknowledgement_calls == 1 && fixture_scores[2].ticks == CSHIG_EMPTY_TICKS);
	assert(ranking_highlight == HIGHSCORE_NO_HIGHLIGHT);
	assert(strstr(captured_dialog, "high score") != NULL);
	reset_fixture(70001U, GAME_FRAME_RATE_LOW);
	fixture_error_message = (const legacy_s8 *)"";
	extended_hiscore_show_error();
	assert(acknowledgement_calls == 1 && captured_dialog[0] != ']');
}

static void check_extended_print_and_preview(void)
{
	reset_fixture(70001U, GAME_FRAME_RATE_LOW);
	strcpy((char *)fixture_scores[0].player_name, "Driver");
	strcpy((char *)fixture_scores[0].car_name, "Car");
	strcpy((char *)fixture_scores[0].opponent, "OP/CAR4");
	fixture_scores[0].car_flag = 1;
	fixture_scores[0].ticks = UINT64_C(140002);
	legacy_u8 offsets[4];
	print_highscore_entry(0, offsets);
	assert(formatted_ticks == UINT64_C(140002));
	assert(strcmp((char *)resID_buffer + offsets[0], "Driver") == 0);
	assert(strcmp((char *)resID_buffer + offsets[1], "Car") == 0);
	assert(strcmp((char *)resID_buffer + offsets[2], "(OP/CAR4)") == 0);
	assert(strcmp((char *)resID_buffer + offsets[3], "116:40.10") == 0);
	assert(framespersec == GAME_FRAME_RATE_LOW);
	framespersec = GAME_FRAME_RATE_NORMAL;
	print_highscore_entry(0, offsets);
	assert(strcmp((char *)resID_buffer + offsets[3], "116:40.10") == 0);
	assert(framespersec == GAME_FRAME_RATE_NORMAL);
	track_menu_draw_highscore();
	assert(preview_labels == 1 && preview_fields == 4);
	assert(strcmp(preview_time, "116:40.10") == 0);
	assert(preview_time_x == TRACK_MENU_HIGHSCORE_TIME_X);
	fixture_scores[0].ticks = CSHIG_EMPTY_TICKS;
	preview_labels = preview_fields = 0;
	track_menu_draw_highscore();
	assert(preview_labels == 0 && preview_fields == 0);
}

static void check_empty_placeholders(void)
{
	reset_fixture(TEST_NEW_SCORE_FRAMES, GAME_FRAME_RATE_NORMAL);
	legacy_s8 expected[TEST_PLACEHOLDER_FIELD_COUNT][RESID_BUFFER_SIZE];
	legacy_u8 offsets[HIGHSCORE_TEXT_FIELD_COUNT];
	fixture_extended = 0;
	assert(highscore_load_or_create(1) == 0);
	print_highscore_entry(0, offsets);
	for (legacy_u16 field = 0; field < TEST_PLACEHOLDER_FIELD_COUNT; field++) {
		strcpy((char *)expected[field], (const char *)resID_buffer + offsets[field]);
	}
	fixture_extended = 1;
	for (legacy_u16 entry = 0; entry < HIGHSCORE_ENTRY_COUNT; entry++) {
		print_highscore_entry(entry, offsets);
		for (legacy_u16 field = 0; field < TEST_PLACEHOLDER_FIELD_COUNT; field++) {
			assert(strcmp((const char *)resID_buffer + offsets[field],
						  (const char *)expected[field]) == 0);
		}
		assert(strcmp((const char *)resID_buffer + offsets[3], " 0:00.00") == 0);
	}
	load_error = 1;
	print_highscore_entry(0, offsets);
	for (legacy_u16 field = 0; field < TEST_PLACEHOLDER_FIELD_COUNT; field++) {
		assert(strcmp((const char *)resID_buffer + offsets[field], (const char *)expected[field]) ==
			   0);
	}
}

static void check_normal_time_alignment(void)
{
	reset_fixture(TEST_NEW_SCORE_FRAMES, GAME_FRAME_RATE_NORMAL);
	fixture_scores[0].ticks = TEST_FIRST_SCORE_TICKS;
	highscore_draw_table();
	assert(strcmp(table_time, " 0:15.00") == 0);
	assert(table_time_x == HIGHSCORE_TIME_COLUMN_X);
	track_menu_draw_highscore();
	assert(strcmp(preview_time, table_time) == 0);
	assert(preview_time_x == TRACK_MENU_HIGHSCORE_TIME_X);
}

static void check_score_insertion_redraw(void)
{
	reset_fixture(TEST_NEW_SCORE_FRAMES, GAME_FRAME_RATE_NORMAL);
	strcpy((char *)fixture_scores[0].player_name, "Previous winner");
	strcpy((char *)fixture_scores[0].car_name, "Previous car");
	strcpy((char *)fixture_scores[0].opponent, "OP/CAR1");
	fixture_scores[0].ticks = TEST_FIRST_SCORE_TICKS;
	strcpy((char *)fixture_scores[1].player_name, "Second driver");
	strcpy((char *)fixture_scores[1].car_name, "Second car");
	strcpy((char *)fixture_scores[1].opponent, "OP/CAR2");
	fixture_scores[1].ticks = TEST_SECOND_SCORE_TICKS;
	strcpy((char *)gnam_string, "New car");
	fixture_record_rank = 0;
	highscore_draw_table();
	struct END_SCREEN_STATE screen = {0};
	end_hiscore_check_score(&screen);
	end_hiscore_enter_score(&screen, (void *)"Name", 0);
	assert(record_calls == 1 && ranking_highlight == fixture_record_rank);
	assert(fixture_scores[1].ticks == TEST_FIRST_SCORE_TICKS);
	assert(fixture_scores[2].ticks == TEST_SECOND_SCORE_TICKS);
	static legacy_u8 after_insertion[END_SCREEN_TOP_HEIGHT][END_SCREEN_WIDTH];
	memcpy(after_insertion, table_pixels, sizeof(after_insertion));
	memset(table_pixels, button_fill_color, sizeof(table_pixels));
	highscore_draw_table();
	assert(memcmp(after_insertion, table_pixels, sizeof(table_pixels)) == 0);
}

static void check_long_time_alignment(void)
{
	reset_fixture(70001U, GAME_FRAME_RATE_LOW);
	fixture_scores[0].ticks = UINT64_MAX - 1U;
	legacy_s8 expected[32];
	extended_highscore_format_time(expected, sizeof(expected), fixture_scores[0].ticks);
	assert(font_text_width(expected) >
		   (legacy_s16)(END_SCREEN_WIDTH - HIGHSCORE_TIME_COLUMN_X - 1U));
	highscore_draw_table();
	assert(first_row_fields == HIGHSCORE_TEXT_FIELD_COUNT);
	assert(strcmp(table_time, (const char *)expected) == 0);
	assert(table_time_x < HIGHSCORE_TIME_COLUMN_X);
	assert(table_time_x + font_text_width((const legacy_s8 *)table_time) <= 319);
	track_menu_draw_highscore();
	assert(preview_labels == 1 && preview_fields == HIGHSCORE_TEXT_FIELD_COUNT);
	assert(strcmp(preview_time, (const char *)expected) == 0);
	assert(preview_time_x < TRACK_MENU_HIGHSCORE_TIME_X);
	assert(preview_time_x + font_text_width((const legacy_s8 *)preview_time) <= 319);
}

static void check_legacy_eligibility(void)
{
	reset_fixture(650U, GAME_FRAME_RATE_NORMAL);
	fixture_extended = 0;
	struct END_SCREEN_STATE screen = {0};
	end_hiscore_check_score(&screen);
	assert(screen.score_status == 1 && screen.finish_time == 650);
	assert(validate_calls == 0 && load_calls == 0 && qualify_calls == 0 && legacy_reads == 1);
	highscore_entry_set_time(&legacy_scores[0], 650U);
	highscore_draw_table();
	assert(table_time_x == HIGHSCORE_TIME_COLUMN_X && strcmp(table_time, "650") == 0);
	track_menu_draw_highscore();
	assert(preview_time_x == TRACK_MENU_HIGHSCORE_TIME_X && strcmp(preview_time, "650") == 0);
}

int main(void)
{
	check_wide_finish(65536U, 0);
	check_wide_finish(70001U, 2);
	check_eligibility();
	check_save_failure();
	check_extended_print_and_preview();
	check_empty_placeholders();
	check_normal_time_alignment();
	check_score_insertion_redraw();
	check_long_time_alignment();
	check_legacy_eligibility();
	puts("Extended highscore UI checks passed");
	return 0;
}
