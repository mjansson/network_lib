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


application_t test_udp_application( void )
{
	application_t app = {0};
	app.name = "Network UDP tests";
	app.short_name = "test_udp";
	app.config_dir = "test_udp";
	app.flags = APPLICATION_UTILITY;
	return app;
}


memory_system_t test_udp_memory_system( void )
{
	return memory_system_malloc();
}


int test_udp_initialize( void )
{
	log_set_suppress( HASH_NETWORK, ERRORLEVEL_INFO );
	return network_initialize( 300 );
}


void test_udp_shutdown( void )
{
	network_shutdown();
}


static void* io_blocking_thread( object_t thread, void* arg )
{
	int iloop;

	object_t sock = *(object_t*)arg;

	char buffer_out[317] = {0};
	char buffer_in[317] = {0};
	
	stream_t* stream = socket_stream( sock );

	for( iloop = 0; iloop < 512; ++iloop )
	{
		log_infof( HASH_NETWORK, "UDP write pass %d", iloop );
		EXPECT_EQ( stream_write( stream, buffer_out, 127 ), 127 );
		EXPECT_EQ( stream_write( stream, buffer_out + 127, 180 ), 180 );
		stream_flush( stream );	
		EXPECT_EQ( stream_write( stream, buffer_out + 307, 10 ), 10 );
		stream_flush( stream );	
		log_infof( HASH_NETWORK, "UDP read pass %d", iloop );
		EXPECT_EQ( stream_read( stream, buffer_in, 235 ), 235 );
		EXPECT_EQ( stream_read( stream, buffer_in + 235, 82 ), 82 );
		thread_yield();
	}

	log_debugf( HASH_NETWORK, "IO complete on socket 0x%llx", sock );
	stream_deallocate( stream );
	
	return 0;
}


DECLARE_TEST( udp, io_ipv4 )
{
	network_address_t** address_local;
	network_address_t* address;

	int server_port, client_port;
	int state, iaddr, asize;
	object_t threads[2] = {0};

	object_t sock_server = udp_socket_create();
	object_t sock_client = udp_socket_create();

	EXPECT_TRUE( socket_is_socket( sock_server ) );
	EXPECT_TRUE( socket_is_socket( sock_client ) );

	address_local = network_address_local();
	for( iaddr = 0, asize = array_size( address_local ); iaddr < asize; ++iaddr )
	{
		if( network_address_family( address_local[iaddr] ) == NETWORK_ADDRESSFAMILY_IPV4 )
		{
			address = address_local[iaddr];
			break;
		}
	}
	EXPECT_NE( address, 0 );

	do
	{
		server_port = random32_range( 1024, 35535 );
		network_address_ip_set_port( address, server_port );
		if( socket_bind( sock_server, address ) )
			break;
	} while( true );

	do
	{
		client_port = random32_range( 1024, 35535 );
		network_address_ip_set_port( address, client_port );
		if( socket_bind( sock_client, address ) )
			break;
	} while( true );
	
	socket_set_blocking( sock_server, false );
	socket_set_blocking( sock_client, false );

	network_address_ip_set_port( address, client_port );
	socket_connect( sock_server, address, 0 );

	network_address_ip_set_port( address, server_port );
	socket_connect( sock_client, address, 0 );

	network_address_array_deallocate( address_local );
	
	state = socket_state( sock_server );
	EXPECT_TRUE( state == SOCKETSTATE_CONNECTED );
	
	state = socket_state( sock_client );
	EXPECT_TRUE( state == SOCKETSTATE_CONNECTED );

	socket_set_blocking( sock_server, true );
	socket_set_blocking( sock_client, true );

	threads[0] = thread_create( io_blocking_thread, "io_thread", THREAD_PRIORITY_NORMAL, 0 );
	threads[1] = thread_create( io_blocking_thread, "io_thread", THREAD_PRIORITY_NORMAL, 0 );

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


DECLARE_TEST( udp, io_ipv6 )
{
	network_address_t** address_local;
	network_address_t* address;

	int server_port, client_port;
	int state, iaddr, asize;
	object_t threads[2] = {0};

	object_t sock_server = udp_socket_create();
	object_t sock_client = udp_socket_create();

	EXPECT_TRUE( socket_is_socket( sock_server ) );
	EXPECT_TRUE( socket_is_socket( sock_client ) );

	address_local = network_address_local();
	for( iaddr = 0, asize = array_size( address_local ); iaddr < asize; ++iaddr )
	{
		if( network_address_family( address_local[iaddr] ) == NETWORK_ADDRESSFAMILY_IPV6 )
		{
			address = address_local[iaddr];
			break;
		}
	}
	EXPECT_NE( address, 0 );

	do
	{
		server_port = random32_range( 1024, 35535 );
		network_address_ip_set_port( address, server_port );
		if( socket_bind( sock_server, address ) )
			break;
	} while( true );

	do
	{
		client_port = random32_range( 1024, 35535 );
		network_address_ip_set_port( address, client_port );
		if( socket_bind( sock_client, address ) )
			break;
	} while( true );
	
	socket_set_blocking( sock_server, false );
	socket_set_blocking( sock_client, false );

	network_address_ip_set_port( address, client_port );
	socket_connect( sock_server, address, 0 );

	network_address_ip_set_port( address, server_port );
	socket_connect( sock_client, address, 0 );

	network_address_array_deallocate( address_local );
	
	state = socket_state( sock_server );
	EXPECT_TRUE( state == SOCKETSTATE_CONNECTED );
	
	state = socket_state( sock_client );
	EXPECT_TRUE( state == SOCKETSTATE_CONNECTED );

	socket_set_blocking( sock_server, true );
	socket_set_blocking( sock_client, true );

	threads[0] = thread_create( io_blocking_thread, "io_thread", THREAD_PRIORITY_NORMAL, 0 );
	threads[1] = thread_create( io_blocking_thread, "io_thread", THREAD_PRIORITY_NORMAL, 0 );

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


void test_udp_declare( void )
{
	ADD_TEST( udp, io_ipv4 );
	ADD_TEST( udp, io_ipv6 );
}


test_suite_t test_udp_suite = {
	test_udp_application,
	test_udp_memory_system,
	test_udp_declare,
	test_udp_initialize,
	test_udp_shutdown
};


#if FOUNDATION_PLATFORM_ANDROID

int test_udp_run( void )
{
	test_suite = test_udp_suite;
	return test_run_all();
}

#else

test_suite_t test_suite_define( void )
{
	return test_udp_suite;
}

#endif

