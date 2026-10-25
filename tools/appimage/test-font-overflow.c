/* Model a filesystem whose inodes/directory cookies do not fit the old ABI. */
#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include "../../src/restunts/c/legacy.h"

legacy_int __xstat(legacy_int version, const legacy_char *path, struct stat *value)
{
	(void)version;
	(void)path;
	(void)value;
	errno = EOVERFLOW;
	return -1;
}

legacy_int __fxstat(legacy_int version, legacy_int descriptor, struct stat *value)
{
	(void)version;
	(void)descriptor;
	(void)value;
	errno = EOVERFLOW;
	return -1;
}

struct dirent *readdir(DIR *directory)
{
	(void)directory;
	errno = EOVERFLOW;
	return NULL;
}
