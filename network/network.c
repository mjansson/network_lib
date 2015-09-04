/* network.c  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/rampantpixels/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/network.h>
#include <network/internal.h>

#include <foundation/foundation.h>

#if FOUNDATION_PLATFORM_WINDOWS
#  include <foundation/windows.h>
#endif

static network_config_t _network_config;
static bool _network_initialized;
static bool _network_supports_ipv4;
static bool _network_supports_ipv6;

static void
network_initialize_config(const network_config_t config) {
	_network_config.max_sockets              = config.max_sockets              ?
	                                           config.max_sockets              : 1024;
	_network_config.max_tcp_packet_size      = config.max_tcp_packet_size      ?
	                                           config.max_tcp_packet_size      : 1024;
	_network_config.max_udp_packet_size      = config.max_udp_packet_size      ?
	                                           config.max_udp_packet_size      : 1024;
	_network_config.socket_write_buffer_size = config.socket_write_buffer_size ?
	                                           config.socket_write_buffer_size : 8192;
	_network_config.socket_read_buffer_size  = config.socket_read_buffer_size  ?
	                                           config.socket_read_buffer_size  : 8192;
	_network_config.poll_queue_size          = config.poll_queue_size          ?
	                                           config.poll_queue_size          : 32;

#define BUILD_SIZE_DEFALT_NUM_SOCKETS         128
	
	_network_config.max_sockets = math_clamp(_network_config.max_sockets, 8, 65535);
}

int
network_initialize(const network_config_t config) {
	int fd;

	if (_network_initialized)
		return 0;

	network_initialize_config(config);

	log_debugf(HASH_NETWORK, STRING_CONST("Initializing network services"));

#if FOUNDATION_PLATFORM_WINDOWS
	{
		WSADATA wsadata;
		int err;
		if ((err = WSAStartup(2/*MAKEWORD( 2, 0 )*/, &wsadata)) != 0) {
			string_const_t errmsg = system_error_message(err);
			log_errorf(HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL,
			           STRING_CONST("Unable to initialize WinSock: %*s (%d)"), STRING_FORMAT(errmsg), err);
			return -1;
		}
	}
#endif

	if (network_event_initialize() < 0)
		return -1;

	if (socket_initialize(max_sockets) < 0)
		return -1;

	//Check support
	fd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	_network_supports_ipv4 = !(fd < 0);
	_socket_close_fd(fd);

	fd = (int)socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	_network_supports_ipv6 = !(fd < 0);
	_socket_close_fd(fd);

	return 0;
}

bool
network_is_initialized(void) {
	return _network_initialized;
}

void
network_finalize(void) {
	if (!_network_initialized)
		return;

	log_debug(HASH_NETWORK, STRING_CONST("Terminating network services"));

	socket_finalize();
	network_event_finalize();

#if FOUNDATION_PLATFORM_WINDOWS
	WSACleanup();
#endif
}

bool
network_supports_ipv4(void) {
	return _network_supports_ipv4;
}

bool
network_supports_ipv6(void) {
	return _network_supports_ipv6;
}
