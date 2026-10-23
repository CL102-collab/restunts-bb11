#include "../c/platform.h"
#include "../c/fileio.h"

static legacy_u16 car_base_initialized;

void file_car_set_base(const legacy_s8 *directory)
{
	const legacy_s8 *end = directory;
	while (*end != 0) {
		end++;
	}
	car_base_initialized =
		end != directory && (end[-1] == '\\' || end[-1] == '/' || end[-1] == ':');
}

static legacy_s16 same_text(const legacy_s8 *left, const legacy_s8 *right)
{
	while (*left != 0 && *left == *right) {
		left++;
		right++;
	}
	return *left == 0 && *right == 0;
}

void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	dos_process_exit(1);
}

legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[])
{
	legacy_s16 valid = 0;
	if (argc == 64) {
		valid = 1;
		for (legacy_s16 index = 1; index < argc; index++) {
			if (!same_text(argv[index], "x")) {
				valid = 0;
			}
		}
	} else if (argc == 4) {
		valid = same_text(argv[1], "quoted") && same_text(argv[2], "one two") &&
				same_text(argv[3], "three");
	}
	valid = valid && car_base_initialized;
	static const legacy_s8 success[] = "DOS command tail passed\r\n";
	static const legacy_s8 failure[] = "DOS command tail failed\r\n";
	dos_write_stdout(valid ? success : failure,
					 valid ? sizeof(success) - 1U : sizeof(failure) - 1U);
	return !valid;
}
