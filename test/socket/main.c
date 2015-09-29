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

application_t
test_socket_application(void) {
	application_t app;
	memset(&app, 0, sizeof(app));
	app.name = string_const(STRING_CONST("Network socket tests"));
	app.short_name = string_const(STRING_CONST("test_socket"));
	app.config_dir = string_const(STRING_CONST("test_socket"));
	app.flags = APPLICATION_UTILITY;
	return app;
}

memory_system_t
test_socket_memory_system(void) {
	return memory_system_malloc();
}

foundation_config_t 
test_socket_foundation_config(void) {
	foundation_config_t config;
	memset(&config, 0, sizeof(config));
	return config;
}

int
test_socket_initialize(void) {
	network_config_t config;
	memset(&config, 0, sizeof(config));
	log_set_suppress(HASH_NETWORK, ERRORLEVEL_INFO);
	return network_module_initialize(config);
}

void
test_socket_finalize(void) {
	network_module_finalize();
}

DECLARE_TEST(tcp, create) {
	object_t sock = tcp_socket_create();

	EXPECT_TRUE(socket_is_socket(sock));

	socket_destroy(sock);

	EXPECT_FALSE(socket_is_socket(sock));

	return 0;
}

DECLARE_TEST(tcp, blocking) {
	object_t sock = tcp_socket_create();

	EXPECT_TRUE(socket_is_socket(sock));

	socket_set_blocking(sock, false);
	EXPECT_FALSE(socket_blocking(sock));

	socket_set_blocking(sock, true);
	EXPECT_TRUE(socket_blocking(sock));

	socket_destroy(sock);

	EXPECT_FALSE(socket_is_socket(sock));

	return 0;
}

DECLARE_TEST(tcp, bind) {
	bool has_ipv4 = network_supports_ipv4();
	bool has_ipv6 = network_supports_ipv6();
	bool was_bound = false;
	int port;

	if (has_ipv4) {
		object_t sock = tcp_socket_create();

		EXPECT_TRUE(socket_is_socket(sock));

		EXPECT_EQ(socket_address_local(sock), 0);
		EXPECT_EQ(socket_address_remote(sock), 0);
		EXPECT_EQ(socket_state(sock), SOCKETSTATE_NOTCONNECTED);

		for (port = 31890; !was_bound && (port < 32890); ++port) {
			network_address_t* address = network_address_ipv4_any();
			network_address_ip_set_port(address, port);

			if (socket_bind(sock, address)) {
				EXPECT_NE(socket_address_local(sock), 0);
				EXPECT_EQ(socket_address_remote(sock), 0);
				EXPECT_EQ(socket_state(sock), SOCKETSTATE_NOTCONNECTED);

				EXPECT_TRUE(network_address_equal(socket_address_local(sock), address));

				was_bound = true;
			}

			memory_deallocate(address);
		}
		EXPECT_TRUE(was_bound);

		socket_destroy(sock);

		EXPECT_FALSE(socket_is_socket(sock));
	}

	if (has_ipv6) {
		object_t sock = tcp_socket_create();

		EXPECT_TRUE(socket_is_socket(sock));

		was_bound = false;
		for (port = 31890; !was_bound && (port < 32890); ++port) {
			network_address_t* address = network_address_ipv6_any();
			network_address_ip_set_port(address, port);

			if (socket_bind(sock, address)) {
				EXPECT_NE(socket_address_local(sock), 0);
				EXPECT_EQ(socket_address_remote(sock), 0);
				EXPECT_EQ(socket_state(sock), SOCKETSTATE_NOTCONNECTED);

				EXPECT_TRUE(network_address_equal(socket_address_local(sock), address));

				was_bound = true;
			}

			memory_deallocate(address);
		}
		EXPECT_TRUE(was_bound);

		socket_destroy(sock);

		EXPECT_FALSE(socket_is_socket(sock));
	}

	return 0;
}

DECLARE_TEST(udp, create) {
	object_t sock = udp_socket_create();

	EXPECT_TRUE(socket_is_socket(sock));

	socket_destroy(sock);

	EXPECT_FALSE(socket_is_socket(sock));

	return 0;
}

DECLARE_TEST(udp, blocking) {
	object_t sock = udp_socket_create();

	EXPECT_TRUE(socket_is_socket(sock));

	socket_set_blocking(sock, false);
	EXPECT_FALSE(socket_blocking(sock));

	socket_set_blocking(sock, true);
	EXPECT_TRUE(socket_blocking(sock));

	socket_destroy(sock);

	EXPECT_FALSE(socket_is_socket(sock));

	return 0;
}

DECLARE_TEST(udp, bind) {
	bool has_ipv4 = network_supports_ipv4();
	bool has_ipv6 = network_supports_ipv6();
	bool was_bound = false;
	int port;

	if (has_ipv4) {
		object_t sock = udp_socket_create();

		EXPECT_TRUE(socket_is_socket(sock));

		EXPECT_EQ(socket_address_local(sock), 0);
		EXPECT_EQ(socket_address_remote(sock), 0);
		EXPECT_EQ(socket_state(sock), SOCKETSTATE_NOTCONNECTED);

		for (port = 31890; !was_bound && (port < 32890); ++port) {
			network_address_t* address = network_address_ipv4_any();
			network_address_ip_set_port(address, port);

			if (socket_bind(sock, address)) {
				EXPECT_NE(socket_address_local(sock), 0);
				EXPECT_EQ(socket_address_remote(sock), 0);
				EXPECT_EQ(socket_state(sock), SOCKETSTATE_NOTCONNECTED);

				EXPECT_TRUE(network_address_equal(socket_address_local(sock), address));

				was_bound = true;
			}

			memory_deallocate(address);
		}
		EXPECT_TRUE(was_bound);

		socket_destroy(sock);

		EXPECT_FALSE(socket_is_socket(sock));
	}

	if (has_ipv6) {
		object_t sock = udp_socket_create();

		EXPECT_TRUE(socket_is_socket(sock));

		was_bound = false;
		for (port = 31890; !was_bound && (port < 32890); ++port) {
			network_address_t* address = network_address_ipv6_any();
			network_address_ip_set_port(address, port);

			if (socket_bind(sock, address)) {
				EXPECT_NE(socket_address_local(sock), 0);
				EXPECT_EQ(socket_address_remote(sock), 0);
				EXPECT_EQ(socket_state(sock), SOCKETSTATE_NOTCONNECTED);

				EXPECT_TRUE(network_address_equal(socket_address_local(sock), address));

				was_bound = true;
			}

			memory_deallocate(address);
		}
		EXPECT_TRUE(was_bound);

		socket_destroy(sock);

		EXPECT_FALSE(socket_is_socket(sock));
	}

	return 0;
}

void
test_socket_declare(void) {
	ADD_TEST(tcp, create);
	ADD_TEST(tcp, blocking);
	ADD_TEST(tcp, bind);

	ADD_TEST(udp, create);
	ADD_TEST(udp, blocking);
	ADD_TEST(udp, bind);
}

test_suite_t test_socket_suite = {
	test_socket_application,
	test_socket_memory_system,
	test_socket_foundation_config,
	test_socket_declare,
	test_socket_initialize,
	test_socket_finalize
};

#if FOUNDATION_PLATFORM_ANDROID || FOUNDATION_PLATFORM_IOS

int
test_socket_run(void) {
	test_suite = test_socket_suite;
	return test_run_all();
}

#else

test_suite_t
test_suite_define(void) {
	return test_socket_suite;
}

#endif

