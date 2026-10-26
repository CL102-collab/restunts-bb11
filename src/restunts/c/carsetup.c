#include "externs.h"
#include "fileio.h"
#include "memmgr.h"
#include "platform.h"
#include "residue.h"
#include "race_resources.h"
#include "car_speed.h"
#include "track_collision.h"
#include "camera.h"
#include "car_model.h"
#include "car_resources.h"
#include "resource.h"
#include "owoot.h"
#include "physics_internal.h"

extern legacy_s8 opponent_name_text_id[];
extern legacy_s8 opponent_path_resource_id[];
extern legacy_s8 opponent_speed_resource_id[];

#define CAR_FILENAME_PREFIX_LENGTH 2U
#define CAR_ID_LENGTH 4U
#define CAR_RESOURCE_EXTENSION_LENGTH 5U
#define CAR_RESOURCE_FILENAME_LENGTH 11U
#define LEGACY_RESOURCE_WORD_SIZE 2U
#define LEGACY_PARAGRAPH_SIZE 16UL
#define LEGACY_TRACKDATA_PARAGRAPHS 1728U
#define LEGACY_CVX_PARAGRAPHS 1401U
#define LEGACY_PENALTY_ALIAS_OFFSET 65534UL
#define AERO_RESISTANCE_TABLE_COUNT 64
#define OPPONENT_ROUTE_PATH_CAPACITY 902U
#define OPPONENT_ROUTE_BRANCH_CAPACITY 258U
#define OPPONENT_ROUTE_DISTANCE_CAPACITY 259U
#define OPPONENT_ROUTE_DISTANCE_LIMIT 999999UL
#define OPPONENT_ROUTE_COST_COUNT 256U

static void legacy_car_filename(legacy_s8 *destination, const legacy_s8 car_id[CAR_ID_LENGTH],
								const legacy_s8 extension[CAR_RESOURCE_EXTENSION_LENGTH])
{
	destination[0] = 's';
	destination[1] = 't';
	for (legacy_u16 index = 0U; index < CAR_ID_LENGTH; index++) {
		destination[index + CAR_FILENAME_PREFIX_LENGTH] = car_id[index];
	}
	for (legacy_u16 index = 0U; index < CAR_RESOURCE_EXTENSION_LENGTH; index++) {
		destination[index + CAR_FILENAME_PREFIX_LENGTH + CAR_ID_LENGTH] = extension[index];
	}
}

static legacy_u16 legacy_car_model_paragraphs(const legacy_s8 car_id[CAR_ID_LENGTH])
{
	static const legacy_s8 compressed_extension[CAR_RESOURCE_EXTENSION_LENGTH] = {'.', 'p', '3',
																				  's', 0};
	legacy_s8 filename[CAR_RESOURCE_FILENAME_LENGTH];
	legacy_car_filename(filename, car_id, compressed_extension);
	legacy_u16 paragraphs = file_decomp_paras_nofatal(filename);
	if (paragraphs != 0U) {
		return paragraphs;
	}
	static const legacy_s8 raw_extension[CAR_RESOURCE_EXTENSION_LENGTH] = {'.', '3', 's', 'h', 0};
	legacy_car_filename(filename, car_id, raw_extension);
	return file_paras_nofatal(filename);
}

static legacy_s16 legacy_raw_resource_word(const legacy_s8 *filename, legacy_u32 offset,
										   legacy_s16 *value)
{
	legacy_u16 handle = dos_file_open(filename, DOS_FILE_OPEN_EXISTING);
	if (handle == 0U) {
		return 0;
	}
	legacy_u8 bytes[LEGACY_RESOURCE_WORD_SIZE];
	if (dos_file_seek(handle, (legacy_s32)offset, DOS_FILE_SEEK_BEGIN) != 0 ||
		dos_file_read(handle, bytes, LEGACY_RESOURCE_WORD_SIZE) != LEGACY_RESOURCE_WORD_SIZE) {
		(void)dos_file_close(handle);
		return 0;
	}
	(void)dos_file_close(handle);
	*value = LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(bytes));
	return 1;
}

