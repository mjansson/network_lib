/* network.h  -  Network library  -  Internal use only  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams.
 *
 * All rights reserved. No part of this library, code or built products may be used without
 * the explicit consent from Rampant Pixels AB
 * 
 */

#pragma once

/*! \file network.h
    Network library entry points */

#include <foundation/platform.h>

#include <network/types.h>
#include <network/hashstrings.h>
#include <network/address.h>
#include <network/poll.h>
#include <network/socket.h>
#include <network/tcp.h>


//! Entry point
NETWORK_API int         network_initialize( unsigned int max_sockets );

//! Exit point
NETWORK_API void        network_shutdown( void );
