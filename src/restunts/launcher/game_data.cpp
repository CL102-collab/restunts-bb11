#include "game_data.h"
#include "../c/legacy.h"
#include "../c/resource.h"
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <algorithm>
#include <new>
#include <utility>
#include <vector>

namespace
{

typedef std::vector<legacy_u8> Bytes;
static const legacy_u32 PARAGRAPH_BYTES = 16U;
static const legacy_u32 WORKSPACE_PARAGRAPHS = 4U;
static const legacy_u32 MAX_RESOURCE_BYTES =
	(LEGACY_S16_MAX - WORKSPACE_PARAGRAPHS) * PARAGRAPH_BYTES;
static const legacy_u32 COMPRESSION_HEADER_BYTES = 4U;
static const legacy_u32 COMPRESSION_SIZE_OFFSET = 1U;
static const legacy_u32 RLE_SOURCE_SIZE_OFFSET = COMPRESSION_HEADER_BYTES;
static const legacy_u32 RLE_FLAGS_OFFSET = COMPRESSION_HEADER_BYTES + LEGACY_DWORD_BYTES;
static const legacy_u32 RLE_ESCAPES_OFFSET = RLE_FLAGS_OFFSET + 1U;
static const legacy_u32 RLE_SEQUENCE_ESCAPE_INDEX = 1U;
static const legacy_u32 RLE_BYTE_COUNT_CODE = 1U;
static const legacy_u32 RLE_WORD_COUNT_CODE = 3U;
static const legacy_u32 VLE_FLAGS_OFFSET = COMPRESSION_HEADER_BYTES;
static const legacy_u32 VLE_COUNTS_OFFSET = VLE_FLAGS_OFFSET + 1U;
static const legacy_u32 VLE_MAX_WIDTH = 16U;
static const legacy_u32 ALPHABET_SIZE = LEGACY_U8_MAX + 1U;
static const legacy_u32 RLE_TYPE = 1U;
static const legacy_u32 VLE_TYPE = 2U;
static const legacy_char VERSION_IDENTIFIER[] = "gver";
static const legacy_char EXPECTED_VERSION[] = "Version 1.1 (Feb 12 1991)";
static const legacy_char INCOMPATIBLE[] =
	"Incompatible game data. Use Broderbund Stunts 1.1 (Feb 12 1991). ";

struct DataError {
	const legacy_char *message;
};

static void require(const Bytes &input, legacy_u32 offset, legacy_u32 count)
{
	if (offset > input.size() || count > input.size() - offset) {
		throw DataError{"Truncated resource data."};
	}
}

static legacy_u32 byte(const Bytes &input, legacy_u32 offset)
{
	require(input, offset, 1U);
	return input[offset];
}

static legacy_u32 number(const Bytes &input, legacy_u32 offset, legacy_u32 count)
{
	require(input, offset, count);
	legacy_u32 value = 0;
	for (legacy_u32 index = 0; index < count; ++index) {
		value |= static_cast<legacy_u32>(input[offset + index]) << (index * LEGACY_BYTE_BITS);
	}
	return value;
}

static legacy_u32 paragraphs(legacy_u32 length)
{
	return (length + PARAGRAPH_BYTES - 1U) / PARAGRAPH_BYTES;
}

static Bytes sequence(const Bytes &input, legacy_u32 start, legacy_u32 length, legacy_u32 escape,
					  legacy_u32 capacity)
{
	require(input, start, length);
	Bytes output;
	const legacy_u32 end = start + length;
	for (legacy_u32 position = start; position < end;) {
		legacy_u32 current = input[position++];
		if (current != escape) {
			if (output.size() == capacity) {
				throw DataError{"RLE sequence exceeds the native workspace."};
			}
			output.push_back(static_cast<legacy_u8>(current));
			continue;
		}
		legacy_u32 begin = position;
		while (position < end && input[position] != escape) {
			++position;
		}
		if (position + 1U >= end) {
			throw DataError{"Unterminated RLE sequence."};
		}
		legacy_u32 repetitions = input[position + 1U];
		if (repetitions == 0) {
			repetitions = ALPHABET_SIZE;
		}
		legacy_u32 run_length = position - begin;
		if (run_length * repetitions > capacity - output.size()) {
			throw DataError{"RLE sequence exceeds the native workspace."};
		}
		for (legacy_u32 repeat = 0; repeat < repetitions; ++repeat) {
			output.insert(output.end(), input.begin() + begin, input.begin() + position);
		}
		position += LEGACY_WORD_BYTES;
	}
	return output;
}

/* A bounded equivalent of the original DOS codecs, following the Android
 * filesystem validator. Preserve run overshoot, the strict >128 sequence flag,
 * VLE's extra output byte, and paragraph-rounded intermediate relocation. The
 * engine codecs require DOS-memory/mmgr globals and cannot safely probe files
 * before the game starts. */
static Bytes rle(Bytes input, legacy_u32 length, legacy_u32 capacity)
{
	legacy_u32 flags = byte(input, RLE_FLAGS_OFFSET);
	legacy_u32 escape_count = flags & (LEGACY_U8_SIGN_BIT - 1U);
	require(input, RLE_ESCAPES_OFFSET, escape_count);
	legacy_u32 lookup[ALPHABET_SIZE] = {};
	for (legacy_u32 index = 0; index < escape_count; ++index) {
		lookup[input[RLE_ESCAPES_OFFSET + index]] = index + 1U;
	}
	legacy_u32 position = RLE_ESCAPES_OFFSET + escape_count;
	if (flags <= LEGACY_U8_SIGN_BIT) {
		input = sequence(input, position,
						 number(input, RLE_SOURCE_SIZE_OFFSET, COMPRESSION_HEADER_BYTES - 1U),
						 byte(input, RLE_ESCAPES_OFFSET + RLE_SEQUENCE_ESCAPE_INDEX), capacity);
		position = 0;
	}
	Bytes output;
	while (output.size() < length) {
		legacy_u32 value = byte(input, position++);
		legacy_u32 code = lookup[value];
		legacy_u32 count = 1U;
		if (code != 0) {
			if (code == RLE_BYTE_COUNT_CODE) {
				count = byte(input, position++);
			} else if (code == RLE_WORD_COUNT_CODE) {
				count = number(input, position, LEGACY_WORD_BYTES);
				position += LEGACY_WORD_BYTES;
			} else {
				count = code - 1U;
			}
			value = byte(input, position++);
		}
		if (count > capacity - output.size()) {
			throw DataError{"RLE byte output exceeds the native workspace."};
		}
		output.insert(output.end(), count, static_cast<legacy_u8>(value));
	}
	return output;
}

static Bytes vle(const Bytes &input, legacy_u32 length, legacy_u32 capacity)
{
	legacy_u32 flags = byte(input, VLE_FLAGS_OFFSET);
	legacy_u32 depth = flags & (LEGACY_U8_SIGN_BIT - 1U);
	if (depth == 0 || depth > VLE_MAX_WIDTH || length >= capacity) {
		throw DataError{"Invalid VLE width or output size."};
	}
	require(input, VLE_COUNTS_OFFSET, depth);
	legacy_u32 starts[VLE_MAX_WIDTH] = {};
	legacy_u32 offsets[VLE_MAX_WIDTH] = {};
	legacy_u32 alphabet_length = 0;
	legacy_u32 code = 0;
	for (legacy_u32 width = 0; width < depth; ++width) {
		starts[width] = code;
		offsets[width] = alphabet_length;
		legacy_u32 count = input[VLE_COUNTS_OFFSET + width];
		code += count;
		alphabet_length += count;
		if (code > (1U << (width + 1U)) || alphabet_length > ALPHABET_SIZE) {
			throw DataError{"Oversubscribed VLE alphabet."};
		}
		code <<= 1U;
	}
	if (alphabet_length == 0) {
		throw DataError{"Empty VLE alphabet."};
	}
	legacy_u32 alphabet_start = VLE_COUNTS_OFFSET + depth;
	require(input, alphabet_start, alphabet_length);
	legacy_u32 bit_position = (alphabet_start + alphabet_length) * LEGACY_BYTE_BITS;
	Bytes output(length + 1U);
	legacy_u32 previous = 0;
	for (legacy_u32 index = 0; index < output.size(); ++index) {
		code = 0;
		legacy_u16 found = 0;
		for (legacy_u32 width = 0; width < depth; ++width) {
			legacy_u32 next = byte(input, bit_position / LEGACY_BYTE_BITS);
			next = (next >> (LEGACY_BYTE_BITS - 1U - bit_position % LEGACY_BYTE_BITS)) & 1U;
			++bit_position;
			code = (code << 1U) | next;
			legacy_u32 count = input[VLE_COUNTS_OFFSET + width];
			if (code >= starts[width] && code - starts[width] < count) {
				legacy_u32 value = input[alphabet_start + offsets[width] + code - starts[width]];
				previous =
					(flags & LEGACY_U8_SIGN_BIT) == 0 ? value : (previous + value) & LEGACY_U8_MAX;
				output[index] = static_cast<legacy_u8>(previous);
				found = 1;
				break;
			}
		}
		if (found == 0) {
			throw DataError{"Invalid VLE prefix code."};
		}
	}
	return output;
}

static Bytes unpack(Bytes input, legacy_u32 final_length)
{
	legacy_u32 capacity = (paragraphs(final_length) + WORKSPACE_PARAGRAPHS) * PARAGRAPH_BYTES;
	legacy_u32 passes = 1U;
	legacy_u32 type = byte(input, 0);
	if ((type & LEGACY_U8_SIGN_BIT) != 0) {
		passes = type & (LEGACY_U8_SIGN_BIT - 1U);
		if (passes == 0) {
			throw DataError{"No compression passes were declared."};
		}
		require(input, 0, COMPRESSION_HEADER_BYTES);
		input.erase(input.begin(), input.begin() + COMPRESSION_HEADER_BYTES);
	}
	for (legacy_u32 pass = 0; pass < passes; ++pass) {
		legacy_u32 length = number(input, COMPRESSION_SIZE_OFFSET, COMPRESSION_HEADER_BYTES - 1U);
		if (length > capacity) {
			throw DataError{"Compression output exceeds the native workspace."};
		}
		type = byte(input, 0);
		Bytes decoded;
		if (type == RLE_TYPE) {
			decoded = rle(std::move(input), length, capacity);
		} else if (type == VLE_TYPE) {
			decoded = vle(input, length, capacity);
		} else {
			throw DataError{"Unknown compression type."};
		}
		if (pass + 1U == passes) {
			return decoded;
		}
		decoded.resize(std::min(static_cast<legacy_u32>(decoded.size()),
								paragraphs(length) * PARAGRAPH_BYTES));
		input.swap(decoded);
	}
	throw DataError{"No decompressed resource was produced."};
}

static void check_version(const Bytes &input, legacy_u32 length)
{
	require(input, 0, length);
	legacy_u32 declared = number(input, RESOURCE_FILE_SIZE_OFFSET, RESOURCE_FILE_OFFSET_SIZE);
	if (declared < RESOURCE_FILE_DIRECTORY_OFFSET || declared > length) {
		throw DataError{"Invalid resource directory size."};
	}
	legacy_u32 count = number(input, RESOURCE_FILE_COUNT_OFFSET, RESOURCE_FILE_COUNT_SIZE);
	const legacy_u32 entry_bytes = RESOURCE_FILE_IDENTIFIER_SIZE + RESOURCE_FILE_OFFSET_SIZE;
	if (count > (declared - RESOURCE_FILE_DIRECTORY_OFFSET) / entry_bytes) {
		throw DataError{"Truncated resource directory."};
	}
	legacy_u32 offsets = RESOURCE_FILE_DIRECTORY_OFFSET + count * RESOURCE_FILE_IDENTIFIER_SIZE;
	legacy_u32 data = RESOURCE_FILE_DIRECTORY_OFFSET + count * entry_bytes;
	legacy_u32 data_length = declared - data;
	legacy_u32 start = 0;
	legacy_u16 found = 0;
	for (legacy_u32 index = 0; index < count; ++index) {
		legacy_u32 offset =
			number(input, offsets + index * RESOURCE_FILE_OFFSET_SIZE, RESOURCE_FILE_OFFSET_SIZE);
		if (offset > data_length) {
			throw DataError{"Resource offset escapes its payload."};
		}
		legacy_u32 identifier =
			RESOURCE_FILE_DIRECTORY_OFFSET + index * RESOURCE_FILE_IDENTIFIER_SIZE;
		if (std::equal(input.begin() + identifier,
					   input.begin() + identifier + RESOURCE_FILE_IDENTIFIER_SIZE,
					   VERSION_IDENTIFIER)) {
			if (found != 0) {
				throw DataError{"Duplicate gver resource."};
			}
			found = 1;
			start = offset;
		}
	}
	if (found == 0) {
		throw DataError{"Missing gver resource."};
	}
	legacy_u32 end = data_length;
	for (legacy_u32 index = 0; index < count; ++index) {
		legacy_u32 offset =
			number(input, offsets + index * RESOURCE_FILE_OFFSET_SIZE, RESOURCE_FILE_OFFSET_SIZE);
		if (offset > start && offset < end) {
			end = offset;
		}
	}
	if (end - start < sizeof(EXPECTED_VERSION) ||
		!std::equal(input.begin() + data + start,
					input.begin() + data + start + sizeof(EXPECTED_VERSION), EXPECTED_VERSION)) {
		throw DataError{"Unsupported or truncated gver version text."};
	}
}

static wxString find_resource(const wxString &folder, const wxString &name)
{
	wxDir directory(folder);
	if (!directory.IsOpened()) {
		throw DataError{"Cannot read the game directory."};
	}
	wxString filename;
	wxString found;
	for (legacy_u16 more =
			 directory.GetFirst(&filename, wxEmptyString, wxDIR_FILES | wxDIR_DIRS | wxDIR_HIDDEN);
		 more != 0; more = directory.GetNext(&filename)) {
		if (filename.CmpNoCase(name) == 0) {
			if (!found.empty()) {
				throw DataError{"Ambiguous MISC resource filename."};
			}
			found = wxFileName(folder, filename).GetFullPath();
		}
	}
	return found;
}

static Bytes read_resource(const wxString &path)
{
	wxFile file;
	if (!wxFileName::FileExists(path) || !file.Open(path)) {
		throw DataError{"Cannot read the MISC resource file."};
	}
	wxFileOffset length = file.Length();
	if (length < RESOURCE_FILE_DIRECTORY_OFFSET || length > MAX_RESOURCE_BYTES) {
		throw DataError{"Invalid MISC resource length."};
	}
	Bytes contents(static_cast<legacy_u32>(length));
	legacy_u32 position = 0;
	while (position < contents.size()) {
		wxFileOffset count = file.Read(contents.data() + position, contents.size() - position);
		if (count <= 0) {
			throw DataError{"Truncated MISC resource."};
		}
		position += static_cast<legacy_u32>(count);
	}
	legacy_u8 extra;
	if (file.Read(&extra, sizeof(extra)) != 0) {
		throw DataError{"MISC resource changed while it was read."};
	}
	return contents;
}

} // namespace

