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


application_t test_address_application( void )
{
	application_t app = {0};
	app.name = "Network address tests";
	app.short_name = "test_address";
	app.config_dir = "test_address";
	app.flags = APPLICATION_UTILITY;
	return app;
}


memory_system_t test_address_memory_system( void )
{
	return memory_system_malloc();
}


int test_address_initialize( void )
{
	log_set_suppress( HASH_NETWORK, ERRORLEVEL_NONE );
	return network_initialize( 32 );
}


void test_address_shutdown( void )
{
	network_shutdown();
}


DECLARE_TEST( address, local )
{
	bool found_localhost = false;
	unsigned int iaddr, iother, addrsize;
	network_address_t** addresses = network_address_local();

	EXPECT_GT( array_size( addresses ), 1 );

	log_debugf( HASH_NETWORK, "%u local addresses (%s)", array_size( addresses ), system_hostname() );
	for( iaddr = 0, addrsize = array_size( addresses ); iaddr < addrsize; ++iaddr )
	{
		char* address_str = network_address_to_string( addresses[iaddr], true );
		log_debugf( HASH_NETWORK, "  %s", address_str );

		if( string_equal( address_str, "127.0.0.1" ) )
			found_localhost = true;
		
		string_deallocate( address_str );
		memory_deallocate( addresses[iaddr] );

		for( iother = iaddr + 1; iother < addrsize; ++iother )
		{
			bool addr_equal = network_address_equal( addresses[iaddr], addresses[iother] );
			EXPECT_FALSE( addr_equal );
		}
	}
	array_deallocate( addresses );

	EXPECT_TRUE( found_localhost );

	return 0;
}


