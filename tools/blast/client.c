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


typedef struct blast_client_t
{
	network_address_t** address;
    object_t sock;
} blast_client_t;


blast_client_t* clients = 0;


static void blast_client_deallocate( blast_client_t* client )
{
}


static int blast_client_initialize( blast_client_t* client, network_address_t** address )
{
    int iaddr, addrsize = 0;
    network_datagram_t datagram;
    packet_t packet;
    
    client->address = address;
    client->sock = udp_socket_create();
    
    if( !client->sock )
        return BLAST_ERROR_UNABLE_TO_CREATE_SOCKET;
    
    datagram.size = sizeof( packet );
    datagram.data = &packet;
    
    packet.type = PACKET_HANDSHAKE;
    packet.size = 0;
    
    for( iaddr = 0, addrsize = array_size( address ); iaddr < addrsize; ++iaddr )
        udp_socket_sendto( client->sock, datagram, address[iaddr] );
    
    return BLAST_RESULT_OK;
}



int blast_client( network_address_t*** target, char** files )
{
	int isock, asize = 0;
	int iaddr, addrsize = 0;
	int iclient, csize = 0;
	int result = BLAST_RESULT_OK;

	for( isock = 0, asize = array_size( target ); isock < asize; ++isock )
	{
		blast_client_t client;
		
#if BUILD_ENABLE_LOG
		char* targetaddress = 0;
		for( iaddr = 0, addrsize = array_size( target[isock] ); iaddr < addrsize; ++iaddr )
		{
			char* address = network_address_to_string( target[isock][iaddr], true );
			targetaddress = string_append( string_append( targetaddress, address ), " " );
			string_deallocate( address );
		}
		log_infof( HASH_BLAST, "Target %s", targetaddress );
		string_deallocate( targetaddress );
#endif

        if( blast_client_initialize( &client, target[isock] ) == BLAST_RESULT_OK )
            array_push( clients, client );
	}

	for( iclient = 0, csize = array_size( clients ); iclient < csize; ++iclient )
		blast_client_deallocate( &clients[iclient] );
	array_deallocate( clients );
	
	return result;
}
