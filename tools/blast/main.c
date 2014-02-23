/* main.c  -  Network blast tool  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 * 
 * https://github.com/rampantpixels/network_lib
 * 
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <foundation/foundation.h>
#include <network/network.h>

#include "errorcodes.h"


hash_t                      HASH_BLAST = 0;

typedef enum _blast_mode
{
	BLAST_NONE = 0,
	BLAST_SERVER,
	BLAST_CLIENT
} blast_mode_t;


typedef struct
{
	blast_mode_t            mode;

	//Server args
	network_address_t**     bind;
	bool                    daemon;

	//Client args
	network_address_t**     target;
	char**                  files;
} blast_input_t;


static bool                 should_exit = false;


static blast_input_t        blast_parse_command_line( char const* const* cmdline );

static void                 blast_print_usage( void );

static void*                blast_event_thread( object_t thread, void* arg );

extern int                  blast_server( network_address_t** bind, bool daemon );
extern int                  blast_client( network_address_t** target, char** files );

bool                        blast_should_exit( void );
void                        blast_process_system_events( void );


int main_initialize( void )
{
	int ret = 0;

	HASH_BLAST = hash( "blast", 5 );

	application_t application = {0};
	application.name = "blast";
	application.short_name = "blast";
	application.config_dir = "blast";
	application.flags = APPLICATION_UTILITY;

	log_enable_prefix( false );
	log_set_suppress( 0, ERRORLEVEL_INFO );

	if( ( ret = foundation_initialize( memory_system_malloc(), application ) ) < 0 )
		return ret;

	log_set_suppress( HASH_NETWORK, ERRORLEVEL_INFO );
	log_set_suppress( HASH_BLAST, ERRORLEVEL_DEBUG );

	if( ( ret = network_initialize( 1024 ) ) < 0 )
		return ret;

	config_set_int( HASH_FOUNDATION, HASH_TEMPORARY_MEMORY, 32 * 1024 );

	return 0;
}


int main_run( void* main_arg )
{
	int result = BLAST_RESULT_OK;
	object_t thread_event;

	blast_input_t input = blast_parse_command_line( environment_command_line() );

	thread_event = thread_create( blast_event_thread, "event_thread", THREAD_PRIORITY_NORMAL, 0 );
	thread_start( thread_event, 0 );

	while( !thread_is_running( thread_event ) )
		thread_yield();

	if( input.mode == BLAST_SERVER )
		result = blast_server( input.bind, input.daemon );
	else if( input.mode == BLAST_CLIENT )
		result = blast_client( input.target, input.files );
	else
		blast_print_usage();

	thread_terminate( thread_event );
	thread_destroy( thread_event );
	while( thread_is_running( thread_event ) || thread_is_thread( thread_event ) )
		thread_yield();

	string_array_deallocate( input.files );
	network_address_array_deallocate( input.bind );
	network_address_array_deallocate( input.target );

	return result;
}


void main_shutdown( void )
{
	network_shutdown();
	foundation_shutdown();
}


blast_input_t blast_parse_command_line( char const* const* cmdline )
{
	blast_input_t input = {0};
	int arg, asize;
	int addr, addrsize;

	error_context_push( "parsing command line", "" );
	for( arg = 1, asize = array_size( cmdline ); arg < asize; ++arg )
	{
		if( string_equal( cmdline[arg], "-s" ) || string_equal( cmdline[arg], "--server" ) )
			input.mode = BLAST_SERVER;
		else if( string_equal( cmdline[arg], "-c" ) || string_equal( cmdline[arg], "--client" ) )
			input.mode = BLAST_CLIENT;
		else if( string_equal( cmdline[arg], "-d" ) || string_equal( cmdline[arg], "--daemon" ) )
			input.daemon = true;
		else if( string_equal( cmdline[arg], "-b" ) || string_equal( cmdline[arg], "--bind" ) )
		{
			if( ++arg < asize )
			{
				network_address_t** resolved = network_address_resolve( cmdline[arg] );
				for( addr = 0, addrsize = array_size( resolved ); addr < addrsize; ++addr )
					array_push( input.bind, resolved[addr] );
			}
		}
		else if( string_equal( cmdline[arg], "-t" ) || string_equal( cmdline[arg], "--target" ) )
		{
			if( ++arg < asize )
			{
				network_address_t** resolved = network_address_resolve( cmdline[arg] );
				for( addr = 0, addrsize = array_size( resolved ); addr < addrsize; ++addr )
					array_push( input.target, resolved[addr] );
			}
		}
		else if( string_equal( cmdline[arg], "--" ) )
			break; //Stop parsing cmdline options
		else if( ( string_length( cmdline[arg] ) > 1 ) && ( cmdline[arg][0] == '-' ) )
			continue; //Cmdline argument not parsed here
		else
			array_push( input.files, string_clone( cmdline[arg] ) );
	}
	error_context_pop();

	return input;
}


void blast_print_usage( void )
{
	log_info( HASH_BLAST, 
		"blast usage:\n"
		"  blast [-s|--server] [-d|-daemon] [-c|--client] [-t|--target host[:port]] [-b|--bind host[:port]] <file> <file> <file> <...>\n"
		"    Required arguments for server:\n"
		"      -s|--server              Start as server\n"
		"      -b||-bind host[:port]    Bind ip address and optional port (muliple)\n"
		"    Required arguments for client:\n"
		"      -c|--client              Start as client\n"
		"      -t|--target host[:port]  Target host (ip or hostname) with optional port (muliple)\n"
		"      <file>                   File name (muliple)\n"
		"    Optional arguments:\n"
		"      -d|--daemon              Run server as daemon\n"
	);
}


void blast_process_system_events( void )
{
	event_block_t* block;
	event_t* event = 0;

	system_process_events();

	block = event_stream_process( system_event_stream() );
	event = 0;

	while( ( event = event_next( block, event ) ) )
	{
		switch( event->id )
		{
			case FOUNDATIONEVENT_TERMINATE:
				log_warn( HASH_BLAST, WARNING_SUSPICIOUS, "Terminating due to event" );
				should_exit = true;
				break;

			default:
				break;
		}
	}
}


void* blast_event_thread( object_t thread, void* arg )
{
	while( !thread_should_terminate( thread ) )
	{
		blast_process_system_events();
		thread_sleep( 1000 );
	}
	return 0;
}


bool blast_should_exit( void )
{
	return should_exit;
}
