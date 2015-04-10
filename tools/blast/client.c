/* client.c  -  Network blast tool  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

typedef enum blast_client_state_t
{
	BLAST_STATE_FINISHED,
	BLAST_STATE_HANDSHAKE,
	BLAST_STATE_TRANSFER
} blast_client_state_t;

typedef struct blast_client_t
{
	network_address_t**     address;
	object_t*               socks;
	object_t                sock;
	blast_client_state_t    state;
	tick_t                  begin_send;
	tick_t                  last_send;
	uint64_t                seq;
	int                     latency;
	int                     latency_history[10];
} blast_client_t;


blast_client_t* clients = 0;


static tick_t blast_time_elapsed_ms( const tick_t since )
{
	const tick_t elapsed = time_elapsed_ticks( since );
	return ( elapsed * 1000LL ) / time_ticks_per_second();
}


static tick_t blast_timestamp( const tick_t begin_send )
{
	return blast_time_elapsed_ms( begin_send ) & PACKET_TIMESTAMP_MASK;
}


static tick_t blast_timestamp_elapsed_ms( const tick_t begin_send, const tick_t timestamp )
{
	const tick_t current = blast_timestamp( begin_send );
	if( current >= timestamp )
		return current - timestamp;
	return ( PACKET_TIMESTAMP_MASK - timestamp ) + current;
}


static uint64_t blast_seq( uint64_t seq )
{
	return seq & PACKET_SEQ_MASK;
}


static void blast_client_deallocate( blast_client_t* client )
{
	int isock, ssize = 0;

	if( client->sock )
		socket_destroy( client->sock );

	for( isock = 0, ssize = array_size( client->socks ); isock < ssize; ++isock )
		socket_destroy( client->socks[isock] );
	array_deallocate( client->socks );
}


static int blast_client_send_handshake( blast_client_t* client )
{
	int iaddr, addrsize = 0;
	network_datagram_t datagram;
	packet_t packet;

	packet.type = PACKET_HANDSHAKE;
	packet.size = 0;
	packet.timestamp = blast_timestamp( client->begin_send );

	datagram.size = sizeof( packet );
	datagram.data = &packet;

	for( iaddr = 0, addrsize = array_size( client->address ); iaddr < addrsize; ++iaddr )
	{
		packet.seq = blast_seq( client->seq++ );
		udp_socket_sendto( client->socks[iaddr], datagram, client->address[iaddr] );

#if BUILD_ENABLE_LOG
		char* addr = network_address_to_string( client->address[iaddr], true );
		log_infof( HASH_BLAST, "Sent handshake to %s (seq %d, timestamp %lld)", addr, (int)packet.seq, (tick_t)packet.timestamp );
		string_deallocate( addr );
#endif
	}

	client->last_send = time_current();

	return 0;
}

static int blast_client_initialize( blast_client_t* client, network_address_t** address, network_poll_t* poll )
{
	int iaddr, addrsize = 0;

	memset( client, 0, sizeof( blast_client_t ) );
	client->address = address;
	client->state = BLAST_STATE_HANDSHAKE;
	client->begin_send = time_current();

	for( iaddr = 0, addrsize = array_size( client->address ); iaddr < addrsize; ++iaddr )
	{
		object_t sock = udp_socket_create();
		array_push( client->socks, sock );

		socket_set_blocking( sock, false );

		network_poll_add_socket( poll, sock );
	}

	return BLAST_RESULT_OK;
}


static int blast_client_handshake( blast_client_t* client, network_poll_t* poll )
{
	int isock, ssize;
	for( isock = 0, ssize = array_size( client->socks ); isock < ssize; ++isock )
	{
		const network_address_t* address = 0;
		network_datagram_t datagram = udp_socket_recvfrom( client->socks[isock], &address );
		if( datagram.size > 0 )
		{
			packet_t* packet = (packet_t*)datagram.data;
			if( packet->type == PACKET_HANDSHAKE )
			{
				if( !client->sock )
				{
					char* addr = network_address_to_string( address, true );
					log_infof( HASH_BLAST, "Got handshake packet from %s (seq %d, timestamp %lld, latency %lld ms)", addr, (int)packet->seq, (tick_t)packet->timestamp, blast_timestamp_elapsed_ms( client->begin_send, packet->timestamp ) );
					string_deallocate( addr );

					client->sock = client->socks[isock];
					client->socks[isock] = 0;

					client->state = BLAST_STATE_TRANSFER;
				}
			}
			else
			{
				log_warnf( HASH_BLAST, WARNING_SUSPICIOUS, "Unknown datagram on socket in handshake state" );
			}
		}
	}

	if( client->state != BLAST_STATE_HANDSHAKE )
	{
		for( isock = 0, ssize = array_size( client->socks ); isock < ssize; ++isock )
		{
			object_t sock = client->socks[isock];
			if( sock )
			{
				network_poll_remove_socket( poll, sock );
				socket_destroy( sock );
			}
		}
		array_deallocate( client->socks );
		client->socks = 0;
	}
	else
	{
		if( blast_time_elapsed_ms( client->last_send ) > 10 )
			blast_client_send_handshake( client );
	}

	return 1;
}


static int blast_client_transfer( blast_client_t* client, network_poll_t* poll )
{
	const network_address_t* address = 0;
	network_datagram_t datagram = udp_socket_recvfrom( client->sock, &address );
	while( datagram.size > 0 )
	{
		packet_t* packet = (packet_t*)datagram.data;
		if( network_address_equal( address, socket_address_remote( client->sock ) ) )
		{
			if( packet->type == PACKET_ACK )
			{

			}
			else if( packet->type == PACKET_TERMINATE )
			{
				log_info( HASH_BLAST, "Client terminating due to TERMINATE packet from server" );
				client->state = BLAST_STATE_FINISHED;
				break;
			}
		}
		else
		{
			char* addr = network_address_to_string( address, true );
			log_warnf( HASH_BLAST, WARNING_SUSPICIOUS, "Ignoring datagram from unknown host %s", addr );
			string_deallocate( addr );
		}

		datagram = udp_socket_recvfrom( client->sock, &address );
	}

	//Send data

	FOUNDATION_UNUSED( poll );

	return 1;
}


static int blast_client_process( blast_client_t* client, network_poll_t* poll )
{
	switch( client->state )
	{
		case BLAST_STATE_HANDSHAKE:
			return blast_client_handshake( client, poll );

		case BLAST_STATE_TRANSFER:
			return blast_client_transfer( client, poll );

		default:
			break;
	}

	return 0;
}


int blast_client( network_address_t*** target, char** files )
{
	int itarg, tsize = 0;
	int iclient, csize = 0;
	bool running = true;
	int result = BLAST_RESULT_OK;
	network_poll_t* poll = 0;

	FOUNDATION_UNUSED( files );

	poll = network_poll_allocate( array_size( clients ) );

	for( itarg = 0, tsize = array_size( target ); itarg < tsize; ++itarg )
	{
		blast_client_t client;
		if( blast_client_initialize( &client, target[itarg], poll ) == BLAST_RESULT_OK )
		{
			network_poll_set_timeout( poll, 0 );
			network_poll( poll );

			array_push( clients, client );

			blast_client_send_handshake( &client );
		}
	}

	while( running && !blast_should_exit() )
	{
		running = false;

		network_poll_set_timeout( poll, 1 );
		network_poll( poll );

		for( iclient = 0, csize = array_size( clients ); iclient < csize; ++iclient )
			running |= blast_client_process( clients + iclient, poll );

		blast_process_system_events();
	}

	network_poll_deallocate( poll );

	for( iclient = 0, csize = array_size( clients ); iclient < csize; ++iclient )
		blast_client_deallocate( &clients[iclient] );
	array_deallocate( clients );

	return result;
}
