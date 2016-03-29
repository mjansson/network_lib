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

typedef struct _test_datagram_arg {
	object_t              sock;
	network_address_t*    target;
} test_datagram_arg_t;

application_t
test_udp_application(void) {
	application_t app;
	memset(&app, 0, sizeof(app));
	app.name = string_const(STRING_CONST("Network UDP tests"));
	app.short_name = string_const(STRING_CONST("test_udp"));
	app.config_dir = string_const(STRING_CONST("test_udp"));
	app.flags = APPLICATION_UTILITY;
	return app;
}

memory_system_t
test_udp_memory_system(void) {
	return memory_system_malloc();
}

foundation_config_t
test_udp_foundation_config(void) {
	foundation_config_t config;
	memset(&config, 0, sizeof(config));
	return config;
}

int
test_udp_initialize(void) {
	network_config_t config;
	memset(&config, 0, sizeof(config));
	log_set_suppress(HASH_NETWORK, ERRORLEVEL_INFO);
	return network_module_initialize(config);
}

void
test_udp_finalize(void) {
	network_module_finalize();
}

static void*
stream_blocking_thread(void* arg) {
	int iloop;

	object_t sock = *(object_t*)arg;

	char buffer_out[317] = {0};
	char buffer_in[317] = {0};

	stream_t* stream = socket_stream(sock);

	for (iloop = 0; !thread_try_wait(0) && iloop < 512; ++iloop) {
		log_infof(HASH_NETWORK, STRING_CONST("UDP write pass %d"), iloop);
		EXPECT_EQ(stream_write(stream, buffer_out, 127), 127);
		EXPECT_EQ(stream_write(stream, buffer_out + 127, 180), 180);
		stream_flush(stream);
		EXPECT_EQ(stream_write(stream, buffer_out + 307, 10), 10);
		stream_flush(stream);
		log_infof(HASH_NETWORK, STRING_CONST("UDP read pass %d"), iloop);
		EXPECT_EQ(stream_read(stream, buffer_in, 235), 235);
		EXPECT_EQ(stream_read(stream, buffer_in + 235, 82), 82);
		thread_yield();
	}

	log_debugf(HASH_NETWORK, STRING_CONST("IO complete on socket 0x%llx"), sock);
	stream_deallocate(stream);

	return 0;
}

static void*
datagram_server_blocking_thread(void* arg) {
	int iloop;
	const network_address_t* from;
	object_t sock = *(object_t*)arg;
	network_datagram_t datagram;

	for (iloop = 0; !thread_try_wait(0) && iloop < 512 * 4; ++iloop) {
		log_infof(HASH_NETWORK, STRING_CONST("UDP mirror pass %d"), iloop);
		datagram = udp_socket_recvfrom(sock, &from);
		EXPECT_EQ(datagram.size, 973);
		EXPECT_EQ(udp_socket_sendto(sock, datagram, from), datagram.size);
		thread_yield();
	}

	log_infof(HASH_NETWORK, STRING_CONST("IO complete on socket 0x%llx"), sock);

	return 0;
}

static void*
datagram_client_blocking_thread(void* arg) {
	int iloop;

	test_datagram_arg_t* darg = arg;
	object_t sock = darg->sock;
	network_address_t* target = darg->target;
	const network_address_t* address;

	char buffer[1024] = {0};
	network_datagram_t datagram = { 973, buffer };

	log_debugf(HASH_NETWORK, STRING_CONST("IO start on socket 0x%llx"), sock);

	for (iloop = 0; !thread_try_wait(0) && iloop < 512; ++iloop) {
		log_infof(HASH_NETWORK, STRING_CONST("UDP read/write pass %d"), iloop);
		EXPECT_EQ(udp_socket_sendto(sock, datagram, target), datagram.size);
		datagram = udp_socket_recvfrom(sock, &address);
		EXPECT_EQ(datagram.size, 973);
		EXPECT_TRUE(network_address_equal(target, address));
		thread_yield();
	}

	log_infof(HASH_NETWORK, STRING_CONST("IO complete on socket 0x%llx"), sock);

	return 0;
}

