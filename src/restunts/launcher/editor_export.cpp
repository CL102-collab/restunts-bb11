#include "editor_export.h"
#include "../c/legacy.h"
#include <wx/file.h>
#include <new>
#include <vector>

static const legacy_char EXPORT_APPLICATION_DIRECTORY[] = "Chocolate Stunts";
static const legacy_char EXPORT_EDITOR_DIRECTORY[] = "Track Editor";
static const legacy_char EXPORT_EDITOR_FILENAME[] = "tracked.html";
typedef std::vector<legacy_u8> EditorBytes;

static legacy_int editor_read_bytes(const wxFileName &path, EditorBytes &bytes)
{
	wxFile input(path.GetFullPath(), wxFile::read);
	if (!input.IsOpened()) {
		return 0;
	}
	const auto length = input.Length();
	if (length <= 0 || static_cast<legacy_u64>(length) > bytes.max_size()) {
		return 0;
	}
	try {
		bytes.resize(static_cast<size_t>(length));
	} catch (const std::bad_alloc &) {
		return 0;
	}
	const auto count = input.Read(bytes.data(), bytes.size());
	return count >= 0 && static_cast<size_t>(count) == bytes.size() && input.Close();
}

wxString launcher_export_editor(const wxFileName &source, const wxString &downloads,
								wxFileName &exported)
{
	wxFileName directory = wxFileName::DirName(downloads);
	if (downloads.empty() || !directory.IsAbsolute() ||
		!directory.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE)) {
		return "Cannot find an absolute Downloads folder for the track editor.";
	}
	EditorBytes payload;
	if (!editor_read_bytes(source, payload)) {
		return "Cannot read the packaged track editor: " + source.GetFullPath();
	}
	directory.AppendDir(EXPORT_APPLICATION_DIRECTORY);
	directory.AppendDir(EXPORT_EDITOR_DIRECTORY);
	wxFileName destination(directory.GetPath(), EXPORT_EDITOR_FILENAME);
	EditorBytes existing;
	if (editor_read_bytes(destination, existing) && existing == payload) {
		exported = destination;
		return wxString();
	}
	if (!directory.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
		return "Cannot create the track editor folder: " + directory.GetPath();
	}
	wxTempFile output(destination.GetFullPath());
	if (!output.IsOpened()) {
		return "Cannot create the track editor copy: " + destination.GetFullPath();
	}
	if (!output.Write(payload.data(), payload.size()) || !output.Flush()) {
		return "Cannot write the track editor copy: " + destination.GetFullPath();
	}
	if (!output.Commit()) {
		output.Discard();
		return "Cannot update the track editor copy: " + destination.GetFullPath();
	}
	exported = destination;
	return wxString();
}
