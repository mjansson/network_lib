/* udp.h  -  Network library  -  Public Domain  -  2014 Mattias Jansson
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

/*! \file udp.h
    UDP socket abstraction */

#include <foundation/platform.h>

#include <network/types.h>
#include <network/socket.h>

NETWORK_API socket_t*
udp_socket_allocate(void);

NETWORK_API void
udp_socket_initialize(socket_t* sock);

NETWORK_API size_t
udp_socket_recvfrom(socket_t* sock, void* buffer, size_t capacity, network_address_t const** address);

NETWORK_API size_t
udp_socket_sendto(socket_t* sock, const void* buffer, size_t size, const network_address_t* address);
