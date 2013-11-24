/* address.h  -  Network library  -  Internal use only  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams.
 *
 * All rights reserved. No part of this library, code or built products may be used without
 * the explicit consent from Rampant Pixels AB
 * 
 */

#pragma once

/*! \file address.h
    Network address abstraction and resolution */

#include <foundation/platform.h>

#include <network/types.h>


NETWORK_API network_address_t*         network_address_clone( const network_address_t* address );

NETWORK_API network_address_t**        network_address_resolve( const char* address );
NETWORK_API char*                      network_address_to_string( const network_address_t* address, bool numeric );

NETWORK_API network_address_t*         network_address_ipv4_any( void );
NETWORK_API network_address_t*         network_address_ipv6_any( void );

NETWORK_API void                       network_address_ip_set_port( network_address_t* address, unsigned int port );
NETWORK_API unsigned int               network_address_ip_port( const network_address_t* address );

NETWORK_API network_address_family_t   network_address_family( const network_address_t* address );

NETWORK_API network_address_t**        network_address_local( void );

NETWORK_API bool                       network_address_equal( const network_address_t* first, const network_address_t* second );

NETWORK_API void                       network_address_array_deallocate( network_address_t** addresses );
