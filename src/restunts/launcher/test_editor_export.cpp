#include "editor_export.h"
#include "../c/legacy.h"
#include <wx/datetime.h>
#include <wx/ffile.h>
#include <wx/file.h>
#include <wx/init.h>
#include <wx/log.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#ifdef __UNIX__
#include <sys/stat.h>
#endif

static const std::string FIRST_PAYLOAD = "<!doctype html>\r\n<html>First \xc3\xa9 snow</html>\r\n";
static const std::string SECOND_PAYLOAD =
	"<!doctype html>\n<html>Updated editor and Antarctica</html>\n";

static void require(legacy_int success, const legacy_char *message)
{
	if (!success) {
		throw std::runtime_error(message);
	}
}

static void write_bytes(const wxFileName &path, const std::string &bytes)
{
	wxFFile file(path.GetFullPath(), "wb");
	require(file.IsOpened() && file.Write(bytes.data(), bytes.size()) == bytes.size() &&
				file.Close(),
			"Cannot write editor test fixture");
}

static std::string read_bytes(const wxFileName &path)
{
	wxFFile file(path.GetFullPath(), "rb");
	require(file.IsOpened() && file.Length() >= 0, "Cannot open exported editor");
	std::string result(static_cast<size_t>(file.Length()), '\0');
	require(file.Read(&result[0], result.size()) == result.size(), "Cannot read exported editor");
	return result;
}

static void scenarios(const wxString &root)
{
	const wxFileName source(root, "packaged.html");
	wxFileName downloads = wxFileName::DirName(root);
	downloads.AppendDir(wxString::FromUTF8("Downloads with spaces \xc3\xa9"));
	downloads.AppendDir("Not yet created");
	write_bytes(source, FIRST_PAYLOAD);
	wxFileName exported;
	require(launcher_export_editor(source, downloads.GetPath(), exported).empty(),
			"Cannot export editor into missing Downloads directories");
	require(exported.GetFullName() == "tracked.html" &&
				exported.GetPath().EndsWith("Chocolate Stunts" + wxString(wxFILE_SEP_PATH) +
											"Track Editor"),
			"Exported editor path changed");
	require(read_bytes(exported) == FIRST_PAYLOAD, "Editor bytes were changed during export");

	const wxDateTime old_time(4, wxDateTime::Jan, 2001, 1, 2, 4);
	require(exported.SetTimes(nullptr, &old_time, nullptr), "Cannot set fixture modification time");
	wxFileName repeated;
	require(launcher_export_editor(source, downloads.GetPath(), repeated).empty() &&
				repeated.GetFullPath() == exported.GetFullPath(),
			"Editor URL was not stable");
	require(repeated.GetModificationTime() == old_time,
			"Unchanged editor copy was rewritten instead of reused");

	write_bytes(source, SECOND_PAYLOAD);
	require(launcher_export_editor(source, downloads.GetPath(), repeated).empty() &&
				read_bytes(repeated) == SECOND_PAYLOAD,
			"New packaged editor was not refreshed");
	require(repeated.GetFullPath() == exported.GetFullPath(), "Updated editor URL changed");

	require(wxRemoveFile(source.GetFullPath()), "Cannot remove packaged editor fixture");
	const wxFileName sentinel(root, "unchanged-output.html");
	repeated = sentinel;
	require(!launcher_export_editor(source, downloads.GetPath(), repeated).empty(),
			"Missing packaged editor was accepted");
	require(repeated.GetFullPath() == sentinel.GetFullPath() &&
				read_bytes(exported) == SECOND_PAYLOAD,
			"Missing source damaged an existing export");
	write_bytes(source, "");
	require(!launcher_export_editor(source, downloads.GetPath(), repeated).empty() &&
				read_bytes(exported) == SECOND_PAYLOAD,
			"Empty source replaced a working export");

	const wxFileName source_directory(root, "packaged-directory.html");
	require(wxFileName::Mkdir(source_directory.GetFullPath()),
			"Cannot create packaged editor directory fixture");
	repeated = sentinel;
	require(!launcher_export_editor(source_directory, downloads.GetPath(), repeated).empty() &&
				repeated.GetFullPath() == sentinel.GetFullPath() &&
				read_bytes(exported) == SECOND_PAYLOAD,
			"Directory source damaged a working export");
#ifdef __UNIX__
	const wxFileName source_fifo(root, "packaged-pipe.html");
	require(::mkfifo(source_fifo.GetFullPath().fn_str(), wxS_IRUSR | wxS_IWUSR) == 0,
			"Cannot create packaged editor pipe fixture");
	// Keep both ends open so a missing regular-file check cannot block in open().
	wxFile fifo_holder(source_fifo.GetFullPath(), wxFile::read_write);
	require(fifo_holder.IsOpened(), "Cannot open packaged editor pipe fixture");
	require(!launcher_export_editor(source_fifo, downloads.GetPath(), repeated).empty() &&
				repeated.GetFullPath() == sentinel.GetFullPath() &&
				read_bytes(exported) == SECOND_PAYLOAD,
			"Pipe source damaged a working export");
	require(fifo_holder.Close(), "Cannot close packaged editor pipe fixture");
#endif

	write_bytes(source, FIRST_PAYLOAD);
	require(!launcher_export_editor(source, "", repeated).empty() &&
				!launcher_export_editor(source, "relative-downloads", repeated).empty(),
			"Invalid Downloads paths were accepted");
	require(read_bytes(exported) == SECOND_PAYLOAD, "Invalid path damaged a previous export");

	const wxFileName blocked(root, "blocked-downloads");
	write_bytes(blocked, "a file, not a directory");
	require(!launcher_export_editor(source, blocked.GetFullPath(), repeated).empty(),
			"A file was accepted as the Downloads directory");
	require(read_bytes(blocked) == "a file, not a directory", "Blocked path was modified");

	require(wxRemoveFile(exported.GetFullPath()), "Cannot replace export with directory fixture");
	require(wxFileName::Mkdir(exported.GetFullPath()), "Cannot create export failure fixture");
	const wxFileName keep(exported.GetFullPath(), "keep.txt");
	write_bytes(keep, "preserved");
	repeated = sentinel;
	require(!launcher_export_editor(source, downloads.GetPath(), repeated).empty() &&
				repeated.GetFullPath() == sentinel.GetFullPath() && read_bytes(keep) == "preserved",
			"Failed commit damaged the existing destination");
}

legacy_int main()
{
	wxInitializer initialize;
	if (!initialize.IsOk()) {
		return 1;
	}
	wxLogNull quiet;
	const wxString root = wxFileName::CreateTempFileName("restunts-editor-export-");
	if (root.empty() || !wxRemoveFile(root) || !wxFileName::Mkdir(root)) {
		return 1;
	}
	legacy_int result = 0;
	try {
		scenarios(root);
		std::puts("Persistent track editor export, refresh, Unicode, and failure checks passed.");
	} catch (const std::exception &error) {
		std::fprintf(stderr, "%s\n", error.what());
		result = 1;
	}
	wxFileName::Rmdir(root, wxPATH_RMDIR_RECURSIVE);
	return result;
}
