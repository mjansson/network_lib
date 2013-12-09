/* build.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

/*! \file build.h
    Build setup */

#include <foundation/platform.h>

#include <network/types.h>


//! Dump network traffic to log (debug). Dump read/write information if > 0, dump full traffic (payload data) if > 1
#define BUILD_ENABLE_NETWORK_DUMP_TRAFFIC     0


// Allocation sizes
#define BUILD_SIZE_NETWORK_EVENT_STREAM       4096

#define BUILD_SIZE_SOCKET_WRITEBUFFER         8192
#define BUILD_SIZE_SOCKET_READBUFFER          8192

#define BUILD_SIZE_POLL_QUEUE                 32
