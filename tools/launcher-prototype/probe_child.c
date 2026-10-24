/* Diagnostic child: record the real Windows argument vector and working directory.
 * The prototype deliberately does not change the game's option parser. */
#include <windows.h>
#include <shellapi.h>
#include "../../src/restunts/c/legacy.h"

#define PROBE_PATH_CAPACITY 32768U
#define PROBE_UTF8_CAPACITY (PROBE_PATH_CAPACITY * 4U)
#define PROBE_SUCCESS 0
#define PROBE_FAILURE 1

static legacy_u8 write_text(HANDLE output, const WCHAR *text)
{
	legacy_char utf8[PROBE_UTF8_CAPACITY];
	legacy_int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
	if (length == 0) {
		return 0;
	}
	DWORD written;
	DWORD bytes = (DWORD)(length - 1);
	return WriteFile(output, utf8, bytes, &written, NULL) && written == bytes;
}

legacy_int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, legacy_int show)
{
	(void)instance;
	(void)previous;
	(void)command;
	(void)show;
	WCHAR path[PROBE_PATH_CAPACITY];
	DWORD length = GetModuleFileNameW(NULL, path, PROBE_PATH_CAPACITY);
	if (length == 0 || length >= PROBE_PATH_CAPACITY) {
		return PROBE_FAILURE;
	}
	while (length > 0 && path[length - 1] != L'\\') {
		length--;
	}
	const WCHAR result_name[] = L"launcher-probe-result.txt";
	const DWORD result_length = sizeof(result_name) / sizeof(result_name[0]);
	if (length + result_length > PROBE_PATH_CAPACITY) {
		return PROBE_FAILURE;
	}
	CopyMemory(path + length, result_name, sizeof(result_name));
	HANDLE output =
		CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (output == INVALID_HANDLE_VALUE) {
		return PROBE_FAILURE;
	}
	legacy_int count;
	WCHAR **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
	legacy_u8 success = arguments != NULL;
	for (legacy_int index = 0; success && index < count; index++) {
		success = write_text(output, arguments[index]) && write_text(output, L"\r\n");
	}
	DWORD directory_length = GetCurrentDirectoryW(PROBE_PATH_CAPACITY, path);
	success = success && directory_length > 0 && directory_length < PROBE_PATH_CAPACITY &&
			  write_text(output, L"cwd=") && write_text(output, path) &&
			  write_text(output, L"\r\n");
	if (arguments != NULL) {
		LocalFree(arguments);
	}
	success = CloseHandle(output) && success;
	return success ? PROBE_SUCCESS : PROBE_FAILURE;
}
