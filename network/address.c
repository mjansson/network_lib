/* address.c  -  Network library  -  Internal use only  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams.
 *
 * All rights reserved. No part of this library, code or built products may be used without
 * the explicit consent from Rampant Pixels AB
 * 
 */

#include <network/address.h>
#include <network/internal.h>
#include <network/hashstrings.h>

#include <foundation/foundation.h>


network_address_t* network_address_clone( const network_address_t* address )
{
	network_address_t* cloned = 0;
	if( address )
	{
		cloned = memory_allocate_context( MEMORYCONTEXT_NETWORK, sizeof( network_address_t ) + address->address_size, 0, MEMORY_PERSISTENT );
		memcpy( cloned, address, sizeof( network_address_t ) + address->address_size );
	}
	return cloned;
}


network_address_t** network_address_resolve( const char* address )
{
	network_address_t** addresses = 0;
	char* localaddress = 0;
	const char* final_address = address;
	unsigned int portdelim;
	int ret;
	struct addrinfo hints = {0};
	struct addrinfo* result = 0;

	if( !address )
		return addresses;
	
	error_context_push( "resolving network address", address );
	
	portdelim = string_rfind( address, ':', STRING_NPOS );
	
	if( string_find_first_not_of( address, "[abcdefABCDEF0123456789.:]", 0 ) == STRING_NPOS )
	{
		//ipv6 hex format has more than one :
		if( ( portdelim != STRING_NPOS ) && ( string_find( address, ':', 0 ) != portdelim ) )
		{
			//ipv6 hex format with port is of format [addr]:port
			if( ( address[0] != '[' ) || ( address[ portdelim - 1 ] != ']' ) )
				portdelim = STRING_NPOS;
		}
	}
	
	if( portdelim != STRING_NPOS )
	{
		unsigned int addrlen;

		localaddress = string_clone( address );
		localaddress[portdelim] = 0;
		final_address = localaddress;

		addrlen = string_length( localaddress );
		if( ( localaddress[0] == '[' ) && ( localaddress[ addrlen - 1 ] == ']' ) )
		{
			localaddress[ addrlen - 1 ] = 0;
			final_address++;
			--portdelim;
		}
	}
	
	hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM;

	ret = getaddrinfo( final_address, ( portdelim != STRING_NPOS ) ? final_address + portdelim + 1 : 0, &hints, &result );
	if( ret == 0 )
	{
		struct addrinfo* curaddr;
		for( curaddr = result; curaddr; curaddr = curaddr->ai_next )
		{
			if( curaddr->ai_family == AF_INET )
			{
				network_address_ipv4_t* ipv4 = memory_allocate_zero_context( MEMORYCONTEXT_NETWORK, sizeof( network_address_ipv4_t ), 0, MEMORY_PERSISTENT );
				ipv4->family = NETWORK_ADDRESSFAMILY_IPV4;
				ipv4->address_size = sizeof( struct sockaddr_in );
				ipv4->saddr.sin_family = AF_INET;
				memcpy( &ipv4->saddr, curaddr->ai_addr, sizeof( struct sockaddr_in ) );

				array_push( addresses, (network_address_t*)ipv4 );
			}
			else if( curaddr->ai_family == AF_INET6 )
			{
				network_address_ipv6_t* ipv6 = memory_allocate_zero_context( MEMORYCONTEXT_NETWORK, sizeof( network_address_ipv6_t ), 0, MEMORY_PERSISTENT );
				ipv6->family = NETWORK_ADDRESSFAMILY_IPV6;
				ipv6->address_size = sizeof( struct sockaddr_in6 );
				ipv6->saddr.sin6_family = AF_INET6;
				memcpy( &ipv6->saddr, curaddr->ai_addr, sizeof( struct sockaddr_in6 ) );

				array_push( addresses, (network_address_t*)ipv6 );
			}
		}
	}
	else
	{
		log_warnf( HASH_NETWORK, WARNING_BAD_DATA, "Unable to resolve network address '%s' (%s): %s", address, final_address, system_error_message( NETWORK_RESOLV_ERROR ) );
	}

	if( localaddress )
		string_deallocate( localaddress );
	
	error_context_pop();
	
	return addresses;
}


