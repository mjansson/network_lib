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
    Network data types */

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

/*! Maximum length of a numeric network address string, including zero terminator */
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

#if FOUNDATION_PLATFORM_POSIX
typedef socklen_t network_address_size_t;
#else
typedef int       network_address_size_t;
#endif

typedef struct network_config_t    network_config_t;
typedef struct network_datagram_t  network_datagram_t;
typedef struct network_address_t   network_address_t;
typedef struct network_poll_slot_t network_poll_slot_t;
typedef struct network_poll_t      network_poll_t;

struct network_config_t {
	size_t max_sockets;
	size_t max_tcp_packet_size;
	size_t max_udp_packet_size;
	size_t socket_write_buffer_size;
	size_t socket_read_buffer_size;
	size_t poll_queue_size;
	size_t event_stream_size;
};

struct network_datagram_t {
	uint64_t size;
	void*    data;
};

#define NETWORK_DECLARE_NETWORK_ADDRESS    \
	network_address_family_t family;       \
	network_address_size_t   address_size

struct network_address_t {
	NETWORK_DECLARE_NETWORK_ADDRESS;
};

struct network_poll_slot_t {
	object_t             sock;
	int                  base;
	int                  fd;
};

FOUNDATION_ALIGNED_STRUCT(network_poll_t, 8) {
	unsigned int timeout;
	unsigned int max_sockets;
	unsigned int num_sockets;
#if FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	int fd_poll;
	struct epoll_event* events;
#elif FOUNDATION_PLATFORM_APPLE
	struct pollfd* pollfds;
#endif
	object_t* queue_add;
	object_t* queue_remove;
	network_poll_slot_t* slots;
	char databuffer[];
};
