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

static application_t
test_tcp_application(void) {
	application_t app;
	memset(&app, 0, sizeof(app));
	app.name = string_const(STRING_CONST("Network TCP/IP tests"));
	app.short_name = string_const(STRING_CONST("test_tcp"));
	app.company = string_const(STRING_CONST("Rampant Pixels"));
	app.flags = APPLICATION_UTILITY;
	app.exception_handler = test_exception_handler;
	return app;
}

static memory_system_t
test_tcp_memory_system(void) {
	return memory_system_malloc();
}

static foundation_config_t
test_tcp_foundation_config(void) {
	foundation_config_t config;
	memset(&config, 0, sizeof(config));
	return config;
}

static int
test_tcp_initialize(void) {
	network_config_t config;
	memset(&config, 0, sizeof(config));
	log_set_suppress(HASH_NETWORK, ERRORLEVEL_INFO);
	return network_module_initialize(config);
}

static void
test_tcp_finalize(void) {
	network_module_finalize();
}

static atomic32_t io_completed;

static void*
io_blocking_thread(void* arg) {
	int iloop;

	socket_t* sock = (socket_t*)arg;

	tcp_socket_set_delay(sock, false);

	char buffer_out[317] = {0};
	char buffer_in[317] = {0};

	for (iloop = 0; iloop < 512; ++iloop) {
		EXPECT_EQ(socket_write(sock, buffer_out, 317), 317);
		EXPECT_EQ(socket_read(sock, buffer_in, 317), 317);
		thread_yield();
	}

	log_debugf(HASH_NETWORK, STRING_CONST("IO complete on socket 0x%" PRIfixPTR), (uintptr_t)sock);

	atomic_incr32(&io_completed);

	return 0;
}

static void*
stream_blocking_thread(void* arg) {
	int iloop;

	socket_t* sock = (socket_t*)arg;

	tcp_socket_set_delay(sock, false);

	char buffer_out[317] = {0};
	char buffer_in[317] = {0};

	stream_t* stream = socket_stream_allocate(sock, 17, 31);

	for (iloop = 0; iloop < 512; ++iloop) {
		EXPECT_EQ(stream_write(stream, buffer_out, 97), 97);
		stream_flush(stream);
		EXPECT_EQ(stream_read(stream, buffer_in, 63), 63);
		EXPECT_EQ(stream_write(stream, buffer_out, 120), 120);
		stream_flush(stream);
		stream_seek(stream, 59, STREAM_SEEK_CURRENT);
		EXPECT_EQ(stream_read(stream, buffer_in, 42), 42);
		EXPECT_EQ(stream_write(stream, buffer_out, 215), 215);
		EXPECT_EQ(stream_write(stream, buffer_out, 1), 1); //433 bytes written
		stream_flush(stream);
		EXPECT_EQ(stream_read(stream, buffer_in, 51), 51);
		EXPECT_EQ(stream_read(stream, buffer_in, 218), 218); //433 bytes read
		thread_yield();
	}

	log_debugf(HASH_NETWORK, STRING_CONST("Stream complete on socket 0x%" PRIfixPTR), (uintptr_t)sock);

	stream_deallocate(stream);

	atomic_incr32(&io_completed);

	return 0;
}

