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
#include "server.h"
#include "writer.h"

#define BLAST_SERVER_TIMEOUT 30

typedef struct blast_server_source_t
{
	network_address_t*       address;
	object_t                 sock;
	uint64_t                 token;
	bool                     got_payload;
	tick_t                   last_recv;
	blast_writer_t*          writer;
	uint32_t                 ack[PACKET_ACK_HISTORY];
	int                      ack_offset;
	tick_t                   last_ack;
	int                      last_ack_offset;
} blast_server_source_t;


typedef struct blast_server_t
{
	blast_server_source_t**  sources;
	uint64_t                 token_counter;
} blast_server_t;


static blast_server_source_t* blast_server_source_allocate( void )
{
	blast_server_source_t* source = memory_allocate( HASH_BLAST, sizeof( blast_server_source_t ), 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED );
	memset( source->ack, 0xFF, sizeof( uint32_t ) * PACKET_ACK_HISTORY );
	return source;
}


static void blast_server_source_deallocate( blast_server_source_t* source )
{
	blast_writer_close( source->writer );
	memory_deallocate( source->address );
	memory_deallocate( source );
}


static int blast_server_time_until_ack( blast_server_t* server )
{
	FOUNDATION_UNUSED( server );
	return 10;
}


static void blast_server_send_ack( blast_server_source_t* source )
{
	network_datagram_t datagram;
	packet_ack_t packet;
	int first_acks = 0;
	int second_acks = 0;

	if( !source->writer || !source->got_payload )
	{
		source->last_ack = time_current();
		return;
	}

	packet.type = PACKET_ACK;
	packet.token = source->token;
	packet.timestamp = 0;
	packet.seq = 0;

	if( source->ack_offset < PACKET_ACK_COUNT )
	{
		first_acks = ( PACKET_ACK_COUNT - source->ack_offset );
		memcpy( packet.ack, source->ack + ( PACKET_ACK_HISTORY - first_acks ), sizeof( uint32_t ) * first_acks );
	}
	second_acks = PACKET_ACK_COUNT - first_acks;
	memcpy( packet.ack + first_acks, source->ack + ( source->ack_offset - second_acks ), sizeof( uint32_t ) * second_acks );

	datagram.data = &packet;
	datagram.size = sizeof( packet );

	//log_infof( HASH_BLAST, "Send ACKs [%d:%d up to %d] (ack seq %d) for transfer of '%s' size %lld with token %d", first_acks, second_acks, source->ack_offset, packet.ack[0], source->writer->name, source->writer->size, source->token );

	udp_socket_sendto( source->sock, datagram, source->address );

	source->last_ack = time_current();
	source->last_ack_offset = source->ack_offset;
}


static void blast_server_send_acks( blast_server_t* server )
{
	int isrc, ssize;
	for( isrc = 0, ssize = array_size( server->sources ); isrc < ssize; ++isrc )
	{
		blast_server_source_t* source = server->sources[isrc];
		if( time_elapsed( source->last_ack ) > 0.05f ) //TODO: Base on round-trip time and back-off rate (back-off reset on recv packet)
		{
			blast_server_send_ack( source );
		}
	}
}


static void blast_server_queue_ack( blast_server_source_t* source, uint32_t ack )
{
	int send_trigger;

	//log_infof( HASH_BLAST, "Queue ACK for seq %d in slot %d", ack, source->ack_offset );

	source->ack[source->ack_offset++] = ack;
	if( source->ack_offset >= PACKET_ACK_HISTORY )
		source->ack_offset = 0;

	send_trigger = source->last_ack_offset + PACKET_ACK_COUNT;
	if( send_trigger >= PACKET_ACK_HISTORY )
		send_trigger -= PACKET_ACK_HISTORY;
	if( source->ack_offset == send_trigger )
		blast_server_send_ack( source );
}


