/* address.h  -  Network library  -  Public Domain  -  2013 Mattias Jansson
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

/*! \file address.h
    Network address abstraction and resolution */

#include <foundation/platform.h>

#include <network/types.h>

NETWORK_API network_address_t*
network_address_clone(const network_address_t* address);

NETWORK_API network_address_t**
network_address_resolve(const char* address, size_t length);

NETWORK_API string_t
network_address_to_string(char* buffer, size_t capacity, const network_address_t* address, bool numeric);

NETWORK_API network_address_family_t
network_address_type(const network_address_t* address);

NETWORK_API network_address_family_t
network_address_family(const network_address_t* address);

NETWORK_API network_address_t**
network_address_local(void);

NETWORK_API bool
network_address_equal(const network_address_t* first, const network_address_t* second);

NETWORK_API void
network_address_deallocate(network_address_t* address);

NETWORK_API void
network_address_array_deallocate(network_address_t** addresses);

/*! Initialize the IPv4 address structure to denote any address and no port
\param address IPv4 address structure
\return Initialized address as a base structure pointer */
NETWORK_API network_address_t*
network_address_ipv4_initialize(network_address_ipv4_t* address);

/*! Initialize the IPv6 address structure to denote any address and no port
\param address IPv6 address structure
\return Initialized address as a base structure pointer */
NETWORK_API network_address_t*
network_address_ipv6_initialize(network_address_ipv6_t* address);

NETWORK_API void
network_address_ip_set_port(network_address_t* address, unsigned int port);

NETWORK_API unsigned int
network_address_ip_port(const network_address_t* address);

NETWORK_API void
network_address_ipv4_set_ip(network_address_t* address, uint32_t ip);

NETWORK_API uint32_t
network_address_ipv4_ip(const network_address_t* address);

NETWORK_API void
network_address_ipv6_set_ip(network_address_t* address, struct in6_addr ip);

NETWORK_API struct in6_addr
network_address_ipv6_ip(const network_address_t* address);

NETWORK_API uint32_t
network_address_ipv4_make_ip(unsigned char c0, unsigned char c1, unsigned char c2, unsigned char c3);