DECLARE_TEST(tcp, connect_ipv4) {
	unsigned int iaddr;
	bool success;
	network_address_t** addresses;
	socket_t* sock_client;
	tick_t start;

	if (!network_supports_ipv4())
		return 0;

	//Blocking with timeout
	sock_client = tcp_socket_allocate();

	socket_set_blocking(sock_client, true);

	success = false;
	addresses = network_address_resolve(STRING_CONST("www.rampantpixels.com:80"));
	start = time_current();
	for (iaddr = 0; !success && iaddr < array_size(addresses); ++iaddr) {
		if (network_address_family(addresses[iaddr]) != NETWORK_ADDRESSFAMILY_IPV4)
			continue;

		success = socket_connect(sock_client, addresses[iaddr], 2000);
		break;
	}
	EXPECT_REALLE(time_elapsed(start), REAL_C(2.5));
	EXPECT_EQ(socket_state(sock_client), success ? SOCKETSTATE_CONNECTED : SOCKETSTATE_NOTCONNECTED);

	network_address_array_deallocate(addresses);
	socket_deallocate(sock_client);

	//Blocking with zero timeout
	success = false;
	sock_client = tcp_socket_allocate();

	socket_set_blocking(sock_client, true);

	success = false;
	addresses = network_address_resolve(STRING_CONST("www.rampantpixels.com:80"));
	for (iaddr = 0; iaddr < array_size(addresses); ++iaddr) {
		if (network_address_family(addresses[iaddr]) != NETWORK_ADDRESSFAMILY_IPV4)
			continue;

		success = socket_connect(sock_client, addresses[iaddr], 0);
		break;
	}
	network_address_array_deallocate(addresses);

	if (success) {
		EXPECT_TRUE((socket_state(sock_client) == SOCKETSTATE_CONNECTING) ||
		            (socket_state(sock_client) == SOCKETSTATE_CONNECTED));
	}
	else {
		EXPECT_EQ(socket_state(sock_client), SOCKETSTATE_NOTCONNECTED);
	}

	socket_deallocate(sock_client);

	//Blocking without timeout
	success = false;
	sock_client = tcp_socket_allocate();

	socket_set_blocking(sock_client, true);

	success = false;
	addresses = network_address_resolve(STRING_CONST("www.rampantpixels.com:80"));
	for (iaddr = 0; iaddr < array_size(addresses); ++iaddr) {
		if (network_address_family(addresses[iaddr]) != NETWORK_ADDRESSFAMILY_IPV4)
			continue;

		success = socket_connect(sock_client, addresses[iaddr], NETWORK_TIMEOUT_INFINITE);
		break;
	}
	network_address_array_deallocate(addresses);

	EXPECT_EQ(socket_state(sock_client), success ? SOCKETSTATE_CONNECTED : SOCKETSTATE_NOTCONNECTED);

	socket_deallocate(sock_client);

	//Unblocking with timeout
	sock_client = tcp_socket_allocate();

	socket_set_blocking(sock_client, false);

	success = false;
	addresses = network_address_resolve(STRING_CONST("www.rampantpixels.com:80"));
	start = time_current();
	for (iaddr = 0; iaddr < array_size(addresses); ++iaddr) {
		if (network_address_family(addresses[iaddr]) != NETWORK_ADDRESSFAMILY_IPV4)
			continue;

		success = socket_connect(sock_client, addresses[iaddr], 2000);
		break;
	}
	EXPECT_REALLE(time_elapsed(start), REAL_C(2.5));
	EXPECT_EQ(socket_state(sock_client), success ? SOCKETSTATE_CONNECTED : SOCKETSTATE_NOTCONNECTED);

	network_address_array_deallocate(addresses);
	socket_deallocate(sock_client);

	//Unblocking with zero timeout
	success = false;
	sock_client = tcp_socket_allocate();

	socket_set_blocking(sock_client, false);

	success = false;
	addresses = network_address_resolve(STRING_CONST("www.rampantpixels.com:80"));
	for (iaddr = 0; iaddr < array_size(addresses); ++iaddr) {
		if (network_address_family(addresses[iaddr]) != NETWORK_ADDRESSFAMILY_IPV4)
			continue;

		success = socket_connect(sock_client, addresses[iaddr], 0);
		break;
	}
	network_address_array_deallocate(addresses);

	if (success) {
		EXPECT_TRUE((socket_state(sock_client) == SOCKETSTATE_CONNECTING) ||
		            (socket_state(sock_client) == SOCKETSTATE_CONNECTED));
	}
	else {
		EXPECT_EQ(socket_state(sock_client), SOCKETSTATE_NOTCONNECTED);
	}

	socket_deallocate(sock_client);

	//Unblocking without timeout
	success = false;
	sock_client = tcp_socket_allocate();

	socket_set_blocking(sock_client, false);

	success = false;
	addresses = network_address_resolve(STRING_CONST("www.rampantpixels.com:80"));
	for (iaddr = 0; iaddr < array_size(addresses); ++iaddr) {
		if (network_address_family(addresses[iaddr]) != NETWORK_ADDRESSFAMILY_IPV4)
			continue;

		success = socket_connect(sock_client, addresses[iaddr], NETWORK_TIMEOUT_INFINITE);
		break;
	}
	network_address_array_deallocate(addresses);

	EXPECT_EQ(socket_state(sock_client), success ? SOCKETSTATE_CONNECTED : SOCKETSTATE_NOTCONNECTED);

	socket_deallocate(sock_client);

	return 0;
}

