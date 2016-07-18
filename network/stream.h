/* stream.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

/*! \file stream.h
    Socket stream abstraction */

#include <foundation/platform.h>

#include <network/types.h>

NETWORK_API stream_t*
socket_stream_allocate(socket_t* sock, size_t buffer_in, size_t buffer_out);

NETWORK_API void
socket_stream_initialize(socket_stream_t* stream, socket_t* sock);

NETWORK_API void
socket_stream_finalize(socket_stream_t* stream);
