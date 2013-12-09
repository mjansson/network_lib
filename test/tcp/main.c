/* main.c  -  Network library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 * 
 * https://github.com/rampantpixels/network_lib
 * 
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/network.h>

#include <foundation/foundation.h>
#include <test/test.h>


application_t test_tcp_application( void )
{
	application_t app = {0};
	app.name = "Network TCP/IP tests";
	app.short_name = "test_tcp";
	app.config_dir = "test_tcp";
	app.flags = APPLICATION_UTILITY;
	return app;
}


memory_system_t test_tcp_memory_system( void )
{
	return memory_system_malloc();
}


int test_tcp_initialize( void )
{
	//log_suppress( ERRORLEVEL_NONE );	
	return network_initialize( 300 );
}


void test_tcp_shutdown( void )
{
	network_shutdown();
}


static void* io_thread( object_t thread, void* arg )
{
	int iloop;

	object_t sock = *(object_t*)arg;

	char buffer_out[317] = {0};
	char buffer_in[317] = {0};
	
	stream_t* stream = socket_stream( sock );

	for( iloop = 0; iloop < 512; ++iloop )
	{
		stream_write( stream, buffer_out, 317 );
		stream_flush( stream );	
		stream_read( stream, buffer_in, 317 );
	}

	log_debugf( HASH_NETWORK, "IO complete on socket 0x%llx", sock );
	stream_deallocate( stream );
	
	return 0;
}



DECLARE_TEST( tcp, connect_ipv4 )
{
	int iaddr;
	bool success = false;
	network_address_t** addresses;

	//Blocking with timeout
	object_t sock_client = tcp_socket_create();

	EXPECT_TRUE( socket_is_socket( sock_client ) );	

	socket_set_blocking( sock_client, true );
	
	addresses = network_address_resolve( "www.rampantpixels.com:80" );
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
	{
		if( network_address_family( addresses[iaddr] ) != NETWORK_ADDRESSFAMILY_IPV4 )
			continue;
		
		success = socket_connect( sock_client, addresses[iaddr], 5000 );
	}
	
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
		memory_deallocate( addresses[iaddr] );
	array_deallocate( addresses );
	
	EXPECT_TRUE( success );
	EXPECT_EQ( socket_state( sock_client ), SOCKETSTATE_CONNECTED );

	socket_free( sock_client );

	EXPECT_FALSE( socket_is_socket( sock_client ) );


	//Blocking without timeout
	success = false;
	sock_client = tcp_socket_create();

	EXPECT_TRUE( socket_is_socket( sock_client ) );	

	socket_set_blocking( sock_client, true );
	
	addresses = network_address_resolve( "www.rampantpixels.com:80" );
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
	{
		if( network_address_family( addresses[iaddr] ) != NETWORK_ADDRESSFAMILY_IPV4 )
			continue;
		
		success = socket_connect( sock_client, addresses[iaddr], 0 );
	}
	
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
		memory_deallocate( addresses[iaddr] );
	array_deallocate( addresses );
	
	EXPECT_TRUE( success );
	EXPECT_EQ( socket_state( sock_client ), SOCKETSTATE_CONNECTED );

	socket_free( sock_client );

	EXPECT_FALSE( socket_is_socket( sock_client ) );

	
	//Unblocking with timeout
	sock_client = tcp_socket_create();

	EXPECT_TRUE( socket_is_socket( sock_client ) );	

	socket_set_blocking( sock_client, false );
	
	addresses = network_address_resolve( "www.rampantpixels.com:80" );
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
	{
		if( network_address_family( addresses[iaddr] ) != NETWORK_ADDRESSFAMILY_IPV4 )
			continue;
		
		success = socket_connect( sock_client, addresses[iaddr], 5000 );
	}
	
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
		memory_deallocate( addresses[iaddr] );
	array_deallocate( addresses );
	
	EXPECT_TRUE( success );
	EXPECT_EQ( socket_state( sock_client ), SOCKETSTATE_CONNECTED );

	socket_free( sock_client );

	EXPECT_FALSE( socket_is_socket( sock_client ) );


	//Unblocking without timeout
	success = false;
	sock_client = tcp_socket_create();

	EXPECT_TRUE( socket_is_socket( sock_client ) );	

	socket_set_blocking( sock_client, false );
	
	addresses = network_address_resolve( "www.rampantpixels.com:80" );
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
	{
		if( network_address_family( addresses[iaddr] ) != NETWORK_ADDRESSFAMILY_IPV4 )
			continue;
		
		success = socket_connect( sock_client, addresses[iaddr], 0 );
	}
	
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
		memory_deallocate( addresses[iaddr] );
	array_deallocate( addresses );
	
	EXPECT_TRUE( success );
	EXPECT_TRUE( ( socket_state( sock_client ) == SOCKETSTATE_CONNECTING ) || ( socket_state( sock_client ) == SOCKETSTATE_CONNECTED ) );

	socket_free( sock_client );

	EXPECT_FALSE( socket_is_socket( sock_client ) );
	
	return 0;
}


DECLARE_TEST( tcp, connect_ipv6 )
{
	int iaddr;
	bool success = false;
	object_t sock_client;
	network_address_t** addresses;

	log_suppress( ERRORLEVEL_NONE );

	//Blocking with timeout
	sock_client = tcp_socket_create();

	EXPECT_TRUE( socket_is_socket( sock_client ) );	

	socket_set_blocking( sock_client, true );
	
	addresses = network_address_resolve( "www.rampantpixels.com:80" );
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
	{
		if( network_address_family( addresses[iaddr] ) != NETWORK_ADDRESSFAMILY_IPV6 )
			continue;
		
		success = socket_connect( sock_client, addresses[iaddr], 5000 );
	}
	
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
		memory_deallocate( addresses[iaddr] );
	array_deallocate( addresses );
	
	EXPECT_TRUE( success );
	EXPECT_EQ( socket_state( sock_client ), SOCKETSTATE_CONNECTED );

	socket_free( sock_client );

	EXPECT_FALSE( socket_is_socket( sock_client ) );


	//Blocking without timeout
	success = false;
	sock_client = tcp_socket_create();

	EXPECT_TRUE( socket_is_socket( sock_client ) );	

	socket_set_blocking( sock_client, true );
	
	addresses = network_address_resolve( "www.rampantpixels.com:80" );
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
	{
		if( network_address_family( addresses[iaddr] ) != NETWORK_ADDRESSFAMILY_IPV6 )
			continue;
		
		success = socket_connect( sock_client, addresses[iaddr], 0 );
	}
	
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
		memory_deallocate( addresses[iaddr] );
	array_deallocate( addresses );
	
	EXPECT_TRUE( success );
	EXPECT_EQ( socket_state( sock_client ), SOCKETSTATE_CONNECTED );

	socket_free( sock_client );

	EXPECT_FALSE( socket_is_socket( sock_client ) );

	
	//Unblocking with timeout
	sock_client = tcp_socket_create();

	EXPECT_TRUE( socket_is_socket( sock_client ) );	

	socket_set_blocking( sock_client, false );
	
	addresses = network_address_resolve( "www.rampantpixels.com:80" );
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
	{
		if( network_address_family( addresses[iaddr] ) != NETWORK_ADDRESSFAMILY_IPV6 )
			continue;
		
		success = socket_connect( sock_client, addresses[iaddr], 5000 );
	}
	
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
		memory_deallocate( addresses[iaddr] );
	array_deallocate( addresses );
	
	EXPECT_TRUE( success );
	EXPECT_EQ( socket_state( sock_client ), SOCKETSTATE_CONNECTED );

	socket_free( sock_client );

	EXPECT_FALSE( socket_is_socket( sock_client ) );


	//Unblocking without timeout
	success = false;
	sock_client = tcp_socket_create();

	EXPECT_TRUE( socket_is_socket( sock_client ) );	

	socket_set_blocking( sock_client, false );
	
	addresses = network_address_resolve( "www.rampantpixels.com:80" );
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
	{
		if( network_address_family( addresses[iaddr] ) != NETWORK_ADDRESSFAMILY_IPV6 )
			continue;
		
		success = socket_connect( sock_client, addresses[iaddr], 0 );
	}
	
	for( iaddr = 0; iaddr < array_size( addresses ); ++iaddr )
		memory_deallocate( addresses[iaddr] );
	array_deallocate( addresses );
	
	EXPECT_TRUE( success );
	EXPECT_EQ( socket_state( sock_client ), SOCKETSTATE_CONNECTING );

	socket_free( sock_client );

	EXPECT_FALSE( socket_is_socket( sock_client ) );
	
	return 0;
}


DECLARE_TEST( tcp, io_ipv4 )
{
	network_address_t* address_bind;
	network_address_t** address_local;
	network_address_t* address_connect;

	int state, iaddr, asize;
	object_t threads[2] = {0};

	object_t sock_listen = tcp_socket_create();
	object_t sock_server = 0;
	object_t sock_client = tcp_socket_create();

	EXPECT_TRUE( socket_is_socket( sock_listen ) );
	EXPECT_TRUE( socket_is_socket( sock_client ) );

	address_bind = network_address_ipv4_any();
	socket_bind( sock_listen, address_bind );
	memory_deallocate( address_bind );

	tcp_socket_listen( sock_listen );
	EXPECT_EQ( socket_state( sock_listen ), SOCKETSTATE_LISTENING );

	address_local = network_address_local();
	address_connect = 0;
	for( iaddr = 0, asize = array_size( address_local ); iaddr < asize; ++iaddr )
	{
		if( network_address_family( address_local[iaddr] ) == NETWORK_ADDRESSFAMILY_IPV4 )
		{
			address_connect = address_local[iaddr];
			break;
		}
	}
	EXPECT_NE( address_connect, 0 );
	network_address_ip_set_port( address_connect, network_address_ip_port( socket_address_local( sock_listen ) ) );
	socket_set_blocking( sock_client, false );
	socket_connect( sock_client, address_connect, 0 );
	state = socket_state( sock_client );
	network_address_array_deallocate( address_local );
	EXPECT_TRUE( ( state == SOCKETSTATE_CONNECTING ) || ( state == SOCKETSTATE_CONNECTED ) );

	sock_server = tcp_socket_accept( sock_listen, 0 );
	EXPECT_EQ( socket_state( sock_client ), SOCKETSTATE_CONNECTED );
	EXPECT_EQ( socket_state( sock_server ), SOCKETSTATE_CONNECTED );

	socket_free( sock_listen );
	EXPECT_FALSE( socket_is_socket( sock_listen ) );

	threads[0] = thread_create( io_thread, "io_thread", THREAD_PRIORITY_NORMAL, 0 );
	threads[1] = thread_create( io_thread, "io_thread", THREAD_PRIORITY_NORMAL, 0 );

	thread_start( threads[0], &sock_server );
	thread_start( threads[1], &sock_client );
	
	test_wait_for_threads_startup( threads, 2 );

	thread_destroy( threads[0] );
	thread_destroy( threads[1] );

	test_wait_for_threads_exit( threads, 2 );
	
	socket_free( sock_server );
	socket_free( sock_client );

	EXPECT_FALSE( socket_is_socket( sock_server ) );
	EXPECT_FALSE( socket_is_socket( sock_client ) );
	
	return 0;
}


DECLARE_TEST( tcp, io_ipv6 )
{
	network_address_t* address_bind;
	network_address_t** address_local;
	network_address_t* address_connect;

	int state, iaddr, asize;
	object_t threads[2] = {0};

	object_t sock_listen = tcp_socket_create();
	object_t sock_server = 0;
	object_t sock_client = tcp_socket_create();

	EXPECT_TRUE( socket_is_socket( sock_listen ) );
	EXPECT_TRUE( socket_is_socket( sock_client ) );

	address_bind = network_address_ipv6_any();
	socket_bind( sock_listen, address_bind );
	memory_deallocate( address_bind );

	tcp_socket_listen( sock_listen );
	EXPECT_EQ( socket_state( sock_listen ), SOCKETSTATE_LISTENING );

	address_local = network_address_local();
	address_connect = 0;
	for( iaddr = 0, asize = array_size( address_local ); iaddr < asize; ++iaddr )
	{
		if( network_address_family( address_local[iaddr] ) == NETWORK_ADDRESSFAMILY_IPV6 )
		{
			address_connect = address_local[iaddr];
			break;
		}
	}
	EXPECT_NE( address_connect, 0 );
	network_address_ip_set_port( address_connect, network_address_ip_port( socket_address_local( sock_listen ) ) );
	socket_set_blocking( sock_client, false );
	socket_connect( sock_client, address_connect, 0 );
	state = socket_state( sock_client );
	EXPECT_TRUE( ( state == SOCKETSTATE_CONNECTING ) || ( state == SOCKETSTATE_CONNECTED ) );

	sock_server = tcp_socket_accept( sock_listen, 0 );
	EXPECT_EQ( socket_state( sock_client ), SOCKETSTATE_CONNECTED );
	EXPECT_EQ( socket_state( sock_server ), SOCKETSTATE_CONNECTED );

	socket_free( sock_listen );
	EXPECT_FALSE( socket_is_socket( sock_listen ) );
	
	threads[0] = thread_create( io_thread, "io_thread", THREAD_PRIORITY_NORMAL, 0 );
	threads[1] = thread_create( io_thread, "io_thread", THREAD_PRIORITY_NORMAL, 0 );

	thread_start( threads[0], &sock_server );
	thread_start( threads[1], &sock_client );
	
	test_wait_for_threads_startup( threads, 2 );

	thread_destroy( threads[0] );
	thread_destroy( threads[1] );

	test_wait_for_threads_exit( threads, 2 );
	
	socket_free( sock_server );
	socket_free( sock_client );

	EXPECT_FALSE( socket_is_socket( sock_server ) );
	EXPECT_FALSE( socket_is_socket( sock_client ) );
	
	return 0;
}


void test_tcp_declare( void )
{
	ADD_TEST( tcp, connect_ipv4 );
	//ADD_TEST( tcp, connect_ipv6 );
	ADD_TEST( tcp, io_ipv4 );
	ADD_TEST( tcp, io_ipv6 );
}


test_suite_t test_tcp_suite = {
	test_tcp_application,
	test_tcp_memory_system,
	test_tcp_declare,
	test_tcp_initialize,
	test_tcp_shutdown
};


#if FOUNDATION_PLATFORM_ANDROID

int test_tcp_run( void )
{
	test_suite = test_tcp_suite;
	return test_run_all();
}

#else

test_suite_t test_suite_define( void )
{
	return test_tcp_suite;
}

#endif