DECLARE_TEST(tcp, connect_ipv6) {
	unsigned int iaddr;
	bool success;
	socket_t* sock_client;
	network_address_t** addresses;
	tick_t start;

	if (!network_supports_ipv6())
		return 0;

	//Blocking with timeout
	sock_client = tcp_socket_allocate();

	socket_set_blocking(sock_client, true);

	success = false;
	addresses = network_address_resolve(STRING_CONST("www.rampantpixels.com:80"));
	start = time_current();
	for (iaddr = 0; iaddr < array_size(addresses); ++iaddr) {
		if (network_address_family(addresses[iaddr]) != NETWORK_ADDRESSFAMILY_IPV6)
			continue;

		success = socket_connect(sock_client, addresses[iaddr], 2000);
		break;
	}
	EXPECT_REALLE(time_elapsed(start), REAL_C(2.5));
	EXPECT_EQ(socket_state(sock_client), success ? SOCKETSTATE_CONNECTED : SOCKETSTATE_NOTCONNECTED);

	network_address_array_deallocate(addresses);
	socket_deallocate(sock_client);

	//Blocking without timeout
	success = false;
	sock_client = tcp_socket_allocate();

	socket_set_blocking(sock_client, true);

	success = false;
	addresses = network_address_resolve(STRING_CONST("www.rampantpixels.com:80"));
	for (iaddr = 0; iaddr < array_size(addresses); ++iaddr) {
		if (network_address_family(addresses[iaddr]) != NETWORK_ADDRESSFAMILY_IPV6)
			continue;

		success = socket_connect(sock_client, addresses[iaddr], 0);
		break;
	}
	network_address_array_deallocate(addresses);

	EXPECT_EQ(socket_state(sock_client), success ? SOCKETSTATE_CONNECTED : SOCKETSTATE_NOTCONNECTED);

	socket_deallocate(sock_client);

	//Unblocking with timeout
	sock_client = tcp_socket_allocate();

	socket_set_blocking(sock_client, false);

	success = false;
	addresses = network_address_resolve(STRING_CONST("www.rampantpixels.com:80"));
	start = time_current();
	for (iaddr = 0; iaddr < array_size(addresses); ++iaddr) {
		if (network_address_family(addresses[iaddr]) != NETWORK_ADDRESSFAMILY_IPV6)
			continue;

		success = socket_connect(sock_client, addresses[iaddr], 5000);
		break;
	}
	EXPECT_REALLE(time_elapsed(start), REAL_C(2.5));
	EXPECT_EQ(socket_state(sock_client), success ? SOCKETSTATE_CONNECTED : SOCKETSTATE_NOTCONNECTED);

	network_address_array_deallocate(addresses);
	socket_deallocate(sock_client);

	//Unblocking without timeout
	sock_client = tcp_socket_allocate();

	socket_set_blocking(sock_client, false);

	success = false;
	addresses = network_address_resolve(STRING_CONST("www.rampantpixels.com:80"));
	for (iaddr = 0; iaddr < array_size(addresses); ++iaddr) {
		if (network_address_family(addresses[iaddr]) != NETWORK_ADDRESSFAMILY_IPV6)
			continue;

		success = socket_connect(sock_client, addresses[iaddr], 0);
		break;
	}
	network_address_array_deallocate(addresses);

	if (success) {
		EXPECT_TRUE((socket_state(sock_client) == SOCKETSTATE_CONNECTING) ||
		            (socket_state(sock_client) == SOCKETSTATE_CONNECTED));
	}
	else {
		EXPECT_EQ(socket_state(sock_client), SOCKETSTATE_NOTCONNECTED);
	}

	socket_deallocate(sock_client);

	return 0;
}

