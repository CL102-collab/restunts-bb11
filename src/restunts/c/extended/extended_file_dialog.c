#include "extended_file_dialog.h"
#include <string.h>
#define EXTENDED_FILE_LEGACY_NAME_CAPACITY 13U
static legacy_s16 extended_file_suffix_equal(const legacy_char *first, const legacy_char *second)
{
	while (*first && *second) {
		legacy_u8 a = (legacy_u8)*first++, b = (legacy_u8)*second++;
		if (a >= 'A' && a <= 'Z') {
			a = (legacy_u8)(a - 'A' + 'a');
		}
		if (b >= 'A' && b <= 'Z') {
			b = (legacy_u8)(b - 'A' + 'a');
		}
		if (a != b) {
			return 0;
		}
	}
	return *first == *second;
}
legacy_u16 extended_file_name_supported(const legacy_char *name, legacy_u16 capacity)
{
	size_t length = strlen(name);
	if (length < EXTENDED_FILE_LEGACY_NAME_CAPACITY) {
		return 1;
	}
	const legacy_char *extension = strrchr(name, '.');
	return length < capacity && extension != NULL && extension - name >= 1 &&
		   (size_t)(extension - name) <= EXTENDED_FILE_STEM_MAX_LENGTH &&
		   (extended_file_suffix_equal(extension, ".cstrk") ||
			extended_file_suffix_equal(extension, ".csrpl") ||
			extended_file_suffix_equal(extension, ".cshig"));
}
