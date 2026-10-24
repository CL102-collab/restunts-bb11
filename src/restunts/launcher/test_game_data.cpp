#include "game_data.h"
#include "../c/legacy.h"
#include "../c/resource.h"
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/log.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace
{

typedef std::vector<legacy_u8> Bytes;
typedef bool legacy_bool;
static const legacy_u32 COMPRESSION_HEADER_BYTES = 4U;
static const legacy_u32 RLE_FLAGS_OFFSET = COMPRESSION_HEADER_BYTES + LEGACY_DWORD_BYTES;
static const legacy_u32 RLE_ESCAPE = 224U;
static const legacy_u32 SEQUENCE_ESCAPE = RLE_ESCAPE + 1U;
static const legacy_u32 VLE_MAX_WIDTH = 16U;
static const legacy_u32 ALPHABET_SIZE = LEGACY_U8_MAX + 1U;
static const legacy_u32 PARAGRAPH_BYTES = 16U;
static const legacy_u32 WORKSPACE_PARAGRAPHS = 4U;
static const legacy_u32 MAX_RESOURCE_BYTES =
	(LEGACY_S16_MAX - WORKSPACE_PARAGRAPHS) * PARAGRAPH_BYTES;
static const legacy_u32 PADDED_RESOURCE_BYTES = 300U;
static const legacy_u32 RESOURCE_ENTRY_BYTES =
	RESOURCE_FILE_IDENTIFIER_SIZE + RESOURCE_FILE_OFFSET_SIZE;
static const legacy_char VERSION_TEXT[] = "Version 1.1 (Feb 12 1991)";
static legacy_u32 scenarios = 0;

static void require(legacy_bool condition, const legacy_char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void number(Bytes &bytes, legacy_u32 offset, legacy_u32 count, legacy_u32 value)
{
	for (legacy_u32 index = 0; index < count; ++index) {
		bytes[offset + index] = static_cast<legacy_u8>(value >> (index * LEGACY_BYTE_BITS));
	}
}

static Bytes resource(const std::vector<Bytes> &values, const std::vector<wxString> &names)
{
	legacy_u32 count = static_cast<legacy_u32>(values.size());
	legacy_u32 start = RESOURCE_FILE_DIRECTORY_OFFSET + count * RESOURCE_ENTRY_BYTES;
	Bytes result(start);
	number(result, RESOURCE_FILE_COUNT_OFFSET, RESOURCE_FILE_COUNT_SIZE, count);
	for (legacy_u32 index = 0; index < count; ++index) {
		for (legacy_u32 character = 0; character < RESOURCE_FILE_IDENTIFIER_SIZE; ++character) {
			result[RESOURCE_FILE_DIRECTORY_OFFSET + index * RESOURCE_FILE_IDENTIFIER_SIZE +
				   character] = static_cast<legacy_u8>(names[index][character].GetValue());
		}
		number(result,
			   RESOURCE_FILE_DIRECTORY_OFFSET + count * RESOURCE_FILE_IDENTIFIER_SIZE +
				   index * RESOURCE_FILE_OFFSET_SIZE,
			   RESOURCE_FILE_OFFSET_SIZE, static_cast<legacy_u32>(result.size()) - start);
		result.insert(result.end(), values[index].begin(), values[index].end());
	}
	number(result, RESOURCE_FILE_SIZE_OFFSET, RESOURCE_FILE_OFFSET_SIZE,
		   static_cast<legacy_u32>(result.size()));
	return result;
}

static Bytes valid_resource()
{
	return resource({Bytes(VERSION_TEXT, VERSION_TEXT + sizeof(VERSION_TEXT))}, {"gver"});
}

static Bytes padded_resource(legacy_u32 length)
{
	Bytes result = valid_resource();
	result.resize(length);
	number(result, RESOURCE_FILE_SIZE_OFFSET, RESOURCE_FILE_OFFSET_SIZE, length);
	return result;
}

/* Independent fixture encoders exercise the decoder through actual files. */
static Bytes rle(const Bytes &source, legacy_bool sequences = false)
{
	legacy_u32 escapes = sequences ? 2U : 1U;
	Bytes result(RLE_FLAGS_OFFSET + 1U + escapes);
	result[0] = 1U;
	number(result, 1U, COMPRESSION_HEADER_BYTES - 1U, static_cast<legacy_u32>(source.size()));
	result[RLE_FLAGS_OFFSET] =
		static_cast<legacy_u8>(escapes | (sequences ? 0 : LEGACY_U8_SIGN_BIT));
	result[RLE_FLAGS_OFFSET + 1U] = RLE_ESCAPE;
	if (sequences) {
		result[RLE_FLAGS_OFFSET + 2U] = SEQUENCE_ESCAPE;
	}
	legacy_u32 stream_start = static_cast<legacy_u32>(result.size());
	for (legacy_u32 position = 0; position < source.size();) {
		legacy_u32 value = source[position++];
		legacy_u32 repeats = 1U;
		while (sequences && position < source.size() && source[position] == value &&
			   repeats < LEGACY_U8_MAX) {
			++repeats;
			++position;
		}
		if (sequences && repeats > 1U) {
			result.push_back(SEQUENCE_ESCAPE);
			result.push_back(static_cast<legacy_u8>(value));
			result.push_back(SEQUENCE_ESCAPE);
			result.push_back(static_cast<legacy_u8>(repeats));
		} else {
			if (value == RLE_ESCAPE) {
				result.push_back(RLE_ESCAPE);
				result.push_back(1U);
			}
			result.push_back(static_cast<legacy_u8>(value));
		}
	}
	number(result, COMPRESSION_HEADER_BYTES, COMPRESSION_HEADER_BYTES - 1U,
		   static_cast<legacy_u32>(result.size()) - stream_start);
	return result;
}

static Bytes vle(Bytes source, legacy_bool additive, legacy_bool long_codes)
{
	source.push_back(0);
	legacy_u32 present[ALPHABET_SIZE] = {};
	legacy_u32 previous = 0;
	for (legacy_u32 index = 0; index < source.size(); ++index) {
		legacy_u32 value = source[index];
		source[index] = static_cast<legacy_u8>(additive ? value - previous : value);
		previous = value;
		present[source[index]] = 1;
	}
	legacy_u32 symbols = 0;
	for (legacy_u32 value = 0; value < ALPHABET_SIZE; ++value) {
		symbols += present[value];
	}
	legacy_u32 short_width = 1U;
	while ((1U << short_width) < symbols) {
		++short_width;
	}
	legacy_u32 depth = long_codes ? VLE_MAX_WIDTH : short_width;
	legacy_u32 counts[VLE_MAX_WIDTH] = {};
	counts[short_width - 1U] = symbols - (long_codes ? 2U : 0U);
	if (long_codes) {
		counts[depth - 1U] = 2U;
	}
	legacy_u32 codes[ALPHABET_SIZE] = {};
	legacy_u32 widths[ALPHABET_SIZE] = {};
	Bytes result(COMPRESSION_HEADER_BYTES + 1U + depth);
	result[0] = 2U;
	number(result, 1U, COMPRESSION_HEADER_BYTES - 1U, static_cast<legacy_u32>(source.size()) - 1U);
	result[COMPRESSION_HEADER_BYTES] =
		static_cast<legacy_u8>(depth | (additive ? LEGACY_U8_SIGN_BIT : 0U));
	legacy_u32 value = 0;
	legacy_u32 code = 0;
	for (legacy_u32 width = 1U; width <= depth; ++width) {
		result[COMPRESSION_HEADER_BYTES + width] = static_cast<legacy_u8>(counts[width - 1U]);
		for (legacy_u32 index = 0; index < counts[width - 1U]; ++index) {
			while (present[value] == 0) {
				++value;
			}
			result.push_back(static_cast<legacy_u8>(value));
			codes[value] = code++;
			widths[value++] = width;
		}
		code <<= 1U;
	}
	legacy_u32 current = 0;
	legacy_u32 used = 0;
	for (legacy_u8 encoded : source) {
		for (legacy_u32 width = widths[encoded]; width != 0; --width) {
			current = (current << 1U) | ((codes[encoded] >> (width - 1U)) & 1U);
			if (++used == LEGACY_BYTE_BITS) {
				result.push_back(static_cast<legacy_u8>(current));
				used = 0;
			}
		}
	}
	if (used != 0) {
		result.push_back(static_cast<legacy_u8>(current << (LEGACY_BYTE_BITS - used)));
	}
	return result;
}

static Bytes multipass(const Bytes &source, legacy_u32 passes, legacy_u32 length)
{
	Bytes result(COMPRESSION_HEADER_BYTES);
	result[0] = static_cast<legacy_u8>(LEGACY_U8_SIGN_BIT | passes);
	number(result, 1U, COMPRESSION_HEADER_BYTES - 1U, length);
	result.insert(result.end(), source.begin(), source.end());
	return result;
}

static wxString scenario(const wxString &root)
{
	wxString path = wxFileName(root, wxString::Format("scenario-%u", scenarios++)).GetFullPath();
	require(wxFileName::Mkdir(path), "Cannot create test folder.");
	return path;
}

static void put(const wxString &folder, const wxString &name, const Bytes &bytes)
{
	wxFile file(wxFileName(folder, name).GetFullPath(), wxFile::write);
	require(file.IsOpened(), "Cannot write test fixture.");
	require(file.Write(bytes.data(), bytes.size()) == bytes.size(), "Cannot finish test fixture.");
}

static void check(const wxString &folder, legacy_bool accepted)
{
	wxString error = launcher_validate_game_data(folder);
	if (accepted && !error.empty()) {
		std::fprintf(stderr, "%s\n", static_cast<const legacy_char *>(error.utf8_str()));
	}
	require(error.empty() == accepted, "Unexpected game-data validation result.");
	if (!accepted) {
		require(error.Contains("Stunts 1.1 (Feb 12 1991)"), "Error omits supported version.");
	}
}

static void fixture(const wxString &root, const wxString &name, const Bytes &bytes,
					legacy_bool accepted)
{
	wxString directory = scenario(root);
	put(directory, name, bytes);
	check(directory, accepted);
}

static void tests(const wxString &root)
{
	Bytes valid = valid_resource();
	fixture(root, "MISC.RES", valid, true);
	fixture(root, "MiSc.ReS", padded_resource(MAX_RESOURCE_BYTES), true);
	Bytes wrong = valid;
	wrong.back() = 'X';
	fixture(root, "misc.res", wrong, false);
	wrong = valid;
	number(wrong, RESOURCE_FILE_COUNT_OFFSET, RESOURCE_FILE_COUNT_SIZE, LEGACY_U16_MAX);
	fixture(root, "misc.res", wrong, false);
	wrong = valid;
	number(wrong, RESOURCE_FILE_DIRECTORY_OFFSET + RESOURCE_FILE_IDENTIFIER_SIZE,
		   RESOURCE_FILE_OFFSET_SIZE, static_cast<legacy_u32>(valid.size()));
	fixture(root, "misc.res", wrong, false);
	fixture(root, "misc.res",
			resource({Bytes(VERSION_TEXT, VERSION_TEXT + sizeof(VERSION_TEXT)),
					  Bytes(VERSION_TEXT, VERSION_TEXT + sizeof(VERSION_TEXT))},
					 {"gver", "gver"}),
			false);
	fixture(root, "misc.res",
			resource({Bytes(VERSION_TEXT, VERSION_TEXT + sizeof(VERSION_TEXT) - 1U), Bytes(1U)},
					 {"gver", "tail"}),
			false);
	fixture(root, "misc.res", Bytes(MAX_RESOURCE_BYTES + 1U), false);
	check(scenario(root), false);
	check(wxFileName(root, "nonexistent").GetFullPath(), false);
	Bytes packed = rle(valid);
	fixture(root, "MISC.PRE", packed, true);
	fixture(root, "misc.pre", rle(padded_resource(PADDED_RESOURCE_BYTES), true), true);
	for (legacy_u16 additive = 0; additive <= 1U; ++additive) {
		for (legacy_u16 long_codes = 0; long_codes <= 1U; ++long_codes) {
			Bytes coded = vle(valid, additive != 0, long_codes != 0);
			fixture(root, "misc.pre", coded, true);
			fixture(root, "misc.pre",
					multipass(rle(coded), 2U, static_cast<legacy_u32>(valid.size())), true);
			fixture(root, "misc.pre",
					multipass(vle(packed, additive != 0, long_codes != 0), 2U,
							  static_cast<legacy_u32>(valid.size())),
					true);
		}
	}
	fixture(root, "misc.pre",
			multipass(rle(rle(packed)), 3U, static_cast<legacy_u32>(valid.size())), true);
	wxString precedence = scenario(root);
	put(precedence, "misc.pre", packed);
	put(precedence, "misc.res", Bytes(RESOURCE_FILE_DIRECTORY_OFFSET));
	check(precedence, false);
	wxString directory = scenario(root);
	require(wxFileName::Mkdir(wxFileName(directory, "misc.res").GetFullPath()),
			"Cannot create RES directory fixture.");
	put(directory, "misc.pre", packed);
	check(directory, false);
	// Check the actual filesystem: macOS and Windows commonly fold case, while
	// either OS can also expose a case-sensitive test volume.
	wxString aliases = scenario(root);
	put(aliases, "MISC.RES", valid);
	put(aliases, "misc.res", valid);
	wxDir alias_directory(aliases);
	wxString alias_name;
	legacy_u32 alias_count = 0;
	for (legacy_bool more = alias_directory.GetFirst(&alias_name); more;
		 more = alias_directory.GetNext(&alias_name)) {
		if (alias_name.CmpNoCase("misc.res") == 0) {
			++alias_count;
		}
	}
	check(aliases, alias_count == 1U);
	wrong = packed;
	wrong[0] = LEGACY_U8_SIGN_BIT;
	fixture(root, "misc.pre", wrong, false);
	wrong[0] = 3U;
	fixture(root, "misc.pre", wrong, false);
	wrong = packed;
	number(wrong, 1U, COMPRESSION_HEADER_BYTES - 1U, MAX_RESOURCE_BYTES + 1U);
	fixture(root, "misc.pre", wrong, false);
	wrong = packed;
	wrong[RLE_FLAGS_OFFSET] = LEGACY_U8_SIGN_BIT;
	fixture(root, "misc.pre", wrong, false);
	for (legacy_u32 length = 0; length < packed.size(); ++length) {
		fixture(root, "misc.pre", Bytes(packed.begin(), packed.begin() + length), false);
	}
	Bytes coded = vle(valid, false, false);
	for (legacy_u32 length = 0; length < coded.size(); ++length) {
		fixture(root, "misc.pre", Bytes(coded.begin(), coded.begin() + length), false);
	}
	wrong = coded;
	wrong[COMPRESSION_HEADER_BYTES] = 0;
	fixture(root, "misc.pre", wrong, false);
	wrong[COMPRESSION_HEADER_BYTES] = VLE_MAX_WIDTH + 1U;
	fixture(root, "misc.pre", wrong, false);
	wrong = coded;
	wrong[COMPRESSION_HEADER_BYTES + 1U] = LEGACY_U8_MAX;
	fixture(root, "misc.pre", wrong, false);
	wxString unicode = wxFileName(root, wxString::FromUTF8("Árvíztűrő 東京")).GetFullPath();
	require(wxFileName::Mkdir(unicode), "Cannot create Unicode test folder.");
	put(unicode, "MISC.RES", valid);
	check(unicode, true);
}

} // namespace

legacy_int main(legacy_int argc, legacy_char **argv)
{
	wxInitializer initialize;
	if (!initialize.IsOk()) {
		return 1;
	}
	wxLogNull quiet;
	wxString root = wxFileName::CreateTempFileName("restunts-data-");
	if (root.empty() || !wxRemoveFile(root) || !wxFileName::Mkdir(root)) {
		return 1;
	}
	legacy_int result = 0;
	try {
		tests(root);
		if (argc > 1) {
			check(wxString::FromUTF8(argv[1]), true);
		}
		std::printf("Launcher game-data version/Unicode/bounded RLE/VLE/multistage checks passed "
					"(%u scenarios).\n",
					scenarios);
	} catch (const std::exception &error) {
		std::fprintf(stderr, "%s\n", error.what());
		result = 1;
	}
	if (!wxFileName::Rmdir(root, wxPATH_RMDIR_RECURSIVE)) {
		result = 1;
	}
	return result;
}
