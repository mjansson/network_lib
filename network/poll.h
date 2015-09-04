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
network_poll_deallocate(network_poll_t* poll);

NETWORK_API bool
network_poll_add_socket(network_poll_t* poll, object_t sock);

NETWORK_API void
network_poll_remove_socket(network_poll_t* poll, object_t sock);

NETWORK_API bool
network_poll_has_socket(network_poll_t* poll, object_t sock);

NETWORK_API unsigned int
network_poll_timeout(network_poll_t* poll);

NETWORK_API void
network_poll_set_timeout(network_poll_t* poll, unsigned int timeoutms);

NETWORK_API unsigned int
network_poll_num_sockets(network_poll_t* poll);

NETWORK_API void
network_poll_sockets(network_poll_t* poll, object_t* sockets, unsigned int max_sockets);

NETWORK_API int
network_poll(network_poll_t* poll);

NETWORK_API void*
network_poll_thread(object_t thread, void* poll);

