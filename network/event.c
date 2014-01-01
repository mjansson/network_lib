/* event.c  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 * 
 * https://github.com/rampantpixels/network_lib
 * 
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/event.h>

#include <foundation/foundation.h>


#define NETWORK_DECLARE_EVENT \
	FOUNDATION_DECLARE_EVENT

typedef struct _network_event
{
	NETWORK_DECLARE_EVENT;
} network_event_t;


static event_stream_t* _network_events = 0;


int _network_event_initialize( void )
{
	if( !_network_events )
		_network_events = event_stream_allocate( BUILD_SIZE_NETWORK_EVENT_STREAM );
	return 0;
}


void _network_event_shutdown( void )
{
	if( _network_events )
		event_stream_deallocate( _network_events );
	_network_events = 0;
}


event_stream_t* network_event_stream( void )
{
	return _network_events;
}


void network_event_post( network_event_id id, object_t sock )
{
	event_post( _network_events, id, 0, sock, 0, 0 );
}


object_t network_event_socket( event_t* event )
{
	return event->object;
}

