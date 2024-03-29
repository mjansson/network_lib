/* types.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/mjansson/network_lib
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

#if FOUNDATION_PLATFORM_WINDOWS
#include <foundation/windows.h>
#elif FOUNDATION_PLATFORM_POSIX
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#if defined(NETWORK_COMPILE) && NETWORK_COMPILE
#ifdef __cplusplus
#define NETWORK_EXTERN extern "C"
#define NETWORK_API extern "C"
#else
#define NETWORK_EXTERN extern
#define NETWORK_API extern
#endif
#else
#ifdef __cplusplus
#define NETWORK_EXTERN extern "C"
#define NETWORK_API extern "C"
#else
#define NETWORK_EXTERN extern
#define NETWORK_API extern
#endif
#endif

/*! Maximum length of a numeric network address string, including zero terminator */
#define NETWORK_ADDRESS_NUMERIC_MAX_LENGTH 46

/*! Infinite timeout */
#define NETWORK_TIMEOUT_INFINITE 0xFFFFFFFF

/*! Invalid socket fd */
#define NETWORK_SOCKET_INVALID -1

typedef enum { NETWORK_ADDRESSFAMILY_IPV4 = 0, NETWORK_ADDRESSFAMILY_IPV6 } network_address_family_t;

typedef enum { NETWORK_SOCKETTYPE_TCP = 0, NETWORK_SOCKETTYPE_UDP } network_socket_type_t;

typedef enum {
	SOCKETSTATE_NOTCONNECTED = 0,
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
typedef size_t network_send_size_t;
#else
typedef int network_address_size_t;
typedef int network_send_size_t;
#endif

typedef struct network_config_t network_config_t;
typedef struct network_address_t network_address_t;
typedef struct network_poll_slot_t network_poll_slot_t;
typedef struct network_poll_event_t network_poll_event_t;
typedef struct network_poll_t network_poll_t;
typedef struct socket_t socket_t;
typedef struct socket_stream_t socket_stream_t;
typedef struct socket_header_t socket_header_t;
typedef union socket_data_t socket_data_t;

typedef void (*socket_open_fn)(socket_t*, unsigned int);
typedef void (*socket_stream_initialize_fn)(socket_t*, stream_t*);

struct network_config_t {
	size_t unused;
};

#define NETWORK_DECLARE_NETWORK_ADDRESS \
	network_address_family_t family;    \
	network_address_size_t address_size

struct network_address_t {
	NETWORK_DECLARE_NETWORK_ADDRESS;
	struct sockaddr_storage saddr;
};

#define NETWORK_DECLARE_NETWORK_ADDRESS_IP NETWORK_DECLARE_NETWORK_ADDRESS

typedef struct network_address_ip_t {
	NETWORK_DECLARE_NETWORK_ADDRESS_IP;
	union {
		struct sockaddr saddr;  // Aliased to ipv4/ipv6 struct
		struct sockaddr_storage saddr_storage;
	};
} network_address_ip_t;

typedef struct network_address_ipv4_t {
	NETWORK_DECLARE_NETWORK_ADDRESS_IP;
	union {
		struct sockaddr_in saddr;
		struct sockaddr_storage saddr_storage;
	};
} network_address_ipv4_t;

typedef struct network_address_ipv6_t {
	NETWORK_DECLARE_NETWORK_ADDRESS_IP;
	union {
		struct sockaddr_in6 saddr;
		struct sockaddr_storage saddr_storage;
	};
} network_address_ipv6_t;

struct network_poll_slot_t {
	socket_t* sock;
	int fd;
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

struct socket_header_t {
	size_t id;
	size_t size;
};

union socket_data_t {
	void* client;
	socket_header_t header;
};

struct socket_t {
	int fd;

	uint32_t flags : 10;
	uint32_t state : 6;
	uint32_t type : 8;
	uint32_t _unused : 8;

	uint32_t id;

	network_address_family_t family;

	network_address_t* address_local;
	network_address_t* address_remote;

	size_t bytes_read;
	size_t bytes_written;

	socket_open_fn open_fn;
	socket_stream_initialize_fn stream_initialize_fn;

	beacon_t* beacon;
	socket_data_t data;

#if FOUNDATION_PLATFORM_WINDOWS
	void* event;
#endif
};

#define NETWORK_DECLARE_POLL_BASE \
	unsigned int timeout;         \
	size_t sockets_max;           \
	size_t sockets_count

#if FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_ANDROID
#define NETWORK_DECLARE_POLL_PLATFORM \
	NETWORK_DECLARE_POLL_BASE;        \
	int fd_poll;                      \
	struct epoll_event* events
#define NETWORK_DECLARE_POLL_DATA(size) \
	network_poll_slot_t slots[size];    \
	struct epoll_event eventarr[size]
#elif FOUNDATION_PLATFORM_APPLE
#define NETWORK_DECLARE_POLL_PLATFORM \
	NETWORK_DECLARE_POLL_BASE;        \
	struct pollfd* pollfds
#define NETWORK_DECLARE_POLL_DATA(size) \
	network_poll_slot_t slots[size];    \
	struct pollfd pollarr[size]
#else
#define NETWORK_DECLARE_POLL_PLATFORM NETWORK_DECLARE_POLL_BASE
#define NETWORK_DECLARE_POLL_DATA(size) network_poll_slot_t slots[size]
#endif

#define NETWORK_DECLARE_POLL       \
	NETWORK_DECLARE_POLL_PLATFORM; \
	network_poll_slot_t slots[FOUNDATION_FLEXIBLE_ARRAY]

#define NETWORK_DECLARE_FIXEDSIZE_POLL(size) \
	NETWORK_DECLARE_POLL_PLATFORM;           \
	NETWORK_DECLARE_POLL_DATA(size)

struct network_poll_t {
	NETWORK_DECLARE_POLL;
};

struct network_poll_event_t {
	network_event_id event;
	socket_t* socket;
};
