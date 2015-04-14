/* udp.h  -  Network library  -  Public Domain  -  2014 Mattias Jansson / Rampant Pixels
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

/*! \file udp.h
    UDP socket abstraction */

#include <foundation/platform.h>

#include <network/types.h>
#include <network/socket.h>


NETWORK_API object_t                      udp_socket_create( void );

NETWORK_API network_datagram_t            udp_socket_recvfrom( object_t sock, network_address_t const** address );
NETWORK_API uint64_t                      udp_socket_sendto( object_t sock, const network_datagram_t datagram, const network_address_t* address );
