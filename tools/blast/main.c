/* main.c  -  Network blast tool  -  Public Domain  -  2013 Mattias Jansson
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/mjansson/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include "blast.h"
#include "client.h"
#include "server.h"

typedef enum { BLAST_NONE = 0, BLAST_SERVER, BLAST_CLIENT } blast_mode_t;

typedef struct {
	blast_mode_t mode;

	// Server args
	network_address_t** bind;
	bool daemon;

	// Client args
	network_address_t*** target;
	string_t* files;
} blast_input_t;

static bool should_exit;

static blast_input_t
blast_parse_command_line(const string_const_t* cmdline);

static void
blast_print_usage(void);

int
main_initialize(void) {
	int ret = 0;
	foundation_config_t config;
	network_config_t network_config;
	application_t application;

	memset(&config, 0, sizeof(config));
	memset(&network_config, 0, sizeof(network_config));

	memset(&application, 0, sizeof(application));
	application.name = string_const(STRING_CONST("blast"));
	application.short_name = string_const(STRING_CONST("blast"));
	application.company = string_const(STRING_CONST(""));
	application.flags = APPLICATION_UTILITY;

	log_enable_prefix(false);
	log_set_suppress(0, ERRORLEVEL_INFO);

	if ((ret = foundation_initialize(memory_system_malloc(), application, config)) < 0)
		return ret;

	log_set_suppress(HASH_NETWORK, ERRORLEVEL_INFO);
	log_set_suppress(HASH_BLAST, ERRORLEVEL_DEBUG);

	if ((ret = network_module_initialize(network_config)) < 0)
		return ret;

	return 0;
}

int
main_run(void* main_arg) {
	unsigned int itarget, tsize = 0;
	int result = BLAST_RESULT_OK;

	FOUNDATION_UNUSED(main_arg);

	blast_input_t input = blast_parse_command_line(environment_command_line());

	if (input.mode == BLAST_SERVER)
		result = blast_server(input.bind, input.daemon);
	else if (input.mode == BLAST_CLIENT)
		result = blast_client(input.target, input.files);
	else
		blast_print_usage();

	string_array_deallocate(input.files);
	network_address_array_deallocate(input.bind);
	for (itarget = 0, tsize = array_size(input.target); itarget < tsize; ++itarget)
		network_address_array_deallocate(input.target[itarget]);
	array_deallocate(input.target);

	return result;
}

void
main_finalize(void) {
	network_module_finalize();
	foundation_finalize();
}

blast_input_t
blast_parse_command_line(const string_const_t* cmdline) {
	blast_input_t input;
	unsigned int arg, asize;
	unsigned int addr, addrsize;

	error_context_push(STRING_CONST("parsing command line"), STRING_CONST(""));
	memset(&input, 0, sizeof(input));
	for (arg = 1, asize = array_size(cmdline); arg < asize; ++arg) {
		if (string_equal(STRING_ARGS(cmdline[arg]), STRING_CONST("-s")) ||
		    string_equal(STRING_ARGS(cmdline[arg]), STRING_CONST("--server")))
			input.mode = BLAST_SERVER;
		else if (string_equal(STRING_ARGS(cmdline[arg]), STRING_CONST("-c")) ||
		         string_equal(STRING_ARGS(cmdline[arg]), STRING_CONST("--client")))
			input.mode = BLAST_CLIENT;
		else if (string_equal(STRING_ARGS(cmdline[arg]), STRING_CONST("-d")) ||
		         string_equal(STRING_ARGS(cmdline[arg]), STRING_CONST("--daemon")))
			input.daemon = true;
		else if (string_equal(STRING_ARGS(cmdline[arg]), STRING_CONST("-b")) ||
		         string_equal(STRING_ARGS(cmdline[arg]), STRING_CONST("--bind"))) {
			if (++arg < asize) {
				network_address_t** resolved = network_address_resolve(STRING_ARGS(cmdline[arg]));
				for (addr = 0, addrsize = array_size(resolved); addr < addrsize; ++addr)
					array_push(input.bind, resolved[addr]);
			}
		} else if (string_equal(STRING_ARGS(cmdline[arg]), STRING_CONST("-t")) ||
		           string_equal(STRING_ARGS(cmdline[arg]), STRING_CONST("--target"))) {
			if (++arg < asize) {
				network_address_t** resolved = network_address_resolve(STRING_ARGS(cmdline[arg]));
				if (resolved && array_size(resolved))
					array_push(input.target, resolved);
			}
		} else if (string_equal(STRING_ARGS(cmdline[arg]), STRING_CONST("--")))
			break;  // Stop parsing cmdline options
		else if ((cmdline[arg].length > 1) && (cmdline[arg].str[0] == '-'))
			continue;  // Cmdline argument not parsed here
		else
			array_push(input.files, string_clone(STRING_ARGS(cmdline[arg])));
	}
	error_context_pop();

	return input;
}

void
blast_print_usage(void) {
	log_info(HASH_BLAST,
	         STRING_CONST("blast usage:\n"
	                      "  blast [-s|--server] [-d|-daemon] [-c|--client] [-t|--target host[:port]] [-b|--bind "
	                      "host[:port]] <file> <file> <file> <...> [--]\n"
	                      "    Required arguments for server:\n"
	                      "      -s|--server              Start as server\n"
	                      "      -b||-bind host[:port]    Bind ip address and optional port (multiple)\n"
	                      "    Required arguments for client:\n"
	                      "      -c|--client              Start as client\n"
	                      "      -t|--target host[:port]  Target host (ip or hostname) with optional port (multiple)\n"
	                      "      <file>                   File name (muliple)\n"
	                      "    Optional arguments:\n"
	                      "      -d|--daemon              Run server as daemon\n"
	                      "      --                       Stop parsing command line options\n"));
}

void
blast_process_system_events(void) {
	event_block_t* block;
	event_t* event = 0;

	system_process_events();

	block = event_stream_process(system_event_stream());

	while ((event = event_next(block, event))) {
		switch (event->id) {
			case FOUNDATIONEVENT_TERMINATE:
				log_debug(HASH_BLAST, STRING_CONST("Terminating due to event"));
				should_exit = true;
				break;

			default:
				break;
		}
	}
}

bool
blast_should_exit(void) {
	return should_exit;
}
