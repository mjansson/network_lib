/* socket.h  -  Network library  -  Internal use only  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams.
 *
 * All rights reserved. No part of this library, code or built products may be used without
 * the explicit consent from Rampant Pixels AB
 * 
 */

#pragma once

/*! \file socket.h
    Socket abstraction */

#include <foundation/platform.h>

#include <network/types.h>


NETWORK_API void                          socket_free( object_t id );

NETWORK_API bool                          socket_bind( object_t id, const network_address_t* address );
NETWORK_API bool                          socket_connect( object_t id, const network_address_t* address, unsigned int timeoutms );
NETWORK_API void                          socket_close( object_t id );

NETWORK_API bool                          socket_blocking( object_t id );
NETWORK_API void                          socket_set_blocking( object_t id, bool block );

NETWORK_API const network_address_t*      socket_address_local( object_t id );
NETWORK_API const network_address_t*      socket_address_remote( object_t id );

NETWORK_API socket_state_t                socket_state( object_t id );

NETWORK_API stream_t*                     socket_stream( object_t id );

NETWORK_API bool                          socket_is_socket( object_t id );