DECLARE_TEST(tcp, io_ipv4) {
	network_address_t* address_bind = 0;
	network_address_t** address_local = 0;
	network_address_t* address_connect = 0;

	unsigned int state, iaddr, asize;
	thread_t threads[2];

	socket_t* sock_listen = 0;
	socket_t* sock_server = 0;
	socket_t* sock_client = 0;

	if (!network_supports_ipv4())
		return 0;

	sock_listen = tcp_socket_allocate();
	sock_client = tcp_socket_allocate();

	network_address_ipv4_t any;
	network_address_ipv4_initialize(&any);
	address_bind = (network_address_t*)&any;
	socket_bind(sock_listen, address_bind);

	tcp_socket_listen(sock_listen);
	EXPECT_EQ(socket_state(sock_listen), SOCKETSTATE_LISTENING);

	address_local = network_address_local();
	address_connect = 0;
	for (iaddr = 0, asize = array_size(address_local); iaddr < asize; ++iaddr) {
		if (network_address_family(address_local[iaddr]) == NETWORK_ADDRESSFAMILY_IPV4) {
			address_connect = address_local[iaddr];
			break;
		}
	}
	EXPECT_NE(address_connect, 0);
	network_address_ip_set_port(address_connect,
	                            network_address_ip_port(socket_address_local(sock_listen)));
	socket_set_blocking(sock_client, false);
	socket_connect(sock_client, address_connect, 0);
	state = socket_state(sock_client);
	EXPECT_TRUE((state == SOCKETSTATE_CONNECTING) || (state == SOCKETSTATE_CONNECTED));

	network_address_array_deallocate(address_local);
	thread_sleep(100);

	sock_server = tcp_socket_accept(sock_listen, 0);

	int client_state = socket_poll_state(sock_client);
	int server_state = socket_poll_state(sock_server);
	EXPECT_INTEQ(client_state, SOCKETSTATE_CONNECTED);
	EXPECT_INTEQ(server_state, SOCKETSTATE_CONNECTED);

	socket_deallocate(sock_listen);

	socket_set_blocking(sock_client, true);
	socket_set_blocking(sock_server, true);

	atomic_store32(&io_completed, 0);

	thread_initialize(&threads[0], io_blocking_thread, sock_server, STRING_CONST("io_thread"),
	                  THREAD_PRIORITY_NORMAL, 0);
	thread_initialize(&threads[1], io_blocking_thread, sock_client, STRING_CONST("io_thread"),
	                  THREAD_PRIORITY_NORMAL, 0);

	thread_start(&threads[0]);
	thread_start(&threads[1]);

	test_wait_for_threads_startup(threads, 2);

	thread_finalize(&threads[0]);
	thread_finalize(&threads[1]);

	socket_deallocate(sock_server);
	socket_deallocate(sock_client);

	EXPECT_EQ(atomic_load32(&io_completed), 2);

	return 0;
}

