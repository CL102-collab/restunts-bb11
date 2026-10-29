#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../c/platform.h"

static int reject_commit;
static int reject_predicate;
static unsigned commit_calls;
static char persisted[32];
static const char *expected_commit;

static void write_text(const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");
	assert(file != NULL);
	assert(fwrite(text, 1, strlen(text), file) == strlen(text));
	assert(fclose(file) == 0);
}

static void expect_text(const char *path, const char *text)
{
	char buffer[64] = {0};
	FILE *file = fopen(path, "rb");
	assert(file != NULL);
	assert(fread(buffer, 1, sizeof(buffer) - 1U, file) == strlen(text));
	assert(strcmp(buffer, text) == 0);
	assert(fclose(file) == 0);
}

static unsigned file_count(void)
{
	DIR *directory = opendir(".");
	assert(directory != NULL);
	unsigned count = 0;
	struct dirent *entry;
	while ((entry = readdir(directory)) != NULL) {
		if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
			count++;
		}
	}
	assert(closedir(directory) == 0);
	return count;
}

legacy_int persist_file_needed(const legacy_char *path)
{
	if (reject_predicate) {
		return -1;
	}
	const char *extension = strrchr(path, '.');
	return extension == NULL || strcmp(extension, ".tmp") != 0;
}

legacy_int persist_file(const legacy_char *path, legacy_int removed)
{
	assert(removed == 0);
	assert(strstr(path, ".tmp") == NULL);
	assert(expected_commit != NULL);
	expect_text(path, expected_commit);
	commit_calls++;
	if (reject_commit) {
		return -1;
	}
	strcpy(persisted, expected_commit);
	return 0;
}

static int replace(const char *source, const char *target)
{
	return sdl3_file_replace((const legacy_s8 *)source, (const legacy_s8 *)target);
}

static void test_success_and_backup_collision(void)
{
	write_text("Saved.CSHIG", "old");
	write_text("_C000001.tmp", "new");
	/* Scratch selection must respect DOS case folding and retain occupied slots. */
	write_text("_b000001.TMP", "occupied");
	expected_commit = "new";
	assert(replace("_c000001.TMP", "saved.cshig") == 0);
	expect_text("Saved.CSHIG", "new");
	expect_text("_b000001.TMP", "occupied");
	assert(sdl3_file_exists("_C000001.tmp") == 0);
	assert(sdl3_file_exists("saved.cshig") == 1);
	assert(strcmp(persisted, "new") == 0 && commit_calls == 1);
	assert(file_count() == 2);
	assert(remove("Saved.CSHIG") == 0 && remove("_b000001.TMP") == 0);
}

static void test_failed_commit_restores_both_files(void)
{
	write_text("saved.cshig", "old");
	write_text("source.tmp", "candidate");
	strcpy(persisted, "old");
	expected_commit = "candidate";
	reject_commit = 1;
	assert(replace("source.tmp", "saved.cshig") == -1);
	assert(dos_file_error() != 0 && dos_file_error() == 0);
	expect_text("saved.cshig", "old");
	expect_text("source.tmp", "candidate");
	assert(strcmp(persisted, "old") == 0 && file_count() == 2);
	/* The caller can retry the same complete temporary file. */
	reject_commit = 0;
	assert(replace("source.tmp", "saved.cshig") == 0);
	expect_text("saved.cshig", "candidate");
	assert(strcmp(persisted, "candidate") == 0 && file_count() == 1);
	assert(remove("saved.cshig") == 0);
}

static void test_failed_new_file_and_predicate(void)
{
	write_text("source.tmp", "new");
	expected_commit = "new";
	reject_commit = 1;
	assert(replace("source.tmp", "new.cshig") == -1);
	expect_text("source.tmp", "new");
	assert(sdl3_file_exists("new.cshig") == 0 && file_count() == 1);
	reject_commit = 0;
	reject_predicate = 1;
	unsigned attempts = commit_calls;
	assert(replace("source.tmp", "new.cshig") == -1);
	assert(commit_calls == attempts);
	expect_text("source.tmp", "new");
	assert(sdl3_file_exists("new.cshig") == 0 && file_count() == 1);
	reject_predicate = 0;
	assert(replace("source.tmp", "new.cshig") == 0);
	expect_text("new.cshig", "new");
	assert(file_count() == 1 && remove("new.cshig") == 0);
}

static void test_failed_source_and_directory_target(void)
{
	write_text("saved.cshig", "old");
	unsigned attempts = commit_calls;
	assert(replace("missing.tmp", "saved.cshig") == -1);
	expect_text("saved.cshig", "old");
	assert(commit_calls == attempts && file_count() == 1);
	assert(remove("saved.cshig") == 0);
	assert(mkdir("directory", 0700) == 0);
	write_text("source.tmp", "new");
	assert(replace("source.tmp", "directory") == -1);
	expect_text("source.tmp", "new");
	struct stat info;
	assert(stat("directory", &info) == 0 && S_ISDIR(info.st_mode));
	assert(commit_calls == attempts && file_count() == 2);
	assert(remove("source.tmp") == 0 && rmdir("directory") == 0);
}

static void test_scratch_stays_synchronous(void)
{
	write_text("source.tmp", "scratch");
	unsigned attempts = commit_calls;
	assert(replace("source.tmp", "target.tmp") == 0);
	expect_text("target.tmp", "scratch");
	assert(commit_calls == attempts && file_count() == 1);
	assert(remove("target.tmp") == 0);
}

int main(void)
{
	char original[4096];
	assert(getcwd(original, sizeof(original)) != NULL);
	char directory[] = "/tmp/restunts-file-replace-XXXXXX";
	assert(mkdtemp(directory) != NULL && chdir(directory) == 0);
	test_success_and_backup_collision();
	test_failed_commit_restores_both_files();
	test_failed_new_file_and_predicate();
	test_failed_source_and_directory_target();
	test_scratch_stays_synchronous();
	assert(file_count() == 0);
	assert(chdir(original) == 0 && rmdir(directory) == 0);
	puts("Browser file replacement preserves virtual and persistent scores on failure.");
	return 0;
}
