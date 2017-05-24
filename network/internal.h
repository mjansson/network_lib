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
	SOCKETFLAG_REUSE_ADDR           = 0x00000004,
	SOCKETFLAG_REUSE_PORT           = 0x00000008
} socket_flag_t;

#if FOUNDATION_PLATFORM_WINDOWS
#  define NETWORK_SOCKET_ERROR ((int)WSAGetLastError())
#  define NETWORK_RESOLV_ERROR NETWORK_SOCKET_ERROR
#else //elif FOUNDATION_PLATFORM_POSIX
#  define NETWORK_SOCKET_ERROR errno
#  define NETWORK_RESOLV_ERROR NETWORK_SOCKET_ERROR
#endif

NETWORK_EXTERN network_config_t  _network_config;

NETWORK_API int
_socket_create_fd(socket_t* sock, network_address_family_t family);

NETWORK_API void
_socket_initialize(socket_t* sock);

NETWORK_API void
_socket_close_fd(int fd);

NETWORK_API void
_socket_store_address_local(socket_t* sock, int family);

NETWORK_API int
_socket_available_fd(int fd);

NETWORK_API int
socket_streams_initialize(void);