static legacy_s16 legacy_alias_from_raw_resource(const legacy_s8 *filename, legacy_u32 *cursor,
												 legacy_s16 *value)
{
	legacy_u16 paragraphs = file_paras_nofatal(filename);
	legacy_u32 length = (legacy_u32)paragraphs * LEGACY_PARAGRAPH_SIZE;
	if (LEGACY_PENALTY_ALIAS_OFFSET >= *cursor &&
		LEGACY_PENALTY_ALIAS_OFFSET + 1UL < *cursor + length) {
		legacy_u32 resource_offset = LEGACY_PENALTY_ALIAS_OFFSET - *cursor;
		return legacy_raw_resource_word(filename, resource_offset, value);
	}
	*cursor += length;
	return 0;
}

/*
 * detect_penalty in the original executable indexes td01 with route -1.
 * Its 16-bit offset wraps to 65534 in the trakdata segment, aliasing a word
 * in resources allocated later. Reconstruct the original low-arena offsets
 * without loading the dashboard or the menu-only car shapes. Words in an
 * unmodeled compressed/render resource are treated as an invalid route; the
 * observed stock and custom resources in that position contain out-of-range
 * values, while valid aliases occur in the small raw resources handled here.
 */
void setup_legacy_penalty_route_word(void)
{
	legacy_s16 value = (legacy_s16)LEGACY_S16_MAX;
	legacy_u32 cursor =
		(legacy_u32)(LEGACY_TRACKDATA_PARAGRAPHS + LEGACY_CVX_PARAGRAPHS) * LEGACY_PARAGRAPH_SIZE;
	legacy_u16 player_model_paragraphs = legacy_car_model_paragraphs(gameconfig.game_playercarid);
	cursor += (legacy_u32)player_model_paragraphs * LEGACY_PARAGRAPH_SIZE;

	if ((legacy_u8)gameconfig.game_opponentcarid[0] != LEGACY_U8_MAX) {
		legacy_s16 same_car = 1;
		for (legacy_u16 index = 0U; index < CAR_ID_LENGTH; index++) {
			if (gameconfig.game_playercarid[index] != gameconfig.game_opponentcarid[index]) {
				same_car = 0;
			}
		}
		if (same_car != 0) {
			cursor += (legacy_u32)LEGACY_U16_WRAP_ADD(player_model_paragraphs, 1U) *
					  LEGACY_PARAGRAPH_SIZE;
		} else {
			legacy_u16 opponent_model_paragraphs =
				legacy_car_model_paragraphs(gameconfig.game_opponentcarid);
			cursor += (legacy_u32)opponent_model_paragraphs * LEGACY_PARAGRAPH_SIZE;
		}
	}

	if (legacy_alias_from_raw_resource("pceng1.vce", &cursor, &value) == 0 &&
		legacy_alias_from_raw_resource("geeng.sfx", &cursor, &value) == 0) {
		(void)legacy_alias_from_raw_resource("fontled.fnt", &cursor, &value);
	}
	legacy_execution_residue.penalty_route_word = value;
}

static void opponent_route_write(legacy_u16 index, legacy_u16 value)
{
	legacy_u16 offset = LEGACY_U16_WRAP_MUL(index, 2U);
	LEGACY_WRITE_U16_LE((legacy_u8 far *)opponent_route_track_indices + offset, value);
}

legacy_u8 car_surface_adhesion[2];
struct CAR_SURFACE_ADHESION_SETTINGS car_surface_adhesion_settings[2] = {
	{CAR_ADHESION_DEFAULT_DISTANCE, CAR_ADHESION_DEFAULT_ALIGNMENT, CAR_ADHESION_SURFACE_MASK},
	{CAR_ADHESION_DEFAULT_DISTANCE, CAR_ADHESION_DEFAULT_ALIGNMENT, CAR_ADHESION_SURFACE_MASK}};

static void reset_car_surface_adhesion(legacy_s16 car_index)
{
	car_surface_adhesion[car_index] = 0;
	car_surface_adhesion_settings[car_index].max_distance = CAR_ADHESION_DEFAULT_DISTANCE;
	car_surface_adhesion_settings[car_index].min_alignment = CAR_ADHESION_DEFAULT_ALIGNMENT;
	car_surface_adhesion_settings[car_index].surface_mask = CAR_ADHESION_SURFACE_MASK;
}

