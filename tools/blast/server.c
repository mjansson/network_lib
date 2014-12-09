/* server.c  -  Network blast tool  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 * 
 * https://github.com/rampantpixels/network_lib
 * 
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include "blast.h"


static int blast_server_run( bool daemon, network_poll_t* poll )
{
	int result = BLAST_RESULT_OK;

    if( daemon )
    {
        config_set_bool( HASH_APPLICATION, HASH_DAEMON, true );
        //TODO: Implement
    }
    
	while( !blast_should_exit() )
	{
		network_poll( poll );
		blast_process_system_events();
	}

	return result;
}


int blast_server( network_address_t** bind, bool daemon )
{
	int isock, asize, added = 0;
	int result = BLAST_RESULT_OK;
	network_poll_t* poll = 0;

	poll = network_poll_allocate( array_size( bind ), 10000 );

	for( isock = 0, asize = array_size( bind ); isock < asize; ++isock )
	{
		object_t sock = udp_socket_create();

		if( socket_bind( sock, bind[isock] ) )
		{
			char* address = network_address_to_string( bind[isock], true );
			log_infof( HASH_BLAST, "Listening to %s", address );
			string_deallocate( address );

			network_poll_add_socket( poll, sock );

			++added;

			log_set_suppress( HASH_NETWORK, ERRORLEVEL_WARNING );
		}

		socket_destroy( sock );
	}

	log_set_suppress( HASH_NETWORK, ERRORLEVEL_INFO );

	if( added )
		result = blast_server_run( daemon, poll );
	else
		log_warnf( HASH_BLAST, WARNING_SUSPICIOUS, "No bind address given" );

	network_poll_deallocate( poll );

	return result;
}
