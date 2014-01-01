/* socket.c  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 * 
 * https://github.com/rampantpixels/network_lib
 * 
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/socket.h>
#include <network/address.h>
#include <network/internal.h>
#include <network/hashstrings.h>

#include <foundation/foundation.h>


socket_base_t*  _socket_base = 0;
int32_t         _socket_base_next = 0;
int32_t         _socket_base_size = 0;

static objectmap_t*    _socket_map = 0;
static stream_vtable_t _socket_stream_vtable = {0};

//! Deallocate socket and free memory
static void            _socket_deallocate( socket_t* sock );
static unsigned int    _socket_buffered_in( const socket_t* sock );
static unsigned int    _socket_buffered_out( const socket_t* sock );
static void            _socket_doflush( socket_t* sock );
static int             _socket_create_fd( socket_t* sock, int family );

static void            _socket_set_blocking_fd( int fd, bool block );

static socket_stream_t*  _socket_stream_allocate( object_t id );
static void              _socket_stream_deallocate( stream_t* stream );
static uint64_t          _socket_read( stream_t* stream, void* buffer, uint64_t size );
static uint64_t          _socket_write( stream_t* stream, const void* buffer, uint64_t size );
static bool              _socket_eos( stream_t* stream );
static uint64_t          _socket_available_read( stream_t* stream );
static void              _socket_buffer_read( stream_t* stream );
static void              _socket_flush( stream_t* stream );
static void              _socket_truncate( stream_t* stream, uint64_t size );
static uint64_t          _socket_size( stream_t* stream );
static void              _socket_seek( stream_t* stream, int64_t offset, stream_seek_mode_t direction );
static int64_t           _socket_tell( stream_t* stream );
static uint64_t          _socket_last_modified( const stream_t* stream );


socket_t* _socket_allocate( void )
{
	socket_t* sock;
	object_t object = _socket_map ? objectmap_reserve( _socket_map ) : 0;
	if( !object )
		return 0;

	sock = memory_allocate_zero_context( HASH_NETWORK, sizeof( socket_t ), 16, MEMORY_PERSISTENT );
	
	log_debugf( HASH_NETWORK, "Allocated socket 0x%llx (0x%" PRIfixPTR ")", object, sock );

	sock->id = object;
	sock->ref = 1;
	sock->objecttype = SOCKET_OBJECTTYPE;
	sock->base = -1;

	objectmap_set( _socket_map, object, sock );

	return sock;
}


int _socket_allocate_base( socket_t* sock )
{
	int32_t startbase;
	int32_t maxbase;

	FOUNDATION_ASSERT( sock );

	if( sock->base >= 0 )
		return sock->base;

	//TODO: Better allocation scheme
	startbase = _socket_base_next;
	maxbase = _socket_base_size;
	do
	{
		socket_base_t* sockbase;

		int base = atomic_exchange_and_add32( &_socket_base_next, 1 );
		if( base >= maxbase )
			continue;

		sockbase = _socket_base + base;
		if( sockbase->object )
			continue;

		if( atomic_cas64( (volatile int64_t*)&sockbase->object, sock->id, 0 ) )
		{
			sock->base = base;
			sockbase->fd = SOCKET_INVALID;
			sockbase->flags = 0;
			sockbase->state = SOCKETSTATE_NOTCONNECTED;
			sockbase->last_event = 0;
			return base;
		}
		
	} while( true );

	return -1;
}


static void _socket_deallocate( socket_t* sock )
{
	const object_t object = sock->id;
	
#if BUILD_ENABLE_DEBUG_LOG
	int fd = SOCKET_INVALID;
	if( sock->base >= 0 )
		fd = _socket_base[ sock->base ].fd;
	log_debugf( HASH_NETWORK, "Deallocating socket 0x%llx (0x%" PRIfixPTR " : %d)", object, sock, fd );
#endif

	objectmap_free( _socket_map, object );
	
	_socket_close( sock );
	
	FOUNDATION_ASSERT_MSG( !sock->stream, "Socket deallocated while still holding stream" );
	stream_deallocate( (stream_t*)sock->stream );

	memory_deallocate( sock );
}


int _socket_create_fd( socket_t* sock, int family )
{
	socket_base_t* sockbase;

	if( _socket_allocate_base( sock ) < 0 )
	{
		log_errorf( HASH_NETWORK, ERROR_OUT_OF_MEMORY, "Unable to allocate base for socket 0x%" PRIfixPTR, sock );
		return SOCKET_INVALID;
	}

	sockbase = _socket_base + sock->base;
	
	if( sockbase->fd == SOCKET_INVALID )
		sock->open_fn( sock, family );

	return sockbase->fd;
}


void socket_free( object_t id )
{
	socket_t* sock;
	int32_t ref;
	do
	{
		sock = objectmap_lookup( _socket_map, id );
		if( sock )
		{
			ref = sock->ref;
			if( atomic_cas32( &sock->ref, ref - 1, ref ) )
			{
				if( ref == 1 )
					_socket_deallocate( sock );
				break;
			}
		}
	} while( sock );
}


bool socket_is_socket( object_t id )
{
	bool is_socket = false;
	socket_t* sock = _socket_lookup( id );
	if( sock )
		is_socket = true;
	socket_free( id );
	return is_socket;
}


bool socket_bind( object_t id, const network_address_t* address )
{
	bool success = false;
	socket_t* sock;
	socket_base_t* sockbase;
	const network_address_ip_t* address_ip;

	FOUNDATION_ASSERT( address );

	sock = _socket_lookup( id );
	if( !sock )
	{
		log_errorf( HASH_NETWORK, ERROR_INVALID_VALUE, "Trying to bind invalid socket 0x%llx", id );
		return false;
	}

	if( _socket_create_fd( sock, address->family ) == SOCKET_INVALID )
		goto exit;
	
	sockbase = _socket_base + sock->base;
	address_ip = (const network_address_ip_t*)address;
	if( bind( sockbase->fd, &address_ip->saddr, address_ip->address_size ) == 0 )
	{
		//Store local address
		_socket_store_address_local( sock, address_ip->family );
		success = true;
	}
	else
	{
#if BUILD_ENABLE_LOG
		int sockerr = NETWORK_SOCKET_ERROR;
		char* address_str = network_address_to_string( address, true );
		log_warnf( HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL, "Unable to bind socket 0x%llx (0x%" PRIfixPTR " : %d) to local address %s: %s", id, sock, sockbase->fd, address_str, system_error_message( sockerr ) );
		string_deallocate( address_str );
#endif
	}

exit:

	socket_free( id );
	
	return success;
}


bool socket_connect( object_t id, const network_address_t* address, unsigned int timeout )
{
	bool success = false;
	int err = 0;
	socket_t* sock;
	socket_base_t* sockbase;

	FOUNDATION_ASSERT( address );

	sock = _socket_lookup( id );
	if( !sock )
	{
		log_errorf( HASH_NETWORK, ERROR_INVALID_VALUE, "Trying to connect invalid socket 0x%llx", id );
		return false;
	}

	if( _socket_create_fd( sock, address->family ) == SOCKET_INVALID )
		goto exit;
	
	sockbase = _socket_base + sock->base;
	if( sockbase->state != SOCKETSTATE_NOTCONNECTED )
	{
#if BUILD_ENABLE_LOG
		char* address_str = network_address_to_string( address, true );
		log_warnf( HASH_NETWORK, WARNING_SUSPICIOUS, "Unable to connect already connected socket 0x%llx (0x%" PRIfixPTR " : %d) to remote address %s", id, sock, sockbase->fd, address_str );
		string_deallocate( address_str );
#endif
		goto exit;
	}

	sockbase->flags &= ~( SOCKETFLAG_CONNECTION_PENDING | SOCKETFLAG_ERROR_PENDING | SOCKETFLAG_HANGUP_PENDING );
    sockbase->last_event = 0;
	
	err = sock->connect_fn ? sock->connect_fn( sock, address, timeout ) : false;
	if( err )
	{
#if BUILD_ENABLE_LOG
		char* address_str = network_address_to_string( address, true );
		log_warnf( HASH_NETWORK, WARNING_SYSTEM_CALL_FAIL, "Unable to connect socket 0x%llx (0x%" PRIfixPTR " : %d) to remote address %s: %s", id, sock, sockbase->fd, address_str, system_error_message( err ) );
		string_deallocate( address_str );
#endif
	}
	else
	{
		success = true;
		memory_deallocate( sock->address_remote );
		sock->address_remote = network_address_clone( address );
	}
	
exit:

	socket_free( id );

	return success;
}


bool socket_blocking( object_t id )
{
	bool blocking = false;
	socket_t* sock = _socket_lookup( id );
	if( sock )
	{
		socket_base_t* sockbase = _socket_base + sock->base;
		blocking = ( ( sockbase->flags & SOCKETFLAG_BLOCKING ) != 0 );
		socket_free( id );
	}
	return blocking;
}


void socket_set_blocking( object_t id, bool block )
{
	socket_t* sock = _socket_lookup( id );
	if( !sock )
	{
		log_errorf( HASH_NETWORK, ERROR_INVALID_VALUE, "Trying to set blocking flag on an invalid socket 0x%llx", id );
		return;
	}

	_socket_set_blocking( sock, block );
	
	socket_free( id );
}


const network_address_t* socket_address_local( object_t id )
{
	network_address_t* addr = 0;
	socket_t* sock = _socket_lookup( id );
	if( sock )
	{
		addr = sock->address_local;
		socket_free( id );
	}
	return addr;
}


const network_address_t* socket_address_remote( object_t id )
{
	network_address_t* addr = 0;
	socket_t* sock = _socket_lookup( id );
	if( sock )
	{
		addr = sock->address_remote;
		socket_free( id );
	}
	return addr;
}


socket_state_t socket_state( object_t id )
{
	socket_state_t state = SOCKETSTATE_DISCONNECTED;
	socket_t* sock = _socket_lookup( id );
	if( sock )
	{
		state = _socket_poll_state( _socket_base + sock->base );
		socket_free( id );
	}
	return state;
}


stream_t* socket_stream( object_t id )
{
	socket_stream_t* stream = 0;
	socket_t* sock = _socket_lookup( id );
	if( !sock )
		return 0;

	if( !sock->stream )
	{
		//Keep reference created by _socket_lookup
		stream = _socket_stream_allocate( id );
		sock->stream = stream;

		if( sock->stream_initialize_fn )
			sock->stream_initialize_fn( sock, (stream_t*)stream );
	}
	else
	{
		stream = sock->stream;
		socket_free( id );
	}
	
	return (stream_t*)stream;
}


socket_t* _socket_lookup( object_t id )
{
	socket_t* sock;
	int32_t ref;
	do
	{
		sock = objectmap_lookup( _socket_map, id );
		if( sock )
		{
			ref = sock->ref;
			if( ref <= 0 )
				break;
			if( atomic_cas32( &sock->ref, ref + 1, ref ) )
				return sock;
		}
	} while( sock );
	return 0;
}


//Returns -1 if nothing available and socket closed, 0 if nothing available but still open, >0 if data available
int _socket_available_fd( int fd )
{
	bool closed = false;
	int available = 0;

	if( fd == SOCKET_INVALID )
		return -1;

#if FOUNDATION_PLATFORM_WINDOWS
	{
		u_long avail = 0;
		if( ioctlsocket( fd, FIONREAD, &avail ) < 0 )
			closed = true;
		available = (int)avail;
	}
#elif FOUNDATION_PLATFORM_POSIX
	if( ioctl( fd, FIONREAD, &available ) < 0 )
		closed = true;
#else
#  error Not implemented
#endif

	return ( !available && closed ) ? -1 : available;
}


void socket_close( object_t id )
{
	socket_t* sock = _socket_lookup( id );
	if( sock )
	{
		_socket_close( sock );
		socket_free( id );
	}
}


void _socket_close( socket_t* sock )
{
	int fd = SOCKET_INVALID;
	network_address_t* local_address = sock->address_local;
	network_address_t* remote_address = sock->address_remote;

	if( sock->base >= 0 )
	{
		socket_base_t* sockbase = _socket_base + sock->base;

		fd               = sockbase->fd;
		sock->base       = -1;
		sockbase->object = 0;
		sockbase->fd     = SOCKET_INVALID;
		sockbase->state  = SOCKETSTATE_NOTCONNECTED;
		sockbase->flags  = 0;
	}

	log_debugf( HASH_NETWORK, "Closing socket 0x%llx (0x%" PRIfixPTR " : %d)", sock->id, sock, fd );
	
	sock->address_local  = 0;
	sock->address_remote = 0;
	
	if( fd != SOCKET_INVALID )
	{
		_socket_set_blocking_fd( fd, false );
#if FOUNDATION_PLATFORM_WINDOWS
		shutdown( fd, SD_BOTH );
		closesocket( fd );
#elif FOUNDATION_PLATFORM_POSIX
		shutdown( fd, SHUT_RDWR );
		close( fd );
#else
#  error Not implemented
#endif
	}

	if( local_address )
		memory_deallocate( local_address );
	if( remote_address )
		memory_deallocate( remote_address );
}


void _socket_set_blocking( socket_t* sock, bool block )
{
	socket_base_t* sockbase;
	int fd;

	if( _socket_allocate_base( sock ) < 0 )
		return;

	sockbase = _socket_base + sock->base;
	sockbase->flags = ( block ? sockbase->flags | SOCKETFLAG_BLOCKING : sockbase->flags & ~SOCKETFLAG_BLOCKING );
	fd = sockbase->fd;
	if( fd != SOCKET_INVALID )
		_socket_set_blocking_fd( fd, block );
}


void _socket_set_blocking_fd( int fd, bool block )
{
	unsigned long param = block ? 0 : 1;
#if FOUNDATION_PLATFORM_WINDOWS
	ioctlsocket( fd, FIONBIO, &param );
#elif FOUNDATION_PLATFORM_POSIX
	ioctl( fd, FIONBIO, &param );
#else
#  error Not implemented
#endif
}


static unsigned int _socket_buffered_in( const socket_t* sock )
{
	FOUNDATION_ASSERT( sock );
	if( sock->offset_write_in >= sock->offset_read_in )
		return sock->offset_write_in - sock->offset_read_in;
	return ( BUILD_SIZE_SOCKET_READBUFFER - sock->offset_read_in ) + sock->offset_write_in;
}


static unsigned int _socket_buffered_out( const socket_t* sock )
{
	FOUNDATION_ASSERT( sock );
	return sock->offset_write_out;
}


unsigned int _socket_available_nonblock_read( const socket_t* sock )
{
	int available = 0;
	FOUNDATION_ASSERT( sock );
	if( sock->base >= 0 )
		available = _socket_available_fd( _socket_base[ sock->base ].fd );
	return _socket_buffered_in( sock ) + ( available > 0 ? available : 0 );
}


static void _socket_doflush( socket_t* sock )
{
	socket_base_t* sockbase;

	FOUNDATION_ASSERT( sock );
	if( sock->base < 0 )
		return;
	if( !sock->offset_write_out )
		return;
	
	sockbase = _socket_base + sock->base;
	if( sockbase->state != SOCKETSTATE_CONNECTED )
		return;

	sock->write_fn( sock );
}


socket_state_t _socket_poll_state( socket_base_t* sockbase )
{
	socket_t* sock = 0;

	if( ( sockbase->state == SOCKETSTATE_NOTCONNECTED ) || ( sockbase->state == SOCKETSTATE_DISCONNECTED ) )
		return sockbase->state;

	switch( sockbase->state )
	{
		case SOCKETSTATE_CONNECTING:
		{
			struct timeval tv;
			fd_set fdwrite, fderr;

			FD_ZERO( &fdwrite );
			FD_ZERO( &fderr );
			FD_SET( sockbase->fd, &fdwrite );
			FD_SET( sockbase->fd, &fderr   );
				
			tv.tv_sec  = 0;
			tv.tv_usec = 0;

			select( (int)( sockbase->fd + 1 ), 0, &fdwrite, &fderr, &tv );

			if( FD_ISSET( sockbase->fd, &fderr ) )
			{
				if( !sock )
					sock = _socket_lookup( sockbase->object );
				log_debugf( HASH_NETWORK, "Socket 0x%llx (0x%" PRIfixPTR " : %d): error in state CONNECTING", sockbase->object, sock, sockbase->fd );
				if( sock )
					_socket_close( sock );
				//network_event_post( NETWORKEVENT_ERROR, sock );
			}
			else if( FD_ISSET( sockbase->fd, &fdwrite ) )
			{
#if BUILD_ENABLE_DEBUG_LOG
				if( !sock )
					sock = _socket_lookup( sockbase->object );
				log_debugf( HASH_NETWORK, "Socket 0x%llx (0x%" PRIfixPTR " : %d): CONNECTING -> CONNECTED", sockbase->object, sock, sockbase->fd );
#endif
				//if( sock->state == SOCKETSTATE_CONNECTING )
				//	network_event_post( NETWORKEVENT_CONNECTED, sock );
				sockbase->state = SOCKETSTATE_CONNECTED;
			}

			break;
		}

		case SOCKETSTATE_CONNECTED:
		{
			int available = _socket_available_fd( sockbase->fd );
			if( available < 0 )
			{
#if BUILD_ENABLE_DEBUG_LOG
				if( !sock )
					sock = _socket_lookup( sockbase->object );
				log_debugf( HASH_NETWORK, "Socket 0x%llx (0x%" PRIfixPTR " : %d): hangup in CONNECTED", sockbase->object, sock, sockbase->fd );
#endif
				sockbase->state = SOCKETSTATE_DISCONNECTED;
				//network_event_post( NETWORKEVENT_HANGUP, sock );
				//Fall through to disconnected check for close
			}
			else
				break;
		}

		case SOCKETSTATE_DISCONNECTED:
		{
			if( !sock )
				sock = _socket_lookup( sockbase->object );
			if( !_socket_buffered_in( sock ) )
			{
				log_debugf( HASH_NETWORK, "Socket 0x%llx (0x%" PRIfixPTR " : %d): all data read in DISCONNECTED", sockbase->object, sock, sockbase->fd );
				if( sock )
					_socket_close( sock );
			}
			break;
		}

		default:
			break;
	}

	if( sock )
		socket_free( sock->id );
	
	return sockbase->state;
}


void _socket_store_address_local( socket_t* sock, int family )
{
	socket_base_t* sockbase;
	network_address_ip_t* address_local = 0;

	FOUNDATION_ASSERT( sock );
	if( sock->base < 0 )
		return;

	sockbase = _socket_base + sock->base;
	if( family == NETWORK_ADDRESSFAMILY_IPV4 )
	{
		address_local = memory_allocate_zero_context( HASH_NETWORK, sizeof( network_address_ipv4_t ), 0, MEMORY_PERSISTENT );
		address_local->family = NETWORK_ADDRESSFAMILY_IPV4;
		address_local->address_size = sizeof( struct sockaddr_in );
	}
	else if( family == NETWORK_ADDRESSFAMILY_IPV6 )
	{
		address_local = memory_allocate_zero_context( HASH_NETWORK, sizeof( network_address_ipv6_t ), 0, MEMORY_PERSISTENT );
		address_local->family = NETWORK_ADDRESSFAMILY_IPV6;
		address_local->address_size = sizeof( struct sockaddr_in6 );
	}
	else
	{
		FOUNDATION_ASSERT_FAILFORMAT_LOG( HASH_NETWORK, "Unable to get local address for socket 0x%llx (0x%" PRIfixPTR " : %d): Unsupported address family %u", sock->id, sock, sockbase->fd, family );
		return;
	}
	getsockname( sockbase->fd, &address_local->saddr, (socklen_t*)&address_local->address_size );
	memory_deallocate( sock->address_local );
	sock->address_local = (network_address_t*)address_local;
}


static socket_stream_t* _socket_stream_allocate( object_t id )
{
	socket_stream_t* sockstream = memory_allocate_zero_context( HASH_NETWORK, sizeof( socket_stream_t ), 0, MEMORY_PERSISTENT );
	stream_t* stream = (stream_t*)sockstream;

	//Network streams are always little endian by default
	_stream_initialize( stream, BYTEORDER_LITTLEENDIAN );

	stream->type = STREAMTYPE_SOCKET;
	stream->sequential = 1;
	stream->inorder = 1;
	stream->reliable = 1;
	stream->path = string_format( "socket://%llx", id );
	stream->mode = STREAM_OUT | STREAM_IN | STREAM_BINARY;
	stream->vtable = &_socket_stream_vtable;

	sockstream->socket = id;

	return sockstream;
}


static void _socket_stream_deallocate( stream_t* stream )
{
	socket_stream_t* sockstream;
	object_t id;

	FOUNDATION_ASSERT( stream );
	FOUNDATION_ASSERT( stream->type == STREAMTYPE_SOCKET );

	sockstream = (socket_stream_t*)stream;
	id = sockstream->socket;

	if( id )
	{
		socket_t* sock = _socket_lookup( id );
		if( sock )
		{
			FOUNDATION_ASSERT_MSGFORMAT( sock->stream == sockstream, "Socket %llx (0x%" PRIfixPTR " : %d): Deallocating stream mismatch, stream is 0x%" PRIfixPTR ", socket stream is 0x%" PRIfixPTR, id, sock, ( sock->base >= 0 ) ? _socket_base[ sock->base ].fd : SOCKET_INVALID, sockstream, sock->stream );
			sock->stream = 0;
			socket_free( id );
		}
	}
	
	sockstream->socket = 0;

	if( id )
		socket_free( id );
}


static uint64_t _socket_read( stream_t* stream, void* buffer, uint64_t size )
{
	socket_stream_t* sockstream;
	socket_t* sock;
	socket_base_t* sockbase;
	uint64_t was_read = 0;
	unsigned int copy;
	int loop_counter = 0;
	bool try_again = false;
	bool polled;
	bool blocking;

	FOUNDATION_ASSERT( stream );
	FOUNDATION_ASSERT( stream->type == STREAMTYPE_SOCKET );

	sockstream = (socket_stream_t*)stream;
	sock = _socket_lookup( sockstream->socket );
	if( !sock )
		return 0;
	if( sock->base < 0 )
		return 0;

	sockbase = _socket_base + sock->base;

	polled = ( ( sockbase->flags & SOCKETFLAG_POLLED ) != 0 );
	blocking = ( ( sockbase->flags & SOCKETFLAG_BLOCKING ) != 0 );
	
	//Trigger read events again (or else poll->read->poll with same amount of buffered data will not trigger event)
	sockbase->last_event = 0;
	
	if( ( sockbase->state != SOCKETSTATE_CONNECTED ) && ( sockbase->state != SOCKETSTATE_DISCONNECTED ) )
		goto exit;

	if( !size )
		goto exit;

	do
	{
		try_again = false;
		
		do
		{
			unsigned int want_read;

			if( sock->offset_write_in >= sock->offset_read_in )
				copy = ( sock->offset_write_in - sock->offset_read_in );
			else
				copy = ( BUILD_SIZE_SOCKET_READBUFFER - sock->offset_read_in );

			want_read = (unsigned int)( size - was_read );
			if( copy > want_read )
				copy = want_read;
			
			if( copy > 0 )
			{
				if( buffer )
					memcpy( buffer, sock->buffer_in + sock->offset_read_in, (size_t)copy );

				was_read += copy;
				sock->offset_read_in += copy;
				if( sock->offset_read_in == BUILD_SIZE_SOCKET_READBUFFER )
					sock->offset_read_in = 0;

				try_again = true;
			}
		} while( copy > 0 );

		if( was_read < size )
		{
			if( ( !blocking && !polled ) || blocking )
				sock->read_fn( sock, (unsigned int)( size - was_read ) );
		}

	} while( ( was_read < size ) && ( try_again || ( ++loop_counter < 2 ) ) );

	if( was_read < size )
	{
		if( was_read )
			log_warnf( HASH_NETWORK, WARNING_SUSPICIOUS, "Socket 0x%llx (0x%" PRIfixPTR " : %d): partial read %d of %d bytes", sock->id, sock, sockbase->fd, was_read, size );
		_socket_poll_state( sockbase );
	}

	sock->bytes_read += was_read;

exit:

	socket_free( sockstream->socket );
	
	return was_read;
}


static uint64_t _socket_write( stream_t* stream, const void* buffer, uint64_t size )
{
	socket_stream_t* sockstream;
	socket_t* sock;
	socket_base_t* sockbase;
	uint64_t was_written = 0;
	unsigned int remain;

	FOUNDATION_ASSERT( stream );
	FOUNDATION_ASSERT( stream->type == STREAMTYPE_SOCKET );

	sockstream = (socket_stream_t*)stream;
	sock = _socket_lookup( sockstream->socket );	
	if( !sock )
		return 0;
	if( sock->base < 0 )
		return 0;

	sockbase = _socket_base + sock->base;
	remain = BUILD_SIZE_SOCKET_WRITEBUFFER - sock->offset_write_out;
	
	if( sockbase->state != SOCKETSTATE_CONNECTED )
		goto exit;

	if( !size || !buffer )
		goto exit;

	do
	{
		if( size <= remain )
		{
			memcpy( sock->buffer_out + sock->offset_write_out, buffer, (size_t)size );

			sock->offset_write_out += (unsigned int)size;
			was_written += size;
			size = 0;

			break;
		}

		if( remain )
		{
			memcpy( sock->buffer_out + sock->offset_write_out, buffer, remain );
			buffer = pointer_offset_const( buffer, remain );

			size -= remain;
			was_written += remain;
			sock->offset_write_out += remain;
		}

		_socket_doflush( sock );

		if( sockbase->state != SOCKETSTATE_CONNECTED )
		{
			log_warnf( HASH_NETWORK, WARNING_SUSPICIOUS, "Socket %llx (0x%" PRIfixPTR " : %d): partial write %d of %d bytes", sock->id, sock, sockbase->fd, was_written, (unsigned int)size );
			break;
		}

		remain = BUILD_SIZE_SOCKET_WRITEBUFFER - sock->offset_write_out;

	} while( remain );

	sock->bytes_written += was_written;

exit:

	socket_free( sockstream->socket );
	
	return was_written;
}


static bool _socket_eos( stream_t* stream )
{
	socket_stream_t* sockstream;
	socket_t* sock;
	socket_base_t* sockbase;
	bool eos = false;
	socket_state_t state;

	FOUNDATION_ASSERT( stream );
	FOUNDATION_ASSERT( stream->type == STREAMTYPE_SOCKET );

	sockstream = (socket_stream_t*)stream;
	sock = _socket_lookup( sockstream->socket );	
	if( !sock )
		return true;
	if( sock->base < 0 )
		return true;

	sockbase = _socket_base + sock->base;
	state = _socket_poll_state( sockbase );
	if( ( ( state != SOCKETSTATE_CONNECTED ) || ( sockbase->fd == SOCKET_INVALID ) ) && !_socket_available_nonblock_read( sock ) )
		eos = true;

	socket_free( sockstream->socket );
	
	return false;
}


static uint64_t _socket_available_read( stream_t* stream )
{
	socket_stream_t* sockstream;
	socket_t* sock;
	unsigned int available;

	FOUNDATION_ASSERT( stream );
	FOUNDATION_ASSERT( stream->type == STREAMTYPE_SOCKET );

	sockstream = (socket_stream_t*)stream;
	sock = _socket_lookup( sockstream->socket );	
	if( !sock )
		return 0;
	if( sock->base < 0 )
		return 0;

	available = _socket_available_nonblock_read( sock );

	socket_free( sockstream->socket );

	return available;
}


static void _socket_buffer_read( stream_t* stream )
{
	socket_stream_t* sockstream;
	socket_t* sock;
	socket_base_t* sockbase;
	int available;

	FOUNDATION_ASSERT( stream );
	FOUNDATION_ASSERT( stream->type == STREAMTYPE_SOCKET );

	sockstream = (socket_stream_t*)stream;
	sock = _socket_lookup( sockstream->socket );	
	if( !sock )
		return;
	if( sock->base < 0 )
		return;

	sockbase = _socket_base + sock->base;
	if( ( sockbase->state != SOCKETSTATE_CONNECTED ) || ( sockbase->flags & SOCKETFLAG_POLLED ) || ( sockbase->fd == SOCKET_INVALID ))
		goto exit;
	if( _socket_buffered_in( sock ) == BUILD_SIZE_SOCKET_READBUFFER )
		goto exit;

	available = _socket_available_fd( sockbase->fd );
	if( available > 0 )
		sock->read_fn( sock, available );

exit:
	
	socket_free( sockstream->socket );
}


static void _socket_flush( stream_t* stream )
{
	socket_stream_t* sockstream;
	socket_t* sock;

	FOUNDATION_ASSERT( stream );
	FOUNDATION_ASSERT( stream->type == STREAMTYPE_SOCKET );

	sockstream = (socket_stream_t*)stream;
	sock = _socket_lookup( sockstream->socket );	
	if( !sock )
		return;

	_socket_doflush( sock );

	socket_free( sockstream->socket );
}


static void _socket_truncate( stream_t* stream, uint64_t size )
{
	FOUNDATION_ASSERT( stream );
	FOUNDATION_ASSERT( stream->type == STREAMTYPE_SOCKET );
}


static uint64_t _socket_size( stream_t* stream )
{
	FOUNDATION_ASSERT( stream );
	FOUNDATION_ASSERT( stream->type == STREAMTYPE_SOCKET );
	return 0;
}


static void _socket_seek( stream_t* stream, int64_t offset, stream_seek_mode_t direction )
{
	socket_stream_t* sockstream;
	socket_t* sock;

	FOUNDATION_ASSERT( stream );
	FOUNDATION_ASSERT( stream->type == STREAMTYPE_SOCKET );

	sockstream = (socket_stream_t*)stream;
	sock = _socket_lookup( sockstream->socket );	
	if( !sock )
		return;
	
	if( ( direction != STREAM_SEEK_CURRENT ) || ( offset < 0 ) )
	{
		log_error( HASH_NETWORK, ERROR_UNSUPPORTED, "Invalid call, only forward seeking allowed on sockets" );
	}
	else
	{
		_socket_read( stream, 0, offset );
	}

	socket_free( sockstream->socket );
}


static int64_t _socket_tell( stream_t* stream )
{
	socket_stream_t* sockstream;
	socket_t* sock;
	int64_t pos;

	FOUNDATION_ASSERT( stream );
	FOUNDATION_ASSERT( stream->type == STREAMTYPE_SOCKET );

	sockstream = (socket_stream_t*)stream;
	sock = _socket_lookup( sockstream->socket );	
	if( !sock )
		return 0;

	pos = (int64_t)sock->bytes_read;

	socket_free( sockstream->socket );

	return pos;
}


static uint64_t _socket_last_modified( const stream_t* stream )
{
	FOUNDATION_ASSERT( stream );
	FOUNDATION_ASSERT( stream->type == STREAMTYPE_SOCKET );
	return time_current();
}



int _socket_initialize( unsigned int max_sockets )
{
	if( !_socket_map )
		_socket_map = objectmap_allocate( max_sockets + ( max_sockets > 256 ? 256 : 8 ) );

	if( !_socket_base )
	{
		_socket_base = memory_allocate_zero_context( HASH_NETWORK, sizeof( socket_base_t ) * max_sockets, 16, MEMORY_PERSISTENT );
		_socket_base_size = (int)max_sockets;
		_socket_base_next = 0;
	}
	
	_socket_stream_vtable.read = _socket_read;
	_socket_stream_vtable.write = _socket_write;
	_socket_stream_vtable.eos = _socket_eos;
	_socket_stream_vtable.flush = _socket_flush;
	_socket_stream_vtable.truncate = _socket_truncate;
	_socket_stream_vtable.size = _socket_size;
	_socket_stream_vtable.seek = _socket_seek;
	_socket_stream_vtable.tell = _socket_tell;
	_socket_stream_vtable.lastmod = _socket_last_modified;
	_socket_stream_vtable.buffer_read = _socket_buffer_read;
	_socket_stream_vtable.available_read = _socket_available_read;
	_socket_stream_vtable.deallocate = _socket_stream_deallocate;
	_socket_stream_vtable.clone = 0;
	
	return 0;
}


void _socket_shutdown( void )
{
	if( _socket_map )
		objectmap_deallocate( _socket_map );
	_socket_map = 0;

	if( _socket_base )
		memory_deallocate( _socket_base );
	_socket_base = 0;
}

