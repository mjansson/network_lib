/* socket.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson
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

/*! \file socket.h
    Socket abstraction */

#include <foundation/platform.h>

#include <network/types.h>

NETWORK_API void
socket_finalize(socket_t* sock);

NETWORK_API void
socket_deallocate(socket_t* sock);

NETWORK_API bool
socket_bind(socket_t* sock, const network_address_t* address);

NETWORK_API bool
socket_connect(socket_t* sock, const network_address_t* address, unsigned int timeoutms);

NETWORK_API void
socket_close(socket_t* sock);

NETWORK_API network_socket_type_t
socket_type(socket_t* sock);

NETWORK_API bool
socket_blocking(const socket_t* sock);

NETWORK_API void
socket_set_blocking(socket_t* sock, bool block);

NETWORK_API bool
socket_reuse_address(const socket_t* sock);

NETWORK_API void
socket_set_reuse_address(socket_t* sock, bool reuse);

NETWORK_API bool
socket_reuse_port(const socket_t* sock);

NETWORK_API void
socket_set_reuse_port(socket_t* sock, bool reuse);

NETWORK_API bool
socket_set_multicast_group(socket_t* sock, network_address_t* address, bool allow_loopback);

NETWORK_API const network_address_t*
socket_address_local(const socket_t* sock);

NETWORK_API const network_address_t*
socket_address_remote(const socket_t* sock);

NETWORK_API socket_state_t
socket_state(const socket_t* sock);

NETWORK_API socket_state_t
socket_poll_state(socket_t* sock);

NETWORK_API int
socket_fd(socket_t* sock);

NETWORK_API size_t
socket_available_read(const socket_t* sock);

NETWORK_API size_t
socket_read(socket_t* sock, void* buffer, size_t size);

NETWORK_API size_t
socket_write(socket_t* sock, const void* buffer, size_t size);

/*! Set beacon to fire when data is available on socket. For listening
sockets the beacon is fired when a connection is available.
\param sock Socket
\param beacon Beacon to fire */
NETWORK_API void
socket_set_beacon(socket_t* sock, beacon_t* beacon);
