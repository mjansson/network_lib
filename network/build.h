/* build.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/mjansson/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#pragma once

/*! \file build.h
    Build setup */

#include <foundation/platform.h>

#include <network/types.h>

/*! Dump network traffic to log (debug). Dump read/write information if > 0,
dump full traffic (payload data) if > 1 */
#define BUILD_ENABLE_NETWORK_DUMP_TRAFFIC 0
