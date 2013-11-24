/* network.c  -  Network library  -  Internal use only  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams.
 *
 * All rights reserved. No part of this library, code or built products may be used without
 * the explicit consent from Rampant Pixels AB
 * 
 */

#include <network/network.h>

#include <foundation/foundation.h>

#if FOUNDATION_PLATFORM_WINDOWS
#  include <foundation/windows.h>
#endif


NETWORK_EXTERN int   _socket_initialize( unsigned int max_sockets );
NETWORK_EXTERN void  _socket_shutdown( void );

NETWORK_EXTERN int   _network_event_initialize( void );
NETWORK_EXTERN void  _network_event_shutdown( void );


int network_initialize( unsigned int max_sockets )
{
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
	
	return 0;
}


void network_shutdown( void )
{
	log_debug( HASH_NETWORK, "Terminating network services" );

	_socket_shutdown();
	_network_event_shutdown();
	
#if FOUNDATION_PLATFORM_WINDOWS
	WSACleanup();
#endif
}
