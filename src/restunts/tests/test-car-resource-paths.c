#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/platform.h"

#define TEST_DIRECTORY_MODE 0700
#define TEST_CAR_CAPACITY 16U
#define TEST_RESOURCE_SIZE 16U
#define TEST_PACK_HEADER_SIZE 10U
#define TEST_ESCAPE_VALUE 224U
#define TEST_ESCAPE_SETTINGS 129U

static legacy_char original_directory[FILE_CAR_PATH_SIZE];
static legacy_char temporary_directory[] = "/tmp/restunts-cars-XXXXXX";
static const legacy_s8 expected_ids[][FILE_CAR_ID_BUFFER_SIZE] = {"A001", "A002", "A003", "A004",
																  "A005", "A006", "A007", "A_08"};

void *_memcpy(void *destination, const void *source, legacy_u16 length)
{
	return memcpy(destination, source, length);
}

void fatal_error(const legacy_s8 *format, ...)
{
	fprintf(stderr, "Unexpected fatal error: %s\n", (const legacy_char *)format);
	abort();
}

void dos_process_exit(legacy_s16 status)
{
	exit(status);
}

static void make_directory(const legacy_char *name)
{
	assert(mkdir(name, TEST_DIRECTORY_MODE) == 0);
}

static void write_resource(const legacy_char *name, legacy_u8 marker, legacy_u16 packed)
{
	legacy_u8 bytes[TEST_PACK_HEADER_SIZE + TEST_RESOURCE_SIZE] = {0};
	legacy_u16 offset = 0;
	if (packed) {
		bytes[0] = 1;
		bytes[1] = TEST_RESOURCE_SIZE;
		bytes[4] = TEST_RESOURCE_SIZE;
		bytes[8] = TEST_ESCAPE_SETTINGS;
		bytes[9] = TEST_ESCAPE_VALUE;
		offset = TEST_PACK_HEADER_SIZE;
	}
	memset(bytes + offset, marker, TEST_RESOURCE_SIZE);
	FILE *file = fopen(name, "wb");
	assert(file != NULL);
	assert(fwrite(bytes, 1, offset + TEST_RESOURCE_SIZE, file) == offset + TEST_RESOURCE_SIZE);
	assert(fclose(file) == 0);
}

static void assert_resource(const legacy_char *name, legacy_u8 marker)
{
	legacy_u8 bytes[TEST_RESOURCE_SIZE];
	assert(file_read_nofatal((const legacy_s8 *)name, bytes) == bytes);
	for (legacy_u16 index = 0; index < sizeof(bytes); index++) {
		assert(bytes[index] == marker);
	}
}

static void assert_loaded(const legacy_char *stem, legacy_u8 marker)
{
	legacy_u8 *resource = file_load_resfile((const legacy_s8 *)stem);
	assert(resource != NULL);
	for (legacy_u16 index = 0; index < TEST_RESOURCE_SIZE; index++) {
		assert(resource[index] == marker);
	}
	mmgr_release(resource);
}

static void assert_discovery(void)
{
	legacy_s8 ids[TEST_CAR_CAPACITY][FILE_CAR_ID_BUFFER_SIZE];
	legacy_u16 count = file_find_car_ids(ids, TEST_CAR_CAPACITY);
	assert(count == sizeof(expected_ids) / sizeof(expected_ids[0]));
	for (legacy_u16 index = 0; index < count; index++) {
		legacy_u16 matches = 0;
		for (legacy_u16 other = 0; other < count; other++) {
			matches += strcmp((const legacy_char *)ids[other],
							  (const legacy_char *)expected_ids[index]) == 0;
		}
		assert(matches == 1);
	}
	assert(file_find_car_ids(ids, 0) == 0);
	assert(file_find_car_ids(ids, 1) == 1);
}

