/* main.c  -  Network library  -  Public Domain  -  2013 Mattias Jansson
 *
 * This library provides a network abstraction built on foundation streams. The latest source code is
 * always available at
 *
 * https://github.com/mjansson/network_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <network/network.h>

#include <foundation/foundation.h>
#include <test/test.h>

static application_t
test_poll_application(void) {
	application_t app;
	memset(&app, 0, sizeof(app));
	app.name = string_const(STRING_CONST("Network poll tests"));
	app.short_name = string_const(STRING_CONST("test_poll"));
	app.company = string_const(STRING_CONST(""));
	app.flags = APPLICATION_UTILITY;
	app.exception_handler = test_exception_handler;
	return app;
}

static memory_system_t
test_poll_memory_system(void) {
	return memory_system_malloc();
}

static foundation_config_t
test_poll_foundation_config(void) {
	foundation_config_t config;
	memset(&config, 0, sizeof(config));
	return config;
}

static int
test_poll_initialize(void) {
	network_config_t config;
	memset(&config, 0, sizeof(config));
	log_set_suppress(HASH_NETWORK, ERRORLEVEL_INFO);
	return network_module_initialize(config);
}

static void
test_poll_finalize(void) {
	network_module_finalize();
}

DECLARE_TEST(poll, poll) {
	socket_t* sock_tcp[2] = {tcp_socket_allocate(), tcp_socket_allocate()};
	socket_t* sock_udp[2] = {udp_socket_allocate(), udp_socket_allocate()};

	network_poll_t* poll = network_poll_allocate(1024);

	network_address_t** local_address = network_address_local();
	socket_bind(sock_udp[0], local_address[0]);
	socket_bind(sock_udp[1], local_address[0]);
	network_address_array_deallocate(local_address);

	network_poll_add_socket(poll, sock_udp[0]);
	network_poll_add_socket(poll, sock_udp[1]);

	uint64_t data = HASH_NETWORK;
	udp_socket_sendto(sock_udp[0], &data, sizeof(data), socket_address_local(sock_udp[1]));

	network_poll_event_t event[64];
	size_t event_capacity = sizeof(event) / sizeof(event[0]);
	size_t event_count = network_poll(poll, event, event_capacity, NETWORK_TIMEOUT_INFINITE);
	EXPECT_EQ(event_count, 1);
	EXPECT_EQ(event[0].event, NETWORKEVENT_DATAIN);
	EXPECT_EQ(event[0].socket, sock_udp[1]);

	network_poll_deallocate(poll);

	socket_deallocate(sock_tcp[0]);
	socket_deallocate(sock_tcp[1]);
	socket_deallocate(sock_udp[0]);
	socket_deallocate(sock_udp[1]);
	return 0;
}

static void
test_poll_declare(void) {
	ADD_TEST(poll, poll);
}

static test_suite_t test_poll_suite = {test_poll_application,
                                       test_poll_memory_system,
                                       test_poll_foundation_config,
                                       test_poll_declare,
                                       test_poll_initialize,
                                       test_poll_finalize,
                                       0};

#if BUILD_MONOLITHIC

int
test_poll_run(void);

int
test_poll_run(void) {
	test_suite = test_poll_suite;
	return test_run_all();
}

#else

test_suite_t
test_suite_define(void);

test_suite_t
test_suite_define(void) {
	return test_poll_suite;
}

#endif
