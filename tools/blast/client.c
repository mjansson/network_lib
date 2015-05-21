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
#include "client.h"
#include "reader.h"

typedef struct blast_pending_t
{
	uint64_t         seq;
	tick_t           last_send;
} blast_pending_t;

typedef enum blast_client_state_t
{
	BLAST_STATE_FINISHED,
	BLAST_STATE_HANDSHAKE,
	BLAST_STATE_TRANSFER
} blast_client_state_t;

typedef struct blast_client_t
{
	blast_reader_t**          readers;
	int                       current;
	unsigned int              token;
	network_address_t**       address;
	const network_address_t*  target;
	object_t*                 socks;
	object_t                  sock;
	blast_client_state_t      state;
	tick_t                    begin_send;
	tick_t                    last_send;
	uint64_t                  seq;
	int                       latency;
	int                       latency_history[10];
	blast_pending_t*          pending;
	uint64_t                  packets_sent;
	uint64_t                  packets_resent;
	tick_t                    last_progress;
	int                       last_progress_percent;
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


static void blast_client_report_progress( blast_client_t* client, bool force )
{
	int progress = (int)( (real)( (float64_t)( ( client->seq - array_size( client->pending ) ) * PACKET_CHUNK_SIZE ) / (float64_t)client->readers[client->current]->size ) * REAL_C(100.0) );
	if( force || ( progress > ( client->last_progress_percent + 5 ) ) || ( time_elapsed( client->last_progress ) > 1.0f ) )
	{
		if( client->packets_sent > 0 )
		{
			real resend_rate = (real)( (float64_t)client->packets_resent / (float64_t)client->packets_sent ) * REAL_C(100.0);
			log_infof( HASH_BLAST, "Progress: %s %d%% (resend rate %.2" PRIREAL "%% %lld/%lld)", client->readers[client->current]->name, progress, resend_rate, client->packets_resent, client->packets_sent );
		}
		client->last_progress = time_current();
		client->last_progress_percent = progress;
	}
}


static void blast_client_deallocate( blast_client_t* client )
{
	int isock, ssize = 0;

	if( client->sock )
		socket_destroy( client->sock );

	for( isock = 0, ssize = array_size( client->socks ); isock < ssize; ++isock )
		socket_destroy( client->socks[isock] );
	array_deallocate( client->socks );

	array_deallocate( client->pending );
}


static int blast_client_send_handshake( blast_client_t* client, blast_reader_t* reader )
{
	int iaddr, addrsize = 0;
	object_t* socks;
	const network_address_t** addrarr;
	network_datagram_t datagram;
	packet_handshake_t packet;

	packet.type = PACKET_HANDSHAKE;
	packet.token = 0;
	packet.timestamp = blast_timestamp( client->begin_send );
	packet.datasize = reader->size;
	packet.namesize = string_length( reader->name );
	packet.seq = blast_seq( client->seq++ );
	string_copy( packet.name, reader->name, PACKET_NAME_MAXSIZE );

	datagram.size = sizeof( packet_handshake_t ) - ( PACKET_NAME_MAXSIZE - packet.namesize ) + 1;
	datagram.data = &packet;

	if( client->target )
	{
		addrarr = &client->target;
		socks = &client->sock;
		addrsize = 1;
	}
	else
	{
		addrarr = (const network_address_t**)client->address;
		socks = client->socks;
		addrsize = array_size( addrarr );
	}

	for( iaddr = 0; iaddr < addrsize; ++iaddr )
	{
		udp_socket_sendto( socks[iaddr], datagram, addrarr[iaddr] );

#if BUILD_ENABLE_LOG
		char* addr = network_address_to_string( addrarr[iaddr], true );
		log_infof( HASH_BLAST, "Sent handshake to %s (seq %lld, timestamp %lld)", addr, packet.seq, (tick_t)packet.timestamp );
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
	array_reserve( client->pending, 1024 );

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
	object_t* socks;

	if( blast_time_elapsed_ms( client->last_send ) > 10 )
		blast_client_send_handshake( client, client->readers[client->current] );

	if( client->sock )
	{
		socks = &client->sock;
		ssize = 1;
	}
	else
	{
		socks = client->socks;
		ssize = array_size( client->socks );
	}

	for( isock = 0; isock < ssize; ++isock )
	{
		const network_address_t* address = 0;
		network_datagram_t datagram = udp_socket_recvfrom( socks[isock], &address );
		if( datagram.size > 0 )
		{
			packet_t* packet = (packet_t*)datagram.data;
			if( packet->type == PACKET_HANDSHAKE )
			{
				char* addr = network_address_to_string( address, true );
				packet_handshake_t* handshake = (packet_handshake_t*)packet;

				log_infof( HASH_BLAST, "Got handshake packet from %s (seq %d, timestamp %lld, latency %lld ms)", addr, (int)packet->seq, (tick_t)packet->timestamp, blast_timestamp_elapsed_ms( client->begin_send, packet->timestamp ) );

				if( !client->sock )
				{
					client->target = address;
					client->sock = client->socks[isock];
					client->socks[isock] = 0;
				}

				if( client->state == BLAST_STATE_HANDSHAKE )
				{
					log_infof( HASH_BLAST, "Begin transfer of '%s' %lld bytes with token %d to %s", client->readers[client->current]->name, client->readers[client->current]->size, handshake->token, addr );
					client->token = handshake->token;
					client->begin_send = time_current();
					client->last_send = 0;
					client->seq = 0;
					array_clear( client->pending );
					client->state = BLAST_STATE_TRANSFER;
				}

				string_deallocate( addr );
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

	return 1;
}


static int blast_client_process_ack( blast_client_t* client, uint32_t* seq, tick_t timestamp )
{
	int ipend, psize;
	int iack, asize;
	for( iack = 0, asize = PACKET_ACK_COUNT; iack < asize; ++iack )
	{
		for( ipend = 0, psize = array_size( client->pending ); ipend < psize; ++ipend )
		{
			if( client->pending[ipend].seq == seq[iack] )
			{
				array_erase( client->pending, ipend );
				break;
			}
		}
	}
	FOUNDATION_UNUSED( timestamp );

	/*log_infof( HASH_BLAST, "ACK processed, %d pending packets remaining (ack seq %d)", array_size( client->pending ), seq[0] );
	if( array_size( client->pending ) )
	{
		char* buf = 0;
		for( ipend = 0, psize = array_size( client->pending ); ipend < psize; ++ipend )
		{
			buf = string_append( buf, string_from_uint_static( client->pending[ipend].seq, false, 0, 0 ) );
			buf = string_append( buf, " " );
		}
		log_infof( HASH_BLAST, "  %s", buf );
		string_deallocate( buf );
	}*/

	return 0;
}


static int blast_client_send_data_chunk( blast_client_t* client, uint64_t seq )
{
	packet_payload_t packet;
	network_datagram_t datagram;
	void* data;
	int res;

	packet.type = PACKET_PAYLOAD;
	packet.token = client->token;
	packet.timestamp = blast_timestamp( client->begin_send );
	packet.seq = seq;

	data = client->readers[client->current]->map( client->readers[client->current], packet.seq * PACKET_CHUNK_SIZE, PACKET_CHUNK_SIZE );
	if( !data )
	{
		log_errorf( HASH_BLAST, ERROR_SYSTEM_CALL_FAIL, "Unable to map source segment at offset %lld", packet.seq * PACKET_CHUNK_SIZE );
		return BLAST_ERROR_UNABLE_TO_READ_FILE;
	}
	memcpy( packet.data, data, PACKET_CHUNK_SIZE );
	client->readers[client->current]->unmap( client->readers[client->current], data, packet.seq * PACKET_CHUNK_SIZE, PACKET_CHUNK_SIZE );

	datagram.size = sizeof( packet_payload_t );
	datagram.data = &packet;

/*
#if BUILD_ENABLE_LOG
	char* addr = network_address_to_string( client->target, true );
	log_infof( HASH_BLAST, "Send payload to %s (seq %lld, timestamp %lld) token %d (file %d/%d)", addr, packet.seq, (tick_t)packet.timestamp, packet.token, client->current + 1, array_size( client->readers ) );
	string_deallocate( addr );
#endif
*/

	res = udp_socket_sendto( client->sock, datagram, client->target );

	return ( res > 0 ? 0 : -1 );
}


static int blast_client_congest_control( blast_client_t* client, tick_t current )
{
	static float64_t mbps = 20.0;
	static tick_t last_ts = 0;
	static float64_t dt = 0.1;
	if( last_ts )
		dt = time_ticks_to_seconds( time_diff( last_ts, current ) );
	float64_t kbytes = ( mbps * 1024.0 ) * dt;
	FOUNDATION_UNUSED( client );
	return 1024.0 * ( kbytes / (float64_t)PACKET_CHUNK_SIZE );
}


static int blast_client_send_data( blast_client_t* client )
{
	blast_pending_t pending;
	bool only_pending;
	int ret = 0;
	uint64_t timestamp;
	int num_sent = 0;
	int max_sent = 1;

	only_pending = ( client->seq * PACKET_CHUNK_SIZE > client->readers[client->current]->size );
	timestamp = time_current();

	max_sent = blast_client_congest_control( client, timestamp );

	if( array_size( client->pending ) )
	{
		int ipend, psize;
		for( ipend = 0, psize = array_size( client->pending ); ( ipend < psize ) && ( num_sent < max_sent ); ++ipend )
		{
			if( only_pending || ( time_elapsed( client->pending[ipend].last_send ) > 1.0f ) ) //TODO: Resend threshold based on round-trip time
			{
				//log_infof( HASH_BLAST, "Resend packet %lld from resend timeout", (uint64_t)client->pending[ipend].seq );
				ret = blast_client_send_data_chunk( client, client->pending[ipend].seq );
				if( ret < 0 )
					break;
				client->pending[ipend].last_send = timestamp;
				client->packets_resent++;
				client->last_send = timestamp;
				++num_sent;
			}
		}
	}

	while( ( num_sent < max_sent ) && ( client->seq * PACKET_CHUNK_SIZE < client->readers[client->current]->size ) )
	{
		uint64_t seq = blast_seq( client->seq++ );
		ret = blast_client_send_data_chunk( client, seq );
		if( ret < 0 )
			break;
		pending.seq = seq;
		pending.last_send = client->last_send;
		array_push( client->pending, pending );
		client->last_send = timestamp;
		client->packets_sent++;
		++num_sent;

		blast_client_report_progress( client, false );
	}

	only_pending = ( client->seq * PACKET_CHUNK_SIZE > client->readers[client->current]->size );

	if( only_pending )
	{
		if( array_size( client->pending ) == 0 )
		{
			blast_client_report_progress( client, true );
			log_infof( HASH_BLAST, "Transfer complete" );
			if( client->current + 1 >= array_size( client->readers ) )
			{
				log_infof( HASH_BLAST, "All transfers complete" );
				return 0;
			}

			client->current++;
			client->seq = 0;
			client->last_send = 0;
			client->state = BLAST_STATE_HANDSHAKE;
			client->begin_send = timestamp;
		}
	}

	//log_infof( HASH_BLAST, "client send data done" );

	blast_client_report_progress( client, false );

	return 1;
}


static void blast_client_read_ack( blast_client_t* client )
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
				packet_ack_t* ack = (packet_ack_t*)packet;
				blast_client_process_ack( client, ack->ack, packet->timestamp );
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
}


static int blast_client_transfer( blast_client_t* client, network_poll_t* poll )
{
	int ret;

	FOUNDATION_UNUSED( poll );

	socket_set_blocking( client->sock, false );

	blast_client_read_ack( client );
	ret = blast_client_send_data( client );

	return ret;
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
	int ifile, fsize = 0;
	bool running = true;
	int result = BLAST_RESULT_OK;
	network_poll_t* poll = 0;
	blast_reader_t* reader = 0;
	blast_reader_t** readers = 0;

	for( ifile = 0, fsize = array_size( files ); ifile < fsize; ++ifile )
	{
		reader = blast_reader_open( files[ifile] );
		if( !reader )
		{
			log_warnf( HASH_BLAST, WARNING_SUSPICIOUS, "Unable to open reader for: %s", files[ifile] );
			return BLAST_ERROR_UNABLE_TO_OPEN_FILE;
		}

		array_push( readers, reader );
	}

	if( array_size( readers ) == 0 )
	{
		log_warnf( HASH_BLAST, WARNING_BAD_DATA, "No input files given" );
		return BLAST_ERROR_UNABLE_TO_OPEN_FILE;
	}

	poll = network_poll_allocate( array_size( clients ) );

	for( itarg = 0, tsize = array_size( target ); itarg < tsize; ++itarg )
	{
		blast_client_t client;
		if( blast_client_initialize( &client, target[itarg], poll ) == BLAST_RESULT_OK )
		{
			network_poll_set_timeout( poll, 0 );
			network_poll( poll );

			client.readers = readers;

			array_push( clients, client );
		}
	}

	while( running && !blast_should_exit() )
	{
		running = false;

		network_poll_set_timeout( poll, 0 ); //TODO: set to 0, look for would-block sends, poll on writable (and readable)
		network_poll( poll );

		for( iclient = 0, csize = array_size( clients ); iclient < csize; ++iclient )
			running |= blast_client_process( clients + iclient, poll );

		blast_process_system_events();
	}

	network_poll_deallocate( poll );

	for( iclient = 0, csize = array_size( clients ); iclient < csize; ++iclient )
		blast_client_deallocate( &clients[iclient] );
	array_deallocate( clients );

	for( ifile = 0, fsize = array_size( readers ); ifile < fsize; ++ifile )
		blast_reader_close( readers[ifile] );
	array_deallocate( readers );

	return result;
}
