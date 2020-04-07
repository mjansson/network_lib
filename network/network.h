/* network.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson
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

/*! \file network.h
    Network module entry points */

#include <foundation/platform.h>

#include <network/types.h>
#include <network/hashstrings.h>
#include <network/address.h>
#include <network/poll.h>
#include <network/socket.h>
#include <network/stream.h>
#include <network/tcp.h>
#include <network/udp.h>

/*! Initialize network functionality. Must be called prior to any other network
module API calls.
\param config Network configuration
\return       0 if initialization successful, <0 if error */
NETWORK_API int
network_module_initialize(const network_config_t config);

/*! Terminate all network functionality. Must match a call to #network_module_initialize.
After this function has been called no network module API calls can be made until
another call to #network_module_initialize. */
NETWORK_API void
network_module_finalize(void);

/*! Query if network module is initialized properly
\return true if initialized, false if not */
NETWORK_API bool
network_module_is_initialized(void);

/*! Query network config
\return Network module config */
NETWORK_API network_config_t
network_module_config(void);

NETWORK_API void
network_module_parse_config(const char* path, size_t path_size, const char* buffer, size_t size,
                            const json_token_t* tokens, size_t num_tokens);

/*! Query network module build version
\return Version of network module */
NETWORK_API version_t
network_module_version(void);

/*! Query if IPv4 is supported
\return true if IPv4 is supported, false if not */
NETWORK_API bool
network_supports_ipv4(void);

/*! Query if IPv6 is supported
\return true if IPv6 is supported, false if not */
NETWORK_API bool
network_supports_ipv6(void);