static bool blast_server_has_ack( blast_server_source_t* source, uint32_t ack )
{
	int iack;
	for( iack = 0; iack < PACKET_ACK_HISTORY; ++iack )
	{
		if( source->ack[iack] == ack )
			return true;
	}
	return false;
}


static void blast_server_process_handshake( blast_server_t* server, object_t sock, const network_datagram_t datagram, const network_address_t* address )
{
	packet_handshake_t* handshake = (packet_handshake_t*)datagram.data;
	blast_server_source_t* source = 0;
	int isrc, ssize;

	char* addr = network_address_to_string( address, true );

	if( handshake->datasize > PACKET_DATA_MAXSIZE )
	{
		log_warnf( HASH_BLAST, WARNING_BAD_DATA, "Invalid data size %lld from %s", handshake->datasize, addr );
		goto exit;
	}

	if( !handshake->namesize || ( handshake->namesize > PACKET_NAME_MAXSIZE ) )
	{
		log_warnf( HASH_BLAST, WARNING_BAD_DATA, "Invalid name size %d from %s", handshake->namesize, addr );
		goto exit;
	}

	log_infof( HASH_BLAST, "Got handshake packet from %s (seq %d, timestamp %lld)", addr, (int)handshake->seq, (tick_t)handshake->timestamp );

	for( isrc = 0, ssize = array_size( server->sources ); isrc < ssize; ++isrc )
	{
		if( network_address_equal( server->sources[isrc]->address, address ) )
		{
			source = server->sources[isrc];
			break;
		}
	}

	if( source )
	{
		if( source->writer && !string_equal_substr( source->writer->name, handshake->name, handshake->namesize ) )
		{
			log_infof( HASH_BLAST, "Source re-initializing with new writer" );
			blast_writer_close( source->writer );
			source->writer = 0;
		}
	}
	else
	{
		source = blast_server_source_allocate();
		source->sock = sock;
		source->address = network_address_clone( address );
		array_push( server->sources, source );
	}

	if( !source->writer )
	{
		source->writer = blast_writer_open( handshake->name, handshake->namesize, handshake->datasize );
		source->token = ( ++server->token_counter ) & PACKET_TOKEN_MASK;
		source->last_ack = time_current();
		log_infof( HASH_BLAST, "Prepare transfer of '%.*s' size %lld with token %d from %s", (int)handshake->namesize, handshake->name, handshake->datasize, source->token, addr );
	}

	handshake->token = source->token;
	udp_socket_sendto( sock, datagram, address );

	source->last_recv = time_current();

exit:

	string_deallocate( addr );
}


static void blast_server_process_payload( blast_server_t* server, object_t sock, const network_datagram_t datagram, const network_address_t* address )
{
	packet_payload_t* packet = (packet_payload_t*)datagram.data;
	blast_server_source_t* source = 0;
	int isrc, ssize;
	void* buffer;
	uint64_t offset;

	for( isrc = 0, ssize = array_size( server->sources ); isrc < ssize; ++isrc )
	{
		if( ( server->sources[isrc]->sock == sock ) && ( network_address_equal( server->sources[isrc]->address, address ) ) )
		{
			source = server->sources[isrc];
			break;
		}
	}

	if( !source )
	{
		log_warnf( HASH_BLAST, WARNING_SUSPICIOUS, "Got payload from unknown source" );
		return;
	}

	source->got_payload = true;
	source->last_recv = time_current();

	if( !source->writer )
	{
		log_warnf( HASH_BLAST, WARNING_SUSPICIOUS, "Got payload from uninitialized source" );
		return;
	}

	if( blast_server_has_ack( source, packet->seq ) )
	{
		log_infof( HASH_BLAST, "Had previous ACK of seq %lld, ignore write and re-ACK", packet->seq );
		blast_server_queue_ack( source, packet->seq );
		return;
	}

	offset = packet->seq * PACKET_CHUNK_SIZE;
	if( offset >= source->writer->size )
	{
		log_warnf( HASH_BLAST, WARNING_SUSPICIOUS, "Got invalid payload seq %lld out of range", packet->seq );
		return;
	}

	buffer = source->writer->map( source->writer, offset, PACKET_CHUNK_SIZE );
	if( !buffer )
	{
		log_warnf( HASH_BLAST, WARNING_SUSPICIOUS, "Unable to map chunk for payload seq %lld out of range", packet->seq );
		return;
	}

	blast_server_queue_ack( source, packet->seq );

	memcpy( buffer, packet->data, PACKET_CHUNK_SIZE );
	source->writer->unmap( source->writer, buffer, offset, PACKET_CHUNK_SIZE );

	//log_infof( HASH_BLAST, "Wrote payload @ offset %lld (seq %lld) in transfer of '%s' size %lld with token %d", offset, packet->seq, source->writer->name, source->writer->size, source->token );
}