void reset_car_capabilities(void)
{
	reset_car_surface_adhesion(0);
	reset_car_surface_adhesion(1);
}

static void setup_car_surface_adhesion(void far *car_resource, legacy_s16 is_opponent)
{
	enum CAR_ADHESION_CHUNK {
		CAR_ADHESION_CHUNK_FLAG,
		CAR_ADHESION_CHUNK_SETTINGS,
		CAR_ADHESION_CHUNK_COUNT
	};
	legacy_s16 car_index = is_opponent != 0;
	reset_car_surface_adhesion(car_index);
	if (!car_resource) {
		return;
	}
	/* Bound both optional payloads using their directory neighbours. Malformed
	 * settings fail closed; a missing settings chunk keeps `fand` compatibility. */
	const legacy_u8 huge *resource = car_resource;
	legacy_u32 size = LEGACY_READ_U32_LE(resource + RESOURCE_FILE_SIZE_OFFSET);
	legacy_u16 count = resource_file_count(car_resource);
	legacy_u32 offsets_start =
		RESOURCE_FILE_DIRECTORY_OFFSET + (legacy_u32)count * RESOURCE_FILE_IDENTIFIER_SIZE;
	legacy_u32 data_start = offsets_start + (legacy_u32)count * RESOURCE_FILE_OFFSET_SIZE;
	if (size <= data_start) {
		return;
	}
	legacy_u32 data_size = size - data_start;
	legacy_u32 starts[CAR_ADHESION_CHUNK_COUNT];
	legacy_u32 ends[CAR_ADHESION_CHUNK_COUNT];
	for (legacy_u16 slot = 0; slot < CAR_ADHESION_CHUNK_COUNT; slot++) {
		starts[slot] = data_size;
		ends[slot] = data_size;
	}
	legacy_u16 aliases[CAR_ADHESION_CHUNK_COUNT] = {0};
	legacy_u16 found = 0;
	for (legacy_u16 index = 0; index < count; index++) {
		legacy_u32 offset = LEGACY_READ_U32_LE(resource + offsets_start +
											   (legacy_u32)index * RESOURCE_FILE_OFFSET_SIZE);
		if (offset > data_size) {
			return;
		}
		const legacy_u8 huge *name = resource + RESOURCE_FILE_DIRECTORY_OFFSET +
									 (legacy_u32)index * RESOURCE_FILE_IDENTIFIER_SIZE;
		legacy_u16 slot;
		if (name[0] == 'f' && name[1] == 'a' && name[2] == 'n' && name[3] == 'd') {
			slot = CAR_ADHESION_CHUNK_FLAG;
		} else if (name[0] == 'a' && name[1] == 'd' && name[2] == 'h' && name[3] == 's') {
			slot = CAR_ADHESION_CHUNK_SETTINGS;
		} else {
			continue;
		}
		if ((found & (1U << slot)) != 0 || offset >= data_size) {
			return;
		}
		starts[slot] = offset;
		found |= 1U << slot;
	}
	if ((found & (1U << CAR_ADHESION_CHUNK_FLAG)) == 0) {
		return;
	}
	for (legacy_u16 index = 0; index < count; index++) {
		legacy_u32 offset = LEGACY_READ_U32_LE(resource + offsets_start +
											   (legacy_u32)index * RESOURCE_FILE_OFFSET_SIZE);
		for (legacy_u16 slot = 0; slot < CAR_ADHESION_CHUNK_COUNT; slot++) {
			if (offset == starts[slot]) {
				aliases[slot]++;
			} else if (offset > starts[slot] && offset < ends[slot]) {
				ends[slot] = offset;
			}
		}
	}
	if (aliases[CAR_ADHESION_CHUNK_FLAG] != 1 ||
		ends[CAR_ADHESION_CHUNK_FLAG] - starts[CAR_ADHESION_CHUNK_FLAG] != CAR_ADHESION_FLAG_SIZE ||
		resource[data_start + starts[CAR_ADHESION_CHUNK_FLAG]] != CAR_ADHESION_FLAG_ENABLED) {
		return;
	}
	if ((found & (1U << CAR_ADHESION_CHUNK_SETTINGS)) != 0) {
		if (aliases[CAR_ADHESION_CHUNK_SETTINGS] != 1 ||
			ends[CAR_ADHESION_CHUNK_SETTINGS] - starts[CAR_ADHESION_CHUNK_SETTINGS] !=
				CAR_ADHESION_SETTINGS_SIZE) {
			return;
		}
		const legacy_u8 huge *settings =
			resource + data_start + starts[CAR_ADHESION_CHUNK_SETTINGS];
		legacy_u16 max_distance =
			LEGACY_READ_U16_LE(settings + CAR_ADHESION_SETTINGS_DISTANCE_OFFSET);
		legacy_u16 min_alignment =
			LEGACY_READ_U16_LE(settings + CAR_ADHESION_SETTINGS_ALIGNMENT_OFFSET);
		legacy_u8 surface_mask = settings[CAR_ADHESION_SETTINGS_SURFACE_OFFSET];
		if (settings[CAR_ADHESION_SETTINGS_VERSION_OFFSET] != CAR_ADHESION_SETTINGS_VERSION ||
			max_distance > CAR_ADHESION_MAX_DISTANCE ||
			min_alignment > CAR_ADHESION_MAX_ALIGNMENT ||
			(surface_mask & ~CAR_ADHESION_SURFACE_MASK) != 0) {
			return;
		}
		car_surface_adhesion_settings[car_index].max_distance = max_distance;
		car_surface_adhesion_settings[car_index].min_alignment = min_alignment;
		car_surface_adhesion_settings[car_index].surface_mask = surface_mask;
	}
	car_surface_adhesion[car_index] = 1;
}

