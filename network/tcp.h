/* tcp.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson
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

/*! \file tcp.h
    TCP socket abstraction */

#include <foundation/platform.h>

#include <network/types.h>
#include <network/socket.h>

NETWORK_API socket_t*
tcp_socket_allocate(void);

NETWORK_API void
tcp_socket_initialize(socket_t* sock);

NETWORK_API socket_t*
tcp_socket_accept(socket_t* sock, unsigned int timeoutms);

NETWORK_API bool
tcp_socket_listen(socket_t* sock);

NETWORK_API bool
tcp_socket_delay(socket_t* sock);

NETWORK_API void
tcp_socket_set_delay(socket_t* sock, bool delay);