DECLARE_TEST(tcp, io_ipv6) {
	network_address_t* address_bind = 0;
	network_address_t** address_local = 0;
	network_address_t* address_connect = 0;

	unsigned int state, iaddr, asize;
	thread_t threads[2];

	socket_t* sock_listen = 0;
	socket_t* sock_server = 0;
	socket_t* sock_client = 0;

	if (!network_supports_ipv6())
		return 0;

	sock_listen = tcp_socket_allocate();
	sock_client = tcp_socket_allocate();

	network_address_ipv6_t any;
	network_address_ipv6_initialize(&any);
	address_bind = (network_address_t*)&any;
	socket_bind(sock_listen, address_bind);

	tcp_socket_listen(sock_listen);
	EXPECT_EQ(socket_state(sock_listen), SOCKETSTATE_LISTENING);

	address_local = network_address_local();
	address_connect = 0;
	for (iaddr = 0, asize = array_size(address_local); iaddr < asize; ++iaddr) {
		if (network_address_family(address_local[iaddr]) == NETWORK_ADDRESSFAMILY_IPV6) {
			address_connect = address_local[iaddr];
			break;
		}
	}
	EXPECT_NE(address_connect, 0);
	network_address_ip_set_port(address_connect,
	                            network_address_ip_port(socket_address_local(sock_listen)));
	socket_set_blocking(sock_client, false);
	socket_connect(sock_client, address_connect, 0);
	state = socket_state(sock_client);
	EXPECT_TRUE((state == SOCKETSTATE_CONNECTING) || (state == SOCKETSTATE_CONNECTED));

	network_address_array_deallocate(address_local);
	thread_sleep(100);

	sock_server = tcp_socket_accept(sock_listen, 0);
	EXPECT_EQ(socket_poll_state(sock_client), SOCKETSTATE_CONNECTED);
	EXPECT_EQ(socket_poll_state(sock_server), SOCKETSTATE_CONNECTED);

	socket_deallocate(sock_listen);

	socket_set_blocking(sock_client, true);
	socket_set_blocking(sock_server, true);

	atomic_store32(&io_completed, 0);

	thread_initialize(&threads[0], io_blocking_thread, sock_server, STRING_CONST("io_thread"),
	                  THREAD_PRIORITY_NORMAL, 0);
	thread_initialize(&threads[1], io_blocking_thread, sock_client, STRING_CONST("io_thread"),
	                  THREAD_PRIORITY_NORMAL, 0);

	thread_start(&threads[0]);
	thread_start(&threads[1]);

	test_wait_for_threads_startup(threads, 2);

	thread_finalize(&threads[0]);
	thread_finalize(&threads[1]);

	socket_deallocate(sock_server);
	socket_deallocate(sock_client);

	EXPECT_EQ(atomic_load32(&io_completed), 2);

	return 0;
}

DECLARE_TEST(tcp, stream_ipv4) {
	network_address_t* address_bind = 0;
	network_address_t** address_local = 0;
	network_address_t* address_connect = 0;

	unsigned int state, iaddr, asize;
	thread_t threads[2];

	socket_t* sock_listen = 0;
	socket_t* sock_server = 0;
	socket_t* sock_client = 0;

	if (!network_supports_ipv4())
		return 0;

	sock_listen = tcp_socket_allocate();
	sock_client = tcp_socket_allocate();

	network_address_ipv4_t any;
	network_address_ipv4_initialize(&any);
	address_bind = (network_address_t*)&any;
	socket_bind(sock_listen, address_bind);

	tcp_socket_listen(sock_listen);
	EXPECT_EQ(socket_state(sock_listen), SOCKETSTATE_LISTENING);

	address_local = network_address_local();
	address_connect = 0;
	for (iaddr = 0, asize = array_size(address_local); iaddr < asize; ++iaddr) {
		if (network_address_family(address_local[iaddr]) == NETWORK_ADDRESSFAMILY_IPV4) {
			address_connect = address_local[iaddr];
			break;
		}
	}
	EXPECT_NE(address_connect, 0);
	network_address_ip_set_port(address_connect,
	                            network_address_ip_port(socket_address_local(sock_listen)));
	socket_set_blocking(sock_client, false);
	socket_connect(sock_client, address_connect, 0);
	state = socket_state(sock_client);
	EXPECT_TRUE((state == SOCKETSTATE_CONNECTING) || (state == SOCKETSTATE_CONNECTED));

	network_address_array_deallocate(address_local);
	thread_sleep(100);

	sock_server = tcp_socket_accept(sock_listen, 0);
	EXPECT_EQ(socket_poll_state(sock_client), SOCKETSTATE_CONNECTED);
	EXPECT_EQ(socket_poll_state(sock_server), SOCKETSTATE_CONNECTED);

	socket_deallocate(sock_listen);

	socket_set_blocking(sock_client, true);
	socket_set_blocking(sock_server, true);

	atomic_store32(&io_completed, 0);

	thread_initialize(&threads[0], stream_blocking_thread, sock_server, STRING_CONST("stream_thread"),
	                  THREAD_PRIORITY_NORMAL, 0);
	thread_initialize(&threads[1], stream_blocking_thread, sock_client, STRING_CONST("stream_thread"),
	                  THREAD_PRIORITY_NORMAL, 0);

	thread_start(&threads[0]);
	thread_start(&threads[1]);

	test_wait_for_threads_startup(threads, 2);

	thread_finalize(&threads[0]);
	thread_finalize(&threads[1]);

	socket_deallocate(sock_server);
	socket_deallocate(sock_client);

	EXPECT_EQ(atomic_load32(&io_completed), 2);

	return 0;
}