DECLARE_TEST(udp, stream_ipv4) {
	network_address_t** address_local = 0;
	network_address_t* address = 0;

	int server_port, client_port;
	int state, iaddr, asize;
	thread_t threads[2];

	object_t sock_server;
	object_t sock_client;

	if (!network_supports_ipv4())
		return 0;

	sock_server = udp_socket_create();
	sock_client = udp_socket_create();

	EXPECT_TRUE(socket_is_socket(sock_server));
	EXPECT_TRUE(socket_is_socket(sock_client));

	address_local = network_address_local();
	for (iaddr = 0, asize = array_size(address_local); iaddr < asize; ++iaddr) {
		if (network_address_family(address_local[iaddr]) == NETWORK_ADDRESSFAMILY_IPV4) {
			address = address_local[iaddr];
			break;
		}
	}
	EXPECT_NE(address, 0);

	do {
		server_port = random32_range(1024, 35535);
		network_address_ip_set_port(address, server_port);
		if (socket_bind(sock_server, address))
			break;
	}
	while (true);

	do {
		client_port = random32_range(1024, 35535);
		network_address_ip_set_port(address, client_port);
		if (socket_bind(sock_client, address))
			break;
	}
	while (true);

	socket_set_blocking(sock_server, false);
	socket_set_blocking(sock_client, false);

	network_address_ip_set_port(address, client_port);
	socket_connect(sock_server, address, 0);

	network_address_ip_set_port(address, server_port);
	socket_connect(sock_client, address, 0);

	network_address_array_deallocate(address_local);

	state = socket_state(sock_server);
	EXPECT_TRUE(state == SOCKETSTATE_CONNECTED);

	state = socket_state(sock_client);
	EXPECT_TRUE(state == SOCKETSTATE_CONNECTED);

	socket_set_blocking(sock_server, true);
	socket_set_blocking(sock_client, true);

	thread_initialize(&threads[0], stream_blocking_thread, &sock_server, STRING_CONST("io_thread"),
	                  THREAD_PRIORITY_NORMAL, 0);
	thread_initialize(&threads[1], stream_blocking_thread, &sock_client, STRING_CONST("io_thread"),
	                  THREAD_PRIORITY_NORMAL, 0);

	thread_start(&threads[0]);
	thread_start(&threads[1]);

	test_wait_for_threads_startup(threads, 2);

	thread_finalize(&threads[0]);
	thread_finalize(&threads[1]);

	socket_destroy(sock_server);
	socket_destroy(sock_client);

	EXPECT_FALSE(socket_is_socket(sock_server));
	EXPECT_FALSE(socket_is_socket(sock_client));

	return 0;
}

