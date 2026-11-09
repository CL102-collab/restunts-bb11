#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/log.h>
#include "extended_options.h"
#include "options_internal.h"
extern "C" {
#include "../c/extended/csrpl.h"
}
#include "../genghost/simulation.h"

namespace
{

static const size_t EXTENDED_TRACK_MAX_BYTES =
	CSTRK_HEADER_SIZE + (CSTRK_MAX_DIMENSION + 1U) * (CSTRK_MAX_DIMENSION + 1U) +
	CSTRK_MAX_DIMENSION * CSTRK_MAX_DIMENSION * 2U + CSTRK_MAX_OBJECTS * CSTRK_OBJECT_SIZE +
	CSTRK_MAX_NOTES + CSTRK_CUSTOM_HORIZON_TRAILER_MAX_SIZE;
static const size_t EXTENDED_REPLAY_MAX_BYTES =
	CSRPL_HEADER_SIZE + EXTENDED_TRACK_MAX_BYTES + GENGHOST_MAX_FRAMES;

static legacy_bool read_extended(const wxString &path, std::vector<legacy_u8> &bytes)
{
	wxLogNull silence;
	wxFFile file(path, "rb");
	if (!file.IsOpened() || file.Length() <= 0 ||
		static_cast<legacy_u64>(file.Length()) > EXTENDED_REPLAY_MAX_BYTES) {
		return false;
	}
	bytes.resize(static_cast<size_t>(file.Length()));
	return file.Read(bytes.data(), bytes.size()) == bytes.size();
}

static legacy_bool read_extended_replay(const wxString &path, std::vector<legacy_u8> &bytes,
										CSRPL_VIEW &view)
{
	return read_extended(path, bytes) &&
		   csrpl_decode(bytes.data(), bytes.size(), &view, nullptr, 0) &&
		   view.metadata.frame_count <= GENGHOST_MAX_FRAMES;
}

} // namespace

legacy_bool extended_genghost_input(const wxString &path)
{
	const wxString extension = wxFileName(path).GetExt().Upper();
	return extension == "CSTRK" || extension == "CSRPL";
}

legacy_bool extended_genghost_replay_car(const wxString &path, wxString &car)
{
	if (wxFileName(path).GetExt().CmpNoCase("csrpl") != 0) {
		return false;
	}
	std::vector<legacy_u8> bytes;
	CSRPL_VIEW view;
	car = read_extended_replay(path, bytes, view)
			  ? genghost_gui_header_car(view.metadata.player_car_id)
			  : wxString();
	return true;
}

legacy_bool extended_genghost_validate_input(const wxString &path, legacy_bool fine_tune,
											 legacy_bool &valid)
{
	const wxString extension = wxFileName(path).GetExt().Upper();
	if (extension == "CSRPL") {
		std::vector<legacy_u8> bytes;
		CSRPL_VIEW view;
		valid =
			read_extended_replay(path, bytes, view) &&
			(!fine_tune || (view.metadata.frame_count != 0 && view.metadata.opponent_type == 0 &&
							!genghost_gui_header_car(view.metadata.player_car_id).empty()));
		return true;
	}
	if (extension == "CSTRK") {
		std::vector<legacy_u8> bytes;
		valid = !fine_tune && read_extended(path, bytes) &&
				cstrk_validate_bytes(bytes.data(), bytes.size(), nullptr, 0);
		return true;
	}
	return false;
}

wxString extended_genghost_output_extension(const wxString &path)
{
	return extended_genghost_input(path) ? "csrpl" : "rpl";
}

legacy_bool extended_genghost_apply_replay_physics(GenghostOptions &options)
{
	const wxString input = genghost_resolved_input(options);
	if (wxFileName(input).GetExt().CmpNoCase("csrpl") != 0) {
		return false;
	}
	std::vector<legacy_u8> bytes;
	CSRPL_VIEW view;
	if (!read_extended_replay(input, bytes, view)) {
		return false;
	}
	static const legacy_u16 rules[GENGHOST_PHYSICS_COUNT] = {DRIVING_RULE_POWER_GEAR,
															 DRIVING_RULE_LEGACY_COLLISION,
															 DRIVING_RULE_LEFT_CORNER_BIAS,
															 DRIVING_RULE_DUAL_WAY_SWITCHING,
															 DRIVING_RULE_CHICANE_FAST_GRASS,
															 DRIVING_RULE_AMIGA_GRASS,
															 DRIVING_RULE_OWOOT};
	for (legacy_uint index = 0; index < GENGHOST_PHYSICS_COUNT; index++) {
		options.physics[index] = (view.metadata.driving_rules & rules[index]) != 0;
	}
	return true;
}