static legacy_s16 aero_resistance_at(legacy_s16 resistance, legacy_s16 speed_index)
{
	legacy_s32 product = LEGACY_S32_WRAP_MUL(resistance, speed_index);
	product = LEGACY_S32_WRAP_MUL(product, speed_index);
	return LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_SAR(product, 9U));
}

void setup_aero_trackdata(void far *carresptr, legacy_s16 is_opponent)
{
	const legacy_u8 far *simd_resource = (const legacy_u8 far *)locate_shape_alt(carresptr, "simd");
	setup_car_surface_adhesion(carresptr, is_opponent);

	if (is_opponent == 0) {
		(void)simd_decode(&simd_player, simd_resource);
		apply_amiga_grass(&simd_player, gameconfig.game_playercarid);
		simd_player.aerorestable = player_aero_resistance_table;
		for (legacy_s16 i = 0; i < AERO_RESISTANCE_TABLE_COUNT; i++) {
			player_aero_resistance_table[i] = aero_resistance_at(simd_player.aero_resistance, i);
		}
		copy_string(gnam_string, locate_shape_alt(carresptr, "gnam"));
	} else {
		(void)simd_decode(&simd_opponent, simd_resource);
		apply_amiga_grass(&simd_opponent, gameconfig.game_opponentcarid);
		simd_opponent.aerorestable = opponent_aero_resistance_table;
		for (legacy_s16 i = 0; i < AERO_RESISTANCE_TABLE_COUNT; i++) {
			opponent_aero_resistance_table[i] =
				aero_resistance_at(simd_opponent.aero_resistance, i);
		}
		copy_string(gsna_string, locate_shape_alt(carresptr, "gsna"));
	}
}

/* The original indexes sped by tile ID, reaching following resource chunks
 * and leftover compressed bytes. Keep all 256 costs in an owned, initialized
 * allocation with the original decompression layout, independent of caches. */
static void far *load_opponent_speed_data(legacy_u8 far **speed_data)
{
	opponent_resource_name[3] = (legacy_s8)((legacy_u8)gameconfig.game_opponenttype + '0');
	void far *resource =
		file_load_resfile_with_tail(opponent_resource_name, OPPONENT_ROUTE_COST_COUNT);
	copy_string(opponent_highscore_name,
				locate_text_res((legacy_s8 far *)resource, opponent_name_text_id));
	(void)locate_shape_alt((legacy_s8 far *)resource, opponent_path_resource_id);
	*speed_data =
		(legacy_u8 far *)locate_shape_alt((legacy_s8 far *)resource, opponent_speed_resource_id);
	for (legacy_u16 index = 0; index < OPPONENT_SPEED_COUNT; index++) {
		oppnentSped[index] = (*speed_data)[index];
	}

	return resource;
}

