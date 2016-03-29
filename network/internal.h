/* internal.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

/*! \file internal.h
    Internal types */

#include <foundation/platform.h>
#include <foundation/types.h>
#include <foundation/internal.h>

#include <network/types.h>
#include <network/hashstrings.h>

#if FOUNDATION_PLATFORM_WINDOWS
#  include <foundation/windows.h>
#elif FOUNDATION_PLATFORM_POSIX
#  include <foundation/posix.h>
#  include <fcntl.h>
#  include <sys/select.h>
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <netdb.h>
#  if !FOUNDATION_PLATFORM_ANDROID
#    include <ifaddrs.h>
#  endif
#endif

#if FOUNDATION_PLATFORM_ANDROID
#  ifndef SO_REUSEPORT
#    if FOUNDATION_ARCH_MIPS
#      define SO_REUSEPORT 0x0200
#    else
#      define SO_REUSEPORT 15
#    endif
#  endif
#endif

typedef enum {
	SOCKETFLAG_BLOCKING             = 0x00000001,
	SOCKETFLAG_TCPDELAY             = 0x00000002,
	SOCKETFLAG_POLLED               = 0x00000004,
	SOCKETFLAG_CONNECTION_PENDING   = 0x00000008,
	SOCKETFLAG_ERROR_PENDING        = 0x00000010,
	SOCKETFLAG_HANGUP_PENDING       = 0x00000020,
	SOCKETFLAG_REFLUSH              = 0x00000040,
	SOCKETFLAG_REUSE_ADDR           = 0x00000080,
	SOCKETFLAG_REUSE_PORT           = 0x00000100
} socket_flag_t;

#define NETWORK_DECLARE_NETWORK_ADDRESS_IP   \
	NETWORK_DECLARE_NETWORK_ADDRESS

typedef struct network_address_ip_t {
	NETWORK_DECLARE_NETWORK_ADDRESS_IP;
	struct sockaddr        saddr; //Aliased to ipv4/ipv6 struct
} network_address_ip_t;

typedef struct network_address_ipv4_t {
	NETWORK_DECLARE_NETWORK_ADDRESS_IP;
	struct sockaddr_in     saddr;
} network_address_ipv4_t;

typedef struct network_address_ipv6_t {
	NETWORK_DECLARE_NETWORK_ADDRESS_IP;
	struct sockaddr_in6    saddr;
} network_address_ipv6_t;

#if FOUNDATION_PLATFORM_WINDOWS
#  define NETWORK_SOCKET_ERROR ((int)WSAGetLastError())
#  define NETWORK_RESOLV_ERROR NETWORK_SOCKET_ERROR
#  define SOCKET_INVALID -1
#else //elif FOUNDATION_PLATFORM_POSIX
#  define NETWORK_SOCKET_ERROR errno
#  define NETWORK_RESOLV_ERROR NETWORK_SOCKET_ERROR
#  define SOCKET_INVALID -1
#endif

typedef struct socket_base_t    socket_base_t;
typedef struct socket_t         socket_t;
typedef struct socket_stream_t  socket_stream_t;

typedef void (*socket_open_fn)(socket_t*, unsigned int);
typedef int (*socket_connect_fn)(socket_t*, const network_address_t*, unsigned int);
typedef size_t (*socket_buffer_read_fn)(socket_t*, size_t);
typedef size_t (*socket_buffer_write_fn)(socket_t*);
typedef void (*socket_stream_initialize_fn)(socket_t*, stream_t*);

FOUNDATION_ALIGNED_STRUCT(socket_stream_t, 16) {
	FOUNDATION_DECLARE_STREAM;
	object_t socket;
};

FOUNDATION_ALIGNED_STRUCT(socket_base_t, 16) {
	object_t object;
	int      fd;
	uint32_t flags: 10;
	uint32_t state: 6;
	uint32_t last_event: 16;
};

FOUNDATION_ALIGNED_STRUCT(socket_t, 16) {
	FOUNDATION_DECLARE_OBJECT;

	int base;
	network_address_family_t family;

	network_address_t* address_local;
	network_address_t* address_remote;

	size_t offset_read_in;
	size_t offset_write_in;
	size_t offset_write_out;
	size_t bytes_read;
	size_t bytes_written;

	socket_open_fn open_fn;
	socket_connect_fn connect_fn;
	socket_buffer_read_fn read_fn;
	socket_buffer_write_fn write_fn;
	socket_stream_initialize_fn stream_initialize_fn;

	socket_stream_t* stream;

	uint8_t* buffer_in;
	uint8_t* buffer_out;
	uint8_t  buffers[];
};

NETWORK_EXTERN network_config_t  _network_config;
NETWORK_EXTERN socket_base_t*    _socket_base;
NETWORK_EXTERN int32_t           _socket_base_size;
NETWORK_EXTERN atomic32_t        _socket_base_next;

NETWORK_API int
_socket_create_fd(socket_t* sock, network_address_family_t family);

NETWORK_API socket_t*
_socket_allocate(void);

NETWORK_API int
_socket_allocate_base(socket_t* sock);

NETWORK_API socket_t*
_socket_lookup(object_t id);

NETWORK_API void
_socket_close(socket_t* sock);

NETWORK_API void
_socket_close_fd(int fd);

NETWORK_API void
_socket_set_blocking(socket_t* sock, bool block);

NETWORK_API void
_socket_set_reuse_address(socket_t* sock, bool reuse);

NETWORK_API void
_socket_set_reuse_port(socket_t* sock, bool reuse);

NETWORK_API void
_socket_store_address_local(socket_t* sock, int family);

NETWORK_API int
_socket_available_fd(int fd);

NETWORK_API size_t
_socket_available_nonblock_read(const socket_t* sock);

NETWORK_API socket_state_t
_socket_poll_state(socket_base_t* sockbase);

NETWORK_API int
socket_initialize(size_t max_sockets);

NETWORK_API void
socket_finalize(void);

NETWORK_API int
network_event_initialize(void);

NETWORK_API void
network_event_finalize(void);