char* network_address_to_string( const network_address_t* address, bool numeric )
{
	if( address )
	{
		if( address->family == NETWORK_ADDRESSFAMILY_IPV4 )
		{
			char host[NI_MAXHOST] = {0};
			char service[NI_MAXSERV] = {0};
			const network_address_ipv4_t* addr_ipv4 = (const network_address_ipv4_t*)address;
			int ret = getnameinfo( (const struct sockaddr*)&addr_ipv4->saddr, addr_ipv4->address_size, host, NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICSERV | ( numeric ? NI_NUMERICHOST : 0 ) );
			if( ret == 0 )
			{
				if( addr_ipv4->saddr.sin_port != 0 )
					return string_format( "%s:%s", host, service );
				else
					return string_format( "%s", host );
			}
		}
		else if( address->family == NETWORK_ADDRESSFAMILY_IPV6 )
		{
			char host[NI_MAXHOST] = {0};
			char service[NI_MAXSERV] = {0};
			const network_address_ipv6_t* addr_ipv6 = (const network_address_ipv6_t*)address;
			int ret = getnameinfo( (const struct sockaddr*)&addr_ipv6->saddr, addr_ipv6->address_size, host, NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICSERV | ( numeric ? NI_NUMERICHOST : 0 ) );
			if( ret == 0 )
			{
				if( addr_ipv6->saddr.sin6_port != 0 )
					return string_format( "[%s]:%s", host, service );
				else
					return string_format( "%s", host );
			}
		}
	}
	else
	{
		return string_clone( "<null>" );
	}
	return string_clone( "invalid_address" );
}


network_address_t* network_address_ipv4_any( void )
{
	network_address_ipv4_t* address = memory_allocate_zero_context( MEMORYCONTEXT_NETWORK, sizeof( network_address_ipv4_t ), 0, MEMORY_PERSISTENT );
#if FOUNDATION_PLATFORM_WINDOWS
	address->saddr.sin_addr.s_addr = INADDR_ANY;
#else
	address->saddr.sin_addr.s_addr = 0;//INADDR_ANY;
#endif
	address->saddr.sin_family = AF_INET;
	address->family = NETWORK_ADDRESSFAMILY_IPV4;
	address->address_size = sizeof( struct sockaddr_in );
	return (network_address_t*)address;
}


network_address_t* network_address_ipv6_any( void )
{
	network_address_ipv6_t* address = memory_allocate_zero_context( MEMORYCONTEXT_NETWORK, sizeof( network_address_ipv6_t ), 0, MEMORY_PERSISTENT );
#if FOUNDATION_PLATFORM_WINDOWS
	address->saddr.sin6_addr = in6addr_any;
#else
	address->saddr.sin6_addr = in6addr_any;
#endif
	address->saddr.sin6_family = AF_INET6;
	address->family = NETWORK_ADDRESSFAMILY_IPV6;
	address->address_size = sizeof( struct sockaddr_in6 );
	return (network_address_t*)address;
}


void network_address_ip_set_port( network_address_t* address, unsigned int port )
{
	if( address->family == NETWORK_ADDRESSFAMILY_IPV4 )
	{
		network_address_ipv4_t* addr_ipv4 = (network_address_ipv4_t*)address;
		addr_ipv4->saddr.sin_port = htons( port );
	}
	else if( address->family == NETWORK_ADDRESSFAMILY_IPV6 )
	{
		network_address_ipv6_t* addr_ipv6 = (network_address_ipv6_t*)address;
		addr_ipv6->saddr.sin6_port = htons( port );
	}
}


unsigned int network_address_ip_port( const network_address_t* address )
{
	if( address->family == NETWORK_ADDRESSFAMILY_IPV4 )
	{
		const network_address_ipv4_t* addr_ipv4 = (const network_address_ipv4_t*)address;
		return ntohs( addr_ipv4->saddr.sin_port );
	}
	else if( address->family == NETWORK_ADDRESSFAMILY_IPV6 )
	{
		const network_address_ipv6_t* addr_ipv6 = (const network_address_ipv6_t*)address;
		return ntohs( addr_ipv6->saddr.sin6_port );
	}
	return 0;
}


network_address_family_t network_address_type( const network_address_t* address )
{
	return address->family;
}


