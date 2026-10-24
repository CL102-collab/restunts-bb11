/* Exercise resolver workers without DNS or network access. */
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <asyncns.h>
#include "../../src/restunts/c/legacy.h"

#define RESOLVER_WORKER_COUNT 1U
#define RESOLVER_BLOCKING_WAIT 1
#define RESOLVER_TEST_SUCCESS 0
#define RESOLVER_TEST_FAILURE 1

static legacy_int query_address(asyncns_t *resolver, const legacy_char *address, legacy_int family)
{
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_flags = AI_NUMERICHOST;
	asyncns_query_t *query = asyncns_getaddrinfo(resolver, address, NULL, &hints);
	if (query == NULL) {
		return RESOLVER_TEST_FAILURE;
	}
	while (!asyncns_isdone(resolver, query)) {
		if (asyncns_wait(resolver, RESOLVER_BLOCKING_WAIT) < 0) {
			return RESOLVER_TEST_FAILURE;
		}
	}
	struct addrinfo *answer = NULL;
	if (asyncns_getaddrinfo_done(resolver, query, &answer) != 0 || answer == NULL) {
		return RESOLVER_TEST_FAILURE;
	}
	legacy_int result = answer->ai_family == family ? RESOLVER_TEST_SUCCESS : RESOLVER_TEST_FAILURE;
	asyncns_freeaddrinfo(answer);
	return result;
}

legacy_int main(void)
{
	asyncns_t *resolver = asyncns_new(RESOLVER_WORKER_COUNT);
	if (resolver == NULL) {
		return RESOLVER_TEST_FAILURE;
	}
	legacy_int result = query_address(resolver, "127.0.0.1", AF_INET);
	if (result == RESOLVER_TEST_SUCCESS) {
		result = query_address(resolver, "::1", AF_INET6);
	}
	asyncns_free(resolver);
	if (result == RESOLVER_TEST_SUCCESS) {
		puts("libasyncns numeric IPv4/IPv6 resolver workers passed");
	}
	return result;
}
