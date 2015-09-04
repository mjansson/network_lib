/* tcp.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

/*! \file tcp.h
    TCP socket abstraction */

#include <foundation/platform.h>

#include <network/types.h>
#include <network/socket.h>

NETWORK_API object_t
tcp_socket_create(void);

NETWORK_API object_t
tcp_socket_accept(object_t id, unsigned int timeoutms);

NETWORK_API bool
tcp_socket_listen(object_t id);

NETWORK_API bool
tcp_socket_delay(object_t id);

NETWORK_API void
tcp_socket_set_delay(object_t id, bool delay);