network_address_t** network_address_local( void )
{
	network_address_t** addresses = 0;

#if FOUNDATION_PLATFORM_WINDOWS
	PIP_ADAPTER_ADDRESSES adapter;
	IP_ADAPTER_ADDRESSES* adapter_address = 0;
	unsigned long address_size = 16 * 1024 * 1024;
	int num_retries = 4;
	unsigned long ret = 0;
#endif

	error_context_push( "getting local network addresses", 0 );	
	
#if FOUNDATION_PLATFORM_WINDOWS

	do
	{
		adapter_address = memory_allocate_zero_context( MEMORYCONTEXT_NETWORK, (unsigned int)address_size, 0, MEMORY_TEMPORARY );

		ret = GetAdaptersAddresses( AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST, 0, adapter_address, &address_size );
		if( ret == ERROR_BUFFER_OVERFLOW )
		{
			memory_deallocate( adapter_address );
			adapter_address = 0;
		}
		else
		{
			break;
		}
	} while( num_retries-- > 0 );

	if( !adapter_address || ( ret != NO_ERROR ) )
	{
		if( adapter_address )
			memory_deallocate( adapter_address );
		log_errorf( HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL, "Unable to get adapters addresses: %s", system_error_message( ret ) );
		error_context_pop();
		return 0;
	}

	for( adapter = adapter_address; adapter; adapter = adapter->Next )
	{
		IP_ADAPTER_UNICAST_ADDRESS* unicast;

		if( adapter->TunnelType == TUNNEL_TYPE_TEREDO )
			continue;

		if( adapter->OperStatus != IfOperStatusUp )
			continue;

		for( unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next )
		{
			if( unicast->Address.lpSockaddr->sa_family == AF_INET )
			{
				network_address_ipv4_t* ipv4 = memory_allocate_zero_context( MEMORYCONTEXT_NETWORK, sizeof( network_address_ipv4_t ), 0, MEMORY_PERSISTENT );
				ipv4->family = NETWORK_ADDRESSFAMILY_IPV4;
				ipv4->address_size = sizeof( struct sockaddr_in );
				ipv4->saddr.sin_family = AF_INET;
				memcpy( &ipv4->saddr, unicast->Address.lpSockaddr, sizeof( struct sockaddr_in ) );

				array_push( addresses, (network_address_t*)ipv4 );
			}
			else if( unicast->Address.lpSockaddr->sa_family == AF_INET6 )
			{
				network_address_ipv6_t* ipv6 = memory_allocate_zero_context( MEMORYCONTEXT_NETWORK, sizeof( network_address_ipv6_t ), 0, MEMORY_PERSISTENT );
				ipv6->family = NETWORK_ADDRESSFAMILY_IPV6;
				ipv6->address_size = sizeof( struct sockaddr_in6 );
				ipv6->saddr.sin6_family = AF_INET6;
				memcpy( &ipv6->saddr, unicast->Address.lpSockaddr, sizeof( struct sockaddr_in6 ) );

				array_push( addresses, (network_address_t*)ipv6 );
			}
		}
	}

	memory_deallocate( adapter_address );

#else

	struct ifaddrs* ifaddr = 0;
	struct ifaddrs* ifa = 0;

	if( getifaddrs( &ifaddr ) < 0 )
	{
		log_context_errorf( HASH_NETWORK, ERROR_SYSTEM_CALL_FAIL, "Unable to get interface addresses: %s", system_error_message( NETWORK_RESOLV_ERROR ) );
		error_context_pop();
		return 0;
	}

	for( ifa = ifaddr; ifa; ifa = ifa->ifa_next )
	{
		if( !ifa->ifa_addr )
			continue;

		if( ifa->ifa_addr->sa_family == AF_INET )
		{
			network_address_ipv4_t* ipv4 = memory_allocate_zero_context( MEMORYCONTEXT_NETWORK, sizeof( network_address_ipv4_t ), 0, MEMORY_PERSISTENT );
			ipv4->family = NETWORK_ADDRESSFAMILY_IPV4;
			ipv4->address_size = sizeof( struct sockaddr_in );
			ipv4->saddr.sin_family = AF_INET;
			memcpy( &ipv4->saddr, ifa->ifa_addr, sizeof( struct sockaddr_in ) );

			array_push( addresses, (network_address_t*)ipv4 );
		}
		else if( ifa->ifa_addr->sa_family == AF_INET6 )
		{
			network_address_ipv6_t* ipv6 = memory_allocate_zero_context( MEMORYCONTEXT_NETWORK, sizeof( network_address_ipv6_t ), 0, MEMORY_PERSISTENT );
			ipv6->family = NETWORK_ADDRESSFAMILY_IPV6;
			ipv6->address_size = sizeof( struct sockaddr_in6 );
			ipv6->saddr.sin6_family = AF_INET6;
			memcpy( &ipv6->saddr, ifa->ifa_addr, sizeof( struct sockaddr_in6 ) );

			array_push( addresses, (network_address_t*)ipv6 );
		}
	}

	freeifaddrs( ifaddr );

#endif

	error_context_pop();

	return addresses;
}


network_address_family_t network_address_family( const network_address_t* address )
{
	return address->family;
}


bool network_address_equal( const network_address_t* first, const network_address_t* second )
{
	if( first == second )
		return true;
	if( !first || !second || ( first->address_size != second->address_size ) )
		return false;
	return memcmp( first, second, first->address_size ) == 0;
}


void network_address_array_deallocate( network_address_t** addresses )
{
	unsigned int iaddr, asize;
	for( iaddr = 0, asize = array_size( addresses ); iaddr < asize; ++iaddr )
		memory_deallocate( addresses[iaddr] );
	array_deallocate( addresses );
}