static void blast_server_read( blast_server_t* server, object_t sock )
{
	const network_address_t* address = 0;
	network_datagram_t datagram = udp_socket_recvfrom( sock, &address );
	while( datagram.size > 0 )
	{
		packet_t* packet = (packet_t*)datagram.data;
		if( packet->type == PACKET_HANDSHAKE )
		{
			blast_server_process_handshake( server, sock, datagram, address );
		}
		else if( packet->type == PACKET_PAYLOAD )
		{
			blast_server_process_payload( server, sock, datagram, address );
		}
		else
		{
			log_warnf( HASH_BLAST, WARNING_SUSPICIOUS, "Unknown datagram on socket" );
		}
		datagram = udp_socket_recvfrom( sock, &address );
	}
}


static void blast_server_process_network_events( blast_server_t* server )
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
				blast_server_read( server, network_event_socket( event ) );
				break;
			}

			default:
				break;
		}
	}
}


static void blast_server_tick( blast_server_t* server )
{
	int isrc, ssize;
	for( isrc = 0, ssize = array_size( server->sources ); isrc < ssize; )
	{
		if( time_elapsed( server->sources[isrc]->last_recv ) > BLAST_SERVER_TIMEOUT )
		{
			char* addr = network_address_to_string( server->sources[isrc]->address, true );
			log_infof( HASH_BLAST, "Deleting inactive source from %s", addr );
			string_deallocate( addr );

			blast_server_source_deallocate( server->sources[isrc] );

			array_erase( server->sources, isrc );
			--ssize;
		}
		else
		{
			blast_server_send_acks( server );
			++isrc;
		}
	}
}


static int blast_server_run( bool daemon, network_poll_t* poll, blast_server_t* server )
{
	int result = BLAST_RESULT_OK;

    if( daemon )
    {
        config_set_bool( HASH_APPLICATION, HASH_DAEMON, true );
        //TODO: Implement
    }

	while( !blast_should_exit() )
	{
	    network_poll_set_timeout( poll, blast_server_time_until_ack( server ) );
		network_poll( poll );

		blast_server_process_network_events( server );
		blast_process_system_events();

		blast_server_tick( server );
	}

	return result;
}


static blast_server_t* blast_server_allocate( void )
{
	return (blast_server_t*)memory_allocate( HASH_BLAST, sizeof( blast_server_t ), 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED );
}


static void blast_server_deallocate( blast_server_t* server )
{
	int isrc, ssize;
	for( isrc = 0, ssize = array_size( server->sources ); isrc < ssize; ++isrc )
		blast_server_source_deallocate( server->sources[isrc] );
	memory_deallocate( server );
}


int blast_server( network_address_t** bind, bool daemon )
{
	int isock, asize, added = 0;
	int result = BLAST_RESULT_OK;
	unsigned int port;
	network_poll_t* poll = 0;
	blast_server_t* server = 0;

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

	server = blast_server_allocate();

	if( added )
		result = blast_server_run( daemon, poll, server );
	else
		log_warnf( HASH_BLAST, WARNING_SUSPICIOUS, "No bind address given" );

	blast_server_deallocate( server );

	network_poll_deallocate( poll );

	return result;
}