DECLARE_TEST(tcp, stream_ipv6) {
	network_address_t* address_bind = 0;
	network_address_t** address_local = 0;
	network_address_t* address_connect = 0;

	unsigned int state, iaddr, asize;
	thread_t threads[2];

	socket_t* sock_listen = 0;
	socket_t* sock_server = 0;
	socket_t* sock_client = 0;

	if (!network_supports_ipv6())
		return 0;

	sock_listen = tcp_socket_allocate();
	sock_client = tcp_socket_allocate();

	network_address_ipv6_t any;
	network_address_ipv6_initialize(&any);
	address_bind = (network_address_t*)&any;
	socket_bind(sock_listen, address_bind);

	tcp_socket_listen(sock_listen);
	EXPECT_EQ(socket_state(sock_listen), SOCKETSTATE_LISTENING);

	address_local = network_address_local();
	address_connect = 0;
	for (iaddr = 0, asize = array_size(address_local); iaddr < asize; ++iaddr) {
		if (network_address_family(address_local[iaddr]) == NETWORK_ADDRESSFAMILY_IPV6) {
			address_connect = address_local[iaddr];
			break;
		}
	}
	EXPECT_NE(address_connect, 0);
	network_address_ip_set_port(address_connect,
	                            network_address_ip_port(socket_address_local(sock_listen)));
	socket_set_blocking(sock_client, false);
	socket_connect(sock_client, address_connect, 0);
	state = socket_state(sock_client);
	EXPECT_TRUE((state == SOCKETSTATE_CONNECTING) || (state == SOCKETSTATE_CONNECTED));

	network_address_array_deallocate(address_local);
	thread_sleep(100);

	sock_server = tcp_socket_accept(sock_listen, 0);
	EXPECT_EQ(socket_poll_state(sock_client), SOCKETSTATE_CONNECTED);
	EXPECT_EQ(socket_poll_state(sock_server), SOCKETSTATE_CONNECTED);

	socket_deallocate(sock_listen);

	socket_set_blocking(sock_client, true);
	socket_set_blocking(sock_server, true);

	atomic_store32(&io_completed, 0);

	thread_initialize(&threads[0], stream_blocking_thread, sock_server, STRING_CONST("stream_thread"),
	                  THREAD_PRIORITY_NORMAL, 0);
	thread_initialize(&threads[1], stream_blocking_thread, sock_client, STRING_CONST("stream_thread"),
	                  THREAD_PRIORITY_NORMAL, 0);

	thread_start(&threads[0]);
	thread_start(&threads[1]);

	test_wait_for_threads_startup(threads, 2);

	thread_finalize(&threads[0]);
	thread_finalize(&threads[1]);

	socket_deallocate(sock_server);
	socket_deallocate(sock_client);

	EXPECT_EQ(atomic_load32(&io_completed), 2);

	return 0;
}

static void
test_tcp_declare(void) {
	ADD_TEST(tcp, connect_ipv4);
	ADD_TEST(tcp, connect_ipv6);
	ADD_TEST(tcp, io_ipv4);
	ADD_TEST(tcp, io_ipv6);
	ADD_TEST(tcp, stream_ipv4);
	ADD_TEST(tcp, stream_ipv6);
}

static test_suite_t test_tcp_suite = {
	test_tcp_application,
	test_tcp_memory_system,
	test_tcp_foundation_config,
	test_tcp_declare,
	test_tcp_initialize,
	test_tcp_finalize,
	0
};

#if BUILD_MONOLITHIC

int
test_tcp_run(void);

int
test_tcp_run(void) {
	test_suite = test_tcp_suite;
	return test_run_all();
}

#else

test_suite_t
test_suite_define(void);

test_suite_t
test_suite_define(void) {
	return test_tcp_suite;
}

#endif