static legacy_s16 opponent_route_is_terminal(const legacy_u16 *path, legacy_u16 path_count,
											 legacy_u16 track_index, legacy_u16 next_track)
{
	if (next_track == 0 || next_track == LEGACY_U16_MAX) {
		return 1;
	}
	for (legacy_u16 index = 0; index < path_count; index++) {
		if (path[index] == track_index) {
			return 1;
		}
	}
	return 0;
}

void load_opponent_data(void)
{
	legacy_u8 far *speed_data;
	void far *resource = load_opponent_speed_data(&speed_data);

	legacy_u32 best_distance = OPPONENT_ROUTE_DISTANCE_LIMIT;
	legacy_u32 pending_distance[OPPONENT_ROUTE_DISTANCE_CAPACITY];
	legacy_u32 distance = 0;
	legacy_u16 track_index = 0;
	legacy_u16 path_count = 0;
	legacy_u16 pending_count = 0;
	legacy_u16 path[OPPONENT_ROUTE_PATH_CAPACITY];
	legacy_u16 pending_track[OPPONENT_ROUTE_BRANCH_CAPACITY];
	legacy_u16 pending_path_count[OPPONENT_ROUTE_BRANCH_CAPACITY];
	for (;;) {
		legacy_u16 next_track = (legacy_u16)track_primary_route_links[track_index];
		legacy_s16 reaches_finish = next_track == 0;
		legacy_s16 terminal = opponent_route_is_terminal(path, path_count, track_index, next_track);

		path[path_count] = track_index;
		path_count++;
		legacy_u8 speed_index = (legacy_u8)track_route_element_ids[track_index];
		legacy_u8 route_cost = speed_data[speed_index];
		distance += (legacy_u32)route_cost + 1UL;
		if (!terminal) {
			legacy_u16 alternate_track = (legacy_u16)track_alternate_route_links[track_index];
			if (alternate_track != LEGACY_U16_MAX) {
				pending_track[pending_count] = alternate_track;
				pending_path_count[pending_count] = path_count;
				pending_distance[pending_count] = distance;
				pending_count++;
			}
			track_index = next_track;
			continue;
		}

		if (reaches_finish && distance < best_distance) {
			path[path_count] = 0;
			path_count++;
			best_distance = distance;
			for (legacy_u16 index = 0; index < path_count; index++) {
				opponent_route_write(index, path[index]);
			}
			opponent_route_write(path_count, 0U);
			opponent_route_write(LEGACY_U16_WRAP_ADD(path_count, 1U), 1U);
		}
		if (pending_count == 0) {
			break;
		}
		pending_count--;
		track_index = pending_track[pending_count];
		path_count = pending_path_count[pending_count];
		distance = pending_distance[pending_count];
	}
	mmgr_release(resource);
}

#ifdef RESTUNTS_HEADLESS
legacy_s16 setup_player_cars_without_dashboard(void)
{
	setup_legacy_penalty_route_word();

	for (legacy_u16 index = 0; index < CAR_ID_LENGTH; index++) {
		car_resource_name[index + 3U] = gameconfig.game_playercarid[index];
	}
	void far *car_resource = file_load_resfile(car_resource_name);
	setup_aero_trackdata(car_resource, 0);
	unload_resource(car_resource);

	if (gameconfig.game_opponenttype != 0) {
		for (legacy_u16 index = 0; index < CAR_ID_LENGTH; index++) {
			car_resource_name[index + 3U] = gameconfig.game_opponentcarid[index];
		}
		car_resource = file_load_resfile(car_resource_name);
		setup_aero_trackdata(car_resource, 1);
		unload_resource(car_resource);
		load_opponent_data();
	}

	owoot_load_player_wheels();

	/* These GAME resources are collision geometry, not renderer state. */
	load_track_collision_resources();

	followOpponentFlag = 0;
	is_in_replay_copy = -1;
	return 0;
}
#endif
