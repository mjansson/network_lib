/* event.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

/*! \file event.h
    Network events */

#include <foundation/platform.h>

#include <network/types.h>


//! Get network event stream
NETWORK_API event_stream_t*         network_event_stream( void );

//! Post network event
NETWORK_API void                    network_event_post( network_event_id id, object_t sock );

//! Get event socket
NETWORK_API object_t                network_event_socket( event_t* event );
