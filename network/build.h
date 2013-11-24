/* build.h  -  Network library  -  Internal use only  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams.
 *
 * All rights reserved. No part of this library, code or built products may be used without
 * the explicit consent from Rampant Pixels AB
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
