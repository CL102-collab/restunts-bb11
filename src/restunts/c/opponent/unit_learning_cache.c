#include "unit_learning_cache.h"

#if defined(RESTUNTS_SDL3)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platform.h"

#define LEARNING_FORMAT_VERSION 1U
#define LEARNING_HEADER_BYTES 48U
#define LEARNING_METADATA_BYTES 96U
#define LEARNING_PIECE_BYTES 36U
#define LEARNING_CHECKSUM_OFFSET 40U
#define LEARNING_CHECKSUM_BYTES 8U
#define LEARNING_MAXIMUM_BYTES                                                                     \
	(LEARNING_HEADER_BYTES + LEARNING_METADATA_BYTES +                                             \
	 UNIT_LEARNING_MAXIMUM_PIECES * LEARNING_PIECE_BYTES +                                         \
	 UNIT_LEARNING_MAXIMUM_COMMANDS * CSRPL_OPPONENT_COMMAND_SIZE)
#define LEARNING_PATH_BYTES 1024U
#define LEARNING_IO_CHUNK 32768U
#define LEARNING_TEMPORARY_ATTEMPTS 256U
#define LEARNING_TEMPORARY_MASK 0xffffffU
#define LEARNING_MAXIMUM_SPEED 255.0
#define LEARNING_MINIMUM_LOOKAHEAD 0.1
#define LEARNING_MAXIMUM_LOOKAHEAD 10.0
#define LEARNING_MAXIMUM_RAMP_APPROACH 4U
#define LEARNING_HASH_OFFSET 14695981039346656037ULL
#define LEARNING_HASH_PRIME 1099511628211ULL
#define LEARNING_SECOND_OFFSET 7809847782465536322ULL
#define LEARNING_SECOND_PRIME 14029467366897019727ULL
#define LEARNING_SECOND_SALT 157U

static const legacy_u8 learning_magic[8] = {'U', 'N', 'I', 'T', 'L', 'R', 'N', 0};
static legacy_s8 learning_directory[LEARNING_PATH_BYTES];
static legacy_u8 learning_directory_override;
static legacy_u32 learning_temporary_serial;

struct LEARNING_STREAM {
	legacy_u8 *bytes;
	legacy_u32 offset;
};

static void learning_write_u32(struct LEARNING_STREAM *stream, legacy_u32 value)
{
	for (legacy_u32 index = 0; index < sizeof(value); index++) {
		stream->bytes[stream->offset++] = (legacy_u8)(value >> (index * LEGACY_BYTE_BITS));
	}
}

static legacy_u32 learning_read_u32(struct LEARNING_STREAM *stream)
{
	legacy_u32 value = 0;
	for (legacy_u32 index = 0; index < sizeof(value); index++) {
		value |= (legacy_u32)stream->bytes[stream->offset++] << (index * LEGACY_BYTE_BITS);
	}
	return value;
}

static void learning_write_u64(struct LEARNING_STREAM *stream, legacy_u64 value)
{
	for (legacy_u32 index = 0; index < sizeof(value); index++) {
		stream->bytes[stream->offset++] = (legacy_u8)(value >> (index * LEGACY_BYTE_BITS));
	}
}

static legacy_u64 learning_read_u64(struct LEARNING_STREAM *stream)
{
	legacy_u64 value = 0;
	for (legacy_u32 index = 0; index < sizeof(value); index++) {
		value |= (legacy_u64)stream->bytes[stream->offset++] << (index * LEGACY_BYTE_BITS);
	}
	return value;
}

static void learning_write_f64(struct LEARNING_STREAM *stream, legacy_f64 value)
{
	legacy_u64 bits;
	memcpy(&bits, &value, sizeof(bits));
	learning_write_u64(stream, bits);
}