DECLARE_TEST(udp, stream_ipv6) {
	network_address_t** address_local = 0;
	network_address_t* address = 0;

	int server_port, client_port;
	int state, iaddr, asize;
	thread_t threads[2];

	object_t sock_server;
	object_t sock_client;

	if (!network_supports_ipv6())
		return 0;

	sock_server = udp_socket_create();
	sock_client = udp_socket_create();

	EXPECT_TRUE(socket_is_socket(sock_server));
	EXPECT_TRUE(socket_is_socket(sock_client));

	address_local = network_address_local();
	for (iaddr = 0, asize = array_size(address_local); iaddr < asize; ++iaddr) {
		if (network_address_family(address_local[iaddr]) == NETWORK_ADDRESSFAMILY_IPV6) {
			address = address_local[iaddr];
			break;
		}
	}
	EXPECT_NE(address, 0);

	do {
		server_port = random32_range(1024, 35535);
		network_address_ip_set_port(address, server_port);
		if (socket_bind(sock_server, address))
			break;
	}
	while (true);

	do {
		client_port = random32_range(1024, 35535);
		network_address_ip_set_port(address, client_port);
		if (socket_bind(sock_client, address))
			break;
	}
	while (true);

	socket_set_blocking(sock_server, false);
	socket_set_blocking(sock_client, false);

	network_address_ip_set_port(address, client_port);
	socket_connect(sock_server, address, 0);

	network_address_ip_set_port(address, server_port);
	socket_connect(sock_client, address, 0);

	network_address_array_deallocate(address_local);

	state = socket_state(sock_server);
	EXPECT_TRUE(state == SOCKETSTATE_CONNECTED);

	state = socket_state(sock_client);
	EXPECT_TRUE(state == SOCKETSTATE_CONNECTED);

	socket_set_blocking(sock_server, true);
	socket_set_blocking(sock_client, true);

	thread_initialize(&threads[0], stream_blocking_thread, &sock_server, STRING_CONST("io_thread"),
	                  THREAD_PRIORITY_NORMAL, 0);
	thread_initialize(&threads[1], stream_blocking_thread, &sock_client, STRING_CONST("io_thread"),
	                  THREAD_PRIORITY_NORMAL, 0);

	thread_start(&threads[0]);
	thread_start(&threads[1]);

	test_wait_for_threads_startup(threads, 2);

	thread_finalize(&threads[0]);
	thread_finalize(&threads[1]);

	socket_destroy(sock_server);
	socket_destroy(sock_client);

	EXPECT_FALSE(socket_is_socket(sock_server));
	EXPECT_FALSE(socket_is_socket(sock_client));

	return 0;
}

DECLARE_TEST(udp, datagram_ipv4) {
	network_address_t** address_local = 0;
	network_address_t* address = 0;
	network_address_t* address_server = 0;
	test_datagram_arg_t client_arg[4];

	int server_port;
	int state, iaddr, asize;
	thread_t threads[5];

	object_t sock_server;
	object_t sock_client[4];

	if (!network_supports_ipv4())
		return 0;

	sock_server = udp_socket_create();
	sock_client[0] = udp_socket_create();
	sock_client[1] = udp_socket_create();
	sock_client[2] = udp_socket_create();
	sock_client[3] = udp_socket_create();

	EXPECT_TRUE(socket_is_socket(sock_server));
	EXPECT_TRUE(socket_is_socket(sock_client[0]));
	EXPECT_TRUE(socket_is_socket(sock_client[1]));
	EXPECT_TRUE(socket_is_socket(sock_client[2]));
	EXPECT_TRUE(socket_is_socket(sock_client[3]));

	address_local = network_address_local();
	for (iaddr = 0, asize = array_size(address_local); iaddr < asize; ++iaddr) {
		if (network_address_family(address_local[iaddr]) == NETWORK_ADDRESSFAMILY_IPV4) {
			address = address_local[iaddr];
			break;
		}
	}
	EXPECT_NE(address, 0);

	do {
		server_port = random32_range(1024, 35535);
		network_address_ip_set_port(address, server_port);
		if (socket_bind(sock_server, address))
			break;
	}
	while (true);

	address_server = network_address_clone(address);
	network_address_ip_set_port(address_server, server_port);

	network_address_array_deallocate(address_local);

	state = socket_state(sock_server);
	EXPECT_TRUE(state == SOCKETSTATE_NOTCONNECTED);

	state = socket_state(sock_client[0]);
	EXPECT_TRUE(state == SOCKETSTATE_NOTCONNECTED);

	state = socket_state(sock_client[1]);
	EXPECT_TRUE(state == SOCKETSTATE_NOTCONNECTED);

	state = socket_state(sock_client[2]);
	EXPECT_TRUE(state == SOCKETSTATE_NOTCONNECTED);

	state = socket_state(sock_client[3]);
	EXPECT_TRUE(state == SOCKETSTATE_NOTCONNECTED);

	socket_set_blocking(sock_server, true);
	socket_set_blocking(sock_client[0], true);
	socket_set_blocking(sock_client[1], true);
	socket_set_blocking(sock_client[2], true);
	socket_set_blocking(sock_client[3], true);

	client_arg[0].sock = sock_client[0]; client_arg[0].target = address_server;
	client_arg[1].sock = sock_client[1]; client_arg[1].target = address_server;
	client_arg[2].sock = sock_client[2]; client_arg[2].target = address_server;
	client_arg[3].sock = sock_client[3]; client_arg[3].target = address_server;

	thread_initialize(&threads[0], datagram_server_blocking_thread, &sock_server,
	                  STRING_CONST("server_thread"), THREAD_PRIORITY_NORMAL, 0);
	thread_initialize(&threads[1], datagram_client_blocking_thread, &client_arg[0],
	                  STRING_CONST("client_thread"), THREAD_PRIORITY_NORMAL, 0);
	thread_initialize(&threads[2], datagram_client_blocking_thread, &client_arg[1],
	                  STRING_CONST("client_thread"), THREAD_PRIORITY_NORMAL, 0);
	thread_initialize(&threads[3], datagram_client_blocking_thread, &client_arg[2],
	                  STRING_CONST("client_thread"), THREAD_PRIORITY_NORMAL, 0);
	thread_initialize(&threads[4], datagram_client_blocking_thread, &client_arg[3],
	                  STRING_CONST("client_thread"), THREAD_PRIORITY_NORMAL, 0);

	thread_start(&threads[0]);
	thread_start(&threads[1]);
	thread_start(&threads[2]);
	thread_start(&threads[3]);
	thread_start(&threads[4]);

	test_wait_for_threads_startup(threads, 5);

	thread_finalize(&threads[0]);
	thread_finalize(&threads[1]);
	thread_finalize(&threads[2]);
	thread_finalize(&threads[3]);
	thread_finalize(&threads[4]);

	socket_destroy(sock_server);
	socket_destroy(sock_client[0]);
	socket_destroy(sock_client[1]);
	socket_destroy(sock_client[2]);
	socket_destroy(sock_client[3]);

	EXPECT_FALSE(socket_is_socket(sock_server));
	EXPECT_FALSE(socket_is_socket(sock_client[0]));
	EXPECT_FALSE(socket_is_socket(sock_client[1]));
	EXPECT_FALSE(socket_is_socket(sock_client[2]));
	EXPECT_FALSE(socket_is_socket(sock_client[3]));

	memory_deallocate(address_server);

	return 0;
}