legacy_int main(void)
{
	assert(getcwd(original_directory, sizeof(original_directory)) != NULL);
	assert(mkdtemp(temporary_directory) != NULL);
	assert(chdir(temporary_directory) == 0);
	make_directory("Data");
	make_directory("App");
	make_directory("Data/CaRs");
	make_directory("App/cars");
	make_directory("Bundled");
	write_resource("Data/CaRa001.ReS", 1, 0);
	write_resource("App/CARA002.RES", 2, 0);
	write_resource("Data/CaRs/CARA003.RES", 3, 0);
	write_resource("App/cars/cara004.res", 4, 0);
	write_resource("Bundled/CARA005.RES", 5, 0);
	write_resource("Data/CaRa006.PrE", 6, 1);
	write_resource("App/cars/CARA006.RES", 60, 0);
	write_resource("App/cars/CARA007.PRE", 7, 1);
	write_resource("Data/CaRs/CaRa_08.ReS", 8, 0);
	write_resource("Data/CaRs/CARA001.RES", 10, 0);
	write_resource("App/CARA001.RES", 11, 0);
	write_resource("App/cars/CARA001.RES", 12, 0);
	write_resource("Data/CaRs/CARXX.RES", 13, 0);
	write_resource("Data/CaRs/CARABCDE.RES", 14, 0);
	write_resource("Data/CaRs/MISC.RES", 15, 0);
	make_directory("Data/CaRs/CARA008.RES");
	write_resource("Data/CaRs/Sta003.3Sh", 33, 0);
	write_resource("App/cars/STA004.3SH", 44, 0);
	write_resource("App/cars/STDaa004.VsH", 45, 0);
	write_resource("App/cars/STDBA004.VSH", 46, 0);
	write_resource("Data/STA006.P3S", 66, 1);
	write_resource("App/cars/STA006.3SH", 67, 0);
	write_resource("Data/STDAA006.PVS", 68, 1);
	write_resource("App/cars/STDAA006.VSH", 69, 0);
	assert(chdir("App") == 0);
	legacy_char executable_directory[FILE_CAR_PATH_SIZE];
	assert(getcwd(executable_directory, sizeof(executable_directory)) != NULL);
	file_car_set_base((const legacy_s8 *)executable_directory);
	assert(chdir("../Data") == 0);
	mmgr_init_conventional_arena();
	assert_discovery();
	assert_resource("cara001.res", 1);
	assert_resource("cara002.res", 2);
	assert_resource("cara003.res", 3);
	assert_resource("cara004.res", 4);
	assert_resource("cara005.res", 5);
	assert_resource("sta003.3sh", 33);
	assert_resource("sta004.3sh", 44);
	assert_resource("stdaa004.vsh", 45);
	assert_resource("stdba004.vsh", 46);
	assert_loaded("cara001", 1);
	assert_loaded("cara004", 4);
	assert_loaded("cara006", 6);
	assert_loaded("cara007", 7);
	assert_loaded("cara_08", 8);
	legacy_u8 *packed_model = file_decomp_nofatal((const legacy_s8 *)"sta006.p3s");
	assert(packed_model != NULL && packed_model[0] == 66);
	mmgr_release(packed_model);
	legacy_u8 *packed_dashboard = file_decomp_nofatal((const legacy_s8 *)"stdaa006.pvs");
	assert(packed_dashboard != NULL && packed_dashboard[0] == 68);
	mmgr_release(packed_dashboard);
	assert(file_open_resource((const legacy_s8 *)"sta006.3sh") == 0);
	assert(file_find((const legacy_s8 *)"sta006.p3s") != NULL);
	assert(file_find((const legacy_s8 *)"stdaa006.vsh") == NULL);
	assert(file_find((const legacy_s8 *)"stdaa006.pvs") != NULL);
	assert(file_find((const legacy_s8 *)"stdaa004.vsh") != NULL);
	assert(file_find((const legacy_s8 *)"misc.res") == NULL);
	assert(file_open_resource((const legacy_s8 *)"misc.res") == 0);
	assert(file_open_resource((const legacy_s8 *)"missing/cara004.res") == 0);
	const legacy_char *const files[] = {"CaRa001.ReS",
										"CaRa006.PrE",
										"STA006.P3S",
										"STDAA006.PVS",
										"CaRs/CARA003.RES",
										"CaRs/CARA001.RES",
										"CaRs/CARXX.RES",
										"CaRs/CARABCDE.RES",
										"CaRs/MISC.RES",
										"CaRs/Sta003.3Sh",
										"CaRs/CaRa_08.ReS",
										"../App/CARA002.RES",
										"../App/CARA001.RES",
										"../App/cars/cara004.res",
										"../App/cars/CARA006.RES",
										"../App/cars/CARA007.PRE",
										"../App/cars/CARA001.RES",
										"../App/cars/STA004.3SH",
										"../App/cars/STDaa004.VsH",
										"../App/cars/STDBA004.VSH",
										"../App/cars/STA006.3SH",
										"../App/cars/STDAA006.VSH",
										"../Bundled/CARA005.RES"};
	for (legacy_u16 index = 0; index < sizeof(files) / sizeof(files[0]); index++) {
		assert(remove(files[index]) == 0);
	}
	assert(rmdir("CaRs/CARA008.RES") == 0);
	assert(rmdir("CaRs") == 0);
	assert(rmdir("../App/cars") == 0);
	assert(chdir("..") == 0);
	assert(rmdir("Data") == 0);
	assert(rmdir("App") == 0);
	assert(rmdir("Bundled") == 0);
	file_car_set_base(NULL);
	assert(chdir(original_directory) == 0);
	assert(rmdir(temporary_directory) == 0);
	puts("Car directory discovery, loose overrides, packed resources and asset paths passed");
	return 0;
}
