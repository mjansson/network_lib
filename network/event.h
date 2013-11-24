/* event.h  -  Network library  -  Internal use only  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams.
 *
 * All rights reserved. No part of this library, code or built products may be used without
 * the explicit consent from Rampant Pixels AB
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