DECLARE_TEST(udp, datagram_ipv6) {
	network_address_t** address_local = 0;
	network_address_t* address = 0;
	network_address_t* address_server = 0;
	test_datagram_arg_t client_arg[4];

	int server_port;
	int state, iaddr, asize;
	thread_t threads[5];

	object_t sock_server;
	object_t sock_client[4];

	if (!network_supports_ipv6())
		return 0;

	sock_server = udp_socket_create();
	sock_client[0] = udp_socket_create();
	sock_client[1] = udp_socket_create();
	sock_client[2] = udp_socket_create();
	sock_client[3] = udp_socket_create();

	EXPECT_TRUE(socket_is_socket(sock_server));
	EXPECT_TRUE(socket_is_socket(sock_client[0]));
	EXPECT_TRUE(socket_is_socket(sock_client[1]));
	EXPECT_TRUE(socket_is_socket(sock_client[2]));
	EXPECT_TRUE(socket_is_socket(sock_client[3]));

	address_local = network_address_local();
	for (iaddr = 0, asize = array_size(address_local); iaddr < asize; ++iaddr) {
		if (network_address_family(address_local[iaddr]) == NETWORK_ADDRESSFAMILY_IPV6) {
			address = address_local[iaddr];
			break;
		}
	}
	EXPECT_NE(address, 0);

	do {
		server_port = random32_range(1024, 35535);
		network_address_ip_set_port(address, server_port);
		if (socket_bind(sock_server, address))
			break;
	}
	while (true);

	address_server = network_address_clone(address);
	network_address_ip_set_port(address_server, server_port);

	network_address_array_deallocate(address_local);

	state = socket_state(sock_server);
	EXPECT_TRUE(state == SOCKETSTATE_NOTCONNECTED);

	state = socket_state(sock_client[0]);
	EXPECT_TRUE(state == SOCKETSTATE_NOTCONNECTED);

	state = socket_state(sock_client[1]);
	EXPECT_TRUE(state == SOCKETSTATE_NOTCONNECTED);

	state = socket_state(sock_client[2]);
	EXPECT_TRUE(state == SOCKETSTATE_NOTCONNECTED);

	state = socket_state(sock_client[3]);
	EXPECT_TRUE(state == SOCKETSTATE_NOTCONNECTED);

	socket_set_blocking(sock_server, true);
	socket_set_blocking(sock_client[0], true);
	socket_set_blocking(sock_client[1], true);
	socket_set_blocking(sock_client[2], true);
	socket_set_blocking(sock_client[3], true);

	client_arg[0].sock = sock_client[0]; client_arg[0].target = address_server;
	client_arg[1].sock = sock_client[1]; client_arg[1].target = address_server;
	client_arg[2].sock = sock_client[2]; client_arg[2].target = address_server;
	client_arg[3].sock = sock_client[3]; client_arg[3].target = address_server;

	thread_initialize(&threads[0], datagram_server_blocking_thread, &sock_server,
	                  STRING_CONST("server_thread"), THREAD_PRIORITY_NORMAL, 0);
	thread_initialize(&threads[0], datagram_client_blocking_thread, &client_arg[0],
	                  STRING_CONST("client_thread"), THREAD_PRIORITY_NORMAL, 0);
	thread_initialize(&threads[0], datagram_client_blocking_thread, &client_arg[1],
	                  STRING_CONST("client_thread"), THREAD_PRIORITY_NORMAL, 0);
	thread_initialize(&threads[0], datagram_client_blocking_thread, &client_arg[2],
	                  STRING_CONST("client_thread"), THREAD_PRIORITY_NORMAL, 0);
	thread_initialize(&threads[0], datagram_client_blocking_thread, &client_arg[3],
	                  STRING_CONST("client_thread"), THREAD_PRIORITY_NORMAL, 0);

	thread_start(&threads[0]);
	thread_start(&threads[1]);
	thread_start(&threads[2]);
	thread_start(&threads[3]);
	thread_start(&threads[4]);

	test_wait_for_threads_startup(threads, 5);

	thread_finalize(&threads[0]);
	thread_finalize(&threads[1]);
	thread_finalize(&threads[2]);
	thread_finalize(&threads[3]);
	thread_finalize(&threads[4]);

	socket_destroy(sock_server);
	socket_destroy(sock_client[0]);
	socket_destroy(sock_client[1]);
	socket_destroy(sock_client[2]);
	socket_destroy(sock_client[3]);

	EXPECT_FALSE(socket_is_socket(sock_server));
	EXPECT_FALSE(socket_is_socket(sock_client[0]));
	EXPECT_FALSE(socket_is_socket(sock_client[1]));
	EXPECT_FALSE(socket_is_socket(sock_client[2]));
	EXPECT_FALSE(socket_is_socket(sock_client[3]));

	memory_deallocate(address_server);

	return 0;
}

void
test_udp_declare(void) {
	ADD_TEST(udp, stream_ipv4);
	ADD_TEST(udp, stream_ipv6);
	ADD_TEST(udp, datagram_ipv4);
	ADD_TEST(udp, datagram_ipv6);
}

test_suite_t test_udp_suite = {
	test_udp_application,
	test_udp_memory_system,
	test_udp_foundation_config,
	test_udp_declare,
	test_udp_initialize,
	test_udp_finalize
};

#if FOUNDATION_PLATFORM_ANDROID || FOUNDATION_PLATFORM_IOS

int
test_udp_run(void) {
	test_suite = test_udp_suite;
	return test_run_all();
}

#else

test_suite_t
test_suite_define(void) {
	return test_udp_suite;
}

#endif

