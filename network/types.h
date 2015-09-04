/* types.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/rampantpixels/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#pragma once

/*! \file types.h
    Network abstraction on top of foundation streams */

#include <foundation/platform.h>
#include <foundation/types.h>

#include <network/build.h>

#if defined( NETWORK_COMPILE ) && NETWORK_COMPILE
#  ifdef __cplusplus
#  define NETWORK_EXTERN extern "C"
#  define NETWORK_API extern "C"
#  else
#  define NETWORK_EXTERN extern
#  define NETWORK_API extern
#  endif
#else
#  ifdef __cplusplus
#  define NETWORK_EXTERN extern "C"
#  define NETWORK_API extern "C"
#  else
#  define NETWORK_EXTERN extern
#  define NETWORK_API extern
#  endif
#endif

#define NETWORK_ADDRESS_NUMERIC_MAX_LENGTH 46

typedef enum {
	NETWORK_ADDRESSFAMILY_IPV4     = 0,
	NETWORK_ADDRESSFAMILY_IPV6
} network_address_family_t;

typedef enum {
	SOCKETSTATE_NOTCONNECTED       = 0,
	SOCKETSTATE_CONNECTING,
	SOCKETSTATE_CONNECTED,
	SOCKETSTATE_LISTENING,
	SOCKETSTATE_DISCONNECTED
} socket_state_t;

typedef enum {
	NETWORKEVENT_CONNECTION = 1,
	NETWORKEVENT_CONNECTED,
	NETWORKEVENT_DATAIN,
	NETWORKEVENT_ERROR,
	NETWORKEVENT_HANGUP,
	NETWORKEVENT_TIMEOUT
} network_event_id;

// OPAQUE COMPLEX TYPES

typedef struct network_address_t network_address_t;
typedef FOUNDATION_ALIGNED_STRUCT(network_poll_t, 8) network_poll_t;

// COMPLEX TYPES

struct network_config_t {
	size_t max_sockets;
	size_t max_tcp_packet_size;
	size_t max_udp_packet_size;
	size_t socket_write_buffer_size;
	size_t socket_read_buffer_size;
	size_t poll_queue_size;
};

struct network_datagram_t {
	uint64_t size;
	void*    data;
};

typedef struct network_datagram_t network_datagram_t;
