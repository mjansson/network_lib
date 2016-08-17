/* poll.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

/*! \file poll.h
    Socket polling */

#include <foundation/platform.h>

#include <network/types.h>

NETWORK_API network_poll_t*
network_poll_allocate(unsigned int num_sockets);

NETWORK_API void
network_poll_initialize(network_poll_t* poll, unsigned int num_sockets);

NETWORK_API void
network_poll_finalize(network_poll_t* poll);

NETWORK_API void
network_poll_deallocate(network_poll_t* poll);

NETWORK_API bool
network_poll_add_socket(network_poll_t* poll, socket_t* sock);

NETWORK_API void
network_poll_update_socket(network_poll_t* poll, socket_t* sock);

NETWORK_API void
network_poll_remove_socket(network_poll_t* poll, socket_t* sock);

NETWORK_API bool
network_poll_has_socket(network_poll_t* poll, socket_t* sock);

NETWORK_API size_t
network_poll_num_sockets(network_poll_t* poll);

NETWORK_API void
network_poll_sockets(network_poll_t* poll, socket_t** sockets, size_t max_sockets);

NETWORK_API size_t
network_poll(network_poll_t* poll, network_poll_event_t* event, size_t capacity,
             unsigned int timeoutms);

