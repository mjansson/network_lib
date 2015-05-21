/* socket.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

/*! \file socket.h
    Socket abstraction */

#include <foundation/platform.h>

#include <network/types.h>


NETWORK_API object_t                      socket_ref( object_t id );
NETWORK_API void                          socket_destroy( object_t id );

NETWORK_API bool                          socket_bind( object_t id, const network_address_t* address );
NETWORK_API bool                          socket_connect( object_t id, const network_address_t* address, unsigned int timeoutms );
NETWORK_API void                          socket_close( object_t id );

NETWORK_API bool                          socket_blocking( object_t id );
NETWORK_API void                          socket_set_blocking( object_t id, bool block );

NETWORK_API bool                          socket_reuse_address( object_t id );
NETWORK_API void                          socket_set_reuse_address( object_t id, bool reuse );

NETWORK_API bool                          socket_reuse_port( object_t id );
NETWORK_API void                          socket_set_reuse_port( object_t id, bool reuse );

NETWORK_API bool                          socket_set_multicast_group( object_t id, network_address_t* address, bool allow_loopback );

NETWORK_API const network_address_t*      socket_address_local( object_t id );
NETWORK_API const network_address_t*      socket_address_remote( object_t id );

NETWORK_API socket_state_t                socket_state( object_t id );

NETWORK_API stream_t*                     socket_stream( object_t id );

NETWORK_API bool                          socket_is_socket( object_t id );
