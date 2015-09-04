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
#include <network/event.h>
#include <network/poll.h>
#include <network/socket.h>
#include <network/tcp.h>
#include <network/udp.h>

//! Entry point
NETWORK_API int
network_initialize(network_config_t config);

NETWORK_API bool
network_is_initialized(void);

//! Exit point
NETWORK_API void
network_finalize(void);

NETWORK_API version_t
network_version(void);

NETWORK_API bool
network_supports_ipv4(void);

NETWORK_API bool
network_supports_ipv6(void);
