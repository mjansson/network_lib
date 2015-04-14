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


static uint64_t _blast_server_token = 0;


static void blast_server_read( object_t sock )
{
	const network_address_t* address = 0;
	network_datagram_t datagram = udp_socket_recvfrom( sock, &address );
	while( datagram.size > 0 )
	{
		packet_t* packet = (packet_t*)datagram.data;
		if( packet->type == PACKET_HANDSHAKE )
		{
			packet_handshake_t* handshake = (packet_handshake_t*)packet;
			char* addr = network_address_to_string( address, true );

			if( handshake->datasize > PACKET_DATA_MAXSIZE )
				log_warnf( HASH_BLAST, WARNING_BAD_DATA, "Invalid data size %lld from %s", handshake->datasize, addr );
			else if( !handshake->namesize || handshake->namesize > PACKET_NAME_MAXSIZE )
				log_warnf( HASH_BLAST, WARNING_BAD_DATA, "Invalid name size %d from %s", handshake->namesize, addr );
			else
			{
				log_infof( HASH_BLAST, "Got handshake packet from %s (seq %d, timestamp %lld)", addr, (int)packet->seq, (tick_t)packet->timestamp );

				handshake->token = ( ++_blast_server_token ) & PACKET_TOKEN_MASK;
				log_infof( HASH_BLAST, "Begin transfer of '%s' size %lld with token %d from %s", handshake->name, handshake->datasize, handshake->token, addr );

				udp_socket_sendto( sock, datagram, address );
			}
			string_deallocate( addr );
		}
		else
		{
			log_warnf( HASH_BLAST, WARNING_SUSPICIOUS, "Unknown datagram on socket in handshake state" );
		}
		datagram = udp_socket_recvfrom( sock, &address );
	}
}


static void blast_server_process_network_events( void )
{
	event_block_t* block;
	event_t* event = 0;

	block = event_stream_process( network_event_stream() );

	while( ( event = event_next( block, event ) ) )
	{
		switch( event->id )
		{
			case NETWORKEVENT_DATAIN:
			{
				blast_server_read( network_event_socket( event ) );
				break;
			}

			default:
				break;
		}
	}
}


static int blast_server_run( bool daemon, network_poll_t* poll )
{
	int result = BLAST_RESULT_OK;

    if( daemon )
    {
        config_set_bool( HASH_APPLICATION, HASH_DAEMON, true );
        //TODO: Implement
    }

    network_poll_set_timeout( poll, -1 );

	while( !blast_should_exit() )
	{
		network_poll( poll );
		blast_server_process_network_events();
		blast_process_system_events();
	}

	return result;
}


int blast_server( network_address_t** bind, bool daemon )
{
	int isock, asize, added = 0;
	int result = BLAST_RESULT_OK;
	unsigned int port;
	network_poll_t* poll = 0;

	poll = network_poll_allocate( array_size( bind ) );

	for( isock = 0, asize = array_size( bind ); isock < asize; ++isock )
	{
		object_t sock = udp_socket_create();

		socket_set_blocking( sock, false );

		if( !network_address_ip_port( bind[isock] ) && port )
			network_address_ip_set_port( bind[isock], port );

		if( socket_bind( sock, bind[isock] ) )
		{
			const network_address_t* address = socket_address_local( sock );
			char* addr = network_address_to_string( address, true );
			log_infof( HASH_BLAST, "Listening to %s", addr );
			string_deallocate( addr );

			network_poll_add_socket( poll, sock );

			if( !port )
				port = network_address_ip_port( address );

			++added;
		}

		socket_destroy( sock );
	}

	if( added )
		result = blast_server_run( daemon, poll );
	else
		log_warnf( HASH_BLAST, WARNING_SUSPICIOUS, "No bind address given" );

	network_poll_deallocate( poll );

	return result;
}