DECLARE_TEST( address, resolve )
{
	unsigned int iaddr, addrsize;
	unsigned int num_addresses = 0;
	
	network_address_t** addresses = network_address_resolve( "localhost" );
	log_debugf( HASH_NETWORK, "localhost -> %u addresses", array_size( addresses ) );
	EXPECT_GT( array_size( addresses ), 0 );
	num_addresses = array_size( addresses );
	for( iaddr = 0, addrsize = array_size( addresses ); iaddr < addrsize; ++iaddr )
	{
		char* address_str = network_address_to_string( addresses[iaddr], true );
		log_debugf( HASH_NETWORK, "  %s", address_str );
		EXPECT_TRUE( string_equal( address_str, "127.0.0.1" ) || string_equal( address_str, "::1" ) || string_equal_substr( address_str, "fe80:", 5 ) );
		string_deallocate( address_str );
		memory_deallocate( addresses[iaddr] );
	}
	array_deallocate( addresses );

	addresses = network_address_resolve( "localhost:80" );
	log_debugf( HASH_NETWORK, "localhost:80 -> %u addresses", array_size( addresses ) );
	EXPECT_EQ( (unsigned int)array_size( addresses ), num_addresses );
	num_addresses = array_size( addresses );
	for( iaddr = 0, addrsize = array_size( addresses ); iaddr < addrsize; ++iaddr )
	{
		char* address_str = network_address_to_string( addresses[iaddr], true );
		log_debugf( HASH_NETWORK, "  %s", address_str );
		EXPECT_TRUE( string_equal( address_str, "127.0.0.1:80" ) || string_equal( address_str, "[::1]:80" ) || string_match_pattern( address_str, "[fe80:*]:80" ) );
		string_deallocate( address_str );
		memory_deallocate( addresses[iaddr] );
	}	
	array_deallocate( addresses );

	addresses = network_address_resolve( "127.0.0.1" );
	log_debugf( HASH_NETWORK, "127.0.0.1 -> %u addresses", array_size( addresses ) );
	EXPECT_EQ( array_size( addresses ), 1 );
	for( iaddr = 0, addrsize = array_size( addresses ); iaddr < addrsize; ++iaddr )
	{
		char* address_str = network_address_to_string( addresses[iaddr], true );
		log_debugf( HASH_NETWORK, "  %s", address_str );
		EXPECT_TRUE( string_equal( address_str, "127.0.0.1" ) );
		string_deallocate( address_str );
		memory_deallocate( addresses[iaddr] );
	}	
	array_deallocate( addresses );

	addresses = network_address_resolve( "::1" );
	log_debugf( HASH_NETWORK, "::1 -> %u addresses", array_size( addresses ) );
	EXPECT_EQ( array_size( addresses ), 1 );
	for( iaddr = 0, addrsize = array_size( addresses ); iaddr < addrsize; ++iaddr )
	{
		char* address_str = network_address_to_string( addresses[iaddr], true );
		log_debugf( HASH_NETWORK, "  %s", address_str );
		EXPECT_TRUE( string_equal( address_str, "::1" ) );
		string_deallocate( address_str );
		memory_deallocate( addresses[iaddr] );
	}	
	array_deallocate( addresses );

	addresses = network_address_resolve( "127.0.0.1:512" );
	log_debugf( HASH_NETWORK, "127.0.0.1:512 -> %u addresses", array_size( addresses ) );
	EXPECT_EQ( array_size( addresses ), 1 );
	for( iaddr = 0, addrsize = array_size( addresses ); iaddr < addrsize; ++iaddr )
	{
		char* address_str = network_address_to_string( addresses[iaddr], true );
		log_debugf( HASH_NETWORK, "  %s", address_str );
		EXPECT_TRUE( string_equal( address_str, "127.0.0.1:512" ) );
		string_deallocate( address_str );
		memory_deallocate( addresses[iaddr] );
	}	
	array_deallocate( addresses );

	addresses = network_address_resolve( "[::1]:512" );
	log_debugf( HASH_NETWORK, "[::1]:512 -> %u addresses", array_size( addresses ) );
	EXPECT_EQ( array_size( addresses ), 1 );
	for( iaddr = 0, addrsize = array_size( addresses ); iaddr < addrsize; ++iaddr )
	{
		char* address_str = network_address_to_string( addresses[iaddr], true );
		log_debugf( HASH_NETWORK, "  %s", address_str );
		EXPECT_TRUE( string_equal( address_str, "[::1]:512" ) );
		string_deallocate( address_str );
		memory_deallocate( addresses[iaddr] );
	}	
	array_deallocate( addresses );

	addresses = network_address_resolve( "zion.rampantpixels.com:1234" );
	log_debugf( HASH_NETWORK, "zion.rampantpixels.com:1234 -> %u addresses", array_size( addresses ) );
	EXPECT_GE( array_size( addresses ), 1 );
	for( iaddr = 0, addrsize = array_size( addresses ); iaddr < addrsize; ++iaddr )
	{
		char* address_str = network_address_to_string( addresses[iaddr], true );
		log_debugf( HASH_NETWORK, "  %s", address_str );
		string_deallocate( address_str );
		memory_deallocate( addresses[iaddr] );
	}	
	array_deallocate( addresses );

	addresses = network_address_resolve( "www.google.com" );
	log_debugf( HASH_NETWORK, "www.google.com -> %u addresses", array_size( addresses ) );
	EXPECT_GT( array_size( addresses ), 2 );
	for( iaddr = 0, addrsize = array_size( addresses ); iaddr < addrsize; ++iaddr )
	{
		char* address_str = network_address_to_string( addresses[iaddr], true );
		log_debugf( HASH_NETWORK, "  %s", address_str );
		string_deallocate( address_str );
		memory_deallocate( addresses[iaddr] );
	}	
	array_deallocate( addresses );
	
	return 0;
}


DECLARE_TEST( address, any )
{
	char* address_str;
	network_address_t** any_resolve;

	network_address_t* any = network_address_ipv4_any();
	EXPECT_NE( any, 0 );
	address_str = network_address_to_string( any, true );
	log_debugf( HASH_NETWORK, "IPv4 any: %s", address_str );
	EXPECT_STREQ( address_str, "0.0.0.0" );

	any_resolve = network_address_resolve( address_str );
	EXPECT_EQ( array_size( any_resolve ), 1 );
	EXPECT_TRUE( network_address_equal( any, any_resolve[0] ) );
	
	string_deallocate( address_str );
	memory_deallocate( any_resolve[0] );
	memory_deallocate( any );
	array_deallocate( any_resolve );

	any = network_address_ipv6_any();
	EXPECT_NE( any, 0 );
	address_str = network_address_to_string( any, true );
	log_debugf( HASH_NETWORK, "IPv6 any: %s", address_str );
	EXPECT_STREQ( address_str, "::" );

	any_resolve = network_address_resolve( address_str );
	EXPECT_EQ( array_size( any_resolve ), 1 );
	EXPECT_TRUE( network_address_equal( any, any_resolve[0] ) );
	
	string_deallocate( address_str );
	memory_deallocate( any_resolve[0] );
	memory_deallocate( any );
	array_deallocate( any_resolve );
	
	return 0;
}


