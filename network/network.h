/* network.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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
