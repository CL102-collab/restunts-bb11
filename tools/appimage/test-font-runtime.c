/* Exercise real font directory enumeration and FreeType file loading. */
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../../src/restunts/c/legacy.h"

#define TEST_ARGUMENT_COUNT 4
#define TEST_FONT_DIRECTORY_ARGUMENT 1
#define TEST_FONT_FILE_ARGUMENT 2
#define TEST_FONT_CONFIG_ARGUMENT 3
#define TEST_GUARD_BYTES 16U
#define TEST_GUARD_VALUE 0xa5U

legacy_int main(legacy_int argc, legacy_char **argv)
{
	FcConfig *config;
	FcFontSet *fonts;
	FT_Library library;
	FT_Face face;
	FcCache *cache;
	FcChar8 *cache_file = NULL;
	struct stat expected_stat;
	struct {
		legacy_u8 before[TEST_GUARD_BYTES];
		struct stat value;
		legacy_u8 after[TEST_GUARD_BYTES];
	} guarded;
	legacy_u8 sentinel[TEST_GUARD_BYTES];
	if (argc != TEST_ARGUMENT_COUNT || FT_Init_FreeType(&library) ||
		FT_New_Face(library, argv[TEST_FONT_FILE_ARGUMENT], 0, &face)) {
		fputs("FreeType could not read the regression font.\n", stderr);
		return EXIT_FAILURE;
	}
	printf("FreeType loaded %s (%ld glyphs).\n", face->family_name, face->num_glyphs);
	FT_Done_Face(face);
	FT_Done_FreeType(library);
	config = FcConfigCreate();
	if (!config ||
		!FcConfigAppFontAddDir(config, (const FcChar8 *)argv[TEST_FONT_DIRECTORY_ARGUMENT])) {
		fputs("Fontconfig could not scan the regression directory.\n", stderr);
		return EXIT_FAILURE;
	}
	fonts = FcConfigGetFonts(config, FcSetApplication);
	if (!fonts || fonts->nfont == 0) {
		fputs("Fontconfig silently lost all regression fonts.\n", stderr);
		return EXIT_FAILURE;
	}
	printf("Fontconfig enumerated %d font(s).\n", fonts->nfont);
	if (!FcConfigParseAndLoad(config, (const FcChar8 *)argv[TEST_FONT_CONFIG_ARGUMENT], FcTrue) ||
		!FcConfigBuildFonts(config)) {
		fputs("Fontconfig configuration loading/building failed.\n", stderr);
		return EXIT_FAILURE;
	}
	fonts = FcConfigGetFonts(config, FcSetSystem);
	if (!fonts || fonts->nfont == 0) {
		fputs("Fontconfig configuration lost its system fonts.\n", stderr);
		return EXIT_FAILURE;
	}
	cache =
		FcDirCacheLoad((const FcChar8 *)argv[TEST_FONT_DIRECTORY_ARGUMENT], config, &cache_file);
	if (!cache || !cache_file) {
		fputs("Fontconfig could not load its written font cache.\n", stderr);
		return EXIT_FAILURE;
	}
	FcDirCacheUnload(cache);
	cache = FcDirCacheLoadFile(cache_file, NULL);
	if (!cache) {
		fputs("Fontconfig could not map its font cache file.\n", stderr);
		return EXIT_FAILURE;
	}
	FcDirCacheUnload(cache);
	/* Match Fontconfig's 64-bit file ABI and check its returned metadata. */
	if (stat((const legacy_char *)cache_file, &expected_stat) != 0) {
		fputs("Could not read the font cache metadata.\n", stderr);
		return EXIT_FAILURE;
	}
	memset(&guarded, TEST_GUARD_VALUE, sizeof guarded);
	memset(sentinel, TEST_GUARD_VALUE, sizeof sentinel);
	cache = FcDirCacheLoadFile(cache_file, &guarded.value);
	if (!cache || guarded.value.st_dev != expected_stat.st_dev ||
		guarded.value.st_ino != expected_stat.st_ino ||
		guarded.value.st_size != expected_stat.st_size ||
		memcmp(guarded.before, sentinel, sizeof sentinel) ||
		memcmp(guarded.after, sentinel, sizeof sentinel)) {
		fputs("Fontconfig returned incompatible 64-bit cache metadata.\n", stderr);
		return EXIT_FAILURE;
	}
	FcDirCacheUnload(cache);
	FcStrFree(cache_file);
	printf("Fontconfig parsed configuration, reused cache, and returned 64-bit metadata.\n");
	FcConfigDestroy(config);
	return EXIT_SUCCESS;
}
