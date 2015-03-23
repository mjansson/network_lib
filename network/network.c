/* network.c  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/rampantpixels/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/network.h>
#include <network/internal.h>

#include <foundation/foundation.h>

#if FOUNDATION_PLATFORM_WINDOWS
#  include <foundation/windows.h>
#endif

static bool _network_initialized;
static bool _network_supports_ipv4;
static bool _network_supports_ipv6;

NETWORK_EXTERN int   _socket_initialize( unsigned int max_sockets );
NETWORK_EXTERN void  _socket_shutdown( void );

NETWORK_EXTERN int   _network_event_initialize( void );
NETWORK_EXTERN void  _network_event_shutdown( void );


int network_initialize( unsigned int max_sockets )
{
	int fd;

	if( _network_initialized )
		return 0;

	if( !max_sockets )
		max_sockets = BUILD_SIZE_DEFALT_NUM_SOCKETS;
	max_sockets = math_clamp( max_sockets, 8, 65535 );

	log_debugf( HASH_NETWORK, "Initializing network services (%u max sockets)", max_sockets );

#if FOUNDATION_PLATFORM_WINDOWS
	{
		WSADATA wsadata;
		int err;
		if( ( err = WSAStartup( 2/*MAKEWORD( 2, 0 )*/, &wsadata ) ) != 0 )
		{
			log_errorf( HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL, "Unable to initialize WinSock: %s", system_error_message( err ) );
			return -1;
		}
	}
#endif

	if( _network_event_initialize() < 0 )
		return -1;

	if( _socket_initialize( max_sockets ) < 0 )
		return -1;

	//Check support
	fd = (int)socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	_network_supports_ipv4 = !( fd < 0 );
	_socket_close_fd( fd );

	fd = (int)socket( AF_INET6, SOCK_DGRAM, IPPROTO_UDP );
	_network_supports_ipv6 = !( fd < 0 );
	_socket_close_fd( fd );

	return 0;
}


bool network_is_initialized( void )
{
	return _network_initialized;
}


void network_shutdown( void )
{
	if( !_network_initialized )
		return;

	log_debug( HASH_NETWORK, "Terminating network services" );

	_socket_shutdown();
	_network_event_shutdown();

#if FOUNDATION_PLATFORM_WINDOWS
	WSACleanup();
#endif
}


bool network_supports_ipv4( void )
{
	return _network_supports_ipv4;
}


bool network_supports_ipv6( void )
{
	return _network_supports_ipv6;
}
