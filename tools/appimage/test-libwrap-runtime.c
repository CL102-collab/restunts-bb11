/* Keep real access policies while omitting the optional NIS client ABI. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <tcpd.h>
#include "../../src/restunts/c/legacy.h"

#define POLICY_PATH_CAPACITY 1024U
#define POLICY_TEST_SUCCESS 0
#define POLICY_TEST_FAILURE 1
#define POLICY_ALLOWED 1
#define POLICY_DENIED 0

struct PolicyCase {
	const legacy_char *allow;
	const legacy_char *deny;
	const legacy_char *daemon;
	const legacy_char *name;
	const legacy_char *address;
	legacy_int expected;
};

static legacy_int write_policy(const legacy_char *path, const legacy_char *contents)
{
	FILE *output = fopen(path, "w");
	if (output == NULL) {
		return POLICY_TEST_FAILURE;
	}
	legacy_int written = fputs(contents, output);
	legacy_int closed = fclose(output);
	return written < 0 || closed != 0 ? POLICY_TEST_FAILURE : POLICY_TEST_SUCCESS;
}

legacy_int main(void)
{
	const struct PolicyCase cases[] = {
		{"", "", "restunts", "localhost", "127.0.0.1", POLICY_ALLOWED},
		{"", "ALL: ALL\n", "restunts", "localhost", "127.0.0.1", POLICY_DENIED},
		{"restunts: 127.0.0.1\n", "ALL: ALL\n", "restunts", "localhost", "127.0.0.1",
		 POLICY_ALLOWED},
		{"other: 127.0.0.1\n", "ALL: ALL\n", "restunts", "localhost", "127.0.0.1", POLICY_DENIED},
		{"restunts: [::1]\n", "ALL: ALL\n", "restunts", "localhost", "::1", POLICY_ALLOWED},
		{"restunts: 192.0.2.0/24\n", "ALL: ALL\n", "restunts", "example", "192.0.2.42",
		 POLICY_ALLOWED},
		{"restunts: 192.0.2.0/24\n", "ALL: ALL\n", "restunts", "example", "198.51.100.42",
		 POLICY_DENIED},
		{"restunts: .example.test\n", "ALL: ALL\n", "restunts", "host.example.test", "192.0.2.42",
		 1},
	};
	legacy_char directory[] = "/tmp/restunts-libwrap-policy.XXXXXX";
	legacy_char allow_path[POLICY_PATH_CAPACITY];
	legacy_char deny_path[POLICY_PATH_CAPACITY];
	if (mkdtemp(directory) == NULL) {
		return POLICY_TEST_FAILURE;
	}
	snprintf(allow_path, sizeof(allow_path), "%s/allow", directory);
	snprintf(deny_path, sizeof(deny_path), "%s/deny", directory);
	hosts_allow_table = allow_path;
	hosts_deny_table = deny_path;
	legacy_int result = POLICY_TEST_SUCCESS;
	for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		const struct PolicyCase *test = &cases[index];
		if (write_policy(allow_path, test->allow) != POLICY_TEST_SUCCESS ||
			write_policy(deny_path, test->deny) != POLICY_TEST_SUCCESS ||
			hosts_ctl((legacy_char *)test->daemon, (legacy_char *)test->name,
					  (legacy_char *)test->address, "user") != test->expected) {
			fprintf(stderr, "libwrap access-policy case %" LEGACY_PRIu32 " failed\n",
					(legacy_u32)index);
			result = POLICY_TEST_FAILURE;
			break;
		}
	}
	unlink(allow_path);
	unlink(deny_path);
	rmdir(directory);
	if (result == POLICY_TEST_SUCCESS) {
		puts("libwrap IPv4/IPv6/CIDR/hostname access policies passed");
	}
	return result;
}
