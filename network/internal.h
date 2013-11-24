/* internal.h  -  Network library  -  Internal use only  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams.
 *
 * All rights reserved. No part of this library, code or built products may be used without
 * the explicit consent from Rampant Pixels AB
 * 
 */

#pragma once

/*! \file internal.h
    Internal types */

#include <foundation/platform.h>
#include <foundation/types.h>
#include <foundation/internal.h>

#include <network/types.h>
#include <network/hashstrings.h>

#if FOUNDATION_PLATFORM_WINDOWS
#  include <foundation/windows.h>
#  define FAR
#elif FOUNDATION_PLATFORM_POSIX
#  include <foundation/posix.h>
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <netdb.h>
#  include <ifaddrs.h>
#endif


typedef enum _socket_flag
{
	SOCKETFLAG_BLOCKING             = 0x00000001,
	SOCKETFLAG_TCPDELAY             = 0x00000002,
	SOCKETFLAG_POLLED               = 0x00000004,
	SOCKETFLAG_CONNECTION_PENDING   = 0x00000008,
	SOCKETFLAG_ERROR_PENDING        = 0x00000010,
	SOCKETFLAG_HANGUP_PENDING       = 0x00000020,
	SOCKETFLAG_REFLUSH              = 0x00000040
} socket_flag_t;


#define NETWORK_DECLARE_NETWORK_ADDRESS      \
	network_address_family_t   family;       \
	int                        address_size

struct _network_address
{
	NETWORK_DECLARE_NETWORK_ADDRESS;
};


#define NETWORK_DECLARE_NETWORK_ADDRESS_IP   \
	NETWORK_DECLARE_NETWORK_ADDRESS

typedef struct _network_address_ip
{
	NETWORK_DECLARE_NETWORK_ADDRESS_IP;
	struct sockaddr        saddr; //Aliased to ipv4/ipv6 struct
} network_address_ip_t;


typedef struct _network_address_ipv4
{
	NETWORK_DECLARE_NETWORK_ADDRESS_IP;
	struct sockaddr_in     saddr;
} network_address_ipv4_t;


typedef struct _network_address_ipv6
{
	NETWORK_DECLARE_NETWORK_ADDRESS_IP;
	struct sockaddr_in6    saddr;
} network_address_ipv6_t;


#if FOUNDATION_PLATFORM_WINDOWS
#  define NETWORK_SOCKET_ERROR ((int)WSAGetLastError())
#  define NETWORK_RESOLV_ERROR NETWORK_SOCKET_ERROR
#  define SOCKET_INVALID -1
#else //elif FOUNDATION_PLATFORM_POSIX
#  define NETWORK_SOCKET_ERROR errno
#  define NETWORK_RESOLV_ERROR NETWORK_SOCKET_ERROR
#  define SOCKET_INVALID -1
#endif


#define SOCKET_OBJECTTYPE  0x00020001U


typedef struct ALIGN(16) _socket_base    socket_base_t;
typedef struct ALIGN(16) _socket         socket_t;
typedef struct ALIGN(16) _socket_stream  socket_stream_t;


typedef void (*socket_open_fn)( socket_t*, unsigned int );
typedef int  (*socket_connect_fn)( socket_t*, const network_address_t*, unsigned int );
typedef void (*socket_buffer_read_fn)( socket_t*, unsigned int );
typedef void (*socket_buffer_write_fn)( socket_t* );
typedef void (*socket_stream_initialize_fn)( socket_t*, stream_t* );


struct ALIGN(16) _socket_stream
{
	FOUNDATION_DECLARE_STREAM;
	object_t                socket;
};


struct ALIGN(16) _socket_base
{
	object_t                object;
	int                     fd;
	uint32_t                flags:10;
	uint32_t                state:6;
	uint32_t                last_event:16;
};


struct ALIGN(16) _socket
{
	FOUNDATION_DECLARE_OBJECT;

	int                            base;
	
	network_address_t*             address_local;
	network_address_t*             address_remote;

	unsigned int                   offset_read_in;
	unsigned int                   offset_write_in;
	unsigned int                   offset_write_out;
	uint64_t                       bytes_read;
	uint64_t                       bytes_written;

	socket_open_fn                 open_fn;
	socket_connect_fn              connect_fn;
	socket_buffer_read_fn          read_fn;
	socket_buffer_write_fn         write_fn;
	socket_stream_initialize_fn    stream_initialize_fn;
	
	socket_stream_t*               stream;

	uint8_t                        buffer_in[BUILD_SIZE_SOCKET_READBUFFER];
	uint8_t                        buffer_out[BUILD_SIZE_SOCKET_WRITEBUFFER];
};

NETWORK_EXTERN socket_base_t*      _socket_base;
NETWORK_EXTERN int32_t             _socket_base_size;
NETWORK_EXTERN int32_t             _socket_base_next;

NETWORK_API socket_t*              _socket_allocate( void );
NETWORK_API int                    _socket_allocate_base( socket_t* sock );
NETWORK_API socket_t*              _socket_lookup( object_t id );
NETWORK_API void                   _socket_close( socket_t* sock );
NETWORK_API void                   _socket_set_blocking( socket_t* sock, bool block );
NETWORK_API void                   _socket_store_address_local( socket_t* sock, int family );
NETWORK_API int                    _socket_available_fd( int fd );
NETWORK_API unsigned int           _socket_available_nonblock_read( const socket_t* sock );
NETWORK_API socket_state_t         _socket_poll_state( socket_base_t* sockbase );

