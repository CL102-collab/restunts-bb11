#ifndef RESTUNTS_EXTENDED_FILE_DIALOG_H
#define RESTUNTS_EXTENDED_FILE_DIALOG_H
#include "../legacy.h"
#define EXTENDED_FILE_STEM_MAX_LENGTH 12U
#define EXTENDED_FILE_STEM_CAPACITY (EXTENDED_FILE_STEM_MAX_LENGTH + 1U)
#define EXTENDED_FILE_NAME_CAPACITY (EXTENDED_FILE_STEM_MAX_LENGTH + sizeof(".cstrk"))
legacy_u16 extended_file_name_supported(const legacy_char *name, legacy_u16 capacity);
#endif
