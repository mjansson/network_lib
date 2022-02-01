/* network.c  -  Network library  -  Public Domain  -  2013 Mattias Jansson
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/mjansson/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/network.h>
#include <network/internal.h>

#include <foundation/foundation.h>

#if FOUNDATION_PLATFORM_WINDOWS
#include <foundation/windows.h>
#endif

network_config_t network_config;
static bool network_initialized;
static bool network_has_ipv4;
static bool network_has_ipv6;

static void
network_initialize_config(const network_config_t config) {
	FOUNDATION_UNUSED(config);
}

int
network_module_initialize(const network_config_t config) {
	int fd;

	if (network_initialized)
		return 0;

	network_initialize_config(config);

#if FOUNDATION_PLATFORM_WINDOWS
	{
		WSADATA wsadata;
		int err;
		if ((err = WSAStartup(2 /*MAKEWORD( 2, 0 )*/, &wsadata)) != 0) {
			string_const_t errmsg = system_error_message(err);
			log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Unable to initialize WinSock: %.*s (%d)"),
			           STRING_FORMAT(errmsg), err);
			return -1;
		}
	}
#endif

	if (socket_streams_initialize() < 0)
		return -1;

	// Check support
	fd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	network_has_ipv4 = !(fd < 0);
	socket_close_fd(fd);

	fd = (int)socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	network_has_ipv6 = !(fd < 0);
	socket_close_fd(fd);

	network_initialized = true;

	return 0;
}

void
network_module_parse_config(const char* path, size_t path_size, const char* buffer, size_t size,
                            const json_token_t* tokens, size_t tokens_count) {
	FOUNDATION_UNUSED(path);
	FOUNDATION_UNUSED(path_size);
	FOUNDATION_UNUSED(buffer);
	FOUNDATION_UNUSED(size);
	FOUNDATION_UNUSED(tokens);
	FOUNDATION_UNUSED(tokens_count);
}

bool
network_module_is_initialized(void) {
	return network_initialized;
}

void
network_module_finalize(void) {
	if (!network_initialized)
		return;

#if FOUNDATION_PLATFORM_WINDOWS
	WSACleanup();
#endif
}

network_config_t
network_module_config(void) {
	return network_config;
}

bool
network_supports_ipv4(void) {
	return network_has_ipv4;
}

bool
network_supports_ipv6(void) {
	return network_has_ipv6;
}