static legacy_f64 learning_read_f64(struct LEARNING_STREAM *stream)
{
	legacy_u64 bits = learning_read_u64(stream);
	legacy_f64 value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static void learning_hash_byte(struct UNIT_LEARNING_KEY *key, legacy_u8 byte)
{
	key->first = (key->first ^ byte) * LEARNING_HASH_PRIME;
	key->second = (key->second ^ (byte + LEARNING_SECOND_SALT)) * LEARNING_SECOND_PRIME;
}

void unit_learning_key_begin(struct UNIT_LEARNING_KEY *key)
{
	if (key) {
		key->first = LEARNING_HASH_OFFSET;
		key->second = LEARNING_SECOND_OFFSET;
		unit_learning_key_u32(key, UNIT_LEARNING_ALGORITHM_VERSION);
	}
}

void unit_learning_key_bytes(struct UNIT_LEARNING_KEY *key, const void *bytes, legacy_u32 size)
{
	if (!key || (!bytes && size)) {
		return;
	}
	/* Field lengths prevent adjacent variable-length inputs from aliasing. */
	legacy_u8 length[sizeof(size)];
	struct LEARNING_STREAM stream = {length, 0};
	learning_write_u32(&stream, size);
	for (legacy_u32 index = 0; index < sizeof(length); index++) {
		learning_hash_byte(key, length[index]);
	}
	const legacy_u8 *source = bytes;
	for (legacy_u32 index = 0; index < size; index++) {
		learning_hash_byte(key, source[index]);
	}
}

void unit_learning_key_u32(struct UNIT_LEARNING_KEY *key, legacy_u32 value)
{
	legacy_u8 bytes[sizeof(value)];
	struct LEARNING_STREAM stream = {bytes, 0};
	learning_write_u32(&stream, value);
	unit_learning_key_bytes(key, bytes, sizeof(bytes));
}

void unit_learning_key_f64(struct UNIT_LEARNING_KEY *key, legacy_f64 value)
{
	legacy_u8 bytes[sizeof(value)];
	struct LEARNING_STREAM stream = {bytes, 0};
	/* Equal zeros have the same fingerprint regardless of their sign bit. */
	learning_write_f64(&stream, value == 0.0 ? 0.0 : value);
	unit_learning_key_bytes(key, bytes, sizeof(bytes));
}

static legacy_u64 learning_checksum(const legacy_u8 *bytes, legacy_u32 size)
{
	legacy_u64 checksum = LEARNING_HASH_OFFSET;
	for (legacy_u32 index = 0; index < size; index++) {
		if (index < LEARNING_CHECKSUM_OFFSET ||
			index >= LEARNING_CHECKSUM_OFFSET + LEARNING_CHECKSUM_BYTES) {
			checksum = (checksum ^ bytes[index]) * LEARNING_HASH_PRIME;
		}
	}
	return checksum;
}

static legacy_int learning_profile_valid(legacy_f64 lane, legacy_f64 apex, legacy_f64 lookahead)
{
	return isfinite(lane) && fabs(lane) <= 1.0 && isfinite(apex) && fabs(apex) <= 1.0 &&
		   isfinite(lookahead) && lookahead >= LEARNING_MINIMUM_LOOKAHEAD &&
		   lookahead <= LEARNING_MAXIMUM_LOOKAHEAD;
}

static legacy_int learning_piece_valid(const struct UNIT_LEARNING_PIECE *piece)
{
	/* Recipes are independent hints. A grouped secondary ramp can retain its
	 * approach without a separate speed override; native acceptance also
	 * stores exit fields independently. Fresh physics checks their use. */
	return isfinite(piece->speed) && piece->speed >= 0.0 &&
		   piece->speed <= LEARNING_MAXIMUM_SPEED && isfinite(piece->exit_fraction) &&
		   piece->exit_fraction >= 0.0 && piece->exit_fraction <= 1.0 &&
		   isfinite(piece->exit_speed) && piece->exit_speed >= 0.0 &&
		   piece->exit_speed <= LEARNING_MAXIMUM_SPEED && isfinite(piece->barrel_offset) &&
		   fabs(piece->barrel_offset) <= 1.0 && piece->ramp_approach != 1U &&
		   piece->ramp_approach <= LEARNING_MAXIMUM_RAMP_APPROACH;
}

static legacy_int learning_metadata_valid(const struct UNIT_LEARNING_DATA *data)
{
	if (!data || !data->piece_count || data->piece_count > UNIT_LEARNING_MAXIMUM_PIECES ||
		data->command_count > UNIT_LEARNING_MAXIMUM_COMMANDS ||
		data->best_frames != data->command_count || data->search.ready > 1U ||
		!isfinite(data->search.lane) || !isfinite(data->search.apex) ||
		!isfinite(data->search.lookahead) || !isfinite(data->best_profile.lane) ||
		!isfinite(data->best_profile.apex) || !isfinite(data->best_profile.lookahead) ||
		!isfinite(data->search.score) || data->search.score < 0.0 ||
		data->search.completed_samples > data->search.next_sample) {
		return 0;
	}
	if (data->search.ready &&
		!learning_profile_valid(data->search.lane, data->search.apex, data->search.lookahead)) {
		return 0;
	}
	return !data->command_count ||
		   learning_profile_valid(data->best_profile.lane, data->best_profile.apex,
								  data->best_profile.lookahead);
}

static legacy_int learning_command_valid(const struct CSRPL_OPPONENT_COMMAND *command,
										 legacy_u32 piece_count)
{
	legacy_u8 wire[CSRPL_OPPONENT_COMMAND_SIZE];
	csrpl_opponent_command_write(wire, command);
	return csrpl_opponent_command_valid(wire) && (legacy_u32)command->route_index < piece_count;
}

static legacy_int learning_data_valid(const struct UNIT_LEARNING_DATA *data)
{
	if (!learning_metadata_valid(data) || !data->pieces ||
		(data->command_count && !data->commands)) {
		return 0;
	}
	for (legacy_u32 index = 0; index < data->piece_count; index++) {
		if (!learning_piece_valid(&data->pieces[index])) {
			return 0;
		}
	}
	for (legacy_u32 index = 0; index < data->command_count; index++) {
		if (!learning_command_valid(&data->commands[index], data->piece_count)) {
			return 0;
		}
	}
	return 1;
}

static void learning_write_metadata(struct LEARNING_STREAM *stream,
									const struct UNIT_LEARNING_DATA *data)
{
	learning_write_f64(stream, data->search.lane);
	learning_write_f64(stream, data->search.apex);
	learning_write_f64(stream, data->search.lookahead);
	learning_write_f64(stream, data->search.score);
	learning_write_u64(stream, data->search.next_sample);
	learning_write_u64(stream, data->search.completed_samples);
	learning_write_u32(stream, data->search.seed);
	learning_write_u32(stream, data->search.ready);
	learning_write_f64(stream, data->best_profile.lane);
	learning_write_f64(stream, data->best_profile.apex);
	learning_write_f64(stream, data->best_profile.lookahead);
	learning_write_u64(stream, data->native_samples);
	learning_write_u32(stream, data->best_frames);
	learning_write_u32(stream, data->command_count);
}

static legacy_int learning_read_metadata(struct LEARNING_STREAM *stream,
										 struct UNIT_LEARNING_DATA *data)
{
	data->search.lane = learning_read_f64(stream);
	data->search.apex = learning_read_f64(stream);
	data->search.lookahead = learning_read_f64(stream);
	data->search.score = learning_read_f64(stream);
	data->search.next_sample = learning_read_u64(stream);
	data->search.completed_samples = learning_read_u64(stream);
	data->search.seed = learning_read_u32(stream);
	legacy_u32 ready = learning_read_u32(stream);
	data->search.ready = (legacy_u8)ready;
	data->best_profile.lane = learning_read_f64(stream);
	data->best_profile.apex = learning_read_f64(stream);
	data->best_profile.lookahead = learning_read_f64(stream);
	data->native_samples = learning_read_u64(stream);
	data->best_frames = learning_read_u32(stream);
	data->command_count = learning_read_u32(stream);
	return ready <= 1U && learning_metadata_valid(data);
}

legacy_int unit_learning_cache_encode(const struct UNIT_LEARNING_KEY *key,
									  const struct UNIT_LEARNING_DATA *data, legacy_u8 **bytes,
									  legacy_u32 *size)
{
	if (!key || !bytes || !size || !learning_data_valid(data)) {
		return 0;
	}
	legacy_u32 length = LEARNING_HEADER_BYTES + LEARNING_METADATA_BYTES +
						data->piece_count * LEARNING_PIECE_BYTES +
						data->command_count * CSRPL_OPPONENT_COMMAND_SIZE;
	legacy_u8 *encoded = malloc(length);
	if (!encoded) {
		return 0;
	}
	memcpy(encoded, learning_magic, sizeof(learning_magic));
	struct LEARNING_STREAM stream = {encoded, sizeof(learning_magic)};
	learning_write_u32(&stream, LEARNING_FORMAT_VERSION);
	learning_write_u32(&stream, UNIT_LEARNING_ALGORITHM_VERSION);
	learning_write_u64(&stream, key->first);
	learning_write_u64(&stream, key->second);
	learning_write_u32(&stream, length);
	learning_write_u32(&stream, data->piece_count);
	learning_write_u64(&stream, 0);
	learning_write_metadata(&stream, data);
	for (legacy_u32 index = 0; index < data->piece_count; index++) {
		const struct UNIT_LEARNING_PIECE *piece = &data->pieces[index];
		learning_write_f64(&stream, piece->speed);
		learning_write_f64(&stream, piece->exit_fraction);
		learning_write_f64(&stream, piece->exit_speed);
		learning_write_f64(&stream, piece->barrel_offset);
		learning_write_u32(&stream, piece->ramp_approach);
	}
	for (legacy_u32 index = 0; index < data->command_count; index++) {
		csrpl_opponent_command_write(stream.bytes + stream.offset, &data->commands[index]);
		stream.offset += CSRPL_OPPONENT_COMMAND_SIZE;
	}
	stream.offset = LEARNING_CHECKSUM_OFFSET;
	learning_write_u64(&stream, learning_checksum(encoded, length));
	*bytes = encoded;
	*size = length;
	return 1;
}

legacy_int unit_learning_cache_decode(const struct UNIT_LEARNING_KEY *key, legacy_u32 count,
									  const legacy_u8 *bytes, legacy_u32 size,
									  struct UNIT_LEARNING_DATA *data)
{
	if (!key || !bytes || !data || !count || count > UNIT_LEARNING_MAXIMUM_PIECES ||
		size < LEARNING_HEADER_BYTES + LEARNING_METADATA_BYTES || size > LEARNING_MAXIMUM_BYTES ||
		memcmp(bytes, learning_magic, sizeof(learning_magic))) {
		return 0;
	}
	struct LEARNING_STREAM stream = {(legacy_u8 *)bytes, sizeof(learning_magic)};
	legacy_u32 version = learning_read_u32(&stream);
	legacy_u32 algorithm = learning_read_u32(&stream);
	legacy_u64 first = learning_read_u64(&stream), second = learning_read_u64(&stream);
	legacy_u32 length = learning_read_u32(&stream), pieces = learning_read_u32(&stream);
	legacy_u64 checksum = learning_read_u64(&stream);
	if (version != LEARNING_FORMAT_VERSION || algorithm != UNIT_LEARNING_ALGORITHM_VERSION ||
		first != key->first || second != key->second || length != size || pieces != count ||
		checksum != learning_checksum(bytes, size)) {
		return 0;
	}
	struct UNIT_LEARNING_DATA decoded = {0};
	decoded.piece_count = count;
	if (!learning_read_metadata(&stream, &decoded) ||
		size != LEARNING_HEADER_BYTES + LEARNING_METADATA_BYTES + count * LEARNING_PIECE_BYTES +
					decoded.command_count * CSRPL_OPPONENT_COMMAND_SIZE) {
		return 0;
	}
	decoded.pieces = calloc(count, sizeof(*decoded.pieces));
	decoded.commands =
		decoded.command_count ? calloc(decoded.command_count, sizeof(*decoded.commands)) : NULL;
	if (!decoded.pieces || (decoded.command_count && !decoded.commands)) {
		unit_learning_cache_release(&decoded);
		return 0;
	}
	for (legacy_u32 index = 0; index < count; index++) {
		struct UNIT_LEARNING_PIECE *piece = &decoded.pieces[index];
		piece->speed = learning_read_f64(&stream);
		piece->exit_fraction = learning_read_f64(&stream);
		piece->exit_speed = learning_read_f64(&stream);
		piece->barrel_offset = learning_read_f64(&stream);
		legacy_u32 ramp = learning_read_u32(&stream);
		piece->ramp_approach = (legacy_u8)ramp;
		if (ramp > LEARNING_MAXIMUM_RAMP_APPROACH || !learning_piece_valid(piece)) {
			unit_learning_cache_release(&decoded);
			return 0;
		}
	}
	for (legacy_u32 index = 0; index < decoded.command_count; index++) {
		csrpl_opponent_command_read(stream.bytes + stream.offset, &decoded.commands[index]);
		stream.offset += CSRPL_OPPONENT_COMMAND_SIZE;
		if (!learning_command_valid(&decoded.commands[index], count)) {
			unit_learning_cache_release(&decoded);
			return 0;
		}
	}
	*data = decoded;
	return 1;
}

void unit_learning_cache_release(struct UNIT_LEARNING_DATA *data)
{
	if (data) {
		free(data->pieces);
		free(data->commands);
		memset(data, 0, sizeof(*data));
	}
}

legacy_int unit_learning_cache_set_directory(const legacy_s8 *directory)
{
	learning_directory_override = directory != NULL;
	learning_directory[0] = 0;
	if (directory) {
		legacy_u32 length = 0;
		while (length < sizeof(learning_directory) && directory[length]) {
			length++;
		}
		if (length >= sizeof(learning_directory)) {
			return 0;
		}
		memcpy(learning_directory, directory, length + 1U);
	}
	return 1;
}

legacy_int unit_learning_cache_enabled(void)
{
	if (learning_directory_override) {
		return learning_directory[0] != 0;
	}
#if defined(RESTUNTS_HEADLESS)
	return 0;
#else
	return 1;
#endif
}

static legacy_int learning_path(const struct UNIT_LEARNING_KEY *key,
								legacy_s8 path[LEARNING_PATH_BYTES])
{
	if (!key || !unit_learning_cache_enabled()) {
		return 0;
	}
	const legacy_char *directory =
		learning_directory_override ? (const legacy_char *)learning_directory : ".";
	legacy_int length = snprintf(
		(legacy_char *)path, LEARNING_PATH_BYTES,
		"%s/U%08" LEGACY_PRIx32 "%08" LEGACY_PRIx32 "%08" LEGACY_PRIx32 "%08" LEGACY_PRIx32 ".ULC",
		directory, (legacy_u32)(key->first >> LEGACY_DWORD_BITS), (legacy_u32)key->first,
		(legacy_u32)(key->second >> LEGACY_DWORD_BITS), (legacy_u32)key->second);
	return length > 0 && (legacy_u32)length < LEARNING_PATH_BYTES;
}

legacy_int unit_learning_cache_load(const struct UNIT_LEARNING_KEY *key, legacy_u32 count,
									struct UNIT_LEARNING_DATA *data)
{
	legacy_s8 path[LEARNING_PATH_BYTES];
	if (!data || !count || count > UNIT_LEARNING_MAXIMUM_PIECES || !learning_path(key, path)) {
		return 0;
	}
	legacy_u16 handle = dos_file_open(path, DOS_FILE_OPEN_EXISTING);
	if (!handle) {
		return 0;
	}
	legacy_s32 length =
		dos_file_seek(handle, 0, DOS_FILE_SEEK_END) == 0 ? dos_file_tell(handle) : -1;
	if (length < (legacy_s32)(LEARNING_HEADER_BYTES + LEARNING_METADATA_BYTES) ||
		length > (legacy_s32)LEARNING_MAXIMUM_BYTES ||
		dos_file_seek(handle, 0, DOS_FILE_SEEK_BEGIN) != 0) {
		(void)dos_file_close(handle);
		return 0;
	}
	legacy_u8 *bytes = malloc((legacy_u32)length);
	legacy_u32 offset = 0;
	while (bytes && offset < (legacy_u32)length) {
		legacy_u16 chunk = (legacy_u16)((legacy_u32)length - offset > LEARNING_IO_CHUNK
											? LEARNING_IO_CHUNK
											: (legacy_u32)length - offset);
		if (dos_file_read(handle, bytes + offset, chunk) != chunk) {
			break;
		}
		offset += chunk;
	}
	legacy_int closed = dos_file_close(handle) == 0;
	legacy_int loaded = bytes && offset == (legacy_u32)length && closed &&
						unit_learning_cache_decode(key, count, bytes, (legacy_u32)length, data);
	free(bytes);
	return loaded;
}

legacy_int unit_learning_cache_save(const struct UNIT_LEARNING_KEY *key,
									const struct UNIT_LEARNING_DATA *data)
{
	legacy_s8 path[LEARNING_PATH_BYTES];
	if (!learning_path(key, path)) {
		return 0;
	}
	legacy_u8 *bytes = NULL;
	legacy_u32 size = 0;
	if (!unit_learning_cache_encode(key, data, &bytes, &size)) {
		return 0;
	}
	legacy_s8 temporary[LEARNING_PATH_BYTES];
	legacy_u32 prefix = 0;
	for (legacy_u32 index = 0; path[index]; index++) {
		if (path[index] == '/' || path[index] == '\\' || path[index] == ':') {
			prefix = index + 1U;
		}
	}
	memcpy(temporary, path, prefix);
	legacy_u16 handle = 0;
	for (legacy_u32 attempt = 0; attempt < LEARNING_TEMPORARY_ATTEMPTS; attempt++) {
		legacy_int written = snprintf((legacy_char *)temporary + prefix, sizeof(temporary) - prefix,
									  "_U%06" LEGACY_PRIX32 ".tmp",
									  ++learning_temporary_serial & LEARNING_TEMPORARY_MASK);
		if (written < 0 || (legacy_u32)written >= sizeof(temporary) - prefix) {
			break;
		}
		legacy_s16 exists = sdl3_file_exists(temporary);
		if (exists < 0) {
			break;
		}
		if (!exists) {
			handle = dos_file_open(temporary, DOS_FILE_CREATE);
			break;
		}
	}
	if (!handle) {
		free(bytes);
		return 0;
	}
	legacy_u32 offset = 0;
	while (offset < size) {
		legacy_u16 chunk =
			(legacy_u16)(size - offset > LEARNING_IO_CHUNK ? LEARNING_IO_CHUNK : size - offset);
		if (dos_file_write(handle, bytes + offset, chunk) != chunk) {
			break;
		}
		offset += chunk;
	}
	legacy_int closed = dos_file_close(handle) == 0;
	free(bytes);
	legacy_int saved = offset == size && closed && sdl3_file_replace(temporary, path) == 0;
	if (!saved) {
		(void)dos_file_remove(temporary);
	}
	return saved;
}
#endif