wxString launcher_validate_game_data(const wxString &folder)
{
	wxLogNull quiet;
	try {
		wxString path = find_resource(folder, "misc.res");
		legacy_u16 packed = path.empty();
		if (packed != 0) {
			path = find_resource(folder, "misc.pre");
		}
		if (path.empty()) {
			throw DataError{"MISC.RES/MISC.PRE is missing."};
		}
		Bytes contents = read_resource(path);
		legacy_u32 length = static_cast<legacy_u32>(contents.size());
		if (packed != 0) {
			length = number(contents, COMPRESSION_SIZE_OFFSET, COMPRESSION_HEADER_BYTES - 1U);
			if (length < RESOURCE_FILE_DIRECTORY_OFFSET || length > MAX_RESOURCE_BYTES ||
				paragraphs(static_cast<legacy_u32>(contents.size())) >
					paragraphs(length) + WORKSPACE_PARAGRAPHS) {
				throw DataError{"Invalid packed resource size."};
			}
			contents = unpack(std::move(contents), length);
		}
		check_version(contents, length);
		return wxEmptyString;
	} catch (const DataError &error) {
		return wxString(INCOMPATIBLE) + error.message;
	} catch (const std::bad_alloc &) {
		return wxString(INCOMPATIBLE) + "Not enough memory to check the MISC resource.";
	}
}
