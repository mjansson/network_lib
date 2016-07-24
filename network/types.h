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

#if FOUNDATION_PLATFORM_POSIX
#  include <sys/socket.h>
#endif

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
	NETWORKEVENT_HANGUP
} network_event_id;

#if FOUNDATION_PLATFORM_POSIX
typedef socklen_t network_address_size_t;
#else
typedef int       network_address_size_t;
#endif

typedef struct network_config_t      network_config_t;
typedef struct network_address_t     network_address_t;
typedef struct network_poll_slot_t   network_poll_slot_t;
typedef struct network_poll_event_t  network_poll_event_t;
typedef struct network_poll_t        network_poll_t;
typedef struct socket_t              socket_t;
typedef struct socket_stream_t       socket_stream_t;

typedef void (*socket_open_fn)(socket_t*, unsigned int);
typedef void (*socket_stream_initialize_fn)(socket_t*, stream_t*);

struct network_config_t {
	size_t _unused;
};

#define NETWORK_DECLARE_NETWORK_ADDRESS    \
	network_address_family_t family;       \
	network_address_size_t   address_size

struct network_address_t {
	NETWORK_DECLARE_NETWORK_ADDRESS;
};

struct network_poll_slot_t {
	socket_t*  sock;
	int        fd;
};

FOUNDATION_ALIGNED_STRUCT(socket_stream_t, 8) {
	FOUNDATION_DECLARE_STREAM;
	socket_t* socket;

	size_t read_in;
	size_t write_in;
	size_t write_out;

	size_t buffer_in_size;
	size_t buffer_out_size;

	uint8_t* buffer_in;
	uint8_t* buffer_out;
};

struct socket_t {
	int fd;

	uint32_t flags: 10;
	uint32_t state: 6;
	uint32_t _unused: 16;

	network_address_family_t family;

	network_address_t* address_local;
	network_address_t* address_remote;

	size_t bytes_read;
	size_t bytes_written;

	socket_open_fn open_fn;
	socket_stream_initialize_fn stream_initialize_fn;

	void* client;
};

struct network_poll_t {
	unsigned int timeout;
	size_t max_sockets;
	size_t num_sockets;
#if FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
	int fd_poll;
	struct epoll_event* events;
#elif FOUNDATION_PLATFORM_APPLE
	struct pollfd* pollfds;
#endif
	network_poll_slot_t slots[FOUNDATION_FLEXIBLE_ARRAY];
};

struct network_poll_event_t {
	network_event_id event;
	socket_t* socket;
};