DECLARE_TEST( address, port )
{
	network_address_t** any_resolve;
	char* address_str;

	network_address_t* any = network_address_ipv4_any();
	EXPECT_EQ( network_address_ip_port( any ), 0 );

	network_address_ip_set_port( any, 80 );
	EXPECT_EQ( network_address_ip_port( any ), 80 );	

	address_str = network_address_to_string( any, true );
	log_debugf( HASH_NETWORK, "IPv4 any: %s", address_str );
	EXPECT_STREQ( address_str, "0.0.0.0:80" );

	any_resolve = network_address_resolve( address_str );
	EXPECT_EQ( array_size( any_resolve ), 1 );
	EXPECT_TRUE( network_address_equal( any, any_resolve[0] ) );
	
	string_deallocate( address_str );
	memory_deallocate( any_resolve[0] );
	memory_deallocate( any );
	array_deallocate( any_resolve );

	any = network_address_ipv6_any();
	EXPECT_EQ( network_address_ip_port( any ), 0 );

	network_address_ip_set_port( any, 80 );
	EXPECT_EQ( network_address_ip_port( any ), 80 );	

	address_str = network_address_to_string( any, true );
	log_debugf( HASH_NETWORK, "IPv6 any: %s", address_str );
	EXPECT_STREQ( address_str, "[::]:80" );

	any_resolve = network_address_resolve( address_str );
	EXPECT_EQ( array_size( any_resolve ), 1 );
	EXPECT_TRUE( network_address_equal( any, any_resolve[0] ) );
	
	string_deallocate( address_str );
	memory_deallocate( any_resolve[0] );
	memory_deallocate( any );
	array_deallocate( any_resolve );
	
	return 0;
}


DECLARE_TEST( address, family )
{
	network_address_t** any_resolve;

	network_address_t* any = network_address_ipv4_any();
	EXPECT_EQ( network_address_family( any ), NETWORK_ADDRESSFAMILY_IPV4 );

	network_address_ip_set_port( any, 80 );
	EXPECT_EQ( network_address_family( any ), NETWORK_ADDRESSFAMILY_IPV4 );

	any_resolve = network_address_resolve( "0.0.0.0:80" );
	EXPECT_EQ( array_size( any_resolve ), 1 );
	EXPECT_EQ( network_address_family( any_resolve[0] ), NETWORK_ADDRESSFAMILY_IPV4 );
	
	memory_deallocate( any_resolve[0] );
	memory_deallocate( any );
	array_deallocate( any_resolve );

	any = network_address_ipv6_any();
	EXPECT_EQ( network_address_family( any ), NETWORK_ADDRESSFAMILY_IPV6 );

	network_address_ip_set_port( any, 80 );
	EXPECT_EQ( network_address_family( any ), NETWORK_ADDRESSFAMILY_IPV6 );

	any_resolve = network_address_resolve( "[::]:80" );
	EXPECT_EQ( array_size( any_resolve ), 1 );
	EXPECT_EQ( network_address_family( any_resolve[0] ), NETWORK_ADDRESSFAMILY_IPV6 );
	
	memory_deallocate( any_resolve[0] );
	memory_deallocate( any );
	array_deallocate( any_resolve );

	return 0;
}


void test_address_declare( void )
{
	ADD_TEST( address, local );
	ADD_TEST( address, resolve );
	ADD_TEST( address, any );
	ADD_TEST( address, port );
	ADD_TEST( address, family );
}


test_suite_t test_address_suite = {
	test_address_application,
	test_address_memory_system,
	test_address_declare,
	test_address_initialize,
	test_address_shutdown
};


#if FOUNDATION_PLATFORM_ANDROID

int test_address_run( void )
{
	test_suite = test_address_suite;
	return test_run_all();
}

#else

test_suite_t test_suite_define( void )
{
	return test_address_suite;
}

#endif

