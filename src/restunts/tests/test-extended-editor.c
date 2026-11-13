/* Exercise browser dispatch with a real sibling process and filesystem. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "../c/legacy.h"

enum {
	TEST_PATH_CAPACITY = 4096,
	TEST_URI_CAPACITY = TEST_PATH_CAPACITY * 3,
	TEST_HELPER_SUCCESS = 0,
	TEST_HELPER_FAILURE = 7,
	TEST_FILE_MODE = S_IRUSR | S_IWUSR,
	TEST_EXECUTABLE_MODE = TEST_FILE_MODE | S_IXUSR
};

static const legacy_char *fixture_base_path;
static legacy_char fixture_development_directory[TEST_PATH_CAPACITY];
static legacy_char fixture_uri[TEST_URI_CAPACITY];
static legacy_u16 fixture_browser_calls;
static legacy_int fixture_browser_success = 1;

static const legacy_char *test_editor_base_path(void)
{
	return fixture_base_path;
}

static legacy_int test_editor_open_url(const legacy_char *uri)
{
	assert(SDL_strlcpy(fixture_uri, uri, sizeof(fixture_uri)) < sizeof(fixture_uri));
	fixture_browser_calls++;
	return fixture_browser_success;
}

#define SDL_GetBasePath test_editor_base_path
#define SDL_OpenURL test_editor_open_url
#define RESTUNTS_EDITOR_DIRECTORY fixture_development_directory
#include "../c/extended/extended_editor.c"
#undef RESTUNTS_EDITOR_DIRECTORY
#undef SDL_OpenURL
#undef SDL_GetBasePath

static void fixture_path(legacy_char *path, const legacy_char *root, const legacy_char *suffix)
{
	legacy_int length = SDL_snprintf(path, TEST_PATH_CAPACITY, "%s/%s", root, suffix);
	assert(length >= 0 && length < TEST_PATH_CAPACITY);
}

static void write_file(const legacy_char *path, const legacy_char *contents)
{
	FILE *file = fopen(path, "wb");
	assert(file != NULL);
	assert(fputs(contents, file) >= 0);
	assert(fclose(file) == 0);
}

static void write_launcher(const legacy_char *path, legacy_int status)
{
	FILE *file = fopen(path, "wb");
	assert(file != NULL);
	assert(fprintf(file, "#!/bin/sh\nprintf '%%s\\n' \"$0\" \"$@\" > \"$0.invocation\"\nexit %d\n",
				   status) > 0);
	assert(fclose(file) == 0);
	assert(chmod(path, TEST_EXECUTABLE_MODE) == 0);
}

static void assert_launcher_arguments(const legacy_char *marker, const legacy_char *launcher)
{
	FILE *file = fopen(marker, "rb");
	assert(file != NULL);
	legacy_char actual[TEST_URI_CAPACITY];
	size_t length = fread(actual, 1, sizeof(actual) - 1U, file);
	assert(!ferror(file) && feof(file));
	actual[length] = 0;
	assert(fclose(file) == 0);
	legacy_char expected[TEST_URI_CAPACITY];
	legacy_int count = SDL_snprintf(expected, sizeof(expected), "%s\n--track-editor\n", launcher);
	assert(count >= 0 && (size_t)count < sizeof(expected));
	assert(strcmp(actual, expected) == 0);
}

static void assert_file_url(const legacy_char *suffix)
{
	assert(fixture_browser_calls == 1);
	assert(strncmp(fixture_uri, "file:///", strlen("file:///")) == 0);
	assert(strchr(fixture_uri, ' ') == NULL);
	assert(strstr(fixture_uri, "%20") != NULL);
	assert(strstr(fixture_uri, "%C3%A9") != NULL);
	assert(strstr(fixture_uri, "%23%25") != NULL);
	assert(SDL_strstr(fixture_uri, suffix) != NULL);
}

legacy_int main(void)
{
	legacy_char root[] = "/tmp/restunts editor \xc3\xa9 #%-XXXXXX";
	assert(mkdtemp(root) != NULL);
	legacy_char base[TEST_PATH_CAPACITY];
	legacy_char launcher[TEST_PATH_CAPACITY];
	legacy_char marker[TEST_PATH_CAPACITY];
	legacy_char packaged_directory[TEST_PATH_CAPACITY];
	legacy_char packaged_file[TEST_PATH_CAPACITY];
	legacy_char development_file[TEST_PATH_CAPACITY];
	fixture_path(base, root, "bin/");
	fixture_path(launcher, root, "bin/restunts-launcher");
	fixture_path(marker, root, "bin/restunts-launcher.invocation");
	fixture_path(packaged_directory, root, "share/restunts/editor");
	fixture_path(packaged_file, root, "share/restunts/editor/tracked.html");
	fixture_path(fixture_development_directory, root, "development editor");
	fixture_path(development_file, fixture_development_directory, "tracked.html");
	assert(SDL_CreateDirectory(base));
	assert(SDL_CreateDirectory(packaged_directory));
	assert(SDL_CreateDirectory(fixture_development_directory));
	fixture_base_path = base;

	/* A missing complete package must not report a browser launch. */
	assert(!extended_editor_open());
	assert(fixture_browser_calls == 0);
	write_file(packaged_file, "<!doctype html><title>Packaged editor</title>");
	assert(extended_editor_open());
	assert_file_url("/share/restunts/editor/tracked.html");

	/* The helper receives an argv path containing spaces, Unicode and URI
	 * punctuation. Successful delegation never opens the private mount URL. */
	write_launcher(launcher, TEST_HELPER_SUCCESS);
	fixture_browser_calls = 0;
	assert(extended_editor_open());
	assert(fixture_browser_calls == 0);
	assert_launcher_arguments(marker, launcher);
	assert(SDL_RemovePath(marker));

	/* An export/portal failure cannot fall back to a browser-invisible URL. */
	write_launcher(launcher, TEST_HELPER_FAILURE);
	assert(!extended_editor_open());
	assert(fixture_browser_calls == 0);
	assert_launcher_arguments(marker, launcher);
	assert(SDL_RemovePath(marker));
	assert(chmod(launcher, TEST_FILE_MODE) == 0);
	assert(!extended_editor_open());
	assert(fixture_browser_calls == 0);
	assert(!SDL_GetPathInfo(marker, NULL));

	/* Builds without the optional desktop launcher retain native file URLs. */
	assert(SDL_RemovePath(launcher));
	assert(extended_editor_open());
	assert_file_url("/share/restunts/editor/tracked.html");
	assert(SDL_RemovePath(packaged_file));
	fixture_browser_calls = 0;
	assert(!extended_editor_open());
	assert(fixture_browser_calls == 0);

	/* The development fallback also works when SDL has no executable path. */
	write_file(development_file, "<!doctype html><title>Development editor</title>");
	fixture_base_path = NULL;
	assert(extended_editor_open());
	assert_file_url("/development%20editor/tracked.html");
	fixture_browser_calls = 0;
	fixture_browser_success = 0;
	assert(!extended_editor_open());
	assert(fixture_browser_calls == 1);

	assert(SDL_RemovePath(development_file));
	assert(SDL_RemovePath(fixture_development_directory));
	assert(SDL_RemovePath(packaged_directory));
	fixture_path(packaged_directory, root, "share/restunts");
	assert(SDL_RemovePath(packaged_directory));
	fixture_path(packaged_directory, root, "share");
	assert(SDL_RemovePath(packaged_directory));
	assert(SDL_RemovePath(base));
	assert(SDL_RemovePath(root));
	puts("In-game editor helper dispatch, failures and file URL fallbacks passed.");
	return 0;
}
